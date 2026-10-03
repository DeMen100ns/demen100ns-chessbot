"""Show that the archived v7 (archive/bots/v7/v7) evaluates with an embedded 768-input NNUE (2x6x64 -> 128 -> 1).

The v7 binary holds two identical 768x128 float tables (one per translation unit that
includes the weights header), followed by the 128 hidden biases and 128 output weights,
at the same layout the current header produces in v7.2 (which has 770 rows: +2
side-to-move features). This script reads those floats, evaluates the 40 speed positions
with the Jun 29 evaluate_nnue() formula (feature = color*384 + type*64 + square, ReLU,
white-POV score negated for black), fits the one value that is compiled into code rather
than data (the scalar output bias), and compares with v7's reported static_eval
from eval_check.csv.

Run from benchmarks/versions after eval_check.py:  .venv/bin/python tools/v7_nnue_check.py
"""

from __future__ import annotations

import csv
import statistics
import struct
import sys
from pathlib import Path

import chess

HERE = Path(__file__).resolve().parents[1]
BINARY = HERE.parents[1] / "archive" / "bots" / "v7" / "v7"
ROWS, HIDDEN = 768, 128
COPY1_OFFSET = 154224          # first 768x128 table
COPY2_OFFSET = 548000          # second table; hidden biases sit just before, output weights just after
TYPES = {chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4, chess.KING: 5}


def floats(data: bytes, offset: int, count: int) -> tuple[float, ...]:
    return struct.unpack_from(f"<{count}f", data, offset)


def main() -> None:
    data = BINARY.read_bytes()
    weights = floats(data, COPY2_OFFSET, ROWS * HIDDEN)
    if floats(data, COPY1_OFFSET, ROWS * HIDDEN) != weights:
        sys.exit("weight tables not found at the expected offsets (different v7 binary?)")
    bias1 = floats(data, COPY2_OFFSET - HIDDEN * 4, HIDDEN)
    out_w = floats(data, COPY2_OFFSET + ROWS * HIDDEN * 4, HIDDEN)

    row = next(r for r in csv.DictReader(open(HERE / "eval_check.csv")) if r["version"] == "v7")
    reported = [int(x) for x in row["static_evals"].split()]
    fens = [line.split("\t")[2] for line in (HERE / "positions.txt").read_text().splitlines()
            if line.startswith("P")]

    raw = []
    for fen in fens:
        board = chess.Board(fen)
        acc = list(bias1)
        for square, piece in board.piece_map().items():
            feature = (0 if piece.color == chess.WHITE else 1) * 384 + TYPES[piece.piece_type] * 64 + square
            base = feature * HIDDEN
            for i in range(HIDDEN):
                acc[i] += weights[base + i]
        raw.append((sum(max(0.0, a) * w for a, w in zip(acc, out_w)),
                    1 if board.turn == chess.WHITE else -1))

    bias2 = statistics.median(e * sign - s for (s, sign), e in zip(raw, reported))
    predicted = [sign * round(bias2 + s) for s, sign in raw]
    exact = sum(p == e for p, e in zip(predicted, reported))
    worst = max(abs(p - e) for p, e in zip(predicted, reported))
    print(f"fitted output bias {bias2:.3f}; exact matches {exact}/{len(fens)}; max |diff| {worst} cp")


if __name__ == "__main__":
    main()
