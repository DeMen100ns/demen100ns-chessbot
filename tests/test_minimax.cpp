#include "chess/minimax.h"

#include <cassert>

int main() {
    ChessBoard board;
    board.initialize();

    Minimax ai(2);
    const Move move = ai.find_best_move(board, ai.depth, 0);

    assert(move.from >= 0);
    assert(move.to >= 0);
    assert(board.valid_move(move, board.turn));
    return 0;
}
