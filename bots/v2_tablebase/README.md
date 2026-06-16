# V2 Tablebase Folder

This folder is a separate `chess-bench-v1` bot slot for your tablebase-enabled reference bot.

Files:

- `bot.json`: manifest read by the benchmark UI
- `v2_tablebase`: executable snapshot used by the bench app
- `run.sh`: launches the frozen `v2_tablebase` binary
- `build_snapshot.sh`: rebuilds the `Release` bridge binary, then copies it into `v2_tablebase`
- local Syzygy 3-4-5 tablebases are expected at `data/tablebases/3-4-5`
- native Fathom probing is compiled from `third_party/Fathom-upstream`

Suggested workflow:

1. Keep `bots/v1_baseline/` as the frozen v1 baseline.
2. Change code in `src/` however you want for the next bot version.
3. Run `./build_snapshot.sh` inside this folder to build `build-release/chess_engine_bridge` in `Release` mode and capture that binary into `v2_tablebase`.
4. Bench `bots/v1_baseline` vs `bots/v2_tablebase`.

`run.sh` never auto-builds. The bench app always runs the last built snapshot already stored in this folder.

`v2_tablebase` exports env vars so the bridge probes the local offline Syzygy tablebase through native Fathom.
It currently uses:

- `data/tablebases/3-4-5`
- `max_pieces = 5`
