#!/usr/bin/env python3
"""Cross-check: TFLite interpreter (int8) vs the firmware's CMSIS-NN run on the
same int8 feature vectors.

Usage:
  python3 model/verify_tflite_vs_cmsis.py model.tflite host/build_x/nn_only [n_vectors]

Expect bit-exact logits (pre-softmax); probs may differ by +-1 (softmax LUT).
"""
import subprocess
import sys
import numpy as np
import tensorflow as tf

tfl, exe = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 20
it = tf.lite.Interpreter(model_path=tfl, experimental_preserve_all_tensors=True)
it.allocate_tensors()
inp = it.get_input_details()[0]
out = it.get_output_details()[0]
rng = np.random.default_rng(3)
vecs = rng.integers(-128, 128, size=(n, 490), dtype=np.int8)
fc_out = None
for op in it._get_ops_details():
    if op["op_name"] == "FULLY_CONNECTED":
        fc_out = op["outputs"][0]
ref_probs, ref_logits = [], []
for v in vecs:
    it.set_tensor(inp["index"], v.reshape(inp["shape"]))
    it.invoke()
    ref_probs.append(it.get_tensor(out["index"]).flatten().copy())
    ref_logits.append(it.get_tensor(fc_out).flatten().copy() if fc_out is not None else None)
open("/tmp/feat.bin", "wb").write(vecs.tobytes())
res = subprocess.run([exe, "/tmp/feat.bin"], capture_output=True, text=True).stdout.strip().split("\n")
max_l, max_p, bad = 0, 0, 0
for i, line in enumerate(res):
    f = dict(kv.split("=") for kv in line.split()[2:])
    lg = np.array([int(x) for x in f["logits"].split(",")])
    pr = np.array([int(x) for x in f["probs"].split(",")])
    if ref_logits[i] is not None:
        max_l = max(max_l, int(np.max(np.abs(lg - ref_logits[i]))))
    max_p = max(max_p, int(np.max(np.abs(pr - ref_probs[i]))))
    if np.argmax(pr) != np.argmax(ref_probs[i]):
        bad += 1
print(f"vectors={n} max|logit diff|={max_l} max|prob diff|={max_p} argmax mismatches={bad}")
