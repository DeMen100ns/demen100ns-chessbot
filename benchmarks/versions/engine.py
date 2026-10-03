"""Shared helpers for driving the frozen bots/<version>/<version> binaries over --serve."""

from __future__ import annotations

import os
import queue
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

VERSIONS = [
    "v1_baseline", "v2_tablebase", "v3_engine", "v4_LMR", "v4.1_LMR", "v4.2_null",
    "v4.3-counter", "v5-engine", "v6-ordering", "v6.1-stack", "v7", "v7-nnue", "v7.2-nnue",
]

# v1..v5 treat time_ms <= 0 as "return the first ordered move without searching"
# (Minimax::find_best_move_timed). "No limit" is only honoured from v6-ordering on, so
# every version is given this effectively infinite budget for fixed-depth searches.
NO_LIMIT_MS = 1_000_000_000


# This benchmark measured the versions as they were frozen on Jun 27. The NNUE-era v7,
# v7-nnue and v7.2-nnue have since moved to archive/bots/; today's bots/v7 is a different
# (handcrafted) engine and is not part of these results.
ARCHIVED = {"v7", "v7-nnue", "v7.2-nnue"}

# Versions frozen after the main benchmark, under a benchmark-only name -> folder in bots/.
EXTRA = {"v7-new": "v7"}


def binary_path(version: str) -> Path:
    if version in EXTRA:
        return ROOT / "bots" / EXTRA[version] / EXTRA[version]
    folder = ROOT / "archive" / "bots" if version in ARCHIVED else ROOT / "bots"
    return folder / version / version


def engine_env() -> dict[str, str]:
    env = dict(os.environ)
    env["CHESS_BRIDGE_ENABLE_TB"] = "0"
    for key in ("CHESS_ONLINE_TB_URL", "CHESS_TB_MAX_PIECES", "CHESS_ONLINE_TB_TIMEOUT_MS"):
        env.pop(key, None)
    return env


class EngineError(Exception):
    """Raised on crash, timeout or protocol error. `kind` is crash | timeout | protocol."""

    def __init__(self, kind: str, message: str):
        super().__init__(f"{kind}: {message}")
        self.kind = kind


@dataclass
class SearchResult:
    bestmove: str
    info: dict[str, str] = field(default_factory=dict)
    elapsed_s: float = 0.0

    def int_field(self, key: str) -> int | None:
        value = self.info.get(key)
        try:
            return int(value) if value not in (None, "") else None
        except ValueError:
            return None


def parse_info(line: str) -> dict[str, str]:
    """Parse an `info\\tk=v\\t...` line.

    Format differences between versions:
      * v1 has no `debug=` field.
      * v2..v5 put `requested_depth=` inside debug, v6+ put `max_depth=`.
      * `search_eval=` only exists from v7 on.
    The debug payload is space separated and may itself contain `=`, so it is kept whole.
    """
    fields: dict[str, str] = {}
    for part in line.rstrip("\n").split("\t")[1:]:
        key, sep, value = part.partition("=")
        if sep:
            fields[key] = value
    return fields


class Engine:
    def __init__(self, version: str, binary: Path | None = None):
        self.version = version
        self.proc = subprocess.Popen(
            [str(binary or binary_path(version)), "--serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
            env=engine_env(),
        )
        self._lines: queue.Queue[str | None] = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            self._lines.put(line.rstrip("\n"))
        self._lines.put(None)

    def _send(self, command: str) -> None:
        try:
            assert self.proc.stdin is not None
            self.proc.stdin.write(command + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            raise EngineError("crash", f"{self.version} stdin closed ({exc})") from exc

    def _read_until(self, prefix: str, deadline: float) -> list[str]:
        lines = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise EngineError("timeout", f"{self.version} gave no '{prefix}' in time")
            try:
                line = self._lines.get(timeout=remaining)
            except queue.Empty:
                raise EngineError("timeout", f"{self.version} gave no '{prefix}' in time") from None
            if line is None:
                raise EngineError("crash", f"{self.version} exited (rc={self.proc.poll()})")
            lines.append(line)
            if line.startswith("error"):
                raise EngineError("protocol", f"{self.version}: {line}")
            if line.split("\t", 1)[0] == prefix:
                return lines

    def ping(self, timeout_s: float = 10.0) -> None:
        self._send("ping")
        self._read_until("pong", time.monotonic() + timeout_s)

    def newgame(self, timeout_s: float = 10.0) -> None:
        self._send("newgame")
        self._read_until("ready", time.monotonic() + timeout_s)

    def go(self, depth: int, time_ms: int, fen: str, history: list[str] | None = None,
           timeout_s: float = 600.0) -> SearchResult:
        command = "\t".join(["go", str(depth), str(time_ms), fen, *(history or [])])
        start = time.monotonic()
        self._send(command)
        lines = self._read_until("bestmove", start + timeout_s)
        elapsed = time.monotonic() - start
        info: dict[str, str] = {}
        for line in lines:
            if line.startswith("info\t"):
                info = parse_info(line)
        parts = lines[-1].split("\t")
        if len(parts) < 2 or not parts[1]:
            raise EngineError("protocol", f"{self.version}: malformed '{lines[-1]}'")
        return SearchResult(bestmove=parts[1], info=info, elapsed_s=elapsed)

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self._send("quit")
                self.proc.wait(timeout=3)
            except (EngineError, subprocess.TimeoutExpired):
                self.proc.kill()
                self.proc.wait()

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def read_positions(path: Path) -> list[tuple[str, str, str]]:
    """Return (id, phase, fen) rows from positions.txt / openings.txt."""
    rows = []
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            pos_id, phase, fen = line.split("\t")
            rows.append((pos_id, phase, fen))
    return rows
