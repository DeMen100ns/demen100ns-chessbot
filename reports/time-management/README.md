# Time management validation — 2026-09-15

## Correctness

- Release targets: chess_engine_bridge, chess_bench_support, test_time_management,
  test_minimax, test_minimax_tt, test_minimax_regression, test_bot_forced_ep.
- All five C++ test executables passed with assertions enabled.
- All 59 targeted Python tests passed. They cover budget monotonicity, low clocks, increment caps, both colors,
  fixed movetime below 50 ms, preparation time, forced moves, emergency fallback,
  capability negotiation, legacy binary fallback, malformed clock commands, and
  real persistent bridge/adapter operation.

## Paired smoke matches

Baseline was built from the working source immediately before the time-management
edits, including the user's existing evaluator and draw-rule changes. Both engines
used Release builds, 2^20 TT entries, the same evaluator configuration, depth ceiling
64, no pondering and no tablebases. Processes were warmed before play and reset
between games. Engines searched sequentially on this machine.

Four games at **10 seconds + 0.1 seconds per move**, swapping colors for each of two
openings (initial position and `1. d4 d5 2. c4 e6`). The old Python bullet allocation
was reproduced (`500 ms + increment`, with its original reserve and 50 ms floor).
The new side used `compute_time_limits` and `go_clock`. Complete position history
was sent on every move. Wall time included pipe communication and C++ preparation;
these local matches did not simulate Lichess network latency or Python adapter
startup. Draws were claimed when available; play stopped at 160 additional plies.

| Measurement | Baseline | New |
|---|---:|---:|
| Legal moves checked | 263 | 264 |
| Time losses | 0 | 0 |
| Mean elapsed per move | 246.770 ms | 244.126 ms |
| p95 positive overrun beyond hard budget | 1.563 ms | 0.226 ms |
| Maximum positive overrun | 26.762 ms | 18.515 ms |
| Stops at adaptive soft target | N/A | 244 |
| Stops at hard deadline | N/A | 20 |

Two games ended in threefold repetition; two were **unfinished** at the ply cap
and are not counted as draws. This is a protocol/clock smoke test, not an Elo
measurement. The maximum overrun also demonstrates that the deadline is
cooperative rather than a strict real-time guarantee. One completed depth can
cross the soft target substantially; next-depth cost prediction remains deferred.

Raw per-move budgets, clocks, elapsed times, moves, and engine diagnostics are in
[smoke-games.json](smoke-games.json); aggregate counts are in
[smoke-summary.json](smoke-summary.json). Small subsequent cleanup removed redundant
move generation and skipped diagnostic evaluation for timed forced moves; the
final build was checked again by the automated tests.

Longer matches across bullet/blitz/rapid and no-increment controls are still needed
to tune the initial allocation/stability coefficients and measure playing strength.
