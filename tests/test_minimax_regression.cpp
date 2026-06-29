#include "chess/chessboard.h"
#include "chess/io.h"
#include "chess/minimax.h"

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
        const int nnue_eval1 = eval_engine.evaluate_nnue(board);
        const int nnue_eval2 = eval_engine.evaluate_nnue(board);
        assert(nnue_eval1 == nnue_eval2);

        (void)eval_engine.evaluate_nnue(board);
        const std::vector<Move> legal_moves = board.generate_moves(board.turn);
        for (std::size_t move_index = 0;
             move_index < legal_moves.size() && move_index < 16;
             ++move_index) {
            const ChessBoard accumulated = board.make_move(legal_moves[move_index]);
            const int accumulated_eval = eval_engine.evaluate_nnue(accumulated);
            const ChessBoard rebuilt(ChessIO::board_to_fen(accumulated));
            const int rebuilt_eval = eval_engine.evaluate_nnue(rebuilt);
            assert(accumulated_eval == rebuilt_eval);
        }

        for (int depth = 1; depth <= 4; ++depth) {
            Minimax engine(depth);
            const Move move = engine.find_best_move(board, depth, 0);
            assert(board.valid_move(move, board.turn));
        }
    }

    const ChessBoard start_white("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const ChessBoard start_black("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
    Minimax eval_engine(2);
    assert(eval_engine.evaluate_nnue(start_white) == eval_engine.evaluate_nnue(start_white));
    assert(eval_engine.evaluate_nnue(start_black) == eval_engine.evaluate_nnue(start_black));

    return 0;
}
