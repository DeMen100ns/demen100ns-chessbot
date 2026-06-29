#!/bin/zsh
set -euo pipefail

BOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$BOT_DIR/upstream/Chess-Coding-Adventure"
PUBLISH_DIR="$BOT_DIR/published"
DOTNET_BIN="${DOTNET_BIN:-/private/tmp/dotnet8/dotnet}"
DOTNET_CLI_HOME="${DOTNET_CLI_HOME:-/private/tmp/dotnet-home}"

if [[ ! -x "$DOTNET_BIN" ]]; then
  echo "Missing dotnet binary: $DOTNET_BIN" >&2
  echo "Install a local SDK first or set DOTNET_BIN=/path/to/dotnet." >&2
  exit 1
fi

mkdir -p "$DOTNET_CLI_HOME"

"${DOTNET_BIN}" --info >/dev/null

export DOTNET_CLI_HOME
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_NOLOGO=1

"$DOTNET_BIN" publish "$PROJECT_DIR/Chess-Coding-Adventure.csproj" \
  -c Release \
  -o "$PUBLISH_DIR" \
  --nologo

chmod +x "$BOT_DIR/run.sh" "$BOT_DIR/bench_bridge.py"

echo "Snapshot updated in $PUBLISH_DIR"
