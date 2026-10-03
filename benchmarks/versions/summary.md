## Speed (time to depth)

40 positions, 5 measured passes, depths [4, 5, 6]. Totals are the sum over all positions within one pass; the table shows the median pass. completed_depth != requested: 0.


### Depth 4

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v1_baseline | 3.84 s | 2% | - | - | 1.00x | 1.00x | - |
| v2_tablebase | 3.11 s | 1% | 1.23x | 1.20x | 1.23x | 1.20x | 0/40 |
| v3_engine | 543 ms | 1% | 5.74x | 5.13x | 7.08x | 6.16x | 0/40 |
| v4_LMR | 458 ms | 1% | 1.19x | 1.17x | 8.40x | 7.19x | 4/40 |
| v4.1_LMR | 348 ms | 1% | 1.32x | 1.25x | 11.06x | 9.02x | 7/40 |
| v4.2_null | 235 ms | 1% | 1.48x | 1.47x | 16.37x | 13.28x | 0/40 |
| v4.3-counter | 232 ms | 2% | 1.01x | 1.03x | 16.58x | 13.62x | 6/40 |
| v5-engine | 234 ms | 4% | 0.99x | 0.93x | 16.43x | 12.70x | 8/40 |
| v6-ordering | 206 ms | 4% | 1.14x | 1.12x | 18.65x | 14.29x | 4/40 |
| v6.1-stack | 178 ms | 2% | 1.16x | 1.15x | 21.64x | 16.45x | 0/40 |
| v7 | 167 ms | 2% | 1.07x | 1.06x | 23.07x | 17.42x | 22/40 |
| v7-nnue | 167 ms | 3% | 1.00x | 1.00x | 22.97x | 17.42x | 0/40 |
| v7.2-nnue | 196 ms | 1% | 0.85x | 0.87x | 19.62x | 15.11x | 23/40 |

### Depth 5

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v1_baseline | 18.05 s | 1% | - | - | 1.00x | 1.00x | - |
| v2_tablebase | 14.63 s | 1% | 1.23x | 1.23x | 1.23x | 1.23x | 2/40 |
| v3_engine | 2.65 s | 1% | 5.53x | 4.95x | 6.82x | 6.07x | 0/40 |
| v4_LMR | 1.53 s | 0% | 1.73x | 1.63x | 11.77x | 9.91x | 6/40 |
| v4.1_LMR | 880 ms | 1% | 1.74x | 1.66x | 20.52x | 16.45x | 8/40 |
| v4.2_null | 652 ms | 2% | 1.35x | 1.35x | 27.68x | 22.17x | 5/40 |
| v4.3-counter | 605 ms | 1% | 1.08x | 1.07x | 29.84x | 23.74x | 4/40 |
| v5-engine | 522 ms | 1% | 1.16x | 1.11x | 34.60x | 26.40x | 6/40 |
| v6-ordering | 449 ms | 1% | 1.16x | 1.16x | 40.17x | 30.66x | 1/40 |
| v6.1-stack | 373 ms | 1% | 1.21x | 1.20x | 48.44x | 36.84x | 0/40 |
| v7 | 327 ms | 1% | 1.14x | 1.09x | 55.13x | 40.05x | 18/40 |
| v7-nnue | 327 ms | 1% | 1.00x | 1.00x | 55.27x | 40.07x | 0/40 |
| v7.2-nnue | 329 ms | 1% | 0.99x | 0.97x | 54.79x | 38.95x | 20/40 |

