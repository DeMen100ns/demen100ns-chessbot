#!/usr/bin/env python3
import argparse
import json
import math
import random
from pathlib import Path
from typing import Any

import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset

try:
    from tqdm.auto import tqdm
except ImportError:
    tqdm = None


PIECE_FEATURE_COUNT = 2 * 6 * 64
SIDE_TO_MOVE_FEATURE_COUNT = 2
FEATURE_COUNT = PIECE_FEATURE_COUNT + SIDE_TO_MOVE_FEATURE_COUNT
PAD_FEATURE_INDEX = FEATURE_COUNT
DEFAULT_MAX_ACTIVE_FEATURES = 33
PIECE_TO_TYPE = {
    "p": 0,
    "n": 1,
    "b": 2,
    "r": 3,
    "q": 4,
    "k": 5,
}


def active_features_from_fen(fen: str) -> list[int]:
    fields = fen.split()
    board_part = fields[0]
    active_color = fields[1] if len(fields) > 1 else "w"
    features: list[int] = []
    rank = 7
    file = 0

    for ch in board_part:
        if ch == "/":
            rank -= 1
            file = 0
            continue
        if ch.isdigit():
            file += int(ch)
            continue

        piece_type = PIECE_TO_TYPE[ch.lower()]
        color = 0 if ch.isupper() else 1
        square = rank * 8 + file
        features.append(color * 6 * 64 + piece_type * 64 + square)
        file += 1

    if active_color == "w":
        features.append(PIECE_FEATURE_COUNT)
    elif active_color == "b":
        features.append(PIECE_FEATURE_COUNT + 1)
    else:
        raise ValueError(f"invalid FEN active color: {active_color}")

    return features


def padded_active_features(fen: str, max_active_features: int) -> list[int]:
    features = active_features_from_fen(fen)
    if len(features) > max_active_features:
        raise ValueError(
            f"position has {len(features)} active features, "
            f"but max_active_features={max_active_features}"
        )
    return features + [PAD_FEATURE_INDEX] * (max_active_features - len(features))


class NnueJsonDataset(Dataset):
    def __init__(self, path: Path, target_clip: float, max_active_features: int):
        with path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        self.positions = payload["positions"]
        self.target_clip = target_clip
        self.max_active_features = max_active_features

    def __len__(self) -> int:
        return len(self.positions)

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor]:
        item = self.positions[index]
        features = padded_active_features(item["fen"], self.max_active_features)
        target = float(item["eval_score"])
        target = max(-self.target_clip, min(self.target_clip, target))
        return (
            torch.tensor(features, dtype=torch.int16),
            torch.tensor([target], dtype=torch.float32),
        )


class NnueCachedDataset(Dataset):
    def __init__(self, active_features: torch.Tensor, targets: torch.Tensor):
        self.active_features = active_features
        self.targets = targets

    def __len__(self) -> int:
        return int(self.targets.shape[0])

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor]:
        return self.active_features[index], self.targets[index]


def cache_metadata(path: Path, target_clip: float, max_active_features: int) -> dict[str, Any]:
    stat = path.stat()
    return {
        "version": 1,
        "source": str(path),
        "source_size": stat.st_size,
        "source_mtime_ns": stat.st_mtime_ns,
        "target_clip": target_clip,
        "max_active_features": max_active_features,
        "piece_feature_count": PIECE_FEATURE_COUNT,
        "feature_count": FEATURE_COUNT,
        "pad_feature_index": PAD_FEATURE_INDEX,
    }


def cache_path_for(input_path: Path, cache_dir: Path, target_clip: float, max_active_features: int) -> Path:
    clip_name = str(target_clip).replace(".", "p")
    return cache_dir / f"{input_path.stem}_clip{clip_name}_max{max_active_features}.pt"


def build_feature_cache(input_path: Path,
                        cache_path: Path,
                        target_clip: float,
                        max_active_features: int,
                        progress: str) -> NnueCachedDataset:
    with input_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    positions = payload["positions"]
    total = len(positions)
    active_features = torch.full(
        (total, max_active_features),
        PAD_FEATURE_INDEX,
        dtype=torch.int16,
    )
    targets = torch.empty((total, 1), dtype=torch.float32)

    use_tqdm = progress in ("auto", "tqdm") and tqdm is not None
    iterator = enumerate(positions)
    if use_tqdm:
        iterator = tqdm(iterator, total=total, desc=f"cache {input_path.name}", unit="pos")

    for index, item in iterator:
        features = active_features_from_fen(item["fen"])
        if len(features) > max_active_features:
            raise ValueError(
                f"{input_path}:{index} has {len(features)} active features, "
                f"but max_active_features={max_active_features}"
            )
        if features:
            active_features[index, :len(features)] = torch.tensor(features, dtype=torch.int16)
        target = float(item["eval_score"])
        targets[index, 0] = max(-target_clip, min(target_clip, target))

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "metadata": cache_metadata(input_path, target_clip, max_active_features),
            "active_features": active_features,
            "targets": targets,
        },
        cache_path,
    )
    print(f"cached={cache_path} positions={total}", flush=True)
    return NnueCachedDataset(active_features, targets)


