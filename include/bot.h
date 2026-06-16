#pragma once

#include "chessboard.h"
#include "minimax.h"
#include "online_tablebase.h"

#include <cstdint>
#include <string>
#include <unordered_map>

struct Bot {
    int depth;
    bool use_online_tablebase = false;
    OnlineTablebase online_tablebase;
    std::unordered_map<std::uint64_t, int> position_history;
    mutable int last_search_completed_depth = 0;
    mutable std::string last_move_debug;
    mutable Minimax searcher;

    explicit Bot(int search_depth = 3) : depth(search_depth), searcher(search_depth) {}

    Move choose_move(const ChessBoard& board) const;
    Move choose_move_timed(const ChessBoard& board, int time_limit_ms) const;
    bool enable_online_tablebase(const std::string& base_url, int timeout_ms = 200);
    void disable_online_tablebase();
    int get_last_search_completed_depth() const;
    const std::string& get_last_move_debug() const;
    void reset_history();
    void record_position(const ChessBoard& board);
};
