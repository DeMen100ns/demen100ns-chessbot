# V6 Ordering

Frozen snapshot of the local C++ engine after the move-ordering pass that adds:

- unified depth/time search API
- SEE-banded capture ordering
- promotion/winning-capture/losing-capture priority bands
- killer, counter, and history scores added on top of base ordering score

Run with the local bench protocol:

```sh
./run.sh
```

Refresh the frozen binary from `build-release/chess_engine_bridge`:

```sh
./build_snapshot.sh
```
