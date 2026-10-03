"""Ponder lifecycle, process responsiveness, and move-submission ordering."""
import os
import time
from datetime import timedelta
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import chess
import chess.engine
import pytest

from homemade import CppBridge
from lib.engine_wrapper import EngineWrapper
from test_bot.test_homemade_bridge import fake_bridge


def predicted_game():
    board = chess.Board()
    result = chess.engine.PlayResult(chess.Move.from_uci("e2e4"), chess.Move.from_uci("c7c5"))
    return board, result


def start_fake_ponder():
    bridge = fake_bridge()
    bridge.supports_ponder = True
    bridge._send_bridge_command.return_value = ["ready\tpondering"]
    board, result = predicted_game()
    bridge.on_move_sent(board, result, True)
    return bridge, board, result


def test_prediction_is_returned_but_search_does_not_start_ponder() -> None:
    bridge = fake_bridge()
    bridge.supports_ponder = True
    bridge._send_bridge_command.return_value = ["info\tponder_move=c7c5", "bestmove\te2e4"]
    result = bridge.search(chess.Board(), chess.engine.Limit(white_clock=30), True, False, None)
    assert result.ponder == chess.Move.from_uci("c7c5")
    assert bridge.ponder_history is None
    assert bridge._send_bridge_command.call_count == 1
    assert bridge._send_bridge_command.call_args.args[0].startswith("go_clock\t")


def test_predicted_reply_uses_ponderhit() -> None:
    bridge, board, result = start_fake_ponder()
    assert bridge._send_bridge_command.call_args.args[0].startswith("go_ponder\t")
    board.push(result.move)
    bridge.on_game_state(board, True)  # Acknowledgement of our own move.
    assert bridge.ponder_history is not None
    board.push(result.ponder)
    bridge.on_game_state(board, True)
    bridge._send_bridge_command.return_value = ["bestmove\tg1f3"]
    reply = bridge.search(board, chess.engine.Limit(white_clock=30), True, False, None)
    assert bridge._send_bridge_command.call_args.args[0].startswith("ponderhit\t")
    assert reply.move in board.legal_moves
    assert bridge.ponder_history is None


@pytest.mark.parametrize("change", ["miss", "takeback", "game_over", "same_fen_different_history"])
def test_state_change_cancels_ponder(change: str) -> None:
    bridge, board, result = start_fake_ponder()
    if change != "takeback":
        board.push(result.move)
    if change == "miss":
        board.push_uci("e7e5")
    if change == "same_fen_different_history":
        board.push(result.ponder)
        board = board.copy(stack=False)
    bridge._send_bridge_command.return_value = ["ready\tstopped"]
    bridge.on_game_state(board, change != "game_over")
    assert bridge._send_bridge_command.call_args.args[0] == "stop"
    assert bridge.ponder_history is None


def test_search_miss_stops_then_searches_actual_board() -> None:
    bridge, board, result = start_fake_ponder()
    board.push(result.move)
    board.push_uci("e7e5")
    bridge._send_bridge_command.reset_mock()
    bridge._send_bridge_command.side_effect = [["ready\tstopped"], ["bestmove\tg1f3"]]
    reply = bridge.search(board, chess.engine.Limit(white_clock=30), True, False, None)
    commands = [call.args[0] for call in bridge._send_bridge_command.call_args_list]
    assert commands[0] == "stop" and commands[1].startswith("go_clock\t")
    assert board.fen() in commands[1]
    assert reply.move in board.legal_moves


@pytest.mark.parametrize("case", ["disabled", "unsupported", "no_prediction", "illegal_prediction", "zero_cap"])
def test_ponder_requires_enabled_supported_legal_prediction(case: str) -> None:
    bridge = fake_bridge()
    bridge.supports_ponder = case != "unsupported"
    board, result = predicted_game()
    if case == "no_prediction":
        result.ponder = None
    if case == "illegal_prediction":
        result.ponder = chess.Move.from_uci("a1a8")
    with patch.dict(os.environ, {"CPP_CHESS_PONDER_MAX_MS": "0" if case == "zero_cap" else "30000"}):
        bridge.on_move_sent(board, result, case != "disabled")
    bridge._send_bridge_command.assert_not_called()


@pytest.mark.parametrize("submission_fails", [False, True])
def test_ponder_begins_only_after_successful_move_submission(submission_fails: bool) -> None:
    bridge = fake_bridge()
    bridge.supports_ponder = True
    bridge.add_comment = Mock()
    bridge.print_stats = Mock()
    board, result = predicted_game()
    order = []
    li = Mock()

    def submit(*args):
        order.append("submit")
        if submission_fails:
            raise RuntimeError("simulated submission failure")

    def command(*args):
        order.append("ponder")
        return ["ready\tpondering"]

    li.make_move.side_effect = submit
    bridge._send_bridge_command.side_effect = command
    cfg = SimpleNamespace(polyglot=None, online_moves=None, draw_or_resign=None, lichess_bot_tbs=None)
    timer = Mock(time_since_reset=Mock(return_value=timedelta(seconds=1)))
    with patch("lib.engine_wrapper.get_book_move", return_value=result):
        if submission_fails:
            with pytest.raises(RuntimeError, match="submission failure"):
                EngineWrapper.play_move(bridge, board, SimpleNamespace(id="test"), li, timer,
                    timedelta(), True, False, timedelta(), cfg, timedelta())
            assert order == ["submit"]
        else:
            EngineWrapper.play_move(bridge, board, SimpleNamespace(id="test"), li, timer,
                timedelta(), True, False, timedelta(), cfg, timedelta())
            assert order == ["submit", "ponder"]


