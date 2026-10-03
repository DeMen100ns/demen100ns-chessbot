"""Part B: head-to-head matches between frozen versions.

Each game starts both engines' `newgame`, then alternates `go <depth> <ms> <FEN> <history...>`
where history is every FEN since the opening position, including the current one (the same
convention the lichess integration uses). python-chess adjudicates checkmate, stalemate,
threefold repetition, the 50-move rule and insufficient material. An illegal move, crash,
protocol error or timeout loses the game for the engine that caused it and is logged.

Every opening is played twice with colours swapped. After the standard 100 games
(openings O001-O050), a pair whose Elo estimate is within +-30 plays another 100 on the
extension openings O051-O100, so no game is a replay of an earlier one.

Results are appended to matches.csv and games are skipped if already present, so an
interrupted run can simply be restarted with the same command.
"""

from __future__ import annotations

import argparse
import csv
import math
import platform
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import chess
import chess.pgn

from engine import HERE, VERSIONS, Engine, EngineError, read_positions

FIELDS = ["pair", "new", "old", "game", "opening", "white", "black", "result", "new_score",
          "termination", "fault_engine", "detail", "plies", "max_think_ms", "timestamp"]

# Versions left out of matches: byte-identical to v7, so v7.2 is compared with v7 directly.
SKIP = {"v7-nnue"}


def sequential_pairs() -> list[tuple[str, str]]:
    chain = [v for v in VERSIONS if v not in SKIP]
    return [(chain[i], chain[i - 1]) for i in range(1, len(chain))]


def vs_v1_pairs() -> list[tuple[str, str]]:
    chain = [v for v in VERSIONS if v not in SKIP]
    return [(v, chain[0]) for v in chain[2:]]  # v2 vs v1 is already a sequential pair


def pair_name(new: str, old: str) -> str:
    return f"{new} vs {old}"


def elo(score: float) -> float:
    if score <= 0.0:
        return -math.inf
    if score >= 1.0:
        return math.inf
    return -400.0 * math.log10(1.0 / score - 1.0)


def elo_summary(scores: list[float]) -> dict:
    n = len(scores)
    mean = sum(scores) / n
    sd = math.sqrt(sum((s - mean) ** 2 for s in scores) / (n - 1)) if n > 1 else 0.0
    half = 1.96 * sd / math.sqrt(n)
    lo, hi = max(mean - half, 0.0), min(mean + half, 1.0)
    return {"n": n, "score": mean, "elo": elo(mean), "elo_lo": elo(lo), "elo_hi": elo(hi),
            "w": scores.count(1.0), "d": scores.count(0.5), "l": scores.count(0.0)}


@dataclass
class GameSpec:
    new: str
    old: str
    game: int          # 0-based game index inside the pair
    opening_id: str
    fen: str

    @property
    def new_is_white(self) -> bool:
        return self.game % 2 == 0


def game_specs(new: str, old: str, openings: list[tuple[str, str, str]], start: int, count: int) -> list[GameSpec]:
    specs = []
    for g in range(start, start + count):
        opening_id, _, fen = openings[g // 2]
        specs.append(GameSpec(new, old, g, opening_id, fen))
    return specs


class EnginePool:
    """One persistent engine per (worker thread, version); restarted after any failure."""

    def __init__(self) -> None:
        self.local = threading.local()
        self.all: list[Engine] = []
        self.lock = threading.Lock()

    def get(self, version: str) -> Engine:
        engines = self.local.__dict__.setdefault("engines", {})
        eng = engines.get(version)
        if eng is None or eng.proc.poll() is not None:
            eng = Engine(version)
            with self.lock:
                self.all.append(eng)
            eng.ping()
            engines[version] = eng
        return eng

    def discard(self, version: str) -> None:
        engines = self.local.__dict__.setdefault("engines", {})
        eng = engines.pop(version, None)
        if eng is not None:
            eng.proc.kill()
            eng.proc.wait()

    def close_all(self) -> None:
        for eng in self.all:
            eng.close()


def play_game(spec: GameSpec, pool: EnginePool, move_ms: int, depth: int, grace_ms: int) -> tuple[dict, chess.pgn.Game]:
    board = chess.Board(spec.fen)
    white, black = (spec.new, spec.old) if spec.new_is_white else (spec.old, spec.new)
    history = [board.fen()]
    termination, fault, detail, result = "", "", "", "*"
    max_think = 0.0

    for version in (white, black):
        try:
            pool.get(version).newgame()
        except EngineError as exc:
            pool.discard(version)
            termination, fault, detail = exc.kind, version, str(exc)
            result = "0-1" if version == white else "1-0"
            break

    while not termination:
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            termination = outcome.termination.name.lower()
            result = outcome.result()
            break
        mover = white if board.turn == chess.WHITE else black
        try:
            res = pool.get(mover).go(depth, move_ms, board.fen(), history,
                                     timeout_s=(move_ms + grace_ms) / 1000)
        except EngineError as exc:
            pool.discard(mover)
            termination, fault, detail = exc.kind, mover, str(exc)
        else:
            max_think = max(max_think, res.elapsed_s)
            try:
                move = chess.Move.from_uci(res.bestmove)
            except ValueError:
                move = None
            if move is None or move not in board.legal_moves:
                termination, fault = "illegal", mover
                detail = f"{mover} played '{res.bestmove}' in {board.fen()}"
            else:
                board.push(move)
                history.append(board.fen())
        if fault:
            result = "0-1" if board.turn == chess.WHITE else "1-0"

    new_score = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[result]
    if not spec.new_is_white:
        new_score = 1.0 - new_score

    pgn = chess.pgn.Game.from_board(board)
    pgn.headers.update({
        "Event": pair_name(spec.new, spec.old), "Round": str(spec.game + 1),
        "White": white, "Black": black, "Result": result, "FEN": spec.fen, "SetUp": "1",
        "Opening": spec.opening_id, "Termination": termination,
        "TimeControl": f"{move_ms}ms/move",
    })
    row = {
        "pair": pair_name(spec.new, spec.old), "new": spec.new, "old": spec.old,
        "game": spec.game, "opening": spec.opening_id, "white": white, "black": black,
        "result": result, "new_score": new_score, "termination": termination,
        "fault_engine": fault, "detail": detail, "plies": board.ply() - chess.Board(spec.fen).ply(),
        "max_think_ms": f"{max_think * 1000:.0f}", "timestamp": datetime.now().isoformat(timespec="seconds"),
    }
    return row, pgn


def load_done(path: Path) -> dict[tuple[str, int], dict]:
    if not path.exists():
        return {}
    with open(path, newline="") as fh:
        return {(r["pair"], int(r["game"])): r for r in csv.DictReader(fh)}


def on_ac_power() -> bool:
    out = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True).stdout
    return "AC Power" in out


