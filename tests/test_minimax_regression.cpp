#include "chessboard.h"
#include "minimax.h"

#include <array>
#include <cassert>

int main() {
    const std::array<const char*, 6> kFens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pp1ppppp/8/2p5/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "3r2k1/5ppp/8/3Pp3/8/8/5PPP/3R2K1 w - e6 0 1",
        "3r4/5QBk/Pqr3p1/1N3pPp/1P2bP1P/8/3R4/R4K2 b - - 0 1",
        "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1",
    };

    for (const char* fen : kFens) {
        const ChessBoard board(fen);
        Minimax eval_engine(2);
        const int eval1 = eval_engine.evaluate(board);
        const int eval2 = eval_engine.evaluate(board);
        assert(eval1 == eval2);

        for (int depth = 1; depth <= 4; ++depth) {
            Minimax engine(depth);
            const Move move = engine.find_best_move(board, depth);
            assert(board.valid_move(move, board.turn));
        }
    }

    return 0;
}
