"""
Some example classes for people who want to create a homemade bot.

With these classes, bot makers will not have to implement the UCI or XBoard interfaces themselves.
"""
import chess
from chess.engine import PlayResult, Limit
import random
import subprocess
import os
from typing import List
from lib.engine_wrapper import MinimalEngine
from lib.lichess_types import MOVE, HOMEMADE_ARGS_TYPE
from lib import model
import logging


# Use this logger variable to print messages to the console or log files.
# logger.info("message") will always print "message" to the console or log file.
# logger.debug("message") will only print "message" if verbose logging is enabled.
logger = logging.getLogger(__name__)
EN_PASSANT_CHAT_MESSAGE = "en passant is forced"


def build_position_history(board: chess.Board) -> List[str]:
    """Rebuild every position in the current game so the C++ bot can track repetition."""
    replay_board = board.copy(stack=True)
    move_stack = list(replay_board.move_stack)
    while replay_board.move_stack:
        replay_board.pop()

    history = [replay_board.fen()]
    for move in move_stack:
        replay_board.push(move)
        history.append(replay_board.fen())

    return history


def _seconds_to_ms(value: object) -> int:
    if isinstance(value, (int, float)):
        return max(0, int(value * 1000))
    return 0


def _move_info_for_chat(board: chess.Board, move: chess.Move) -> dict[str, str]:
    if board.is_en_passant(move):
        return {"chat": EN_PASSANT_CHAT_MESSAGE}
    return {}


def _base_move_budget_ms(game: model.Game | None) -> int:
    if game is None:
        return 1000

    if game.speed == "bullet":
        return 500
    if game.speed == "blitz":
        return 1000
    if game.speed == "rapid":
        return 3000
    return 1000


