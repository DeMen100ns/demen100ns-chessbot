#include "chess/minimax.h"

#include "minimax_internal.h"

#include <algorithm>
#include <utility>

using namespace MinimaxInternal;

namespace {

int repetition_count_for_current_path(const ChessBoard& board,
                                      const std::vector<std::uint64_t>& repetition_history) {
    if (repetition_history.empty()) {
        return 0;
    }

    const std::uint64_t current_key = board.position_key();
    const int last_index = static_cast<int>(repetition_history.size()) - 1;
    const int first_reversible_index = std::max(0, last_index - board.halfmove_clock);

    int count = 0;
    for (int index = last_index; index >= first_reversible_index; index -= 2) {
        if (repetition_history[static_cast<std::size_t>(index)] == current_key) {
            ++count;
        }
    }

    return count;
}

bool has_insufficient_material(const ChessBoard& board) {
    if (board.piece_bitboard(W_PAWN) != 0 ||
        board.piece_bitboard(B_PAWN) != 0 ||
        board.piece_bitboard(W_ROOK) != 0 ||
        board.piece_bitboard(B_ROOK) != 0 ||
        board.piece_bitboard(W_QUEEN) != 0 ||
        board.piece_bitboard(B_QUEEN) != 0) {
        return false;
    }

    const Bitboard white_bishops = board.piece_bitboard(W_BISHOP);
    const Bitboard black_bishops = board.piece_bitboard(B_BISHOP);
    const Bitboard white_knights = board.piece_bitboard(W_KNIGHT);
    const Bitboard black_knights = board.piece_bitboard(B_KNIGHT);
    const int bishop_count = __builtin_popcountll(white_bishops | black_bishops);
    const int knight_count = __builtin_popcountll(white_knights | black_knights);
    const int minor_count = bishop_count + knight_count;

    if (minor_count <= 1) {
        return true;
    }

    const bool exactly_one_bishop_each =
        bishop_count == 2 &&
        knight_count == 0 &&
        __builtin_popcountll(white_bishops) == 1 &&
        __builtin_popcountll(black_bishops) == 1;
    if (!exactly_one_bishop_each) {
        return false;
    }

    const int white_bishop_square = __builtin_ctzll(white_bishops);
    const int black_bishop_square = __builtin_ctzll(black_bishops);
    const int white_bishop_color =
        (row_of(white_bishop_square) + col_of(white_bishop_square)) & 1;
    const int black_bishop_color =
        (row_of(black_bishop_square) + col_of(black_bishop_square)) & 1;
    return white_bishop_color == black_bishop_color;
}

bool is_draw_by_rule(const ChessBoard& board) {
    return board.halfmove_clock >= 100 || has_insufficient_material(board);
}

bool is_pawn_move(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    return moving_piece == W_PAWN || moving_piece == B_PAWN;
}

bool is_quiet_non_pawn_move(const ChessBoard& board, const Move& move) {
    return move.promotion == EMPTY &&
           !is_capture_move(board, move) &&
           !is_pawn_move(board, move);
}

bool has_non_pawn_material(const ChessBoard& board, Color color) {
    const Bitboard knights =
        board.piece_bitboard(color == WHITE ? W_KNIGHT : B_KNIGHT);
    const Bitboard bishops =
        board.piece_bitboard(color == WHITE ? W_BISHOP : B_BISHOP);
    const Bitboard rooks =
        board.piece_bitboard(color == WHITE ? W_ROOK : B_ROOK);
    const Bitboard queens =
        board.piece_bitboard(color == WHITE ? W_QUEEN : B_QUEEN);
    return (knights | bishops | rooks | queens) != 0;
}

int count_non_pawn_pieces(const ChessBoard& board, Color color) {
    const Bitboard knights =
        board.piece_bitboard(color == WHITE ? W_KNIGHT : B_KNIGHT);
    const Bitboard bishops =
        board.piece_bitboard(color == WHITE ? W_BISHOP : B_BISHOP);
    const Bitboard rooks =
        board.piece_bitboard(color == WHITE ? W_ROOK : B_ROOK);
    const Bitboard queens =
        board.piece_bitboard(color == WHITE ? W_QUEEN : B_QUEEN);
    return __builtin_popcountll(knights | bishops | rooks | queens);
}

bool null_move_material_safe(const ChessBoard& board) {
    const int white_rooks =
        __builtin_popcountll(board.piece_bitboard(W_ROOK));
    const int black_rooks =
        __builtin_popcountll(board.piece_bitboard(B_ROOK));
    const int white_queens =
        __builtin_popcountll(board.piece_bitboard(W_QUEEN));
    const int black_queens =
        __builtin_popcountll(board.piece_bitboard(B_QUEEN));
    const int total_major_pieces = white_rooks + black_rooks + white_queens + black_queens;

    const int white_minors =
        __builtin_popcountll(board.piece_bitboard(W_KNIGHT) | board.piece_bitboard(W_BISHOP));
    const int black_minors =
        __builtin_popcountll(board.piece_bitboard(B_KNIGHT) | board.piece_bitboard(B_BISHOP));
    const int total_minor_pieces = white_minors + black_minors;

    if (total_major_pieces == 0 && total_minor_pieces <= 2) {
        return false;
    }

    return true;
}

ChessBoard make_null_move(const ChessBoard& board) {
    ChessBoard next = board;

    xor_en_passant_file(next.zobrist_key, board);
    next.en_passant_square = -1;
    ++next.halfmove_clock;
    if (next.turn == BLACK) {
        ++next.turn_number;
    }
    next.set_turn(opposite_color(next.turn));
    next.zobrist_key ^= zobrist_side_to_move_key();
    return next;
}

bool should_try_null_move(const ChessBoard& board,
                          int depth_left,
                          bool in_check,
                          bool is_pv_node,
                          bool allow_null_move) {
    constexpr int kNullMoveMinDepth = 3;

    if (!allow_null_move || depth_left < kNullMoveMinDepth || in_check || is_pv_node) {
        return false;
    }

    if (!has_non_pawn_material(board, board.turn)) {
        return false;
    }

    if (count_non_pawn_pieces(board, opposite_color(board.turn)) == 0 &&
        count_non_pawn_pieces(board, board.turn) <= 2) {
        return false;
    }

    return null_move_material_safe(board);
}

bool should_reduce_move(const ChessBoard& board,
                        const Move& move,
                        int move_index,
                        int depth_left,
                        bool in_check,
                        bool is_pv_node,
                        bool is_killer) {
    constexpr int kLmrMinDepth = 3;
    constexpr int kLmrMinMoveIndex = 1;
    constexpr bool kAllowQuietNonPawn = true;
    constexpr bool kAllowQuietPawn = true;
    constexpr bool kAllowBadCaptures = true;
    constexpr bool kGuardInCheck = true;
    constexpr bool kGuardPv = false;
    constexpr bool kGuardKiller = true;
    constexpr bool kGuardGivesCheck = true;

    if (depth_left < kLmrMinDepth || move_index < kLmrMinMoveIndex) {
        return false;
    }

    if (kGuardInCheck && in_check) {
        return false;
    }

    if (kGuardPv && is_pv_node) {
        return false;
    }

    if (move.promotion != EMPTY) {
        return false;
    }

    if (kGuardKiller && is_killer) {
        return false;
    }

    if (kGuardGivesCheck && is_checking_move(board, move)) {
        return false;
    }

    if (kAllowQuietNonPawn && is_quiet_non_pawn_move(board, move)) {
        return true;
    }

    if (kAllowQuietPawn &&
        move.promotion == EMPTY &&
        !is_capture_move(board, move) &&
        is_pawn_move(board, move)) {
        return true;
    }

    if (kAllowBadCaptures && is_capture_move(board, move)) {
        return static_exchange_eval(board, move) < 0;
    }

    return false;
}

}  // namespace

