"""Clock allocation and Python-to-C++ budget propagation."""
from unittest.mock import Mock, patch

import chess
import chess.engine
import pytest

from homemade import CppBridge, compute_time_limits


def test_budget_shrinks_with_clock() -> None:
    board = chess.Board()
    budgets = [compute_time_limits(board, chess.engine.Limit(white_clock=t))
               for t in [180, 30, 3.5, 0.1, 0.01, 0]]
    assert all(a.soft_ms > b.soft_ms for a, b in zip(budgets, budgets[1:]))
    assert budgets[2].hard_ms < 500  # The old rapid budget consumed 3 of 3.5 seconds.
    assert budgets[-1].hard_ms == 0


@pytest.mark.parametrize("clock_ms", [0, 1, 5, 10, 49, 50, 100, 300, 3500, 30000, 180000])
@pytest.mark.parametrize("inc_ms", [0, 100, 2000, 10000])
def test_budget_never_spends_future_increment(clock_ms: int, inc_ms: int) -> None:
    budget = compute_time_limits(chess.Board(), chess.engine.Limit(
        white_clock=clock_ms / 1000, white_inc=inc_ms / 1000))
    assert 0 <= budget.soft_ms <= budget.hard_ms <= clock_ms
    assert budget.hard_ms <= max(1, int(clock_ms * 0.35))
    if clock_ms <= 5:
        assert budget.hard_ms == 0


def test_increment_and_active_side() -> None:
    board = chess.Board()
    plain = compute_time_limits(board, chess.engine.Limit(white_clock=30))
    increment = compute_time_limits(board, chess.engine.Limit(white_clock=30, white_inc=1))
    assert increment.soft_ms > plain.soft_ms
    board.push_uci("e2e4")
    black = compute_time_limits(board, chess.engine.Limit(
        white_clock=1000, white_inc=60, black_clock=30, black_inc=1))
    assert black == increment


@pytest.mark.parametrize("seconds", [0, 0.001, 0.01, 1.25])
def test_explicit_movetime_is_respected(seconds: float) -> None:
    budget = compute_time_limits(chess.Board(), chess.engine.Limit(time=seconds))
    assert budget.soft_ms == budget.hard_ms == int(seconds * 1000)
    assert not budget.adaptive


def test_moves_to_go_and_missing_clock() -> None:
    board = chess.Board()
    normal = compute_time_limits(board, chess.engine.Limit(white_clock=30))
    cyclic = compute_time_limits(board, chess.engine.Limit(white_clock=30, remaining_moves=10))
    assert cyclic.soft_ms > normal.soft_ms
    assert compute_time_limits(board, chess.engine.Limit()).hard_ms == 0


def fake_bridge(cold: bool = False) -> CppBridge:
    bridge = object.__new__(CppBridge)
    bridge.bridge_process = None if cold else Mock(poll=Mock(return_value=None))
    bridge.supports_clock = True
    bridge.supports_ponder = False
    bridge._clear_ponder()
    bridge.quit = Mock()
    bridge._send_bridge_command = Mock(return_value=["bestmove\te2e4"])
    return bridge


def test_bridge_subtracts_preparation_time() -> None:
    bridge = fake_bridge()
    board = chess.Board()
    limit = chess.engine.Limit(white_clock=30)
    original = compute_time_limits(board, limit)
    with patch("homemade.time.monotonic", side_effect=[0, 1.1, 1.2]):
        result = bridge.search(board, limit, False, False, None)
    fields = bridge._send_bridge_command.call_args.args[0].split("\t")
    assert fields[0] == "go_clock"
    assert fields[2] == "0"  # Soft budget used up while preparing, hard time remains.
    assert int(fields[3]) == original.hard_ms - 1100
    assert result.move == chess.Move.from_uci("e2e4")


@pytest.mark.parametrize("cold", [True, False])
def test_emergency_never_sends_unlimited_go(cold: bool) -> None:
    bridge = fake_bridge(cold)
    board = chess.Board()
    result = bridge.search(board, chess.engine.Limit(white_clock=0.001), False, False, None)
    assert result.move in board.legal_moves
    bridge._send_bridge_command.assert_not_called()


def test_forced_move_does_not_start_process() -> None:
    bridge = fake_bridge(True)
    board = chess.Board("7k/8/5K2/8/8/8/8/7R b - - 0 1")
    result = bridge.search(board, chess.engine.Limit(black_clock=100), False, False, None)
    assert result.move == next(iter(board.legal_moves))
    bridge._send_bridge_command.assert_not_called()


