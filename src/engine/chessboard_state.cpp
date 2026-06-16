#include "chessboard.h"

#include "chessboard_internal.h"

void ChessBoard::clear_square(int square) {
    Piece& square_entry = square_piece[static_cast<std::size_t>(square)];
    const Piece piece = square_entry;
    if (piece == EMPTY) {
        return;
    }

    const Bitboard bit = square_bb(square);
    square_entry = EMPTY;
    piece_bitboards[static_cast<std::size_t>(piece)] &= ~bit;
    if (is_white_piece(piece)) {
        color_bitboards[WHITE] &= ~bit;
    } else {
        color_bitboards[BLACK] &= ~bit;
    }
    occupied &= ~bit;
}

void ChessBoard::set_square(int square, Piece piece) {
    clear_square(square);
    if (piece == EMPTY) {
        return;
    }

    const Bitboard bit = square_bb(square);
    square_piece[static_cast<std::size_t>(square)] = piece;
    piece_bitboards[static_cast<std::size_t>(piece)] |= bit;
    if (is_white_piece(piece)) {
        color_bitboards[WHITE] |= bit;
    } else {
        color_bitboards[BLACK] |= bit;
    }
    occupied |= bit;
}

void ChessBoard::initialize() {
    square_piece.fill(EMPTY);
    piece_bitboards.fill(0);
    color_bitboards.fill(0);
    occupied = 0;
    zobrist_key = 0;
    turn = WHITE;
    turn_number = 1;
    halfmove_clock = 0;
    white_can_castle_kingside = true;
    white_can_castle_queenside = true;
    black_can_castle_kingside = true;
    black_can_castle_queenside = true;
    en_passant_square = -1;

    set_square(0, W_ROOK);
    set_square(1, W_KNIGHT);
    set_square(2, W_BISHOP);
    set_square(3, W_QUEEN);
    set_square(4, W_KING);
    set_square(5, W_BISHOP);
    set_square(6, W_KNIGHT);
    set_square(7, W_ROOK);
    for (int i = 8; i < 16; ++i) {
        set_square(i, W_PAWN);
    }

    set_square(56, B_ROOK);
    set_square(57, B_KNIGHT);
    set_square(58, B_BISHOP);
    set_square(59, B_QUEEN);
    set_square(60, B_KING);
    set_square(61, B_BISHOP);
    set_square(62, B_KNIGHT);
    set_square(63, B_ROOK);
    for (int i = 48; i < 56; ++i) {
        set_square(i, B_PAWN);
    }

    zobrist_key = compute_position_key_full(*this);
}

std::uint64_t ChessBoard::piece_bitboard(Piece piece) const {
    return piece_bitboards[static_cast<std::size_t>(piece)];
}

std::uint64_t ChessBoard::color_bitboard(Color color) const {
    return color_bitboards[static_cast<std::size_t>(color)];
}
