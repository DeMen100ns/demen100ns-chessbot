# V3 Engine Folder

This folder is a separate `chess-bench-v1` bot slot for the current engine iteration after `v2`.

Files:

- `bot.json`: manifest read by the benchmark UI
- `v3_engine`: executable snapshot used by the bench app
- `run.sh`: launches the frozen `v3_engine` binary
- `build_snapshot.sh`: rebuilds the `Release` bridge binary, then copies it into `v3_engine`
- local Syzygy 3-4-5 tablebases are expected at `data/tablebases/3-4-5`
- native Fathom probing is compiled from `third_party/Fathom-upstream`

Suggested workflow:

1. Keep `bots/v1_baseline/` as the frozen v1 baseline.
2. Keep `bots/v2_tablebase/` as the frozen v2 baseline.
3. Change code in `src/` however you want for the next bot version.
4. Run `./build_snapshot.sh` inside this folder to build `build-release/chess_engine_bridge` in `Release` mode and capture that binary into `v3_engine`.
5. Bench `bots/v2_tablebase` vs `bots/v3_engine`.

`run.sh` never auto-builds. The bench app always runs the last built snapshot already stored in this folder.

`v3_engine` exports env vars so the bridge probes the local offline Syzygy tablebase through native Fathom.
It currently uses:

- `data/tablebases/3-4-5`
- `max_pieces = 5`
