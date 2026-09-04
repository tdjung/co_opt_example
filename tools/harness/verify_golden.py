#!/usr/bin/env python3
"""verify_golden.py -- functional verdict for one run.

  python3 tools/harness/verify_golden.py run.log data/golden/<clip>.log [--max-isr-us 20] [--json]

Checks (all must pass for a valid experiment):
  1. every GOLDEN line: logits bit-exact vs golden
  2. probs bit-exact (softmax is integer, so equality is expected)
  3. MFCC int8 features per frame identical
  4. CTRL seq_cksum identical and ticks identical (control loop produced the same u/y sequence)
  5. CTRL max ISR latency <= limit (target only; host reports 0)
  6. RUN overruns == 0 (audio ring never overran)
Layer checksums are reported on mismatch to localise the first divergent layer.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from parse_log import parse  # noqa: E402


def verify(run_text, gold_text, max_isr_us=20.0, cpu_hz=100_000_000):
    r, g = parse(run_text), parse(gold_text)
    res = {"ok": True, "errors": [], "warnings": []}

    def fail(msg):
        res["ok"] = False
        res["errors"].append(msg)

    if not r["run"]:
        fail("no RUN line: firmware did not finish")
        return res
    if len(r["golden"]) != len(g["golden"]):
        fail(f"inference count {len(r['golden'])} != golden {len(g['golden'])}")
    for a, b in zip(r["golden"], g["golden"]):
        if a["logits"] != b["logits"]:
            first = next((i for i, (x, y) in enumerate(zip(a["cksum"], b["cksum"])) if x != y), None)
            fail(f"infer {a['n']}: logits differ {a['logits']} vs {b['logits']}; first divergent layer index={first}")
        elif a["probs"] != b["probs"]:
            fail(f"infer {a['n']}: probs differ")
    for f, v in g["mfcc"].items():
        if r["mfcc"].get(f) != v:
            fail(f"MFCC frame {f} differs: {r['mfcc'].get(f)} vs {v}")
            break
    if r["ctrl"] and g["ctrl"]:
        if r["ctrl"]["seq_cksum"] != g["ctrl"]["seq_cksum"] or r["ctrl"]["ticks"] != g["ctrl"]["ticks"]:
            fail(f"CTRL sequence differs (ticks {r['ctrl']['ticks']} vs {g['ctrl']['ticks']}, cksum {r['ctrl']['seq_cksum']} vs {g['ctrl']['seq_cksum']})")
        lat_us = r["ctrl"]["max_latency_cycles"] * 1e6 / cpu_hz
        if lat_us > max_isr_us:
            fail(f"ISR max latency {lat_us:.1f} us > {max_isr_us} us")
    else:
        fail("missing CTRL line")
    if r["run"].get("overruns", 0):
        fail(f"audio ring overruns={r['run']['overruns']}")
    if r["run"].get("deadline_miss", 0):
        res["warnings"].append(f"deadline misses={r['run']['deadline_miss']} (performance, not correctness)")
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("run_log")
    ap.add_argument("golden_log")
    ap.add_argument("--max-isr-us", type=float, default=20.0)
    ap.add_argument("--cpu-hz", type=int, default=100_000_000)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    res = verify(open(a.run_log).read(), open(a.golden_log).read(), a.max_isr_us, a.cpu_hz)
    if a.json:
        print(json.dumps(res, indent=1))
    else:
        print("VERIFY", "OK" if res["ok"] else "FAIL")
        for e in res["errors"]:
            print("  error:", e)
        for w in res["warnings"]:
            print("  warn:", w)
    sys.exit(0 if res["ok"] else 1)
