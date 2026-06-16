#include "chessboard.h"
#include "minimax.h"

#include <array>
#include <chrono>
#include <cstdio>

namespace {
double elapsed_ms(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
}  // namespace

int main() {
    const std::array<const char*, 6> kFens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pp1ppppp/8/2p5/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "3r2k1/5ppp/8/3Pp3/8/8/5PPP/3R2K1 w - e6 0 1",
        "3r4/5QBk/Pqr3p1/1N3pPp/1P2bP1P/8/3R4/R4K2 b - - 0 1",
        "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1",
    };

    std::printf("Official minimax benchmark over %zu FENs\n", kFens.size());
    for (int depth = 1; depth <= 6; ++depth) {
        double total_ms = 0.0;

        for (const char* fen : kFens) {
            const ChessBoard board(fen);
            Minimax engine(depth);
            const auto start = std::chrono::steady_clock::now();
            (void)engine.find_best_move(board, depth);
            const auto end = std::chrono::steady_clock::now();
            total_ms += elapsed_ms(start, end);
        }

        std::printf("depth=%d total_ms=%.3f avg_ms=%.3f\n",
                    depth,
                    total_ms,
                    total_ms / kFens.size());
    }

    return 0;
}
