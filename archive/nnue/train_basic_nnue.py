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


SQUARE_COUNT = 64
HALFKA_PIECE_KIND_COUNT = 11
PERSPECTIVE_COUNT = 2
PERSPECTIVE_FEATURE_COUNT = SQUARE_COUNT * SQUARE_COUNT * HALFKA_PIECE_KIND_COUNT
PAD_FEATURE_INDEX = PERSPECTIVE_FEATURE_COUNT
DEFAULT_MAX_ACTIVE_FEATURES = 32
DEFAULT_HIDDEN_SIZE = 256
OUTPUT_HEAD_HIDDEN_SIZE = 32
PIECE_TO_TYPE = {
    "p": 0,
    "n": 1,
    "b": 2,
    "r": 3,
    "q": 4,
    "k": 5,
}


def perspective_square(square: int, perspective_color: int) -> int:
    return square if perspective_color == 0 else square ^ 56


def halfka_piece_kind(piece_type: int, color: int, perspective_color: int) -> int:
    if piece_type == PIECE_TO_TYPE["k"]:
        return HALFKA_PIECE_KIND_COUNT - 1
    return piece_type * 2 + (1 if color == perspective_color else 0)


def active_color_from_fen(fen: str) -> str:
    fields = fen.split()
    return fields[1] if len(fields) > 1 else "w"


def halfka_features_for_perspective(
    pieces: list[tuple[int, int, int]],
    king_square: int,
    perspective_color: int,
) -> list[int]:
    perspective_king_square = perspective_square(king_square, perspective_color)
    return [
        (
            (perspective_king_square * SQUARE_COUNT +
             perspective_square(square, perspective_color)) *
            HALFKA_PIECE_KIND_COUNT +
            halfka_piece_kind(piece_type, color, perspective_color)
        )
        for piece_type, color, square in pieces
    ]


def active_features_from_fen(fen: str) -> list[list[int]]:
    fields = fen.split()
    board_part = fields[0]
    active_color = fields[1] if len(fields) > 1 else "w"
    side_to_move = 0 if active_color == "w" else 1 if active_color == "b" else -1
    if side_to_move < 0:
        raise ValueError(f"invalid FEN active color: {active_color}")
    pieces: list[tuple[int, int, int]] = []
    king_squares = [-1, -1]
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
        pieces.append((piece_type, color, square))
        if ch == "K":
            king_squares[0] = square
        elif ch == "k":
            king_squares[1] = square
        file += 1

    if king_squares[side_to_move] < 0:
        raise ValueError(f"missing side-to-move king in FEN: {fen}")
    other_side = 1 - side_to_move
    if king_squares[other_side] < 0:
        raise ValueError(f"missing other-side king in FEN: {fen}")

    return [
        halfka_features_for_perspective(pieces, king_squares[side_to_move], side_to_move),
        halfka_features_for_perspective(pieces, king_squares[other_side], other_side),
    ]


def padded_active_features(fen: str, max_active_features: int) -> list[list[int]]:
    features_by_perspective = active_features_from_fen(fen)
    padded = []
    for perspective_index, features in enumerate(features_by_perspective):
        if len(features) > max_active_features:
            raise ValueError(
                f"position has {len(features)} active features for perspective "
                f"{perspective_index}, but max_active_features={max_active_features}"
            )
        padded.append(features + [PAD_FEATURE_INDEX] * (max_active_features - len(features)))
    return padded


def target_and_base_from_item(item: dict[str, Any],
                              target_clip: float,
                              target_mode: str) -> tuple[float, float]:
    active_color = active_color_from_fen(item["fen"])
    perspective_sign = 1.0 if active_color == "w" else -1.0
    eval_score = float(item["eval_score"])
    eval_score = max(-target_clip, min(target_clip, eval_score))
    eval_score *= perspective_sign
    if target_mode == "eval":
        return eval_score, 0.0

    if target_mode == "residual_static":
        if "static_eval" not in item:
            raise ValueError("target_mode=residual_static requires static_eval in each position")
        base_eval = float(item["static_eval"]) * perspective_sign
        return eval_score - base_eval, base_eval

    raise ValueError(f"unsupported target_mode: {target_mode}")


class NnueJsonDataset(Dataset):
    def __init__(self,
                 path: Path,
                 target_clip: float,
                 max_active_features: int,
                 target_mode: str):
        with path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        self.positions = payload["positions"]
        self.target_clip = target_clip
        self.max_active_features = max_active_features
        self.target_mode = target_mode

    def __len__(self) -> int:
        return len(self.positions)

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        item = self.positions[index]
        features = padded_active_features(item["fen"], self.max_active_features)
        target, base_eval = target_and_base_from_item(item, self.target_clip, self.target_mode)
        return (
            torch.tensor(features, dtype=torch.int32),
            torch.tensor([target], dtype=torch.float32),
            torch.tensor([base_eval], dtype=torch.float32),
        )