### Depth 6

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v1_baseline | 78.99 s | 1% | - | - | 1.00x | 1.00x | - |
| v2_tablebase | 62.47 s | 2% | 1.26x | 1.28x | 1.26x | 1.28x | 1/40 |
| v3_engine | 10.92 s | 1% | 5.72x | 5.13x | 7.24x | 6.55x | 0/40 |
| v4_LMR | 6.29 s | 1% | 1.73x | 1.66x | 12.55x | 10.85x | 5/40 |
| v4.1_LMR | 3.18 s | 1% | 1.98x | 1.80x | 24.85x | 19.50x | 13/40 |
| v4.2_null | 1.86 s | 2% | 1.70x | 1.68x | 42.37x | 32.78x | 6/40 |
| v4.3-counter | 1.68 s | 2% | 1.11x | 1.06x | 47.02x | 34.86x | 5/40 |
| v5-engine | 1.34 s | 0% | 1.26x | 1.25x | 59.13x | 43.44x | 8/40 |
| v6-ordering | 1.15 s | 1% | 1.17x | 1.17x | 68.91x | 50.63x | 7/40 |
| v6.1-stack | 940 ms | 1% | 1.22x | 1.23x | 84.01x | 62.38x | 0/40 |
| v7 | 846 ms | 2% | 1.11x | 1.05x | 93.38x | 65.62x | 20/40 |
| v7-nnue | 845 ms | 1% | 1.00x | 1.00x | 93.48x | 65.55x | 0/40 |
| v7.2-nnue | 1.22 s | 1% | 0.69x | 0.75x | 64.51x | 49.26x | 25/40 |

### Best-move changes vs predecessor

- depth 4: median change rate 10%; flagged (>= 2x median and >= 25%): v7 (55%), v7.2-nnue (57%)
- depth 5: median change rate 11%; flagged (>= 2x median and >= 25%): v7 (45%), v7.2-nnue (50%)
- depth 6: median change rate 14%; flagged (>= 2x median and >= 25%): v4.1_LMR (32%), v7 (50%), v7.2-nnue (62%)
- non-deterministic best moves across passes: 0

### Depth 6 speedup vs predecessor by phase (geomean per position)

| version | opening | middlegame | endgame |
|---|---:|---:|---:|
| v2_tablebase | 1.35x | 1.23x | 1.25x |
| v3_engine | 5.12x | 5.67x | 4.39x |
| v4_LMR | 1.34x | 1.91x | 1.77x |
| v4.1_LMR | 2.36x | 1.78x | 1.24x |
| v4.2_null | 1.68x | 1.78x | 1.54x |
| v4.3-counter | 1.08x | 1.13x | 0.94x |
| v5-engine | 1.12x | 1.28x | 1.38x |
| v6-ordering | 1.12x | 1.17x | 1.23x |
| v6.1-stack | 1.20x | 1.22x | 1.30x |
| v7 | 1.26x | 1.16x | 0.69x |
| v7-nnue | 1.00x | 1.00x | 1.00x |
| v7.2-nnue | 0.80x | 0.61x | 0.96x |

### Thermal / drift check: depth-6 total per pass

| version | pass 1 | pass 2 | pass 3 | pass 4 | pass 5 |
|---|---:|---:|---:|---:|---:|
| v1_baseline | 78.30 s | 78.99 s | 79.06 s | 78.96 s | 79.17 s |
| v2_tablebase | 62.45 s | 62.47 s | 62.50 s | 61.55 s | 62.49 s |
| v3_engine | 10.92 s | 10.94 s | 10.88 s | 10.84 s | 10.96 s |
| v4_LMR | 6.29 s | 6.31 s | 6.29 s | 6.27 s | 6.29 s |
| v4.1_LMR | 3.17 s | 3.20 s | 3.18 s | 3.18 s | 3.18 s |
| v4.2_null | 1.86 s | 1.90 s | 1.86 s | 1.87 s | 1.86 s |
| v4.3-counter | 1.68 s | 1.70 s | 1.68 s | 1.68 s | 1.68 s |
| v5-engine | 1.34 s | 1.33 s | 1.34 s | 1.34 s | 1.33 s |
| v6-ordering | 1.14 s | 1.15 s | 1.15 s | 1.15 s | 1.15 s |
| v6.1-stack | 940 ms | 940 ms | 941 ms | 945 ms | 939 ms |
| v7 | 845 ms | 846 ms | 838 ms | 852 ms | 854 ms |
| v7-nnue | 851 ms | 843 ms | 845 ms | 842 ms | 845 ms |
| v7.2-nnue | 1.21 s | 1.22 s | 1.22 s | 1.23 s | 1.23 s |
