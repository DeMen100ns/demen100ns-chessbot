#pragma once

#include <optional>
#include <string>

class UciEngine {
public:
    struct SearchResult {
        std::string bestmove_uci;
        bool has_score = false;
        int score_cp = 0;
        bool score_is_mate = false;
        int mate_plies = 0;
    };

    explicit UciEngine(std::string binary_path);
    ~UciEngine();

    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    bool is_running() const;

    bool set_option(const std::string& name, const std::string& value, std::string* error = nullptr);
    bool new_game(std::string* error = nullptr);
    std::optional<SearchResult> search_fen_depth(const std::string& fen,
                                                 int depth,
                                                 std::string* error = nullptr);
    std::optional<SearchResult> search_fen_movetime(const std::string& fen,
                                                    int movetime_ms,
                                                    std::string* error = nullptr);

private:
    bool send_line(const std::string& line, std::string* error);
    bool wait_for_token(const std::string& token, std::string* error);
    std::optional<SearchResult> run_search_command(const std::string& position_command,
                                                   const std::string& go_command,
                                                   std::string* error);

    std::string binary_path;
    int child_pid = -1;
    int write_fd = -1;
    int read_fd = -1;
    void* write_file = nullptr;
    void* read_file = nullptr;
};