class NnueCachedDataset(Dataset):
    def __init__(self,
                 active_features: torch.Tensor,
                 targets: torch.Tensor,
                 base_evals: torch.Tensor):
        self.active_features = active_features
        self.targets = targets
        self.base_evals = base_evals

    def __len__(self) -> int:
        return int(self.targets.shape[0])

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        return self.active_features[index], self.targets[index], self.base_evals[index]


def cache_metadata(path: Path,
                   target_clip: float,
                   max_active_features: int,
                   target_mode: str) -> dict[str, Any]:
    stat = path.stat()
    return {
        "version": 4,
        "source": str(path),
        "source_size": stat.st_size,
        "source_mtime_ns": stat.st_mtime_ns,
        "target_clip": target_clip,
        "target_mode": target_mode,
        "max_active_features": max_active_features,
        "feature_set": "halfka-v2-dual-perspective",
        "perspective_count": PERSPECTIVE_COUNT,
        "square_count": SQUARE_COUNT,
        "halfka_piece_kind_count": HALFKA_PIECE_KIND_COUNT,
        "feature_count": PERSPECTIVE_FEATURE_COUNT,
        "pad_feature_index": PAD_FEATURE_INDEX,
    }


def cache_path_for(input_path: Path,
                   cache_dir: Path,
                   target_clip: float,
                   max_active_features: int,
                   target_mode: str) -> Path:
    clip_name = str(target_clip).replace(".", "p")
    return cache_dir / (
        f"{input_path.stem}_{target_mode}_clip{clip_name}_max{max_active_features}.pt"
    )


def build_feature_cache(input_path: Path,
                        cache_path: Path,
                        target_clip: float,
                        max_active_features: int,
                        target_mode: str,
                        progress: str) -> NnueCachedDataset:
    with input_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    positions = payload["positions"]
    total = len(positions)
    active_features = torch.full(
        (total, PERSPECTIVE_COUNT, max_active_features),
        PAD_FEATURE_INDEX,
        dtype=torch.int32,
    )
    targets = torch.empty((total, 1), dtype=torch.float32)
    base_evals = torch.empty((total, 1), dtype=torch.float32)

    use_tqdm = progress in ("auto", "tqdm") and tqdm is not None
    iterator = enumerate(positions)
    if use_tqdm:
        iterator = tqdm(iterator, total=total, desc=f"cache {input_path.name}", unit="pos")

    for index, item in iterator:
        features_by_perspective = active_features_from_fen(item["fen"])
        for perspective_index, features in enumerate(features_by_perspective):
            if len(features) > max_active_features:
                raise ValueError(
                    f"{input_path}:{index} has {len(features)} active features for perspective "
                    f"{perspective_index}, but max_active_features={max_active_features}"
                )
            if features:
                active_features[index, perspective_index, :len(features)] = torch.tensor(
                    features,
                    dtype=torch.int32,
                )
        target, base_eval = target_and_base_from_item(item, target_clip, target_mode)
        targets[index, 0] = target
        base_evals[index, 0] = base_eval

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "metadata": cache_metadata(input_path, target_clip, max_active_features, target_mode),
            "active_features": active_features,
            "targets": targets,
            "base_evals": base_evals,
        },
        cache_path,
    )
    print(f"cached={cache_path} positions={total}", flush=True)
    return NnueCachedDataset(active_features, targets, base_evals)


def load_cached_dataset(input_path: Path,
                        cache_dir: Path,
                        target_clip: float,
                        max_active_features: int,
                        target_mode: str,
                        rebuild_cache: bool,
                        progress: str) -> NnueCachedDataset:
    cache_path = cache_path_for(input_path, cache_dir, target_clip, max_active_features, target_mode)
    expected_metadata = cache_metadata(input_path, target_clip, max_active_features, target_mode)
    if not rebuild_cache and cache_path.exists():
        payload = torch.load(cache_path, map_location="cpu")
        if payload.get("metadata") == expected_metadata:
            print(f"cache_hit={cache_path}", flush=True)
            return NnueCachedDataset(
                payload["active_features"],
                payload["targets"],
                payload["base_evals"],
            )
        print(f"cache_stale={cache_path}; rebuilding", flush=True)

    return build_feature_cache(
        input_path,
        cache_path,
        target_clip,
        max_active_features,
        target_mode,
        progress,
    )


