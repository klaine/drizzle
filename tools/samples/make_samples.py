#!/usr/bin/env python3
"""Build Drizzle's factory samples (plan/04 section 6): samples/factory/*.wav and *.mp3.

    python3 tools/samples/make_samples.py            # fetch what is missing, build everything
    python3 tools/samples/make_samples.py --list     # what each sample is made of

Two kinds of material, both free of conditions:

- Recordings from VS Chamber Orchestra: Community Edition (VSCO 2 CE) by Versilian Studios, CC0 1.0.
  The files are fetched once from the pinned commit below into tools/samples/cache/ (git-ignored),
  decoded with ffmpeg to 44.1 kHz mono, measured for pitch, and cut, tuned and mixed here.
- A choir synthesised here: additive voices (a glottal source tilt, five vowel formants from
  published formant tables, vibrato, per-singer detune and drift, breath noise), four singers per part.
  Drizzle's own, so CC0 like the rest of the module's own material.

The outputs are committed: the build, the package and the web site use them as they are. The .wav
files (16-bit mono 44.1 kHz) go to the Move; the .mp3 files (96 kb/s, lower-case names) to the browser.
Needs ffmpeg and numpy.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import urllib.parse
import urllib.request
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, "tools", "samples", "cache")
OUT = os.path.join(ROOT, "samples", "factory")
FS = 44100

VSCO_COMMIT = "440300901dfe9275fd84e0b7763af1f8443ae62e"  # sgossner/VSCO-2-CE master, CC0 1.0
VSCO_RAW = "https://raw.githubusercontent.com/sgossner/VSCO-2-CE/" + VSCO_COMMIT + "/"

SRC = {
    "vln_C4": "Strings/Violin Section/susVib/VlnEns_susVib_C4_v2.wav",
    "vln_E4": "Strings/Violin Section/susVib/VlnEns_susVib_E4_v2.wav",
    "vln_G4": "Strings/Violin Section/susVib/VlnEns_susVib_G4_v2.wav",
    "vla_C4": "Strings/Viola Section/susvib/ViolaEns_susvib_C4_v2_1.wav",
    "vla_G4": "Strings/Viola Section/susvib/ViolaEns_susvib_G4_v2_1.wav",
    "cel_C3": "Strings/Cello Section/susvib/susvib_C3_v3_1.wav",
    "cel_G3": "Strings/Cello Section/susvib/susvib_G3_v3_1.wav",
    "trem_A3": "Strings/Violin Section/Trem/VlnEns_Trem_A3_v2.wav",
    "trem_E4": "Strings/Violin Section/Trem/VlnEns_Trem_E4_v2.wav",
    "horn_C3": "Brass/F Horn/sus/MOHorn_sus_C3_v3_1.wav",
    "flute_C5": "Woodwinds/Flute/susvib/LDFlute_susvib_C5_v1_1.wav",
}
PIZZ = ["G2", "B2", "D3", "F#3", "A3", "C4", "E4", "G4", "B4", "D5"]
for n in PIZZ:
    SRC["pizz_" + n] = "Strings/Violin Section/Pizz/VlnEns_Pizz_%s_v2_rr1.wav" % n
HARP = ["C3", "E3", "G3", "B3", "D4", "F4", "A4", "C5", "E5", "G5", "B5"]
for n in HARP:
    SRC["harp_" + n] = "Strings/Harp/KSHarp_%s_mf.wav" % n

# VSCO 2 CE names strings, horn and flute with middle C as "C3" (they sound an octave above their
# names); the harp uses the usual C4. make_samples measures every note anyway.
NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def midi(name):
    """'F#3' -> 54"""
    p, o = name[:-1], int(name[-1])
    return 12 * (o + 1) + NAMES[p]


# ---------------------------------------------------------------------------------------- sources

def fetch(key):
    path = os.path.join(CACHE, key + ".wav")
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        url = VSCO_RAW + urllib.parse.quote(SRC[key])
        print("fetch", SRC[key])
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
    return path


def load(key):
    """A source as float mono at 44.1 kHz (ffmpeg decodes any depth and rate)."""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", fetch(key), "-ac", "1", "-ar", str(FS), "-f", "f32le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def f0(x, lo=40.0, hi=1500.0):
    """The pitch of a steady stretch, by autocorrelation (the middle half of the note)."""
    n = len(x)
    seg = x[n // 4: n // 4 + min(n // 2, 32768)]
    seg = seg - seg.mean()
    spec = np.fft.rfft(seg * np.hanning(len(seg)), 2 * len(seg))
    ac = np.fft.irfft(np.abs(spec) ** 2)[: len(seg)]
    a, b = int(FS / hi), int(FS / lo)
    k = a + int(np.argmax(ac[a:b]))
    # the first peak within 85 % of the best one (avoids octave-low picks)
    for j in range(a, b):
        if ac[j] > 0.85 * ac[k] and ac[j] >= ac[j - 1] and ac[j] >= ac[j + 1]:
            k = j
            break
    y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
    d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
    return FS / (k + d)


def resample(x, ratio):
    """Play x `ratio` times faster (cubic interpolation; ratios here are within a semitone or two)."""
    n = int(len(x) / ratio)
    t = np.arange(n) * ratio
    i = np.floor(t).astype(int)
    f = t - i
    xp = np.concatenate([[x[0]], x, [x[-1], x[-1], x[-1]]])
    xm1, x0, x1, x2 = xp[i], xp[i + 1], xp[i + 2], xp[i + 3]
    c1 = 0.5 * (x1 - xm1)
    c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2
    c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1)
    return ((c3 * f + c2) * f + c1) * f + x0


def tuned(key, note):
    """A sustained source, measured and tuned to exactly `note` (MIDI)."""
    x = load(key)
    hz = f0(x)
    want = 440.0 * 2 ** ((note - 69) / 12)
    cents = 1200 * math.log2(hz / want)
    if abs(cents) > 60:
        raise SystemExit("%s: measured %.1f Hz, %.0f cents from note %d: check the source" % (key, hz, cents, note))
    return resample(x, want / hz), cents


def onset(x, thr=0.02):
    """Drop the silence before the note."""
    k = np.argmax(np.abs(x) > thr * np.max(np.abs(x)))
    return x[max(0, k - 64):]


def fade(x, a=0.01, b=0.05):
    x = x.copy()
    na, nb = int(a * FS), int(b * FS)
    if na:
        x[:na] *= np.linspace(0, 1, na) ** 2
    if nb:
        x[-nb:] *= np.linspace(1, 0, nb) ** 2
    return x


def seg(x, start, length):
    s = int(start * FS)
    return x[s: s + int(length * FS)]


def norm(x, peak=0.9):
    return x * (peak / max(1e-9, np.max(np.abs(x))))


# ------------------------------------------------------------------------------------- the choir

# Five formants per vowel and voice part: (Hz, dB, bandwidth Hz), from the widely published formant
# tables used by Csound's FOF examples.
FORMANTS = {
    "bass": {
        "a": [(600, 0, 60), (1040, -7, 70), (2250, -9, 110), (2450, -9, 120), (2750, -20, 130)],
        "e": [(400, 0, 40), (1620, -12, 80), (2400, -9, 100), (2800, -12, 120), (3100, -18, 120)],
        "i": [(250, 0, 60), (1750, -30, 90), (2600, -16, 100), (3050, -22, 120), (3340, -28, 120)],
        "o": [(400, 0, 40), (750, -11, 80), (2400, -21, 100), (2600, -20, 120), (2900, -40, 120)],
        "u": [(350, 0, 40), (600, -20, 80), (2400, -32, 100), (2675, -28, 120), (2950, -36, 120)],
    },
    "tenor": {
        "a": [(650, 0, 80), (1080, -6, 90), (2650, -7, 120), (2900, -8, 130), (3250, -22, 140)],
        "e": [(400, 0, 70), (1700, -14, 80), (2600, -12, 100), (3200, -14, 120), (3580, -20, 120)],
        "i": [(290, 0, 40), (1870, -15, 90), (2800, -18, 100), (3250, -20, 120), (3540, -30, 120)],
        "o": [(400, 0, 40), (800, -10, 80), (2600, -12, 100), (2800, -12, 120), (3000, -26, 120)],
        "u": [(350, 0, 40), (600, -20, 60), (2700, -17, 100), (2900, -14, 120), (3300, -26, 120)],
    },
    "alto": {
        "a": [(800, 0, 80), (1150, -4, 90), (2800, -20, 120), (3500, -36, 130), (4950, -60, 140)],
        "o": [(450, 0, 70), (800, -9, 80), (2830, -16, 100), (3500, -28, 130), (4950, -55, 135)],
        "u": [(325, 0, 50), (700, -12, 60), (2530, -30, 170), (3500, -40, 180), (4950, -64, 200)],
    },
    "soprano": {
        "a": [(800, 0, 80), (1150, -6, 90), (2900, -32, 120), (3900, -20, 130), (4950, -50, 140)],
        "o": [(450, 0, 70), (800, -11, 80), (2830, -22, 100), (3800, -22, 130), (4950, -50, 135)],
        "u": [(325, 0, 50), (700, -16, 60), (2700, -35, 170), (3800, -40, 180), (4950, -60, 200)],
    },
}


def envelope(freqs, vowel_mix):
    """Spectral envelope at `freqs` (array, Hz) for a weighted mix of formant sets."""
    out = np.zeros_like(freqs)
    for w, fm in vowel_mix:
        e = np.zeros_like(freqs)
        for hz, db, bw in fm:
            e += 10 ** (db / 20) / (1 + ((freqs - hz) / (0.5 * bw * 1.6)) ** 2)
        out += w * e
    return out


def singer(rng, part, note, dur, vowels, gain=1.0):
    """One singer: an additive voice whose harmonics follow the vowel's formants as they move."""
    n = int(dur * FS)
    t = np.arange(n) / FS
    base = 440.0 * 2 ** ((note - 69) / 12) * 2 ** (rng.uniform(-9, 9) / 1200)
    vib_rate, vib_depth = rng.uniform(4.8, 5.8), rng.uniform(0.25, 0.45)  # semitones
    vib_on = np.clip((t - rng.uniform(0.25, 0.6)) / 0.8, 0, 1)
    drift = np.cumsum(rng.standard_normal(n)) / FS
    drift = (drift - np.linspace(drift[0], drift[-1], n)) * 6.0  # a slow random walk, cents
    semis = vib_on * vib_depth * np.sin(2 * np.pi * vib_rate * t + rng.uniform(0, 6.3)) + drift / 100
    hz = base * 2 ** (semis / 12)
    phase = 2 * np.pi * np.cumsum(hz) / FS
    # the vowel path: a list of (time 0..1, vowel); weights interpolate between neighbours
    times = np.array([p[0] for p in vowels])
    sets = [FORMANTS[part][p[1]] for p in vowels]
    blocks = 64
    edges = np.linspace(0, n, blocks + 1).astype(int)
    y = np.zeros(n)
    kmax = int(5500 / base)
    for k in range(1, kmax + 1):
        amp = np.zeros(n)
        for b in range(blocks):
            a0, a1 = edges[b], edges[b + 1]
            u = (a0 + a1) / 2 / n
            j = int(np.clip(np.searchsorted(times, u) - 1, 0, len(times) - 2)) if len(times) > 1 else 0
            if len(times) > 1:
                w = np.clip((u - times[j]) / max(1e-6, times[j + 1] - times[j]), 0, 1)
                w = 0.5 - 0.5 * math.cos(math.pi * w)
                mix = [(1 - w, sets[j]), (w, sets[j + 1])]
            else:
                mix = [(1.0, sets[0])]
            amp[a0:a1] = envelope(np.array([k * base]), mix)[0]
        amp = np.convolve(amp, np.ones(512) / 512, mode="same")  # no steps between blocks
        tilt = 1.0 / k ** 1.1
        y += amp * tilt * np.sin(k * phase + rng.uniform(0, 6.3))
    # breath: noise shaped by the first vowel's formants, a little
    noise = rng.standard_normal(n)
    spec = np.fft.rfft(noise)
    fr = np.fft.rfftfreq(n, 1 / FS)
    spec *= envelope(fr, [(1.0, sets[0])]) * (fr > 800)
    breath = np.fft.irfft(spec, n)
    y += 0.04 * breath / max(1e-9, np.max(np.abs(breath))) * np.max(np.abs(y))
    att, rel = rng.uniform(0.25, 0.45), 0.4
    env = np.clip(t / att, 0, 1) ** 1.5 * np.clip((dur - t) / rel, 0, 1)
    return gain * y * env


