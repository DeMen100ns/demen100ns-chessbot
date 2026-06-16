#include "bot.h"
#include "minimax.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct SearchResult {
    std::string best_move_uci;
    int static_eval = 0;
    int current_repetition = 0;
    int completed_depth = 0;
    std::size_t history_positions = 0;
    std::string debug_info;
};

std::string move_to_uci(const Move& move) {
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
        throw std::invalid_argument("Move is out of bounds");
    }

    std::string text = "a1a1";
    text[0] = static_cast<char>('a' + (move.from % 8));
    text[1] = static_cast<char>('1' + (move.from / 8));
    text[2] = static_cast<char>('a' + (move.to % 8));
    text[3] = static_cast<char>('1' + (move.to / 8));

    switch (move.promotion) {
        case W_QUEEN:
        case B_QUEEN:
            text.push_back('q');
            break;
        case W_ROOK:
        case B_ROOK:
            text.push_back('r');
            break;
        case W_BISHOP:
        case B_BISHOP:
            text.push_back('b');
            break;
        case W_KNIGHT:
        case B_KNIGHT:
            text.push_back('n');
            break;
        case EMPTY:
            break;
        default:
            throw std::invalid_argument("Unsupported promotion piece");
    }

    return text;
}

void print_usage() {
    std::cerr << "Usage: chess_engine_bridge [--serve] --fen \"<FEN>\" [--depth N] "
                 "[--time-limit-ms N] [--history-fen \"<FEN>\"]...\n";
}

std::vector<std::string> split_tab_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

SearchResult run_search(Bot& ai,
                        const std::string& fen,
                        int depth,
                        int time_limit_ms,
                        const std::vector<std::string>& history_fens) {
    ChessBoard board(fen);
    const std::vector<Move> legal_moves = board.generate_moves(board.turn);
    if (legal_moves.empty()) {
        throw std::runtime_error("Position has no legal moves");
    }

    ai.depth = depth;
    ai.position_history.clear();
    for (const std::string& history_fen : history_fens) {
        ai.record_position(ChessBoard(history_fen));
    }

    Minimax evaluator(depth);
    const std::uint64_t current_key = board.position_key();
    const int current_repetition =
        ai.position_history.count(current_key) ? ai.position_history[current_key] : 0;
    const Move best_move = ai.choose_move_timed(board, time_limit_ms);

    SearchResult result;
    result.best_move_uci = move_to_uci(best_move);
    result.static_eval = evaluator.evaluate(board);
    result.current_repetition = current_repetition;
    result.completed_depth = ai.get_last_search_completed_depth();
    result.history_positions = history_fens.size();
    result.debug_info = ai.get_last_move_debug();
    return result;
}

void configure_bridge_tablebase(Bot& ai) {
    const char* enable_tb = std::getenv("CHESS_BRIDGE_ENABLE_TB");
    if (enable_tb == nullptr || enable_tb[0] == '\0' || std::string(enable_tb) == "0") {
        ai.disable_online_tablebase();
        return;
    }

    const char* tablebase_source = std::getenv("CHESS_ONLINE_TB_URL");
    if (tablebase_source == nullptr || tablebase_source[0] == '\0') {
        ai.disable_online_tablebase();
        return;
    }

    const char* timeout_ms_env = std::getenv("CHESS_ONLINE_TB_TIMEOUT_MS");
    const int timeout_ms = (timeout_ms_env != nullptr && timeout_ms_env[0] != '\0')
        ? std::max(1, std::atoi(timeout_ms_env))
        : 100;
    ai.enable_online_tablebase(tablebase_source, timeout_ms);
}

int serve_loop() {
    Bot ai(64);
    configure_bridge_tablebase(ai);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "ping") {
            std::cout << "pong\n" << std::flush;
            continue;
        }
        if (line == "quit") {
            std::cout << "bye\n" << std::flush;
            return 0;
        }
        if (line == "newgame") {
            ai.reset_history();
            std::cout << "ready\n" << std::flush;
            continue;
        }

        const std::vector<std::string> fields = split_tab_fields(line);
        if (fields.size() < 4 || fields[0] != "go") {
            std::cout << "error\tinvalid_command\n" << std::flush;
            continue;
        }

        try {
            const int depth = std::atoi(fields[1].c_str());
            const int time_limit_ms = std::atoi(fields[2].c_str());
            const std::string& fen = fields[3];
            std::vector<std::string> history_fens;
            history_fens.reserve(fields.size() > 4 ? fields.size() - 4 : 0);
            for (std::size_t i = 4; i < fields.size(); ++i) {
                history_fens.push_back(fields[i]);
            }

            const SearchResult result = run_search(ai, fen, depth, time_limit_ms, history_fens);
            std::cout << "info"
                      << "\ttime_limit_ms=" << time_limit_ms
                      << "\tstatic_eval=" << result.static_eval
                      << "\thistory_positions=" << result.history_positions
                      << "\tcurrent_repetition=" << result.current_repetition
                      << "\tcompleted_depth=" << result.completed_depth
                      << "\tbest_move=" << result.best_move_uci
                      << "\tdebug=" << result.debug_info
                      << "\n";
            std::cout << "bestmove\t" << result.best_move_uci << "\n" << std::flush;
        } catch (const std::exception& ex) {
            std::cout << "error\t" << ex.what() << "\n" << std::flush;
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        std::string fen;
        int depth = 3;
        int time_limit_ms = 1000;
        std::vector<std::string> history_fens;
        bool serve_mode = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--serve") {
                serve_mode = true;
            } else if (arg == "--fen" && i + 1 < argc) {
                fen = argv[++i];
            } else if (arg == "--depth" && i + 1 < argc) {
                depth = std::atoi(argv[++i]);
            } else if (arg == "--time-limit-ms" && i + 1 < argc) {
                time_limit_ms = std::atoi(argv[++i]);
            } else if (arg == "--history-fen" && i + 1 < argc) {
                history_fens.push_back(argv[++i]);
            } else {
                print_usage();
                return 2;
            }
        }

        if (serve_mode) {
            return serve_loop();
        }

        if (fen.empty()) {
            print_usage();
            return 2;
        }

        Bot ai(64);
        configure_bridge_tablebase(ai);
        const SearchResult result = run_search(ai, fen, depth, time_limit_ms, history_fens);

        std::cerr << "bridge info: depth_arg=" << depth
                  << " time_limit_ms=" << time_limit_ms
                  << " static_eval=" << result.static_eval
                  << " history_positions=" << result.history_positions
                  << " current_repetition=" << result.current_repetition
                  << " completed_depth=" << result.completed_depth
                  << " best_move=" << result.best_move_uci << "\n";
        std::cout << result.best_move_uci << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "engine_bridge error: " << ex.what() << "\n";
        return 1;
    }
}