class ClippedReLU(nn.Module):
    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return torch.clamp(x, 0.0, 1.0)


class BasicNnue(nn.Module):
    def __init__(self, hidden_size: int):
        super().__init__()
        self.hidden_size = hidden_size
        self.feature_weights = nn.ModuleList(
            [
                nn.Embedding(
                    PERSPECTIVE_FEATURE_COUNT + 1,
                    hidden_size,
                    padding_idx=PAD_FEATURE_INDEX,
                )
                for _ in range(PERSPECTIVE_COUNT)
            ]
        )
        self.hidden_bias = nn.Parameter(torch.empty(PERSPECTIVE_COUNT, hidden_size))
        self.act = ClippedReLU()
        self.fc2 = nn.Linear(PERSPECTIVE_COUNT * hidden_size, OUTPUT_HEAD_HIDDEN_SIZE)
        self.fc3 = nn.Linear(OUTPUT_HEAD_HIDDEN_SIZE, OUTPUT_HEAD_HIDDEN_SIZE)
        self.fc4 = nn.Linear(OUTPUT_HEAD_HIDDEN_SIZE, 1)
        self.reset_parameters()

    def reset_parameters(self) -> None:
        bound = 1.0 / math.sqrt(PERSPECTIVE_FEATURE_COUNT)
        for feature_weights in self.feature_weights:
            nn.init.uniform_(feature_weights.weight, -bound, bound)
            with torch.no_grad():
                feature_weights.weight[PAD_FEATURE_INDEX].zero_()
        nn.init.uniform_(self.hidden_bias, -bound, bound)
        self.fc2.reset_parameters()
        self.fc3.reset_parameters()
        self.fc4.reset_parameters()

    def forward(self, active_features: torch.Tensor) -> torch.Tensor:
        perspective_hiddens = []
        for perspective_index, feature_weights in enumerate(self.feature_weights):
            hidden = (
                feature_weights(active_features[:, perspective_index, :]).sum(dim=1) +
                self.hidden_bias[perspective_index]
            )
            perspective_hiddens.append(self.act(hidden))
        x = torch.cat(perspective_hiddens, dim=1)
        x = self.act(self.fc2(x))
        x = self.act(self.fc3(x))
        return self.fc4(x)


class DenseNnue(nn.Module):
    def __init__(self, hidden_size: int, dense_layers: int):
        super().__init__()
        if dense_layers <= 0:
            raise ValueError("dense_layers must be positive")

        layers: list[nn.Module] = []
        input_size = PERSPECTIVE_COUNT * PERSPECTIVE_FEATURE_COUNT
        for _ in range(dense_layers):
            layers.append(nn.Linear(input_size, hidden_size))
            layers.append(ClippedReLU())
            input_size = hidden_size
        layers.append(nn.Linear(input_size, 1))
        self.net = nn.Sequential(*layers)

    def forward(self, active_features: torch.Tensor) -> torch.Tensor:
        batch_size = active_features.shape[0]
        x = torch.zeros(
            (batch_size, PERSPECTIVE_COUNT * PERSPECTIVE_FEATURE_COUNT),
            device=active_features.device,
            dtype=torch.float32,
        )
        valid = active_features < PERSPECTIVE_FEATURE_COUNT
        if valid.any():
            perspective_offsets = (
                torch.arange(PERSPECTIVE_COUNT, device=active_features.device)
                .view(1, PERSPECTIVE_COUNT, 1) *
                PERSPECTIVE_FEATURE_COUNT
            )
            feature_indices = active_features + perspective_offsets
            rows = (
                torch.arange(batch_size, device=active_features.device)
                .view(batch_size, 1, 1)
                .expand_as(active_features)
            )
            x[rows[valid], feature_indices[valid].to(dtype=torch.long)] = 1.0
        return self.net(x)


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
        for features, targets, base_evals in loader:
            features = features.to(device=device, dtype=torch.long)
            targets = targets.to(device)
            base_evals = base_evals.to(device)
            preds = model(features)
            loss = loss_fn(preds / target_scale, targets / target_scale)
            count = targets.numel()
            total_loss += float(loss.item()) * count
            total_abs_error += float(((preds + base_evals) - (targets + base_evals)).abs().sum().item())
            total_count += count
    return total_loss / max(1, total_count), total_abs_error / max(1, total_count)


