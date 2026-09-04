#!/usr/bin/env python3
"""Generate deterministic synthetic 16 kHz mono 16-bit WAV clips for smoke tests.

Real keyword clips (Google Speech Commands) must be added by hand under
data/audio/<label>/*.wav -- this only produces signals with known structure so
the pipeline (DMA -> MFCC -> CNN) can be exercised and regressed.
"""
import argparse
import os
import struct
import wave
import numpy as np

SR = 16000


def write_wav(path, x):
    x = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(x.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "data", "audio_synth"))
    ap.add_argument("--seconds", type=float, default=1.0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    n = int(SR * a.seconds)
    t = np.arange(n) / SR
    rng = np.random.default_rng(1234)
    clips = {
        "silence": np.zeros(n),
        "noise": 0.1 * rng.standard_normal(n),
        "tone440": 0.5 * np.sin(2 * np.pi * 440 * t),
        "tone1k_burst": 0.6 * np.sin(2 * np.pi * 1000 * t) * ((t > 0.3) & (t < 0.6)),
        "chirp": 0.5 * np.sin(2 * np.pi * (200 * t + (3000 - 200) / (2 * a.seconds) * t * t)),
        "two_tone": 0.3 * np.sin(2 * np.pi * 300 * t) + 0.3 * np.sin(2 * np.pi * 2500 * t),
    }
    for name, x in clips.items():
        write_wav(os.path.join(a.out, f"{name}.wav"), x)
    # 3 s stream: silence + burst + chirp for the streaming demo
    stream = np.concatenate([np.zeros(SR // 2), clips["tone1k_burst"], clips["chirp"], np.zeros(SR // 2)])
    write_wav(os.path.join(a.out, "stream_3s.wav"), stream)
    print("wrote", len(clips) + 1, "clips to", a.out)


if __name__ == "__main__":
    main()
