#!/usr/bin/env python3
"""gen_linker.py -- instantiate firmware/link/cm4_template.ld for a HW tier.

  python3 tools/harness/gen_linker.py tools/tiers/small.json firmware/link/small.ld

Also exposes tier_area(tier, cost_table) used by the harness to report area.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def render(tier, template_path):
    txt = open(template_path).read()
    for name, r in tier["regions"].items():
        pat = re.compile(rf"^(\s*{name}\s*\(\w+\)\s*:\s*ORIGIN\s*=\s*)0x[0-9A-Fa-f]+(,\s*LENGTH\s*=\s*)\S+(\s*/\*\s*@@{name}@@\s*\*/)", re.M)
        txt, n = pat.subn(rf"\g<1>{r['origin']}\g<2>{r['length_kb']}K\g<3>", txt)
        if n != 1:
            sys.exit(f"region {name} marker not found in template")
    return txt


def tier_area(tier, cost):
    a = cost["area"]
    sram = sum(r["length_kb"] for k, r in tier["regions"].items() if k.startswith("SRAM"))
    tcm = tier["regions"]["ITCM"]["length_kb"] + tier["regions"]["DTCM"]["length_kb"]
    area = sram * a["sram_per_kb"] + tcm * a["tcm_per_kb"]
    ic = tier["icache"]
    area += ic["size_kb"] * a["icache_per_kb"] + ic.get("ways", 0) * a["icache_per_way"]
    if tier["flash"].get("prefetch"):
        area += a["flash_prefetch_buffer"]
    area += tier["dma"]["channels"] * a["dma_per_channel"] + tier["dma"]["fifo_depth"] * a["dma_fifo_per_entry"]
    if tier["bus"]["width_bits"] == 64:
        area += a["bus_64bit_extra"]
    if tier["mac"]["lanes"]:
        area += a["mac_base"] + tier["mac"]["lanes"] * a["mac_per_lane"]
    return round(area, 2)


if __name__ == "__main__":
    tier = json.load(open(sys.argv[1]))
    out = render(tier, os.path.join(ROOT, "firmware", "link", "cm4_template.ld"))
    if len(sys.argv) > 2:
        open(sys.argv[2], "w").write(out)
        print("wrote", sys.argv[2])
    else:
        print(out)
    cost = json.load(open(os.path.join(ROOT, "tools", "cost", "cost_table.json")))
    print("tier", tier["name"], "area =", tier_area(tier, cost))
