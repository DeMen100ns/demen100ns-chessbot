#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRIDGE="$BOT_DIR/bench_bridge.py"
ENGINE_BIN="$BOT_DIR/teki"

if [[ ! -x "$BRIDGE" ]]; then
  echo "Missing bench bridge: $BRIDGE" >&2
  exit 1
fi

if [[ ! -x "$ENGINE_BIN" ]]; then
  echo "Missing Teki binary: $ENGINE_BIN" >&2
  echo "Run ./build_snapshot.sh inside bots/Teki to create it." >&2
  exit 1
fi

exec "$BRIDGE"
