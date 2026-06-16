#pragma once

#include "chess/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ChessBoard {
    std::array<Piece, 64> square_piece;
    std::array<std::uint64_t, 13> piece_bitboards;
    std::array<std::uint64_t, 2> color_bitboards;
    std::uint64_t occupied;
    std::uint64_t zobrist_key;
    Color turn;
    int turn_number;
    int halfmove_clock;
    bool white_can_castle_kingside;
    bool white_can_castle_queenside;
    bool black_can_castle_kingside;
    bool black_can_castle_queenside;
    int en_passant_square;

    ChessBoard()
        : square_piece{},
          piece_bitboards{},
          color_bitboards{},
          occupied(0),
          zobrist_key(0),
          turn(WHITE),
          turn_number(1),
          halfmove_clock(0),
          white_can_castle_kingside(false),
          white_can_castle_queenside(false),
          black_can_castle_kingside(false),
          black_can_castle_queenside(false),
          en_passant_square(-1) {}

    explicit ChessBoard(const std::string& fen);

    void initialize();
    std::vector<Move> generate_pseudo_moves(Color color) const;
    std::vector<Move> generate_moves(Color color) const;
    std::vector<Move> generate_evasions(Color color) const;
    std::vector<Move> generate_quiescence_moves(Color color,
                                                bool include_quiet_checks) const;
    ChessBoard make_move(const Move& move) const;
    bool valid_move(const Move& move, Color color) const;
    bool is_square_attacked(int square, Color by_color) const;
    bool is_in_check(Color color) const;
    bool is_checkmate(Color color) const;
    bool is_stalemate(Color color) const;
    int count_pieces() const;
    std::uint64_t position_key() const;
    Piece piece_at(int square) const {
        return square_piece[static_cast<std::size_t>(square)];
    }
    bool is_empty(int square) const { return (occupied & (1ULL << square)) == 0; }
    bool has_piece(int square, Piece piece) const {
        return (piece_bitboards[static_cast<std::size_t>(piece)] & (1ULL << square)) != 0;
    }
    bool has_color_piece(int square, Color color) const {
        return (color_bitboards[static_cast<std::size_t>(color)] & (1ULL << square)) != 0;
    }
    std::uint64_t piece_bitboard(Piece piece) const;
    std::uint64_t color_bitboard(Color color) const;

private:
    void clear_square(int square);
    void set_square(int square, Piece piece);
};
