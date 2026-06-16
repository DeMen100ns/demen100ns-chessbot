#include "chessboard.h"

#include "chess/attacks.h"
#include "chessboard_internal.h"

bool ChessBoard::is_square_attacked(int square, Color by_color) const {
    const Bitboard target = square_bb(square);
    const Piece pawn = by_color == WHITE ? W_PAWN : B_PAWN;
    const Piece knight = by_color == WHITE ? W_KNIGHT : B_KNIGHT;
    const Piece bishop = by_color == WHITE ? W_BISHOP : B_BISHOP;
    const Piece rook = by_color == WHITE ? W_ROOK : B_ROOK;
    const Piece queen = by_color == WHITE ? W_QUEEN : B_QUEEN;
    const Piece king = by_color == WHITE ? W_KING : B_KING;

    if (pawn_attacks(piece_bitboard(pawn), by_color) & target) {
        return true;
    }
    if (knight_attacks(square) & piece_bitboard(knight)) {
        return true;
    }
    if (king_attacks(square) & piece_bitboard(king)) {
        return true;
    }

    const Bitboard bishops_and_queens = piece_bitboard(bishop) | piece_bitboard(queen);
    if (bishop_attacks(square, occupied) & bishops_and_queens) {
        return true;
    }

    const Bitboard rooks_and_queens = piece_bitboard(rook) | piece_bitboard(queen);
    return (rook_attacks(square, occupied) & rooks_and_queens) != 0;
}

bool ChessBoard::is_in_check(Color color) const {
    const int king_square = find_king_square(*this, color);
    if (king_square == -1) {
        return false;
    }
    return is_square_attacked(king_square, opposite_color(color));
}

bool ChessBoard::is_checkmate(Color color) const {
    return is_in_check(color) && generate_moves(color).empty();
}

bool ChessBoard::is_stalemate(Color color) const {
    return !is_in_check(color) && generate_moves(color).empty();
}
