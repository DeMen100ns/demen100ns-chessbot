# V5 Engine Folder

This folder is a separate `chess-bench-v1` bot slot for the latest engine snapshot after `v4.3-counter`.

Files:

- `bot.json`: manifest read by the benchmark UI
- `v5-engine`: executable snapshot used by the bench app
- `run.sh`: launches the frozen `v5-engine` binary
- `build_snapshot.sh`: rebuilds the `Release` bridge binary, then copies it into `v5-engine`
- local Syzygy 3-4-5 tablebases are expected at `data/tablebases/3-4-5`
- native Fathom probing is compiled from `third_party/Fathom-upstream`

Suggested workflow:

1. Keep `bots/v4.3-counter/` as the frozen v4.3 baseline.
2. Change code in `src/` for the next engine iteration.
3. Run `./build_snapshot.sh` inside this folder to build `build-release/chess_engine_bridge` in `Release` mode and capture that binary into `v5-engine`.
4. Bench `bots/v4.3-counter` vs `bots/v5-engine`.

`run.sh` never auto-builds. The bench app always runs the last built snapshot already stored in this folder.

`v5-engine` exports env vars so the bridge probes the local offline Syzygy tablebase through native Fathom.
It currently uses:

- `data/tablebases/3-4-5`
- `max_pieces = 5`
