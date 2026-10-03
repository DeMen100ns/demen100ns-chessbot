#!/bin/zsh
# Rerun the frozen-version benchmark. Plug the laptop in and close heavy apps first.
#   ./run_all.sh             Part A (speed, ~20 min) + evaluator check (~2 min)
#   ./run_all.sh --matches   also Part B: sequential pairs v2 vs v1, v3 vs v2, ... (~2-2.5 h)
#   ./run_all.sh --vs-v1     also Part B vs-v1 pairs on top of --matches (~3 h more)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

if [[ ! -x .venv/bin/python ]]; then
  python3 -m venv .venv
  .venv/bin/pip install -q 'chess==1.11.2'
fi
PY=.venv/bin/python

$PY select_positions.py                       # positions.txt + openings.txt (deterministic)
$PY speed.py 2>&1 | tee speed.log             # Part A -> speed.csv
$PY speed.py --versions v6.1-stack,v7-new --out speed_v61_v7.csv   # new v7 vs v6.1 (~20 s)
$PY analyze.py --speed speed_v61_v7.csv --matches /dev/null --out summary_v61_v7.md > /dev/null
$PY eval_check.py                             # which evaluator each version uses -> eval_check.csv
$PY tools/v7_nnue_check.py                    # v7's embedded 768-input NNUE reproduces its evals

# Part B (not run for REPORT.md). matches.csv / matches.pgn are appended to and finished
# games are skipped, so an interrupted run resumes. Delete both files to start from scratch.
if [[ "${1:-}" == "--matches" || "${1:-}" == "--vs-v1" ]]; then
  $PY match.py --pairs sequential 2>&1 | tee -a matches.log
fi
if [[ "${1:-}" == "--vs-v1" ]]; then
  $PY match.py --pairs vs-v1 2>&1 | tee -a matches.log
fi

$PY analyze.py > /dev/null                    # summary.md (tables used in REPORT.md)
echo "done: see summary.md"
