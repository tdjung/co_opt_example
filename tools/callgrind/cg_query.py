#!/usr/bin/env python3
"""cg_query.py -- AI-facing query tools over callgrind profiles.

CLI:
  cg_query.py summary  <prof> [--event Cycle]
  cg_query.py top      <prof> --event Cycle [-n 15] [--inclusive]
  cg_query.py annotate <prof> --function arm_convolve_s8 [--source-root DIR] [-n 25]
  cg_query.py callers  <prof> --function memcpy
  cg_query.py diff     <profA> <profB> --event Cycle [-n 15]
  cg_query.py stalls   <prof>            # per-function stall decomposition if events exist
  cg_query.py regions  <prof>            # memory-region access mix per function (Acc* events)
All commands accept --json for machine-readable output.

Python:
  from cg_query import load, summary, top, annotate, diff
"""
from __future__ import annotations
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import callgrind  # noqa: E402

STALL_EVENTS = ["StallI", "StallD", "BusWait", "StallPipe"]
REGION_PREFIX = "Acc"


def load(path):
    return callgrind.parse(path)


def _pick_event(p, event):
    if event in p.events:
        return event
    for cand in ("Cycle", "Cycles", "Cyc", "Ir"):
        if cand in p.events:
            return cand
    return p.events[0]


def summary(p, event="Cycle"):
    ev = _pick_event(p, event)
    tot = p.total()
    out = {"events": p.events, "totals": dict(tot), "primary_event": ev, "n_functions": len(p.functions)}
    if "Ir" in tot and ev != "Ir" and tot["Ir"]:
        out["cycles_per_instr"] = round(tot[ev] / tot["Ir"], 3)
        out["stall_total"] = tot[ev] - tot["Ir"]
    st = {e: tot[e] for e in STALL_EVENTS if e in tot}
    if st:
        out["stall_breakdown"] = st
    reg = {e: tot[e] for e in tot if e.startswith(REGION_PREFIX)}
    if reg:
        s = sum(reg.values()) or 1
        out["region_access_mix"] = {e: round(v / s, 4) for e, v in reg.items()}
    top5 = top(p, ev, n=5)
    out["top5"] = top5
    return out


def top(p, event="Cycle", n=15, inclusive=False):
    ev = _pick_event(p, event)
    tot = p.total()[ev] or 1
    rows = []
    for f in p.functions.values():
        c = p.inclusive(f.name)[ev] if inclusive else f.self_cost.get(ev, 0)
        if c:
            rows.append({"function": f.name, "file": os.path.basename(f.file), ev: c, "pct": round(100.0 * c / tot, 2)})
    rows.sort(key=lambda r: -r[ev])
    return rows[:n]


def annotate(p, function, n=25, source_root=None, event="Cycle"):
    ev = _pick_event(p, event)
    f = p.functions.get(function)
    if not f:
        cands = [k for k in p.functions if function in k]
        return {"error": f"function not found: {function}", "candidates": cands[:10]}
    rows = []
    src = None
    if source_root and f.file:
        for cand in (os.path.join(source_root, f.file), f.file):
            if os.path.exists(cand):
                src = open(cand, errors="replace").read().splitlines()
                break
    for ln, costs in f.lines.items():
        row = {"line": ln, **{e: costs.get(e, 0) for e in p.events}}
        if src and 0 < ln <= len(src):
            row["src"] = src[ln - 1].strip()[:100]
        rows.append(row)
    rows.sort(key=lambda r: -r.get(ev, 0))
    return {"function": function, "file": f.file, "self": dict(f.self_cost),
            "inclusive": dict(p.inclusive(function)), "calls": {k: {"count": e.count, **dict(e.cost)} for k, e in f.calls.items()},
            "hot_lines": rows[:n]}


def callers(p, function, event="Cycle"):
    ev = _pick_event(p, event)
    return [{"caller": c, "count": e.count, ev: e.cost.get(ev, 0)} for c, e in p.callers(function)]