def choir(seed, parts, dur, singers=4):
    """parts: [(part, MIDI note, vowel path, gain)]"""
    rng = np.random.default_rng(seed)
    y = np.zeros(int(dur * FS))
    for part, note, vowels, gain in parts:
        for _ in range(singers):
            y += singer(rng, part, note, dur, vowels, gain)
    return y


def room(x, seed=7, mix=0.18, decay=1.6):
    """A small diffuse tail (exponentially decaying noise), so the choir sits in a space."""
    rng = np.random.default_rng(seed)
    n = int(decay * FS)
    ir = rng.standard_normal(n) * np.exp(-6.9 * np.arange(n) / n)
    ir[: int(0.012 * FS)] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    wet = np.fft.irfft(np.fft.rfft(x, len(x) + n) * np.fft.rfft(ir, len(x) + n))[: len(x)]
    return x + mix * wet * (np.std(x) / max(1e-9, np.std(wet)))


# ------------------------------------------------------------------------------------- the bank

def build():
    """name -> (samples, root MIDI note, description)"""
    out = {}

    cel, _ = tuned("cel_C3", 60)
    cel5, _ = tuned("cel_G3", 67)
    vla, _ = tuned("vla_C4", 72)
    vln_e, _ = tuned("vln_E4", 76)
    vln_g, _ = tuned("vln_G4", 79)
    L = 5.0
    chord = sum(seg(onset(x), 0.15, L) * g for x, g in ((cel, 1.0), (cel5, 0.8), (vla, 0.8), (vln_e, 0.8), (vln_g, 0.7)))
    out["Strings Chord"] = (fade(norm(chord), 0.08, 0.4), 60, "string sections on C major: cellos C4 G4, violas C5, violins E5 G5")

    out["Violins E5"] = (fade(norm(seg(onset(vln_e), 0.1, 5.0)), 0.04, 0.3), 76, "violin section, sustained with vibrato")
    out["Cellos C4"] = (fade(norm(seg(onset(cel), 0.1, 5.0)), 0.04, 0.3), 60, "cello section, sustained with vibrato")

    ta, _ = tuned("trem_A3", 69)
    te, _ = tuned("trem_E4", 76)
    trem = seg(onset(ta), 0.05, 4.5) + 0.8 * seg(onset(te), 0.05, 4.5)
    out["Tremolo Strings"] = (fade(norm(trem), 0.02, 0.3), 69, "violin section tremolo on A4 and E5")

    step = 0.42
    walk = np.zeros(int((step * len(PIZZ) + 1.2) * FS))
    for i, nm in enumerate(PIZZ):
        x = onset(load("pizz_" + nm))[: int(1.2 * FS)]
        x = fade(norm(x), 0.0, 0.3)
        a = int(i * step * FS)
        walk[a: a + len(x)] += x
    out["Pizzicato Walk"] = (fade(norm(walk), 0.0, 0.3), 55, "violin pizzicato, G3 up to D6, one note every 0.42 s")

    gl = np.zeros(int(4.6 * FS))
    for i, nm in enumerate(HARP):
        x = onset(load("harp_" + nm))[: int(3.4 * FS)]
        x = fade(norm(x) * (0.95 ** i), 0.0, 0.6)
        a = int(i * 0.11 * FS)
        gl[a: a + len(x)] += x[: len(gl) - a]
    out["Harp Glissando"] = (fade(norm(gl), 0.0, 0.5), 48, "harp notes C3 to B5, 0.11 s apart, ringing")

    # the orchestra, every instrument on C: Position walks from the cellos to the flute
    horn, _ = tuned("horn_C3", 60)
    flute, _ = tuned("flute_C5", 84)
    vln_c, _ = tuned("vln_C4", 72)
    tour_parts = [cel, vla, vln_c, horn, flute]
    piece, xf = 1.1, 0.25
    tour = np.zeros(int((len(tour_parts) * (piece - xf) + xf) * FS))
    for i, x in enumerate(tour_parts):
        p = norm(seg(onset(x), 0.2, piece))
        p = fade(p, xf if i else 0.02, xf)
        a = int(i * (piece - xf) * FS)
        tour[a: a + len(p)] += p
    out["Orchestra Tour"] = (fade(norm(tour), 0.02, 0.2), 60, "cellos, violas, violins, horn, flute, all on C, 0.85 s each")

    # the choir (synthesised here)
    satb = [("bass", 48, [(0, "a")], 1.0), ("tenor", 55, [(0, "a")], 0.8), ("alto", 64, [(0, "a")], 0.7), ("soprano", 72, [(0, "a")], 0.6)]
    ah = choir(11, satb, 5.0)
    out["Choir Ah"] = (fade(norm(room(ah)), 0.05, 0.3), 48, "synthesised choir, C major, 'ah', four singers a part")

    path = [(0.0, "a"), (0.25, "e"), (0.5, "i"), (0.75, "o"), (1.0, "u")]
    vow = choir(23, [("bass", 48, path, 1.0), ("tenor", 48, path, 0.9)], 5.0)
    out["Choir Vowels"] = (fade(norm(room(vow)), 0.05, 0.3), 48, "synthesised men's choir on C3, a-e-i-o-u across the file")

    oo = choir(31, [("alto", 60, [(0, "u"), (1, "o")], 1.0), ("soprano", 67, [(0, "u"), (1, "o")], 0.8)], 5.0)
    out["Choir Oo"] = (fade(norm(room(oo)), 0.05, 0.3), 60, "synthesised women's choir, C4 and G4, 'oo' opening to 'oh'")
    return out