void Minimax::reset_node_stats() {
    current_node_stats = {};
    last_node_stats = {};
}

bool Minimax::is_time_up() {
    if (stop_search) {
        return true;
    }
    SearchControl* control = active_limits.control;
    if (control && control->stop.load(std::memory_order_relaxed)) {
        last_stop_reason = "stopped";
        return stop_search = true;
    }
    if (control && active_limits.pondering && control->hit_ready.load(std::memory_order_acquire)) {
        last_ponder_ms = std::max(0.0, std::chrono::duration<double, std::milli>(
            control->hit_limits.start - active_limits.start).count());
        last_ponder_depth = last_completed_depth;
        last_ponder_hit = true;
        active_limits = control->hit_limits;
        active_limits.control = control;
        active_limits.pondering = false;
        use_time_limit = active_limits.timed();
        deadline = active_limits.start + std::chrono::milliseconds(active_limits.hard_ms);
        if (active_limits.managed()) {
            last_target_ms = std::min(double(active_limits.hard_ms), active_limits.soft_ms * time_factor);
            ponder_credit_ms = std::min(last_ponder_ms, last_target_ms);
            if (last_completed_depth > 0 && ponder_credit_ms >= last_target_ms) {
                last_stop_reason = "ponder_ready";
                return stop_search = true;
            }
        }
    }

    if (use_time_limit && Clock::now() >= deadline) {
        last_stop_reason = active_limits.pondering ? "ponder_limit" : "hard_limit";
        stop_search = true;
    }

    return stop_search;
}

