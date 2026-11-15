#!/usr/bin/env python3
"""Compare the golden fingerprints (plan/13 section 3) with tests/golden/golden.json.

  tests/golden/check_golden.py build/drizzle_golden           check
  tests/golden/check_golden.py build/drizzle_golden --write   store the current renders as the reference

A slice fails when its level differs by more than 1 dB (above -60 dBFS) or its zero-crossing rate by
more than 8 % (above -40 dBFS). An intended sound change needs --write and the reason in the change
description; never rewrite the reference just to make a failure go away.
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "golden.json")

exe = sys.argv[1]
cur = json.loads(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)
if "--write" in sys.argv:
    with open(REF, "w", encoding="utf-8") as f:
        json.dump(cur, f, indent=0, sort_keys=False)
        f.write("\n")
    print("golden: wrote %d cases to %s" % (len(cur), os.path.relpath(REF)))
    sys.exit(0)
ref = json.load(open(REF, encoding="utf-8"))
bad = []
for name in sorted(set(ref) | set(cur)):
    if name not in cur:
        bad.append("%s: missing from the renders" % name)
        continue
    if name not in ref:
        bad.append("%s: new case, not in golden.json (run with --write)" % name)
        continue
    for i, (a, b) in enumerate(zip(ref[name], cur[name])):
        for ch in (0, 1):
            if max(a[ch], b[ch]) > -60 and abs(a[ch] - b[ch]) > 1.0:
                bad.append("%s: slice %d channel %d level %.2f dB, reference %.2f dB" % (name, i, ch, b[ch], a[ch]))
        if max(a[0], b[0]) > -40 and abs(a[2] - b[2]) > 0.08 * max(a[2], 1e-4):
            bad.append("%s: slice %d zero-crossing rate %.4f, reference %.4f" % (name, i, b[2], a[2]))
if bad:
    print("golden FAILED:")
    for b in bad[:40]:
        print("  - " + b)
    sys.exit(1)
print("golden ok: %d cases" % len(cur))
