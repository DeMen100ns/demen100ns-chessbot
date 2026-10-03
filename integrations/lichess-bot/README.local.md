# Local Bridge Notes

This vendored `lichess-bot` copy is kept separate from the C++ engine core.

## Bridge Flow

1. `lichess-bot` receives a board position from Lichess.
2. `CppBridge` in `homemade.py` calls the local C++ executable.
3. `chess_engine_bridge` prints one UCI move.
4. `lichess-bot` sends that move back to Lichess.

## Build The C++ Bridge

From the project root:

```bash
./build --target chess_engine_bridge
```

## Configure The Vendored lichess-bot

From `integrations/lichess-bot/`:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Then:

```bash
cp config.yml.default config.yml
```

Then copy the `engine:`, `challenge:`, and `matchmaking:` values you want from
`config.yml.project.example` into `config.yml`.

At minimum, make sure `config.yml` uses:

- `engine.protocol: "homemade"`
- `engine.name: "CppBridge"`
- `engine.dir: "../.."`
- `engine.working_dir: "../.."`

Then set your token plus any challenge or matchmaking preferences.

## Run

From `integrations/lichess-bot/`:

```bash
source .venv/bin/activate
python3 lichess-bot.py
```

Optional overrides:

```bash
export CPP_CHESS_ENGINE_BIN=/absolute/path/to/build-release/chess_engine_bridge
export CPP_CHESS_ENGINE_DEPTH=3
```

## Clock Management

Build `chess_engine_bridge` after updating the source. Clock games use adaptive
soft/hard budgets; `CPP_CHESS_ENGINE_MAX_DEPTH` defaults to 64 and takes precedence
over the older `CPP_CHESS_ENGINE_DEPTH` setting. A low depth override can stop the
engine before it uses its allocated time. Restart a running lichess-bot process
to load the updated Python code and bridge binary.

`homemade.compute_time_limits` allocates milliseconds as follows:

```text
reserve = min(250, max(2, clock / 20))
usable  = max(0, clock - reserve)
horizon = remaining_moves clamped to [1, 30], or 30 when unspecified
base    = usable / horizon + 0.8 * increment
soft    = min(base, 0.15 * usable)
hard    = min(4 * soft, 0.35 * usable)
```

Positive budgets are rounded down to milliseconds, with a minimum of 1 ms.
Less than 4 ms of usable clock produces an immediate legal fallback. The clock
received from lichess-bot already excludes its configured network/setup overhead;
the reserve above is an additional small cushion, not a second network deduction.
Explicit `Limit(time=...)` requests retain fixed-movetime behavior and may be less
than 50 ms. A single permitted move is returned immediately. To avoid cold process
startup on an emergency clock, a cold bridge with at most 50 ms of hard budget also
returns a legal fallback. A warm bridge does this at at most 2 ms.

After each completed depth, C++ adjusts the soft target:

- Same best move for 2 or 4 consecutive depth transitions: multiply by 0.8 or 0.65.
- Best move changed since the previous completed depth: multiply by 1.5.
- Eval fell since the previous completed depth: multiply by up to 2, reaching 2 at
  a 100-centipawn drop. Depth 1 is not compared with static evaluation.
- The combined multiplier is bounded to [0.65, 3], and the target cannot exceed hard.

The soft target is checked at completed-depth boundaries. The hard deadline is
checked inside search, and an interrupted depth cannot replace the last completed
result. These are initial heuristics; strength requires longer paired matches.
Next-iteration cost prediction remains deferred. Pondering is described below.

Python charges history reconstruction and process startup to the budget before
sending it. C++ carries one start time through request parsing, preparation,
tablebase lookup, and search. Forced moves and expired requests skip search.
Deadline enforcement is cooperative: individual move-generation/evaluation
operations and OS scheduling can cause overruns. Native disk probes are synchronous
and have no strict timeout.
Blocking tablebase helper scripts are not started during timed play; cached helper
results remain usable. Untimed analysis can still populate that cache. Native
Fathom probes remain enabled and their time is charged to the request.

### Bridge Protocol and Diagnostics

Fields below are separated by tabs:

```text
capabilities                                      -> ready<TAB>go_clock<TAB>ponder
go_clock<TAB>depth<TAB>soft_ms<TAB>hard_ms<TAB>fen[<TAB>history_fen...]
go<TAB>depth<TAB>movetime_ms<TAB>fen[<TAB>history_fen...]
```

