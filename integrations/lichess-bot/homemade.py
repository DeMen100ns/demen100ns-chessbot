"""
Some example classes for people who want to create a homemade bot.

With these classes, bot makers will not have to implement the UCI or XBoard interfaces themselves.
"""
import chess
from chess.engine import PlayResult, Limit
import random
import subprocess
import os
import time
from dataclasses import dataclass
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


@dataclass(frozen=True)
class MoveTimeBudget:
    soft_ms: int
    hard_ms: int
    clock_ms: int | None
    adaptive: bool = True


def compute_time_limits(board: chess.Board, time_limit: Limit) -> MoveTimeBudget:
    """Clocks already exclude lichess-bot's network/setup overhead.

    Keep a small additional clock reserve, then spread usable time over a
    30-move horizon. The constants are initial heuristics, not Elo-tuned values.
    Zero is an emergency fallback, never an unlimited search.
    """
    if isinstance(time_limit.time, (int, float)):
        fixed_ms = _seconds_to_ms(time_limit.time)
        return MoveTimeBudget(fixed_ms, fixed_ms, None, adaptive=False)

    clock_ms = _seconds_to_ms(time_limit.white_clock if board.turn else time_limit.black_clock)
    inc_ms = _seconds_to_ms(time_limit.white_inc if board.turn else time_limit.black_inc)
    reserve_ms = min(250, max(2, clock_ms // 20))
    usable_ms = max(0, clock_ms - reserve_ms)
    if usable_ms < 4:
        return MoveTimeBudget(0, 0, clock_ms)

    horizon = max(1, min(30, time_limit.remaining_moves or 30))
    base_ms = usable_ms / horizon + 0.8 * inc_ms
    soft_ms = max(1, int(min(base_ms, 0.15 * usable_ms)))
    hard_ms = max(soft_ms, int(min(4 * soft_ms, 0.35 * usable_ms)))
    return MoveTimeBudget(soft_ms, hard_ms, clock_ms)


def compute_time_budget_ms(board: chess.Board, time_limit: Limit, game: model.Game | None = None) -> int:
    """Compatibility helper returning the hard budget; cadence labels are unused."""
    return compute_time_limits(board, time_limit).hard_ms


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
        self.supports_clock = False
        self.supports_ponder = False
        self.ponder_history: tuple[str, ...] | None = None
        self.ponder_parent_history: tuple[str, ...] | None = None

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

    def _clear_ponder(self) -> None:
        self.ponder_history = None
        self.ponder_parent_history = None

    def stop_ponder(self) -> None:
        if self.ponder_history is None:
            return
        self._clear_ponder()
        if self.bridge_process is None or self.bridge_process.poll() is not None:
            return
        try:
            response = self._send_bridge_command("stop")
            if response[-1] != "ready\tstopped":
                raise RuntimeError(f"Unexpected stop response: {response!r}")
            logger.info("C++ bridge ponder stopped")
        except Exception:
            logger.exception("Could not stop ponder; closing bridge")
            self.quit()

    def on_game_state(self, board: chess.Board, active: bool) -> None:
        if self.ponder_history is None:
            return
        history = tuple(build_position_history(board)) if active else None
        # Our move acknowledgement and the predicted reply belong to this ponder.
        # Compare full histories too: the same FEN after a takeback is not a hit.
        if not active or history not in (self.ponder_parent_history, self.ponder_history):
            self.stop_ponder()

    def on_move_sent(self, board: chess.Board, result: PlayResult, can_ponder: bool) -> None:
        self.stop_ponder()
        if not can_ponder or not self.supports_ponder or result.move is None or result.ponder is None:
            return
        try:
            predicted = board.copy(stack=True)
            predicted.push(result.move)
            if predicted.is_game_over(claim_draw=True) or result.ponder not in predicted.legal_moves:
                return
            predicted.push(result.ponder)
            if predicted.is_game_over(claim_draw=True):
                return
            max_ms = max(0, int(os.getenv("CPP_CHESS_PONDER_MAX_MS", "90000")))
            if max_ms == 0:
                return
            depth = os.getenv("CPP_CHESS_ENGINE_MAX_DEPTH", os.getenv("CPP_CHESS_ENGINE_DEPTH", "64"))
            history = build_position_history(predicted)
            response = self._send_bridge_command("\t".join(
                ["go_ponder", depth, str(max_ms), predicted.fen(), *history]))
            if response[-1] != "ready\tpondering":
                raise RuntimeError(f"Unexpected ponder response: {response!r}")
            self.ponder_history = tuple(history)
            self.ponder_parent_history = tuple(history[:-1])
            logger.info("C++ bridge ponder started: predicted_move=%s max_ms=%d", result.ponder.uci(), max_ms)
        except Exception:
            # The move was already sent successfully; a ponder failure must not
            # turn it into a failed move submission or leave an orphan worker.
            logger.exception("Could not start ponder; closing bridge")
            self.quit()

    def notify(self, method_name: str, *args: object, **kwargs: object) -> None:
        if method_name in ("__exit__", "close", "quit"):
            self.quit()
        elif method_name in ("send_game_result", "stop"):
            self.stop_ponder()

    def quit(self) -> None:
        self._clear_ponder()
        if self.bridge_process is None:
            return
        process = self.bridge_process
        self.bridge_process = None
        try:
            if process.poll() is None:
                process.communicate(input="quit\n", timeout=2)
        except Exception:
            process.kill()
            process.communicate(timeout=2)

    def search(self,
               board: chess.Board,
               time_limit: Limit,
               ponder: bool,  # noqa: ARG002 (starting ponder is deferred to on_move_sent)
               draw_offered: bool,  # noqa: ARG002
               root_moves: MOVE) -> PlayResult:
        depth = os.getenv(
            "CPP_CHESS_ENGINE_MAX_DEPTH",
            os.getenv("CPP_CHESS_ENGINE_DEPTH", "64"),
        )
        started = time.monotonic()
        budget = compute_time_limits(board, time_limit)
        legal_moves = root_moves if isinstance(root_moves, list) else list(board.legal_moves)
        if not legal_moves:
            self.stop_ponder()
            return PlayResult(None, None)
        # Avoid starting a cold process for a forced move or an emergency reply.
        cold = self.bridge_process is None or self.bridge_process.poll() is not None
        if cold:
            self._clear_ponder()
        history = build_position_history(board) if self.ponder_history is not None else None
        ponder_hit = budget.adaptive and history is not None and tuple(history) == self.ponder_history
        if not ponder_hit:
            self.stop_ponder()
        if len(legal_moves) == 1 or budget.hard_ms <= (50 if cold else 2):
            self.stop_ponder()
            reason = "forced_move" if len(legal_moves) == 1 else "clock_emergency"
            logger.info("C++ bridge fallback: clock_ms=%s soft_ms=%d hard_ms=%d stop_reason=%s",
                        budget.clock_ms, budget.soft_ms, budget.hard_ms, reason)
            return PlayResult(legal_moves[0], None, info=_move_info_for_chat(board, legal_moves[0]))

        try:
            history = history if history is not None else build_position_history(board)
            # Wait for first-process initialization before assigning the remaining
            # budget. Existing benchmark binaries still work with fixed movetime.
            if cold:
                self._send_bridge_command("ping")
                capabilities = self._send_bridge_command("capabilities")[-1].split("\t")
                self.supports_clock = capabilities[0] == "ready" and "go_clock" in capabilities
                self.supports_ponder = capabilities[0] == "ready" and "ponder" in capabilities
                if budget.adaptive and not self.supports_clock:
                    logger.warning("Bridge has no go_clock support; using fixed soft budget. Rebuild to enable adaptation.")
            setup_ms = int((time.monotonic() - started) * 1000)
            hard_ms = max(0, budget.hard_ms - setup_ms)
            soft_ms = max(0, min(hard_ms, budget.soft_ms - setup_ms))
            logger.info("C++ bridge time: clock_ms=%s soft_ms=%d hard_ms=%d setup_ms=%d",
                        budget.clock_ms, soft_ms, hard_ms, setup_ms)
            if hard_ms <= 2:
                self.stop_ponder()
                logger.info("C++ bridge stop_reason=clock_emergency")
                return PlayResult(legal_moves[0], None, info=_move_info_for_chat(board, legal_moves[0]))

            if ponder_hit:
                self._clear_ponder()
                command = ["ponderhit", str(soft_ms), str(hard_ms)]
                logger.info("C++ bridge ponder hit")
            elif budget.adaptive and self.supports_clock:
                command = ["go_clock", depth, str(soft_ms), str(hard_ms), board.fen(), *history]
            else:
                fixed_ms = max(1, soft_ms) if budget.adaptive else hard_ms
                command = ["go", depth, str(fixed_ms), board.fen(), *history]
            responses = self._send_bridge_command("\t".join(command))
            uci = ""
            ponder_uci = "none"
            for response in responses:
                if response.startswith("info\t"):
                    logger.info("C++ bridge info: %s", response.removeprefix("info\t"))
                    for field in response.split("\t"):
                        if field.startswith("ponder_move="):
                            ponder_uci = field.split("=", 1)[1]
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
            logger.info("C++ bridge total_elapsed_ms=%.3f", (time.monotonic() - started) * 1000)
            predicted_reply = None
            if ponder_uci != "none":
                try:
                    candidate = chess.Move.from_uci(ponder_uci)
                    after_move = board.copy(stack=False)
                    after_move.push(move)
                    if candidate in after_move.legal_moves:
                        predicted_reply = candidate
                except ValueError:
                    logger.warning("Bridge returned invalid ponder move: %s", ponder_uci)
            return PlayResult(move, predicted_reply, info=_move_info_for_chat(board, move))
        except Exception as exc:
            self.quit()
            logger.exception("CppBridge failed, falling back to a random legal move: %s", exc)
            legal_moves = root_moves if isinstance(root_moves, list) else list(board.legal_moves)
            move = random.choice(legal_moves)
            return PlayResult(move, None, info=_move_info_for_chat(board, move))
