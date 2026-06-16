#pragma once

#include "chess/types.h"

#include <cstdint>

using Bitboard = std::uint64_t;

Bitboard knight_attacks(int square);
Bitboard king_attacks(int square);
Bitboard pawn_attacks(Bitboard pawns, Color color);
Bitboard bishop_attacks(int square, Bitboard occupied);
Bitboard rook_attacks(int square, Bitboard occupied);
Bitboard queen_attacks(int square, Bitboard occupied);
