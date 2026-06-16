#include "bench/uci_engine.h"

#include "bench/chess_io.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

FILE* to_file(void* handle) {
    return static_cast<FILE*>(handle);
}

bool parse_score_line(const std::string& line, UciEngine::SearchResult& result) {
    const std::string score_key = " score ";
    const std::size_t score_pos = line.find(score_key);
    if (score_pos == std::string::npos) {
        return false;
    }

    const std::size_t type_pos = score_pos + score_key.size();
    if (line.compare(type_pos, 3, "cp ") == 0) {
        const std::string value = line.substr(type_pos + 3);
        result.score_cp = std::atoi(value.c_str());
        result.has_score = true;
        result.score_is_mate = false;
        result.mate_plies = 0;
        return true;
    }

    if (line.compare(type_pos, 5, "mate ") == 0) {
        const std::string value = line.substr(type_pos + 5);
        result.mate_plies = std::atoi(value.c_str());
        result.score_is_mate = true;
        result.has_score = true;
        result.score_cp = result.mate_plies >= 0 ? 100000 : -100000;
        return true;
    }

    return false;
}

}  // namespace

UciEngine::UciEngine(std::string binary_path_in)
    : binary_path(std::move(binary_path_in)) {}

UciEngine::~UciEngine() {
    stop();
}

bool UciEngine::is_running() const {
    return child_pid > 0;
}

bool UciEngine::start(std::string* error) {
    if (is_running()) {
        return true;
    }

    if (binary_path.empty()) {
        if (error != nullptr) {
            *error = "Stockfish path is empty";
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

    const pid_t pid = fork();
    if (pid < 0) {
        if (error != nullptr) {
            *error = std::string("fork failed: ") + std::strerror(errno);
        }
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return false;
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);

        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);

        if (binary_path.find('/') != std::string::npos) {
            char* const argv[] = {const_cast<char*>(binary_path.c_str()), nullptr};
            execv(binary_path.c_str(), argv);
        } else {
            char* const argv[] = {const_cast<char*>(binary_path.c_str()), nullptr};
            execvp(binary_path.c_str(), argv);
        }
        _exit(127);
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

    if (!send_line("uci", error) || !wait_for_token("uciok", error)) {
        stop();
        return false;
    }
    if (!send_line("isready", error) || !wait_for_token("readyok", error)) {
        stop();
        return false;
    }

    return true;
}

void UciEngine::stop() {
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

bool UciEngine::send_line(const std::string& line, std::string* error) {
    if (!is_running() || write_file == nullptr) {
        if (error != nullptr) {
            *error = "UCI engine is not running";
        }
        return false;
    }

    if (std::fprintf(to_file(write_file), "%s\n", line.c_str()) < 0) {
        if (error != nullptr) {
            *error = std::string("Failed to write to UCI engine: ") + std::strerror(errno);
        }
        return false;
    }

    if (std::fflush(to_file(write_file)) != 0) {
        if (error != nullptr) {
            *error = std::string("Failed to flush UCI engine input: ") + std::strerror(errno);
        }
        return false;
    }

    return true;
}

bool UciEngine::wait_for_token(const std::string& token, std::string* error) {
    if (!is_running() || read_file == nullptr) {
        if (error != nullptr) {
            *error = "UCI engine is not running";
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
        *error = "UCI engine closed while waiting for " + token;
    }
    return false;
}

bool UciEngine::set_option(const std::string& name, const std::string& value, std::string* error) {
    if (!send_line("setoption name " + name + " value " + value, error)) {
        return false;
    }
    if (!send_line("isready", error)) {
        return false;
    }
    return wait_for_token("readyok", error);
}

bool UciEngine::new_game(std::string* error) {
    if (!send_line("ucinewgame", error)) {
        return false;
    }
    if (!send_line("isready", error)) {
        return false;
    }
    return wait_for_token("readyok", error);
}

std::optional<UciEngine::SearchResult> UciEngine::run_search_command(const std::string& position_command,
                                                                     const std::string& go_command,
                                                                     std::string* error) {
    if (!is_running()) {
        if (!start(error)) {
            return std::nullopt;
        }
    }

    if (!send_line(position_command, error) || !send_line(go_command, error)) {
        return std::nullopt;
    }

    SearchResult result;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), to_file(read_file)) != nullptr) {
        const std::string line = ChessIO::trim(buffer.data());
        if (line.rfind("info ", 0) == 0) {
            parse_score_line(line, result);
            continue;
        }
        if (line.rfind("bestmove ", 0) == 0) {
            const std::size_t split = line.find(' ', 9);
            result.bestmove_uci = split == std::string::npos ? line.substr(9) : line.substr(9, split - 9);
            return result;
        }
    }

    if (error != nullptr) {
        *error = "UCI engine closed during search";
    }
    return std::nullopt;
}

std::optional<UciEngine::SearchResult> UciEngine::search_fen_depth(const std::string& fen,
                                                                   int depth,
                                                                   std::string* error) {
    return run_search_command("position fen " + fen,
                              "go depth " + std::to_string(depth),
                              error);
}

std::optional<UciEngine::SearchResult> UciEngine::search_fen_movetime(const std::string& fen,
                                                                      int movetime_ms,
                                                                      std::string* error) {
    return run_search_command("position fen " + fen,
                              "go movetime " + std::to_string(movetime_ms),
                              error);
}
