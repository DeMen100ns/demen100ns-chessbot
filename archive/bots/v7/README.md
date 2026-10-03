# V7 (archived)

Old frozen V7 snapshot, built Jun 27. Replaced by the current `bots/v7` (handcrafted evaluator).

- `Minimax::evaluate()` runs an embedded NNUE: `2 x 6 x 64 -> 128 -> 1`, ReLU, white-POV score
  negated for black. Verified by `benchmarks/versions/tools/v7_nnue_check.py`, which reproduces
  this binary's static evals from the weights stored inside it.
- Byte-identical to `../v7-nnue/v7-nnue`.
- Fastest frozen version to depth 4, 5 and 6 in `benchmarks/versions/REPORT.md`.

The source for this exact binary is not in git (the closest snapshot is commit `9a5e177`).

```sh
./run.sh
```
