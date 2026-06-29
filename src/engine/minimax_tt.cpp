#include "minimax_internal.h"

#include <algorithm>

namespace {

constexpr int kMateScoreWindow = 100000;

bool is_positive_mate_score(int score) {
    return score > Chess::MAX_SCORE - kMateScoreWindow;
}

bool is_negative_mate_score(int score) {
    return score < -Chess::MAX_SCORE + kMateScoreWindow;
}

int score_to_transposition(int score, int current_depth) {
    if (is_positive_mate_score(score)) {
        return score + current_depth;
    }
    if (is_negative_mate_score(score)) {
        return score - current_depth;
    }
    return score;
}

int score_from_transposition(int score, int current_depth) {
    if (is_positive_mate_score(score)) {
        return score - current_depth;
    }
    if (is_negative_mate_score(score)) {
        return score + current_depth;
    }
    return score;
}

}  // namespace

void Minimax::clear_killer_moves() {
    for (auto& killer_slot : killer_moves) {
        killer_slot[0] = std::nullopt;
        killer_slot[1] = std::nullopt;
    }
}

void Minimax::clear_counter_moves() {
    for (auto& by_from : counter_moves) {
        for (auto& by_to : by_from) {
            for (auto& entry : by_to) {
                entry = std::nullopt;
            }
        }
    }
}

void Minimax::clear_history_scores() {
    for (auto& by_from : history_scores) {
        for (auto& by_to : by_from) {
            by_to.fill(0);
        }
    }
}

std::optional<Move> Minimax::get_counter_move(Color side,
                                              const std::optional<Move>& previous_move) const {
    if (!previous_move.has_value()) {
        return std::nullopt;
    }

    return counter_moves[static_cast<std::size_t>(side)]
                        [static_cast<std::size_t>(previous_move->from)]
                        [static_cast<std::size_t>(previous_move->to)];
}

void Minimax::store_counter_move(Color side,
                                 const std::optional<Move>& previous_move,
                                 const Move& move) {
    if (!previous_move.has_value()) {
        return;
    }

    counter_moves[static_cast<std::size_t>(side)]
                 [static_cast<std::size_t>(previous_move->from)]
                 [static_cast<std::size_t>(previous_move->to)] = move;
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
    const std::vector<std::uint64_t>& repetition_history) const {
    const std::uint64_t current_key = board.position_key();
    int repetition_count = 0;
    const int last_index = static_cast<int>(repetition_history.size()) - 1;
    const int first_reversible_index = std::max(0, last_index - board.halfmove_clock);
    for (int index = last_index; index >= first_reversible_index; index -= 2) {
        if (repetition_history[static_cast<std::size_t>(index)] == current_key) {
            ++repetition_count;
        }
    }
    return repetition_count <= 1;
}

std::optional<Minimax::TTEntry> Minimax::probe_transposition(const ChessBoard& board,
                                                             int required_depth,
                                                             int current_depth) const {
    if (transposition_table.empty()) {
        return std::nullopt;
    }

    const std::uint64_t key = board.position_key();
    const std::size_t index =
        static_cast<std::size_t>(key & (transposition_table.size() - 1));
    const TTEntry& entry = transposition_table[index];
    if (!entry.occupied || entry.key != key || entry.depth < required_depth) {
        return std::nullopt;
    }

    TTEntry adjusted = entry;
    adjusted.score = score_from_transposition(adjusted.score, current_depth);
    return adjusted;
}

void Minimax::store_transposition(const ChessBoard& board,
                                  int searched_depth,
                                  int score,
                                  TTFlag flag,
                                  const Move& best_move,
                                  int current_depth) {
    if (transposition_table.empty()) {
        return;
    }

    const std::uint64_t key = board.position_key();
    const std::size_t index =
        static_cast<std::size_t>(key & (transposition_table.size() - 1));
    TTEntry& old_entry = transposition_table[index];

    TTEntry new_entry;
    new_entry.key = key;
    new_entry.score = score_to_transposition(score, current_depth);
    new_entry.depth = searched_depth;
    new_entry.flag = flag;
    new_entry.best_move = best_move;
    new_entry.occupied = true;
    new_entry.age = transposition_age;

    if (!old_entry.occupied) {
        old_entry = new_entry;
        return;
    }

    const bool same_position = old_entry.key == key;
    const bool prefer_new_entry = same_position
        ? (searched_depth > old_entry.depth ||
           (searched_depth == old_entry.depth &&
            flag == TTFlag::Exact &&
            old_entry.flag != TTFlag::Exact))
        : (old_entry.age != transposition_age ||
           searched_depth >= old_entry.depth ||
           flag == TTFlag::Exact);
    if (prefer_new_entry) {
        old_entry = new_entry;
    }
}

void Minimax::advance_transposition_age() {
    ++transposition_age;
    if (transposition_age == 0) {
        transposition_age = 1;
    }
}

void Minimax::clear_eval_cache() {
    pawn_eval_cache.clear();
}

void Minimax::clear_transposition_table() {
    for (TTEntry& entry : transposition_table) {
        entry = TTEntry{};
    }
}
