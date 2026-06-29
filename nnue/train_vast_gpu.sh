#!/usr/bin/env bash
set -euo pipefail

TRAIN="${TRAIN:-nnue/splits/train.json}"
VAL="${VAL:-nnue/splits/val.json}"
OUT_DIR="${OUT_DIR:-nnue/runs/basic_1m_h128_vast}"
HIDDEN_SIZE="${HIDDEN_SIZE:-128}"
EPOCHS="${EPOCHS:-80}"
BATCH_SIZE="${BATCH_SIZE:-16384}"
LR="${LR:-0.001}"
WEIGHT_DECAY="${WEIGHT_DECAY:-0.0001}"
TARGET_CLIP="${TARGET_CLIP:-2000.0}"
TARGET_SCALE="${TARGET_SCALE:-1000.0}"
CACHE_DIR="${CACHE_DIR:-nnue/cache}"
PROGRESS="${PROGRESS:-tqdm}"
DEVICE="${DEVICE:-cuda}"

if [[ ! -f "$TRAIN" ]]; then
  echo "Missing train split: $TRAIN" >&2
  exit 1
fi

if [[ ! -f "$VAL" ]]; then
  echo "Missing val split: $VAL" >&2
  exit 1
fi

python3 - <<'PY'
import torch
print("torch", torch.__version__)
print("cuda_available", torch.cuda.is_available())
if torch.cuda.is_available():
    print("cuda_device", torch.cuda.get_device_name(0))
PY

python3 nnue/train_basic_nnue.py \
  --train "$TRAIN" \
  --val "$VAL" \
  --out-dir "$OUT_DIR" \
  --hidden-size "$HIDDEN_SIZE" \
  --epochs "$EPOCHS" \
  --batch-size "$BATCH_SIZE" \
  --lr "$LR" \
  --weight-decay "$WEIGHT_DECAY" \
  --target-clip "$TARGET_CLIP" \
  --target-scale "$TARGET_SCALE" \
  --cache-dir "$CACHE_DIR" \
  --device "$DEVICE" \
  --progress "$PROGRESS"
