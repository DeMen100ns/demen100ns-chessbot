#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$BOT_DIR/../.." && pwd)"
ENGINE_BIN="$BOT_DIR/v4.2_null"
TB_DIR="$ROOT_DIR/data/tablebases/3-4-5"

if [[ ! -x "$ENGINE_BIN" ]]; then
  echo "Missing frozen bot binary: $ENGINE_BIN" >&2
  echo "Run ./build_snapshot.sh inside bots/v4.2_null to create it." >&2
  exit 1
fi

if [[ ! -d "$TB_DIR" ]]; then
  echo "Missing local Syzygy tablebase directory: $TB_DIR" >&2
  exit 1
fi

export CHESS_BRIDGE_ENABLE_TB=1
export CHESS_ONLINE_TB_URL="$TB_DIR"
export CHESS_ONLINE_TB_TIMEOUT_MS="${CHESS_ONLINE_TB_TIMEOUT_MS:-100}"
export CHESS_TB_MAX_PIECES="${CHESS_TB_MAX_PIECES:-5}"

exec "$ENGINE_BIN" --serve
