"""callgrind.py -- parser for the callgrind output format.

Handles: events/positions headers, name compression ((id) name), fl/fi/fe/fn,
cfl/cfi/cfn/calls, cost lines with relative/absolute positions and sub-position
compression (+n/-n/*). Custom event names (e.g. AccFlash, StallI) are picked up
from the `events:` header automatically.

Result: Profile with
  .events            list of event names
  .functions[name]   Function(name, file, self_cost{event: n}, lines{lineno: {event: n}},
                              calls{callee: CallEdge(count, cost)})
  .inclusive(name)   inclusive cost dict (memoised, cycle-safe)
"""
from __future__ import annotations
import re
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class CallEdge:
    count: int = 0
    cost: Dict[str, int] = field(default_factory=lambda: defaultdict(int))


@dataclass
class Function:
    name: str
    file: str = ""
    self_cost: Dict[str, int] = field(default_factory=lambda: defaultdict(int))
    lines: Dict[int, Dict[str, int]] = field(default_factory=lambda: defaultdict(lambda: defaultdict(int)))
    calls: Dict[str, CallEdge] = field(default_factory=dict)
    line_min: int = 0


class Profile:
    def __init__(self):
        self.events: List[str] = []
        self.positions: List[str] = ["line"]
        self.functions: Dict[str, Function] = {}
        self.header: Dict[str, str] = {}
        self._incl_cache: Dict[str, Dict[str, int]] = {}

    # ---- queries -------------------------------------------------------
    def total(self) -> Dict[str, int]:
        t = defaultdict(int)
        for f in self.functions.values():
            for e, v in f.self_cost.items():
                t[e] += v
        return t

    def inclusive(self, name: str) -> Dict[str, int]:
        if name in self._incl_cache:
            return self._incl_cache[name]
        visiting = set()

        def rec(n):
            if n in self._incl_cache:
                return self._incl_cache[n]
            if n in visiting or n not in self.functions:
                return defaultdict(int)
            visiting.add(n)
            f = self.functions[n]
            tot = defaultdict(int, f.self_cost)
            for callee, edge in f.calls.items():
                for e, v in edge.cost.items():
                    tot[e] += v
            visiting.discard(n)
            self._incl_cache[n] = tot
            return tot
        return rec(name)

    def callers(self, name: str):
        return [(c.name, edge) for c in self.functions.values() for callee, edge in c.calls.items() if callee == name]


_NAME_RE = re.compile(r"^\((\d+)\)(?:\s+(.*))?$")


def _decompress(table: Dict[int, str], s: str) -> str:
    m = _NAME_RE.match(s.strip())
    if not m:
        return s.strip()
    idx = int(m.group(1))
    if m.group(2) is not None:
        table[idx] = m.group(2)
        return m.group(2)
    return table.get(idx, f"?{idx}")


def parse(path: str) -> Profile:
    p = Profile()
    files: Dict[int, str] = {}
    fns: Dict[int, str] = {}
    cur_fn: Optional[Function] = None
    cur_file = ""
    cur_cfn: Optional[str] = None
    pending_calls: Optional[int] = None
    pos = [0] * 4
    with open(path, "r", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            if ":" in line and not line[0].isdigit() and not line[0] in "+-*":
                key, _, val = line.partition(":")
                key = key.strip()
                val = val.strip()
                if key == "events":
                    p.events = val.split()
                    continue
                if key == "positions":
                    p.positions = val.split()
                    continue
                if key in ("fl", "fi", "fe"):
                    cur_file = _decompress(files, val)
                    continue
                if key == "fn":
                    name = _decompress(fns, val)
                    cur_fn = p.functions.get(name)
                    if cur_fn is None:
                        cur_fn = Function(name, cur_file)
                        p.functions[name] = cur_fn
                    elif not cur_fn.file:
                        cur_fn.file = cur_file
                    pos = [0] * 4
                    continue
                if key in ("cfl", "cfi"):
                    _decompress(files, val)
                    continue
                if key == "cfn":
                    cur_cfn = _decompress(fns, val)
                    continue
                if key == "calls":
                    parts = val.split()
                    pending_calls = int(parts[0])
                    continue
                if key in ("ob", "cob", "jump", "jcnd", "totals", "summary"):
                    p.header[key] = val
                    continue
                p.header[key] = val
                continue
            # cost line
            if cur_fn is None:
                continue
            toks = line.split()
            npos = len(p.positions)
            for i in range(npos):
                t = toks[i]
                if t == "*":
                    pass
                elif t[0] in "+-":
                    pos[i] += int(t)
                else:
                    pos[i] = int(t)
            costs = [int(x) for x in toks[npos:]]
            lineno = pos[p.positions.index("line")] if "line" in p.positions else 0
            if pending_calls is not None:
                edge = cur_fn.calls.setdefault(cur_cfn, CallEdge())
                edge.count += pending_calls
                for e, v in zip(p.events, costs):
                    edge.cost[e] += v
                pending_calls = None
                cur_cfn = None
            else:
                for e, v in zip(p.events, costs):
                    cur_fn.self_cost[e] += v
                    cur_fn.lines[lineno][e] += v
    return p
