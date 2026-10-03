#include "chess/minimax.h"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

int main() {
    ChessBoard board;
    board.initialize();
    Minimax engine(3, 1u << 16);
    const Move best = engine.find_best_move(board, 3, 0);
    const auto reply = engine.ponder_move(board, best);
    assert(reply.has_value());
    const ChessBoard after = board.make_move(best);
    assert(after.valid_move(*reply, after.turn));

    SearchControl cancelled;
    cancelled.stop.store(true);
    SearchLimits cancel_limits;
    cancel_limits.control = &cancelled;
    cancel_limits.pondering = true;
    cancel_limits.hard_ms = 5000;
    assert(board.valid_move(engine.find_best_move(board, 64, cancel_limits), board.turn));
    assert(engine.last_completed_depth == 0);
    assert(std::string(engine.last_stop_reason) == "stopped");

    // A published hit starts a new clock even when the original ponder budget
    // has expired. It must not inherit the opponent's elapsed time as hard time.
    SearchControl early_hit;
    SearchLimits pondering;
    pondering.control = &early_hit;
    pondering.pondering = true;
    pondering.hard_ms = 1;
    pondering.start -= std::chrono::seconds(5);
    SearchLimits actual;
    actual.soft_ms = 0;
    actual.hard_ms = 1000;
    early_hit.ponderhit(actual);
    const Move early = engine.find_best_move(board, 64, pondering);
    assert(board.valid_move(early, board.turn));
    assert(engine.last_ponder_hit);
    assert(engine.last_completed_depth == 1);
    assert(engine.last_ponder_ms >= 5000);
    assert(engine.last_elapsed_ms < 1000);

    // Exercise actual cross-thread publication while searching. Only the worker
    // accesses Minimax; the test inspects its results after join.
    SearchControl live;
    pondering = SearchLimits{};
    pondering.control = &live;
    pondering.pondering = true;
    pondering.hard_ms = 5000;
    Move answer(-1, -1);
    std::thread worker([&] { answer = engine.find_best_move(board, 64, pondering); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    actual = SearchLimits{};
    actual.soft_ms = 0;
    actual.hard_ms = 1000;
    live.ponderhit(actual);
    worker.join();
    assert(board.valid_move(answer, board.turn));
    assert(engine.last_ponder_hit);
    assert(engine.last_completed_depth >= 1);
    assert(std::string(engine.last_stop_reason) == "ponder_ready" ||
           std::string(engine.last_stop_reason) == "soft_limit");

    // A capped background search produces a reusable result and cannot run forever.
    pondering = SearchLimits{};
    pondering.pondering = true;
    pondering.hard_ms = 10;
    assert(board.valid_move(engine.find_best_move(board, 64, pondering), board.turn));
    assert(std::string(engine.last_stop_reason) == "ponder_limit");

    assert(board.valid_move(engine.find_best_move(board, 2, 0), board.turn));
    assert(engine.last_completed_depth == 2);
    assert(!engine.last_ponder_hit);
}
