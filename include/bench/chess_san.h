#pragma once

#include "chess/chessboard.h"

#include <optional>
#include <string>

namespace ChessSan {

std::string normalize_san(std::string san);
std::string move_to_san(const ChessBoard& board, const Move& move);
std::optional<Move> move_from_san(const ChessBoard& board, const std::string& san);

}  // namespace ChessSan
