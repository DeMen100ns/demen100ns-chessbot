#include "chess/bot.h"
#include "chess/io.h"
#include "chess/minimax.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct SearchResult {
    std::string best_move_uci;
    std::string ponder_move_uci = "none";
    int static_eval = 0;
    int search_eval = 0;
    int current_repetition = 0;
    int completed_depth = 0;
    std::size_t history_positions = 0;
    double elapsed_ms = 0;
    std::string debug_info;
};

void print_usage() {
    std::cerr << "Usage: chess_engine_bridge [--serve] --fen \"<FEN>\" [--depth N] "
                 "[--time-limit-ms N] [--soft-time-limit-ms N] [--history-fen \"<FEN>\"]...\n";
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

int repetition_count_in_history(const std::vector<std::uint64_t>& history,
                                std::uint64_t key) {
    int count = 0;
    for (std::uint64_t history_key : history) {
        if (history_key == key) {
            ++count;
        }
    }
    return count;
}

std::size_t bridge_transposition_entries() {
    const char* bits_env = std::getenv("CHESS_TT_BITS");
    if (bits_env == nullptr || bits_env[0] == '\0') {
        return Minimax::kTranspositionTableSize;
    }

    const int bits = std::clamp(std::atoi(bits_env), 0, 20);
    return std::size_t{1} << bits;
}

SearchResult run_search(Bot& ai,
                        const std::string& fen,
                        int depth,
                        const SearchLimits& limits,
                        const std::vector<std::string>& history_fens) {
    ChessBoard board(fen);
    const std::vector<Move> legal_moves = board.generate_moves(board.turn);
    if (legal_moves.empty()) {
        throw std::runtime_error("Position has no legal moves");
    }

    ai.position_history.clear();
    for (const std::string& history_fen : history_fens) {
        if (limits.control && limits.control->stop.load(std::memory_order_relaxed)) {
            throw std::runtime_error("search_cancelled");
        }
        ai.record_position(ChessBoard(history_fen));
    }

    Minimax evaluator(depth, 0);
    const std::uint64_t current_key = board.position_key();
    const int current_repetition = repetition_count_in_history(ai.position_history, current_key);
    // Include diagnostic evaluation in the request budget, not after the deadline.
    const bool immediate_reply = limits.expired() || (limits.timed() && legal_moves.size() == 1);
    const int static_eval = immediate_reply ? 0 : evaluator.evaluate(board);
    const Move best_move = ai.choose_move(board, depth, limits);

    SearchResult result;
    result.best_move_uci = ChessIO::move_to_uci(best_move);
    if (ai.get_last_search_completed_depth() >= 2) {
        if (const auto reply = ai.searcher.ponder_move(board, best_move); reply.has_value()) {
            result.ponder_move_uci = ChessIO::move_to_uci(*reply);
        }
    }
    result.static_eval = static_eval;
    result.search_eval = ai.get_last_search_eval();
    result.current_repetition = current_repetition;
    result.completed_depth = ai.get_last_search_completed_depth();
    result.history_positions = history_fens.size();
    result.debug_info = ai.get_last_move_debug();
    result.elapsed_ms = limits.elapsed_ms();
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

int parse_ms(const std::string& value) {
    std::size_t used = 0;
    const int ms = std::stoi(value, &used);
    if (ms < 0 || used != value.size()) {
        throw std::runtime_error("invalid_clock_budget");
    }
    return ms;
}

void print_result(const SearchResult& result, const SearchLimits& limits, bool ponder_hit = false) {
    std::cout << "info"
              << "\ttime_limit_ms=" << limits.hard_ms
              << "\tsoft_ms=" << limits.soft_ms
              << "\thard_ms=" << limits.hard_ms
              << "\telapsed_ms=" << limits.elapsed_ms()
              << "\tstatic_eval=" << result.static_eval
              << "\tsearch_eval=" << result.search_eval
              << "\thistory_positions=" << result.history_positions
              << "\tcurrent_repetition=" << result.current_repetition
              << "\tcompleted_depth=" << result.completed_depth
              << "\tbest_move=" << result.best_move_uci
              << "\tponder_move=" << result.ponder_move_uci
              << "\tponder_hit=" << ponder_hit
              << "\tdebug=" << result.debug_info << "\n";
    std::cout << "bestmove\t" << result.best_move_uci << "\n" << std::flush;
}

// Only the worker touches Bot during a ponder. Input handling never reads its
// search state or TT until join, and only this input thread writes stdout.
class PonderSearch {
public:
    explicit PonderSearch(Bot& bot) : ai(bot) {}
    ~PonderSearch() { cancel(); }
    bool active() const { return worker.joinable(); }

    void cancel() {
        if (worker.joinable()) {
            control->stop.store(true, std::memory_order_relaxed);
            worker.join();
        }
        control.reset();
        result.reset();
        error.clear();
    }

    void start(const std::string& fen, int depth, SearchLimits limits,
               const std::vector<std::string>& history) {
        cancel();
        control = std::make_unique<SearchControl>();
        limits.control = control.get();
        limits.pondering = true;
        worker = std::thread([this, fen, depth, limits, history] {
            try {
                result = run_search(ai, fen, depth, limits, history);
            } catch (const std::exception& ex) {
                error = ex.what();
            } catch (...) {
                error = "ponder_search_failed";
            }
        });
    }

    SearchResult hit(const SearchLimits& limits) {
        if (!active()) {
            throw std::runtime_error("no_active_ponder");
        }
        control->ponderhit(limits);
        worker.join();
        control.reset();
        if (!error.empty()) {
            const std::string message = error;
            error.clear();
            throw std::runtime_error(message);
        }
        SearchResult answer = std::move(*result);
        result.reset();
        return answer;
    }

private:
    Bot& ai;
    std::unique_ptr<SearchControl> control;
    std::thread worker;
    std::optional<SearchResult> result;
    std::string error;
};

int serve_loop() {
    Bot ai(64, bridge_transposition_entries());
    configure_bridge_tablebase(ai);
    PonderSearch ponder(ai);

    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request_start = SearchLimits::Clock::now();
        if (line == "ping") {
            std::cout << "pong\n" << std::flush;
            continue;
        }
        if (line == "capabilities") {
            std::cout << "ready\tgo_clock\tponder\n" << std::flush;
            continue;
        }
        if (line == "quit") {
            ponder.cancel();
            std::cout << "bye\n" << std::flush;
            return 0;
        }
        if (line == "newgame") {
            ponder.cancel();
            ai.reset_history();
            std::cout << "ready\n" << std::flush;
            continue;
        }

        if (line == "stop") {
            ponder.cancel();
            std::cout << "ready\tstopped\n" << std::flush;
            continue;
        }
        const std::vector<std::string> fields = split_tab_fields(line);
        if (!fields.empty() && (fields[0] == "go_ponder" || fields[0] == "ponderhit")) {
            try {
                if (fields[0] == "ponderhit") {
                    if (fields.size() != 3) {
                        throw std::runtime_error("invalid_ponderhit");
                    }
                    SearchLimits limits;
                    limits.start = request_start;
                    limits.soft_ms = parse_ms(fields[1]);
                    limits.hard_ms = parse_ms(fields[2]);
                    if (limits.soft_ms > limits.hard_ms) {
                        throw std::runtime_error("soft_budget_exceeds_hard_budget");
                    }
                    print_result(ponder.hit(limits), limits, true);
                } else {
                    if (fields.size() < 4) {
                        throw std::runtime_error("invalid_go_ponder");
                    }
                    SearchLimits limits;
                    limits.start = request_start;
                    limits.hard_ms = parse_ms(fields[2]);
                    std::vector<std::string> history(fields.begin() + 4, fields.end());
                    ponder.start(fields[3], std::stoi(fields[1]), limits, history);
                    std::cout << "ready\tpondering\n" << std::flush;
                }
            } catch (const std::exception& ex) {
                std::cout << "error\t" << ex.what() << "\n" << std::flush;
            }
            continue;
        }
        const bool managed = !fields.empty() && fields[0] == "go_clock";
        if (fields.size() < (managed ? 5u : 4u) || (!managed && fields[0] != "go")) {
            std::cout << "error\tinvalid_command\n" << std::flush;
            continue;
        }

        try {
            const int depth = std::atoi(fields[1].c_str());
            SearchLimits limits;
            if (managed) {
                limits.soft_ms = parse_ms(fields[2]);
                limits.hard_ms = parse_ms(fields[3]);
                if (limits.soft_ms > limits.hard_ms) {
                    throw std::runtime_error("soft_budget_exceeds_hard_budget");
                }
            } else {
                limits = SearchLimits::fixed(std::atoi(fields[2].c_str()));
            }
            limits.start = request_start;
            const std::size_t fen_index = managed ? 4 : 3;
            const std::string& fen = fields[fen_index];
            std::vector<std::string> history_fens;
            history_fens.reserve(fields.size() - fen_index - 1);
            for (std::size_t i = fen_index + 1; i < fields.size(); ++i) {
                history_fens.push_back(fields[i]);
            }

            ponder.cancel();
            const SearchResult result = run_search(ai, fen, depth, limits, history_fens);
            print_result(result, limits);
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
        int depth = 64;
        int time_limit_ms = 1000;
        int soft_time_limit_ms = -1;
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
            } else if (arg == "--soft-time-limit-ms" && i + 1 < argc) {
                soft_time_limit_ms = std::stoi(argv[++i]);
                if (soft_time_limit_ms < 0) {
                    throw std::runtime_error("invalid_soft_time_limit");
                }
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

        Bot ai(64, bridge_transposition_entries());
        configure_bridge_tablebase(ai);
        SearchLimits limits = SearchLimits::fixed(time_limit_ms);
        if (soft_time_limit_ms >= 0) {
            if (time_limit_ms < soft_time_limit_ms) {
                throw std::runtime_error("soft_budget_exceeds_hard_budget");
            }
            limits.soft_ms = soft_time_limit_ms;
            limits.hard_ms = time_limit_ms;
        }
        const SearchResult result = run_search(ai, fen, depth, limits, history_fens);

        std::cerr << "bridge info: depth_arg=" << depth
                  << " time_limit_ms=" << time_limit_ms
                  << " static_eval=" << result.static_eval
                  << " search_eval=" << result.search_eval
                  << " history_positions=" << result.history_positions
                  << " current_repetition=" << result.current_repetition
                  << " completed_depth=" << result.completed_depth
                  << " best_move=" << result.best_move_uci
                  << " elapsed_ms=" << result.elapsed_ms
                  << " debug=" << result.debug_info << "\n";
        std::cout << result.best_move_uci << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "engine_bridge error: " << ex.what() << "\n";
        return 1;
    }
}
