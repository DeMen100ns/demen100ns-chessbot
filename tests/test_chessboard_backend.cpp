#include "chess/chessboard.h"
#include "chess/io.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
std::uint64_t square_bit(int square) {
    return 1ULL << square;
}

int move_key(const Move& move) {
    return move.from * 64 * 16 + move.to * 16 + static_cast<int>(move.promotion);
}

std::vector<int> sorted_move_keys(std::vector<Move> moves) {
    std::vector<int> keys;
    keys.reserve(moves.size());
    for (const Move& move : moves) {
        keys.push_back(move_key(move));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void assert_bitboard_invariants(const ChessBoard& board) {
    std::array<std::uint64_t, 13> expected_piece_bitboards{};
    std::array<std::uint64_t, 2> expected_color_bitboards{};
    std::uint64_t expected_occupied = 0;

    for (int square = 0; square < 64; ++square) {
        const Piece piece = board.piece_at(square);
        if (piece == EMPTY) {
            continue;
        }

        const std::uint64_t bit = square_bit(square);
        expected_piece_bitboards[static_cast<std::size_t>(piece)] |= bit;
        expected_occupied |= bit;

        if (piece >= W_PAWN && piece <= W_KING) {
            expected_color_bitboards[WHITE] |= bit;
        } else {
            expected_color_bitboards[BLACK] |= bit;
        }
    }

    assert(board.piece_bitboards == expected_piece_bitboards);
    assert(board.color_bitboards == expected_color_bitboards);
    assert(board.occupied == expected_occupied);
    assert(board.position_key() == ChessBoard(ChessIO::board_to_fen(board)).position_key());
}

void assert_evasions_match_legal_moves(const char* fen) {
    const ChessBoard board(fen);
    assert(board.is_in_check(board.turn));
    assert(sorted_move_keys(board.generate_evasions(board.turn)) ==
           sorted_move_keys(board.generate_moves(board.turn)));
}

}  // namespace

int main() {
    ChessBoard start;
    start.initialize();
    assert_bitboard_invariants(start);
    assert(start.generate_moves(WHITE).size() == 20);
    assert(start.generate_moves(BLACK).size() == 20);

    const std::array<const char*, 9> kFens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pp1ppppp/8/2p5/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1",
        "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
        "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1",
        "3r2k1/5ppp/8/3Pp3/8/8/5PPP/3R2K1 w - e6 0 1",
        "3r4/5QBk/Pqr3p1/1N3pPp/1P2bP1P/8/3R4/R4K2 b - - 0 1",
        "r3k2r/8/8/8/8/8/8/Q3K2R w Kkq - 0 1",
    };

    for (const char* fen : kFens) {
        const ChessBoard board(fen);
        assert_bitboard_invariants(board);
        for (const Move& move : board.generate_moves(board.turn)) {
            const ChessBoard next = board.make_move(move);
            assert_bitboard_invariants(next);
        }
    }

    const std::array<const char*, 8> kCheckedFens = {
        "4k3/8/8/8/8/8/4r3/4K3 w - - 0 1",
        "4k3/8/8/8/8/3n1n2/8/4K3 w - - 0 1",
        "4k3/8/8/8/8/8/4q3/R3K2R w KQ - 0 1",
        "4k3/8/8/8/1b6/8/8/R3K2R w KQ - 0 1",
        "4k3/8/8/8/8/8/3p4/4K3 w - - 0 1",
        "4k3/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
        "4r3/8/8/8/8/2B5/8/4K3 w - - 0 1",
        "7k/8/8/8/7q/5N2/8/4K3 w - - 0 1",
    };

    for (const char* fen : kCheckedFens) {
        assert_evasions_match_legal_moves(fen);
    }

    return 0;
}
