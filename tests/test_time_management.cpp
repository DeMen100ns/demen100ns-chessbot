#include "chess/bot.h"
#include "chess/io.h"

#include <cassert>
#include <chrono>
#include <string>

int main() {
    ChessBoard board;
    board.initialize();
    Minimax engine(3, 1024);

    // Fixed-depth callers keep zero == unlimited and are unaffected by adaptation.
    const Move fixed = engine.find_best_move(board, 2, 0);
    assert(board.valid_move(fixed, board.turn));
    assert(engine.get_last_completed_depth() == 2);
    assert(std::string(engine.last_stop_reason) == "max_depth");

    // Already-spent time includes work done before entering the search.
    SearchLimits expired;
    expired.soft_ms = 10;
    expired.hard_ms = 40;
    expired.start -= std::chrono::seconds(1);
    const Move fallback = engine.find_best_move(board, 64, expired);
    assert(board.valid_move(fallback, board.turn));
    assert(engine.get_last_completed_depth() == 0);
    assert(engine.get_last_node_stats().nodes == 0);
    assert(std::string(engine.last_stop_reason) == "hard_limit");

    SearchLimits zero;
    zero.soft_ms = zero.hard_ms = 0;
    assert(board.valid_move(engine.find_best_move(board, 64, zero), board.turn));
    assert(engine.get_last_completed_depth() == 0);

    // Soft budget exhausted during preparation still allows a completed first
    // iteration when hard time remains, then stops at the iteration boundary.
    SearchLimits soft;
    soft.soft_ms = 0;
    soft.hard_ms = 5000;
    const Move shallow = engine.find_best_move(board, 64, soft);
    assert(board.valid_move(shallow, board.turn));
    assert(engine.get_last_completed_depth() == 1);
    assert(std::string(engine.last_stop_reason) == "soft_limit");

    SearchLimits hard;
    hard.soft_ms = hard.hard_ms = 20;
    assert(board.valid_move(engine.find_best_move(board, 64, hard), board.turn));
    assert(engine.last_elapsed_ms < 500); // Scheduling tolerance, not a 20ms real-time guarantee.
    assert(std::string(engine.last_stop_reason) == "hard_limit" ||
           std::string(engine.last_stop_reason) == "soft_limit");

    // A timeout must not poison the next search's stop flag or completed result.
    assert(board.valid_move(engine.find_best_move(board, 2, 0), board.turn));
    assert(engine.get_last_completed_depth() == 2);

    ChessBoard forced("7k/8/5K2/8/8/8/8/7R b - - 0 1");
    assert(forced.generate_moves(forced.turn).size() == 1);
    Bot bot(64, 1024);
    SearchLimits forced_limits;
    forced_limits.soft_ms = 1000;
    forced_limits.hard_ms = 4000;
    assert(forced.valid_move(bot.choose_move(forced, 64, forced_limits), forced.turn));
    assert(bot.get_last_search_completed_depth() == 0);
    assert(bot.get_last_move_debug().find("stop_reason=forced_move") != std::string::npos);

    // Stability saves time; changed moves and falling eval permit more time.
    assert(search_time_factor(4, false, 0) < search_time_factor(0, false, 0));
    assert(search_time_factor(0, true, 0) > search_time_factor(0, false, 0));
    assert(search_time_factor(0, false, 100) > search_time_factor(0, false, 0));
    assert(search_time_factor(0, true, Chess::MAX_SCORE) <= 3.0);

    // Timed helper probes can consume cached answers but must not launch a
    // blocking external process. Skips must not poison the tablebase cache.
    OnlineTablebase tb;
    tb.enabled = true;
    tb.backend = TablebaseBackend::HelperScript;
    const auto fen = ChessIO::board_to_fen(forced);
    assert(!tb.choose_move(forced, true).has_value());
    assert(tb.last_probe_debug == "tb_skipped_unbounded_probe");
    assert(tb.move_cache.empty());
    tb.move_cache[fen] = forced.generate_moves(forced.turn)[0];
    assert(tb.choose_move(forced, true).has_value());
    assert(tb.last_probe_debug == "tb_hit_cache");
}
