# Chess Bot

Small C++ chess bot project with:

- a local engine layer
- a simple macOS app UI with mouse input
- a PGN-to-benchmark dataset tool
- a macOS benchmark dashboard for bot-folder vs bot-folder matches
- a vendored upstream `lichess-bot` bridge

## Structure

```text
include/chess/               public headers
include/bench/               benchmark-only headers
src/engine/                  chess engine core
src/ui/                      macOS app UI
src/app/                     executable entry points
src/bench/                   benchmark tools + benchmark UI
perft/                       standalone perft CLI + helper script
data/                        local PGN + generated benchmark datasets
integrations/lichess-bot/    vendored upstream Python bridge
tests/                       engine + integration tests
config/                      example config files
```

## Build

Use the root build helper so outputs stay consistent with the `Release` CMake build:

```bash
./build
```

Build one target only:

```bash
./build --target chess_engine_bridge
./build --target chess_app
./build --target chess_bench_app
./build --target prepare_bench_dataset
```

## Build The Benchmark Dataset Tool

```bash
./build --target prepare_bench_dataset
```

Example usage:

```bash
export CHESS_STOCKFISH_PATH=/absolute/path/to/stockfish
./build-release/prepare_bench_dataset \
  --input data/lichess_db_standard_rated_2014-01.pgn \
  --output data/bench_positions.json \
  --target-count 1000 \
  --eval-depth 10 \
  --min-cp -30 \
  --max-cp 30
```

Each saved position includes:

- `fen_history`
- `opening_name`
- `move_number`
- `game_url`
- `selected_move_san` / `selected_move_uci`
- `stockfish_eval_cp`

## Build The Benchmark UI

```bash
./build --target chess_bench_app
```

The benchmark app reads two bot folders, launches each one as a child process, and runs the saved dataset positions as Bot A vs Bot B.

## Run Perft

```bash
./perft/run.sh --depth 4
```

Optional flags:

- `--fen "<fen>"` to run perft on a custom position
- `--divide` to print the root move breakdown

## Run Benchmarks

Use the root `bench` helper so you always hit the current `Release` binaries:

```bash
./bench 4 4 10
./bench minimax
./bench --build 4 4 10
```

`./bench` runs the existing `build-release` binaries directly.
Use `./bench --build ...` only when you want to rebuild first.

To compare an old source tree against a new one fairly, use two separate source folders and let
`hyperfine` alternate the commands:

```bash
./bench_compare --old /path/to/chess-old --new /path/to/chess-new time 4 4 10
./bench_compare --old /path/to/chess-old --new /path/to/chess-new minimax
```

`./bench_compare` configures each tree into its own `build-compare-release/` directory and builds
the matching Release benchmark target before timing it. Add `--skip-build` if both trees are
already built and you only want to re-run the benchmark.

## Bot Folder Contract

Each benchable bot should live in its own folder and include a `bot.json` manifest.

Example:

```text
bots/
  my_bot/
    bot.json
    run.sh
```

Example `bot.json`:

```json
{
  "name": "My Bot",
  "protocol": "chess-bench-v1",
  "entry": "./run.sh",
  "args": [],
  "cwd": "."
}
```

The launched process must:

- read commands from stdin
- support `ping`, `newgame`, `go`, and `quit`
- return `bestmove\t<uci>` for each `go` command

The repo already includes a working sample at `bots/v1_baseline/`.

## Build The Upstream Bridge Adapter

```bash
./build --target chess_engine_bridge
```

## Run

macOS app:

```bash
./build-release/chess_app.app/Contents/MacOS/chess_app
```

Benchmark app:

```bash
./build-release/chess_bench_app.app/Contents/MacOS/chess_bench_app
```

Suggested first run:

1. Build `chess_engine_bridge`
2. Open `./build-release/chess_bench_app.app/Contents/MacOS/chess_bench_app`
3. Set `Dataset` to `data/bench_positions_generated.json`
4. Set `Bot A folder` to `bots/v1_baseline`
5. Set `Bot B folder` to another bot folder that follows the same manifest contract
6. Click `Start Bench`

The macOS app lets you:

- choose `Human` or `Computer` for White
- choose `Human` or `Computer` for Black
- play by clicking a piece and then clicking a destination square

## Custom Piece PNGs

The macOS app will look for PNG files in `assets/pieces/` first, then fall back to the built-in rendered pieces.

Supported filenames:

- `wp.png`
- `wn.png`
- `wb.png`
- `wr.png`
- `wq.png`
- `wk.png`
- `bp.png`
- `bn.png`
- `bb.png`
- `br.png`
- `bq.png`
- `bk.png`

The app will also use `assets/boards/board.png` for the board background if that file exists.

## CMake

If `cmake` is installed on your machine, the repo also includes a `CMakeLists.txt` with the engine, bridge, dataset tool, and both macOS app targets.

## Vendored `lichess-bot`

This repo now also contains a vendored copy of:

- https://github.com/lichess-bot-devs/lichess-bot

Path:

- `integrations/lichess-bot/`

Added locally on top of upstream:

- `CppBridge` in `integrations/lichess-bot/homemade.py`
- `config.yml.project.example`
- `README.local.md`

Recommended usage:

1. Build `chess_engine_bridge`
2. `cd integrations/lichess-bot`
3. `python3 -m venv .venv && source .venv/bin/activate`
4. `pip install -r requirements.txt`
5. `cp config.yml.default config.yml`
6. Copy the project-specific `engine`, `challenge`, and `matchmaking` settings from `config.yml.project.example`
7. Put your token in `config.yml`
8. `python3 lichess-bot.py`

Shortcut from the project root:

```bash
./run_lichess
```
