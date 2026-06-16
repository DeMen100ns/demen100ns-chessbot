#pragma once

#include <optional>
#include <string>
#include <vector>

class BotProcess {
public:
    struct Config {
        std::string display_name;
        std::string executable_path;
        std::vector<std::string> args;
        std::string working_directory;
    };

    struct SearchResult {
        std::string bestmove_uci;
        std::vector<std::string> info_lines;
    };

    explicit BotProcess(Config config);
    ~BotProcess();

    BotProcess(const BotProcess&) = delete;
    BotProcess& operator=(const BotProcess&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    bool is_running() const;
    const Config& get_config() const { return config; }

    bool ping(std::string* error = nullptr);
    bool new_game(std::string* error = nullptr);
    std::optional<SearchResult> search_move(int depth,
                                            int time_limit_ms,
                                            const std::string& fen,
                                            const std::vector<std::string>& history_fens,
                                            std::string* error = nullptr);

private:
    bool send_line(const std::string& line, std::string* error);
    bool wait_for_token(const std::string& token, std::string* error);

    Config config;
    int child_pid = -1;
    int write_fd = -1;
    int read_fd = -1;
    void* write_file = nullptr;
    void* read_file = nullptr;
};
