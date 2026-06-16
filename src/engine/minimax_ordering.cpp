#include "minimax_internal.h"

#include <algorithm>
#include <array>

namespace {

using namespace MinimaxInternal;
using PieceBitboards = std::array<Bitboard, 13>;

struct ScoredMove {
    Move move;
    int score;
};

struct AttackerInfo {
    Piece piece;
    int square;
    int value;
};

Bitboard attackers_to_square(const PieceBitboards& piece_bitboards,
                             Bitboard occupied,
                             int target,
                             Color side) {
    const Bitboard target_bb = square_bb(target);
    const Piece pawn = side == WHITE ? W_PAWN : B_PAWN;
    const Piece knight = side == WHITE ? W_KNIGHT : B_KNIGHT;
    const Piece bishop = side == WHITE ? W_BISHOP : B_BISHOP;
    const Piece rook = side == WHITE ? W_ROOK : B_ROOK;
    const Piece queen = side == WHITE ? W_QUEEN : B_QUEEN;
    const Piece king = side == WHITE ? W_KING : B_KING;

    Bitboard attackers = 0;
    attackers |= pawn_attacks(target_bb, opposite_color(side)) &
                 piece_bitboards[static_cast<std::size_t>(pawn)];
    attackers |= knight_attacks(target) & piece_bitboards[static_cast<std::size_t>(knight)];
    attackers |= king_attacks(target) & piece_bitboards[static_cast<std::size_t>(king)];
    attackers |= bishop_attacks(target, occupied) &
                 (piece_bitboards[static_cast<std::size_t>(bishop)] |
                  piece_bitboards[static_cast<std::size_t>(queen)]);
    attackers |= rook_attacks(target, occupied) &
                 (piece_bitboards[static_cast<std::size_t>(rook)] |
                  piece_bitboards[static_cast<std::size_t>(queen)]);
    return attackers;
}

std::optional<AttackerInfo> least_valuable_attacker(const PieceBitboards& piece_bitboards,
                                                    Bitboard occupied,
                                                    int target,
                                                    Color side) {
    const std::array<Piece, 6> piece_order = side == WHITE
        ? std::array<Piece, 6>{W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING}
        : std::array<Piece, 6>{B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING};
    const Bitboard attackers = attackers_to_square(piece_bitboards, occupied, target, side);

    for (Piece piece : piece_order) {
        Bitboard candidates = attackers & piece_bitboards[static_cast<std::size_t>(piece)];
        if (candidates == 0) {
            continue;
        }

        const int square = pop_lsb(candidates);
        return AttackerInfo{piece, square, piece_value(piece)};
    }

    return std::nullopt;
}

int see_recapture_gain(PieceBitboards& piece_bitboards,
                       Bitboard occupied,
                       int target,
                       Color side,
                       int victim_value) {
    const auto attacker = least_valuable_attacker(piece_bitboards, occupied, target, side);
    if (!attacker.has_value()) {
        return 0;
    }

    const Bitboard from_bit = square_bb(attacker->square);
    piece_bitboards[static_cast<std::size_t>(attacker->piece)] &= ~from_bit;
    occupied &= ~from_bit;

    const int continuation =
        see_recapture_gain(piece_bitboards,
                           occupied,
                           target,
                           opposite_color(side),
                           attacker->value);
    return std::max(0, victim_value - continuation);
}

int killer_move_bonus(const Move& move, const KillerSlot& killer_slot) {
    if (killer_slot[0].has_value() && same_move(move, *killer_slot[0])) {
        return 9000;
    }
    if (killer_slot[1].has_value() && same_move(move, *killer_slot[1])) {
        return 8000;
    }
    return 0;
}

int move_order_score(const ChessBoard& board, const Move& move) {
    int score = 0;

    if (move.promotion != EMPTY) {
        score += 100000 + piece_value(move.promotion);
    }

    if (is_capture_move(board, move)) {
        score += mvv_lva_score(board, move);
    }

    return score;
}

template <typename ScoreMove>
void sort_by_cached_score(std::vector<Move>& moves, ScoreMove score_move) {
    std::vector<ScoredMove> scored_moves;
    scored_moves.reserve(moves.size());
    for (const Move& move : moves) {
        scored_moves.push_back({move, score_move(move)});
    }

    std::sort(scored_moves.begin(), scored_moves.end(), [](const ScoredMove& lhs,
                                                           const ScoredMove& rhs) {
        return lhs.score > rhs.score;
    });

    for (std::size_t i = 0; i < scored_moves.size(); ++i) {
        moves[i] = scored_moves[i].move;
    }
}

}  // namespace

