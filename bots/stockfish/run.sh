#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec /usr/bin/env python3 "$BOT_DIR/stockfish_bench_adapter.py"
