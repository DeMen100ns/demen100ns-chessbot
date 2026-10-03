# Frozen versions benchmark

How much each frozen version in `bots/` improved over the previous one. Run on 2026-10-03.

**Scope.** Part A (speed: time to fixed depth) was run in full. Part B (strength:
head-to-head matches) was **not run**. The match runner is written and tested, but there is
no Elo table in this report (see [Part B status](#part-b-strength--not-run)). Nothing under
`bots/` or `src/` was modified while measuring.

> **Archived since this report.** In this report, "v7", "v7-nnue" and "v7.2-nnue" are the NNUE
> builds frozen on Jun 27. They now live in `archive/bots/`, and the scripts here load them from
> there. Today's `bots/v7` is a different engine: current `main` with the unused NNUE code
> removed (see [Update: new v7 vs v6.1-stack](#update-new-v7-vs-v61-stack)).

## Key findings

1. **v1 → v7 is 93× faster to depth 6** (79.0 s → 0.85 s for 40 positions). The biggest
   single step is **v3_engine (5.7×)**, then **v4.1_LMR (2.0×)**, **v4_LMR (1.7×)** and
   **v4.2_null (1.7×)**.
2. **v7 is the fastest version at every depth.** v7.2-nnue is **31% slower than v7** at depth 6.
3. **v1 through v6.1 all use the same handcrafted evaluator** (identical static evals on 40/40
   positions). Every speed difference in that range comes from search and implementation.
4. **v7 and v7.2 both evaluate with NNUE**, despite what their READMEs say:
   - **v7** uses a 768-input network. Its weights, pulled out of the binary, reproduce its evals.
   - **v7.2** uses the 770-input network in the repo today. That network adds side-to-move
     inputs and gives the side to move a flat bonus of about +85 cp.
5. **`v7` and `v7-nnue` are byte-identical binaries.**
6. **Heroku runs none of the frozen versions.** It builds current `main` (`b1c6b89`), which is
   back on the v1–v6.1 handcrafted evaluator, plus newer search features: time management,
   pondering, and draw detection inside the search.
7. **In v1–v5, `go` with `time_ms=0` doesn't search at all.** It returns the first move in
   the engine's ordered move list. All fixed-depth runs here use `time_ms=1000000000` instead.

---

## Machine and settings

| | |
|---|---|
| Machine | MacBook Air M2 (`Mac14,2`), fanless; 4 performance + 4 efficiency cores; 8 GB RAM |
| OS | macOS 26.5 (build 25F71) |
| Power | AC power (checked by the scripts); load average about 3.9 at start (system daemons, Claude app) |
| Python | 3.12.4, python-chess 1.11.2, in `benchmarks/versions/.venv` |
| Engines | `bots/<v>/<v> --serve` (v7, v7-nnue, v7.2-nnue: now `archive/bots/<v>/<v>`) started directly (not via `run.sh`), env `CHESS_BRIDGE_ENABLE_TB=0`, TB path variables removed. Peak memory 9–90 MB per engine |
| Search command | `newgame`, then `go\t<D>\t1000000000\t<FEN>` (no history). TT and history are cleared before every search |
| Depths | 4, 5, 6. Depth 7 was skipped because the rule was "add 7 only if every version does depth 6 in under 2 s per position", and v1 takes up to about 4 s |
| Passes | 1 warm-up + 5 measured. Version order v1→v7.2 in even passes and v7.2→v1 in odd passes. Each version gets a fresh process per pass |
| Timing | `time.monotonic()` from writing `go` to reading `bestmove` |
| Duration | about 224 s per pass, 22.5 min in total |

### Positions

`data/bench_positions.json` and `data/bench_positions_generated.json` are **byte-identical**:
1,000 lichess positions, all with a Stockfish depth-10 eval within ±30 cp, so none is already
decided. `select_positions.py` picks evenly spaced positions from each phase, all with more
than 5 pieces and the game not over. The result is in `positions.txt`:

| Phase | Count | Rule |
|---|---:|---|
| Opening | 14 | ply ≤ 16, ≥ 28 pieces |
| Middlegame | 16 | ply ≥ 20, ≥ 16 pieces |
| Endgame | 10 | 6–14 pieces |

`openings.txt` holds 100 further balanced positions for Part B: 50 standard openings, plus 50
for extending close pairs.

## Step 0: protocol check and source code

All 13 binaries start and answer `ping` → `pong`, `newgame` → `ready`, `go` → `info` +
`bestmove`, and `quit` → `bye`.

**The `time_ms=0` problem (v1_baseline … v5-engine).** In those versions,
`Minimax::find_best_move_timed` returns `moves[0]` after ordering when `time_limit_ms <= 0`.
The `info` line shows `completed_depth=0`, and the move is often a bad capture: in the depth-3
test it played Nxe5, losing a knight. Only v6-ordering and later treat 0 as "no limit".
Every version here is given 1,000,000,000 ms. With that, all 9,360 searches (warm-up included) reached
exactly the requested depth.

**Differences in the `info` line** (the parser in `engine.py` handles all of them):

| Field | v1 | v2–v5 | v6–v6.1 | v7+ |
|---|---|---|---|---|
| `debug=` | missing | `requested_depth=…` | `max_depth=…` | `max_depth=…` |
| `search_eval=` | missing | missing | missing | present |

Because `search_eval` is missing before v7, best-move agreement is the only cross-version
comparison of results.

**Source code and `bench_time`.** Git has only two engine snapshots from the frozen period:
- **Initial import (`b6b5b7e`, Jun 16 21:49):** behaves like v3_engine (same depth-6 best
  moves on 20/20 positions, similar time), but isn't provably identical.
- **Jun 29 commit (`9a5e177`):** matches none of the frozen binaries.

There are no local backups, so `bench_time` couldn't be built per version. **No node counts
are available, and everything below is measured on the binaries alone.**

## Part A: speed (time to depth)

### Depth 6 (main table)

Total time = sum over the 40 positions in one pass; the median of the 5 passes is shown.
"Geomean" is the geometric mean of the per-position speedups (each position's median time).
"Moves ≠ prev" counts positions where the best move differs from the previous version.

| Version | Time to depth 6 | vs previous | vs previous (geomean) | vs v1 | vs v1 (geomean) | Moves ≠ prev |
|---|---:|---:|---:|---:|---:|---:|
| v1_baseline | 78.99 s | – | – | 1.00× | 1.00× | – |
| v2_tablebase | 62.47 s | 1.26× | 1.28× | 1.26× | 1.28× | 1/40 |
| v3_engine | 10.92 s | **5.72×** | 5.13× | 7.24× | 6.55× | 0/40 |
| v4_LMR | 6.29 s | 1.73× | 1.66× | 12.55× | 10.85× | 5/40 |
| v4.1_LMR | 3.18 s | 1.98× | 1.80× | 24.85× | 19.50× | **13/40** |
| v4.2_null | 1.86 s | 1.70× | 1.68× | 42.37× | 32.78× | 6/40 |
| v4.3-counter | 1.68 s | 1.11× | 1.06× | 47.02× | 34.86× | 5/40 |
| v5-engine | 1.34 s | 1.26× | 1.25× | 59.13× | 43.44× | 8/40 |
| v6-ordering | 1.15 s | 1.17× | 1.17× | 68.91× | 50.63× | 7/40 |
| v6.1-stack | 940 ms | 1.22× | 1.23× | 84.01× | 62.38× | 0/40 |
| **v7** | **846 ms** | 1.11× | 1.05× | **93.38×** | **65.62×** | **20/40** |
| v7-nnue | 845 ms | 1.00× | 1.00× | 93.48× | 65.55× | 0/40 |
| v7.2-nnue | 1.22 s | **0.69×** | 0.75× | 64.51× | 49.26× | **25/40** |

### Speedup vs previous version at each depth (total time)

| Version | d4 | d5 | d6 | Moves ≠ prev d4 / d5 / d6 |
|---|---:|---:|---:|---|
| v2_tablebase | 1.23× | 1.23× | 1.26× | 0 / 2 / 1 |
| v3_engine | 5.74× | 5.53× | 5.72× | 0 / 0 / 0 |
| v4_LMR | 1.19× | 1.73× | 1.73× | 4 / 6 / 5 |
| v4.1_LMR | 1.32× | 1.74× | 1.98× | 7 / 8 / 13 |
| v4.2_null | 1.48× | 1.35× | 1.70× | 0 / 5 / 6 |
| v4.3-counter | 1.01× | 1.08× | 1.11× | 6 / 4 / 5 |
| v5-engine | 0.99× | 1.16× | 1.26× | 8 / 6 / 8 |
| v6-ordering | 1.14× | 1.16× | 1.17× | 4 / 1 / 7 |
| v6.1-stack | 1.16× | 1.21× | 1.22× | 0 / 0 / 0 |
| v7 | 1.07× | 1.14× | 1.11× | 22 / 18 / 20 |
| v7-nnue | 1.00× | 1.00× | 1.00× | 0 / 0 / 0 |
| v7.2-nnue | 0.85× | 0.99× | 0.69× | 23 / 20 / 25 |

Median time for all 40 positions, v1 → v7: 3.84 s → 167 ms at depth 4 (23×), 18.05 s →
327 ms at depth 5 (55×), and 78.99 s → 846 ms at depth 6 (93×).

### Depth 6 speedup vs previous version, by phase (geomean)

| Version | Opening | Middlegame | Endgame |
|---|---:|---:|---:|
| v2_tablebase | 1.35× | 1.23× | 1.25× |
| v3_engine | 5.12× | 5.67× | 4.39× |
| v4_LMR | 1.34× | 1.91× | 1.77× |
| v4.1_LMR | 2.36× | 1.78× | 1.24× |
| v4.2_null | 1.68× | 1.78× | 1.54× |
| v4.3-counter | 1.08× | 1.13× | 0.94× |
| v5-engine | 1.12× | 1.28× | 1.38× |
| v6-ordering | 1.12× | 1.17× | 1.23× |
| v6.1-stack | 1.20× | 1.22× | 1.30× |
| v7 | 1.26× | 1.16× | **0.69×** |
| v7.2-nnue | 0.80× | **0.61×** | 0.96× |

### Measurement quality

- Pass-to-pass spread of the totals is 0–4% for every version and depth.
- The depth-6 totals show no drift across passes (v1: 78.3 / 79.0 / 79.1 / 79.0 / 79.2 s), so
  heat didn't affect the results despite the fanless machine.
- Best moves are identical across all 5 passes for every (version, depth, position).
  The searches are deterministic.

### Unusual best-move changes

Typical versions change the best move on 10–14% of positions compared with the previous version. Flagged
(at least 2× that median and at least 25%):

- **v7 (45–55%) and v7.2-nnue (50–62%), at every depth.** Explained by the evaluator change
  (next section).
- **v4.1_LMR at depth 6 (32%).** More aggressive LMR changes deeper results, as expected.
  Part B would be needed to tell whether those changes cost strength.

### Separating faster nodes from fewer nodes

Time to depth mixes two effects:
- **faster node processing:** move generation, make/unmake, allocation, hashing, evaluation cost;
- **fewer nodes searched:** pruning, LMR, null move, move ordering, TT.

Without node counts they can't be measured separately. These signals help:

- **Pure per-node speedup: v6.1-stack, 1.16–1.22×.** Its best moves are identical to v6
  everywhere, the evaluator is identical, and the README describes only stack-allocation changes.
- **A speedup that grows with depth means fewer nodes**, because pruning savings compound
  with depth. That points to v4_LMR (1.19 → 1.73×), v4.1_LMR (1.32 → 1.98×), v4.2_null
  (1.48 → 1.70×) and v5-engine (0.99 → 1.26×).
- **A flat speedup across depths is ambiguous.** v2 (1.23–1.26×), v3 (5.5–5.7×) and v6
  (1.14–1.17×) fit per-node gains, but better ordering or TT use also gives a roughly
  constant factor, and plain alpha-beta returns the same score whatever the move order.
  So v3's identical moves can't separate the two. v3's 5.7× is most likely mostly per-node
  (core move generation and board representation), but that's an inference.
- **v7.2 is slower because of the tree, not the network.** Its 770-input network costs
  essentially the same per evaluation as v7's 768-input one: just one extra weight row is
  added. Yet the slowdown jumps around by depth (0.85 / 0.99 / 0.69×) and by phase
  (middlegame 0.61×, endgame 0.96×). That fits a different, larger search tree caused by the
  side-to-move bias (below).

Building each version's `bench_time` would settle this, but that needs per-version source,
which doesn't exist (see Step 0).

## Which evaluator each version uses

Every bridge prints `static_eval` = `Minimax::evaluate(board)`, the function the search calls
in every source snapshot we have. `eval_check.py` compares it, on the 40 positions, against
the two evaluators in the current source (`b1c6b89`). It also checks side-to-move symmetry:
on quiet positions where flipping the side to move is legal, a symmetric evaluator gives
eval(white to move) + eval(black to move) = 0.

| Version | = current handcrafted | = current NNUE (770→128) | eval(W to move) + eval(B to move) | Evaluator |
|---|---:|---:|---|---|
| v1_baseline … v6.1-stack (10 versions) | 40/40 | 0/40 | 0 | Handcrafted (unchanged from v1 to v6.1) |
| v7 = v7-nnue | 0/40 | 0/40 | 0 | **NNUE, 768 inputs** (2×6×64 → 128 → 1) |
| v7.2-nnue | 0/40 | **40/40** | **+171** (+170…+171) | **NNUE, 770 inputs** (768 + 2 side-to-move) |
| current `main` / Heroku (`b1c6b89`) | 40/40 | 0/40 | 0 | Handcrafted (same as v1–v6.1) |

**How v7 was verified.** The v7 binary contains two identical tables of exactly 768 × 128
floats, plus the hidden biases and output weights, in the same layout the current header
produces in v7.2. That's 2,048 bytes less constant data than v7.2, which is exactly
2 rows × 128 × 4 bytes × 2 copies. `tools/v7_nnue_check.py` runs those weights with the Jun 29
`evaluate_nnue()` formula and fits only the output bias (a compiled-in constant). It reproduces
v7's `static_eval` exactly on **39/40** positions; the last differs by 1 cp from rounding.

**The READMEs are wrong about this.** The READMEs for v7, v7-nnue and v7.2-nnue say
"`Minimax::evaluate()` uses the pre-NNUE handcrafted evaluator". They were rewritten on Jun 29
(v7.2's on Jul 4), after the binaries were frozen on Jun 27. They describe the Jun 29 code,
which had switched back to the handcrafted evaluator. v7.2's README also describes a
HalfKAv2 network (45056 → 256 per side). That network can't be in the 1 MB binary; the
binary has the small 770 → 128 network.

### v7 vs v7.2

- **What changed:** the network gained 2 side-to-move inputs (768 → 770). The binary also has
  one new function (`ChessBoard::set_turn`) and about 1 KB more code.
- **Side-to-move bias:** the new network adds about **+85 cp for the side to move**, on top of
  the position assessment. Negamax search treats the side to move symmetrically, so this flat
  bonus shifts leaf scores by ±85 depending on the parity of the line, including quiescence
  stand-pat. It also biases null-move pruning, where passing hands the bonus to the opponent,
  and comparisons against draw scores (0) and aspiration windows.
- **Effect:** different best moves on 50–62% of positions and a 31% slower search to
  depth 6, mostly in middlegames.
- **Is it weaker?** Unknown without Part B. A large constant tempo term isn't necessarily
  bad, but it's an outlier: typical engine tempo bonuses are 10–30 cp.

### Current Heroku build

- `Procfile` runs `run_lichess` (lichess-bot). `bin/post_compile` builds
  `chess_engine_bridge` from `src/` with CMake, and `homemade.py` runs
  `build-release/chess_engine_bridge` unless `CPP_CHESS_ENGINE_BIN` is set. The Heroku config
  vars weren't checked; see the Heroku note at the end.
- The deployed `heroku/main` (`79422ea`) has the same `src/`, `include/` and `CMakeLists.txt`
  as `main` (`b1c6b89`).
- **Evaluator:** handcrafted, identical to v1–v6.1. The search doesn't call `evaluate_nnue()`.
- **Newer than v7.2:** adaptive time management (soft/hard limits, more or less time depending
  on how stable the best move is), pondering, draw detection inside the search (50-move rule,
  insufficient material), and NNUE code changes (clipped ReLU, HalfKA support) that don't
  affect play.
- In short: **v6.1's evaluation + the v7-era search + new time management**. It isn't any of
  the frozen versions, and it wasn't speed-tested here.

## Rankings

**Biggest speed gains (time to depth 6, vs previous version):**

1. **v3_engine: 5.72×**, identical moves. Probably core engine speed (per-node).
2. **v4.1_LMR: 1.98×**, fewer nodes (more aggressive LMR). It also changes the most moves of
   any pre-v7 version.
3. **v4_LMR: 1.73×**, fewer nodes (first LMR).
4. **v4.2_null: 1.70×**, fewer nodes (null-move pruning).
5. v2_tablebase 1.26× and v5-engine 1.26×, v6.1-stack 1.22× (pure per-node), v6-ordering
   1.17×, v7 1.11×, v4.3-counter 1.11×.
6. **v7.2-nnue: 0.69×**, the only regression.

**Biggest Elo gains:** not measured. Part B wasn't run.

### Surprises

- **v7 is both a new evaluator and the fastest version.** It switched to NNUE and still
  beats v6.1 by 1.11× overall. But it's 0.69× in endgames, a figure that rests on only 10 positions.
- **v7.2 is slower and has a large side-to-move bias.** Whether it's stronger or weaker than
  v7 is exactly the question Part B would answer. Heroku has since gone back to the
  handcrafted evaluator.
- **The "nnue" names are misleading:** v7 already uses NNUE, and v7-nnue is the same file as v7.
- **The `time_ms=0` behaviour in v1–v5:** any earlier test that ran those versions with `0`
  measured instant moves, not searches.
- **v4.3-counter adds almost no speed** (1.01–1.11×, endgames 0.94×). **v5-engine is slightly
  slower than v4.3 at depth 4** (0.99×) but faster deeper.
- **v2_tablebase is 1.26× faster than v1 with tablebases off.** Since its moves are nearly
  identical, this is a per-node change that came along with the tablebase work.

## Part B: strength — not run

Not run, by decision. A run was started and stopped after about 1.3 pairs. That partial data
isn't part of these deliverables, and `matches.csv` isn't included.

The runner, `match.py`, was smoke-tested (8 games, all terminations correct, moves 101–106 ms
against the 100 ms budget):

- **Engines and history:** two `--serve` engines per game. Each `go` is
  `go\t64\t100\t<FEN>\t<all FENs since the opening, including the current one>`, the same
  history convention the lichess integration uses.
- **Adjudication:** python-chess `outcome(claim_draw=True)` handles checkmate, stalemate,
  threefold repetition, the 50-move rule and insufficient material. An illegal move, crash or
  timeout (budget + 2 s) loses the game for the engine that caused it and is logged.
- **Openings:** each opening is played twice with colours swapped. A pair within ±30 Elo after
  100 games plays 100 more on the extension openings O051–O100, so no game repeats an earlier one.
- **Elo:** `-400·log10(1/score − 1)`, with a 95% CI from the per-game score standard deviation
  (Wilson interval when every game has the same result). Results whose CI includes 0 are
  marked "not significant".
- **Pairs:** v7-nnue is left out because it's identical to v7, so v7.2 is compared with v7 directly.
- **Concurrency:** 2 games at a time (one game per two performance cores). macOS has no
  CPU-affinity API, so cores can't be pinned. With 2 games, at most 2 engines search at once on
  4 performance cores.
- **Resuming:** `matches.csv` is appended to, so an interrupted run resumes where it stopped.
- **Estimated time:** about 5.5 s of wall time per game. That's roughly 2–2.5 h for the 11
  vs-previous pairs, including likely extensions, and about 3 h more for the vs-v1 pairs.

## Commands to rerun everything

From the repo root, with the laptop on AC power and heavy apps closed:

```bash
cd benchmarks/versions && ./run_all.sh
```

That runs:
1. `select_positions.py`: regenerates `positions.txt` and `openings.txt` (deterministic, same result).
2. `speed.py`: Part A, writes `speed.csv` and `speed.log` (about 22 min).
3. `eval_check.py`: builds commit `b1c6b89` into `.build/` and writes `eval_check.csv` (about 2 min).
4. `tools/v7_nnue_check.py`: reproduces v7's evals from its embedded network.
5. `analyze.py`: writes `summary.md`, the generated tables used in this report.

Individual steps:

```bash
cd benchmarks/versions && .venv/bin/python speed.py --depths 4,5,6 --passes 5
```

```bash
cd benchmarks/versions && .venv/bin/python analyze.py
```

Part B, if you want it later (resumable):

```bash
cd benchmarks/versions && ./run_all.sh --matches
```

```bash
cd benchmarks/versions && .venv/bin/python match.py --pairs vs-v1
```

## Files

| File | Contents |
|---|---|
| `speed.csv` | Raw Part A results: pass, warm-up flag, version, position, phase, depth, time_ms, best move, search_eval, static_eval, completed_depth (7,800 measured rows + 1,560 warm-up rows) |
| `summary.md` | Generated tables (`analyze.py`) |
| `speed_v61_v7.csv`, `summary_v61_v7.md` | Raw results and tables for new v7 vs v6.1-stack |
| `eval_check.csv` | Static evals of every version + current `main`, and the side-to-move symmetry check |
| `positions.txt`, `openings.txt` | Fixed speed positions and match openings |
| `engine.py` | `--serve` protocol driver (parser handles all `info` formats) |
| `speed.py`, `match.py`, `analyze.py`, `select_positions.py`, `eval_check.py` | Scripts |
| `tools/evalboth.cpp`, `tools/v7_nnue_check.py` | Evaluator identification helpers |
| `run_all.sh` | One-command rerun |

## Update: new v7 vs v6.1-stack

After this report, the unused NNUE code was removed from `src/` (archived in
`archive/engine-nnue/`) and the result was frozen as the new `bots/v7`: current `main` with the
handcrafted evaluator. The natural comparison is with the last handcrafted frozen version,
v6.1-stack. Same protocol as Part A: 40 positions, 1 warm-up + 5 measured passes, interleaved.
The new v7 is called `v7-new` in the benchmark files.

| Depth | v6.1-stack | New v7 | Speedup (total) | Speedup (geomean/pos) | Best move differs |
|---|---:|---:|---:|---:|---:|
| 4 | 177 ms | 118 ms | 1.50× | 1.68× | 0/40 |
| 5 | 371 ms | 331 ms | 1.12× | 1.25× | 0/40 |
| 6 | 936 ms | 916 ms | **1.02×** | 1.09× | 0/40 |

Depth 6 by phase (geomean): opening 1.01×, middlegame 1.04×, endgame 1.30×.
Pass-to-pass spread is at most 1%. v6.1's times match its row in the main table (940 ms at depth 6).

- **About equal speed at depth 6, and identical results.** Both use the same evaluator, and
  every best move matches at every depth. Within these 40 positions, the newer search
  features don't change fixed-depth results.
- **The gap shrinks with depth.** v7 is ahead by 59 ms in total at depth 4, 40 ms at depth 5
  and 20 ms at depth 6. That fits v7 saving a roughly fixed cost per search while doing
  slightly more work per node (possibly the new in-search draw checks). Without v6.1's
  source this can't be confirmed.
- **v7's additions are about games, not time to depth.** Adaptive time management,
  pondering and in-search draw detection only show their value in real games. Whether v7
  plays better than v6.1 needs Part B (a 100-game v7 vs v6.1-stack match takes about 10–15 min).

Rerun:

```bash
cd benchmarks/versions && .venv/bin/python speed.py --versions v6.1-stack,v7-new --out speed_v61_v7.csv
```

```bash
cd benchmarks/versions && .venv/bin/python analyze.py --speed speed_v61_v7.csv --matches /dev/null --out summary_v61_v7.md
```

## Suggested next steps

- **Run Part B for v7.2 vs v7 and v7 vs v6.1 first** (about 25 min for both). These answer the
  most important open question: did NNUE, and then the side-to-move network, gain or lose
  strength?
- **Play the new v7 against v6.1-stack** (and the archived v7 / v7.2). Speed is now
  measured (above), but strength isn't. Once deployed, v7 is the version that plays on lichess.
- **Look at the +85 cp side-to-move bias** in the 770-input network before reusing it.
- ~~Fix the v7 / v7-nnue / v7.2 READMEs~~: done, in `archive/bots/`.
- **Check the Heroku config:** confirm `CPP_CHESS_ENGINE_BIN` isn't set, using `heroku config`.
