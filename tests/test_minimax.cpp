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

    ChessBoard terminal_checkmate("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    Minimax mate_ai(3);
    const Move mate_move = mate_ai.find_best_move(terminal_checkmate, 3, 0);
    assert(mate_move.from == -1);
    assert(mate_move.to == -1);
    assert(mate_ai.get_last_search_eval() == -Chess::MAX_SCORE);
    assert(mate_ai.get_last_completed_depth() == 0);

    ChessBoard terminal_stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    Minimax stalemate_ai(3);
    const Move stalemate_move = stalemate_ai.find_best_move(terminal_stalemate, 3, 0);
    assert(stalemate_move.from == -1);
    assert(stalemate_move.to == -1);
    assert(stalemate_ai.get_last_search_eval() == 0);
    assert(stalemate_ai.get_last_completed_depth() == 0);

    ChessBoard fifty_move_draw("4k3/8/8/8/8/8/8/R3K3 w - - 100 1");
    Minimax fifty_move_ai(3);
    const Move fifty_move = fifty_move_ai.find_best_move(fifty_move_draw, 3, 0);
    assert(fifty_move_draw.valid_move(fifty_move, fifty_move_draw.turn));
    assert(fifty_move_ai.get_last_search_eval() == 0);
    assert(fifty_move_ai.get_last_completed_depth() == 0);

    ChessBoard quiet_move_reaches_fifty("4k3/8/8/8/8/8/8/R3K3 w - - 99 1");
    Minimax quiet_ai(1);
    const Move quiet_move = quiet_ai.find_best_move(quiet_move_reaches_fifty, 1, 0);
    assert(quiet_move_reaches_fifty.valid_move(quiet_move, quiet_move_reaches_fifty.turn));
    assert(quiet_ai.get_last_search_eval() == 0);
    assert(quiet_ai.get_last_completed_depth() == 1);

    ChessBoard insufficient_material("4k3/8/8/8/8/8/8/4KB2 w - - 0 1");
    Minimax insufficient_ai(3);
    const Move insufficient_move =
        insufficient_ai.find_best_move(insufficient_material, 3, 0);
    assert(insufficient_material.valid_move(insufficient_move, insufficient_material.turn));
    assert(insufficient_ai.get_last_search_eval() == 0);
    assert(insufficient_ai.get_last_completed_depth() == 0);

    return 0;
}
