#include "chess/chessboard.h"

#include "chessboard_internal.h"

#include <sstream>
#include <stdexcept>

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

Piece piece_from_fen_char(char symbol) {
    switch (symbol) {
        case 'P': return W_PAWN;
        case 'N': return W_KNIGHT;
        case 'B': return W_BISHOP;
        case 'R': return W_ROOK;
        case 'Q': return W_QUEEN;
        case 'K': return W_KING;
        case 'p': return B_PAWN;
        case 'n': return B_KNIGHT;
        case 'b': return B_BISHOP;
        case 'r': return B_ROOK;
        case 'q': return B_QUEEN;
        case 'k': return B_KING;
        default:
            throw std::invalid_argument("Invalid FEN piece character");
    }
}

int square_from_algebraic(const std::string& square) {
    if (square.size() != 2 ||
        square[0] < 'a' ||
        square[0] > 'h' ||
        square[1] < '1' ||
        square[1] > '8') {
        throw std::invalid_argument("Invalid FEN square");
    }

    const int file = square[0] - 'a';
    const int rank = square[1] - '1';
    return rank * 8 + file;
}

ChessBoard::ChessBoard(const std::string& fen) : ChessBoard() {
    std::istringstream stream(fen);
    std::string placement;
    std::string active_color;
    std::string castling;
    std::string en_passant;
    int parsed_halfmove_clock = 0;
    int fullmove_number = 1;

    if (!(stream >> placement >> active_color >> castling >> en_passant)) {
        throw std::invalid_argument("FEN must contain at least 4 fields");
    }

    if (stream >> parsed_halfmove_clock >> fullmove_number) {
        if (parsed_halfmove_clock < 0 || fullmove_number <= 0) {
            throw std::invalid_argument("Invalid FEN move counters");
        }
    }

    int rank = 7;
    int file = 0;
    for (char symbol : placement) {
        if (symbol == '/') {
            if (file != 8 || rank == 0) {
                throw std::invalid_argument("Invalid FEN board layout");
            }
            --rank;
            file = 0;
            continue;
        }

        if (symbol >= '1' && symbol <= '8') {
            file += symbol - '0';
            if (file > 8) {
                throw std::invalid_argument("Invalid FEN empty-square count");
            }
            continue;
        }

        if (file >= 8 || rank < 0) {
            throw std::invalid_argument("Invalid FEN board layout");
        }

        set_square(rank * 8 + file, piece_from_fen_char(symbol));
        ++file;
    }

    if (rank != 0 || file != 8) {
        throw std::invalid_argument("Invalid FEN board layout");
    }

    if (active_color == "w") {
        turn = WHITE;
    } else if (active_color == "b") {
        turn = BLACK;
    } else {
        throw std::invalid_argument("Invalid FEN active color");
    }

    white_can_castle_kingside = castling.find('K') != std::string::npos;
    white_can_castle_queenside = castling.find('Q') != std::string::npos;
    black_can_castle_kingside = castling.find('k') != std::string::npos;
    black_can_castle_queenside = castling.find('q') != std::string::npos;
    if (castling != "-" &&
        castling.find_first_not_of("KQkq") != std::string::npos) {
        throw std::invalid_argument("Invalid FEN castling field");
    }

    en_passant_square = (en_passant == "-") ? -1 : square_from_algebraic(en_passant);
    halfmove_clock = parsed_halfmove_clock;
    turn_number = fullmove_number;
    zobrist_key = compute_position_key_full(*this);
}
