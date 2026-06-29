#include "chess/chessboard.h"
#include "chess/minimax.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path input = "nnue/data_fen_1M";
    fs::path output = "nnue/data_1M.json";
    int depth = 8;
    int limit = 0;
    int progress_every = 100;
};

std::string json_escape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string trim(std::string value) {
    const std::size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool parse_int_arg(const char* text, int& value) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < 0 || parsed > 1000000000L) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

void print_usage() {
    std::cerr << "Usage: evaluate_fens [--input nnue/data_fen_1M] "
                 "[--output nnue/data_1M.json] [--depth 8] "
                 "[--limit N] [--progress-every N]\n";
}

bool parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (arg == "--depth" && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], options.depth) || options.depth <= 0) {
                return false;
            }
        } else if (arg == "--limit" && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], options.limit)) {
                return false;
            }
        } else if (arg == "--progress-every" && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], options.progress_every)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return !options.input.empty() && !options.output.empty() && options.depth > 0;
}

int evaluate_for_white(Minimax& engine, const ChessBoard& board, int depth) {
    (void)engine.find_best_move(board, depth, 0);
    const int side_to_move_score = engine.get_last_search_eval();
    return board.turn == WHITE ? side_to_move_score : -side_to_move_score;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        print_usage();
        return 2;
    }

    try {
        std::ifstream input(options.input);
        if (!input) {
            throw std::runtime_error("Failed to open input file: " + options.input.string());
        }

        const fs::path tmp_output = options.output.string() + ".tmp";
        std::ofstream output(tmp_output);
        if (!output) {
            throw std::runtime_error("Failed to open output file: " + tmp_output.string());
        }

        Minimax engine(options.depth);
        std::uint64_t count = 0;
        std::string line;
        const auto start = std::chrono::steady_clock::now();

        output << "{\n";
        output << "  \"depth\": " << options.depth << ",\n";
        output << "  \"score_perspective\": \"white\",\n";
        output << "  \"positions\": [\n";

        while (std::getline(input, line)) {
            const std::string fen = trim(line);
            if (fen.empty()) {
                continue;
            }
            if (options.limit > 0 && count >= static_cast<std::uint64_t>(options.limit)) {
                break;
            }

            const ChessBoard board(fen);
            const int eval_score = evaluate_for_white(engine, board, options.depth);

            if (count > 0) {
                output << ",\n";
            }
            output << "    {\"fen\":\"" << json_escape(fen)
                   << "\",\"eval_score\":" << eval_score << "}";
            ++count;

            if (options.progress_every > 0 &&
                count % static_cast<std::uint64_t>(options.progress_every) == 0) {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed_seconds =
                    std::chrono::duration<double>(now - start).count();
                const double positions_per_second =
                    elapsed_seconds > 0.0 ? static_cast<double>(count) / elapsed_seconds : 0.0;
                std::cerr << "evaluated=" << count
                          << " elapsed_s=" << std::fixed << std::setprecision(1)
                          << elapsed_seconds
                          << " pos_per_s=" << std::setprecision(3)
                          << positions_per_second << '\n';
            }
        }

        output << "\n  ]\n";
        output << "}\n";
        output.close();
        if (!output) {
            throw std::runtime_error("Failed while writing output file: " + tmp_output.string());
        }

        fs::rename(tmp_output, options.output);
        std::cerr << "done evaluated=" << count
                  << " output=" << options.output.string() << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "evaluate_fens error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