def make_loss_fn(loss_name: str, huber_delta_cp: float, target_scale: float) -> nn.Module:
    if loss_name == "huber":
        return nn.HuberLoss(delta=huber_delta_cp / target_scale)
    if loss_name == "l1":
        return nn.L1Loss()
    if loss_name == "mse":
        return nn.MSELoss()
    raise ValueError(f"unsupported loss: {loss_name}")


def transform_active_features(active_features: torch.Tensor,
                              flip_files: bool,
                              flip_ranks_and_colors: bool) -> torch.Tensor:
    if not flip_files and not flip_ranks_and_colors:
        return active_features

    transformed = active_features.clone()
    valid_mask = transformed < PERSPECTIVE_FEATURE_COUNT
    if valid_mask.any():
        feature_indices = transformed[valid_mask].to(dtype=torch.long)
        pck = feature_indices % HALFKA_PIECE_KIND_COUNT
        square_pair = feature_indices // HALFKA_PIECE_KIND_COUNT
        pc_square = square_pair % SQUARE_COUNT
        king_square = square_pair // SQUARE_COUNT

        if flip_files:
            king_square = king_square ^ 7
            pc_square = pc_square ^ 7

        transformed[valid_mask] = (
            (king_square * SQUARE_COUNT + pc_square) *
            HALFKA_PIECE_KIND_COUNT +
            pck
        ).to(dtype=transformed.dtype)

    return transformed


def augment_batch(active_features: torch.Tensor,
                  targets: torch.Tensor,
                  base_evals: torch.Tensor,
                  augment_symmetry: str) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    if augment_symmetry == "none":
        return active_features, targets, base_evals

    if augment_symmetry == "mirror":
        choice = int(torch.randint(0, 2, (1,), device=active_features.device).item())
        return transform_active_features(active_features, flip_files=choice == 1,
                                         flip_ranks_and_colors=False), targets, base_evals

    if augment_symmetry == "color":
        choice = int(torch.randint(0, 2, (1,), device=active_features.device).item())
        if choice == 0:
            return active_features, targets, base_evals
        return (
            transform_active_features(active_features, flip_files=False,
                                      flip_ranks_and_colors=True),
            targets,
            base_evals,
        )

    if augment_symmetry == "all":
        choice = int(torch.randint(0, 4, (1,), device=active_features.device).item())
        flip_files = choice in (1, 3)
        flip_ranks_and_colors = choice in (2, 3)
        return (
            transform_active_features(active_features, flip_files=flip_files,
                                      flip_ranks_and_colors=flip_ranks_and_colors),
            targets,
            base_evals,
        )

    raise ValueError(f"unsupported augment_symmetry: {augment_symmetry}")


def export_basic_weights_json(model: BasicNnue,
                              path: Path,
                              hidden_size: int,
                              target_scale: float) -> None:
    state = {key: value.detach().cpu() for key, value in model.state_dict().items()}
    feature_weights_by_perspective = [
        state[f"feature_weights.{perspective_index}.weight"][:PERSPECTIVE_FEATURE_COUNT]
        for perspective_index in range(PERSPECTIVE_COUNT)
    ]
    payload = {
        "format": "basic-nnue-v2",
        "feature_set": "halfka-v2-dual-perspective",
        "input_features": PERSPECTIVE_FEATURE_COUNT,
        "perspective_count": PERSPECTIVE_COUNT,
        "square_count": SQUARE_COUNT,
        "halfka_piece_kind_count": HALFKA_PIECE_KIND_COUNT,
        "hidden_size": hidden_size,
        "output_hidden_size": PERSPECTIVE_COUNT * hidden_size,
        "output_head_hidden_size": OUTPUT_HEAD_HIDDEN_SIZE,
        "target_scale": target_scale,
        "activation": "clipped_relu",
        "score_perspective": "side_to_move",
        "fc1_weight_by_perspective": [
            feature_weight.transpose(0, 1).tolist()
            for feature_weight in feature_weights_by_perspective
        ],
        "fc1_bias_by_perspective": state["hidden_bias"].tolist(),
        "output_layers": [
            {
                "weight": state["fc2.weight"].tolist(),
                "bias": state["fc2.bias"].tolist(),
                "activation": "clipped_relu",
            },
            {
                "weight": state["fc3.weight"].tolist(),
                "bias": state["fc3.bias"].tolist(),
                "activation": "clipped_relu",
            },
            {
                "weight": state["fc4.weight"].tolist(),
                "bias": state["fc4.bias"].tolist(),
                "activation": "none",
            },
        ],
    }
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
        handle.write("\n")