def test_expired_during_setup_returns_fallback() -> None:
    bridge = fake_bridge()
    board = chess.Board()
    with patch("homemade.time.monotonic", side_effect=[0, 10]):
        result = bridge.search(board, chess.engine.Limit(white_clock=30), False, False, None)
    assert result.move in board.legal_moves
    bridge._send_bridge_command.assert_not_called()


def test_old_binary_uses_fixed_soft_budget() -> None:
    bridge = fake_bridge(True)
    bridge._send_bridge_command.side_effect = [
        ["pong"], ["error\tinvalid_command"], ["bestmove\te2e4"]]
    with patch("homemade.time.monotonic", side_effect=[0, 0, 0]):
        bridge.search(chess.Board(), chess.engine.Limit(white_clock=30), False, False, None)
    fields = bridge._send_bridge_command.call_args.args[0].split("\t")
    assert fields[0] == "go"
    assert int(fields[2]) == compute_time_limits(chess.Board(), chess.engine.Limit(white_clock=30)).soft_ms


def test_persistent_cpp_protocol() -> None:
    import os
    import subprocess
    from pathlib import Path

    binary = Path(__file__).resolve().parents[3] / "build-release" / "chess_engine_bridge"
    if not binary.exists():
        pytest.skip("Build chess_engine_bridge for the protocol integration test")
    fen = chess.STARTING_FEN
    forced = "7k/8/5K2/8/8/8/8/7R b - - 0 1"
    commands = [
        "ping", "capabilities",
        f"go_clock\t64\t0\t0\t{fen}",
        f"go_clock\t64\t0\t5000\t{fen}\t{fen}\t{fen}\t{fen}",
        f"go\t2\t0\t{fen}",
        f"go_clock\t64\t1000\t4000\t{forced}",
        f"go_clock\t64\t10\t5\t{fen}",
        f"go_clock\t64\t0\t-1\t{fen}",
        f"go_clock\t64\t0\toops\t{fen}",
        "newgame", "quit",
    ]
    env = dict(os.environ, CHESS_TT_BITS="12", CHESS_BRIDGE_ENABLE_TB="0")
    completed = subprocess.run([str(binary), "--serve"], input="\n".join(commands) + "\n",
                               capture_output=True, text=True, env=env, timeout=10, check=True)
    lines = completed.stdout.splitlines()
    assert lines[:2] == ["pong", "ready\tgo_clock\tponder"]
    info = [line for line in lines if line.startswith("info\t")]
    assert len(info) == 4
    assert "stop_reason=hard_limit" in info[0] and "completed_depth=0" in info[0]
    assert "stop_reason=soft_limit" in info[1] and "completed_depth=1" in info[1]
    assert "history_positions=3" in info[1] and "current_repetition=3" in info[1]
    assert "completed_depth=2" in info[2] and "stop_reason=max_depth" in info[2]
    assert "stop_reason=forced_move" in info[3] and "completed_depth=0" in info[3]
    moves = [line.split("\t")[1] for line in lines if line.startswith("bestmove\t")]
    for fen_value, move in zip([fen, fen, fen, forced], moves):
        assert chess.Move.from_uci(move) in chess.Board(fen_value).legal_moves
    assert len([line for line in lines if line.startswith("error\t")]) == 3
    assert lines[-2:] == ["ready", "bye"]


def test_cpp_adapter_negotiates_and_plays() -> None:
    import os
    from pathlib import Path

    binary = Path(__file__).resolve().parents[3] / "build-release" / "chess_engine_bridge"
    if not binary.exists():
        pytest.skip("Build chess_engine_bridge for the adapter integration test")
    bridge = object.__new__(CppBridge)
    bridge.binary = str(binary)
    bridge.bridge_process = None
    bridge.supports_clock = False
    bridge.supports_ponder = False
    bridge._clear_ponder()
    board = chess.Board()
    try:
        with patch.dict(os.environ, {"CHESS_TT_BITS": "12", "CHESS_BRIDGE_ENABLE_TB": "0"}):
            result = bridge.search(board, chess.engine.Limit(white_clock=3), False, False, None)
        assert result.move in board.legal_moves
        assert bridge.supports_clock
        assert bridge.bridge_process is not None
        assert bridge.bridge_process.poll() is None
        # An explicit sub-50ms request on the same process remains fixed movetime.
        board.push(result.move)
        reply = bridge.search(board, chess.engine.Limit(time=0.01), False, False, None)
        assert reply.move in board.legal_moves
    finally:
        bridge.quit()
