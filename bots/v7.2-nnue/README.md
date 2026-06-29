# V7.2 NNUE

Frozen snapshot of the local C++ engine with NNUE kept available alongside the
default `Minimax::evaluate()` path.

- `Minimax::evaluate()` uses the pre-NNUE handcrafted evaluator
- `Minimax::evaluate_nnue()` runs the NNUE-backed evaluator
- NNUE architecture supports `2 x 6 x 64` piece features plus optional
  side-to-move features when the embedded weights include them
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