int Minimax::remaining_depth(int current_depth) const {
    return std::max(0, depth - current_depth);
}

int Minimax::quiescence(const ChessBoard& board,
                        int current_depth,
                        int alpha,
                        int beta,
                        std::vector<std::uint64_t>& repetition_history) {
    ++current_node_stats.qnodes;

    if (is_draw_by_rule(board) ||
        repetition_count_for_current_path(board, repetition_history) >= 3) {
        ++current_node_stats.qleaves;
        return 0;
    }

    const bool in_check = board.is_in_check(board.turn);
    if (in_check) {
        MoveList evasions;
        generate_evasions_into(board, board.turn, evasions);
        if (evasions.empty()) {
            ++current_node_stats.qleaves;
            return -mate_score(current_depth);
        }

        if (is_time_up()) {
            ++current_node_stats.qleaves;
            return evaluate(board);
        }

        order_moves(board, evasions);

        int best = -Chess::MAX_SCORE;
        for (const Move& move : evasions) {
            const ChessBoard new_board = board.make_move(move);
            repetition_history.push_back(new_board.position_key());
            const int score =
                -quiescence(new_board, current_depth + 1, -beta, -alpha, repetition_history);
            repetition_history.pop_back();
            if (stop_search) {
                break;
            }
            best = std::max(best, score);
            alpha = std::max(alpha, score);
            if (alpha >= beta) {
                break;
            }
        }

        return best;
    }

    if (is_time_up()) {
        ++current_node_stats.qleaves;
        return evaluate(board);
    }
    const int stand_pat = evaluate(board);
    if (stand_pat >= beta) {
        ++current_node_stats.qleaves;
        return stand_pat;
    }
    alpha = std::max(alpha, stand_pat);

    const int quiescence_ply = std::max(0, current_depth - depth);
    const bool include_quiet_checks =
        quiescence_ply < kMaxQuiescenceCheckPlies &&
        stand_pat + kQuietCheckDeltaMargin >= alpha;
    MoveList moves;
    generate_quiescence_moves_into(board, board.turn, include_quiet_checks, moves);

    std::size_t write_index = 0;
    for (std::size_t read_index = 0; read_index < moves.size(); ++read_index) {
        const Move& move = moves[read_index];
        bool pruned = false;
        if (move.promotion != EMPTY) {
            moves[write_index++] = move;
            continue;
        }
        const bool is_capture = is_capture_move(board, move);
        if (is_capture) {
            if (!is_checking_move(board, move) &&
                stand_pat + capture_value(board, move) + kDeltaPruningMargin < alpha) {
                pruned = true;
            }
            if (!pruned && static_exchange_eval(board, move) < 0) {
                pruned = true;
            }
        }
        if (!pruned) {
            moves[write_index++] = move;
        }
    }
    moves.truncate(write_index);

    if (moves.empty()) {
        ++current_node_stats.qleaves;
        return stand_pat;
    }

    order_moves(board, moves);

    int best = stand_pat;
    for (const Move& move : moves) {
        const ChessBoard new_board = board.make_move(move);
        repetition_history.push_back(new_board.position_key());
        const int score =
            -quiescence(new_board, current_depth + 1, -beta, -alpha, repetition_history);
        repetition_history.pop_back();
        if (stop_search) {
            break;
        }
        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    return best;
}

int Minimax::negamax(const ChessBoard& board,
                     int current_depth,
                     int alpha,
                     int beta,
                     std::vector<std::uint64_t>& repetition_history,
                     bool allow_null_move,
                     const std::optional<Move>& previous_move) {
    ++current_node_stats.nodes;

    if (is_draw_by_rule(board) ||
        repetition_count_for_current_path(board, repetition_history) >= 3) {
        return 0;
    }

    if (is_time_up()) {
        return evaluate(board);
    }

    if (current_depth >= depth) {
        ++current_node_stats.leaves;
        return quiescence(board, current_depth, alpha, beta, repetition_history);
    }

    const int depth_left = remaining_depth(current_depth);
    const bool in_check = board.is_in_check(board.turn);
    const bool is_pv_node = beta > alpha + 1;

    if (should_try_null_move(board, depth_left, in_check, is_pv_node, allow_null_move)) {
        constexpr int kNullMoveReduction = 2;
        const ChessBoard null_board = make_null_move(board);
        const int null_score = -negamax(null_board,
                                        current_depth + 1 + kNullMoveReduction,
                                        -beta,
                                        -beta + 1,
                                        repetition_history,
                                        false,
                                        std::nullopt);
        if (stop_search) {
            return null_score;
        }
        if (null_score >= beta) {
            return beta;
        }
    }

    std::optional<Move> tt_best_move;
    if (can_use_transposition(board, repetition_history)) {
        if (const auto tt_entry = probe_transposition(board, depth_left, current_depth);
            tt_entry.has_value()) {
            tt_best_move = tt_entry->best_move;
            switch (static_cast<TTFlag>(tt_entry->flag)) {
                case TTFlag::Exact:
                    return tt_entry->score;
                case TTFlag::LowerBound:
                    alpha = std::max(alpha, tt_entry->score);
                    break;
                case TTFlag::UpperBound:
                    beta = std::min(beta, tt_entry->score);
                    break;
            }
            if (alpha >= beta) {
                return tt_entry->score;
            }
        }
    }

    const int original_alpha = alpha;
    const int original_beta = beta;
    int best = -Chess::MAX_SCORE;
    MoveList moves;
    generate_moves_into(board, board.turn, moves);
    if (moves.empty()) {
        return in_check ? -mate_score(current_depth) : 0;
    }

    const auto& killer_slot =
        killer_moves[static_cast<std::size_t>(std::min(current_depth, kMaxKillerPlies - 1))];
    const std::optional<Move> counter_move = get_counter_move(board.turn, previous_move);
    order_moves(board,
                moves,
                tt_best_move,
                killer_slot,
                history_scores[static_cast<std::size_t>(board.turn)],
                counter_move);

    Move best_move = moves[0];
    for (std::size_t move_index = 0; move_index < moves.size(); ++move_index) {
        const Move& move = moves[move_index];
        const ChessBoard new_board = board.make_move(move);
        repetition_history.push_back(new_board.position_key());
        int score = -Chess::MAX_SCORE;

        if (move_index == 0) {
            score = -negamax(new_board,
                             current_depth + 1,
                             -beta,
                             -alpha,
                             repetition_history,
                             true,
                             move);
        } else {
            const bool can_reduce = should_reduce_move(board,
                                                       move,
                                                       static_cast<int>(move_index),
                                                       depth_left,
                                                       in_check,
                                                       is_pv_node,
                                                       is_killer_move(move, current_depth));
            if (can_reduce) {
                constexpr int kLmrReduction = 1;
                score = -negamax(new_board,
                                 current_depth + 1 + kLmrReduction,
                                 -alpha - 1,
                                 -alpha,
                                 repetition_history,
                                 true,
                                 move);
            }

            if (!can_reduce || score > alpha) {
                score = -negamax(new_board,
                                 current_depth + 1,
                                 -alpha - 1,
                                 -alpha,
                                 repetition_history,
                                 true,
                                 move);
                if (score > alpha && score < beta) {
                    score = -negamax(new_board,
                                     current_depth + 1,
                                     -beta,
                                     -alpha,
                                     repetition_history,
                                     true,
                                     move);
                }
            }
        }
        repetition_history.pop_back();
        if (stop_search) {
            break;
        }
        if (score > best) {
            best = score;
            best_move = move;
        }
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            if (move.promotion == EMPTY && !is_capture_move(board, move)) {
                store_killer_move(move, current_depth);
                store_counter_move(board.turn, previous_move, move);
                store_history_score(board, move, depth_left);
            }
            break;
        }
    }

    if (!stop_search && can_use_transposition(board, repetition_history)) {
        TTFlag flag = TTFlag::Exact;
        if (best <= original_alpha) {
            flag = TTFlag::UpperBound;
        } else if (best >= original_beta) {
            flag = TTFlag::LowerBound;
        }
        store_transposition(board, depth_left, best, flag, best_move, current_depth);
    }

    return best;
}