def load_cached_dataset(input_path: Path,
                        cache_dir: Path,
                        target_clip: float,
                        max_active_features: int,
                        rebuild_cache: bool,
                        progress: str) -> NnueCachedDataset:
    cache_path = cache_path_for(input_path, cache_dir, target_clip, max_active_features)
    expected_metadata = cache_metadata(input_path, target_clip, max_active_features)
    if not rebuild_cache and cache_path.exists():
        payload = torch.load(cache_path, map_location="cpu")
        if payload.get("metadata") == expected_metadata:
            print(f"cache_hit={cache_path}", flush=True)
            return NnueCachedDataset(payload["active_features"], payload["targets"])
        print(f"cache_stale={cache_path}; rebuilding", flush=True)

    return build_feature_cache(input_path, cache_path, target_clip, max_active_features, progress)


class BasicNnue(nn.Module):
    def __init__(self, hidden_size: int):
        super().__init__()
        self.feature_weights = nn.Embedding(
            FEATURE_COUNT + 1,
            hidden_size,
            padding_idx=PAD_FEATURE_INDEX,
        )
        self.hidden_bias = nn.Parameter(torch.empty(hidden_size))
        self.act = nn.ReLU()
        self.fc2 = nn.Linear(hidden_size, 1)
        self.reset_parameters()

    def reset_parameters(self) -> None:
        bound = 1.0 / math.sqrt(FEATURE_COUNT)
        nn.init.uniform_(self.feature_weights.weight, -bound, bound)
        with torch.no_grad():
            self.feature_weights.weight[PAD_FEATURE_INDEX].zero_()
        nn.init.uniform_(self.hidden_bias, -bound, bound)
        self.fc2.reset_parameters()

    def forward(self, active_features: torch.Tensor) -> torch.Tensor:
        hidden = self.feature_weights(active_features).sum(dim=1) + self.hidden_bias
        return self.fc2(self.act(hidden))


def choose_device(device_arg: str) -> torch.device:
    if device_arg != "auto":
        return torch.device(device_arg)
    if torch.backends.mps.is_available():
        return torch.device("mps")
    if torch.cuda.is_available():
        return torch.device("cuda")
    return torch.device("cpu")


def evaluate(model: nn.Module,
             loader: DataLoader,
             device: torch.device,
             loss_fn: nn.Module,
             target_scale: float) -> tuple[float, float]:
    model.eval()
    total_loss = 0.0
    total_abs_error = 0.0
    total_count = 0
    with torch.no_grad():
        for features, targets in loader:
            features = features.to(device=device, dtype=torch.long)
            targets = targets.to(device)
            preds = model(features)
            loss = loss_fn(preds / target_scale, targets / target_scale)
            count = targets.numel()
            total_loss += float(loss.item()) * count
            total_abs_error += float((preds - targets).abs().sum().item())
            total_count += count
    return total_loss / max(1, total_count), total_abs_error / max(1, total_count)


def export_weights_json(model: BasicNnue, path: Path, hidden_size: int, target_scale: float) -> None:
    state = {key: value.detach().cpu() for key, value in model.state_dict().items()}
    feature_weight = state["feature_weights.weight"][:FEATURE_COUNT]
    payload = {
        "format": "basic-nnue-v1",
        "input_features": FEATURE_COUNT,
        "hidden_size": hidden_size,
        "target_scale": target_scale,
        "activation": "relu",
        "score_perspective": "white",
        "fc1_weight": feature_weight.transpose(0, 1).tolist(),
        "fc1_bias": state["hidden_bias"].tolist(),
        "fc2_weight": state["fc2.weight"].tolist()[0],
        "fc2_bias": state["fc2.bias"].tolist()[0],
    }
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
        handle.write("\n")


