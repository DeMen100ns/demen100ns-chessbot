#include "chessboard.h"

#include "chess/attacks.h"
#include "chessboard_internal.h"

#include <algorithm>
#include <array>
#include <cassert>

void add_promotion_moves(std::vector<Move>& moves, int from, int to, Color color) {
    if (color == WHITE) {
        moves.emplace_back(from, to, W_QUEEN);
        moves.emplace_back(from, to, W_ROOK);
        moves.emplace_back(from, to, W_BISHOP);
        moves.emplace_back(from, to, W_KNIGHT);
        return;
    }

    moves.emplace_back(from, to, B_QUEEN);
    moves.emplace_back(from, to, B_ROOK);
    moves.emplace_back(from, to, B_BISHOP);
    moves.emplace_back(from, to, B_KNIGHT);
}

void add_pawn_move(std::vector<Move>& moves, int from, int to, Color color) {
    const int promotion_row = (color == WHITE) ? 7 : 0;
    if (row_of(to) == promotion_row) {
        add_promotion_moves(moves, from, to, color);
        return;
    }

    moves.emplace_back(from, to);
}

void append_targets(std::vector<Move>& moves, int from, Bitboard targets) {
    Bitboard remaining = targets;
    while (remaining != 0) {
        moves.emplace_back(from, pop_lsb(remaining));
    }
}

bool is_tactical_quiescence_candidate(const ChessBoard& board, const Move& move) {
    if (move.promotion != EMPTY) {
        return true;
    }

    const Piece moving_piece = board.piece_at(move.from);
    if ((moving_piece == W_PAWN || moving_piece == B_PAWN) &&
        move.to == board.en_passant_square &&
        board.is_empty(move.to)) {
        return true;
    }

    return !board.is_empty(move.to);
}

Bitboard attackers_to_square(const ChessBoard& board,
                             int square,
                             Color by_color,
                             Bitboard occupied) {
    const Bitboard target = square_bb(square);
    const Piece pawn = by_color == WHITE ? W_PAWN : B_PAWN;
    const Piece knight = by_color == WHITE ? W_KNIGHT : B_KNIGHT;
    const Piece bishop = by_color == WHITE ? W_BISHOP : B_BISHOP;
    const Piece rook = by_color == WHITE ? W_ROOK : B_ROOK;
    const Piece queen = by_color == WHITE ? W_QUEEN : B_QUEEN;
    const Piece king = by_color == WHITE ? W_KING : B_KING;

    Bitboard attackers = pawn_attacks(target, opposite_color(by_color)) &
                         board.piece_bitboard(pawn);
    attackers |= knight_attacks(square) & board.piece_bitboard(knight);
    attackers |= king_attacks(square) & board.piece_bitboard(king);
    attackers |= bishop_attacks(square, occupied) &
                 (board.piece_bitboard(bishop) | board.piece_bitboard(queen));
    attackers |= rook_attacks(square, occupied) &
                 (board.piece_bitboard(rook) | board.piece_bitboard(queen));
    return attackers;
}

int sign_int(int value) {
    if (value > 0) {
        return 1;
    }
    if (value < 0) {
        return -1;
    }
    return 0;
}

Bitboard squares_between(int from, int to) {
    const int from_rank = row_of(from);
    const int from_file = col_of(from);
    const int to_rank = row_of(to);
    const int to_file = col_of(to);
    const int rank_delta = to_rank - from_rank;
    const int file_delta = to_file - from_file;

    if (rank_delta != 0 && file_delta != 0 &&
        abs_int(rank_delta) != abs_int(file_delta)) {
        return 0;
    }

    const int rank_step = sign_int(rank_delta);
    const int file_step = sign_int(file_delta);
    if (rank_step == 0 && file_step == 0) {
        return 0;
    }

    Bitboard between = 0;
    int rank = from_rank + rank_step;
    int file = from_file + file_step;
    while (rank != to_rank || file != to_file) {
        between |= square_bb(rank * 8 + file);
        rank += rank_step;
        file += file_step;
    }
    return between;
}

bool is_slider(Piece piece) {
    return piece == W_BISHOP || piece == B_BISHOP ||
           piece == W_ROOK || piece == B_ROOK ||
           piece == W_QUEEN || piece == B_QUEEN;
}

void append_legal_evasion(const ChessBoard& board,
                          std::vector<Move>& moves,
                          const Move& move,
                          Color color) {
    const ChessBoard next = board.make_move(move);
    if (!next.is_in_check(color)) {
        moves.push_back(move);
    }
}