Move Minimax::search_root(const ChessBoard& board,
                          int search_depth,
                          bool& completed,
                          int& search_eval,
                          std::vector<std::uint64_t>& repetition_history,
                          const std::optional<Move>& preferred_move) {
    depth = search_depth;
    MoveList moves;
    generate_moves_into(board, board.turn, moves);
    if (is_draw_by_rule(board)) {
        search_eval = 0;
        completed = true;
        return moves.empty() ? Move(-1, -1) : moves[0];
    }
    if (moves.empty()) {
        search_eval = board.is_in_check(board.turn) ? -mate_score(0) : 0;
        completed = true;
        return Move(-1, -1);
    }

    std::optional<Move> tt_best_move;
    if (const auto tt_entry = probe_transposition(board, search_depth, 0); tt_entry.has_value()) {
        tt_best_move = tt_entry->best_move;
    } else {
        tt_best_move = preferred_move;
    }
    order_moves(board,
                moves,
                tt_best_move,
                killer_moves[0],
                history_scores[static_cast<std::size_t>(board.turn)],
                std::nullopt);

    Move best_move = moves[0];
    int best_eval = -Chess::MAX_SCORE;
    int alpha = -Chess::MAX_SCORE;
    const int beta = Chess::MAX_SCORE;

    for (std::size_t move_index = 0; move_index < moves.size(); ++move_index) {
        const Move& move = moves[move_index];
        if (is_time_up()) {
            completed = false;
            return best_move;
        }

        const ChessBoard new_board = board.make_move(move);
        repetition_history.push_back(new_board.position_key());
        int eval = -Chess::MAX_SCORE;
        if (move_index == 0) {
            eval = -negamax(new_board, 1, -beta, -alpha, repetition_history, true, move);
        } else {
            eval = -negamax(new_board, 1, -alpha - 1, -alpha, repetition_history, true, move);
            if (eval > alpha && eval < beta) {
                eval = -negamax(new_board, 1, -beta, -alpha, repetition_history, true, move);
            }
        }
        repetition_history.pop_back();
        if (stop_search) {
            completed = false;
            return best_move;
        }

        if (eval > best_eval) {
            best_eval = eval;
            best_move = move;
        }
        alpha = std::max(alpha, eval);
    }

    store_transposition(board, search_depth, best_eval, TTFlag::Exact, best_move, 0);
    search_eval = best_eval;
    completed = true;
    return best_move;
}

