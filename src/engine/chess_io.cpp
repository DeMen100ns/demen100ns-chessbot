#include "chess/io.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace ChessIO {

char piece_to_fen(Piece piece) {
    switch (piece) {
        case W_PAWN: return 'P';
        case W_KNIGHT: return 'N';
        case W_BISHOP: return 'B';
        case W_ROOK: return 'R';
        case W_QUEEN: return 'Q';
        case W_KING: return 'K';
        case B_PAWN: return 'p';
        case B_KNIGHT: return 'n';
        case B_BISHOP: return 'b';
        case B_ROOK: return 'r';
        case B_QUEEN: return 'q';
        case B_KING: return 'k';
        case EMPTY: break;
    }
    return '1';
}

char piece_to_upper_letter(Piece piece) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(piece_to_fen(piece))));
}

std::string square_to_algebraic(int square) {
    if (square < 0 || square >= 64) {
        throw std::invalid_argument("Square is out of bounds");
    }

    std::string value = "a1";
    value[0] = static_cast<char>('a' + (square % 8));
    value[1] = static_cast<char>('1' + (square / 8));
    return value;
}

std::optional<int> square_from_algebraic(const std::string& text) {
    if (text.size() != 2) {
        return std::nullopt;
    }

    const char file = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    const char rank = text[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return std::nullopt;
    }

    return (rank - '1') * 8 + (file - 'a');
}

std::string board_to_fen(const ChessBoard& board) {
    std::ostringstream fen;

    for (int rank = 7; rank >= 0; --rank) {
        int empty_count = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = board.piece_at(rank * 8 + file);
            if (piece == EMPTY) {
                ++empty_count;
                continue;
            }

            if (empty_count > 0) {
                fen << empty_count;
                empty_count = 0;
            }
            fen << piece_to_fen(piece);
        }

        if (empty_count > 0) {
            fen << empty_count;
        }
        if (rank > 0) {
            fen << '/';
        }
    }

    fen << ' ' << (board.turn == WHITE ? 'w' : 'b') << ' ';

    std::string castling;
    if (board.white_can_castle_kingside) castling.push_back('K');
    if (board.white_can_castle_queenside) castling.push_back('Q');
    if (board.black_can_castle_kingside) castling.push_back('k');
    if (board.black_can_castle_queenside) castling.push_back('q');
    fen << (castling.empty() ? "-" : castling) << ' ';

    if (board.en_passant_square >= 0 && board.en_passant_square < 64) {
        fen << square_to_algebraic(board.en_passant_square);
    } else {
        fen << '-';
    }

    fen << ' ' << board.halfmove_clock << ' ' << board.turn_number;
    return fen.str();
}

std::string move_to_uci(const Move& move) {
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
        throw std::invalid_argument("Move is out of bounds");
    }

    std::string text = square_to_algebraic(move.from) + square_to_algebraic(move.to);

    switch (move.promotion) {
        case W_QUEEN:
        case B_QUEEN:
            text.push_back('q');
            break;
        case W_ROOK:
        case B_ROOK:
            text.push_back('r');
            break;
        case W_BISHOP:
        case B_BISHOP:
            text.push_back('b');
            break;
        case W_KNIGHT:
        case B_KNIGHT:
            text.push_back('n');
            break;
        case EMPTY:
            break;
        default:
            throw std::invalid_argument("Unsupported promotion piece");
    }

    return text;
}

std::optional<Move> move_from_uci(const std::string& uci) {
    if (uci.size() < 4) {
        return std::nullopt;
    }

    const auto from = square_from_algebraic(uci.substr(0, 2));
    const auto to = square_from_algebraic(uci.substr(2, 2));
    if (!from.has_value() || !to.has_value()) {
        return std::nullopt;
    }

    Piece promotion = EMPTY;
    if (uci.size() >= 5) {
        const bool white_promotion = uci[3] == '8';
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])))) {
            case 'q': promotion = white_promotion ? W_QUEEN : B_QUEEN; break;
            case 'r': promotion = white_promotion ? W_ROOK : B_ROOK; break;
            case 'b': promotion = white_promotion ? W_BISHOP : B_BISHOP; break;
            case 'n': promotion = white_promotion ? W_KNIGHT : B_KNIGHT; break;
            default: return std::nullopt;
        }
    }

    return Move(*from, *to, promotion);
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    return value.substr(start);
}

}  // namespace ChessIO