void append_pawn_evasions_to_target(const ChessBoard& board,
                                    std::vector<Move>& moves,
                                    int target,
                                    Color color) {
    const Piece own_pawn = color == WHITE ? W_PAWN : B_PAWN;
    const Bitboard target_bit = square_bb(target);
    const bool target_empty = (board.occupied & target_bit) == 0;

    if (!target_empty) {
        Bitboard pawns = pawn_attacks(target_bit, opposite_color(color)) &
                         board.piece_bitboard(own_pawn);
        while (pawns != 0) {
            const int from = pop_lsb(pawns);
            std::vector<Move> pawn_moves;
            add_pawn_move(pawn_moves, from, target, color);
            for (const Move& move : pawn_moves) {
                append_legal_evasion(board, moves, move, color);
            }
        }
        return;
    }

    if (color == WHITE) {
        const int one_step_from = target - 8;
        if (one_step_from >= 0 && board.has_piece(one_step_from, own_pawn)) {
            std::vector<Move> pawn_moves;
            add_pawn_move(pawn_moves, one_step_from, target, color);
            for (const Move& move : pawn_moves) {
                append_legal_evasion(board, moves, move, color);
            }
        }

        const int two_step_from = target - 16;
        if (row_of(target) == 3 &&
            two_step_from >= 0 &&
            board.has_piece(two_step_from, own_pawn) &&
            board.is_empty(target - 8)) {
            append_legal_evasion(board, moves, Move(two_step_from, target), color);
        }
        return;
    }

    const int one_step_from = target + 8;
    if (one_step_from < 64 && board.has_piece(one_step_from, own_pawn)) {
        std::vector<Move> pawn_moves;
        add_pawn_move(pawn_moves, one_step_from, target, color);
        for (const Move& move : pawn_moves) {
            append_legal_evasion(board, moves, move, color);
        }
    }

    const int two_step_from = target + 16;
    if (row_of(target) == 4 &&
        two_step_from < 64 &&
        board.has_piece(two_step_from, own_pawn) &&
        board.is_empty(target + 8)) {
        append_legal_evasion(board, moves, Move(two_step_from, target), color);
    }
}

void append_en_passant_evasions(const ChessBoard& board,
                                std::vector<Move>& moves,
                                Bitboard evasion_targets,
                                int checker_square,
                                Color color) {
    if (board.en_passant_square < 0) {
        return;
    }

    const int captured_square =
        board.en_passant_square + (color == WHITE ? -8 : 8);
    if (captured_square != checker_square &&
        (evasion_targets & square_bb(board.en_passant_square)) == 0) {
        return;
    }

    const Piece own_pawn = color == WHITE ? W_PAWN : B_PAWN;
    Bitboard pawns = pawn_attacks(square_bb(board.en_passant_square), opposite_color(color)) &
                     board.piece_bitboard(own_pawn);
    while (pawns != 0) {
        append_legal_evasion(board,
                             moves,
                             Move(pop_lsb(pawns), board.en_passant_square),
                             color);
    }
}

void append_piece_evasions(const ChessBoard& board,
                           std::vector<Move>& moves,
                           Piece piece,
                           Bitboard evasion_targets,
                           Color color) {
    Bitboard pieces = board.piece_bitboard(piece);
    while (pieces != 0) {
        const int from = pop_lsb(pieces);
        Bitboard targets = 0;
        switch (piece) {
            case W_KNIGHT:
            case B_KNIGHT:
                targets = knight_attacks(from);
                break;
            case W_BISHOP:
            case B_BISHOP:
                targets = bishop_attacks(from, board.occupied);
                break;
            case W_ROOK:
            case B_ROOK:
                targets = rook_attacks(from, board.occupied);
                break;
            case W_QUEEN:
            case B_QUEEN:
                targets = queen_attacks(from, board.occupied);
                break;
            default:
                break;
        }

        targets &= evasion_targets;
        while (targets != 0) {
            append_legal_evasion(board, moves, Move(from, pop_lsb(targets)), color);
        }
    }
}

struct PinInfo {
    Bitboard pinned = 0;
    std::array<Bitboard, 64> legal_rays{};
};

bool is_king(Piece piece) {
    return piece == W_KING || piece == B_KING;
}

bool is_pawn(Piece piece) {
    return piece == W_PAWN || piece == B_PAWN;
}

