# SebLauge Folder

This folder vendors SebLague's `Chess-Coding-Adventure` UCI engine and wraps it in the local `chess-bench-v1` protocol used by the bench UI.

Files:

- `bot.json`: manifest read by the benchmark UI
- `run.sh`: launches the local bench wrapper
- `bench_bridge.py`: translates `ping/newgame/go` into UCI
- `build_snapshot.sh`: publishes the C# engine into `published/`
- `upstream/`: vendored copy of `SebLague/Chess-Coding-Adventure`
- `published/`: generated .NET publish output used by the bench app

Notes:

- The upstream project is retargeted from `net6.0` to `net8.0` so it can build with the local SDK path used here.
- `build_snapshot.sh` expects a local dotnet binary at `/private/tmp/dotnet8/dotnet` by default.
- The bench wrapper currently uses the bench UI's `ms/move` time control and ignores history FENs because the local protocol does not provide move history in UCI form.
