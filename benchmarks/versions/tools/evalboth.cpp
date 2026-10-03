// Reads FENs on stdin, prints "<handcrafted evaluate()>\t<evaluate_nnue()>" per line,
// using the engine library built from the repo's current source (what Heroku runs).
#include "chess/chessboard.h"
#include "chess/minimax.h"

#include <iostream>
#include <string>

int main() {
    std::string fen;
    Minimax minimax(1);
    while (std::getline(std::cin, fen)) {
        if (fen.empty()) {
            continue;
        }
        const ChessBoard board(fen);
        std::cout << minimax.evaluate(board) << "\t" << minimax.evaluate_nnue(board) << "\n";
    }
}
