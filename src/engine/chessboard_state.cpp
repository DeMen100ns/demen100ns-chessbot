#include "chess/chessboard.h"

#include "chess/nnue_basic_weights.h"
#include "chessboard_internal.h"

namespace {

constexpr int kPieceFeatureCount = 2 * 6 * 64;
constexpr int kWhiteToMoveFeatureIndex = kPieceFeatureCount;
constexpr int kBlackToMoveFeatureIndex = kPieceFeatureCount + 1;

int nnue_piece_type(Piece piece) {
    switch (piece) {
        case W_PAWN:
        case B_PAWN:
            return 0;
        case W_KNIGHT:
        case B_KNIGHT:
            return 1;
        case W_BISHOP:
        case B_BISHOP:
            return 2;
        case W_ROOK:
        case B_ROOK:
            return 3;
        case W_QUEEN:
        case B_QUEEN:
            return 4;
        case W_KING:
        case B_KING:
            return 5;
        case EMPTY:
            return -1;
    }

    return -1;
}

int nnue_feature_index(Piece piece, int square) {
    const Color color = is_white_piece(piece) ? WHITE : BLACK;
    return static_cast<int>(color) * 6 * 64 + nnue_piece_type(piece) * 64 + square;
}

void update_nnue_accumulator_feature(ChessBoard& board, Piece piece, int square, float scale) {
    if (!board.nnue_accumulator_valid || piece == EMPTY) {
        return;
    }

    const int feature_index = nnue_feature_index(piece, square);
    if (feature_index >= ChessNnueWeights::kInputFeatures) {
        return;
    }

    const float* feature_weights =
        ChessNnueWeights::kFc1WeightByFeature[feature_index];
    for (int hidden_index = 0; hidden_index < ChessNnueWeights::kHiddenSize; ++hidden_index) {
        board.nnue_accumulator[static_cast<std::size_t>(hidden_index)] +=
            scale * feature_weights[hidden_index];
    }
}

void update_nnue_accumulator_turn_feature(ChessBoard& board, Color color, float scale) {
    if (!board.nnue_accumulator_valid) {
        return;
    }

    const int feature_index =
        color == WHITE ? kWhiteToMoveFeatureIndex : kBlackToMoveFeatureIndex;
    if (feature_index >= ChessNnueWeights::kInputFeatures) {
        return;
    }

    const float* feature_weights = ChessNnueWeights::kFc1WeightByFeature[feature_index];
    for (int hidden_index = 0; hidden_index < ChessNnueWeights::kHiddenSize; ++hidden_index) {
        board.nnue_accumulator[static_cast<std::size_t>(hidden_index)] +=
            scale * feature_weights[hidden_index];
    }
}

}  // namespace

void ChessBoard::clear_square(int square) {
    Piece& square_entry = square_piece[static_cast<std::size_t>(square)];
    const Piece piece = square_entry;
    if (piece == EMPTY) {
        return;
    }

    update_nnue_accumulator_feature(*this, piece, square, -1.0F);

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

    update_nnue_accumulator_feature(*this, piece, square, 1.0F);

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

void ChessBoard::set_turn(Color color) {
    if (turn == color) {
        return;
    }

    update_nnue_accumulator_turn_feature(*this, turn, -1.0F);
    turn = color;
    update_nnue_accumulator_turn_feature(*this, turn, 1.0F);
}

void ChessBoard::initialize() {
    square_piece.fill(EMPTY);
    piece_bitboards.fill(0);
    color_bitboards.fill(0);
    occupied = 0;
    nnue_accumulator.fill(0.0F);
    nnue_accumulator_valid = false;
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