bool is_en_passant_move(const ChessBoard& board, const Move& move, Piece moving_piece) {
    return is_pawn(moving_piece) &&
           move.to == board.en_passant_square &&
           board.is_empty(move.to);
}

bool is_compatible_pinner(Piece piece, int rank_step, int file_step) {
    if (piece == W_QUEEN || piece == B_QUEEN) {
        return true;
    }

    const bool diagonal = rank_step != 0 && file_step != 0;
    if (diagonal) {
        return piece == W_BISHOP || piece == B_BISHOP;
    }
    return piece == W_ROOK || piece == B_ROOK;
}

PinInfo compute_pin_info(const ChessBoard& board, Color color, int king_square) {
    PinInfo info;
    const Color enemy_color = opposite_color(color);
    const int king_rank = row_of(king_square);
    const int king_file = col_of(king_square);
    static constexpr int kDirections[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };

    for (const auto& direction : kDirections) {
        int rank = king_rank + direction[0];
        int file = king_file + direction[1];
        int pinned_square = -1;

        while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
            const int square = rank * 8 + file;
            const Piece piece = board.piece_at(square);
            if (piece == EMPTY) {
                rank += direction[0];
                file += direction[1];
                continue;
            }

            if (is_piece_of_color(piece, color)) {
                if (pinned_square != -1) {
                    break;
                }
                pinned_square = square;
                rank += direction[0];
                file += direction[1];
                continue;
            }

            if (is_piece_of_color(piece, enemy_color) &&
                pinned_square != -1 &&
                is_compatible_pinner(piece, direction[0], direction[1])) {
                info.pinned |= square_bb(pinned_square);
                info.legal_rays[static_cast<std::size_t>(pinned_square)] =
                    squares_between(king_square, square) | square_bb(square);
            }
            break;
        }
    }

    return info;
}

std::vector<Move> generate_moves_by_filtering(const ChessBoard& board, Color color) {
    const std::vector<Move> pseudo_moves = board.generate_pseudo_moves(color);
    std::vector<Move> legal_moves;
    legal_moves.reserve(pseudo_moves.size());

    for (const Move& move : pseudo_moves) {
        const ChessBoard next = board.make_move(move);
        if (!next.is_in_check(color)) {
            legal_moves.push_back(move);
        }
    }

    return legal_moves;
}

#ifndef NDEBUG
int move_key(const Move& move) {
    return move.from * 64 * 16 + move.to * 16 + static_cast<int>(move.promotion);
}

bool same_move_set(std::vector<Move> lhs, std::vector<Move> rhs) {
    const auto by_key = [](const Move& left, const Move& right) {
        return move_key(left) < move_key(right);
    };
    std::sort(lhs.begin(), lhs.end(), by_key);
    std::sort(rhs.begin(), rhs.end(), by_key);
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (move_key(lhs[i]) != move_key(rhs[i])) {
            return false;
        }
    }
    return true;
}
#endif

