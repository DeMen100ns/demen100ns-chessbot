# V7

Frozen snapshot of the local C++ engine as deployed to Heroku (`main` at `b1c6b89`),
rebuilt after removing unused evaluator code:

- handcrafted `Minimax::evaluate()` (the same evaluator as v1–v6.1)
- adaptive time management: soft/hard limits, more time when the best move or eval is unstable
- pondering on the opponent's time
- draw detection inside the search (50-move rule, insufficient material)
- same best moves as v6.1-stack at depths 4-6 on the 40 benchmark positions; 1.50x faster
  to depth 4, about equal (1.02x) at depth 6 (`benchmarks/versions/REPORT.md`)

Run with the local bench protocol:

```sh
./run.sh
```

Refresh the frozen binary from `build-release/chess_engine_bridge`:

```sh
./build_snapshot.sh
```
