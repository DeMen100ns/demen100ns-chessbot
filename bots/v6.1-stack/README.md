# V6.1 Stack

Frozen snapshot of the local C++ engine after the stack-allocation cleanup pass:

- fixed-capacity `MoveList` for search move generation
- stack scoring buffer in move ordering
- fixed-size killer move table
- fixed-size pawn square scratch buffer in evaluation
- Release test targets keep assertions enabled

Run with the local bench protocol:

```sh
./run.sh
```

Refresh the frozen binary from `build-release/chess_engine_bridge`:

```sh
./build_snapshot.sh
```
