#!/usr/bin/env python3
"""run_experiment.py -- one experiment = (HW tier, SW knobs, clips) -> metrics + verdict.

  python3 tools/harness/run_experiment.py --name base_small --tier small --backend host
  python3 tools/harness/run_experiment.py --name exp12 --tier medium --backend target \
      --knob OFFLOAD_PW1=1 --knob SECTION_WEIGHTS_PW1='PLACE_DATA(REGION_SRAM1)' \
      --clips data/audio_synth/chirp.wav

Knobs are macros from firmware/app/placement.h passed as -D defines, so the
source tree is never modified by an experiment (edit placement.h only to
change the *baseline*). Results go to experiments/<name>/ and one JSON line
is appended to experiments/ledger.jsonl.

Exit code 0 = built, ran, and passed golden verification.
"""
import argparse
import glob
import hashlib
import json
import os
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "callgrind"))
import sim_run                     # noqa: E402
from parse_log import parse, metrics   # noqa: E402
from verify_golden import verify   # noqa: E402
from gen_linker import render, tier_area  # noqa: E402


def git_rev():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception:
        return "nogit"


def golden_path(clip):
    return os.path.join(ROOT, "data", "golden", os.path.splitext(os.path.basename(clip))[0] + ".log")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--tier", default="small")
    ap.add_argument("--backend", choices=["host", "target"], default="host")
    ap.add_argument("--knob", action="append", default=[], help="MACRO=VALUE (placement.h knob)")
    ap.add_argument("--clips", nargs="*", help="WAV files (default: data/audio/**/*.wav or data/audio_synth/*.wav)")
    ap.add_argument("--gen", help="alternative generated weights dir")
    ap.add_argument("--max-isr-us", type=float, default=20.0)
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--mac", action="store_true", help="host: emulate MAC accelerator")
    a = ap.parse_args()

    tier = json.load(open(os.path.join(ROOT, "tools", "tiers", f"{a.tier}.json")))
    cost = json.load(open(os.path.join(ROOT, "tools", "cost", "cost_table.json")))
    clips = a.clips or sorted(glob.glob(os.path.join(ROOT, "data", "audio", "*", "*.wav"))) \
        or sorted(glob.glob(os.path.join(ROOT, "data", "audio_synth", "*.wav")))
    exp_dir = os.path.join(ROOT, "experiments", a.name)
    os.makedirs(exp_dir, exist_ok=True)

    knobs = dict(k.split("=", 1) for k in a.knob)
    extra = " ".join(f"-D{k}={shlex.quote(v)}" for k, v in knobs.items())
    cfg_hash = hashlib.sha1(json.dumps({"tier": tier, "knobs": knobs, "gen": a.gen}, sort_keys=True).encode()).hexdigest()[:10]
    t0 = time.time()

    # ---- build
    if a.backend == "host":
        exe = sim_run.build_host(a.name, extra, a.gen)
    else:
        ld = os.path.join(exp_dir, "link.ld")
        open(ld, "w").write(render(tier, os.path.join(ROOT, "firmware", "link", "cm4_template.ld")))
        exe = sim_run.build_target(a.name, ld, extra, a.gen)
    t_build = time.time() - t0

    # ---- run each clip
    results = []
    all_ok = True
    for clip in clips:
        out = os.path.join(exp_dir, os.path.splitext(os.path.basename(clip))[0])
        t1 = time.time()
        if a.backend == "host":
            r = sim_run.run_host(exe, clip, out, mac=a.mac or tier["mac"]["lanes"] > 0)
        else:
            r = sim_run.run_target(exe, clip, tier.get("platform_ir"), out)
        text = open(r["log"], errors="replace").read()
        p = parse(text)
        m = metrics(p, tier["cpu_hz"])
        v = {"ok": None, "errors": [], "warnings": []}
        if not a.no_verify:
            gp = golden_path(clip)
            if os.path.exists(gp):
                v = verify(text, open(gp).read(), a.max_isr_us, tier["cpu_hz"])
            else:
                v = {"ok": None, "errors": [], "warnings": [f"no golden for {clip}"]}
        if v["ok"] is False or r["rc"] != 0:
            all_ok = False
        results.append({"clip": os.path.relpath(clip, ROOT), "rc": r["rc"], "metrics": m, "verify": v,
                        "sim_seconds": round(time.time() - t1, 1), "callgrind": r.get("callgrind"), "fst": r.get("fst")})

    # ---- aggregate
    infer = [x["metrics"]["avg_infer_cycles"] for x in results if x["metrics"]["avg_infer_cycles"]]
    lat = [x["metrics"]["ctrl_max_latency_us"] for x in results if x["metrics"]["ctrl_max_latency_us"] is not None]
    record = {
        "name": a.name, "time": time.strftime("%Y-%m-%dT%H:%M:%S"), "git": git_rev(), "cfg_hash": cfg_hash,
        "backend": a.backend, "tier": a.tier, "area": tier_area(tier, cost), "knobs": knobs, "gen": a.gen,
        "build_seconds": round(t_build, 1), "clips": len(results),
        "verify_ok": all_ok,
        "avg_infer_cycles": (sum(infer) // len(infer)) if infer else None,
        "max_infer_cycles": max((x["metrics"]["max_infer_cycles"] or 0) for x in results) if results else None,
        "deadline_miss": sum((x["metrics"]["deadline_miss"] or 0) for x in results),
        "ctrl_max_latency_us": max(lat) if lat else None,
        "results": results,
    }
    json.dump(record, open(os.path.join(exp_dir, "result.json"), "w"), indent=1)
    slim = {k: v for k, v in record.items() if k != "results"}
    with open(os.path.join(ROOT, "experiments", "ledger.jsonl"), "a") as f:
        f.write(json.dumps(slim) + "\n")
    print(json.dumps(slim, indent=1))
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
