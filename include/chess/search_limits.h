#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

struct SearchControl;

// A single start time follows the request through preparation, tablebase and search.
// Negative hard_ms means unlimited. Zero means return a legal fallback immediately.
struct SearchLimits {
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now();
    int soft_ms = -1;  // Negative disables adaptive time management (fixed movetime).
    int hard_ms = -1;
    bool pondering = false;
    SearchControl* control = nullptr; // Owner must keep this alive until search joins.

    static SearchLimits fixed(int time_ms) {
        SearchLimits limits;
        limits.hard_ms = time_ms > 0 ? time_ms : -1;  // Legacy API: 0 is unlimited.
        return limits;
    }

    bool timed() const { return hard_ms >= 0; }
    bool managed() const { return timed() && soft_ms >= 0; }
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    bool expired() const { return timed() && elapsed_ms() >= hard_ms; }
};

// One publisher (bridge input thread), one reader (search worker). Hit limits
// are written exactly once, before the release-store, and then remain immutable.
struct SearchControl {
    std::atomic<bool> stop{false};
    std::atomic<bool> hit_ready{false};
    SearchLimits hit_limits;

    void ponderhit(const SearchLimits& limits) {
        hit_limits = limits;
        hit_ready.store(true, std::memory_order_release);
    }
};

// Small, bounded heuristics; deliberately independent of Stockfish's tuned constants.
inline double search_time_factor(int stable_depths, bool best_move_changed, int eval_drop_cp) {
    const double stability = stable_depths >= 4 ? 0.65 : stable_depths >= 2 ? 0.8 : 1.0;
    const double instability = best_move_changed ? 1.5 : 1.0;
    const double falling_eval = 1.0 + std::clamp(eval_drop_cp / 100.0, 0.0, 1.0);
    return std::clamp(stability * instability * falling_eval, 0.65, 3.0);
}
