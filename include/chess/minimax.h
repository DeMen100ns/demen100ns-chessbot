#pragma once

#include "chess/chessboard.h"

#include <array>
#include <chrono>
#include <cstddef>
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
    int last_search_eval = 0;

    explicit Minimax(int d = 3)
        : depth(d),
          transposition_table(kTranspositionTableSize) {}

    int evaluate(const ChessBoard& board);
    int evaluate_nnue(const ChessBoard& board);
    Move find_best_move(const ChessBoard& board,
                        int max_depth,
                        int time_limit_ms,
                        std::vector<std::uint64_t> repetition_history = {});
    int get_last_completed_depth() const { return last_completed_depth; }
    int get_last_search_eval() const { return last_search_eval; }
    const NodeStats& get_last_node_stats() const { return last_node_stats; }
    void clear_transposition_table();

private:
    using Clock = std::chrono::steady_clock;
    static constexpr int kMaxKillerPlies = 128;
    static constexpr int kBoardSquareCount = 64;
    static constexpr std::size_t kTranspositionTableSize = 1u << 20;
    enum class TTFlag {
        Exact,
        LowerBound,
        UpperBound,
    };

    struct TTEntry {
        std::uint64_t key = 0;
        int score = 0;
        int depth = -1;
        TTFlag flag = TTFlag::Exact;
        Move best_move{-1, -1};
        bool occupied = false;
        int age = 0;
    };

    Clock::time_point deadline;
    bool use_time_limit = false;
    bool stop_search = false;
    std::vector<TTEntry> transposition_table;
    int transposition_age = 0;
    std::array<std::array<std::optional<Move>, 2>, kMaxKillerPlies> killer_moves{};
    std::array<std::array<std::array<std::optional<Move>, kBoardSquareCount>, kBoardSquareCount>, 2>
        counter_moves{};
    std::array<std::array<std::array<int, kBoardSquareCount>, kBoardSquareCount>, 2> history_scores{};
    std::unordered_map<std::uint64_t, std::array<int, 4>> pawn_eval_cache;
    NodeStats current_node_stats{};
    NodeStats last_node_stats{};

    bool is_time_up();
    void reset_node_stats();
    Move search_root(const ChessBoard& board,
                     int search_depth,
                     bool& completed,
                     int& search_eval,
                     std::vector<std::uint64_t>& repetition_history,
                     const std::optional<Move>& preferred_move);
    int remaining_depth(int current_depth) const;
    bool can_use_transposition(const ChessBoard& board,
                               const std::vector<std::uint64_t>& repetition_history) const;
    std::optional<TTEntry> probe_transposition(const ChessBoard& board,
                                               int required_depth,
                                               int current_depth) const;
    void store_transposition(const ChessBoard& board,
                             int searched_depth,
                             int score,
                             TTFlag flag,
                             const Move& best_move,
                             int current_depth);
    void advance_transposition_age();
    void clear_eval_cache();
    void clear_killer_moves();
    void clear_counter_moves();
    void clear_history_scores();
    bool is_killer_move(const Move& move, int current_depth) const;
    std::optional<Move> get_counter_move(Color side,
                                         const std::optional<Move>& previous_move) const;
    void store_counter_move(Color side,
                            const std::optional<Move>& previous_move,
                            const Move& move);
    void store_killer_move(const Move& move, int current_depth);
    void store_history_score(const ChessBoard& board,
                             const Move& move,
                             int searched_depth);
    int quiescence(const ChessBoard& board,
                   int current_depth,
                   int alpha,
                   int beta,
                   std::vector<std::uint64_t>& repetition_history);
    int negamax(const ChessBoard& board,
                int current_depth,
                int alpha,
                int beta,
                std::vector<std::uint64_t>& repetition_history,
                bool allow_null_move,
                const std::optional<Move>& previous_move);
};
