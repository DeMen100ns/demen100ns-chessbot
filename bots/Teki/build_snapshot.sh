#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$BOT_DIR/src"
TARGET_BIN="$BOT_DIR/teki"

clang++ -std=c++17 -O3 -DNDEBUG -DNAME='"Teki"' \
  "$SRC_DIR/main.cpp" \
  "$SRC_DIR/uci.cpp" \
  "$SRC_DIR/lookups.cpp" \
  "$SRC_DIR/position.cpp" \
  "$SRC_DIR/movegen.cpp" \
  "$SRC_DIR/move.cpp" \
  "$SRC_DIR/search.cpp" \
  "$SRC_DIR/evaluate.cpp" \
  "$SRC_DIR/options.cpp" \
  "$SRC_DIR/mcts.cpp" \
  "$SRC_DIR/syzygy/tbprobe.c" \
  -I"$SRC_DIR" \
  -I"$SRC_DIR/syzygy" \
  -o "$TARGET_BIN"

chmod +x "$TARGET_BIN"
chmod +x "$BOT_DIR/run.sh" "$BOT_DIR/bench_bridge.py"

echo "Snapshot updated: $TARGET_BIN"
