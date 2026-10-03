#!/usr/bin/env bash
set -euo pipefail

TRAIN="${TRAIN:-nnue/splits/train.json}"
VAL="${VAL:-nnue/splits/val.json}"
OUT_DIR="${OUT_DIR:-nnue/runs/dual_halfka_h256_mlp32_vast}"
MODEL_TYPE="${MODEL_TYPE:-basic}"
HIDDEN_SIZE="${HIDDEN_SIZE:-256}"
DENSE_LAYERS="${DENSE_LAYERS:-2}"
EPOCHS="${EPOCHS:-80}"
BATCH_SIZE="${BATCH_SIZE:-16384}"
LR="${LR:-0.001}"
WEIGHT_DECAY="${WEIGHT_DECAY:-0.0001}"
TARGET_CLIP="${TARGET_CLIP:-2000.0}"
TARGET_SCALE="${TARGET_SCALE:-1000.0}"
TARGET_MODE="${TARGET_MODE:-eval}"
LOSS="${LOSS:-huber}"
HUBER_DELTA_CP="${HUBER_DELTA_CP:-1000.0}"
CACHE_DIR="${CACHE_DIR:-nnue/cache}"
PROGRESS="${PROGRESS:-tqdm}"
DEVICE="${DEVICE:-cuda}"
SELECTION_METRIC="${SELECTION_METRIC:-val_loss}"
STOP_MAE_CP="${STOP_MAE_CP:-0.0}"
EXPORT_WEIGHTS="${EXPORT_WEIGHTS:-best}"
AUGMENT_SYMMETRY="${AUGMENT_SYMMETRY:-none}"
RESUME="${RESUME:-}"

RESUME_ARGS=()
if [[ -n "$RESUME" ]]; then
  RESUME_ARGS=(--resume "$RESUME")
fi

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
  --model-type "$MODEL_TYPE" \
  --hidden-size "$HIDDEN_SIZE" \
  --dense-layers "$DENSE_LAYERS" \
  --epochs "$EPOCHS" \
  --batch-size "$BATCH_SIZE" \
  --lr "$LR" \
  --weight-decay "$WEIGHT_DECAY" \
  --target-clip "$TARGET_CLIP" \
  --target-scale "$TARGET_SCALE" \
  --target-mode "$TARGET_MODE" \
  --loss "$LOSS" \
  --huber-delta-cp "$HUBER_DELTA_CP" \
  --cache-dir "$CACHE_DIR" \
  --device "$DEVICE" \
  --progress "$PROGRESS" \
  --selection-metric "$SELECTION_METRIC" \
  --stop-mae-cp "$STOP_MAE_CP" \
  --export-weights "$EXPORT_WEIGHTS" \
  --augment-symmetry "$AUGMENT_SYMMETRY" \
  "${RESUME_ARGS[@]}"
