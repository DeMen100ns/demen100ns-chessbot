#include "chess/chessboard.h"
#include "chess/minimax.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string trim(std::string value) {
    const std::size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool parse_positive_int(const char* text, int& value) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 1000) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

std::vector<std::string> load_fens(const fs::path& fen_path) {
    std::ifstream input(fen_path);
    if (!input) {
        throw std::runtime_error("Failed to open " + fen_path.string());
    }

    std::string first_line;
    if (!std::getline(input, first_line)) {
        throw std::runtime_error("Missing count line in " + fen_path.string());
    }

    const std::string trimmed_count = trim(first_line);
    if (trimmed_count.empty()) {
        throw std::runtime_error("Empty count line in " + fen_path.string());
    }

    char* end = nullptr;
    const long expected_count = std::strtol(trimmed_count.c_str(), &end, 10);
    if (end == trimmed_count.c_str() || *end != '\0' || expected_count <= 0) {
        throw std::runtime_error("Invalid count line in " + fen_path.string());
    }

    std::vector<std::string> fens;
    fens.reserve(static_cast<std::size_t>(expected_count));

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            fens.push_back(line);
        }
    }

    if (static_cast<long>(fens.size()) != expected_count) {
        throw std::runtime_error(
            "Expected " + std::to_string(expected_count) + " FENs but found " +
            std::to_string(fens.size()) + " in " + fen_path.string());
    }

    return fens;
}

void print_header() {
    std::cout << std::left
              << std::setw(8) << "depth"
              << std::setw(16) << "total nodes"
              << std::setw(16) << "total qnodes"
              << std::setw(16) << "total leaves"
              << std::setw(16) << "total qleaves"
              << std::setw(20) << "avg nodes/position"
              << std::setw(20) << "avg qnodes/position"
              << std::setw(20) << "avg leaves/position"
              << std::setw(22) << "avg qleaves/position"
              << '\n';
}

void print_row(int depth,
               const Minimax::NodeStats& total,
               std::size_t position_count) {
    const double positions = static_cast<double>(position_count);

    std::cout << std::left
              << std::setw(8) << depth
              << std::setw(16) << total.nodes
              << std::setw(16) << total.qnodes
              << std::setw(16) << total.leaves
              << std::setw(16) << total.qleaves
              << std::setw(20) << std::fixed << std::setprecision(2)
              << (total.nodes / positions)
              << std::setw(20) << (total.qnodes / positions)
              << std::setw(20) << (total.leaves / positions)
              << std::setw(22) << (total.qleaves / positions)
              << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    int min_depth = 0;
    int max_depth = 0;
    int max_positions = 0;
    if (argc != 4 ||
        !parse_positive_int(argv[1], min_depth) ||
        !parse_positive_int(argv[2], max_depth) ||
        !parse_positive_int(argv[3], max_positions) ||
        min_depth > max_depth) {
        std::cerr << "Usage: bench_time <min_depth> <max_depth> <positions>\n";
        return 1;
    }

    try {
        const fs::path fen_path = fs::path(__FILE__).parent_path() / "fen.txt";
        std::vector<std::string> fens = load_fens(fen_path);
        if (max_positions > static_cast<int>(fens.size())) {
            throw std::runtime_error(
                "Requested " + std::to_string(max_positions) + " positions but only " +
                std::to_string(fens.size()) + " are available in " + fen_path.string());
        }
        fens.resize(static_cast<std::size_t>(max_positions));

        std::cout << "Loaded " << fens.size() << " positions from " << fen_path.string() << '\n';
        print_header();

        for (int depth = min_depth; depth <= max_depth; ++depth) {
            Minimax::NodeStats total_stats;

            for (const std::string& fen : fens) {
                const ChessBoard board(fen);
                Minimax engine(depth);
                (void)engine.find_best_move(board, depth);
                const Minimax::NodeStats& stats = engine.get_last_node_stats();
                total_stats.nodes += stats.nodes;
                total_stats.qnodes += stats.qnodes;
                total_stats.leaves += stats.leaves;
                total_stats.qleaves += stats.qleaves;
            }

            print_row(depth, total_stats, fens.size());
        }
    } catch (const std::exception& ex) {
        std::cerr << "bench_time error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
