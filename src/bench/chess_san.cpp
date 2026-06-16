#include "bench/chess_san.h"

#include "bench/chess_io.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace ChessSan {
std::string normalize_san(std::string san);

namespace {

bool is_white_piece(Piece piece) {
    return piece >= W_PAWN && piece <= W_KING;
}

bool is_capture_move(const ChessBoard& board, const Move& move) {
    if (!board.is_empty(move.to)) {
        return true;
    }

    const Piece moving_piece = board.piece_at(move.from);
    return (moving_piece == W_PAWN || moving_piece == B_PAWN) &&
           move.to == board.en_passant_square &&
           (move.from % 8 != move.to % 8);
}

bool is_castling_move(const ChessBoard& board, const Move& move) {
    const Piece moving_piece = board.piece_at(move.from);
    if (moving_piece != W_KING && moving_piece != B_KING) {
        return false;
    }

    return std::abs(move.to - move.from) == 2;
}

bool would_be_checkmate(const ChessBoard& board, const Move& move) {
    const ChessBoard next = board.make_move(move);
    return next.is_checkmate(next.turn);
}

char san_piece_letter(Piece piece) {
    switch (piece) {
        case W_KNIGHT:
        case B_KNIGHT:
            return 'N';
        case W_BISHOP:
        case B_BISHOP:
            return 'B';
        case W_ROOK:
        case B_ROOK:
            return 'R';
        case W_QUEEN:
        case B_QUEEN:
            return 'Q';
        case W_KING:
        case B_KING:
            return 'K';
        case W_PAWN:
        case B_PAWN:
        case EMPTY:
        default:
            return '\0';
    }
}

void add_candidate(std::vector<std::string>& candidates,
                   std::unordered_set<std::string>& seen,
                   std::string candidate) {
    candidate = normalize_san(std::move(candidate));
    if (candidate.empty()) {
        return;
    }
    if (seen.insert(candidate).second) {
        candidates.push_back(std::move(candidate));
    }
}

std::vector<std::string> move_candidate_notations(const ChessBoard& board, const Move& move) {
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;

    add_candidate(candidates, seen, move_to_san(board, move));

    const Piece moving_piece = board.piece_at(move.from);
    const char piece_letter = san_piece_letter(moving_piece);
    const bool is_pawn = piece_letter == '\0';
    const bool is_capture = is_capture_move(board, move);
    const std::string from_square = ChessIO::square_to_algebraic(move.from);
    const std::string to_square = ChessIO::square_to_algebraic(move.to);
    const std::string capture_text = is_capture ? "x" : "";

    std::string promotion_suffix;
    if (move.promotion != EMPTY) {
        promotion_suffix = "=";
        promotion_suffix.push_back(ChessIO::piece_to_upper_letter(move.promotion));
    }

    if (is_castling_move(board, move)) {
        add_candidate(candidates, seen, move.to > move.from ? "O-O" : "O-O-O");
        return candidates;
    }

    if (is_pawn) {
        add_candidate(candidates, seen, from_square + capture_text + to_square + promotion_suffix);
        add_candidate(candidates, seen, from_square[0] + capture_text + to_square + promotion_suffix);
        add_candidate(candidates, seen, from_square + to_square + promotion_suffix);
        return candidates;
    }

    const std::string piece_prefix(1, piece_letter);
    add_candidate(candidates, seen, piece_prefix + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square[0] + capture_text + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square[1] + capture_text + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square + capture_text + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square[0] + to_square + promotion_suffix);
    add_candidate(candidates, seen, piece_prefix + from_square[1] + to_square + promotion_suffix);
    return candidates;
}

std::string strip_suffix_annotations(std::string san) {
    while (!san.empty()) {
        const char tail = san.back();
        if (tail == '+' || tail == '#' || tail == '!' || tail == '?') {
            san.pop_back();
            continue;
        }
        break;
    }
    return san;
}

}  // namespace

std::string normalize_san(std::string san) {
    san = ChessIO::trim(std::move(san));
    san = strip_suffix_annotations(std::move(san));
    san.erase(std::remove_if(san.begin(), san.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), san.end());

    for (char& ch : san) {
        if (ch == '0') {
            ch = 'O';
        }
    }

    const std::string en_passant_suffix = "e.p.";
    if (san.size() >= en_passant_suffix.size() &&
        san.compare(san.size() - en_passant_suffix.size(), en_passant_suffix.size(), en_passant_suffix) == 0) {
        san.resize(san.size() - en_passant_suffix.size());
    }

    while (!san.empty() && san.back() == '.') {
        san.pop_back();
    }

    return san;
}

std::string move_to_san(const ChessBoard& board, const Move& move) {
    if (!board.valid_move(move, board.turn)) {
        return "";
    }

    if (is_castling_move(board, move)) {
        std::string san = move.to > move.from ? "O-O" : "O-O-O";
        if (would_be_checkmate(board, move)) {
            san.push_back('#');
        } else if (board.make_move(move).is_in_check(board.make_move(move).turn)) {
            san.push_back('+');
        }
        return san;
    }

    const Piece moving_piece = board.piece_at(move.from);
    const char piece_letter = san_piece_letter(moving_piece);
    const bool is_pawn = piece_letter == '\0';
    const bool is_capture = is_capture_move(board, move);
    std::string san;

    if (!is_pawn) {
        san.push_back(piece_letter);

        bool same_file = false;
        bool same_rank = false;
        bool ambiguous = false;
        const std::vector<Move> legal_moves = board.generate_moves(board.turn);
        for (const Move& other : legal_moves) {
            if (other.from == move.from &&
                other.to == move.to &&
                other.promotion == move.promotion) {
                continue;
            }
            if (other.to != move.to || other.promotion != move.promotion) {
                continue;
            }
            if (board.piece_at(other.from) != moving_piece) {
                continue;
            }

            ambiguous = true;
            same_file = same_file || (other.from % 8 == move.from % 8);
            same_rank = same_rank || (other.from / 8 == move.from / 8);
        }

        if (ambiguous) {
            const std::string from_square = ChessIO::square_to_algebraic(move.from);
            if (!same_file) {
                san.push_back(from_square[0]);
            } else if (!same_rank) {
                san.push_back(from_square[1]);
            } else {
                san += from_square;
            }
        }
    } else if (is_capture) {
        san.push_back(ChessIO::square_to_algebraic(move.from)[0]);
    }

    if (is_capture) {
        san.push_back('x');
    }

    san += ChessIO::square_to_algebraic(move.to);

    if (move.promotion != EMPTY) {
        san.push_back('=');
        san.push_back(ChessIO::piece_to_upper_letter(move.promotion));
    }

    const ChessBoard next = board.make_move(move);
    if (next.is_checkmate(next.turn)) {
        san.push_back('#');
    } else if (next.is_in_check(next.turn)) {
        san.push_back('+');
    }

    return san;
}

std::optional<Move> move_from_san(const ChessBoard& board, const std::string& san) {
    const std::string normalized_target = normalize_san(san);
    if (normalized_target.empty()) {
        return std::nullopt;
    }

    const std::vector<Move> legal_moves = board.generate_moves(board.turn);
    for (const Move& move : legal_moves) {
        const std::vector<std::string> candidates = move_candidate_notations(board, move);
        for (const std::string& candidate : candidates) {
            if (candidate == normalized_target) {
                return move;
            }
        }
    }

    return std::nullopt;
}

}  // namespace ChessSan