def load_checkpoint_state(model: BasicNnue, checkpoint: dict[str, Any]) -> None:
    state = checkpoint["model_state"]
    if "feature_weights.weight" in state:
        feature_weights = state["feature_weights.weight"]
        if feature_weights.shape == model.feature_weights.weight.shape:
            model.load_state_dict(state)
            return
        with torch.no_grad():
            shared_features = min(feature_weights.shape[0] - 1, FEATURE_COUNT)
            model.feature_weights.weight[:shared_features].copy_(feature_weights[:shared_features])
            model.feature_weights.weight[PAD_FEATURE_INDEX].zero_()
            model.hidden_bias.copy_(state["hidden_bias"])
            model.fc2.weight.copy_(state["fc2.weight"])
            model.fc2.bias.copy_(state["fc2.bias"])
        return

    if "fc1.weight" not in state or "fc1.bias" not in state:
        raise ValueError("unsupported checkpoint model_state format")

    with torch.no_grad():
        old_piece_weights = state["fc1.weight"].transpose(0, 1)
        model.feature_weights.weight[:old_piece_weights.shape[0]].copy_(old_piece_weights)
        model.feature_weights.weight[PAD_FEATURE_INDEX].zero_()
        model.hidden_bias.copy_(state["fc1.bias"])
        model.fc2.weight.copy_(state["fc2.weight"])
        model.fc2.bias.copy_(state["fc2.bias"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Train a basic 2x6x64 NNUE-style evaluator.")
    parser.add_argument("--train", default="nnue/splits/train.json")
    parser.add_argument("--val", default="nnue/splits/val.json")
    parser.add_argument("--out-dir", default="nnue/runs/basic")
    parser.add_argument("--hidden-size", type=int, default=128)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--target-clip", type=float, default=2000.0)
    parser.add_argument("--target-scale", type=float, default=1000.0)
    parser.add_argument("--max-active-features", type=int, default=DEFAULT_MAX_ACTIVE_FEATURES)
    parser.add_argument("--cache-dir", default="nnue/cache")
    parser.add_argument("--rebuild-cache", action="store_true")
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--seed", type=int, default=20260627)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--progress", choices=("auto", "tqdm", "log", "none"), default="auto")
    parser.add_argument("--log-every-batches", type=int, default=10)
    parser.add_argument(
        "--resume",
        default="",
        help="Optional checkpoint path to continue training from. Epochs are additional epochs.",
    )
    args = parser.parse_args()

    random.seed(args.seed)
    torch.manual_seed(args.seed)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    train_path = Path(args.train)
    val_path = Path(args.val)
    if args.no_cache:
        train_dataset = NnueJsonDataset(train_path, args.target_clip, args.max_active_features)
        val_dataset = NnueJsonDataset(val_path, args.target_clip, args.max_active_features)
    else:
        cache_dir = Path(args.cache_dir)
        train_dataset = load_cached_dataset(
            train_path,
            cache_dir,
            args.target_clip,
            args.max_active_features,
            args.rebuild_cache,
            args.progress,
        )
        val_dataset = load_cached_dataset(
            val_path,
            cache_dir,
            args.target_clip,
            args.max_active_features,
            args.rebuild_cache,
            args.progress,
        )

    generator = torch.Generator()
    generator.manual_seed(args.seed)
    device = choose_device(args.device)
    pin_memory = device.type == "cuda"
    train_loader = DataLoader(
        train_dataset,
        batch_size=args.batch_size,
        shuffle=True,
        generator=generator,
        pin_memory=pin_memory,
    )
    val_loader = DataLoader(
        val_dataset,
        batch_size=args.batch_size,
        shuffle=False,
        pin_memory=pin_memory,
    )

    model = BasicNnue(args.hidden_size).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    loss_fn = nn.HuberLoss(delta=1.0)

    best_val_loss = math.inf
    metrics = []
    start_epoch = 1
    saved_best = False

    if args.resume:
        checkpoint_path = Path(args.resume)
        checkpoint = torch.load(checkpoint_path, map_location="cpu")
        checkpoint_hidden_size = int(checkpoint.get("hidden_size", args.hidden_size))
        checkpoint_feature_count = int(checkpoint.get("feature_count", FEATURE_COUNT))
        if checkpoint_hidden_size != args.hidden_size:
            raise ValueError(
                f"checkpoint hidden_size={checkpoint_hidden_size} "
                f"does not match --hidden-size={args.hidden_size}"
            )
        if checkpoint_feature_count not in (PIECE_FEATURE_COUNT, FEATURE_COUNT):
            raise ValueError(
                f"checkpoint feature_count={checkpoint_feature_count} "
                f"does not match expected {PIECE_FEATURE_COUNT} or {FEATURE_COUNT}"
            )
        if checkpoint_feature_count != FEATURE_COUNT:
            print(
                f"info: checkpoint feature_count={checkpoint_feature_count}; "
                f"initializing new side-to-move features",
                flush=True,
            )
        checkpoint_target_scale = float(checkpoint.get("target_scale", args.target_scale))
        checkpoint_target_clip = float(checkpoint.get("target_clip", args.target_clip))
        if checkpoint_target_scale != args.target_scale:
            print(
                f"warning: checkpoint target_scale={checkpoint_target_scale} "
                f"but --target-scale={args.target_scale}",
                flush=True,
            )
        if checkpoint_target_clip != args.target_clip:
            print(
                f"warning: checkpoint target_clip={checkpoint_target_clip} "
                f"but --target-clip={args.target_clip}",
                flush=True,
            )

        load_checkpoint_state(model, checkpoint)
        previous_metrics = checkpoint.get("metrics", [])
        if isinstance(previous_metrics, list):
            metrics = list(previous_metrics)
            if metrics:
                best_val_loss = min(float(row["val_loss"]) for row in metrics)
                start_epoch = int(metrics[-1].get("epoch", len(metrics))) + 1
        print(
            f"resumed={checkpoint_path} start_epoch={start_epoch} "
            f"previous_epochs={len(metrics)}",
            flush=True,
        )

    print(f"device={device}")
    print(f"train={len(train_dataset)} val={len(val_dataset)} hidden={args.hidden_size}")
    if args.progress == "tqdm" and tqdm is None:
        print("progress=tqdm requested but tqdm is not installed; falling back to batch logs.")

    final_epoch = start_epoch + args.epochs - 1
    for epoch in range(start_epoch, final_epoch + 1):
        model.train()
        total_loss = 0.0
        total_count = 0
        batch_total = len(train_loader)
        use_tqdm = args.progress in ("auto", "tqdm") and tqdm is not None
        train_iter = tqdm(
            train_loader,
            total=batch_total,
            desc=f"epoch {epoch}/{final_epoch}",
            unit="batch",
        ) if use_tqdm else train_loader

        for batch_index, (features, targets) in enumerate(train_iter, start=1):
            features = features.to(device=device, dtype=torch.long)
            targets = (targets / args.target_scale).to(device)

            optimizer.zero_grad(set_to_none=True)
            preds = model(features) / args.target_scale
            loss = loss_fn(preds, targets)
            loss.backward()
            optimizer.step()

            count = targets.numel()
            total_loss += float(loss.item()) * count
            total_count += count

            running_loss = total_loss / max(1, total_count)
            if use_tqdm:
                train_iter.set_postfix(loss=f"{running_loss:.6f}")
            elif args.progress != "none" and args.log_every_batches > 0:
                if batch_index % args.log_every_batches == 0 or batch_index == batch_total:
                    print(
                        f"epoch={epoch}/{final_epoch} "
                        f"batch={batch_index}/{batch_total} "
                        f"train_loss={running_loss:.6f}",
                        flush=True,
                    )

        train_loss = total_loss / max(1, total_count)
        val_loss, val_mae_cp = evaluate(model, val_loader, device, loss_fn, args.target_scale)

        row = {
            "epoch": epoch,
            "train_loss": train_loss,
            "val_loss": val_loss,
            "val_mae_cp": val_mae_cp,
        }
        metrics.append(row)
        print(
            f"epoch={epoch} train_loss={train_loss:.6f} "
            f"val_loss={val_loss:.6f} val_mae_cp={val_mae_cp:.2f}",
            flush=True,
        )

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(
                {
                    "model_state": model.state_dict(),
                    "hidden_size": args.hidden_size,
                    "target_scale": args.target_scale,
                    "target_clip": args.target_clip,
                    "piece_feature_count": PIECE_FEATURE_COUNT,
                    "feature_count": FEATURE_COUNT,
                    "max_active_features": args.max_active_features,
                    "model_format": "embedding-sum-v2",
                    "metrics": metrics,
                },
                out_dir / "basic_nnue.pt",
            )
            export_weights_json(model, out_dir / "basic_nnue_weights.json", args.hidden_size, args.target_scale)
            saved_best = True

    if not saved_best:
        torch.save(
            {
                "model_state": model.state_dict(),
                "hidden_size": args.hidden_size,
                "target_scale": args.target_scale,
                "target_clip": args.target_clip,
                "piece_feature_count": PIECE_FEATURE_COUNT,
                "feature_count": FEATURE_COUNT,
                "max_active_features": args.max_active_features,
                "model_format": "embedding-sum-v2",
                "metrics": metrics,
            },
            out_dir / "basic_nnue.pt",
        )
        export_weights_json(model, out_dir / "basic_nnue_weights.json", args.hidden_size, args.target_scale)

    with (out_dir / "metrics.json").open("w", encoding="utf-8") as handle:
        json.dump(metrics, handle, indent=2)
        handle.write("\n")

    print(f"saved={out_dir / 'basic_nnue.pt'}")
    print(f"exported={out_dir / 'basic_nnue_weights.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
