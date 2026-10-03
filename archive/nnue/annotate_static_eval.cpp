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
    fs::path input = "nnue/data_1M.json";
    fs::path output = "nnue/data_1M_static.json";
    int progress_every = 10000;
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
    std::cerr << "Usage: annotate_static_eval [--input nnue/data_1M.json] "
                 "[--output nnue/data_1M_static.json] [--progress-every N]\n";
}

bool parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (arg == "--progress-every" && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], options.progress_every)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return !options.input.empty() && !options.output.empty();
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + path.string());
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string parse_json_string(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') {
        throw std::runtime_error("Expected JSON string");
    }
    ++pos;

    std::string value;
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"') {
            return value;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= text.size()) {
            throw std::runtime_error("Unterminated JSON escape");
        }
        const char escaped = text[pos++];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                throw std::runtime_error("Unsupported JSON escape in FEN");
        }
    }

    throw std::runtime_error("Unterminated JSON string");
}

int parse_json_int(const std::string& text, std::size_t& pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                                text[pos] == '\r' || text[pos] == '\t')) {
        ++pos;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text.c_str() + pos, &end, 10);
    if (end == text.c_str() + pos) {
        throw std::runtime_error("Expected JSON integer");
    }
    pos = static_cast<std::size_t>(end - text.c_str());
    return static_cast<int>(parsed);
}

int static_eval_for_white(Minimax& engine, const ChessBoard& board) {
    const int side_to_move_score = engine.evaluate(board);
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
        const std::string data = read_file(options.input);
        const fs::path tmp_output = options.output.string() + ".tmp";
        std::ofstream output(tmp_output);
        if (!output) {
            throw std::runtime_error("Failed to open output file: " + tmp_output.string());
        }

        Minimax engine(1);
        std::uint64_t count = 0;
        std::size_t pos = 0;
        const auto start = std::chrono::steady_clock::now();

        output << "{\n";
        output << "  \"source_file\": \"" << json_escape(options.input.string()) << "\",\n";
        output << "  \"depth\": 5,\n";
        output << "  \"type\": 1,\n";
        output << "  \"label\": \"depth_eval\",\n";
        output << "  \"score_clip\": 2000,\n";
        output << "  \"score_perspective\": \"white\",\n";
        output << "  \"positions\": [\n";

        while (true) {
            const std::size_t fen_key = data.find("\"fen\":\"", pos);
            if (fen_key == std::string::npos) {
                break;
            }

            std::size_t fen_pos = fen_key + std::string("\"fen\":").size();
            const std::string fen = parse_json_string(data, fen_pos);

            const std::size_t eval_key = data.find("\"eval_score\":", fen_pos);
            if (eval_key == std::string::npos) {
                throw std::runtime_error("Missing eval_score after FEN");
            }
            std::size_t eval_pos = eval_key + std::string("\"eval_score\":").size();
            const int eval_score = parse_json_int(data, eval_pos);

            const ChessBoard board(fen);
            const int static_eval = static_eval_for_white(engine, board);

            if (count > 0) {
                output << ",\n";
            }
            output << "    {\"fen\":\"" << json_escape(fen)
                   << "\",\"eval_score\":" << eval_score
                   << ",\"static_eval\":" << static_eval << "}";
            ++count;
            pos = eval_pos;

            if (options.progress_every > 0 &&
                count % static_cast<std::uint64_t>(options.progress_every) == 0) {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed_seconds =
                    std::chrono::duration<double>(now - start).count();
                const double positions_per_second =
                    elapsed_seconds > 0.0 ? static_cast<double>(count) / elapsed_seconds : 0.0;
                std::cerr << "annotated=" << count
                          << " elapsed_s=" << std::fixed << std::setprecision(1)
                          << elapsed_seconds
                          << " pos_per_s=" << std::setprecision(1)
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
        std::cerr << "done annotated=" << count
                  << " output=" << options.output.string() << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "annotate_static_eval error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
