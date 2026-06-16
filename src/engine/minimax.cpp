#include "minimax.h"

#include "minimax_internal.h"

#include <algorithm>

using namespace MinimaxInternal;

void Minimax::reset_node_stats() {
    current_node_stats = {};
    last_node_stats = {};
}

bool Minimax::is_time_up() {
    if (!use_time_limit || stop_search) {
        return stop_search;
    }

    if (Clock::now() >= deadline) {
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
                        std::unordered_map<std::uint64_t, int>& repetition_count) {
    ++current_node_stats.qnodes;

    const auto repetition_it = repetition_count.find(board.position_key());
    if (repetition_it != repetition_count.end() && repetition_it->second >= 3) {
        ++current_node_stats.qleaves;
        return 0;
    }

    const bool in_check = board.is_in_check(board.turn);
    if (in_check) {
        std::vector<Move> evasions = board.generate_evasions(board.turn);
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
            const std::uint64_t child_key = new_board.position_key();
            ++repetition_count[child_key];
            const int score =
                -quiescence(new_board, current_depth + 1, -beta, -alpha, repetition_count);
            auto child_it = repetition_count.find(child_key);
            if (child_it != repetition_count.end()) {
                --child_it->second;
                if (child_it->second == 0) {
                    repetition_count.erase(child_it);
                }
            }
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
    std::vector<Move> moves = board.generate_quiescence_moves(board.turn, include_quiet_checks);

    moves.erase(std::remove_if(moves.begin(), moves.end(), [&](const Move& move) {
        if (move.promotion != EMPTY) {
            return false;
        }
        if (!is_capture_move(board, move)) {
            return false;
        }
        if (!is_checking_move(board, move) &&
            stand_pat + capture_value(board, move) + kDeltaPruningMargin < alpha) {
            return true;
        }
        return static_exchange_eval(board, move) < 0;
    }), moves.end());

    if (moves.empty()) {
        ++current_node_stats.qleaves;
        return stand_pat;
    }

    order_moves(board, moves);

    int best = stand_pat;
    for (const Move& move : moves) {
        const ChessBoard new_board = board.make_move(move);
        const std::uint64_t child_key = new_board.position_key();
        ++repetition_count[child_key];
        const int score =
            -quiescence(new_board, current_depth + 1, -beta, -alpha, repetition_count);
        auto child_it = repetition_count.find(child_key);
        if (child_it != repetition_count.end()) {
            --child_it->second;
            if (child_it->second == 0) {
                repetition_count.erase(child_it);
            }
        }
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
                     std::unordered_map<std::uint64_t, int>& repetition_count) {
    ++current_node_stats.nodes;

    const auto repetition_it = repetition_count.find(board.position_key());
    if (repetition_it != repetition_count.end() && repetition_it->second >= 3) {
        return 0;
    }

    std::vector<Move> moves = board.generate_moves(board.turn);
    if (moves.empty()) {
        return board.is_in_check(board.turn) ? -mate_score(current_depth) : 0;
    }

    if (is_time_up()) {
        return evaluate(board);
    }

    if (current_depth >= depth) {
        ++current_node_stats.leaves;
        return quiescence(board, current_depth, alpha, beta, repetition_count);
    }

    const int depth_left = remaining_depth(current_depth);
    std::optional<Move> tt_best_move;
    if (can_use_transposition(board, repetition_count)) {
        if (const auto tt_entry = probe_transposition(board, depth_left); tt_entry.has_value()) {
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

    const auto& killer_slot =
        killer_moves[static_cast<std::size_t>(std::min(current_depth, kMaxKillerPlies - 1))];
    order_moves(board,
                moves,
                tt_best_move,
                killer_slot,
                history_scores[static_cast<std::size_t>(board.turn)]);

    const int original_alpha = alpha;
    const int original_beta = beta;
    int best = -Chess::MAX_SCORE;
    Move best_move = moves[0];
    for (const Move& move : moves) {
        const ChessBoard new_board = board.make_move(move);
        const std::uint64_t child_key = new_board.position_key();
        ++repetition_count[child_key];
        const int score = -negamax(new_board, current_depth + 1, -beta, -alpha, repetition_count);
        auto child_it = repetition_count.find(child_key);
        if (child_it != repetition_count.end()) {
            --child_it->second;
            if (child_it->second == 0) {
                repetition_count.erase(child_it);
            }
        }
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
                store_history_score(board, move, depth_left);
            }
            break;
        }
    }

    if (!stop_search && can_use_transposition(board, repetition_count)) {
        TTFlag flag = TTFlag::Exact;
        if (best <= original_alpha) {
            flag = TTFlag::UpperBound;
        } else if (best >= original_beta) {
            flag = TTFlag::LowerBound;
        }
        store_transposition(board, depth_left, best, flag, best_move);
    }

    return best;
}

Move Minimax::search_root(const ChessBoard& board,
                          int search_depth,
                          bool& completed,
                          std::unordered_map<std::uint64_t, int>& repetition_count) {
    depth = search_depth;
    std::vector<Move> moves = board.generate_moves(board.turn);
    if (moves.empty()) {
        completed = true;
        return Move(-1, -1);
    }

    std::optional<Move> tt_best_move;
    if (const auto tt_entry = probe_transposition(board, search_depth); tt_entry.has_value()) {
        tt_best_move = tt_entry->best_move;
    }
    order_moves(board,
                moves,
                tt_best_move,
                killer_moves[0],
                history_scores[static_cast<std::size_t>(board.turn)]);

    Move best_move = moves[0];
    int best_eval = -Chess::MAX_SCORE;
    int alpha = -Chess::MAX_SCORE;
    const int beta = Chess::MAX_SCORE;

    for (const Move& move : moves) {
        if (is_time_up()) {
            completed = false;
            return best_move;
        }

        const ChessBoard new_board = board.make_move(move);
        const std::uint64_t child_key = new_board.position_key();
        ++repetition_count[child_key];
        const int eval = -negamax(new_board, 1, -beta, -alpha, repetition_count);
        auto child_it = repetition_count.find(child_key);
        if (child_it != repetition_count.end()) {
            --child_it->second;
            if (child_it->second == 0) {
                repetition_count.erase(child_it);
            }
        }
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

    completed = true;
    return best_move;
}

Move Minimax::find_best_move(const ChessBoard& board, int search_depth) {
    std::unordered_map<std::uint64_t, int> repetition_count;
    repetition_count[board.position_key()] = 1;
    return find_best_move(board, std::move(repetition_count), search_depth);
}

Move Minimax::find_best_move(const ChessBoard& board,
                             std::unordered_map<std::uint64_t, int> repetition_count,
                             int search_depth) {
    use_time_limit = false;
    stop_search = false;
    last_completed_depth = 0;
    reset_node_stats();
    clear_killer_moves();
    clear_history_scores();
    if (repetition_count[board.position_key()] == 0) {
        repetition_count[board.position_key()] = 1;
    }

    bool completed = false;
    const Move best_move = search_root(board, search_depth, completed, repetition_count);
    last_completed_depth = completed ? search_depth : 0;
    last_node_stats = current_node_stats;
    return best_move;
}

Move Minimax::find_best_move(const ChessBoard& board) {
    std::unordered_map<std::uint64_t, int> repetition_count;
    repetition_count[board.position_key()] = 1;
    return find_best_move(board, std::move(repetition_count));
}

Move Minimax::find_best_move(const ChessBoard& board,
                             std::unordered_map<std::uint64_t, int> repetition_count) {
    return find_best_move(board, std::move(repetition_count), depth);
}

Move Minimax::find_best_move_timed(const ChessBoard& board,
                                   int time_limit_ms,
                                   std::unordered_map<std::uint64_t, int> repetition_count) {
    last_completed_depth = 0;
    reset_node_stats();
    clear_killer_moves();
    clear_history_scores();
    std::vector<Move> moves = board.generate_moves(board.turn);
    if (moves.empty()) {
        return Move(-1, -1);
    }

    if (repetition_count[board.position_key()] == 0) {
        repetition_count[board.position_key()] = 1;
    }

    order_moves(board, moves);
    Move best_move = moves[0];

    if (time_limit_ms <= 0) {
        return best_move;
    }

    const int max_depth = std::max(1, depth);
    deadline = Clock::now() + std::chrono::milliseconds(time_limit_ms);
    use_time_limit = true;
    stop_search = false;

    for (int current_search_depth = 1; current_search_depth <= max_depth; ++current_search_depth) {
        if (Clock::now() >= deadline) {
            break;
        }

        stop_search = false;
        current_node_stats = {};
        bool completed = false;
        const Move candidate = search_root(board, current_search_depth, completed, repetition_count);
        if (!completed) {
            break;
        }
        best_move = candidate;
        last_completed_depth = current_search_depth;
        last_node_stats = current_node_stats;
    }

    use_time_limit = false;
    stop_search = false;
    return best_move;
}