def slug(name):
    """The browser's copy: "Choir Ah" -> choir_ah.mp3 (src/web/app.js derives the same name)."""
    return "".join(c if c.isalnum() else "_" for c in name.lower())


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767), -32767, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    bank = build()
    os.makedirs(OUT, exist_ok=True)
    index = []
    for name, (x, root, desc) in bank.items():
        if args.list:
            print("%-16s root %3d  %.1f s  %s" % (name, root, len(x) / FS, desc))
            continue
        wav = os.path.join(OUT, name + ".wav")
        write_wav(wav, x)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wav, "-codec:a", "libmp3lame", "-b:a", "96k", "-ac", "1",
                        os.path.join(OUT, slug(name) + ".mp3")], check=True)
        index.append({"name": name, "file": name + ".wav", "root": root, "seconds": round(len(x) / FS, 2), "about": desc,
                      "source": "VSCO 2 CE (CC0 1.0)" if not name.startswith("Choir") else "synthesised by make_samples.py (CC0 1.0)"})
        print("wrote %s (%.1f s, root %d)" % (name, len(x) / FS, root))
    if not args.list:
        with open(os.path.join(OUT, "index.json"), "w") as f:
            json.dump(index, f, indent=1)
            f.write("\n")


if __name__ == "__main__":
    sys.exit(main())
