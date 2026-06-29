#pragma once

#include "chess/chessboard.h"

#include <optional>
#include <string>

namespace ChessIO {

char piece_to_fen(Piece piece);
char piece_to_upper_letter(Piece piece);

std::string square_to_algebraic(int square);
std::optional<int> square_from_algebraic(const std::string& text);

std::string board_to_fen(const ChessBoard& board);
std::string move_to_uci(const Move& move);
std::optional<Move> move_from_uci(const std::string& uci);
std::string trim(std::string value);

}  // namespace ChessIO
