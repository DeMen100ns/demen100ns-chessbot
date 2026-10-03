# V7.2 NNUE (archived)

Old frozen V7.2 snapshot, built Jun 27.

- `Minimax::evaluate()` runs the 770-input NNUE stored in `../../engine-nnue/nnue_basic_weights.h`:
  768 piece-square features + 2 side-to-move features, `-> 128 -> 1`. Its static evals match
  that network exactly on all 40 benchmark positions.
- The network adds about +85 cp for the side to move, which changes the search tree:
  31% slower than V7 to depth 6, different best move on 50-62% of positions.

An earlier version of this README described a HalfKAv2 network (`45056 -> 256` per side).
That network is not in this binary.

The source for this exact binary is not in git (the closest snapshot is commit `9a5e177`).

```sh
./run.sh
```
