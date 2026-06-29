#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "chess/chessboard.h"

#define private public
#include "chess/minimax.h"
#undef private

#include <cassert>

int main() {
    const ChessBoard board("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    const Move best_move(4, 12);
    Minimax engine(1);

    const int quiet_score = 1234;
    engine.store_transposition(
        board, 3, quiet_score, Minimax::TTFlag::Exact, best_move, 6);
    const auto quiet_probe = engine.probe_transposition(board, 3, 0);
    assert(quiet_probe.has_value());
    assert(quiet_probe->score == quiet_score);

    engine.clear_transposition_table();
    const int positive_mate_at_ply_6 = Chess::MAX_SCORE - 6;
    engine.store_transposition(
        board, 3, positive_mate_at_ply_6, Minimax::TTFlag::Exact, best_move, 6);
    const auto positive_same_ply = engine.probe_transposition(board, 3, 6);
    const auto positive_root_ply = engine.probe_transposition(board, 3, 0);
    assert(positive_same_ply.has_value());
    assert(positive_root_ply.has_value());
    assert(positive_same_ply->score == positive_mate_at_ply_6);
    assert(positive_root_ply->score == Chess::MAX_SCORE);

    engine.clear_transposition_table();
    const int negative_mate_at_ply_7 = -Chess::MAX_SCORE + 7;
    engine.store_transposition(
        board, 3, negative_mate_at_ply_7, Minimax::TTFlag::Exact, best_move, 7);
    const auto negative_same_ply = engine.probe_transposition(board, 3, 7);
    const auto negative_ply_2 = engine.probe_transposition(board, 3, 2);
    assert(negative_same_ply.has_value());
    assert(negative_ply_2.has_value());
    assert(negative_same_ply->score == negative_mate_at_ply_7);
    assert(negative_ply_2->score == -Chess::MAX_SCORE + 2);

    return 0;
}