Move Minimax::find_best_move(const ChessBoard& board,
                             int max_depth,
                             int time_limit_ms,
                             std::vector<std::uint64_t> repetition_history) {
    return find_best_move(board, max_depth, SearchLimits::fixed(time_limit_ms),
                          std::move(repetition_history));
}

Move Minimax::find_best_move(const ChessBoard& board,
                             int max_depth,
                             const SearchLimits& limits,
                             std::vector<std::uint64_t> repetition_history) {
    last_completed_depth = 0;
    last_search_eval = 0;
    last_best_move_changes = 0;
    last_elapsed_ms = 0;
    last_target_ms = limits.managed() ? std::min(limits.soft_ms, limits.hard_ms) : -1;
    last_stop_reason = "max_depth";
    last_ponder_hit = false;
    last_ponder_depth = 0;
    last_ponder_ms = 0;
    active_limits = limits;
    time_factor = 1;
    ponder_credit_ms = 0;
    use_time_limit = limits.timed();
    stop_search = false;
    if (use_time_limit) {
        deadline = limits.start + std::chrono::milliseconds(limits.hard_ms);
    }
    const auto finish = [&](const Move& move, const char* reason) {
        last_stop_reason = reason;
        last_elapsed_ms = active_limits.elapsed_ms();
        if (active_limits.pondering) {
            last_ponder_ms = last_elapsed_ms;
            last_ponder_depth = last_completed_depth;
        }
        use_time_limit = false;
        stop_search = false;
        return move;
    };
    reset_node_stats();
    MoveList moves;
    generate_moves_into(board, board.turn, moves);
    if (moves.empty()) {
        last_search_eval = board.is_in_check(board.turn) ? -mate_score(0) : 0;
        return finish(Move(-1, -1), "terminal");
    }
    if (is_draw_by_rule(board)) {
        return finish(moves[0], "draw");
    }
    // Keep fixed-depth analysis semantics; timed play needs no search for a forced move.
    if (limits.timed() && moves.size() == 1) {
        return finish(moves[0], "forced_move");
    }
    if (is_time_up()) {
        return finish(moves[0], last_stop_reason);
    }

    advance_transposition_age();
    clear_eval_cache();
    clear_killer_moves();
    clear_counter_moves();
    clear_history_scores();
    last_search_eval = evaluate(board);

    if (repetition_history.empty() || repetition_history.back() != board.position_key()) {
        repetition_history.push_back(board.position_key());
    }

    order_moves(board, moves);
    Move best_move = moves[0];

    const int bounded_max_depth = std::max(1, max_depth);
    std::optional<Move> previous_best;
    int stable_depths = 0;

    for (int current_search_depth = 1;
         current_search_depth <= bounded_max_depth;
         ++current_search_depth) {
        if (is_time_up()) {
            return finish(best_move, last_stop_reason);
        }

        stop_search = false;
        current_node_stats = {};
        bool completed = false;
        int search_eval = 0;
        const Move candidate =
            search_root(board, current_search_depth, completed, search_eval, repetition_history, previous_best);
        if (!completed) {
            return finish(best_move, last_stop_reason);
        }
        const bool changed = previous_best.has_value() && !same_move(candidate, *previous_best);
        stable_depths = previous_best.has_value() && !changed ? stable_depths + 1 : 0;
        last_best_move_changes += changed ? 1 : 0;
        // Scores are side-to-move relative at this root. Do not compare depth 1
        // against static eval, and clamp mate transitions before any multiplication.
        const int eval_drop = previous_best.has_value()
            ? std::clamp(last_search_eval - search_eval, 0, 100) : 0;
        best_move = candidate;
        previous_best = best_move;
        last_completed_depth = current_search_depth;
        last_search_eval = search_eval;
        last_node_stats = current_node_stats;
        time_factor = search_time_factor(stable_depths, changed, eval_drop);
        if (is_time_up()) {
            return finish(best_move, last_stop_reason);
        }
        if (active_limits.managed() && !active_limits.pondering) {
            last_target_ms = std::min(double(active_limits.hard_ms), active_limits.soft_ms * time_factor);
            if (active_limits.elapsed_ms() + ponder_credit_ms >= last_target_ms) {
                return finish(best_move, "soft_limit");
            }
        }
    }

    return finish(best_move, "max_depth");
}
