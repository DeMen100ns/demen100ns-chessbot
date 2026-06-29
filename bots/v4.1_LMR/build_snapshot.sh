#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$BOT_DIR/../.." && pwd)"
SOURCE_BIN="$ROOT_DIR/build-release/chess_engine_bridge"
TARGET_BIN="$BOT_DIR/v4.1_LMR"

cd "$ROOT_DIR"

"$ROOT_DIR/build" --target chess_engine_bridge

if [[ ! -x "$SOURCE_BIN" ]]; then
  echo "Missing release engine binary: $SOURCE_BIN" >&2
  exit 1
fi

cp "$SOURCE_BIN" "$TARGET_BIN"
chmod +x "$TARGET_BIN"

echo "Snapshot updated from $SOURCE_BIN -> $TARGET_BIN"
