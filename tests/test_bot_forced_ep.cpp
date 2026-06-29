#include "chess/bot.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    const ChessBoard board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const std::vector<Move> legal_moves = board.generate_moves(board.turn);
    int en_passant_count = 0;
    for (const Move& move : legal_moves) {
        const Piece moving_piece = board.piece_at(move.from);
        if ((moving_piece == W_PAWN || moving_piece == B_PAWN) &&
            move.to == board.en_passant_square &&
            board.is_empty(move.to)) {
            ++en_passant_count;
        }
    }
    assert(legal_moves.size() > 1);
    assert(en_passant_count == 1);

    Bot bot(3);
    const Move untimed = bot.choose_move(board, bot.depth, 0);
    assert(board.valid_move(untimed, board.turn));
    assert(bot.get_last_search_completed_depth() == 3);
    assert(bot.get_last_move_debug().find("decision=search") != std::string::npos);

    Bot timed_bot(3);
    const Move timed = timed_bot.choose_move(board, 64, 5000);
    assert(board.valid_move(timed, board.turn));
    assert(timed_bot.get_last_search_completed_depth() > 0);
    assert(timed_bot.get_last_move_debug().find("decision=search") != std::string::npos);

    return 0;
}