std::vector<Move> ChessBoard::generate_pseudo_moves(Color color) const {
    std::vector<Move> moves;
    moves.reserve(64);

    const Bitboard own_occupied = color_bitboard(color);
    const Bitboard enemy_occupied = color_bitboard(opposite_color(color));

    Bitboard pawns = piece_bitboard(color == WHITE ? W_PAWN : B_PAWN);
    while (pawns != 0) {
        const int from = pop_lsb(pawns);
        const Bitboard from_bit = square_bb(from);

        if (color == WHITE) {
            const int one_step = from + 8;
            if (one_step < 64 && (occupied & square_bb(one_step)) == 0) {
                add_pawn_move(moves, from, one_step, color);
                if ((from_bit & kRank2Mask) != 0) {
                    const int two_step = from + 16;
                    if ((occupied & square_bb(two_step)) == 0) {
                        moves.emplace_back(from, two_step);
                    }
                }
            }

            const Bitboard capture_targets =
                pawn_attacks(from_bit, WHITE) & (enemy_occupied |
                (en_passant_square >= 0 ? square_bb(en_passant_square) : 0ULL));
            Bitboard captures = capture_targets;
            while (captures != 0) {
                add_pawn_move(moves, from, pop_lsb(captures), color);
            }
        } else {
            const int one_step = from - 8;
            if (one_step >= 0 && (occupied & square_bb(one_step)) == 0) {
                add_pawn_move(moves, from, one_step, color);
                if ((from_bit & kRank7Mask) != 0) {
                    const int two_step = from - 16;
                    if ((occupied & square_bb(two_step)) == 0) {
                        moves.emplace_back(from, two_step);
                    }
                }
            }

            const Bitboard capture_targets =
                pawn_attacks(from_bit, BLACK) & (enemy_occupied |
                (en_passant_square >= 0 ? square_bb(en_passant_square) : 0ULL));
            Bitboard captures = capture_targets;
            while (captures != 0) {
                add_pawn_move(moves, from, pop_lsb(captures), color);
            }
        }
    }

    Bitboard knights = piece_bitboard(color == WHITE ? W_KNIGHT : B_KNIGHT);
    while (knights != 0) {
        const int from = pop_lsb(knights);
        append_targets(moves, from, knight_attacks(from) & ~own_occupied);
    }

    Bitboard bishops = piece_bitboard(color == WHITE ? W_BISHOP : B_BISHOP);
    while (bishops != 0) {
        const int from = pop_lsb(bishops);
        append_targets(moves, from, bishop_attacks(from, occupied) & ~own_occupied);
    }

    Bitboard rooks = piece_bitboard(color == WHITE ? W_ROOK : B_ROOK);
    while (rooks != 0) {
        const int from = pop_lsb(rooks);
        append_targets(moves, from, rook_attacks(from, occupied) & ~own_occupied);
    }

    Bitboard queens = piece_bitboard(color == WHITE ? W_QUEEN : B_QUEEN);
    while (queens != 0) {
        const int from = pop_lsb(queens);
        append_targets(moves, from, queen_attacks(from, occupied) & ~own_occupied);
    }

    const Piece king_piece = color == WHITE ? W_KING : B_KING;
    const Bitboard king_bb = piece_bitboard(king_piece);
    if (king_bb != 0) {
        const int from = __builtin_ctzll(king_bb);
        append_targets(moves, from, king_attacks(from) & ~own_occupied);

        if (color == WHITE && from == 4) {
            if (white_can_castle_kingside &&
                has_piece(7, W_ROOK) &&
                (occupied & kWhiteCastleKingsideEmpty) == 0 &&
                !is_square_attacked(4, BLACK) &&
                !is_square_attacked(5, BLACK) &&
                !is_square_attacked(6, BLACK)) {
                moves.emplace_back(4, 6);
            }
            if (white_can_castle_queenside &&
                has_piece(0, W_ROOK) &&
                (occupied & kWhiteCastleQueensideEmpty) == 0 &&
                !is_square_attacked(4, BLACK) &&
                !is_square_attacked(3, BLACK) &&
                !is_square_attacked(2, BLACK)) {
                moves.emplace_back(4, 2);
            }
        }

        if (color == BLACK && from == 60) {
            if (black_can_castle_kingside &&
                has_piece(63, B_ROOK) &&
                (occupied & kBlackCastleKingsideEmpty) == 0 &&
                !is_square_attacked(60, WHITE) &&
                !is_square_attacked(61, WHITE) &&
                !is_square_attacked(62, WHITE)) {
                moves.emplace_back(60, 62);
            }
            if (black_can_castle_queenside &&
                has_piece(56, B_ROOK) &&
                (occupied & kBlackCastleQueensideEmpty) == 0 &&
                !is_square_attacked(60, WHITE) &&
                !is_square_attacked(59, WHITE) &&
                !is_square_attacked(58, WHITE)) {
                moves.emplace_back(60, 58);
            }
        }
    }

    return moves;
}

std::vector<Move> ChessBoard::generate_moves(Color color) const {
    const int king_square = find_king_square(*this, color);
    if (king_square == -1) {
        return {};
    }

    if (attackers_to_square(*this, king_square, opposite_color(color), occupied) != 0) {
        std::vector<Move> evasions = generate_evasions(color);
#ifndef NDEBUG
        const std::vector<Move> slow_moves = generate_moves_by_filtering(*this, color);
        assert(same_move_set(evasions, slow_moves));
#endif
        return evasions;
    }

    const std::vector<Move> pseudo_moves = generate_pseudo_moves(color);
    std::vector<Move> legal_moves;
    legal_moves.reserve(pseudo_moves.size());
    const PinInfo pin_info = compute_pin_info(*this, color, king_square);

    for (const Move& move : pseudo_moves) {
        const Piece moving_piece = piece_at(move.from);
        if (is_king(moving_piece) || is_en_passant_move(*this, move, moving_piece)) {
            const ChessBoard next = make_move(move);
            if (!next.is_in_check(color)) {
                legal_moves.push_back(move);
            }
            continue;
        }

        const Bitboard from_bit = square_bb(move.from);
        if ((pin_info.pinned & from_bit) == 0 ||
            (pin_info.legal_rays[static_cast<std::size_t>(move.from)] & square_bb(move.to)) != 0) {
            legal_moves.push_back(move);
        }
    }

#ifndef NDEBUG
    const std::vector<Move> slow_moves = generate_moves_by_filtering(*this, color);
    assert(same_move_set(legal_moves, slow_moves));
#endif
    return legal_moves;
}

