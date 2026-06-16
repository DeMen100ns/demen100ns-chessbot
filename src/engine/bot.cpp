#include "bot.h"

#include "minimax.h"

#include <optional>
#include <utility>

namespace {
bool is_forced_en_passant_move(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    return (moving_piece == W_PAWN || moving_piece == B_PAWN) &&
           move.to == board.en_passant_square &&
           board.is_empty(move.to);
}

std::optional<Move> find_en_passant_move(const ChessBoard& board) {
    for (const Move& move : board.generate_moves(board.turn)) {
        if (is_forced_en_passant_move(board, move)) {
            return move;
        }
    }
    return std::nullopt;
}
}  // namespace

Move Bot::choose_move(const ChessBoard& board) const {
    const int piece_count = board.count_pieces();
    if (const auto en_passant_move = find_en_passant_move(board); en_passant_move.has_value()) {
        last_search_completed_depth = 0;
        last_move_debug = "decision=forced_en_passant";
        return *en_passant_move;
    }

    if (use_online_tablebase && piece_count <= online_tablebase.max_pieces) {
        if (const auto tablebase_move = online_tablebase.choose_move(board); tablebase_move.has_value()) {
            last_search_completed_depth = 0;
            last_move_debug = "decision=tablebase " + online_tablebase.last_probe_debug;
            return *tablebase_move;
        }
    }

    searcher.depth = depth;
    auto repetition_count = position_history;
    const std::uint64_t key = board.position_key();
    if (repetition_count[key] == 0) {
        repetition_count[key] = 1;
    }
    const Move best_move = searcher.find_best_move(board, std::move(repetition_count));
    last_search_completed_depth = searcher.get_last_completed_depth();
    const std::string tb_debug = use_online_tablebase
        ? (piece_count <= online_tablebase.max_pieces
            ? online_tablebase.last_probe_debug
            : "tb_skipped_piece_count=" + std::to_string(piece_count))
        : "tb_disabled";
    last_move_debug = "decision=search requested_depth=" + std::to_string(depth) +
                      " completed_depth=" + std::to_string(last_search_completed_depth) +
                      " " + tb_debug;
    return best_move;
}

Move Bot::choose_move_timed(const ChessBoard& board, int time_limit_ms) const {
    const int piece_count = board.count_pieces();
    if (const auto en_passant_move = find_en_passant_move(board); en_passant_move.has_value()) {
        last_search_completed_depth = 0;
        last_move_debug = "decision=forced_en_passant";
        return *en_passant_move;
    }

    if (use_online_tablebase && piece_count <= online_tablebase.max_pieces) {
        if (const auto tablebase_move = online_tablebase.choose_move(board); tablebase_move.has_value()) {
            last_search_completed_depth = 0;
            last_move_debug = "decision=tablebase " + online_tablebase.last_probe_debug;
            return *tablebase_move;
        }
    }

    searcher.depth = depth;
    auto repetition_count = position_history;
    const std::uint64_t key = board.position_key();
    if (repetition_count[key] == 0) {
        repetition_count[key] = 1;
    }
    const Move best_move =
        searcher.find_best_move_timed(board, time_limit_ms, std::move(repetition_count));
    last_search_completed_depth = searcher.get_last_completed_depth();
    const std::string tb_debug = use_online_tablebase
        ? (piece_count <= online_tablebase.max_pieces
            ? online_tablebase.last_probe_debug
            : "tb_skipped_piece_count=" + std::to_string(piece_count))
        : "tb_disabled";
    last_move_debug = "decision=search requested_depth=" + std::to_string(depth) +
                      " completed_depth=" + std::to_string(last_search_completed_depth) +
                      " time_limit_ms=" + std::to_string(time_limit_ms) +
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

const std::string& Bot::get_last_move_debug() const {
    return last_move_debug;
}

void Bot::reset_history() {
    position_history.clear();
    searcher.clear_transposition_table();
}

void Bot::record_position(const ChessBoard& board) {
    ++position_history[board.position_key()];
}
