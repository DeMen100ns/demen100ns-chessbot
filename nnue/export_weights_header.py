#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def format_float(value: float) -> str:
    text = f"{value:.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return f"{text}f"


def write_vector(handle, values: list[float], indent: str = "    ") -> None:
    for start in range(0, len(values), 8):
        chunk = values[start:start + 8]
        handle.write(indent)
        handle.write(", ".join(format_float(value) for value in chunk))
        if start + 8 < len(values):
            handle.write(",")
        handle.write("\n")


def write_matrix(handle, values: list[list[float]], indent: str = "    ") -> None:
    for row_index, row in enumerate(values):
        handle.write(f"{indent}{{\n")
        write_vector(handle, row, indent + "    ")
        handle.write(f"{indent}}}")
        handle.write("," if row_index + 1 < len(values) else "")
        handle.write("\n")


def validate_dense_layer(layer: dict,
                         input_size: int,
                         output_size: int,
                         layer_name: str) -> tuple[list[list[float]], list[float]]:
    weight = layer["weight"]
    bias = layer["bias"]
    if len(weight) != output_size or len(bias) != output_size:
        raise SystemExit(f"{layer_name} output size mismatch")
    if any(len(row) != input_size for row in weight):
        raise SystemExit(f"{layer_name} input size mismatch")
    return weight, bias


def write_legacy_header(handle,
                        input_features: int,
                        hidden_size: int,
                        fc1_weight: list[list[float]],
                        fc1_bias: list[float],
                        fc2_weight: list[float],
                        fc2_bias: float) -> None:
    if input_features < 770:
        raise SystemExit(f"expected at least 770 input features, got {input_features}")
    if len(fc1_weight) != hidden_size or len(fc1_bias) != hidden_size or len(fc2_weight) != hidden_size:
        raise SystemExit("hidden size mismatch in weights")
    if any(len(row) != input_features for row in fc1_weight):
        raise SystemExit("fc1 row width mismatch")

    handle.write("#define CHESS_NNUE_DUAL_PERSPECTIVE 0\n\n")
    handle.write("namespace ChessNnueWeights {\n\n")
    handle.write(f"constexpr int kInputFeatures = {input_features};\n")
    handle.write(f"constexpr int kHiddenSize = {hidden_size};\n")
    handle.write(f"constexpr int kOutputHiddenSize = {hidden_size};\n")
    handle.write("constexpr float kFc1Bias[kHiddenSize] = {\n")
    write_vector(handle, fc1_bias)
    handle.write("};\n\n")
    handle.write("constexpr float kFc1WeightByFeature[kInputFeatures][kHiddenSize] = {\n")
    for feature_index in range(input_features):
        row = [
            fc1_weight[hidden_index][feature_index]
            for hidden_index in range(hidden_size)
        ]
        handle.write("    {\n")
        write_vector(handle, row, "        ")
        handle.write("    }")
        handle.write("," if feature_index + 1 < input_features else "")
        handle.write("\n")
    handle.write("};\n\n")
    handle.write("constexpr float kFc2Weight[kOutputHiddenSize] = {\n")
    write_vector(handle, fc2_weight)
    handle.write("};\n\n")
    handle.write(f"constexpr float kFc2Bias = {format_float(fc2_bias)};\n\n")
    handle.write("}  // namespace ChessNnueWeights\n")


