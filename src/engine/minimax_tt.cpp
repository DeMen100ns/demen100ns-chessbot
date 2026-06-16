#include "minimax_internal.h"

#include <algorithm>

void Minimax::clear_killer_moves() {
    for (auto& killer_slot : killer_moves) {
        killer_slot[0] = std::nullopt;
        killer_slot[1] = std::nullopt;
    }
}

void Minimax::clear_history_scores() {
    for (auto& by_from : history_scores) {
        for (auto& by_to : by_from) {
            by_to.fill(0);
        }
    }
}

bool Minimax::is_killer_move(const Move& move, int current_depth) const {
    if (current_depth < 0 || current_depth >= static_cast<int>(killer_moves.size())) {
        return false;
    }

    const auto& killer_slot = killer_moves[static_cast<std::size_t>(current_depth)];
    return (killer_slot[0].has_value() && MinimaxInternal::same_move(move, *killer_slot[0])) ||
           (killer_slot[1].has_value() && MinimaxInternal::same_move(move, *killer_slot[1]));
}

void Minimax::store_killer_move(const Move& move, int current_depth) {
    if (current_depth < 0 ||
        current_depth >= static_cast<int>(killer_moves.size()) ||
        is_killer_move(move, current_depth)) {
        return;
    }

    auto& killer_slot = killer_moves[static_cast<std::size_t>(current_depth)];
    killer_slot[1] = killer_slot[0];
    killer_slot[0] = move;
}

void Minimax::store_history_score(const ChessBoard& board,
                                  const Move& move,
                                  int searched_depth) {
    const int clamped_depth = std::max(1, searched_depth);
    const int bonus = clamped_depth * clamped_depth;
    int& score = history_scores[static_cast<std::size_t>(board.turn)]
                               [static_cast<std::size_t>(move.from)]
                               [static_cast<std::size_t>(move.to)];
    score = std::min(score + bonus, 1000000);
}

bool Minimax::can_use_transposition(
    const ChessBoard& board,
    const std::unordered_map<std::uint64_t, int>& repetition_count) const {
    const auto repetition_it = repetition_count.find(board.position_key());
    return repetition_it == repetition_count.end() || repetition_it->second <= 1;
}

std::optional<Minimax::TTEntry> Minimax::probe_transposition(const ChessBoard& board,
                                                             int required_depth) const {
    const auto tt_it = transposition_table.find(board.position_key());
    if (tt_it == transposition_table.end() || tt_it->second.depth < required_depth) {
        return std::nullopt;
    }

    return tt_it->second;
}

void Minimax::store_transposition(const ChessBoard& board,
                                  int searched_depth,
                                  int score,
                                  TTFlag flag,
                                  const Move& best_move) {
    const std::uint64_t key = board.position_key();
    const TTEntry new_entry{score, searched_depth, flag, best_move};
    const auto tt_it = transposition_table.find(key);
    if (tt_it == transposition_table.end()) {
        transposition_table.emplace(key, new_entry);
        return;
    }

    const TTEntry& old_entry = tt_it->second;
    const bool prefer_new_entry =
        searched_depth > old_entry.depth ||
        (searched_depth == old_entry.depth &&
         flag == TTFlag::Exact &&
         old_entry.flag != TTFlag::Exact);
    if (prefer_new_entry) {
        tt_it->second = new_entry;
    }
}

void Minimax::clear_transposition_table() {
    transposition_table.clear();
}
