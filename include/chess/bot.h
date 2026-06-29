#pragma once

#include "chess/chessboard.h"
#include "chess/minimax.h"
#include "chess/online_tablebase.h"

#include <cstdint>
#include <string>
#include <vector>

struct Bot {
    int depth;
    bool use_online_tablebase = false;
    OnlineTablebase online_tablebase;
    std::vector<std::uint64_t> position_history;
    mutable int last_search_completed_depth = 0;
    mutable int last_search_eval = 0;
    mutable std::string last_move_debug;
    mutable Minimax searcher;

    explicit Bot(int search_depth = 3) : depth(search_depth), searcher(search_depth) {}

    Move choose_move(const ChessBoard& board, int max_depth, int time_limit_ms) const;
    bool enable_online_tablebase(const std::string& base_url, int timeout_ms = 200);
    void disable_online_tablebase();
    int get_last_search_completed_depth() const;
    int get_last_search_eval() const;
    const std::string& get_last_move_debug() const;
    void reset_history();
    void record_position(const ChessBoard& board);
};
