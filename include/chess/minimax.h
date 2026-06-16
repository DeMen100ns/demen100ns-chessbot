#pragma once

#include "chess/chessboard.h"

#include <chrono>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

struct Minimax {
    struct NodeStats {
        std::uint64_t nodes = 0;
        std::uint64_t qnodes = 0;
        std::uint64_t leaves = 0;
        std::uint64_t qleaves = 0;
    };

    int depth;
    int last_completed_depth = 0;

    explicit Minimax(int d = 3)
        : depth(d),
          killer_moves(kMaxKillerPlies, {std::nullopt, std::nullopt}) {}

    int evaluate(const ChessBoard& board);
    Move find_best_move(const ChessBoard& board, int search_depth);
    Move find_best_move(const ChessBoard& board);
    Move find_best_move(const ChessBoard& board,
                        std::unordered_map<std::uint64_t, int> repetition_count,
                        int search_depth);
    Move find_best_move(const ChessBoard& board,
                        std::unordered_map<std::uint64_t, int> repetition_count);
    Move find_best_move_timed(const ChessBoard& board,
                              int time_limit_ms,
                              std::unordered_map<std::uint64_t, int> repetition_count);
    int get_last_completed_depth() const { return last_completed_depth; }
    const NodeStats& get_last_node_stats() const { return last_node_stats; }
    void clear_transposition_table();

private:
    using Clock = std::chrono::steady_clock;
    static constexpr int kMaxKillerPlies = 128;
    static constexpr int kBoardSquareCount = 64;
    enum class TTFlag {
        Exact,
        LowerBound,
        UpperBound,
    };

    struct TTEntry {
        int score;
        int depth;
        TTFlag flag;
        Move best_move;
    };

    Clock::time_point deadline;
    bool use_time_limit = false;
    bool stop_search = false;
    std::unordered_map<std::uint64_t, TTEntry> transposition_table;
    std::vector<std::array<std::optional<Move>, 2>> killer_moves;
    std::array<std::array<std::array<int, kBoardSquareCount>, kBoardSquareCount>, 2> history_scores{};
    NodeStats current_node_stats{};
    NodeStats last_node_stats{};

    bool is_time_up();
    void reset_node_stats();
    Move search_root(const ChessBoard& board,
                     int search_depth,
                     bool& completed,
                     std::unordered_map<std::uint64_t, int>& repetition_count);
    int remaining_depth(int current_depth) const;
    bool can_use_transposition(const ChessBoard& board,
                               const std::unordered_map<std::uint64_t, int>& repetition_count) const;
    std::optional<TTEntry> probe_transposition(const ChessBoard& board,
                                               int required_depth) const;
    void store_transposition(const ChessBoard& board,
                             int searched_depth,
                             int score,
                             TTFlag flag,
                             const Move& best_move);
    void clear_killer_moves();
    void clear_history_scores();
    bool is_killer_move(const Move& move, int current_depth) const;
    void store_killer_move(const Move& move, int current_depth);
    void store_history_score(const ChessBoard& board,
                             const Move& move,
                             int searched_depth);
    int quiescence(const ChessBoard& board,
                   int current_depth,
                   int alpha,
                   int beta,
                   std::unordered_map<std::uint64_t, int>& repetition_count);
    int negamax(const ChessBoard& board,
                int current_depth,
                int alpha,
                int beta,
                std::unordered_map<std::uint64_t, int>& repetition_count);
};
