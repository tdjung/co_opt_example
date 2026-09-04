#!/usr/bin/env python3
"""make_golden.py -- (re)generate data/golden/<clip>.log from the host build.

  python3 tools/harness/make_golden.py            # all clips in data/audio/** and data/audio_synth
  python3 tools/harness/make_golden.py --gen /path/to/generated   # other weights

Run this whenever the model weights or the algorithm (not placement/knobs)
change. Knob changes must NOT change the golden -- that is the whole point.
"""
import argparse
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sim_run  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--gen")
ap.add_argument("--clips", nargs="*")
a = ap.parse_args()
clips = a.clips or (sorted(glob.glob(os.path.join(ROOT, "data", "audio", "*", "*.wav"))) +
                    sorted(glob.glob(os.path.join(ROOT, "data", "audio_synth", "*.wav"))))
exe = sim_run.build_host("golden", "", a.gen)
gd = os.path.join(ROOT, "data", "golden")
os.makedirs(gd, exist_ok=True)
for c in clips:
    tmp = os.path.join(ROOT, "experiments", "_golden_tmp")
    r = sim_run.run_host(exe, c, tmp)
    dst = os.path.join(gd, os.path.splitext(os.path.basename(c))[0] + ".log")
    shutil.copy(r["log"], dst)
    print("golden", dst, "rc", r["rc"])
shutil.rmtree(os.path.join(ROOT, "experiments", "_golden_tmp"), ignore_errors=True)
