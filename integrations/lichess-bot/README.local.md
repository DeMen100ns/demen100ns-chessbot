# Local Bridge Notes

This vendored `lichess-bot` copy is kept separate from the C++ engine core.

## Bridge Flow

1. `lichess-bot` receives a board position from Lichess.
2. `CppBridge` in `homemade.py` calls the local C++ executable.
3. `chess_engine_bridge` prints one UCI move.
4. `lichess-bot` sends that move back to Lichess.

## Build The C++ Bridge

From the project root:

```bash
./build --target chess_engine_bridge
```

## Configure The Vendored lichess-bot

From `integrations/lichess-bot/`:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Then:

```bash
cp config.yml.default config.yml
```

Then copy the `engine:`, `challenge:`, and `matchmaking:` values you want from
`config.yml.project.example` into `config.yml`.

At minimum, make sure `config.yml` uses:

- `engine.protocol: "homemade"`
- `engine.name: "CppBridge"`
- `engine.dir: "../.."`
- `engine.working_dir: "../.."`

Then set your token plus any challenge or matchmaking preferences.

## Run

From `integrations/lichess-bot/`:

```bash
source .venv/bin/activate
python3 lichess-bot.py
```

Optional overrides:

```bash
export CPP_CHESS_ENGINE_BIN=/absolute/path/to/build-release/chess_engine_bridge
export CPP_CHESS_ENGINE_DEPTH=3
```
