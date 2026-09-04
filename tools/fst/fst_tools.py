#!/usr/bin/env python3
"""fst_tools.py -- waveform tools for the AI loop (FST via pylibfst).

Signal names differ per platform, so they live in a mapping JSON
(tools/fst/signals.json). Edit that file, not this one.

Sub-commands:
  list        <fst>                                   list signals (grep with --grep)
  windows     <fst> [--window-us 10] [--csv out.csv]  per-window bus activity table
  at_pc       <fst> --pc 0x1234 [--cycles 200] [--occurrence 0]   signals around a PC hit
  range       <fst> --t0 US --t1 US [--signals a,b,c]  raw value changes in a window
  markers     <fst>                                   TRACE_MARKER writes (id, time)

Window table columns: t_us, per master: req count; per slave: busy cycles;
overlap cycles (>=2 masters active in the same cycle); func (active function
if a 'func' signal is mapped).

pip install pylibfst
"""
from __future__ import annotations
import argparse
import csv
import json
import os
import sys
from collections import defaultdict

try:
    import pylibfst
    from pylibfst import lib, ffi
except ImportError:
    pylibfst = None

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAP = os.path.join(HERE, "signals.json")


def load_map(path):
    return json.load(open(path))


class Wave:
    """Loads selected signals into memory as sorted (time, value) lists."""

    def __init__(self, path, want_names):
        if pylibfst is None:
            sys.exit("pip install pylibfst")
        self.fst = lib.fstReaderOpen(path.encode())
        if self.fst == ffi.NULL:
            sys.exit(f"cannot open {path}")
        scopes, sigs = pylibfst.get_scopes_signals2(self.fst)
        self.by_name = sigs.by_name
        self.timescale = lib.fstReaderGetTimescale(self.fst)  # e.g. -12 = ps
        self.end_time = lib.fstReaderGetEndTime(self.fst)
        self.handles = {}
        for n in want_names:
            if n in self.by_name:
                self.handles[n] = self.by_name[n].handle
        self.changes = defaultdict(list)
        lib.fstReaderClrFacProcessMaskAll(self.fst)
        for h in self.handles.values():
            lib.fstReaderSetFacProcessMask(self.fst, h)
        h2n = {h: n for n, h in self.handles.items()}
        strlike = {n for n in self.handles if self.by_name[n].length >= 64 and self.by_name[n].length % 8 == 0}

        def cb(user, time, handle, value):
            n = h2n.get(handle)
            if n is None:
                return
            v = pylibfst.string(value)
            if set(v) <= {"0", "1"}:
                if n in strlike:   # ASCII-packed string signal (e.g. function name)
                    b = int(v, 2).to_bytes(len(v) // 8, "big")
                    self.changes[n].append((time, b.decode("ascii", "replace").strip("\x00 ")))
                    return
                self.changes[n].append((time, int(v, 2)))
            else:
                self.changes[n].append((time, v))
        pylibfst.fstReaderIterBlocks(self.fst, cb)

    def to_us(self, t):
        return t * (10.0 ** self.timescale) * 1e6

    def from_us(self, us):
        return int(us * 1e-6 / (10.0 ** self.timescale))

    def close(self):
        lib.fstReaderClose(self.fst)


def cmd_list(a):
    fst = lib.fstReaderOpen(a.fst.encode())
    _, sigs = pylibfst.get_scopes_signals2(fst)
    for n, s in sorted(sigs.by_name.items()):
        if not a.grep or a.grep in n:
            print(f"{n}  [{s.length}]")
    lib.fstReaderClose(fst)


def _active_intervals(changes, valid_value=1):
    """From (time, value) of a 'valid/busy' signal -> list of (t_start, t_end)."""
    out, start = [], None
    for t, v in changes:
        if v == valid_value and start is None:
            start = t
        elif v != valid_value and start is not None:
            out.append((start, t))
            start = None
    if start is not None:
        out.append((start, None))
    return out


def cmd_windows(a):
    m = load_map(a.map)
    masters = m.get("bus_masters", {})     # name -> {"req": signal, "busy": signal?}
    slaves = m.get("bus_slaves", {})       # name -> {"busy": signal}
    names = [s["req"] for s in masters.values()] + [s.get("busy") for s in masters.values() if s.get("busy")] \
        + [s["busy"] for s in slaves.values()] + ([m["func"]] if m.get("func") else [])
    w = Wave(a.fst, names)
    cyc_us = 1e6 / m.get("cpu_hz", 100_000_000)
    win = a.window_us
    n_win = int(w.to_us(w.end_time) / win) + 1
    rows = [{"t_us": round(i * win, 3)} for i in range(n_win)]
    for mn, s in masters.items():
        for r in rows:
            r[f"{mn}_req"] = 0
        for t, v in w.changes.get(s["req"], []):
            if v == 1:
                rows[min(int(w.to_us(t) / win), n_win - 1)][f"{mn}_req"] += 1
    # busy cycles per master / slave and overlap
    busy_iv = {}
    for grp, key in ((masters, "busy"), (slaves, "busy")):
        for nm, s in grp.items():
            if s.get(key) in w.changes:
                busy_iv[nm] = _active_intervals(w.changes[s[key]])
                for r in rows:
                    r[f"{nm}_busy_cyc"] = 0
    for nm, ivs in busy_iv.items():
        for t0, t1 in ivs:
            t1 = w.end_time if t1 is None else t1
            u0, u1 = w.to_us(t0), w.to_us(t1)
            i0, i1 = int(u0 / win), int(u1 / win)
            for i in range(i0, min(i1, n_win - 1) + 1):
                lo, hi = max(u0, i * win), min(u1, (i + 1) * win)
                rows[i][f"{nm}_busy_cyc"] += int((hi - lo) / cyc_us + 0.5)
    # overlap: cycles where >=2 masters busy
    mbusy = [busy_iv[nm] for nm in masters if nm in busy_iv]
    if len(mbusy) >= 2:
        for r in rows:
            r["overlap_cyc"] = 0
        events = []
        for ivs in mbusy:
            for t0, t1 in ivs:
                events.append((t0, 1)); events.append((w.end_time if t1 is None else t1, -1))
        events.sort()
        depth, last = 0, 0
        for t, d in events:
            if depth >= 2 and t > last:
                u0, u1 = w.to_us(last), w.to_us(t)
                for i in range(int(u0 / win), min(int(u1 / win), n_win - 1) + 1):
                    lo, hi = max(u0, i * win), min(u1, (i + 1) * win)
                    rows[i]["overlap_cyc"] += int((hi - lo) / cyc_us + 0.5)
            depth += d; last = t
    if m.get("func") and m["func"] in w.changes:
        fc = w.changes[m["func"]]
        j = 0
        for r in rows:
            t = w.from_us(r["t_us"])
            while j + 1 < len(fc) and fc[j + 1][0] <= t:
                j += 1
            r["func"] = fc[j][1] if fc else ""
    keys = list(rows[0].keys())
    out = open(a.csv, "w", newline="") if a.csv else sys.stdout
    cw = csv.DictWriter(out, fieldnames=keys)
    cw.writeheader()
    for r in rows:
        cw.writerow(r)
    w.close()


def cmd_at_pc(a):
    m = load_map(a.map)
    pc_sig = m["core_pc"]
    extra = m.get("core_extra", []) + [s["req"] for s in m.get("bus_masters", {}).values()] + m.get("bus_extra", [])
    w = Wave(a.fst, [pc_sig] + extra)
    pc = int(a.pc, 0)
    hits = [t for t, v in w.changes.get(pc_sig, []) if v == pc]
    if not hits:
        print(f"PC {a.pc} never committed"); return
    t = hits[min(a.occurrence, len(hits) - 1)]
    cyc = 1e6 / m.get("cpu_hz", 100_000_000)
    t0, t1 = w.from_us(w.to_us(t) - a.cycles * cyc), w.from_us(w.to_us(t) + a.cycles * cyc)
    print(f"# PC {a.pc}: {len(hits)} hits, showing #{a.occurrence} at {w.to_us(t):.3f} us, +/-{a.cycles} cycles")
    _dump_range(w, t0, t1)
    w.close()


def _dump_range(w, t0, t1, signals=None):
    rows = []
    for n, ch in w.changes.items():
        if signals and n not in signals:
            continue
        for t, v in ch:
            if t0 <= t <= t1:
                rows.append((t, n, v))
    rows.sort()
    for t, n, v in rows:
        print(f"{w.to_us(t):12.3f} us  {n:40s} {v if isinstance(v, str) else hex(v)}")


def cmd_range(a):
    m = load_map(a.map)
    names = a.signals.split(",") if a.signals else list(m.get("all_signals", []))
    w = Wave(a.fst, names)
    _dump_range(w, w.from_us(a.t0), w.from_us(a.t1))
    w.close()


def cmd_markers(a):
    m = load_map(a.map)
    sig = m.get("trace_marker")
    if not sig:
        sys.exit("no trace_marker in signals.json")
    w = Wave(a.fst, [sig])
    for t, v in w.changes.get(sig, []):
        print(f"{w.to_us(t):12.3f} us  marker {v if isinstance(v, str) else hex(v)}")
    w.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "windows", "at_pc", "range", "markers"])
    ap.add_argument("fst")
    ap.add_argument("--map", default=DEFAULT_MAP)
    ap.add_argument("--grep")
    ap.add_argument("--window-us", type=float, default=10.0)
    ap.add_argument("--csv")
    ap.add_argument("--pc")
    ap.add_argument("--cycles", type=int, default=200)
    ap.add_argument("--occurrence", type=int, default=0)
    ap.add_argument("--t0", type=float)
    ap.add_argument("--t1", type=float)
    ap.add_argument("--signals")
    a = ap.parse_args()
    {"list": cmd_list, "windows": cmd_windows, "at_pc": cmd_at_pc, "range": cmd_range, "markers": cmd_markers}[a.cmd](a)


if __name__ == "__main__":
    main()
