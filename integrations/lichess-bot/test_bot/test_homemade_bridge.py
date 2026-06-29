import chess
import chess.engine
from datetime import timedelta

from homemade import compute_time_budget_ms
from lib.model import Game
from lib.lichess_types import GameEventType


def make_game(speed: str, initial_ms: int, increment_ms: int) -> Game:
    game_event: GameEventType = {
        "id": "bridgebudget",
        "variant": {"key": "standard", "name": "Standard", "short": "Std"},
        "clock": {"initial": initial_ms, "increment": increment_ms},
        "speed": speed,
        "perf": {"name": speed.title()},
        "rated": True,
        "createdAt": 1600000000000,
        "white": {"id": "whitebot", "name": "whitebot", "title": "BOT", "rating": 3000},
        "black": {"id": "blackbot", "name": "blackbot", "title": "BOT", "rating": 3000},
        "initialFen": "startpos",
        "type": "gameFull",
        "state": {
            "type": "gameState",
            "moves": "",
            "wtime": initial_ms,
            "btime": initial_ms,
            "winc": increment_ms,
            "binc": increment_ms,
            "status": "started",
        },
    }
    return Game(game_event, "whitebot", "https://lichess.org", timedelta(seconds=60))


def test_compute_time_budget_ms_uses_bullet_base() -> None:
    board = chess.Board()
    game = make_game("bullet", 60_000, 2_000)
    limit = chess.engine.Limit(white_clock=60.0, black_clock=60.0, white_inc=2.0, black_inc=2.0)
    assert compute_time_budget_ms(board, limit, game) == 2_500


def test_compute_time_budget_ms_uses_blitz_base() -> None:
    board = chess.Board()
    game = make_game("blitz", 180_000, 2_000)
    limit = chess.engine.Limit(white_clock=180.0, black_clock=180.0, white_inc=2.0, black_inc=2.0)
    assert compute_time_budget_ms(board, limit, game) == 3_000


def test_compute_time_budget_ms_uses_rapid_base() -> None:
    board = chess.Board()
    game = make_game("rapid", 600_000, 5_000)
    limit = chess.engine.Limit(white_clock=600.0, black_clock=600.0, white_inc=5.0, black_inc=5.0)
    assert compute_time_budget_ms(board, limit, game) == 8_000


def test_compute_time_budget_ms_still_keeps_reserve() -> None:
    board = chess.Board()
    game = make_game("rapid", 3_500, 0)
    limit = chess.engine.Limit(white_clock=3.5, black_clock=3.5, white_inc=0.0, black_inc=0.0)
    assert compute_time_budget_ms(board, limit, game) == 3_150
