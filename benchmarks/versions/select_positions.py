"""Pick the fixed speed-test positions and match openings from data/bench_positions.json.

data/bench_positions.json and data/bench_positions_generated.json are byte-identical
(1000 lichess positions, Stockfish depth-10 eval within +-30 cp), so only one is read.

Writes:
  positions.txt  40 speed positions: 14 opening, 16 middlegame, 10 endgame (all > 5 pieces)
  openings.txt   100 balanced match openings, disjoint from positions.txt.
                 The first 50 are the standard set; 51-100 are only used when a pair is
                 within +-30 Elo after 100 games and needs another 100.
"""

from __future__ import annotations

import json

import chess

from engine import HERE, ROOT


def piece_count(fen: str) -> int:
    return sum(ch.isalpha() for ch in fen.split()[0])


def phase_of(pos: dict) -> str | None:
    pieces = piece_count(pos["selected_fen"])
    if 6 <= pieces <= 14:
        return "endgame"
    if pos["ply_index"] <= 16 and pieces >= 28:
        return "opening"
    if pos["ply_index"] >= 20 and pieces >= 16:
        return "middlegame"
    return None


def usable(fen: str) -> bool:
    board = chess.Board(fen)
    return board.is_valid() and not board.is_game_over(claim_draw=True) and piece_count(fen) > 5


def spread(items: list[dict], n: int) -> list[dict]:
    """Evenly spaced picks from a deterministic ordering."""
    if len(items) < n:
        raise SystemExit(f"need {n} positions, only {len(items)} available")
    step = len(items) / n
    return [items[int(i * step)] for i in range(n)]


def main() -> None:
    data = json.loads((ROOT / "data" / "bench_positions.json").read_text())
    seen: set[str] = set()
    pool: dict[str, list[dict]] = {"opening": [], "middlegame": [], "endgame": []}
    for pos in sorted(data["positions"], key=lambda p: p["id"]):
        fen = pos["selected_fen"]
        board_key = " ".join(fen.split()[:4])
        phase = phase_of(pos)
        if phase is None or board_key in seen or not usable(fen):
            continue
        seen.add(board_key)
        pool[phase].append(pos)

    speed = (
        [("opening", p) for p in spread(pool["opening"], 14)]
        + [("middlegame", p) for p in spread(pool["middlegame"], 16)]
        + [("endgame", p) for p in spread(pool["endgame"], 10)]
    )
    speed_ids = {p["id"] for _, p in speed}

    lines = ["# id\tphase\tFEN   (40 speed positions, from data/bench_positions.json)"]
    for i, (phase, pos) in enumerate(speed, 1):
        lines.append(f"P{i:02d}\t{phase}\t{pos['selected_fen']}")
    (HERE / "positions.txt").write_text("\n".join(lines) + "\n")

    # Openings: early positions (ply 8-24, >= 26 pieces) with distinct opening names,
    # so the match set covers as many different structures as possible.
    candidates = [
        p for p in sorted(data["positions"], key=lambda p: p["id"])
        if p["id"] not in speed_ids and 8 <= p["ply_index"] <= 24
        and piece_count(p["selected_fen"]) >= 26 and usable(p["selected_fen"])
    ]
    by_name: dict[str, dict] = {}
    for pos in candidates:
        by_name.setdefault(pos["opening_name"].split(":")[0], pos)
    distinct = list(by_name.values())
    rest = [p for p in candidates if p not in distinct]
    openings = spread(distinct, min(100, len(distinct)))
    if len(openings) < 100:
        openings += spread(rest, 100 - len(openings))
    lines = ["# id\tply\tFEN   (100 balanced match openings; O001-O050 standard, O051-O100 extension)"]
    for i, pos in enumerate(openings, 1):
        lines.append(f"O{i:03d}\tply{pos['ply_index']}\t{pos['selected_fen']}")
    (HERE / "openings.txt").write_text("\n".join(lines) + "\n")

    print(f"pool sizes: { {k: len(v) for k, v in pool.items()} }; "
          f"opening families: {len(distinct)}")
    print("wrote positions.txt (40) and openings.txt (100)")


if __name__ == "__main__":
    main()