def compute_time_budget_ms(board: chess.Board, time_limit: Limit, game: model.Game | None = None) -> int:
    """Allocate a safe per-move budget from the lichess clocks.

    For clock games we aim to spend a cadence-aware fixed budget plus the full
    increment, while still keeping a small reserve so we do not flag when the
    remaining main clock gets low.
    """
    if isinstance(time_limit.time, (int, float)):
        return max(50, _seconds_to_ms(time_limit.time))

    if board.turn == chess.WHITE:
        my_time_ms = _seconds_to_ms(time_limit.white_clock)
        my_inc_ms = _seconds_to_ms(time_limit.white_inc)
    else:
        my_time_ms = _seconds_to_ms(time_limit.black_clock)
        my_inc_ms = _seconds_to_ms(time_limit.black_inc)

    if my_time_ms <= 0:
        return 50

    reserve_ms = max(250, min(5000, my_inc_ms + 250, my_time_ms // 10))
    max_budget_ms = max(50, my_time_ms - reserve_ms)
    target_budget_ms = max(50, _base_move_budget_ms(game) + my_inc_ms)
    return min(max_budget_ms, target_budget_ms)


class ExampleEngine(MinimalEngine):
    """An example engine that all homemade engines inherit."""


# Bot names and ideas from tom7's excellent eloWorld video

class RandomMove(ExampleEngine):
    """Get a random move."""

    def search(self, board: chess.Board, *args: HOMEMADE_ARGS_TYPE) -> PlayResult:  # noqa: ARG002
        """Choose a random move."""
        return PlayResult(random.choice(list(board.legal_moves)), None)


class Alphabetical(ExampleEngine):
    """Get the first move when sorted by san representation."""

    def search(self, board: chess.Board, *args: HOMEMADE_ARGS_TYPE) -> PlayResult:  # noqa: ARG002
        """Choose the first move alphabetically."""
        moves = list(board.legal_moves)
        moves.sort(key=board.san)
        return PlayResult(moves[0], None)


class FirstMove(ExampleEngine):
    """Get the first move when sorted by uci representation."""

    def search(self, board: chess.Board, *args: HOMEMADE_ARGS_TYPE) -> PlayResult:  # noqa: ARG002
        """Choose the first move alphabetically in uci representation."""
        moves = list(board.legal_moves)
        moves.sort(key=str)
        return PlayResult(moves[0], None)


class ComboEngine(ExampleEngine):
    """
    Get a move using multiple different methods.

    This engine demonstrates how one can use `time_limit`, `draw_offered`, and `root_moves`.
    """

    def search(self,
               board: chess.Board,
               time_limit: Limit,
               ponder: bool,  # noqa: ARG002
               draw_offered: bool,
               root_moves: MOVE) -> PlayResult:
        """
        Choose a move using multiple different methods.

        :param board: The current position.
        :param time_limit: Conditions for how long the engine can search (e.g. we have 10 seconds and search up to depth 10).
        :param ponder: Whether the engine can ponder after playing a move.
        :param draw_offered: Whether the bot was offered a draw.
        :param root_moves: If it is a list, the engine should only play a move that is in `root_moves`.
        :return: The move to play.
        """
        if isinstance(time_limit.time, int):
            my_time = time_limit.time
            my_inc = 0
        elif board.turn == chess.WHITE:
            my_time = time_limit.white_clock if isinstance(time_limit.white_clock, int) else 0
            my_inc = time_limit.white_inc if isinstance(time_limit.white_inc, int) else 0
        else:
            my_time = time_limit.black_clock if isinstance(time_limit.black_clock, int) else 0
            my_inc = time_limit.black_inc if isinstance(time_limit.black_inc, int) else 0

        possible_moves = root_moves if isinstance(root_moves, list) else list(board.legal_moves)

        if my_time / 60 + my_inc > 10:
            # Choose a random move.
            move = random.choice(possible_moves)
        else:
            # Choose the first move alphabetically in uci representation.
            possible_moves.sort(key=str)
            move = possible_moves[0]
        return PlayResult(move, None, draw_offered=draw_offered)


class CppBridge(ExampleEngine):
    """Bridge lichess-bot to the local C++ engine executable."""

    def __init__(self,
                 commands: HOMEMADE_ARGS_TYPE,
                 options: object,
                 stderr: int | None,
                 draw_or_resign: object,
                 game: model.Game | None,
                 debug: bool,
                 **popen_args: str) -> None:
        super().__init__(commands, options, stderr, draw_or_resign, game, debug, **popen_args)
        self.game = game
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        release_binary = os.path.join(project_root, "build-release", "chess_engine_bridge")
        self.binary = os.getenv("CPP_CHESS_ENGINE_BIN", release_binary)
        self.bridge_process: subprocess.Popen[str] | None = None

    def _ensure_bridge_process(self) -> subprocess.Popen[str]:
        if self.bridge_process is not None and self.bridge_process.poll() is None:
            return self.bridge_process

        logger.debug("Starting persistent C++ bridge binary=%s", self.binary)
        self.bridge_process = subprocess.Popen(
            [self.binary, "--serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        return self.bridge_process

    def _send_bridge_command(self, command: str) -> list[str]:
        process = self._ensure_bridge_process()
        if process.stdin is None or process.stdout is None:
            raise RuntimeError("Bridge process is missing stdio pipes")

        process.stdin.write(command + "\n")
        process.stdin.flush()

        responses: list[str] = []
        while True:
            line = process.stdout.readline()
            if line == "":
                raise RuntimeError("Bridge process closed unexpectedly")

            line = line.rstrip("\n")
            responses.append(line)
            if line.startswith(("bestmove\t", "pong", "bye", "ready", "error\t")):
                return responses

    def ping(self) -> None:
        if self.bridge_process is None:
            return
        if self.bridge_process.poll() is not None:
            return
        responses = self._send_bridge_command("ping")
        if not responses or responses[-1] != "pong":
            raise RuntimeError(f"Unexpected bridge ping response: {responses!r}")

    def quit(self) -> None:
        if self.bridge_process is None:
            return

        process = self.bridge_process
        self.bridge_process = None
        try:
            if process.poll() is None:
                if process.stdin is not None and process.stdout is not None:
                    process.stdin.write("quit\n")
                    process.stdin.flush()
                    process.stdout.readline()
                process.wait(timeout=2)
        except Exception:
            process.kill()
            process.wait(timeout=2)

    def search(self,
               board: chess.Board,
               time_limit: Limit,
               ponder: bool,  # noqa: ARG002
               draw_offered: bool,  # noqa: ARG002
               root_moves: MOVE) -> PlayResult:
        depth = os.getenv(
            "CPP_CHESS_ENGINE_MAX_DEPTH",
            os.getenv("CPP_CHESS_ENGINE_DEPTH", "64"),
        )
        time_budget_ms = compute_time_budget_ms(board, time_limit, self.game)
        history = build_position_history(board)
        logger.debug(
            "Calling persistent C++ bridge binary=%s depth=%s time_budget_ms=%d history_positions=%d",
            self.binary,
            depth,
            time_budget_ms,
            len(history),
        )

        try:
            responses = self._send_bridge_command(
                "\t".join(["go", depth, str(time_budget_ms), board.fen(), *history])
            )
            uci = ""
            for response in responses:
                if response.startswith("info\t"):
                    logger.info("C++ bridge info: %s", response.removeprefix("info\t"))
                elif response.startswith("bestmove\t"):
                    uci = response.split("\t", 1)[1]
                elif response.startswith("error\t"):
                    raise RuntimeError(response.split("\t", 1)[1])

            if not uci:
                raise RuntimeError(f"Bridge did not return bestmove: {responses!r}")

            move = chess.Move.from_uci(uci)
            legal_moves = root_moves if isinstance(root_moves, list) else list(board.legal_moves)
            if move not in legal_moves:
                raise ValueError(f"Bridge returned illegal move: {uci}")
            return PlayResult(move, None, info=_move_info_for_chat(board, move))
        except Exception as exc:
            self.quit()
            logger.exception("CppBridge failed, falling back to a random legal move: %s", exc)
            legal_moves = root_moves if isinstance(root_moves, list) else list(board.legal_moves)
            move = random.choice(legal_moves)
            return PlayResult(move, None, info=_move_info_for_chat(board, move))
