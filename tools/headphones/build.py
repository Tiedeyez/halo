#!/usr/bin/env python3
"""Builds src/data/headphones.json from OPRA's database_v1.jsonl.

OPRA (https://github.com/opra-project/OPRA) is CC BY-SA 4.0 and includes
AutoEQ's data (MIT). The output is an adaptation of OPRA's dataset and stays
under CC BY-SA 4.0; see src/data/HEADPHONES-LICENSE.md.

Keeps one parametric correction per product: the most trusted measurement
(oratory1990, then crinacle, Rtings, Innerfidelity, then any), and only
corrections CymtX's engine can run (peaks and shelves, at most 16 bands).

usage: python3 -I tools/headphones/build.py database_v1.jsonl src/data/headphones.json"""
import json, sys

PREFERENCE = ["oratory1990", "crinacle", "Rtings", "Innerfidelity"]
TYPES = {"peak_dip": 0, "low_shelf": 1, "high_shelf": 2}

def rank(details):
    for i, name in enumerate(PREFERENCE):
        if name.lower() in details.lower():
            return i
    return len(PREFERENCE)

vendors, products, eqs = {}, {}, {}
with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        d = json.loads(line)
        if d["type"] == "vendor":
            vendors[d["id"]] = d["data"]["name"]
        elif d["type"] == "product":
            products[d["id"]] = d["data"]
        elif d["type"] == "eq" and d["data"].get("type") == "parametric_eq":
            vendor, rest = d["id"].split(":", 1)
            product = vendor + "::" + rest.split("::", 1)[0]
            eqs.setdefault(product, []).append(d["data"])

out = []
for pid, p in sorted(products.items()):
    usable = [e for e in eqs.get(pid, [])
              if all(b["type"] in TYPES for b in e["parameters"]["bands"]) and 0 < len(e["parameters"]["bands"]) <= 16]
    if not usable:
        continue
    best = min(usable, key=lambda e: rank(e.get("details", "")))
    bands = [[TYPES[b["type"]], round(b["frequency"], 1), round(b["gain_db"], 2), round(b["q"], 3)]
             for b in best["parameters"]["bands"]]
    source = best.get("details", "").replace("Measured by ", "") or best.get("author", "")
    out.append([vendors.get(p["vendor_id"], ""), p["name"], p.get("subtype", ""), source,
                round(best["parameters"].get("gain_db", 0), 2), bands])

with open(sys.argv[2], "w", encoding="utf-8") as f:
    json.dump({"source": "OPRA database_v1 (CC BY-SA 4.0), including AutoEQ (MIT)",
               "format": "[vendor, product, subtype, measured by, preamp dB, [[type 0 peak|1 low shelf|2 high shelf, Hz, dB, Q]]]",
               "headphones": out}, f, ensure_ascii=False, separators=(",", ":"))
print(len(out), "headphones")