def run_batch(specs: list[GameSpec], done: dict, args, writer, csv_fh, pgn_fh, lock) -> None:
    todo = [s for s in specs if (pair_name(s.new, s.old), s.game) not in done]
    if not todo:
        return
    pool = EnginePool()

    def worker(spec: GameSpec):
        return play_game(spec, pool, args.move_ms, args.depth, args.grace_ms)

    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        futures = [ex.submit(worker, s) for s in todo]
        for fut in as_completed(futures):
            row, pgn = fut.result()
            with lock:
                writer.writerow(row)
                csv_fh.flush()
                print(pgn, file=pgn_fh, end="\n\n")
                pgn_fh.flush()
                done[(row["pair"], int(row["game"]))] = row
                if row["fault_engine"]:
                    print(f"  FAULT {row['pair']} game {row['game']}: {row['detail']}", flush=True)
    pool.close_all()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pairs", default="sequential",
                    help="sequential | vs-v1 | all | comma list like 'v3_engine:v2_tablebase'")
    ap.add_argument("--move-ms", type=int, default=100)
    ap.add_argument("--depth", type=int, default=64, help="max depth sent with go (lichess default)")
    ap.add_argument("--grace-ms", type=int, default=2000, help="extra wall time before a move is a timeout")
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--extend-games", type=int, default=100)
    ap.add_argument("--extend-elo", type=float, default=30.0)
    ap.add_argument("--concurrency", type=int, default=2,
                    help="parallel games; default 2 = one game per two of the M2's 4 P-cores")
    ap.add_argument("--out", default=str(HERE / "matches.csv"))
    ap.add_argument("--pgn", default=str(HERE / "matches.pgn"))
    ap.add_argument("--allow-battery", action="store_true")
    args = ap.parse_args()

    if platform.system() == "Darwin" and not on_ac_power() and not args.allow_battery:
        sys.exit("Not on AC power. Plug the laptop in (or pass --allow-battery).")

    if args.pairs == "sequential":
        pairs = sequential_pairs()
    elif args.pairs == "vs-v1":
        pairs = vs_v1_pairs()
    elif args.pairs == "all":
        pairs = sequential_pairs() + vs_v1_pairs()
    else:
        pairs = [tuple(p.split(":")) for p in args.pairs.split(",")]

    openings = read_positions(HERE / "openings.txt")
    if args.games + args.extend_games > 2 * len(openings):
        sys.exit("not enough openings for the requested number of games")

    out = Path(args.out)
    done = load_done(out)
    new_file = not out.exists()
    lock = threading.Lock()
    with open(out, "a", newline="") as csv_fh, open(args.pgn, "a") as pgn_fh:
        writer = csv.DictWriter(csv_fh, fieldnames=FIELDS)
        if new_file:
            writer.writeheader()
        for new, old in pairs:
            name = pair_name(new, old)
            t0 = time.monotonic()
            run_batch(game_specs(new, old, openings, 0, args.games), done, args, writer, csv_fh, pgn_fh, lock)
            first = elo_summary([float(done[(name, g)]["new_score"]) for g in range(args.games)])
            total = first
            if abs(first["elo"]) <= args.extend_elo and args.extend_games > 0:
                print(f"{name}: {first['elo']:+.0f} Elo after {args.games} games -> extending", flush=True)
                run_batch(game_specs(new, old, openings, args.games, args.extend_games),
                          done, args, writer, csv_fh, pgn_fh, lock)
                n = args.games + args.extend_games
                total = elo_summary([float(done[(name, g)]["new_score"]) for g in range(n)])
            print(f"{name}: +{total['w']} ={total['d']} -{total['l']}  score {total['score']:.3f}  "
                  f"Elo {total['elo']:+.0f} [{total['elo_lo']:+.0f}, {total['elo_hi']:+.0f}]  "
                  f"({time.monotonic() - t0:.0f}s)", flush=True)


if __name__ == "__main__":
    main()
