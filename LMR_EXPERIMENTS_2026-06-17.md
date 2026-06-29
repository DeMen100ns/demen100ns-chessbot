# LMR Constraint Experiments

Date: 2026-06-17

Scope:
- Node-only investigation for `LMR` constraints on the current engine.
- Metrics: `nodes`, `qnodes`, `leaves`, `qleaves` from `bench_time 6 6 10`.
- Wall-clock intentionally excluded from decision-making for this round.

Implementation notes:
- Added a temporary env-driven LMR experiment harness in [src/engine/minimax.cpp](/Users/demen100ns/Documents/Code%20Training/Project/Chess%20bot/src/engine/minimax.cpp).
- All variants below were run on the same binary/workload by changing only `CHESS_LMR_*` env vars.

Current default LMR behavior:
- reduce quiet non-pawn moves
- reduce bad captures (`SEE < 0`)
- do not reduce in PV nodes
- do not reduce when in check
- do not reduce killer moves
- do not reduce checking moves
- minimum depth `3`
- no late-move index guard

## Baseline Matrix

`bench_time 6 6 10`

| Variant | Env Delta | Nodes | QNodes | Leaves | QLeaves |
|---|---|---:|---:|---:|---:|
| `no_lmr` | `CHESS_LMR_ENABLE=0` | 2,394,689 | 2,672,719 | 1,948,617 | 2,150,043 |
| `current` | default | 1,556,099 | 1,899,565 | 1,194,955 | 1,404,695 |
| `late4` | `CHESS_LMR_MIN_MOVE_INDEX=3` | 1,720,085 | 2,102,102 | 1,331,080 | 1,557,002 |
| `quiet_only` | `CHESS_LMR_ALLOW_BAD_CAPTURES=0` | 1,609,443 | 1,968,355 | 1,241,700 | 1,457,302 |
| `quiet_only_late4` | `ALLOW_BAD_CAPTURES=0, MIN_MOVE_INDEX=3` | 1,726,313 | 2,108,448 | 1,336,733 | 1,562,942 |
| `allow_pawn` | `CHESS_LMR_ALLOW_QUIET_PAWN=1` | 1,145,485 | 1,429,952 | 844,210 | 1,006,965 |
| `allow_pawn_late4` | `ALLOW_QUIET_PAWN=1, MIN_MOVE_INDEX=3` | 1,332,391 | 1,740,911 | 989,496 | 1,207,665 |
| `no_killer_guard` | `CHESS_LMR_GUARD_KILLER=0` | 1,539,691 | 1,912,629 | 1,175,355 | 1,394,674 |
| `no_gives_check_guard` | `CHESS_LMR_GUARD_GIVES_CHECK=0` | 1,619,415 | 1,966,005 | 1,236,306 | 1,449,045 |
| `no_pv_guard` | `CHESS_LMR_GUARD_PV=0` | 933,224 | 1,188,747 | 683,542 | 832,903 |
| `depth4` | `CHESS_LMR_MIN_DEPTH=4` | 2,344,381 | 2,658,781 | 1,943,725 | 2,149,443 |

## Focused Follow-Up Around `allow_pawn`

| Variant | Env Delta | Nodes | QNodes | Leaves | QLeaves |
|---|---|---:|---:|---:|---:|
| `allow_pawn + no_pv` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0` | 551,947 | 704,449 | 400,913 | 483,804 |
| `allow_pawn + no_killer` | `ALLOW_QUIET_PAWN=1, GUARD_KILLER=0` | 1,019,788 | 1,319,732 | 746,636 | 914,215 |
| `allow_pawn + no_gives_check` | `ALLOW_QUIET_PAWN=1, GUARD_GIVES_CHECK=0` | 1,180,078 | 1,508,235 | 860,632 | 1,044,034 |
| `allow_pawn + depth4` | `ALLOW_QUIET_PAWN=1, MIN_DEPTH=4` | 2,074,378 | 2,390,326 | 1,736,815 | 1,927,207 |
| `allow_pawn + no_pv + no_killer` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, GUARD_KILLER=0` | 547,299 | 719,177 | 400,887 | 490,136 |
| `allow_pawn + no_pv + no_gives_check` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, GUARD_GIVES_CHECK=0` | 538,148 | 694,146 | 389,894 | 474,456 |
| `allow_pawn + no_pv + no_killer + no_gives_check` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, GUARD_KILLER=0, GUARD_GIVES_CHECK=0` | 531,377 | 683,505 | 389,704 | 470,594 |

