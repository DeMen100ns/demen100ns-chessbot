# Vast.ai GPU Training

Recommended Docker image:

```text
vastai/pytorch:cuda-13.2.1-auto
```

The plain `vastai/pytorch` page currently advertises a recent `cuda-13.2.1-auto` tag, while the overview text still mentions an old CUDA/PyTorch stack. Use the explicit tag above.

Inside the instance, clone or upload this repo, then run from the repo root:

```bash
bash nnue/train_vast_gpu.sh
```

Useful overrides:

```bash
OUT_DIR=nnue/runs/basic_1m_h128_vast \
EPOCHS=80 \
BATCH_SIZE=16384 \
LR=0.001 \
WEIGHT_DECAY=0.0001 \
bash nnue/train_vast_gpu.sh
```

Continue from a local checkpoint by calling the trainer directly:

```bash
python3 nnue/train_basic_nnue.py \
  --train nnue/splits/train.json \
  --val nnue/splits/val.json \
  --out-dir nnue/runs/basic_1m_h128_vast_continue \
  --resume nnue/runs/basic_1m_h128_vast/basic_nnue.pt \
  --hidden-size 128 \
  --epochs 40 \
  --batch-size 16384 \
  --lr 0.0003 \
  --weight-decay 0.0001 \
  --device cuda \
  --progress tqdm
```

The first run builds feature caches under `nnue/cache/`. Later runs should show `cache_hit=...`.
