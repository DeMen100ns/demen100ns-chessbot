#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def format_float(value: float) -> str:
    return f"{value:.9g}f"


def write_vector(handle, values: list[float], indent: str = "    ") -> None:
    for start in range(0, len(values), 8):
        chunk = values[start:start + 8]
        handle.write(indent)
        handle.write(", ".join(format_float(value) for value in chunk))
        if start + 8 < len(values):
            handle.write(",")
        handle.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Export basic NNUE JSON weights to a C++ header.")
    parser.add_argument("--input", default="nnue/runs/basic/basic_nnue_weights.json")
    parser.add_argument("--output", default="include/chess/nnue_basic_weights.h")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    with input_path.open("r", encoding="utf-8") as handle:
        weights = json.load(handle)

    if weights.get("format") != "basic-nnue-v1":
        raise SystemExit(f"unsupported weights format: {weights.get('format')}")

    input_features = int(weights["input_features"])
    hidden_size = int(weights["hidden_size"])
    fc1_weight = weights["fc1_weight"]
    fc1_bias = weights["fc1_bias"]
    fc2_weight = weights["fc2_weight"]
    fc2_bias = float(weights["fc2_bias"])

    if input_features < 768:
        raise SystemExit(f"expected at least 768 input features, got {input_features}")
    if len(fc1_weight) != hidden_size or len(fc1_bias) != hidden_size or len(fc2_weight) != hidden_size:
        raise SystemExit("hidden size mismatch in weights")
    if any(len(row) != input_features for row in fc1_weight):
        raise SystemExit("fc1 row width mismatch")
    fc1_weight_by_feature = [
        [fc1_weight[hidden_index][feature_index] for hidden_index in range(hidden_size)]
        for feature_index in range(input_features)
    ]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        handle.write("#pragma once\n\n")
        handle.write("#include <array>\n\n")
        handle.write("namespace ChessNnueWeights {\n\n")
        handle.write(f"constexpr int kInputFeatures = {input_features};\n")
        handle.write(f"constexpr int kHiddenSize = {hidden_size};\n")
        handle.write("constexpr float kFc1Bias[kHiddenSize] = {\n")
        write_vector(handle, fc1_bias)
        handle.write("};\n\n")
        handle.write("constexpr float kFc1WeightByFeature[kInputFeatures][kHiddenSize] = {\n")
        for feature_index, row in enumerate(fc1_weight_by_feature):
            handle.write("    {\n")
            write_vector(handle, row, "        ")
            handle.write("    }")
            handle.write("," if feature_index + 1 < input_features else "")
            handle.write("\n")
        handle.write("};\n\n")
        handle.write("constexpr float kFc2Weight[kHiddenSize] = {\n")
        write_vector(handle, fc2_weight)
        handle.write("};\n\n")
        handle.write(f"constexpr float kFc2Bias = {format_float(fc2_bias)};\n\n")
        handle.write("}  // namespace ChessNnueWeights\n")

    print(f"wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
