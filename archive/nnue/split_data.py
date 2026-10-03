#!/usr/bin/env python3
import argparse
import json
import random
from pathlib import Path


def write_split(path: Path, source: dict, split_name: str, positions: list[dict]) -> None:
    payload = {
        "source_file": source.get("source_file"),
        "source_depth": source.get("depth"),
        "score_perspective": source.get("score_perspective", "white"),
        "split": split_name,
        "count": len(positions),
        "positions": positions,
    }
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
        handle.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Split NNUE JSON data into train/val/test.")
    parser.add_argument("--input", default="nnue/data_100k.json")
    parser.add_argument("--output-dir", default="nnue/splits")
    parser.add_argument("--seed", type=int, default=20260627)
    parser.add_argument("--train-ratio", type=float, default=0.90)
    parser.add_argument("--val-ratio", type=float, default=0.05)
    parser.add_argument("--test-ratio", type=float, default=0.05)
    args = parser.parse_args()

    ratio_sum = args.train_ratio + args.val_ratio + args.test_ratio
    if abs(ratio_sum - 1.0) > 1e-9:
        raise SystemExit(f"ratios must sum to 1.0, got {ratio_sum}")

    input_path = Path(args.input)
    with input_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    positions = list(data["positions"])
    random.Random(args.seed).shuffle(positions)

    total = len(positions)
    train_count = int(total * args.train_ratio)
    val_count = int(total * args.val_ratio)
    test_count = total - train_count - val_count

    train = positions[:train_count]
    val = positions[train_count:train_count + val_count]
    test = positions[train_count + val_count:]

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    data = dict(data)
    data["source_file"] = str(input_path)
    write_split(output_dir / "train.json", data, "train", train)
    write_split(output_dir / "val.json", data, "val", val)
    write_split(output_dir / "test.json", data, "test", test)

    print(f"input={input_path}")
    print(f"total={total}")
    print(f"train={len(train)} val={len(val)} test={len(test)}")
    print(f"output_dir={output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