def write_dual_perspective_header(handle,
                                  input_features: int,
                                  perspective_count: int,
                                  hidden_size: int,
                                  output_hidden_size: int,
                                  fc1_weight_by_perspective: list[list[list[float]]],
                                  fc1_bias_by_perspective: list[list[float]],
                                  output_layers: list[dict]) -> None:
    if perspective_count != 2:
        raise SystemExit(f"expected 2 perspectives, got {perspective_count}")
    if input_features != 64 * 64 * 11:
        raise SystemExit(f"expected 45056 HalfKAv2 features, got {input_features}")
    if output_hidden_size != perspective_count * hidden_size:
        raise SystemExit(
            f"expected output_hidden_size={perspective_count * hidden_size}, "
            f"got {output_hidden_size}"
        )
    if len(fc1_weight_by_perspective) != perspective_count:
        raise SystemExit("perspective weight count mismatch")
    if len(fc1_bias_by_perspective) != perspective_count:
        raise SystemExit("perspective bias count mismatch")
    if len(output_layers) != 3:
        raise SystemExit(f"expected 3 output layers, got {len(output_layers)}")
    output_layer1_size = len(output_layers[0]["bias"])
    output_layer2_size = len(output_layers[1]["bias"])
    if output_layer1_size != 32 or output_layer2_size != 32:
        raise SystemExit(
            f"expected output head sizes 32,32; got {output_layer1_size},{output_layer2_size}"
        )
    output_layer1_weight, output_layer1_bias = validate_dense_layer(
        output_layers[0],
        output_hidden_size,
        output_layer1_size,
        "output layer 1",
    )
    output_layer2_weight, output_layer2_bias = validate_dense_layer(
        output_layers[1],
        output_layer1_size,
        output_layer2_size,
        "output layer 2",
    )
    output_layer3_weight, output_layer3_bias = validate_dense_layer(
        output_layers[2],
        output_layer2_size,
        1,
        "output layer 3",
    )

    for perspective_index, fc1_weight in enumerate(fc1_weight_by_perspective):
        if len(fc1_weight) != hidden_size:
            raise SystemExit(f"hidden size mismatch in perspective {perspective_index}")
        if len(fc1_bias_by_perspective[perspective_index]) != hidden_size:
            raise SystemExit(f"bias size mismatch in perspective {perspective_index}")
        if any(len(row) != input_features for row in fc1_weight):
            raise SystemExit(f"fc1 row width mismatch in perspective {perspective_index}")

    handle.write("#define CHESS_NNUE_DUAL_PERSPECTIVE 1\n\n")
    handle.write("namespace ChessNnueWeights {\n\n")
    handle.write(f"constexpr int kInputFeatures = {input_features};\n")
    handle.write(f"constexpr int kPerspectiveCount = {perspective_count};\n")
    handle.write(f"constexpr int kHiddenSize = {hidden_size};\n")
    handle.write(f"constexpr int kOutputHiddenSize = {output_hidden_size};\n")
    handle.write(f"constexpr int kOutputLayer1Size = {output_layer1_size};\n")
    handle.write(f"constexpr int kOutputLayer2Size = {output_layer2_size};\n")
    handle.write("constexpr float kFc1BiasByPerspective[kPerspectiveCount][kHiddenSize] = {\n")
    for perspective_index, bias in enumerate(fc1_bias_by_perspective):
        handle.write("    {\n")
        write_vector(handle, bias, "        ")
        handle.write("    }")
        handle.write("," if perspective_index + 1 < perspective_count else "")
        handle.write("\n")
    handle.write("};\n\n")
    handle.write(
        "constexpr float "
        "kFc1WeightByPerspectiveFeature[kPerspectiveCount][kInputFeatures][kHiddenSize] = {\n"
    )
    for perspective_index, fc1_weight in enumerate(fc1_weight_by_perspective):
        handle.write("    {\n")
        for feature_index in range(input_features):
            row = [
                fc1_weight[hidden_index][feature_index]
                for hidden_index in range(hidden_size)
            ]
            handle.write("        {\n")
            write_vector(handle, row, "            ")
            handle.write("        }")
            handle.write("," if feature_index + 1 < input_features else "")
            handle.write("\n")
        handle.write("    }")
        handle.write("," if perspective_index + 1 < perspective_count else "")
        handle.write("\n")
    handle.write("};\n\n")
    handle.write("constexpr float kOutputLayer1Weight[kOutputLayer1Size][kOutputHiddenSize] = {\n")
    write_matrix(handle, output_layer1_weight)
    handle.write("};\n\n")
    handle.write("constexpr float kOutputLayer1Bias[kOutputLayer1Size] = {\n")
    write_vector(handle, output_layer1_bias)
    handle.write("};\n\n")
    handle.write("constexpr float kOutputLayer2Weight[kOutputLayer2Size][kOutputLayer1Size] = {\n")
    write_matrix(handle, output_layer2_weight)
    handle.write("};\n\n")
    handle.write("constexpr float kOutputLayer2Bias[kOutputLayer2Size] = {\n")
    write_vector(handle, output_layer2_bias)
    handle.write("};\n\n")
    handle.write("constexpr float kOutputLayer3Weight[kOutputLayer2Size] = {\n")
    write_vector(handle, output_layer3_weight[0])
    handle.write("};\n\n")
    handle.write(f"constexpr float kOutputLayer3Bias = {format_float(output_layer3_bias[0])};\n\n")
    handle.write("}  // namespace ChessNnueWeights\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Export basic NNUE JSON weights to a C++ header.")
    parser.add_argument("--input", default="nnue/runs/basic/basic_nnue_weights.json")
    parser.add_argument("--output", default="include/chess/nnue_basic_weights.h")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    with input_path.open("r", encoding="utf-8") as handle:
        weights = json.load(handle)

    weights_format = weights.get("format")
    if weights_format not in ("basic-nnue-v1", "basic-nnue-v2"):
        raise SystemExit(f"unsupported weights format: {weights_format}")

    input_features = int(weights["input_features"])
    hidden_size = int(weights["hidden_size"])
    perspective_count = int(weights.get("perspective_count", 1))
    output_hidden_size = int(
        weights.get("output_hidden_size", perspective_count * hidden_size)
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        handle.write("#pragma once\n\n")
        handle.write("#include <array>\n\n")
        if perspective_count == 1:
            write_legacy_header(
                handle,
                input_features,
                hidden_size,
                weights["fc1_weight"],
                weights["fc1_bias"],
                weights["fc2_weight"],
                float(weights["fc2_bias"]),
            )
        else:
            write_dual_perspective_header(
                handle,
                input_features,
                perspective_count,
                hidden_size,
                output_hidden_size,
                weights["fc1_weight_by_perspective"],
                weights["fc1_bias_by_perspective"],
                weights["output_layers"],
            )

    print(f"wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
