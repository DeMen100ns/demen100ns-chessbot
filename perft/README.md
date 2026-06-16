# Perft Tool

This folder contains a standalone CLI for running legal-move perft checks against the current engine build.

## Quick Start

From the project root:

```bash
./perft/run.sh --depth 4
```

That script rebuilds `perft_tool` in `Release` mode, then runs it.

## Examples

Start position:

```bash
./perft/run.sh --depth 5
```

Custom FEN:

```bash
./perft/run.sh --depth 4 --fen "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
```

Root move breakdown:

```bash
./perft/run.sh --depth 3 --divide
```

## Direct Build

If you only want to build the tool:

```bash
./build --target perft_tool
```

Then run it directly:

```bash
./build-release/perft_tool --depth 4
```
