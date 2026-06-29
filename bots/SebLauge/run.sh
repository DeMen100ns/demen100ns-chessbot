#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRIDGE="$BOT_DIR/bench_bridge.py"
PUBLISH_DIR="$BOT_DIR/published"
DOTNET_FALLBACK="/private/tmp/dotnet8_full/dotnet"

if [[ ! -x "$BRIDGE" ]]; then
  echo "Missing bench bridge: $BRIDGE" >&2
  exit 1
fi

if [[ ! -f "$PUBLISH_DIR/Chess-Coding-Adventure.dll" ]]; then
  echo "Missing published SebLague engine snapshot in $PUBLISH_DIR" >&2
  echo "Run ./build_snapshot.sh inside bots/SebLauge to create it." >&2
  exit 1
fi

if [[ -z "${SEBLAUGE_DOTNET:-}" && -x "$DOTNET_FALLBACK" ]]; then
  export SEBLAUGE_DOTNET="$DOTNET_FALLBACK"
fi

exec "$BRIDGE"
