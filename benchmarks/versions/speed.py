"""Part A: time to fixed depth for every frozen version.

For each pass, every version is started once with --serve, and for each position and
depth it gets `newgame` (clears history + TT) then `go <D> <NO_LIMIT_MS> <FEN>`.
Wall time is measured from writing `go` to reading `bestmove` with time.monotonic().

Pass 0 is the warm-up and is recorded with warmup=1 (excluded from the analysis).
Versions run v1 -> v7.2 in even passes and v7.2 -> v1 in odd passes, so thermal
throttling and background load hit every version equally.
"""

from __future__ import annotations

import argparse
import csv
import platform
import subprocess
import sys
import time

from engine import HERE, NO_LIMIT_MS, VERSIONS, Engine, read_positions

FIELDS = ["pass", "warmup", "order", "version", "position", "phase", "depth", "time_ms",
          "bestmove", "search_eval", "static_eval", "completed_depth"]


def on_ac_power() -> bool:
    out = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True).stdout
    return "AC Power" in out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--depths", default="4,5,6")
    ap.add_argument("--passes", type=int, default=5, help="measured passes (plus 1 warm-up)")
    ap.add_argument("--versions", default=",".join(VERSIONS))
    ap.add_argument("--out", default=str(HERE / "speed.csv"))
    ap.add_argument("--allow-battery", action="store_true")
    args = ap.parse_args()

    if platform.system() == "Darwin" and not on_ac_power() and not args.allow_battery:
        sys.exit("Not on AC power. Plug the laptop in (or pass --allow-battery).")

    depths = [int(d) for d in args.depths.split(",")]
    versions = args.versions.split(",")
    positions = read_positions(HERE / "positions.txt")
    mismatches = 0

    with open(args.out, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        for pass_no in range(args.passes + 1):
            order = versions if pass_no % 2 == 0 else list(reversed(versions))
            pass_start = time.monotonic()
            for slot, version in enumerate(order):
                with Engine(version) as eng:
                    eng.ping()
                    for pos_id, phase, fen in positions:
                        for depth in depths:
                            eng.newgame()
                            res = eng.go(depth, NO_LIMIT_MS, fen)
                            completed = res.int_field("completed_depth")
                            if completed != depth:
                                mismatches += 1
                                print(f"  WARNING {version} {pos_id} d{depth}: completed_depth={completed}",
                                      file=sys.stderr)
                            writer.writerow({
                                "pass": pass_no, "warmup": int(pass_no == 0), "order": slot,
                                "version": version, "position": pos_id, "phase": phase,
                                "depth": depth, "time_ms": f"{res.elapsed_s * 1000:.3f}",
                                "bestmove": res.bestmove,
                                "search_eval": res.info.get("search_eval", ""),
                                "static_eval": res.info.get("static_eval", ""),
                                "completed_depth": "" if completed is None else completed,
                            })
                fh.flush()
            label = "warm-up" if pass_no == 0 else f"pass {pass_no}/{args.passes}"
            print(f"{label} done in {time.monotonic() - pass_start:.0f}s "
                  f"(order: {'forward' if pass_no % 2 == 0 else 'reverse'})", flush=True)

    print(f"wrote {args.out}; completed_depth mismatches: {mismatches}")


if __name__ == "__main__":
    main()