@pytest.fixture
def real_bridge(monkeypatch):
    binary = Path(__file__).resolve().parents[3] / "build-release" / "chess_engine_bridge"
    if not binary.exists():
        pytest.skip("Build chess_engine_bridge for ponder protocol tests")
    monkeypatch.setenv("CHESS_TT_BITS", "16")
    monkeypatch.setenv("CHESS_BRIDGE_ENABLE_TB", "0")
    bridge = object.__new__(CppBridge)
    bridge.binary = str(binary)
    bridge.bridge_process = None
    bridge.supports_clock = bridge.supports_ponder = True
    bridge._clear_ponder()
    assert bridge._send_bridge_command("ping") == ["pong"]
    # Warm evaluation before measuring cancellation responsiveness.
    bridge._send_bridge_command(f"go\t1\t0\t{chess.STARTING_FEN}")
    try:
        yield bridge
    finally:
        bridge.quit()


def go_ponder(bridge, fen=chess.STARTING_FEN, depth=64, cap=30000):
    assert bridge._send_bridge_command(f"go_ponder\t{depth}\t{cap}\t{fen}") == ["ready\tpondering"]


@pytest.mark.timeout(10)
def test_live_ponder_ping_stop_and_fresh_search(real_bridge) -> None:
    go_ponder(real_bridge)
    started = time.monotonic()
    assert real_bridge._send_bridge_command("ping") == ["pong"]
    assert real_bridge._send_bridge_command("stop") == ["ready\tstopped"]
    assert time.monotonic() - started < 1
    response = real_bridge._send_bridge_command(f"go\t2\t0\t{chess.STARTING_FEN}")
    assert "completed_depth=2" in response[0]
    assert chess.Move.from_uci(response[-1].split("\t")[1]) in chess.Board().legal_moves
    assert real_bridge._send_bridge_command("ponderhit\t0\t100") == ["error\tno_active_ponder"]


@pytest.mark.timeout(10)
def test_live_ponderhit_returns_legal_completed_result(real_bridge) -> None:
    go_ponder(real_bridge)
    time.sleep(0.2)
    started = time.monotonic()
    response = real_bridge._send_bridge_command("ponderhit\t50\t1000")
    assert time.monotonic() - started < 1
    assert "ponder_hit=1" in response[0]
    assert "stop_reason=ponder_ready" in response[0]
    assert "completed_depth=0\t" not in response[0]
    assert chess.Move.from_uci(response[-1].split("\t")[1]) in chess.Board().legal_moves
    assert real_bridge._send_bridge_command("ping") == ["pong"]


@pytest.mark.timeout(10)
def test_capped_ponder_reuses_finished_result(real_bridge) -> None:
    go_ponder(real_bridge, cap=20)
    time.sleep(0.1)
    started = time.monotonic()
    response = real_bridge._send_bridge_command("ponderhit\t2000\t4000")
    assert time.monotonic() - started < 1
    assert "stop_reason=ponder_limit" in response[0]
    assert "ponder_hit=1" in response[0]


@pytest.mark.timeout(10)
@pytest.mark.parametrize("command", ["newgame", "quit", "replacement", "eof"])
def test_ponder_shutdown_and_replacement(real_bridge, command: str) -> None:
    go_ponder(real_bridge)
    started = time.monotonic()
    if command == "replacement":
        board = chess.Board()
        board.push_uci("e2e4")
        response = real_bridge._send_bridge_command(f"go_clock\t64\t0\t1000\t{board.fen()}")
        assert "ponder_hit=0" in response[0]
        assert chess.Move.from_uci(response[-1].split("\t")[1]) in board.legal_moves
    elif command == "eof":
        real_bridge.bridge_process.stdin.close()
        real_bridge.bridge_process.wait(timeout=1)
    else:
        response = real_bridge._send_bridge_command(command)
        assert response == (["ready"] if command == "newgame" else ["bye"])
        if command == "newgame":
            assert real_bridge._send_bridge_command("ponderhit\t0\t100") == ["error\tno_active_ponder"]
    assert time.monotonic() - started < 1


@pytest.mark.timeout(10)
def test_real_adapter_hit_and_miss(real_bridge) -> None:
    board, result = predicted_game()
    real_bridge.on_move_sent(board, result, True)
    board.push(result.move)
    board.push(result.ponder)
    time.sleep(0.03)
    reply = real_bridge.search(board, chess.engine.Limit(white_clock=3), True, False, None)
    assert reply.move in board.legal_moves
    assert real_bridge.ponder_history is None
    # Force a different opponent reply next time and ensure the branch is discarded.
    board, result = predicted_game()
    real_bridge.on_move_sent(board, result, True)
    board.push(result.move)
    board.push_uci("e7e5")
    real_bridge.on_game_state(board, True)
    assert real_bridge.ponder_history is None
    reply = real_bridge.search(board, chess.engine.Limit(white_clock=3), True, False, None)
    assert reply.move in board.legal_moves
