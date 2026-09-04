#!/usr/bin/env python3
"""Write a small synthetic FST matching tools/fst/signals.json for testing fst_tools.py."""
import sys
from pylibfst import lib, ffi
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test.fst"
w = lib.fstWriterCreate(out.encode(), 1)
lib.fstWriterSetTimescale(w, -9)   # ns
def var(name, bits):
    return lib.fstWriterCreateVar(w, 0, 0, bits, name.encode(), 0)
lib.fstWriterSetScope(w, 0, b"top", ffi.NULL)
lib.fstWriterSetScope(w, 0, b"core", ffi.NULL)
pc = var("commit_pc", 32); func = var("commit_func", 256)
lib.fstWriterSetUpscope(w)
lib.fstWriterSetScope(w, 0, b"bus", ffi.NULL)
m = [var(f"m{i}_req", 1) for i in range(3)]; ma = [var(f"m{i}_active", 1) for i in range(3)]
s = [var(f"s{i}_active", 1) for i in range(3)]; addr = var("addr", 32)
lib.fstWriterSetUpscope(w)
lib.fstWriterSetScope(w, 0, b"ctrl", ffi.NULL)
mk = var("trace_marker", 32)
lib.fstWriterSetUpscope(w); lib.fstWriterSetUpscope(w)
def emit(h, bits, v):
    lib.fstWriterEmitValueChange(w, h, format(v, f"0{bits}b").encode())
def emit_str(h, sstr):
    lib.fstWriterEmitValueChange(w, h, "".join(format(ord(c), "08b") for c in sstr.ljust(32)[:32]).encode())
t = 0
lib.fstWriterEmitTimeChange(w, 0)
for h in m + ma + s: emit(h, 1, 0)
emit(mk, 32, 0); emit(pc, 32, 0); emit_str(func, "main")
# 100 us of activity at 10 ns cycle: cpu busy on sram0 always; dma bursts every 20 us for 2 us; mac at 50-60 us
for cyc in range(10000):
    t = cyc * 10
    lib.fstWriterEmitTimeChange(w, t)
    emit(pc, 32, 0x1000 + (cyc % 64) * 2)
    if cyc == 2000: emit_str(func, "mfcc_push_hop"); emit(mk, 32, 0x10)
    if cyc == 5000: emit_str(func, "ds_cnn_run"); emit(mk, 32, 0x20)
    emit(m[0], 1, cyc % 2); emit(ma[0], 1, 1); emit(s[0], 1, 1)
    dma_on = (cyc % 2000) < 200
    emit(ma[1], 1, 1 if dma_on else 0); emit(m[1], 1, 1 if (dma_on and cyc % 4 == 0) else 0)
    mac_on = 5000 <= cyc < 6000
    emit(ma[2], 1, 1 if mac_on else 0); emit(m[2], 1, 1 if (mac_on and cyc % 8 == 0) else 0)
    emit(s[1], 1, 1 if mac_on else 0); emit(s[2], 1, 0)
    emit(addr, 32, 0x24000000 + (cyc * 4) % 65536)
lib.fstWriterClose(w)
print("wrote", out)
