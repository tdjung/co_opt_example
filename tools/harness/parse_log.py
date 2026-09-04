"""parse_log.py -- parse the firmware log format into a metrics dict.

Line formats (firmware/app/main.c):
  MFCC frame=<f> <10 ints>
  PROF frame=<f> mfcc_cycles=<c>
  [infer <n>] frame=<f> t=<ms>ms top1=<label> (<s>) top2=<label> (<s>) cycles=<c>
  GOLDEN infer=<n> frame=<f> logits=<12> probs=<12> cksum=<11 hex>
  LAYERS infer=<n> <name>=<cycles> ...
  RESULT infer=<n> label=<l> score=<s> cycles=<c>
  CTRL ticks=<n> max_latency_cycles=<c> avg_latency_cycles=<c> seq_cksum=<hex> last_y=<y> last_u=<u>
  RUN frames=<f> inferences=<n> total_cycles=<c> deadline_us=<d> deadline_miss=<m> max_infer_cycles=<c> overruns=<o>
  SMOKE ... PASS/FAIL lines
"""
import re


def _kv(s):
    out = {}
    for tok in s.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def parse(text):
    m = {"mfcc": {}, "mfcc_cycles": {}, "infer": [], "golden": [], "layers": [], "ctrl": None, "run": None,
         "smoke": {"pass": [], "fail": []}, "boot": False}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("MFCC "):
            p = line.split()
            m["mfcc"][int(p[1].split("=")[1])] = [int(x) for x in p[2:]]
        elif line.startswith("PROF "):
            d = _kv(line[5:])
            m["mfcc_cycles"][int(d["frame"])] = int(d["mfcc_cycles"])
        elif line.startswith("[infer "):
            r = re.match(r"\[infer (\d+)\] frame=(\d+) t=(\d+)ms top1=(\w+) \((\d+)\) top2=(\w+) \((\d+)\) cycles=(\d+)", line)
            if r:
                m["infer"].append({"n": int(r[1]), "frame": int(r[2]), "t_ms": int(r[3]), "top1": r[4], "s1": int(r[5]),
                                   "top2": r[6], "s2": int(r[7]), "cycles": int(r[8])})
        elif line.startswith("GOLDEN "):
            d = _kv(line[7:])
            m["golden"].append({"n": int(d["infer"]), "frame": int(d["frame"]),
                                "logits": [int(x) for x in d["logits"].split(",")],
                                "probs": [int(x) for x in d["probs"].split(",")],
                                "cksum": d["cksum"].split(",")})
        elif line.startswith("LAYERS "):
            d = _kv(line[7:])
            n = int(d.pop("infer"))
            m["layers"].append({"n": n, **{k: int(v) for k, v in d.items()}})
        elif line.startswith("CTRL "):
            d = _kv(line[5:])
            m["ctrl"] = {k: (int(v) if not k.endswith("cksum") else v) for k, v in d.items()}
        elif line.startswith("RUN "):
            m["run"] = {k: int(v) for k, v in _kv(line[4:]).items()}
        elif line.startswith("BOOT"):
            m["boot"] = True
        elif line.startswith("PASS "):
            m["smoke"]["pass"].append(line[5:])
        elif line.startswith("FAIL "):
            m["smoke"]["fail"].append(line[5:])
    return m


def metrics(parsed, cpu_hz=100_000_000):
    """Flat metric dict for the ledger."""
    run = parsed.get("run") or {}
    ctrl = parsed.get("ctrl") or {}
    inf = parsed.get("infer") or []
    layers = parsed.get("layers") or []
    out = {
        "frames": run.get("frames"), "inferences": run.get("inferences"),
        "total_cycles": run.get("total_cycles"), "deadline_us": run.get("deadline_us"),
        "deadline_miss": run.get("deadline_miss"), "max_infer_cycles": run.get("max_infer_cycles"),
        "overruns": run.get("overruns"),
        "avg_infer_cycles": (sum(i["cycles"] for i in inf) // len(inf)) if inf else None,
        "mfcc_cycles_avg": (sum(parsed["mfcc_cycles"].values()) // len(parsed["mfcc_cycles"])) if parsed["mfcc_cycles"] else None,
        "ctrl_max_latency_cycles": ctrl.get("max_latency_cycles"),
        "ctrl_max_latency_us": (ctrl["max_latency_cycles"] * 1e6 / cpu_hz) if ctrl.get("max_latency_cycles") is not None else None,
        "ctrl_ticks": ctrl.get("ticks"),
    }
    if layers:
        names = [k for k in layers[-1] if k != "n"]
        out["layer_cycles_avg"] = {k: sum(l[k] for l in layers) // len(layers) for k in names}
    return out
