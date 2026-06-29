#include "chess/chessboard.h"

#include "chessboard_internal.h"

#include <array>

namespace {

using PieceSquareKeys = std::array<std::array<std::uint64_t, 64>, 13>;

const PieceSquareKeys& zobrist_piece_keys() {
    static const PieceSquareKeys keys = [] {
        PieceSquareKeys generated{};
        std::uint64_t seed = 0x4d595df4d0f33173ULL;
        for (auto& by_square : generated) {
            for (std::uint64_t& key : by_square) {
                seed = splitmix64(seed);
                key = seed;
            }
        }
        return generated;
    }();
    return keys;
}

const std::array<std::uint64_t, 4>& zobrist_castling_keys() {
    static const std::array<std::uint64_t, 4> keys = [] {
        std::array<std::uint64_t, 4> generated{};
        std::uint64_t seed = 0x9b9773e99e3779b9ULL;
        for (std::uint64_t& key : generated) {
            seed = splitmix64(seed);
            key = seed;
        }
        return generated;
    }();
    return keys;
}

const std::array<std::uint64_t, 8>& zobrist_en_passant_keys() {
    static const std::array<std::uint64_t, 8> keys = [] {
        std::array<std::uint64_t, 8> generated{};
        std::uint64_t seed = 0x243f6a8885a308d3ULL;
        for (std::uint64_t& key : generated) {
            seed = splitmix64(seed);
            key = seed;
        }
        return generated;
    }();
    return keys;
}

}  // namespace

std::uint64_t zobrist_side_to_move_key() {
    static const std::uint64_t key = splitmix64(0xa4093822299f31d0ULL);
    return key;
}

std::uint64_t piece_square_key(Piece piece, int square) {
    return zobrist_piece_keys()[static_cast<std::size_t>(piece)][static_cast<std::size_t>(square)];
}

void xor_castling_rights(std::uint64_t& key, const ChessBoard& board) {
    const auto& castle_keys = zobrist_castling_keys();
    if (board.white_can_castle_kingside) {
        key ^= castle_keys[0];
    }
    if (board.white_can_castle_queenside) {
        key ^= castle_keys[1];
    }
    if (board.black_can_castle_kingside) {
        key ^= castle_keys[2];
    }
    if (board.black_can_castle_queenside) {
        key ^= castle_keys[3];
    }
}

void xor_en_passant_file(std::uint64_t& key, const ChessBoard& board) {
    if (has_en_passant_capture(board)) {
        key ^= zobrist_en_passant_keys()[static_cast<std::size_t>(col_of(board.en_passant_square))];
    }
}

std::uint64_t compute_position_key_full(const ChessBoard& board) {
    std::uint64_t key = 0;

    for (int piece = W_PAWN; piece <= B_KING; ++piece) {
        Bitboard pieces = board.piece_bitboards[static_cast<std::size_t>(piece)];
        while (pieces != 0) {
            key ^= piece_square_key(static_cast<Piece>(piece), pop_lsb(pieces));
        }
    }

    if (board.turn == BLACK) {
        key ^= zobrist_side_to_move_key();
    }
    xor_castling_rights(key, board);
    xor_en_passant_file(key, board);
    return key;
}

bool has_en_passant_capture(const ChessBoard& board_state) {
    if (board_state.en_passant_square < 0) {
        return false;
    }

    const Piece pawn = board_state.turn == WHITE ? W_PAWN : B_PAWN;
    const Bitboard target = square_bb(board_state.en_passant_square);
    return (pawn_attacks(board_state.piece_bitboard(pawn), board_state.turn) & target) != 0;
}

int ChessBoard::count_pieces() const {
    return __builtin_popcountll(occupied);
}

std::uint64_t ChessBoard::position_key() const {
    return zobrist_key;
}
