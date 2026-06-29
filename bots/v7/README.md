# V7

Frozen snapshot of the local C++ engine with the handcrafted evaluator as the default search path and NNUE kept available separately.

- `Minimax::evaluate()` uses the pre-NNUE handcrafted evaluator
- `Minimax::evaluate_nnue()` runs the NNUE-backed evaluator
- NNUE architecture: `2 x 6 x 64 -> 128 -> 1`
- weights are embedded in `include/chess/nnue_basic_weights.h`
- output score convention remains side-to-move

Run with the local bench protocol:

```sh
./run.sh
```

Refresh the frozen binary from `build-release/chess_engine_bridge`:

```sh
./build_snapshot.sh
```