`go_clock` requires `0 <= soft_ms <= hard_ms`. Hard zero returns a legal fallback;
soft zero with positive hard allows a first completed iteration, then stops. The
legacy `go` command and C++ integer overloads still interpret zero as unlimited
fixed-depth analysis. Do not send an emergency zero through that legacy API.
For one-shot adaptive searches, use `--soft-time-limit-ms N --time-limit-ms M`.

The Python adapter negotiates capabilities once per process. Older binaries use
the new soft allocation as fixed movetime, with a warning that adaptation requires
a rebuild. Logs include the clock, soft/hard budgets, setup time, total elapsed
time, completed depth, best-move changes, adjusted target, and stop reason.

### Targeted Validation

From the project root:

```bash
cmake --build build-release --target chess_engine_bridge test_time_management
./build-release/test_time_management
cd integrations/lichess-bot
PYTHONPATH=. .venv/bin/python -m pytest test_bot/test_homemade_bridge.py
```

The Python suite includes a persistent-process protocol test when the release
bridge is present. Install the test dependencies from `test_bot/test-requirements.txt`
if needed. For meaningful strength comparisons, keep compiler options, evaluator,
hash size, and openings identical and swap colors between versions. Fixed-movetime
benchmarks do not exercise adaptive clock allocation.

## Pondering

`engine.ponder: true` enables thinking during the opponent's turn for CppBridge.
The local config and project example enable it. Restart lichess-bot after rebuilding
the bridge. Older binaries without the `ponder` capability continue playing normally
without background search. The wrapper's existing suppression during the first two
plies still applies.

The engine returns a legal predicted opponent reply from the transposition table
after a completed search of at least depth 2. After Lichess accepts our move, the
adapter starts a background search of the position **after that predicted reply**.
No prediction means no ponder; forced/emergency moves and tablebase-only results
usually have no prediction. Ponder setup never delays submission of the chosen move.

- **Hit:** the actual position and complete position history match the prediction.
  The same search continues with fresh soft/hard budgets. Time already spent
  pondering contributes toward the soft target, so sufficiently developed results
  can be returned immediately. The hard clock starts at `ponderhit`; opponent time
  is not charged to it. If background search already finished, its result is reused.
- **Miss:** cancel and join the background worker, then search the actual position.
  The transposition table is retained, but no stale root result is returned.
- Own-move acknowledgements leave ponder running. Takebacks, mismatched histories,
  game completion, replacement searches, `newgame`, `quit`, and stdin EOF cancel it.
  An exception during game teardown also closes the process.

One worker runs per bridge process, using the same Bot and TT exclusively. The
input thread stays responsive, and all protocol output is written by the input
thread. A background result is never printed unsolicited. Tablebase probes are
skipped for ponder because they cannot be cooperatively cancelled.

Background thinking is capped at **90,000 ms per opponent turn** by default, or the
configured maximum depth, whichever comes first. The result remains available after
the cap. Set `CPP_CHESS_PONDER_MAX_MS` to change the cap; `0` disables starting ponder.
Each simultaneous game can use one CPU worker while waiting, so disable or shorten
ponder when sharing limited CPU capacity across many games.

Additional tab-separated commands:

```text
go_ponder<TAB>depth<TAB>max_ms<TAB>fen[<TAB>history_fen...] -> ready<TAB>pondering
ponderhit<TAB>soft_ms<TAB>hard_ms                         -> info / bestmove
stop                                                    -> ready<TAB>stopped
```

`ponderhit` requires an active (running or finished) ponder and nonnegative budgets
with soft <= hard. The caller must confirm the actual position/history. `stop`
discards the background result and acknowledges only after the worker has joined.
`ping` and `capabilities` remain available during ponder.

Logs report starts, stops, hits, predicted moves, and whether the running search
observed a hit (`search_ponder_transition`). The top-level `ponder_hit=1` also covers
reuse of a ponder that had already finished before the hit arrived.

Tests:

```bash
cmake --build build-release --target chess_engine_bridge test_ponder
./build-release/test_ponder
cd integrations/lichess-bot
PYTHONPATH=. .venv/bin/python -m pytest test_bot/test_homemade_bridge.py test_bot/test_bridge_ponder.py
```
