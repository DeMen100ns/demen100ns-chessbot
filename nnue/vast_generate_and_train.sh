#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

INPUT="${INPUT:-nnue/data_fen_1M}"
DATA_OUT="${DATA_OUT:-nnue/data_1M.json}"
SPLIT_DIR="${SPLIT_DIR:-nnue/splits}"
DEPTH="${DEPTH:-5}"
TYPE="${TYPE:-1}"
SCORE_CLIP="${SCORE_CLIP:-2000}"
WORKERS="${WORKERS:-32}"
PROGRESS_EVERY="${PROGRESS_EVERY:-1000}"
RUN_TESTS="${RUN_TESTS:-1}"
KEEP_CHUNKS="${KEEP_CHUNKS:-0}"
ACTIVATE_VENV="${ACTIVATE_VENV:-/venv/main/bin/activate}"

CHUNK_DIR="${CHUNK_DIR:-nnue/chunks/generate_d${DEPTH}_t${TYPE}}"
LOG_DIR="${LOG_DIR:-nnue/runs/generate_d${DEPTH}_t${TYPE}_logs}"
BUILD_JOBS="${BUILD_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"

if [[ -f "$ACTIVATE_VENV" ]]; then
  # shellcheck disable=SC1090
  source "$ACTIVATE_VENV"
fi

echo "started_at=$(date -Is)"
echo "input=$INPUT"
echo "data_out=$DATA_OUT"
echo "split_dir=$SPLIT_DIR"
echo "depth=$DEPTH type=$TYPE score_clip=$SCORE_CLIP workers=$WORKERS"

cmake --preset release
cmake --build --preset release --target nnue_evaluate_fens test_minimax -j "$BUILD_JOBS"

if [[ "$RUN_TESTS" != "0" ]]; then
  ./build-release/test_minimax
fi

rm -rf "$CHUNK_DIR" "$LOG_DIR"
mkdir -p "$CHUNK_DIR" "$LOG_DIR" "$(dirname "$DATA_OUT")" "$SPLIT_DIR"

python3 - "$INPUT" "$CHUNK_DIR" "$WORKERS" <<'PY'
from pathlib import Path
import math
import sys

input_path = Path(sys.argv[1])
chunk_dir = Path(sys.argv[2])
workers = max(1, int(sys.argv[3]))

with input_path.open("r", encoding="utf-8") as handle:
    fens = [line for line in handle if line.strip()]

chunk_size = max(1, math.ceil(len(fens) / workers))
for index in range(workers):
    chunk = fens[index * chunk_size:(index + 1) * chunk_size]
    if not chunk:
        break
    path = chunk_dir / f"fens_{index:03d}.fen"
    path.write_text("".join(chunk), encoding="utf-8")
    print(f"chunk={path} positions={len(chunk)}")

print(f"total_positions={len(fens)} chunks={len(list(chunk_dir.glob('*.fen')))}")
PY

pids=()
status=0
for chunk in "$CHUNK_DIR"/fens_*.fen; do
  [[ -s "$chunk" ]] || continue
  base="$(basename "$chunk" .fen)"
  out="$CHUNK_DIR/${base}.json"
  echo "launch_generator chunk=$chunk output=$out"
  ./build-release/nnue_evaluate_fens \
    --input "$chunk" \
    --output "$out" \
    --depth "$DEPTH" \
    --type "$TYPE" \
    --score-clip "$SCORE_CLIP" \
    --progress-every "$PROGRESS_EVERY" \
    >"$LOG_DIR/${base}.out" \
    2>"$LOG_DIR/${base}.err" &
  pids+=("$!")
done

for pid in "${pids[@]}"; do
  if ! wait "$pid"; then
    status=1
  fi
done

if [[ "$status" != "0" ]]; then
  echo "one or more generator workers failed; see $LOG_DIR" >&2
  exit "$status"
fi

python3 - "$CHUNK_DIR" "$DATA_OUT" "$DEPTH" "$TYPE" "$SCORE_CLIP" "$INPUT" <<'PY'
from pathlib import Path
import json
import sys

chunk_dir = Path(sys.argv[1])
output_path = Path(sys.argv[2])
depth = int(sys.argv[3])
data_type = int(sys.argv[4])
score_clip = int(sys.argv[5])
input_path = sys.argv[6]
label = "depth_eval" if data_type == 1 else "depth_eval_minus_static_eval"

positions = []
for path in sorted(chunk_dir.glob("fens_*.json")):
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    positions.extend(payload["positions"])
    print(f"merged={path} positions={len(payload['positions'])}")

merged = {
    "source_file": input_path,
    "depth": depth,
    "type": data_type,
    "label": label,
    "score_clip": score_clip,
    "score_perspective": "white",
    "positions": positions,
}

tmp_path = output_path.with_suffix(output_path.suffix + ".tmp")
with tmp_path.open("w", encoding="utf-8") as handle:
    json.dump(merged, handle, separators=(",", ":"))
    handle.write("\n")
tmp_path.replace(output_path)
print(f"data_out={output_path} positions={len(positions)}")
PY

python3 nnue/split_data.py --input "$DATA_OUT" --output-dir "$SPLIT_DIR"

if [[ "$KEEP_CHUNKS" == "0" ]]; then
  rm -rf "$CHUNK_DIR"
fi

TRAIN="$SPLIT_DIR/train.json" VAL="$SPLIT_DIR/val.json" bash nnue/train_vast_gpu.sh

echo "finished_at=$(date -Is)"
