## Speed (time to depth)

40 positions, 5 measured passes, depths [4, 5, 6]. Totals are the sum over all positions within one pass; the table shows the median pass. completed_depth != requested: 0.


### Depth 4

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v6.1-stack | 177 ms | 1% | - | - | 1.00x | 1.00x | - |
| v7-new | 118 ms | 1% | 1.50x | 1.68x | 1.50x | 1.68x | 0/40 |

### Depth 5

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v6.1-stack | 371 ms | 1% | - | - | 1.00x | 1.00x | - |
| v7-new | 331 ms | 1% | 1.12x | 1.25x | 1.12x | 1.25x | 0/40 |

### Depth 6

| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |
|---|---:|---:|---:|---:|---:|---:|---:|
| v6.1-stack | 936 ms | 0% | - | - | 1.00x | 1.00x | - |
| v7-new | 916 ms | 1% | 1.02x | 1.09x | 1.02x | 1.09x | 0/40 |

### Best-move changes vs predecessor

- depth 4: median change rate 0%; flagged (>= 2x median and >= 25%): none
- depth 5: median change rate 0%; flagged (>= 2x median and >= 25%): none
- depth 6: median change rate 0%; flagged (>= 2x median and >= 25%): none
- non-deterministic best moves across passes: 0

### Depth 6 speedup vs predecessor by phase (geomean per position)

| version | opening | middlegame | endgame |
|---|---:|---:|---:|
| v7-new | 1.01x | 1.04x | 1.30x |

### Thermal / drift check: depth-6 total per pass

| version | pass 1 | pass 2 | pass 3 | pass 4 | pass 5 |
|---|---:|---:|---:|---:|---:|
| v6.1-stack | 934 ms | 935 ms | 937 ms | 937 ms | 936 ms |
| v7-new | 912 ms | 914 ms | 917 ms | 917 ms | 916 ms |