## Frontier Around `allow_pawn + no_pv`

These variants push more aggressively on the already strong `allow_pawn + no_pv` family.

| Variant | Env Delta | Nodes | QNodes | Leaves | QLeaves |
|---|---|---:|---:|---:|---:|
| `allow_pawn + no_pv + depth2` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, MIN_DEPTH=2` | 440,882 | 534,873 | 386,383 | 426,752 |
| `allow_pawn + no_pv + reduction2` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, REDUCTION=2` | 244,422 | 358,122 | 185,686 | 235,301 |
| `allow_pawn + no_pv + quiet_only` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, ALLOW_BAD_CAPTURES=0` | 549,966 | 717,225 | 401,573 | 492,118 |
| `allow_pawn + no_pv + no_in_check` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, GUARD_IN_CHECK=0` | 548,208 | 695,828 | 398,714 | 479,461 |
| `allow_pawn + no_pv + reduction2 + depth2` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, REDUCTION=2, MIN_DEPTH=2` | 212,492 | 299,179 | 185,948 | 219,226 |
| `allow_pawn + no_pv + reduction2 + no_gives_check` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, REDUCTION=2, GUARD_GIVES_CHECK=0` | 227,038 | 337,255 | 171,902 | 219,583 |
| `allow_pawn + no_pv + reduction2 + no_killer` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, REDUCTION=2, GUARD_KILLER=0` | 219,344 | 324,746 | 168,241 | 213,315 |
| `allow_pawn + no_pv + reduction2 + no_in_check` | `ALLOW_QUIET_PAWN=1, GUARD_PV=0, REDUCTION=2, GUARD_IN_CHECK=0` | 246,932 | 359,569 | 187,475 | 237,583 |

## What The Data Says

### 1. The current default LMR is already doing real work

`current` vs `no_lmr`:
- nodes: `1,556,099` vs `2,394,689`
- delta: about `35%` fewer nodes

So LMR is definitely one of the major search-tree reducers in the current engine.

### 2. The `move_index >= 3` guard is expensive in node terms

`late4` vs `current`:
- nodes rise from `1,556,099` to `1,720,085`

So restoring a strict "only reduce from move 4 onward" guard costs a noticeable amount of pruning.

### 3. Allowing bad captures to reduce helps a bit

`quiet_only` vs `current`:
- nodes rise from `1,556,099` to `1,609,443`

That means the current `SEE < 0` capture reduction is actually earning some node reduction.

### 4. Quiet pawn reductions are the biggest single extra lever tested

`allow_pawn` vs `current`:
- nodes drop from `1,556,099` to `1,145,485`
- leaves drop from `1,194,955` to `844,210`

This is the strongest "single-condition relaxation" in the whole experiment.

### 5. The PV-node guard is the most expensive guard currently in place

`no_pv_guard` vs `current`:
- nodes drop from `1,556,099` to `933,224`

And combined with pawn reductions:
- `allow_pawn + no_pv` drops to `551,947`

This is the largest node-only gain observed from removing one existing guard.

### 6. The gives-check guard matters, but less than the PV guard

`no_gives_check_guard` vs `current`:
- nodes rise from `1,556,099` to `1,619,415`

Interpretation:
- removing the gives-check guard in the current non-pawn config does not help this workload
- but in the more aggressive `allow_pawn + no_pv` regime, removing it helps further:
  - `551,947` -> `538,148`

So this guard only starts looking "expensive" once the search is already very aggressively reduced.

### 7. The killer guard has modest effect alone, more effect in aggressive configs

`no_killer_guard` vs `current`:
- nodes: `1,539,691`

Small win by itself.

But with `allow_pawn + no_pv`:
- `551,947` -> `547,299`

Still only a small extra reduction.

### 8. Raising min depth from 3 to 4 almost turns LMR off

`depth4` vs `current`:
- nodes jump to `2,344,381`

That is nearly back to `no_lmr`.

Conclusion: `min_depth = 3` is doing important work. `4` is far too restrictive for node reduction.

### 9. Lowering `min_depth` from 3 to 2 is very powerful in the aggressive family

`allow_pawn + no_pv + depth2`:
- nodes drop from `551,947` to `440,882`

And with stronger reduction:
- `allow_pawn + no_pv + reduction2 + depth2` reaches `212,492` nodes

This is one of the strongest levers found in the whole sweep.

### 10. `reduction = 2` is the biggest single extra lever after `allow_pawn + no_pv`

`allow_pawn + no_pv + reduction2`:
- nodes: `244,422`
- qnodes: `358,122`

This is already lower than the Seb baseline node count measured earlier (`264,919` nodes).

### 11. Bad-capture reductions still help in the aggressive family

`allow_pawn + no_pv` vs `allow_pawn + no_pv + quiet_only`:
- nodes rise from `551,947` to `549,966`? effectively flat on total nodes, but qnodes/leaves worsen slightly

Interpretation:
- allowing bad captures is not the dominant lever here
- but there is no strong node-only reason to remove it

### 12. In-check guard is less important than expected in the aggressive family

`allow_pawn + no_pv + no_in_check`:
- nodes: `548,208`, slightly below `551,947`

And with `reduction2`:
- `246,932`, which is slightly worse than plain `reduction2`

Interpretation:
- dropping the in-check guard does not buy much by itself
- once `reduction=2` is active, keeping the in-check guard may actually be cleaner without sacrificing node count

## Practical Buckets

### Conservative

Good if the goal is to preserve current safeguards while trimming only obvious mistakes.

- `current`
- `quiet_only`
- `late4`

Observations:
- `quiet_only` and `late4` both reduce pruning versus current.
- neither looks attractive if node count is the only goal.

### Balanced

Good if the goal is materially fewer nodes without throwing away every safety rail.

- `allow_pawn`
- `allow_pawn_late4`
- `no_killer_guard`

Best in this bucket:
- `allow_pawn`

### Aggressive

Good if the goal is pure node minimization and tactical risk is acceptable for later validation.

- `no_pv_guard`
- `allow_pawn + no_pv`
- `allow_pawn + no_pv + no_gives_check`
- `allow_pawn + no_pv + no_killer + no_gives_check`

Best node-only result:
- `allow_pawn + no_pv + no_killer + no_gives_check`
- nodes: `531,377`

## Recommended Candidates To Consider Next

If deciding purely by node count:

1. `allow_pawn + no_pv + reduction2 + depth2`
2. `allow_pawn + no_pv + reduction2 + no_killer`
3. `allow_pawn + no_pv + reduction2 + no_gives_check`
4. `allow_pawn + no_pv + reduction2`
5. `allow_pawn + no_pv + depth2`

If deciding by lower risk while still getting a clear node win:

1. `allow_pawn`
2. `allow_pawn + no_pv`
3. `allow_pawn + no_pv + depth2`
4. `allow_pawn + no_pv + reduction2`

## Caveats

- This report intentionally ignores wall-clock time.
- Lower node count does not guarantee higher Elo.
- Removing the PV guard is especially risky conceptually, because it allows reduced searches inside PV nodes.
- Allowing quiet pawn reductions is the cleanest high-impact lever found in this sweep.
- `reduction = 2` plus `min_depth = 2` is extremely aggressive and should be treated as a frontier candidate, not a safe default.
- The env-driven experiment harness should be treated as temporary until a final policy is chosen.
