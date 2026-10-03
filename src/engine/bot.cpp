#include "chess/bot.h"

#include <utility>

Move Bot::choose_move(const ChessBoard& board, int max_depth, int time_limit_ms) const {
    return choose_move(board, max_depth, SearchLimits::fixed(time_limit_ms));
}

Move Bot::choose_move(const ChessBoard& board, int max_depth, const SearchLimits& limits) const {
    const int piece_count = board.count_pieces();
    const bool probe_tablebase = use_online_tablebase && piece_count <= online_tablebase.max_pieces;
    const bool forced = probe_tablebase && limits.timed() && board.generate_moves(board.turn).size() == 1;

    if (probe_tablebase && !forced && !limits.expired() && !limits.pondering) {
        // Helper scripts use blocking popen/pclose and cannot honor a search deadline.
        // Native probes stay local; their elapsed time is charged to the same budget.
        const bool cache_only = limits.timed() && online_tablebase.backend == TablebaseBackend::HelperScript;
        if (const auto tablebase_move = online_tablebase.choose_move(board, cache_only); tablebase_move.has_value()) {
            last_search_completed_depth = 0;
            last_search_eval = 0;
            last_move_debug = "decision=tablebase stop_reason=tablebase elapsed_ms=" +
                std::to_string(limits.elapsed_ms()) + " " + online_tablebase.last_probe_debug;
            return *tablebase_move;
        }
    }

    searcher.depth = max_depth;
    auto repetition_history = position_history;
    const std::uint64_t key = board.position_key();
    if (repetition_history.empty() || repetition_history.back() != key) {
        repetition_history.push_back(key);
    }
    const Move best_move = searcher.find_best_move(
        board,
        max_depth,
        limits,
        std::move(repetition_history));
    last_search_completed_depth = searcher.get_last_completed_depth();
    last_search_eval = searcher.get_last_search_eval();
    const std::string tb_debug = use_online_tablebase
        ? (piece_count <= online_tablebase.max_pieces
            ? online_tablebase.last_probe_debug
            : "tb_skipped_piece_count=" + std::to_string(piece_count))
        : "tb_disabled";
    last_move_debug = "decision=search max_depth=" + std::to_string(max_depth) +
                      " completed_depth=" + std::to_string(last_search_completed_depth) +
                      " time_limit_ms=" + std::to_string(limits.hard_ms) +
                      " soft_ms=" + std::to_string(limits.soft_ms) +
                      " hard_ms=" + std::to_string(limits.hard_ms) +
                      " target_ms=" + std::to_string(searcher.last_target_ms) +
                      " elapsed_ms=" + std::to_string(searcher.last_elapsed_ms) +
                      " search_ponder_transition=" + std::to_string(searcher.last_ponder_hit) +
                      " ponder_depth=" + std::to_string(searcher.last_ponder_depth) +
                      " ponder_ms=" + std::to_string(searcher.last_ponder_ms) +
                      " best_move_changes=" + std::to_string(searcher.last_best_move_changes) +
                      " stop_reason=" + searcher.last_stop_reason +
                      " " + tb_debug;
    return best_move;
}

bool Bot::enable_online_tablebase(const std::string& base_url, int timeout_ms) {
    use_online_tablebase = true;
    return online_tablebase.configure(base_url, timeout_ms);
}

void Bot::disable_online_tablebase() {
    use_online_tablebase = false;
}

int Bot::get_last_search_completed_depth() const {
    return last_search_completed_depth;
}

int Bot::get_last_search_eval() const {
    return last_search_eval;
}

const std::string& Bot::get_last_move_debug() const {
    return last_move_debug;
}

void Bot::reset_history() {
    position_history.clear();
    searcher.clear_transposition_table();
}

void Bot::record_position(const ChessBoard& board) {
    position_history.push_back(board.position_key());
}
