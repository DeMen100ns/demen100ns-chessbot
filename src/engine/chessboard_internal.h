#pragma once

#include "chess/attacks.h"
#include "chess/chessboard.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

constexpr Bitboard kFileAMask = 0x0101010101010101ULL;
constexpr Bitboard kFileHMask = 0x8080808080808080ULL;
constexpr Bitboard kRank2Mask = 0x000000000000FF00ULL;
constexpr Bitboard kRank7Mask = 0x00FF000000000000ULL;
constexpr Bitboard kWhiteCastleKingsideEmpty = (1ULL << 5) | (1ULL << 6);
constexpr Bitboard kWhiteCastleQueensideEmpty = (1ULL << 1) | (1ULL << 2) | (1ULL << 3);
constexpr Bitboard kBlackCastleKingsideEmpty = (1ULL << 61) | (1ULL << 62);
constexpr Bitboard kBlackCastleQueensideEmpty = (1ULL << 57) | (1ULL << 58) | (1ULL << 59);

inline bool is_white_piece(Piece piece) {
    return piece >= W_PAWN && piece <= W_KING;
}

inline bool is_black_piece(Piece piece) {
    return piece >= B_PAWN && piece <= B_KING;
}

inline bool is_piece_of_color(Piece piece, Color color) {
    return color == WHITE ? is_white_piece(piece) : is_black_piece(piece);
}

inline bool is_enemy_piece(Piece piece, Color color) {
    return piece != EMPTY && !is_piece_of_color(piece, color);
}

inline Color opposite_color(Color color) {
    return color == WHITE ? BLACK : WHITE;
}

constexpr int row_of(int square) {
    return square / 8;
}

constexpr int col_of(int square) {
    return square % 8;
}

inline int abs_int(int value) {
    return value < 0 ? -value : value;
}

constexpr Bitboard square_bb(int square) {
    return 1ULL << square;
}

inline int pop_lsb(Bitboard& bitboard) {
    const int square = __builtin_ctzll(bitboard);
    bitboard &= bitboard - 1;
    return square;
}

struct MoveList {
    static constexpr std::size_t kCapacity = 256;

    std::array<Move, kCapacity> moves;
    std::size_t count = 0;

    void clear() { count = 0; }
    void truncate(std::size_t new_count) {
        assert(new_count <= count);
        count = new_count;
    }
    bool empty() const { return count == 0; }
    std::size_t size() const { return count; }

    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }

    Move& operator[](std::size_t index) {
        assert(index < count);
        return moves[index];
    }

    const Move& operator[](std::size_t index) const {
        assert(index < count);
        return moves[index];
    }

    void push_back(const Move& move) {
        assert(count < kCapacity);
        moves[count++] = move;
    }

    void emplace_back(int from, int to, Piece promotion = EMPTY) {
        push_back(Move(from, to, promotion));
    }

    std::vector<Move> to_vector() const {
        return std::vector<Move>(begin(), end());
    }
};

std::uint64_t splitmix64(std::uint64_t value);
std::uint64_t piece_square_key(Piece piece, int square);
std::uint64_t zobrist_side_to_move_key();
void xor_castling_rights(std::uint64_t& key, const ChessBoard& board);
void xor_en_passant_file(std::uint64_t& key, const ChessBoard& board);
std::uint64_t compute_position_key_full(const ChessBoard& board);
Piece piece_from_fen_char(char symbol);
int square_from_algebraic(const std::string& square);
int find_king_square(const ChessBoard& board, Color color);
bool is_checking_move_fast(const ChessBoard& board, const Move& move);
bool has_en_passant_capture(const ChessBoard& board_state);
void generate_pseudo_moves_into(const ChessBoard& board, Color color, MoveList& moves);
void generate_moves_into(const ChessBoard& board, Color color, MoveList& moves);
void generate_evasions_into(const ChessBoard& board, Color color, MoveList& moves);
void generate_quiescence_moves_into(const ChessBoard& board,
                                    Color color,
                                    bool include_quiet_checks,
                                    MoveList& moves);
