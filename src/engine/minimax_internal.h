#pragma once

#include "chess/attacks.h"
#include "chess/minimax.h"
#include "chessboard_internal.h"

#include <array>
#include <optional>
#include <vector>

namespace MinimaxInternal {

using KillerSlot = std::array<std::optional<Move>, 2>;
using HistoryForSide = std::array<std::array<int, 64>, 64>;

constexpr Bitboard kFileBMask = 0x0202020202020202ULL;
constexpr Bitboard kFileCMask = 0x0404040404040404ULL;
constexpr Bitboard kFileDMask = 0x0808080808080808ULL;
constexpr Bitboard kFileEMask = 0x1010101010101010ULL;
constexpr Bitboard kFileFMask = 0x2020202020202020ULL;
constexpr Bitboard kFileGMask = 0x4040404040404040ULL;
inline constexpr std::array<Bitboard, 8> kFileMasks = {
    kFileAMask, kFileBMask, kFileCMask, kFileDMask,
    kFileEMask, kFileFMask, kFileGMask, kFileHMask,
};

struct EvalTerm {
    int mg = 0;
    int eg = 0;
};

constexpr int kDeltaPruningMargin = 200;
constexpr int kQuietCheckDeltaMargin = 900;
constexpr int kMaxQuiescenceCheckPlies = 2;

constexpr int mate_score(int ply) {
    return Chess::MAX_SCORE - ply;
}

constexpr int flip_square(int square) {
    return square ^ 56;
}

int piece_type_index(Piece piece);
Color piece_color(Piece piece);
int piece_value(Piece piece);

int capture_value(const ChessBoard& board, const Move& move);
int mvv_lva_score(const ChessBoard& board, const Move& move);
bool is_capture_move(const ChessBoard& board, const Move& move);
bool is_en_passant_move(const ChessBoard& board, const Move& move);
int static_exchange_eval(const ChessBoard& board, const Move& move);
bool is_checking_move(const ChessBoard& board, const Move& move);
bool same_move(const Move& lhs, const Move& rhs);
bool is_tactical_move(const ChessBoard& board, const Move& move);

void order_moves(const ChessBoard& board, std::vector<Move>& moves);
void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const KillerSlot& killer_slot,
                 const HistoryForSide& history_for_side);
void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const std::optional<Move>& tt_move);
void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const std::optional<Move>& tt_move,
                 const KillerSlot& killer_slot,
                 const HistoryForSide& history_for_side);

}  // namespace MinimaxInternal
