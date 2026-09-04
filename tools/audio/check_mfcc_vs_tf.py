#!/usr/bin/env python3
"""Compare firmware float MFCC (MFCCF lines from the host build) against
TensorFlow audio_ops (the ML-zoo preprocessing). Usage:
  host/build/kws_host --wav clip.wav | grep MFCCF > ours.txt
  python3 tools/audio/check_mfcc_vs_tf.py clip.wav ours.txt
"""
import sys
import numpy as np
import tensorflow as tf
from tensorflow.python.ops import gen_audio_ops as audio_ops

wav, ours_path = sys.argv[1], sys.argv[2]
audio = tf.audio.decode_wav(tf.io.read_file(wav), desired_channels=1)
spec = audio_ops.audio_spectrogram(audio.audio, window_size=640, stride=320, magnitude_squared=True)
mfcc = audio_ops.mfcc(spec, audio.sample_rate, dct_coefficient_count=10).numpy()[0]  # [frames, 10]
ours = {}
for line in open(ours_path):
    p = line.split()
    ours[int(p[1].split("=")[1])] = np.array([float(v) for v in p[2:]])
maxerr = 0.0
for f, v in ours.items():
    ref = mfcc[f - 1]           # firmware frame f uses hops f-1,f -> TF frame index f-1
    err = np.max(np.abs(v - ref))
    maxerr = max(maxerr, err)
print(f"frames compared={len(ours)} max_abs_err={maxerr:.6f} (TF range {mfcc.min():.2f}..{mfcc.max():.2f})")
