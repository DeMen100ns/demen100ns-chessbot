# Archive

Retired NNUE evaluator work, kept for reference and for future NNUE experiments. Nothing in
this folder is built, deployed (it is excluded from the Heroku slug) or used by the engine.

| Path | Contents |
|---|---|
| `bots/v7/` | Old frozen V7 (Jun 27). Searches with a 768-input NNUE (`2 x 6 x 64 -> 128 -> 1`) |
| `bots/v7-nnue/` | Byte-identical copy of the old V7 binary |
| `bots/v7.2-nnue/` | Old frozen V7.2 (Jun 27). Searches with a 770-input NNUE (768 + 2 side-to-move features) |
| `nnue/` | Training, data generation, export and evaluation scripts, datasets, splits and runs |
| `nnue/data_1M_static.json` | Local only, not in git (108 MB, over GitHub's file limit). Regenerate with `nnue/annotate_static_eval.cpp` |
| `engine-nnue/nnue_basic_weights.h` | Last embedded network weights (770 -> 128 -> 1) |
| `engine-nnue/remove-nnue.patch` | The change that removed NNUE from the engine (`evaluate_nnue()`, board accumulator, test checks, CMake targets) |

The archived bots still run (`./run.sh` inside each folder). Their rebuild scripts were removed,
because rebuilding from today's `src/` would overwrite them with a different engine.

## Measured facts (see `benchmarks/versions/REPORT.md`)

- Old V7 was the fastest frozen version to a fixed depth; V7.2 was 31% slower to depth 6.
- The 770-input network gives the side to move a flat bonus of about +85 cp.
- In the last NNUE-era `main` (`b1c6b89`), the search never called the network, so the code
  was removed as unused. Play was unchanged (identical best moves and evals on all 40
  benchmark positions).

## Restoring NNUE in the engine

From the repo root:

```sh
git apply -R archive/engine-nnue/remove-nnue.patch
git mv archive/engine-nnue/nnue_basic_weights.h include/chess/nnue_basic_weights.h
git mv archive/nnue nnue
```
