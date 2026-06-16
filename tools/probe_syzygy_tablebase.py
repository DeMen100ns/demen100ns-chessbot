#!/usr/bin/env python3

import argparse
import math
import sys
from pathlib import Path

import chess
import chess.syzygy


def dtz_scorer(tablebase: chess.syzygy.Tablebase, board: chess.Board) -> int | float:
    dtz: int | float = -tablebase.probe_dtz(board)
    dtz = dtz if board.halfmove_clock or not dtz else math.copysign(0.5, dtz)
    return dtz + (math.copysign(board.halfmove_clock, dtz) if dtz else 0)


def dtz_to_wdl(dtz: float) -> int:
    if dtz <= -100:
        return -1
    if dtz < 0:
        return -2
    if dtz == 0:
        return 0
    if dtz < 100:
        return 2
    return 1


def probe_best_move(tablebase: chess.syzygy.Tablebase, board: chess.Board) -> chess.Move | None:
    scored_moves: list[tuple[chess.Move, int | float]] = []

    for move in board.legal_moves:
        board.push(move)
        try:
            dtz = dtz_scorer(tablebase, board)
        except Exception:
            board.pop()
            continue
        board.pop()
        scored_moves.append((move, dtz))

    if scored_moves:
        best_wdl = max(dtz_to_wdl(float(dtz)) for _, dtz in scored_moves)
        good_moves = [(move, dtz) for move, dtz in scored_moves if dtz_to_wdl(float(dtz)) == best_wdl]
        best_dtz = min(dtz for _, dtz in good_moves)
        best_moves = [move for move, dtz in good_moves if dtz == best_dtz]
        best_moves.sort(key=lambda move: move.uci())
        return best_moves[0]

    fallback_moves: list[tuple[chess.Move, int]] = []
    for move in board.legal_moves:
        board.push(move)
        try:
            wdl = -tablebase.probe_wdl(board)
        except Exception:
            board.pop()
            continue
        board.pop()
        fallback_moves.append((move, wdl))

    if not fallback_moves:
        return None

    best_wdl = max(wdl for _, wdl in fallback_moves)
    best_moves = [move for move, wdl in fallback_moves if wdl == best_wdl]
    best_moves.sort(key=lambda move: move.uci())
    return best_moves[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True, help="Local Syzygy directory path")
    parser.add_argument("--fen", required=True)
    parser.add_argument("--timeout-ms", type=int, default=100)
    args = parser.parse_args()

    del args.timeout_ms

    tablebase_dir = Path(args.url).expanduser()
    if not tablebase_dir.is_dir():
        return 0

    try:
        board = chess.Board(args.fen)
        with chess.syzygy.open_tablebase(str(tablebase_dir)) as tablebase:
            move = probe_best_move(tablebase, board)
    except Exception:
        return 0

    if move is not None:
        sys.stdout.write(move.uci())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