def stalls(p, n=15, event="Cycle"):
    ev = _pick_event(p, event)
    rows = []
    for f in p.functions.values():
        cyc = f.self_cost.get(ev, 0)
        if not cyc:
            continue
        r = {"function": f.name, ev: cyc, "Ir": f.self_cost.get("Ir", 0)}
        r["stall"] = cyc - r["Ir"] if r["Ir"] else None
        for e in STALL_EVENTS + ["CacheMiss", "BranchMiss", "Bm"]:
            if e in f.self_cost:
                r[e] = f.self_cost[e]
        rows.append(r)
    rows.sort(key=lambda r: -r[ev])
    return rows[:n]


def regions(p, n=15, event="Cycle"):
    ev = _pick_event(p, event)
    reg_events = [e for e in p.events if e.startswith(REGION_PREFIX)]
    rows = []
    for f in p.functions.values():
        cyc = f.self_cost.get(ev, 0)
        if not cyc:
            continue
        acc = {e: f.self_cost.get(e, 0) for e in reg_events}
        s = sum(acc.values())
        rows.append({"function": f.name, ev: cyc, "accesses": s,
                     "mix": {e: round(v / s, 3) for e, v in acc.items()} if s else {}})
    rows.sort(key=lambda r: -r[ev])
    return {"region_events": reg_events, "functions": rows[:n]}


def diff(pa, pb, event="Cycle", n=15):
    ev = _pick_event(pa, event)
    names = set(pa.functions) | set(pb.functions)
    rows = []
    for nme in names:
        a = pa.functions[nme].self_cost.get(ev, 0) if nme in pa.functions else 0
        b = pb.functions[nme].self_cost.get(ev, 0) if nme in pb.functions else 0
        if a != b:
            rows.append({"function": nme, "A": a, "B": b, "delta": b - a,
                         "pct": round(100.0 * (b - a) / a, 1) if a else None})
    rows.sort(key=lambda r: -abs(r["delta"]))
    ta, tb = pa.total()[ev], pb.total()[ev]
    return {"event": ev, "total_A": ta, "total_B": tb, "delta": tb - ta,
            "pct": round(100.0 * (tb - ta) / ta, 2) if ta else None, "functions": rows[:n]}


def _print(obj, as_json):
    if as_json:
        print(json.dumps(obj, indent=1))
        return
    if isinstance(obj, list):
        if not obj:
            print("(empty)")
            return
        if not isinstance(obj[0], dict):
            print(" ".join(str(x) for x in obj))
            return
        keys = [k for k in obj[0].keys() if k not in ("mix",)]
        print("  ".join(f"{k:>14}" if k != "function" else f"{k:<40}" for k in keys))
        for r in obj:
            print("  ".join(f"{str(r.get(k, '')):>14}" if k != "function" else f"{str(r.get(k, ''))[:40]:<40}" for k in keys))
    elif isinstance(obj, dict):
        for k, v in obj.items():
            if isinstance(v, list):
                print(f"{k}:")
                _print(v, False)
            else:
                print(f"{k}: {v}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["summary", "top", "annotate", "callers", "diff", "stalls", "regions"])
    ap.add_argument("prof", nargs="+")
    ap.add_argument("--event", default="Cycle")
    ap.add_argument("-n", type=int, default=15)
    ap.add_argument("--inclusive", action="store_true")
    ap.add_argument("--function")
    ap.add_argument("--source-root")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    p = load(a.prof[0])
    if a.cmd == "summary":
        out = summary(p, a.event)
    elif a.cmd == "top":
        out = top(p, a.event, a.n, a.inclusive)
    elif a.cmd == "annotate":
        out = annotate(p, a.function, a.n, a.source_root, a.event)
    elif a.cmd == "callers":
        out = callers(p, a.function, a.event)
    elif a.cmd == "stalls":
        out = stalls(p, a.n, a.event)
    elif a.cmd == "regions":
        out = regions(p, a.n, a.event)
    else:
        out = diff(p, load(a.prof[1]), a.event, a.n)
    _print(out, a.json)


if __name__ == "__main__":
    main()
