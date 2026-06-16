#pragma once

#include <cstdint>

enum Color : std::uint8_t {
    WHITE = 0,
    BLACK = 1
};

enum Piece : std::uint8_t {
    EMPTY = 0,

    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING
};

namespace Chess {
constexpr int KNIGHT_OFFSETS[8] = {17, 10, -6, -15, -17, -10, 6, 15};
constexpr int KING_OFFSETS[8] = {8, 9, 1, -7, -8, -9, -1, 7};
constexpr int BISHOP_OFFSETS[4] = {9, 7, -7, -9};
constexpr int ROOK_OFFSETS[4] = {8, 1, -8, -1};
constexpr int QUEEN_OFFSETS[8] = {8, 9, 1, -7, -8, -9, -1, 7};
constexpr int MAX_SCORE = 1000000000;
}  // namespace Chess

struct Move {
    int from;
    int to;
    Piece promotion;

    Move(int f, int t, Piece p = EMPTY) : from(f), to(t), promotion(p) {}
};