def export_dense_weights_json(model: DenseNnue,
                              path: Path,
                              hidden_size: int,
                              dense_layers: int,
                              target_scale: float) -> None:
    layers = []
    for module in model.net:
        if isinstance(module, nn.Linear):
            layers.append({
                "weight": module.weight.detach().cpu().tolist(),
                "bias": module.bias.detach().cpu().tolist(),
            })

    payload = {
        "format": "dense-nnue-v1",
        "feature_set": "halfka-v2-dual-perspective",
        "input_features": PERSPECTIVE_COUNT * PERSPECTIVE_FEATURE_COUNT,
        "perspective_count": PERSPECTIVE_COUNT,
        "perspective_input_features": PERSPECTIVE_FEATURE_COUNT,
        "square_count": SQUARE_COUNT,
        "halfka_piece_kind_count": HALFKA_PIECE_KIND_COUNT,
        "hidden_size": hidden_size,
        "dense_layers": dense_layers,
        "target_scale": target_scale,
        "activation": "clipped_relu",
        "score_perspective": "side_to_move",
        "layers": layers,
    }
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
        handle.write("\n")


def export_weights_json(model: nn.Module,
                        path: Path,
                        model_type: str,
                        hidden_size: int,
                        dense_layers: int,
                        target_scale: float) -> None:
    if model_type == "basic":
        if not isinstance(model, BasicNnue):
            raise TypeError("basic export requires BasicNnue")
        export_basic_weights_json(model, path, hidden_size, target_scale)
        return

    if model_type == "dense":
        if not isinstance(model, DenseNnue):
            raise TypeError("dense export requires DenseNnue")
        export_dense_weights_json(model, path, hidden_size, dense_layers, target_scale)
        return

    raise ValueError(f"unsupported model_type: {model_type}")


