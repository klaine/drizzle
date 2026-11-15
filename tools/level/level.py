#!/usr/bin/env python3
"""Level the factory bank (plan/12 section 4): set each patch's patch_vol so its held C minor chord
sits at the INIT patch's level.

  tools/level/level.py build/drizzle_golden          report each patch's level
  tools/level/level.py build/drizzle_golden --write  adjust presets/factory/*.json (run gen.py after)

The level is the mean of the four loudest 250 ms slices (RMS, both channels) of the golden render's
first 3 s (tests/golden), in dBFS: a held pad and a decaying pluck or a sparse rhythm compare by how
loud they sound when they play, not by their average. patch_vol scales the output as 4 * vol^2, so a change of d dB is vol * 10^(d / 40). The master's
soft clip bends that a little near full scale: run --write until the report settles (twice is enough).
"""
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOL = 1.5  # dB: the master's soft clip bends the loudest patches a little
PEAK = -4.0  # dBFS (random grains can coincide: margin): no patch peaks above this, even if it then stays below the target level


def levels(exe):
    """{name: (level dB, peak dBFS)} from the golden renders."""
    cur = json.loads(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)
    out = {}
    for name, slices in cur.items():
        if not name.startswith("patch "):
            continue
        loud = sorted(((s[0] + s[1]) / 2 for s in slices[:12]), reverse=True)[:4]
        out[name.split(" ", 2)[2]] = (sum(loud) / len(loud), max(s[3] for s in slices))
    return out


def main():
    exe = sys.argv[1]
    lv = levels(exe)
    target = lv["Init"][0]
    files = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "presets", "factory", "*.json"))):
        d = json.load(open(path, encoding="utf-8"))
        files[d["name"]] = (path, d)
    off = 0
    for name, (l, pk) in lv.items():
        d = l - target
        d = max(d, pk - PEAK)  # a peaky patch is held below the ceiling instead
        capped = pk - PEAK >= l - target - 0.01 and l - target < -TOL
        flag = "" if abs(d) <= TOL else "  <-"
        off += abs(d) > TOL
        print("%-20s %6.1f dB  peak %5.1f  %+5.1f%s%s" % (name, l, pk, l - target, flag, "  (peak-limited)" if capped else ""))
        if "--write" in sys.argv and name in files and abs(d) > 0.3 and not (capped and abs(pk - PEAK) < 0.5):
            path, data = files[name]
            vol = float(data["values"].get("patch_vol", 0.5))
            vol = max(0.05, min(1.0, vol * 10 ** (-d / 40.0)))
            data["values"]["patch_vol"] = round(vol, 3)
            with open(path, "w", encoding="utf-8") as f:
                f.write(json.dumps(data, indent=2) + "\n")
    print("target %.1f dB (Init); %d patches more than %.0f dB off" % (target, off, TOL))
    return off


if __name__ == "__main__":
    sys.exit(1 if main() and "--write" not in sys.argv else 0)
