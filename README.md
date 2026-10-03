# Chess Bot

C++ chess engine playground with a macOS UI, benchmark tooling, frozen bot snapshots, and a local bridge for `lichess-bot`.

The search uses `Minimax::evaluate()`, a handcrafted tapered evaluator: material, piece-square tables, mobility, rook files, king safety, and pawn structure.

## Quick Start

```bash
./build
./build-release/test_minimax
./build-release/chess_engine_bridge \
  --fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" \
  --depth 3 \
  --time-limit-ms 250
```

Expected bridge output is a UCI move such as `g1f3`.

## Project Layout

```text
include/chess/               public engine headers
src/engine/                  board, move generation, search, evaluation
src/app/                     CLI and app entry points
src/ui/                      macOS game UI
src/bench/                   bot protocol, benchmark UI, dataset tooling
src/bench-time/              timing benchmarks
tests/                       assert-based engine tests
perft/                       perft CLI and helper script
bots/                        benchable bot snapshots and adapters
benchmarks/versions/         frozen-version speed benchmark and report
archive/                     retired snapshots and experiments (not built or deployed)
integrations/lichess-bot/    vendored lichess-bot integration
third_party/                 tablebase probing dependency
tools/                       tablebase probe helpers
data/                        small datasets plus local ignored raw data
```

## Build

The root helper configures and builds the Release preset into `build-release/`.

```bash
./build
```

Build one target:

```bash
./build --target chess_engine_bridge
./build --target chess_app
./build --target chess_bench_app
./build --target prepare_bench_dataset
./build --target test_minimax_regression
```

Direct CMake is also supported:

```bash
cmake --preset release
cmake --build --preset release
```

## Test

Useful smoke suite:

```bash
./build --target test_chessboard test_minimax test_minimax_regression test_minimax_tt
./build-release/test_chessboard
./build-release/test_minimax
./build-release/test_minimax_regression
./build-release/test_minimax_tt
```

`test_minimax_regression` checks that evaluation is deterministic, that a board reached by making a move evaluates the same as the board rebuilt from its FEN, and that search returns legal moves.

## Run The Engine Bridge

`chess_engine_bridge` is the CLI used by benchmark adapters and the Lichess bridge.

```bash
./build --target chess_engine_bridge
./build-release/chess_engine_bridge \
  --fen "<FEN>" \
  --depth 4 \
  --time-limit-ms 1000
```

It prints the selected move in UCI notation. With `--serve`, it speaks the local tab-separated benchmark protocol over stdin/stdout.

## Run The Apps

macOS game UI:

```bash
./build --target chess_app
./build-release/chess_app.app/Contents/MacOS/chess_app
```

Benchmark UI:

```bash
./build --target chess_bench_app
./build-release/chess_bench_app.app/Contents/MacOS/chess_bench_app
```

In the benchmark UI, set a dataset such as `data/bench_positions_generated.json`, then choose two folders under `bots/`.

## Benchmarks

Timing helper:

```bash
./bench 4 4 10
./bench minimax
./bench --build 4 4 10
```

Perft:

```bash
./perft/run.sh --depth 4
./perft/run.sh --depth 4 --divide
./perft/run.sh --fen "<FEN>" --depth 5
```

Compare two source trees:

```bash
./bench_compare --old /path/to/chess-old --new /path/to/chess-new time 4 4 10
./bench_compare --old /path/to/chess-old --new /path/to/chess-new minimax
```

Compare the frozen bot snapshots (time to fixed depth): see `benchmarks/versions/REPORT.md`.

## Bot Folder Contract

Each benchable bot folder contains a `bot.json` manifest and an executable entry, usually `run.sh`.

```json
{
  "name": "My Bot",
  "protocol": "chess-bench-v1",
  "entry": "./run.sh",
  "args": [],
  "cwd": "."
}
```

The process must read stdin commands and support:

- `ping`
- `newgame`
- `go`
- `quit`

For each `go`, it should return:

```text
bestmove	<uci>
```

Included folders cover local engine snapshots, Stockfish adapter support, and external bots used for comparison.

## Stockfish Adapter

`bots/stockfish` adapts a local UCI Stockfish binary to the benchmark protocol.

```bash
export CHESS_STOCKFISH_PATH=/absolute/path/to/stockfish
export STOCKFISH_ELO=1600
export STOCKFISH_BENCH_MOVETIME_MS=50
```

If `CHESS_STOCKFISH_PATH` is unset, the adapter checks common Homebrew paths and then `stockfish` from `PATH`.

## Lichess Bot

The vendored integration is in `integrations/lichess-bot/`.

Setup:

```bash
./build --target chess_engine_bridge
cd integrations/lichess-bot
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp config.yml.default config.yml
```

Then copy the project-specific `engine`, `challenge`, and `matchmaking` settings from `config.yml.project.example` into `config.yml`, add your token, and run:

```bash
python3 lichess-bot.py
```

From the project root, the shortcut is:

```bash
./run_lichess
```

Optional bridge overrides:

```bash
export CPP_CHESS_ENGINE_BIN=/absolute/path/to/build-release/chess_engine_bridge
export CPP_CHESS_ENGINE_DEPTH=3
```

## Local Files And Large Data

The repository intentionally ignores local configuration, build outputs, logs, virtual environments, and raw heavy data.

Notable ignored files/directories:

- `.env`, `.env.*`, `.vscode/`
- `build/`, `build-*/`, `cmake-build-*/`
- `integrations/lichess-bot/config.yml`
- `integrations/lichess-bot/.venv/`
- `lichess_bot_auto_logs/`
- `data/lichess_db_standard_rated_2014-01.pgn`

Keep raw PGN/tablebase/training files larger than 100 MB local unless they are deliberately moved to a proper large-file storage flow.

## Future Work

- NNUE evaluation. Earlier NNUE experiments (training scripts, datasets, weights, and the old
  v7 / v7.2 snapshots) are kept in `archive/`. `archive/README.md` lists what was measured and
  how to restore the engine code.
