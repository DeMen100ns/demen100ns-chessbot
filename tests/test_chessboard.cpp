#include "chess/chessboard.h"

#include <cassert>

int main() {
    ChessBoard board;
    board.initialize();

    assert(board.generate_moves(WHITE).size() == 20);
    assert(board.generate_moves(BLACK).size() == 20);

    ChessBoard mate("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    assert(mate.is_checkmate(BLACK));
    assert(!mate.is_stalemate(BLACK));

    ChessBoard stale("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    assert(!stale.is_checkmate(BLACK));
    assert(stale.is_stalemate(BLACK));

    return 0;
}
