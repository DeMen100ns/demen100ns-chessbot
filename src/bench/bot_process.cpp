#include "bench/bot_process.h"

#include "chess/io.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

FILE* to_file(void* handle) {
    return static_cast<FILE*>(handle);
}

}  // namespace

BotProcess::BotProcess(Config config_in)
    : config(std::move(config_in)) {}

BotProcess::~BotProcess() {
    stop();
}

bool BotProcess::is_running() const {
    return child_pid > 0;
}

bool BotProcess::start(std::string* error) {
    if (is_running()) {
        return true;
    }

    if (config.executable_path.empty()) {
        if (error != nullptr) {
            *error = "Bot executable path is empty";
        }
        return false;
    }

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        if (error != nullptr) {
            *error = std::string("Failed to create pipes: ") + std::strerror(errno);
        }
        if (to_child[0] >= 0) close(to_child[0]);
        if (to_child[1] >= 0) close(to_child[1]);
        if (from_child[0] >= 0) close(from_child[0]);
        if (from_child[1] >= 0) close(from_child[1]);
        return false;
    }

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, from_child[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, from_child[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, to_child[1]);
    posix_spawn_file_actions_addclose(&file_actions, from_child[0]);
#if defined(__APPLE__)
    if (!config.working_directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&file_actions, config.working_directory.c_str());
    }
#endif

    std::vector<char*> argv;
    argv.reserve(config.args.size() + 2);
    argv.push_back(const_cast<char*>(config.executable_path.c_str()));
    for (const std::string& arg : config.args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawn_result = posix_spawn(&pid,
                                         config.executable_path.c_str(),
                                         &file_actions,
                                         nullptr,
                                         argv.data(),
                                         environ);
    posix_spawn_file_actions_destroy(&file_actions);
    if (spawn_result != 0) {
        if (error != nullptr) {
            *error = std::string("posix_spawn failed: ") + std::strerror(spawn_result);
        }
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return false;
    }

    close(to_child[0]);
    close(from_child[1]);

    child_pid = static_cast<int>(pid);
    write_fd = to_child[1];
    read_fd = from_child[0];
    write_file = fdopen(write_fd, "w");
    read_file = fdopen(read_fd, "r");
    if (write_file == nullptr || read_file == nullptr) {
        if (error != nullptr) {
            *error = std::string("fdopen failed: ") + std::strerror(errno);
        }
        stop();
        return false;
    }

    setvbuf(to_file(write_file), nullptr, _IOLBF, 0);

    return ping(error);
}

void BotProcess::stop() {
    if (is_running()) {
        std::string ignored;
        send_line("quit", &ignored);
    }

    if (write_file != nullptr) {
        fclose(to_file(write_file));
        write_file = nullptr;
    } else if (write_fd >= 0) {
        close(write_fd);
    }
    write_fd = -1;

    if (read_file != nullptr) {
        fclose(to_file(read_file));
        read_file = nullptr;
    } else if (read_fd >= 0) {
        close(read_fd);
    }
    read_fd = -1;

    if (child_pid > 0) {
        int status = 0;
        if (waitpid(child_pid, &status, WNOHANG) == 0) {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, &status, 0);
        }
    }
    child_pid = -1;
}

bool BotProcess::send_line(const std::string& line, std::string* error) {
    if (!is_running() || write_file == nullptr) {
        if (error != nullptr) {
            *error = "Bot process is not running";
        }
        return false;
    }

    if (std::fprintf(to_file(write_file), "%s\n", line.c_str()) < 0) {
        if (error != nullptr) {
            *error = std::string("Failed to write to bot process: ") + std::strerror(errno);
        }
        return false;
    }

    if (std::fflush(to_file(write_file)) != 0) {
        if (error != nullptr) {
            *error = std::string("Failed to flush bot process input: ") + std::strerror(errno);
        }
        return false;
    }

    return true;
}

bool BotProcess::wait_for_token(const std::string& token, std::string* error) {
    if (!is_running() || read_file == nullptr) {
        if (error != nullptr) {
            *error = "Bot process is not running";
        }
        return false;
    }

    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), to_file(read_file)) != nullptr) {
        const std::string line = ChessIO::trim(buffer.data());
        if (line == token) {
            return true;
        }
    }

    if (error != nullptr) {
        *error = "Bot process closed while waiting for " + token;
    }
    return false;
}

bool BotProcess::ping(std::string* error) {
    if (!send_line("ping", error)) {
        return false;
    }
    return wait_for_token("pong", error);
}

bool BotProcess::new_game(std::string* error) {
    if (!send_line("newgame", error)) {
        return false;
    }
    return wait_for_token("ready", error);
}

std::optional<BotProcess::SearchResult> BotProcess::search_move(int depth,
                                                                int time_limit_ms,
                                                                const std::string& fen,
                                                                const std::vector<std::string>& history_fens,
                                                                std::string* error) {
    std::string command = "go\t" + std::to_string(depth) + "\t" +
                          std::to_string(time_limit_ms) + "\t" + fen;
    for (const std::string& history_fen : history_fens) {
        command.push_back('\t');
        command += history_fen;
    }

    if (!send_line(command, error)) {
        return std::nullopt;
    }

    SearchResult result;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), to_file(read_file)) != nullptr) {
        const std::string line = ChessIO::trim(buffer.data());
        if (line.rfind("info\t", 0) == 0) {
            result.info_lines.push_back(line);
            continue;
        }
        if (line.rfind("bestmove\t", 0) == 0) {
            result.bestmove_uci = line.substr(9);
            return result;
        }
        if (line.rfind("error\t", 0) == 0) {
            if (error != nullptr) {
                *error = line.substr(6);
            }
            return std::nullopt;
        }
    }

    if (error != nullptr) {
        *error = "Bot process closed during search";
    }
    return std::nullopt;
}
