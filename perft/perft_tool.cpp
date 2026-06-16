#include "chessboard.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* kStartPositionFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct Options {
    int depth = 1;
    std::string fen = kStartPositionFen;
    bool divide = false;
};

std::string move_to_uci(const Move& move) {
    std::string uci = "a1a1";
    uci[0] = static_cast<char>('a' + (move.from % 8));
    uci[1] = static_cast<char>('1' + (move.from / 8));
    uci[2] = static_cast<char>('a' + (move.to % 8));
    uci[3] = static_cast<char>('1' + (move.to / 8));
    if (move.promotion != EMPTY) {
        switch (move.promotion) {
            case W_QUEEN:
            case B_QUEEN: uci.push_back('q'); break;
            case W_ROOK:
            case B_ROOK: uci.push_back('r'); break;
            case W_BISHOP:
            case B_BISHOP: uci.push_back('b'); break;
            case W_KNIGHT:
            case B_KNIGHT: uci.push_back('n'); break;
            case EMPTY: break;
            default: break;
        }
    }
    return uci;
}

std::uint64_t perft(const ChessBoard& board, int depth) {
    if (depth == 0) {
        return 1;
    }

    const std::vector<Move> moves = board.generate_moves(board.turn);
    if (depth == 1) {
        return static_cast<std::uint64_t>(moves.size());
    }

    std::uint64_t nodes = 0;
    for (const Move& move : moves) {
        nodes += perft(board.make_move(move), depth - 1);
    }
    return nodes;
}

void print_usage(const char* program_name) {
    std::cerr
        << "Usage: " << program_name << " --depth <n> [--fen <fen>] [--divide]\n"
        << "Defaults:\n"
        << "  --fen    start position\n"
        << "  --depth  1\n";
}

std::optional<Options> parse_args(int argc, char* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--depth") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after --depth\n";
                return std::nullopt;
            }
            options.depth = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "--fen") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after --fen\n";
                return std::nullopt;
            }
            options.fen = argv[++i];
            continue;
        }
        if (arg == "--divide") {
            options.divide = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return std::nullopt;
        }

        std::cerr << "Unknown argument: " << arg << '\n';
        return std::nullopt;
    }

    if (options.depth < 0) {
        std::cerr << "Depth must be non-negative\n";
        return std::nullopt;
    }

    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = parse_args(argc, argv);
        if (!options.has_value()) {
            return 1;
        }

        const ChessBoard board(options->fen);
        const auto start = std::chrono::steady_clock::now();

        std::uint64_t nodes = 0;
        if (options->divide && options->depth > 0) {
            std::vector<std::pair<std::string, std::uint64_t>> breakdown;
            const std::vector<Move> moves = board.generate_moves(board.turn);
            breakdown.reserve(moves.size());
            for (const Move& move : moves) {
                const std::uint64_t move_nodes =
                    perft(board.make_move(move), options->depth - 1);
                breakdown.emplace_back(move_to_uci(move), move_nodes);
                nodes += move_nodes;
            }

            std::sort(breakdown.begin(), breakdown.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });

            for (const auto& [uci, move_nodes] : breakdown) {
                std::cout << uci << ": " << move_nodes << '\n';
            }
        } else {
            nodes = perft(board, options->depth);
        }

        const auto end = std::chrono::steady_clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        const double seconds = elapsed_ms / 1000.0;
        const double nps = seconds > 0.0 ? static_cast<double>(nodes) / seconds : 0.0;

        std::cout << "fen: " << options->fen << '\n'
                  << "depth: " << options->depth << '\n'
                  << "nodes: " << nodes << '\n'
                  << std::fixed << std::setprecision(3)
                  << "elapsed_ms: " << elapsed_ms << '\n'
                  << std::setprecision(0)
                  << "nps: " << nps << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "perft_tool error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
