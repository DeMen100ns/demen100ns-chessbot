# V7.2 NNUE

Frozen snapshot of the local C++ engine with NNUE kept available alongside the
default `Minimax::evaluate()` path.

- `Minimax::evaluate()` uses the pre-NNUE handcrafted evaluator
- `Minimax::evaluate_nnue()` runs the NNUE-backed evaluator
- NNUE architecture supports HalfKAv2 input features:
  `64 king squares x 64 piece squares x 11 piece/ourness kinds` per perspective
- default network body is two separate perspective subnets:
  `2 x (SparseLinear(45056 -> 256) -> ClippedReLU) -> concat[512]`
  `-> Linear(512 -> 32) -> ClippedReLU -> Linear(32 -> 32) -> ClippedReLU -> Linear(32 -> 1)`
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