def load_checkpoint_state(model: nn.Module, checkpoint: dict[str, Any]) -> None:
    state = checkpoint["model_state"]
    try:
        model.load_state_dict(state)
        return
    except RuntimeError:
        pass

    if isinstance(model, BasicNnue) and "feature_weights.weight" in state:
        feature_weights = state["feature_weights.weight"]
        with torch.no_grad():
            old_feature_count = feature_weights.shape[0] - 1
            shared_features = min(old_feature_count, PERSPECTIVE_FEATURE_COUNT)
            shared_hidden = min(feature_weights.shape[1], model.hidden_size)
            for perspective_weights in model.feature_weights:
                perspective_weights.weight[:shared_features, :shared_hidden].copy_(
                    feature_weights[:shared_features, :shared_hidden]
                )
                perspective_weights.weight[PAD_FEATURE_INDEX].zero_()
            old_hidden_bias = state["hidden_bias"]
            shared_bias = min(old_hidden_bias.numel(), model.hidden_size)
            for perspective_index in range(PERSPECTIVE_COUNT):
                model.hidden_bias[perspective_index, :shared_bias].copy_(
                    old_hidden_bias[:shared_bias]
                )
            if (
                "fc2.weight" in state and
                "fc2.bias" in state and
                state["fc2.weight"].shape == model.fc2.weight.shape and
                state["fc2.bias"].shape == model.fc2.bias.shape
            ):
                model.fc2.weight.copy_(state["fc2.weight"])
                model.fc2.bias.copy_(state["fc2.bias"])
            if (
                "fc2.bias" in state and
                state["fc2.bias"].numel() == 1 and
                model.fc4.bias.numel() == 1
            ):
                model.fc4.bias.copy_(state["fc2.bias"])
        return

    if isinstance(model, BasicNnue) and "feature_weights.0.weight" in state:
        with torch.no_grad():
            for perspective_index, perspective_weights in enumerate(model.feature_weights):
                key = f"feature_weights.{perspective_index}.weight"
                if key not in state:
                    continue
                old_weights = state[key]
                shared_features = min(old_weights.shape[0] - 1, PERSPECTIVE_FEATURE_COUNT)
                shared_hidden = min(old_weights.shape[1], model.hidden_size)
                perspective_weights.weight[:shared_features, :shared_hidden].copy_(
                    old_weights[:shared_features, :shared_hidden]
                )
                perspective_weights.weight[PAD_FEATURE_INDEX].zero_()

            if "hidden_bias" in state and state["hidden_bias"].shape == model.hidden_bias.shape:
                model.hidden_bias.copy_(state["hidden_bias"])

            for layer_name in ("fc2", "fc3", "fc4"):
                weight_key = f"{layer_name}.weight"
                bias_key = f"{layer_name}.bias"
                layer = getattr(model, layer_name)
                if (
                    weight_key in state and
                    bias_key in state and
                    state[weight_key].shape == layer.weight.shape and
                    state[bias_key].shape == layer.bias.shape
                ):
                    layer.weight.copy_(state[weight_key])
                    layer.bias.copy_(state[bias_key])
            if (
                "fc2.bias" in state and
                state["fc2.bias"].numel() == 1 and
                model.fc4.bias.numel() == 1
            ):
                model.fc4.bias.copy_(state["fc2.bias"])
        return

    if "fc1.weight" not in state or "fc1.bias" not in state:
        raise ValueError("unsupported checkpoint model_state format")

    if not isinstance(model, BasicNnue):
        raise ValueError("legacy dense checkpoint conversion is unsupported")

    with torch.no_grad():
        model.hidden_bias.zero_()
        model.fc2.weight.zero_()
        for perspective_weights in model.feature_weights:
            perspective_weights.weight[PAD_FEATURE_INDEX].zero_()
        shared_bias = min(state["fc1.bias"].numel(), model.hidden_size)
        for perspective_index in range(PERSPECTIVE_COUNT):
            model.hidden_bias[perspective_index, :shared_bias].copy_(state["fc1.bias"][:shared_bias])
        shared_output = min(state["fc2.weight"].shape[1], model.hidden_size)
        model.fc2.weight[:, :shared_output].copy_(state["fc2.weight"][:, :shared_output])
        model.fc2.bias.copy_(state["fc2.bias"])


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Train a dual-perspective HalfKAv2 NNUE-style evaluator."
    )
    parser.add_argument("--train", default="nnue/splits/train.json")
    parser.add_argument("--val", default="nnue/splits/val.json")
    parser.add_argument("--out-dir", default="nnue/runs/basic")
    parser.add_argument("--model-type", choices=("basic", "dense"), default="basic")
    parser.add_argument("--hidden-size", type=int, default=DEFAULT_HIDDEN_SIZE)
    parser.add_argument("--dense-layers", type=int, default=2)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--target-clip", type=float, default=2000.0)
    parser.add_argument("--target-scale", type=float, default=1000.0)
    parser.add_argument(
        "--target-mode",
        choices=("eval", "residual_static"),
        default="eval",
        help="Train directly on eval_score or on eval_score - static_eval.",
    )
    parser.add_argument("--loss", choices=("huber", "l1", "mse"), default="huber")
    parser.add_argument(
        "--huber-delta-cp",
        type=float,
        default=1000.0,
        help="Huber transition point in centipawns before target scaling.",
    )
    parser.add_argument("--max-active-features", type=int, default=DEFAULT_MAX_ACTIVE_FEATURES)
    parser.add_argument("--cache-dir", default="nnue/cache")
    parser.add_argument("--rebuild-cache", action="store_true")
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--seed", type=int, default=20260627)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--progress", choices=("auto", "tqdm", "log", "none"), default="auto")
    parser.add_argument("--log-every-batches", type=int, default=10)
    parser.add_argument(
        "--selection-metric",
        choices=("val_loss", "val_mae_cp"),
        default="val_loss",
        help="Metric used to choose the saved best checkpoint.",
    )
    parser.add_argument(
        "--stop-mae-cp",
        type=float,
        default=0.0,
        help="Stop early once validation MAE is at or below this centipawn value.",
    )
    parser.add_argument(
        "--export-weights",
        choices=("best", "final", "none"),
        default="best",
        help="When to export JSON weights alongside the PyTorch checkpoint.",
    )
    parser.add_argument(
        "--augment-symmetry",
        choices=("none", "mirror", "color", "all"),
        default="none",
        help="Apply chess symmetry augmentation to training batches.",
    )
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
        train_dataset = NnueJsonDataset(
            train_path,
            args.target_clip,
            args.max_active_features,
            args.target_mode,
        )
        val_dataset = NnueJsonDataset(
            val_path,
            args.target_clip,
            args.max_active_features,
            args.target_mode,
        )
    else:
        cache_dir = Path(args.cache_dir)
        train_dataset = load_cached_dataset(
            train_path,
            cache_dir,
            args.target_clip,
            args.max_active_features,
            args.target_mode,
            args.rebuild_cache,
            args.progress,
        )
        val_dataset = load_cached_dataset(
            val_path,
            cache_dir,
            args.target_clip,
            args.max_active_features,
            args.target_mode,
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

    if args.model_type == "basic":
        model = BasicNnue(args.hidden_size).to(device)
    else:
        model = DenseNnue(args.hidden_size, args.dense_layers).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    loss_fn = make_loss_fn(args.loss, args.huber_delta_cp, args.target_scale)

    best_selection_value = math.inf
    metrics = []
    start_epoch = 1
    saved_best = False

    if args.resume:
        checkpoint_path = Path(args.resume)
        checkpoint = torch.load(checkpoint_path, map_location="cpu")
        checkpoint_hidden_size = int(checkpoint.get("hidden_size", args.hidden_size))
        checkpoint_feature_count = checkpoint.get("feature_count")
        checkpoint_state = checkpoint.get("model_state", {})
        if checkpoint_feature_count is None:
            feature_weights = checkpoint_state.get("feature_weights.0.weight")
            if feature_weights is None:
                feature_weights = checkpoint_state.get("feature_weights.weight")
            checkpoint_feature_count = (
                int(feature_weights.shape[0] - 1)
                if feature_weights is not None
                else PERSPECTIVE_FEATURE_COUNT
            )
        checkpoint_feature_count = int(checkpoint_feature_count)
        checkpoint_perspective_count = int(
            checkpoint.get(
                "perspective_count",
                PERSPECTIVE_COUNT if "feature_weights.0.weight" in checkpoint_state else 1,
            )
        )
        checkpoint_model_type = str(checkpoint.get("model_type", "basic"))
        checkpoint_dense_layers = int(checkpoint.get("dense_layers", args.dense_layers))
        if checkpoint_model_type != args.model_type:
            raise ValueError(
                f"checkpoint model_type={checkpoint_model_type} "
                f"does not match --model-type={args.model_type}"
            )
        if checkpoint_hidden_size != args.hidden_size:
            raise ValueError(
                f"checkpoint hidden_size={checkpoint_hidden_size} "
                f"does not match --hidden-size={args.hidden_size}"
            )
        if args.model_type == "dense" and checkpoint_dense_layers != args.dense_layers:
            raise ValueError(
                f"checkpoint dense_layers={checkpoint_dense_layers} "
                f"does not match --dense-layers={args.dense_layers}"
            )
        expected_dense_feature_count = PERSPECTIVE_COUNT * PERSPECTIVE_FEATURE_COUNT
        if args.model_type == "dense" and checkpoint_feature_count != expected_dense_feature_count:
            raise ValueError(
                f"checkpoint feature_count={checkpoint_feature_count} "
                f"does not match expected {expected_dense_feature_count} for dense model"
            )
        if checkpoint_feature_count != PERSPECTIVE_FEATURE_COUNT:
            print(
                f"info: checkpoint feature_count={checkpoint_feature_count}; "
                f"reinitializing dual-perspective HalfKAv2 feature embeddings",
                flush=True,
            )
        if args.model_type == "basic" and checkpoint_perspective_count != PERSPECTIVE_COUNT:
            print(
                f"info: checkpoint perspective_count={checkpoint_perspective_count}; "
                f"adapting to perspective_count={PERSPECTIVE_COUNT}",
                flush=True,
            )
        checkpoint_target_scale = float(checkpoint.get("target_scale", args.target_scale))
        checkpoint_target_clip = float(checkpoint.get("target_clip", args.target_clip))
        checkpoint_loss = str(checkpoint.get("loss", args.loss))
        checkpoint_huber_delta_cp = float(checkpoint.get("huber_delta_cp", args.huber_delta_cp))
        checkpoint_target_mode = str(checkpoint.get("target_mode", args.target_mode))
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
        if checkpoint_loss != args.loss:
            print(
                f"warning: checkpoint loss={checkpoint_loss} but --loss={args.loss}",
                flush=True,
            )
        if checkpoint_huber_delta_cp != args.huber_delta_cp:
            print(
                f"warning: checkpoint huber_delta_cp={checkpoint_huber_delta_cp} "
                f"but --huber-delta-cp={args.huber_delta_cp}",
                flush=True,
            )
        if checkpoint_target_mode != args.target_mode:
            print(
                f"warning: checkpoint target_mode={checkpoint_target_mode} "
                f"but --target-mode={args.target_mode}",
                flush=True,
            )

        load_checkpoint_state(model, checkpoint)
        previous_metrics = checkpoint.get("metrics", [])
        if isinstance(previous_metrics, list):
            metrics = list(previous_metrics)
            if metrics:
                best_selection_value = min(float(row[args.selection_metric]) for row in metrics)
                start_epoch = int(metrics[-1].get("epoch", len(metrics))) + 1
        print(
            f"resumed={checkpoint_path} start_epoch={start_epoch} "
            f"previous_epochs={len(metrics)}",
            flush=True,
        )

    print(f"device={device}")
    print(
        f"train={len(train_dataset)} val={len(val_dataset)} "
        f"model={args.model_type} hidden={args.hidden_size} dense_layers={args.dense_layers}"
    )
    print(
        f"loss={args.loss} huber_delta_cp={args.huber_delta_cp} "
        f"target_scale={args.target_scale} target_mode={args.target_mode} "
        f"augment_symmetry={args.augment_symmetry}"
    )
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

        for batch_index, (features, targets, base_evals) in enumerate(train_iter, start=1):
            features = features.to(device=device, dtype=torch.long)
            targets = targets.to(device)
            base_evals = base_evals.to(device)
            features, targets, base_evals = augment_batch(
                features,
                targets,
                base_evals,
                args.augment_symmetry,
            )
            targets = targets / args.target_scale

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

        with (out_dir / "metrics.json").open("w", encoding="utf-8") as handle:
            json.dump(metrics, handle, indent=2)
            handle.write("\n")

        selection_value = float(row[args.selection_metric])
        if selection_value < best_selection_value:
            best_selection_value = selection_value
            torch.save(
                {
                    "model_state": model.state_dict(),
                    "hidden_size": args.hidden_size,
                    "output_head_hidden_size": OUTPUT_HEAD_HIDDEN_SIZE,
                    "target_scale": args.target_scale,
                    "target_clip": args.target_clip,
                    "target_mode": args.target_mode,
                    "loss": args.loss,
                    "huber_delta_cp": args.huber_delta_cp,
                    "feature_set": "halfka-v2-dual-perspective",
                    "perspective_count": PERSPECTIVE_COUNT,
                    "square_count": SQUARE_COUNT,
                    "halfka_piece_kind_count": HALFKA_PIECE_KIND_COUNT,
                    "feature_count": PERSPECTIVE_FEATURE_COUNT,
                    "max_active_features": args.max_active_features,
                    "model_type": args.model_type,
                    "dense_layers": args.dense_layers,
                    "model_format": "dual-perspective-embedding-sum-v2-halfka-v2" if args.model_type == "basic" else "dual-perspective-dense-mlp-v1-halfka-v2",
                    "selection_metric": args.selection_metric,
                    "augment_symmetry": args.augment_symmetry,
                    "metrics": metrics,
                },
                out_dir / "basic_nnue.pt",
            )
            if args.export_weights == "best":
                export_weights_json(
                    model,
                    out_dir / "basic_nnue_weights.json",
                    args.model_type,
                    args.hidden_size,
                    args.dense_layers,
                    args.target_scale,
                )
            saved_best = True

        if args.stop_mae_cp > 0.0 and val_mae_cp <= args.stop_mae_cp:
            print(
                f"early_stop=val_mae_cp threshold={args.stop_mae_cp:.2f} "
                f"actual={val_mae_cp:.2f}",
                flush=True,
            )
            break

    if not saved_best:
        torch.save(
            {
                "model_state": model.state_dict(),
                "hidden_size": args.hidden_size,
                "output_head_hidden_size": OUTPUT_HEAD_HIDDEN_SIZE,
                "target_scale": args.target_scale,
                "target_clip": args.target_clip,
                "target_mode": args.target_mode,
                "loss": args.loss,
                "huber_delta_cp": args.huber_delta_cp,
                "feature_set": "halfka-v2-dual-perspective",
                "perspective_count": PERSPECTIVE_COUNT,
                "square_count": SQUARE_COUNT,
                "halfka_piece_kind_count": HALFKA_PIECE_KIND_COUNT,
                "feature_count": PERSPECTIVE_FEATURE_COUNT,
                "max_active_features": args.max_active_features,
                "model_type": args.model_type,
                "dense_layers": args.dense_layers,
                "model_format": "dual-perspective-embedding-sum-v2-halfka-v2" if args.model_type == "basic" else "dual-perspective-dense-mlp-v1-halfka-v2",
                "selection_metric": args.selection_metric,
                "augment_symmetry": args.augment_symmetry,
                "metrics": metrics,
            },
            out_dir / "basic_nnue.pt",
        )
        if args.export_weights != "none":
            export_weights_json(
                model,
                out_dir / "basic_nnue_weights.json",
                args.model_type,
                args.hidden_size,
                args.dense_layers,
                args.target_scale,
            )

    if saved_best and args.export_weights == "final":
        export_weights_json(
            model,
            out_dir / "basic_nnue_weights.json",
            args.model_type,
            args.hidden_size,
            args.dense_layers,
            args.target_scale,
        )

    print(f"saved={out_dir / 'basic_nnue.pt'}")
    if args.export_weights != "none":
        print(f"exported={out_dir / 'basic_nnue_weights.json'}")
    else:
        print("exported=none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