namespace MinimaxInternal {

int capture_value(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    Piece captured_piece = board.piece_at(move.to);

    if ((moving_piece == W_PAWN || moving_piece == B_PAWN) &&
        move.to == board.en_passant_square &&
        captured_piece == EMPTY) {
        captured_piece = moving_piece == W_PAWN ? B_PAWN : W_PAWN;
    }

    return piece_value(captured_piece);
}

int mvv_lva_score(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    const int victim_value = capture_value(board, move);
    if (victim_value == 0) {
        return 0;
    }

    return 10000 + 16 * victim_value - piece_value(moving_piece);
}

bool is_capture_move(const ChessBoard& board, const Move& move) {
    return capture_value(board, move) > 0;
}

bool is_en_passant_move(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    return (moving_piece == W_PAWN || moving_piece == B_PAWN) &&
           move.to == board.en_passant_square &&
           board.is_empty(move.to);
}

int static_exchange_eval(const ChessBoard& board, const Move& move) {
    if (!is_capture_move(board, move)) {
        return 0;
    }

    const Piece moving_piece = board.piece_at(move.from);
    const Piece placed_piece = move.promotion != EMPTY ? move.promotion : moving_piece;
    const Color moving_color = is_white_piece(moving_piece) ? WHITE : BLACK;
    const Color enemy_color = opposite_color(moving_color);
    const bool is_en_passant_capture =
        (moving_piece == W_PAWN || moving_piece == B_PAWN) &&
        move.to == board.en_passant_square &&
        board.is_empty(move.to);
    const Piece captured_piece = is_en_passant_capture
        ? (moving_color == WHITE ? B_PAWN : W_PAWN)
        : board.piece_at(move.to);
    const int captured_square = is_en_passant_capture
        ? move.to - (moving_color == WHITE ? 8 : -8)
        : move.to;
    const int captured_value = capture_value(board, move);
    if (captured_value == 0) {
        return 0;
    }

    PieceBitboards piece_bitboards = board.piece_bitboards;
    Bitboard occupied = board.occupied;
    const Bitboard from_bit = square_bb(move.from);
    const Bitboard to_bit = square_bb(move.to);

    piece_bitboards[static_cast<std::size_t>(moving_piece)] &= ~from_bit;
    piece_bitboards[static_cast<std::size_t>(captured_piece)] &= ~square_bb(captured_square);
    occupied &= ~from_bit;
    occupied &= ~square_bb(captured_square);
    occupied |= to_bit;

    return captured_value - see_recapture_gain(piece_bitboards,
                                               occupied,
                                               move.to,
                                               enemy_color,
                                               piece_value(placed_piece));
}

bool is_checking_move(const ChessBoard& board, const Move& move) {
    const ChessBoard next = board.make_move(move);
    return next.is_in_check(next.turn);
}

bool same_move(const Move& lhs, const Move& rhs) {
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.promotion == rhs.promotion;
}

bool is_tactical_move(const ChessBoard& board, const Move& move) {
    if (move.promotion != EMPTY) {
        return true;
    }

    if (is_capture_move(board, move)) {
        return true;
    }

    return is_en_passant_move(board, move);
}

void order_moves(const ChessBoard& board, std::vector<Move>& moves) {
    sort_by_cached_score(moves, [&](const Move& move) {
        return move_order_score(board, move);
    });
}

void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const KillerSlot& killer_slot,
                 const HistoryForSide& history_for_side) {
    sort_by_cached_score(moves, [&](const Move& move) {
        const int base_score = move_order_score(board, move);
        if (base_score != 0) {
            return base_score;
        }

        return killer_move_bonus(move, killer_slot) +
               history_for_side[static_cast<std::size_t>(move.from)]
                               [static_cast<std::size_t>(move.to)];
    });
}

void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const std::optional<Move>& tt_move) {
    order_moves(board, moves);
    if (!tt_move.has_value()) {
        return;
    }

    const auto tt_it = std::find_if(moves.begin(), moves.end(), [&](const Move& move) {
        return same_move(move, *tt_move);
    });
    if (tt_it != moves.end()) {
        std::iter_swap(moves.begin(), tt_it);
    }
}

void order_moves(const ChessBoard& board,
                 std::vector<Move>& moves,
                 const std::optional<Move>& tt_move,
                 const KillerSlot& killer_slot,
                 const HistoryForSide& history_for_side) {
    order_moves(board, moves, killer_slot, history_for_side);
    if (!tt_move.has_value()) {
        return;
    }

    const auto tt_it = std::find_if(moves.begin(), moves.end(), [&](const Move& move) {
        return same_move(move, *tt_move);
    });
    if (tt_it != moves.end()) {
        std::iter_swap(moves.begin(), tt_it);
    }
}

}  // namespace MinimaxInternal