std::vector<Move> ChessBoard::generate_evasions(Color color) const {
    const int king_square = find_king_square(*this, color);
    if (king_square == -1) {
        return {};
    }

    const Color enemy_color = opposite_color(color);
    const Bitboard own_occupied = color_bitboard(color);
    const Bitboard checkers =
        attackers_to_square(*this, king_square, enemy_color, occupied);
    if (checkers == 0) {
        return generate_moves(color);
    }

    std::vector<Move> moves;
    moves.reserve(16);

    Bitboard king_targets = king_attacks(king_square) & ~own_occupied;
    while (king_targets != 0) {
        append_legal_evasion(*this,
                             moves,
                             Move(king_square, pop_lsb(king_targets)),
                             color);
    }

    if ((checkers & (checkers - 1)) != 0) {
        return moves;
    }

    const int checker_square = __builtin_ctzll(checkers);
    const Piece checker = piece_at(checker_square);
    Bitboard evasion_targets = square_bb(checker_square);
    if (is_slider(checker)) {
        evasion_targets |= squares_between(king_square, checker_square);
    }

    Bitboard remaining_targets = evasion_targets;
    while (remaining_targets != 0) {
        append_pawn_evasions_to_target(*this, moves, pop_lsb(remaining_targets), color);
    }
    append_en_passant_evasions(*this, moves, evasion_targets, checker_square, color);

    if (color == WHITE) {
        append_piece_evasions(*this, moves, W_KNIGHT, evasion_targets, color);
        append_piece_evasions(*this, moves, W_BISHOP, evasion_targets, color);
        append_piece_evasions(*this, moves, W_ROOK, evasion_targets, color);
        append_piece_evasions(*this, moves, W_QUEEN, evasion_targets, color);
    } else {
        append_piece_evasions(*this, moves, B_KNIGHT, evasion_targets, color);
        append_piece_evasions(*this, moves, B_BISHOP, evasion_targets, color);
        append_piece_evasions(*this, moves, B_ROOK, evasion_targets, color);
        append_piece_evasions(*this, moves, B_QUEEN, evasion_targets, color);
    }

    return moves;
}

std::vector<Move> ChessBoard::generate_quiescence_moves(Color color,
                                                        bool include_quiet_checks) const {
    const std::vector<Move> pseudo_moves = generate_pseudo_moves(color);
    std::vector<Move> legal_moves;
    legal_moves.reserve(pseudo_moves.size());

    for (const Move& move : pseudo_moves) {
        const bool is_tactical = is_tactical_quiescence_candidate(*this, move);
        if (!is_tactical && !include_quiet_checks) {
            continue;
        }

        const ChessBoard next = make_move(move);
        if (next.is_in_check(color)) {
            continue;
        }

        if (is_tactical || next.is_in_check(next.turn)) {
            legal_moves.push_back(move);
        }
    }

    return legal_moves;
}

