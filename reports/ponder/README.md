# Ponder validation — 2026-09-16

## Implemented behavior

- Predicted legal opponent reply from the TT after a completed depth >= 2.
- Ponder starts only after successful move submission, on the predicted reply position.
- One background search worker per bridge; input remains responsive and output is serialized.
- Hit continues the existing search with new clock limits and soft-time credit for pondering.
- Finished/capped ponder results remain available until hit or cancellation.
- Miss, takeback, mismatched history, game end, replacement, newgame, quit and EOF cancel/join.
- The full position history is matched before reuse, not just the root FEN.
- Original validation used a 30-second ponder cap; the default was subsequently raised to 90 seconds. No tablebase probes run during ponder.
- Local engine configuration and the project example enable ponder; correspondence stays unchanged.

## Checks

- All six Release C++ test programs passed: test_ponder, test_time_management,
  test_minimax, test_minimax_tt, test_minimax_regression, test_bot_forced_ep.
- 81 targeted Python tests passed across test_homemade_bridge.py and test_bridge_ponder.py.
  Coverage includes real bridge processes, live hit with positive soft budget, cap/reuse,
  cancellation responsiveness, state/history mismatches, and submission ordering.
- Existing mocked lichess-bot test_bot.py::test_homemade passed (local IPC permission required).
- No real Lichess game or external message was sent during validation.
- git diff --check passed.

## Concurrency analysis and limitation

Only the worker accesses Bot, TT, or search results during ponder. The input thread
publishes hit limits once, before an atomic release-store; the worker acquires that
flag before reading them. Cancellation is atomic. Results and errors are read or
cleared by the input thread only after joining the worker. Replacing or clearing a
search always joins first. Search cancellation remains cooperative between engine
operations rather than a hard real-time interrupt.

A ThreadSanitizer build completed, but its runtime exited with signal 11 (exit 139)
without diagnostics on this host. A minimal standalone atomic/thread program built
with the same sanitizer also exited 139, both inside and outside the sandbox.
Therefore no successful ThreadSanitizer race check is claimed. Functional threaded
and process-lifecycle tests passed; running TSan on a supported toolchain/host would
provide additional concurrency validation.

Playing-strength improvement has not been measured. Ponder hit rate depends on the
opponent, and each concurrent game may consume a CPU worker during the opponent's turn.
