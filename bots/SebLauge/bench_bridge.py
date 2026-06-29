#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


BOT_DIR = Path(__file__).resolve().parent
PUBLISH_DIR = BOT_DIR / "published"
DOTNET_BIN = Path(os.environ.get("SEBLAUGE_DOTNET", os.environ.get("DOTNET_BIN", "/private/tmp/dotnet8/dotnet")))
ENGINE_DLL = PUBLISH_DIR / "Chess-Coding-Adventure.dll"


class BridgeError(RuntimeError):
    pass


class UciEngine:
    def __init__(self) -> None:
        if not DOTNET_BIN.is_file():
            raise BridgeError(f"dotnet binary not found: {DOTNET_BIN}")
        if not ENGINE_DLL.is_file():
            raise BridgeError(f"engine snapshot not found: {ENGINE_DLL}")

        env = os.environ.copy()
        env.setdefault("DOTNET_ROOT", str(DOTNET_BIN.parent))
        self.proc = subprocess.Popen(
            [str(DOTNET_BIN), str(ENGINE_DLL)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=str(PUBLISH_DIR),
            env=env,
        )
        self._init_uci()

    def _send(self, line: str) -> None:
        if self.proc.stdin is None:
            raise BridgeError("engine stdin is closed")
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def _readline(self) -> str:
        if self.proc.stdout is None:
            raise BridgeError("engine stdout is closed")
        line = self.proc.stdout.readline()
        if line == "":
            raise BridgeError("engine process closed")
        return line.rstrip("\r\n")

    def _wait_for(self, token: str) -> None:
        while True:
            line = self._readline()
            if line == token:
                return

    def _init_uci(self) -> None:
        self._send("uci")
        self._wait_for("uciok")
        self._send("isready")
        self._wait_for("readyok")

    def new_game(self) -> None:
        self._send("ucinewgame")
        self._send("isready")
        self._wait_for("readyok")

    def search(self, depth: int, time_ms: int, fen: str) -> tuple[str, list[str]]:
        self._send(f"position fen {fen}")
        go_parts = ["go"]
        if time_ms > 0:
            go_parts.extend(["movetime", str(time_ms)])
        elif depth > 0:
            go_parts.extend(["depth", str(depth)])
        else:
            go_parts.extend(["movetime", "50"])
        self._send(" ".join(go_parts))

        info_lines: list[str] = []
        while True:
            line = self._readline()
            if line.startswith("bestmove "):
                return line.split(" ", 1)[1].strip(), info_lines
            if line:
                info_lines.append(line)

    def quit(self) -> None:
        try:
            self._send("quit")
        except Exception:
            pass
        try:
            self.proc.terminate()
        except Exception:
            pass


def main() -> int:
    engine: UciEngine | None = None

    def reset_engine() -> UciEngine:
        nonlocal engine
        if engine is not None:
            engine.quit()
        engine = UciEngine()
        return engine

    try:
        for raw_line in sys.stdin:
            line = raw_line.rstrip("\r\n")
            if not line:
                continue
            if line == "ping":
                print("pong", flush=True)
                continue
            if line == "newgame":
                try:
                    active = reset_engine()
                    active.new_game()
                    print("ready", flush=True)
                except Exception as exc:
                    print(f"error\t{exc}", flush=True)
                continue
            if line == "quit":
                break
            if line.startswith("go\t"):
                parts = line.split("\t")
                if len(parts) < 4:
                    print("error\tinvalid go command", flush=True)
                    continue
                try:
                    active = engine if engine is not None else reset_engine()
                    depth = int(parts[1])
                    time_ms = int(parts[2])
                    fen = parts[3]
                    bestmove, info_lines = active.search(depth, time_ms, fen)
                    for info_line in info_lines:
                        print(f"info\tuci:{info_line}", flush=True)
                    print(f"bestmove\t{bestmove}", flush=True)
                except Exception as exc:
                    print(f"error\t{exc}", flush=True)
                continue
            print(f"error\tunknown command: {line}", flush=True)
    finally:
        if engine is not None:
            engine.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