ChessBoard ChessBoard::make_move(const Move& move) const {
    ChessBoard next = *this;
    const Piece moving_piece = piece_at(move.from);
    const Piece placed_piece = move.promotion != EMPTY ? move.promotion : moving_piece;
    const Color moving_color = is_white_piece(moving_piece) ? WHITE : BLACK;
    const Color enemy_color = opposite_color(moving_color);
    const bool is_pawn = moving_piece == W_PAWN || moving_piece == B_PAWN;
    const bool is_king = moving_piece == W_KING || moving_piece == B_KING;
    const bool is_rook = moving_piece == W_ROOK || moving_piece == B_ROOK;
    const bool is_en_passant_capture =
        is_pawn && move.to == en_passant_square && is_empty(move.to);
    const Piece destination_piece = is_en_passant_capture ? EMPTY : piece_at(move.to);
    const Piece captured_piece = is_en_passant_capture
        ? (moving_color == WHITE ? B_PAWN : W_PAWN)
        : destination_piece;
    const int captured_square = is_en_passant_capture
        ? move.to - (moving_color == WHITE ? 8 : -8)
        : (captured_piece != EMPTY ? move.to : -1);
    const bool is_capture = captured_piece != EMPTY;
    const bool is_castle = is_king && abs_int(move.to - move.from) == 2;

    xor_en_passant_file(next.zobrist_key, *this);
    xor_castling_rights(next.zobrist_key, *this);
    next.zobrist_key ^= piece_square_key(moving_piece, move.from);
    if (is_en_passant_capture) {
        next.zobrist_key ^= piece_square_key(captured_piece, captured_square);
    } else if (destination_piece != EMPTY) {
        next.zobrist_key ^= piece_square_key(destination_piece, move.to);
    }
    next.zobrist_key ^= piece_square_key(placed_piece, move.to);

    next.en_passant_square = -1;

    if (destination_piece == W_ROOK) {
        if (move.to == 0) {
            next.white_can_castle_queenside = false;
        } else if (move.to == 7) {
            next.white_can_castle_kingside = false;
        }
    } else if (destination_piece == B_ROOK) {
        if (move.to == 56) {
            next.black_can_castle_queenside = false;
        } else if (move.to == 63) {
            next.black_can_castle_kingside = false;
        }
    }

    if (is_en_passant_capture) {
        next.clear_square(captured_square);
    }

    next.clear_square(move.from);
    next.set_square(move.to, placed_piece);

    if (is_king) {
        if (moving_color == WHITE) {
            next.white_can_castle_kingside = false;
            next.white_can_castle_queenside = false;
        } else {
            next.black_can_castle_kingside = false;
            next.black_can_castle_queenside = false;
        }

        if (is_castle) {
            if (move.to == 6) {
                next.zobrist_key ^= piece_square_key(W_ROOK, 7);
                next.zobrist_key ^= piece_square_key(W_ROOK, 5);
                next.clear_square(7);
                next.set_square(5, W_ROOK);
            } else if (move.to == 2) {
                next.zobrist_key ^= piece_square_key(W_ROOK, 0);
                next.zobrist_key ^= piece_square_key(W_ROOK, 3);
                next.clear_square(0);
                next.set_square(3, W_ROOK);
            } else if (move.to == 62) {
                next.zobrist_key ^= piece_square_key(B_ROOK, 63);
                next.zobrist_key ^= piece_square_key(B_ROOK, 61);
                next.clear_square(63);
                next.set_square(61, B_ROOK);
            } else if (move.to == 58) {
                next.zobrist_key ^= piece_square_key(B_ROOK, 56);
                next.zobrist_key ^= piece_square_key(B_ROOK, 59);
                next.clear_square(56);
                next.set_square(59, B_ROOK);
            }
        }
    }

    if (is_rook) {
        if (move.from == 0) {
            next.white_can_castle_queenside = false;
        } else if (move.from == 7) {
            next.white_can_castle_kingside = false;
        } else if (move.from == 56) {
            next.black_can_castle_queenside = false;
        } else if (move.from == 63) {
            next.black_can_castle_kingside = false;
        }
    }

    if (is_pawn && abs_int(move.to - move.from) == 16) {
        next.en_passant_square = move.from + (moving_color == WHITE ? 8 : -8);
    }

    next.halfmove_clock = (is_pawn || is_capture) ? 0 : (halfmove_clock + 1);
    if (moving_color == BLACK) {
        ++next.turn_number;
    }

    next.turn = enemy_color;
    xor_castling_rights(next.zobrist_key, next);
    xor_en_passant_file(next.zobrist_key, next);
    next.zobrist_key ^= zobrist_side_to_move_key();
    return next;
}

bool ChessBoard::valid_move(const Move& move, Color color) const {
    if (color != turn) {
        return false;
    }
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
        return false;
    }
    if (!has_color_piece(move.from, color)) {
        return false;
    }
    if (has_color_piece(move.to, color)) {
        return false;
    }

    const std::vector<Move> legal_moves = generate_moves(color);
    for (const Move& candidate : legal_moves) {
        if (candidate.from == move.from &&
            candidate.to == move.to &&
            candidate.promotion == move.promotion) {
            return true;
        }
    }

    return false;
}
