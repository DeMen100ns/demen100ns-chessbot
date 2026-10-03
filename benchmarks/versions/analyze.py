"""Summarise speed.csv and matches.csv into markdown tables (printed and written to summary.md)."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

from engine import EXTRA, HERE, VERSIONS

Z = 1.96


def geomean(values: list[float]) -> float:
    return math.exp(sum(math.log(v) for v in values) / len(values))


def fmt_s(seconds: float) -> str:
    return f"{seconds:.2f} s" if seconds >= 1 else f"{seconds * 1000:.0f} ms"


# ----------------------------------------------------------------------------- speed

def speed_section(path: Path) -> list[str]:
    rows = [r for r in csv.DictReader(open(path)) if r["warmup"] == "0"]
    versions = [v for v in [*VERSIONS, *EXTRA] if any(r["version"] == v for r in rows)]
    depths = sorted({int(r["depth"]) for r in rows})
    positions = sorted({r["position"] for r in rows})
    phase = {r["position"]: r["phase"] for r in rows}
    passes = sorted({int(r["pass"]) for r in rows})

    t = defaultdict(list)          # (v, d, pos) -> [ms per pass]
    moves = defaultdict(set)       # (v, d, pos) -> {bestmove}
    first_move = {}                # (v, d, pos) -> bestmove from first measured pass
    totals = defaultdict(float)    # (v, d, pass) -> sum ms
    bad_depth = 0
    for r in rows:
        key = (r["version"], int(r["depth"]), r["position"])
        ms = float(r["time_ms"])
        t[key].append(ms)
        moves[key].add(r["bestmove"])
        if int(r["pass"]) == passes[0]:
            first_move[key] = r["bestmove"]
        totals[(r["version"], int(r["depth"]), int(r["pass"]))] += ms
        bad_depth += r["completed_depth"] != r["depth"]

    med_total = {(v, d): statistics.median(totals[(v, d, p)] for p in passes) for v in versions for d in depths}
    spread = {(v, d): (max(totals[(v, d, p)] for p in passes) - min(totals[(v, d, p)] for p in passes)) / med_total[(v, d)]
              for v in versions for d in depths}
    med_pos = {k: statistics.median(v) for k, v in t.items()}

    def pos_geo(a: str, b: str, d: int, subset=None) -> float:
        ps = [p for p in positions if subset is None or phase[p] == subset]
        return geomean([med_pos[(a, d, p)] / med_pos[(b, d, p)] for p in ps])

    out = [f"## Speed (time to depth)\n",
           f"{len(positions)} positions, {len(passes)} measured passes, depths {depths}. "
           f"Totals are the sum over all positions within one pass; the table shows the median "
           f"pass. completed_depth != requested: {bad_depth}.\n"]

    for d in depths:
        out.append(f"\n### Depth {d}\n")
        out.append("| version | median total | pass spread | vs prev (total) | vs prev (geomean/pos) | vs v1 (total) | vs v1 (geomean/pos) | bestmove differs from prev |")
        out.append("|---|---:|---:|---:|---:|---:|---:|---:|")
        for i, v in enumerate(versions):
            prev = versions[i - 1] if i else None
            if prev:
                diff = sum(first_move[(v, d, p)] != first_move[(prev, d, p)] for p in positions)
                prev_tot = f"{med_total[(prev, d)] / med_total[(v, d)]:.2f}x"
                prev_geo = f"{pos_geo(prev, v, d):.2f}x"
                diff_s = f"{diff}/{len(positions)}"
            else:
                prev_tot = prev_geo = diff_s = "-"
            out.append(f"| {v} | {fmt_s(med_total[(v, d)] / 1000)} | {spread[(v, d)] * 100:.0f}% | {prev_tot} | {prev_geo} | "
                       f"{med_total[(versions[0], d)] / med_total[(v, d)]:.2f}x | {pos_geo(versions[0], v, d):.2f}x | {diff_s} |")

    # Best-move disagreement flags, per depth: flag pairs far above the typical rate.
    out.append("\n### Best-move changes vs predecessor\n")
    for d in depths:
        rates = {versions[i]: sum(first_move[(versions[i], d, p)] != first_move[(versions[i - 1], d, p)] for p in positions) / len(positions)
                 for i in range(1, len(versions))}
        typical = statistics.median(rates.values())
        flagged = [f"{v} ({r * 100:.0f}%)" for v, r in rates.items() if r >= max(2 * typical, 0.25)]
        out.append(f"- depth {d}: median change rate {typical * 100:.0f}%; flagged (>= 2x median and >= 25%): "
                   f"{', '.join(flagged) if flagged else 'none'}")
    nondet = [(k, sorted(m)) for k, m in moves.items() if len(m) > 1]
    out.append(f"- non-deterministic best moves across passes: {len(nondet)}"
               + (" — " + "; ".join(f"{k[0]} d{k[1]} {k[2]} {m}" for k, m in nondet[:10]) if nondet else ""))

    dmax = depths[-1]
    out.append(f"\n### Depth {dmax} speedup vs predecessor by phase (geomean per position)\n")
    phases = ["opening", "middlegame", "endgame"]
    out.append("| version | " + " | ".join(phases) + " |")
    out.append("|---|" + "---:|" * len(phases))
    for i in range(1, len(versions)):
        out.append(f"| {versions[i]} | " + " | ".join(f"{pos_geo(versions[i - 1], versions[i], dmax, ph):.2f}x" for ph in phases) + " |")

    out.append(f"\n### Thermal / drift check: depth-{dmax} total per pass\n")
    out.append("| version | " + " | ".join(f"pass {p}" for p in passes) + " |")
    out.append("|---|" + "---:|" * len(passes))
    for v in versions:
        out.append(f"| {v} | " + " | ".join(fmt_s(totals[(v, dmax, p)] / 1000) for p in passes) + " |")
    return out


# ----------------------------------------------------------------------------- matches

def elo(score: float) -> float:
    if score <= 0:
        return -math.inf
    if score >= 1:
        return math.inf
    return -400 * math.log10(1 / score - 1)


def fmt_elo(x: float) -> str:
    return "+inf" if x == math.inf else "-inf" if x == -math.inf else f"{x + 0.0:+.0f}"


def match_stats(scores: list[float]) -> dict:
    n = len(scores)
    mean = sum(scores) / n
    sd = statistics.stdev(scores) if n > 1 else 0.0
    if sd == 0:
        # All games had the same result: the normal approximation collapses, so use the
        # Wilson interval for the score instead.
        denom = 1 + Z * Z / n
        centre = (mean + Z * Z / (2 * n)) / denom
        half = Z * math.sqrt(mean * (1 - mean) / n + Z * Z / (4 * n * n)) / denom
    else:
        centre, half = mean, Z * sd / math.sqrt(n)
    lo, hi = max(centre - half, 0.0), min(centre + half, 1.0)
    return {"n": n, "w": scores.count(1.0), "d": scores.count(0.5), "l": scores.count(0.0),
            "score": mean, "sd": sd, "elo": elo(mean), "lo": elo(lo), "hi": elo(hi)}


def match_section(path: Path) -> list[str]:
    rows = list(csv.DictReader(open(path)))
    by_pair: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for r in rows:
        by_pair[(r["new"], r["old"])].append(r)
    order = {v: i for i, v in enumerate(VERSIONS)}
    pairs = sorted(by_pair, key=lambda p: (p[1] != "v1_baseline" or order[p[0]] == 1, order[p[0]]))
    # Sequential pairs first (new's predecessor), then vs-v1 pairs.
    seq = [p for p in pairs if not (p[1] == "v1_baseline" and order[p[0]] > 1)]
    vs1 = [p for p in pairs if p not in seq]

    out = ["## Strength (head-to-head)\n",
           "W/D/L are from the newer version's point of view. Elo = -400*log10(1/score - 1); "
           "95% CI from the per-game score standard deviation (Wilson interval when every game "
           "had the same result).\n",
           "| pair | games | W/D/L | score | Elo | 95% CI | significant | faults |",
           "|---|---:|---:|---:|---:|---|---|---:|"]
    stats = {}
    for p in seq + vs1:
        g = by_pair[p]
        s = match_stats([float(r["new_score"]) for r in g])
        stats[p] = s
        faults = sum(1 for r in g if r["fault_engine"])
        sig = "yes" if (s["lo"] > 0 or s["hi"] < 0) else "not significant"
        out.append(f"| {p[0]} vs {p[1]} | {s['n']} | {s['w']}/{s['d']}/{s['l']} | {s['score'] * 100:.1f}% | "
                   f"{fmt_elo(s['elo'])} | [{fmt_elo(s['lo'])}, {fmt_elo(s['hi'])}] | {sig} | {faults} |")

    chain = [stats[p]["elo"] for p in seq]
    if chain and all(math.isfinite(x) for x in chain):
        out.append(f"\nSum of sequential Elo gains: {sum(chain):+.0f} "
                   f"(sequential Elo is not strictly additive; compare with the direct vs-v1 rows).")

    terms = defaultdict(int)
    for r in rows:
        terms[r["termination"]] += 1
    out.append("\nTerminations: " + ", ".join(f"{k} {v}" for k, v in sorted(terms.items(), key=lambda kv: -kv[1])))
    faults = [r for r in rows if r["fault_engine"]]
    if faults:
        out.append("\nFaults (illegal move / crash / timeout):")
        out += [f"- {r['pair']} game {r['game']}: {r['termination']} by {r['fault_engine']} — {r['detail']}" for r in faults]
    think = [int(r["max_think_ms"]) for r in rows]
    out.append(f"\nSlowest single move across all games: {max(think)} ms (budget {100} ms).")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--speed", type=Path, default=HERE / "speed.csv")
    ap.add_argument("--matches", type=Path, default=HERE / "matches.csv")
    ap.add_argument("--out", type=Path, default=HERE / "summary.md")
    args = ap.parse_args()
    lines: list[str] = []
    if args.speed.exists():
        lines += speed_section(args.speed)
    if args.matches.exists() and args.matches.stat().st_size > 0:
        lines += [""] + match_section(args.matches)
    text = "\n".join(lines) + "\n"
    args.out.write_text(text)
    print(text)


if __name__ == "__main__":
    main()
