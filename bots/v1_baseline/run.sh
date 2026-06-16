#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_BIN="$BOT_DIR/v1_baseline"

if [[ ! -x "$ENGINE_BIN" ]]; then
  echo "Missing frozen bot binary: $ENGINE_BIN" >&2
  exit 1
fi

exec "$ENGINE_BIN" --serve
