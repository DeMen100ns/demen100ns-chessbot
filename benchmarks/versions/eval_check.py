"""Identify which evaluator each frozen version (and current main / Heroku) uses.

1. Builds commit b1c6b89 (the Heroku build at the time of the benchmark, and the last commit
   whose engine still has evaluate_nnue()) into .build/ and links tools/evalboth.cpp against
   it, which prints the handcrafted evaluate() and evaluate_nnue() for a FEN.
2. Asks every frozen binary, the b1c6b89 build and today's bots/v7 for its `static_eval` on
   the 40 speed positions (static_eval is printed by every bridge version).
3. Side-to-move symmetry: on quiet positions where flipping the side to move is legal, a
   symmetric side-to-move evaluator gives eval(A to move) + eval(B to move) == 0.

Writes eval_check.csv and prints a summary table.
"""

from __future__ import annotations

import csv
import io
import subprocess
import tarfile
from pathlib import Path

import chess

from engine import HERE, NO_LIMIT_MS, ROOT, VERSIONS, Engine, read_positions

BUILD = HERE / ".build"
REFERENCE_COMMIT = "b1c6b89"


def build_current() -> tuple[Path, Path, str]:
    """Build chess_engine_bridge + evalboth from REFERENCE_COMMIT. Returns (bridge, evalboth, commit)."""
    commit = REFERENCE_COMMIT
    src = BUILD / f"src_{commit}"
    bridge = src / "cbuild" / "chess_engine_bridge"
    evalboth = BUILD / f"evalboth_{commit}"
    if not bridge.exists():
        src.mkdir(parents=True, exist_ok=True)
        archive = subprocess.run(["git", "archive", commit], cwd=ROOT, capture_output=True, check=True).stdout
        tarfile.open(fileobj=io.BytesIO(archive)).extractall(src, filter="data")
        subprocess.run(["cmake", "-S", ".", "-B", "cbuild", "-DCMAKE_BUILD_TYPE=Release"], cwd=src,
                       check=True, capture_output=True)
        subprocess.run(["cmake", "--build", "cbuild", "-j4", "--target", "chess_engine_bridge"],
                       cwd=src, check=True, capture_output=True)
    if not evalboth.exists():
        subprocess.run(["c++", "-std=c++20", "-O2", "-Iinclude", "-Iinclude/chess",
                        str(HERE / "tools" / "evalboth.cpp"), "cbuild/libchess_engine.a",
                        "-o", str(evalboth)], cwd=src, check=True)
    return bridge, evalboth, commit


def static_evals(engine: Engine, fens: list[str]) -> list[int]:
    out = []
    for fen in fens:
        engine.newgame()
        out.append(engine.go(1, NO_LIMIT_MS, fen).int_field("static_eval"))
    return out


def flipped(fen: str) -> str | None:
    board = chess.Board(fen)
    if board.is_check():
        return None
    board.turn = not board.turn
    board.ep_square = None
    if not board.is_valid() or board.is_check() or not any(board.legal_moves):
        return None
    return board.fen()


def main() -> None:
    bridge, evalboth, commit = build_current()
    positions = read_positions(HERE / "positions.txt")
    fens = [fen for _, _, fen in positions]
    ref = subprocess.run([str(evalboth)], input="\n".join(fens) + "\n", capture_output=True,
                         text=True, check=True).stdout.split("\n")
    hand = [int(line.split("\t")[0]) for line in ref if line]
    nnue = [int(line.split("\t")[1]) for line in ref if line]

    pairs = [(fen, f) for fen in fens if (f := flipped(fen))][:12]

    engines: dict[str, Path | None] = {v: None for v in VERSIONS}
    engines[f"main {commit} (Heroku build)"] = bridge
    engines["bots/v7 (new, handcrafted)"] = ROOT / "bots" / "v7" / "v7"
    rows = []
    for name, binary in engines.items():
        with Engine(name, binary=binary) as eng:
            ev = static_evals(eng, fens)
            sym = [a + b for a, b in zip(static_evals(eng, [p[0] for p in pairs]),
                                         static_evals(eng, [p[1] for p in pairs]))]
        rows.append({
            "version": name,
            "match_handcrafted": sum(a == b for a, b in zip(ev, hand)),
            "match_nnue": sum(a == b for a, b in zip(ev, nnue)),
            "stm_sum_mean": round(sum(sym) / len(sym), 1),
            "stm_sum_min": min(sym), "stm_sum_max": max(sym),
            "static_evals": " ".join(map(str, ev)),
        })

    with open(HERE / "eval_check.csv", "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    print(f"Reference evaluators from commit {commit}; {len(fens)} positions; "
          f"{len(pairs)} side-to-move flip pairs.\n")
    print("| version | == current handcrafted | == current NNUE | eval(A)+eval(B) mean [min, max] |")
    print("|---|---:|---:|---|")
    for r in rows:
        print(f"| {r['version']} | {r['match_handcrafted']}/{len(fens)} | {r['match_nnue']}/{len(fens)} | "
              f"{r['stm_sum_mean']:+} [{r['stm_sum_min']:+}, {r['stm_sum_max']:+}] |")


if __name__ == "__main__":
    main()
