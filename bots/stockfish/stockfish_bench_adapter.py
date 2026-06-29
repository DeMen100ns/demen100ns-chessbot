#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass


COMMON_STOCKFISH_PATHS = (
    "/opt/homebrew/bin/stockfish",
    "/usr/local/bin/stockfish",
    "/usr/bin/stockfish",
)


def resolve_stockfish_path() -> str:
    env_path = os.getenv("CHESS_STOCKFISH_PATH")
    if env_path:
        return env_path

    for path in COMMON_STOCKFISH_PATHS:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path

    path = shutil.which("stockfish")
    return path or "stockfish"


@dataclass
class SearchInfo:
    depth: str = ""
    score: str = ""
    nodes: str = ""
    nps: str = ""


class StockfishBenchAdapter:
    def __init__(self) -> None:
        self.binary_path = resolve_stockfish_path()
        self.process: subprocess.Popen[str] | None = None

    def start(self) -> None:
        if self.process is not None and self.process.poll() is None:
            return

        self.process = subprocess.Popen(
            [self.binary_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self._send("uci")
        self._wait_for("uciok")
        self._configure()
        self._ready()

    def stop(self) -> None:
        if self.process is None:
            return

        if self.process.poll() is None:
            try:
                self._send("quit")
                self.process.wait(timeout=2)
            except Exception:
                self.process.kill()
                self.process.wait(timeout=2)
        self.process = None

    def new_game(self) -> None:
        self._send("ucinewgame")
        self._ready()

    def search(self, fen: str, depth: int, time_limit_ms: int) -> tuple[str, SearchInfo]:
        self._send(f"position fen {fen}")
        self._send(self._go_command(depth, time_limit_ms))

        info = SearchInfo()
        while True:
            line = self._read_line()
            if line.startswith("info "):
                self._update_info(line, info)
                continue
            if line.startswith("bestmove "):
                parts = line.split()
                if len(parts) >= 2:
                    return parts[1], info
                return "0000", info

    def _configure(self) -> None:
        threads = os.getenv("STOCKFISH_THREADS")
        if threads:
            self._set_option("Threads", threads)

        hash_mb = os.getenv("STOCKFISH_HASH_MB")
        if hash_mb:
            self._set_option("Hash", hash_mb)

        elo = os.getenv("STOCKFISH_ELO")
        if elo:
            self._set_option("UCI_LimitStrength", "true")
            self._set_option("UCI_Elo", elo)
            return

        skill = os.getenv("STOCKFISH_SKILL_LEVEL")
        if skill:
            self._set_option("Skill Level", skill)

    def _go_command(self, depth: int, time_limit_ms: int) -> str:
        mode = os.getenv("STOCKFISH_BENCH_MODE", "movetime").strip().lower()
        depth_override = os.getenv("STOCKFISH_BENCH_DEPTH")
        movetime_override = os.getenv("STOCKFISH_BENCH_MOVETIME_MS")

        if mode == "depth":
            search_depth = int(depth_override) if depth_override else max(1, depth)
            return f"go depth {search_depth}"

        movetime = int(movetime_override) if movetime_override else max(1, time_limit_ms)
        return f"go movetime {movetime}"

    def _set_option(self, name: str, value: str) -> None:
        self._send(f"setoption name {name} value {value}")

    def _ready(self) -> None:
        self._send("isready")
        self._wait_for("readyok")

    def _send(self, line: str) -> None:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("Stockfish process is not running")
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def _read_line(self) -> str:
        if self.process is None or self.process.stdout is None:
            raise RuntimeError("Stockfish process is not running")
        line = self.process.stdout.readline()
        if line == "":
            raise RuntimeError("Stockfish closed unexpectedly")
        return line.strip()

    def _wait_for(self, token: str) -> None:
        while True:
            if self._read_line() == token:
                return

    @staticmethod
    def _update_info(line: str, info: SearchInfo) -> None:
        parts = line.split()
        for index, part in enumerate(parts):
            if part == "depth" and index + 1 < len(parts):
                info.depth = parts[index + 1]
            elif part == "nodes" and index + 1 < len(parts):
                info.nodes = parts[index + 1]
            elif part == "nps" and index + 1 < len(parts):
                info.nps = parts[index + 1]
            elif part == "score" and index + 2 < len(parts):
                info.score = parts[index + 1] + " " + parts[index + 2]


def print_info(info: SearchInfo, bestmove: str) -> None:
    fields = [
        "info",
        "engine=stockfish",
        f"completed_depth={info.depth or '?'}",
        f"best_move={bestmove}",
    ]
    if info.score:
        fields.append(f"score={info.score}")
    if info.nodes:
        fields.append(f"nodes={info.nodes}")
    if info.nps:
        fields.append(f"nps={info.nps}")
    print("\t".join(fields), flush=True)


def main() -> int:
    adapter = StockfishBenchAdapter()
    try:
        adapter.start()
        for raw_line in sys.stdin:
            line = raw_line.rstrip("\n")
            if line == "ping":
                print("pong", flush=True)
            elif line == "newgame":
                adapter.new_game()
                print("ready", flush=True)
            elif line == "quit":
                adapter.stop()
                return 0
            elif line.startswith("go\t"):
                fields = line.split("\t")
                if len(fields) < 4:
                    print("error\tinvalid_go_command", flush=True)
                    continue
                depth = int(fields[1])
                time_limit_ms = int(fields[2])
                fen = fields[3]
                bestmove, info = adapter.search(fen, depth, time_limit_ms)
                print_info(info, bestmove)
                print(f"bestmove\t{bestmove}", flush=True)
            else:
                print("error\tinvalid_command", flush=True)
    except Exception as exc:
        print(f"error\t{exc}", flush=True)
        return 1
    finally:
        adapter.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
