#!/bin/zsh
set -euo pipefail

PERFT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$PERFT_DIR/.." && pwd)"

"$ROOT_DIR/build" --target perft_tool
exec "$ROOT_DIR/build-release/perft_tool" "$@"
