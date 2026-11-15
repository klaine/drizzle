"""Drizzle parameter definitions: the single source of truth (plan/10-parameter-reference.md).

gen.py turns this file into the C descriptor table (src/dsp/params_gen.*), the chain_params and
ui_hierarchy contract strings (src/dsp/contract_gen.c), the window tables (src/dsp/windows_gen.c),
the factory bank (src/dsp/factory_presets.c) and the web UI metadata (build/ui/*.json).

Rules (plan/10):
  * Enum option order is append-only and keys are never renamed once released: indices are stored
    in patches.
  * Short names are at most 5 characters; short enum options at most 4.
  * Every key of plan/10 is here from M1 on; keys whose feature comes later are stored but inert
    (plan/16 lists them). A Move page appears once its milestone is built (LEVELS, MILESTONE).
"""

MILESTONE = 5  # pages of later milestones are left out of ui_hierarchy

# --- enum option lists (append only) -----------------------------------------------------------
import json as _json
import os as _os

OFF_ON = ["Off", "On"]
# Amino's wavetable names, in bank order (tools/wavegen/manifest.json). The table knobs are indices
# (two 74-name enums would cost chain_params space); the cards and the web UI name them.
with open(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", "wavegen", "manifest.json")) as _f:
    TABLE_NAMES = [t["name"] for t in _json.load(_f)["tables"]]
SRC_TYPES = ["Wavetable", "Analog", "Sample"]
BUF_MODES = ["Live", "Scan"]
AN_WAVES = ["Sine", "Tri", "Saw", "Pulse"]
WT_XMODS = ["Off", "Ring", "PM", "Sync"]
WT_QUALITY = ["Vintage", "Clean"]
SW_MODES = ["One", "Loop", "Bounce"]
SL_WAVES = ["Sine", "Tri", "Saw", "Square", "S&H", "Smooth"]
SL_SYNC = ["Off", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/4D", "1/2", "1 bar", "2 bar", "4 bar", "8 bar"]
GR_WINDOWS = ["Hann", "Gauss", "Tukey", "Perc", "Swell", "Rect"]
GR_PQ = ["Off", "Oct", "Fifth", "Scale"]
GR_MODES = ["Cloud", "Tempo"]
GR_DIVS = ["1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D",
           "1/2", "1 bar"]
GR_RANDOM = ["Free", "Note", "Bar"]
V_MODES = ["Poly", "Mono", "Legato"]
FLT_TYPES = ["Off", "LP24", "LP12", "BP12", "HP12"]
ME_TRIG = ["Note", "First"]
LFO_WAVES = ["Sine", "Tri", "Saw Up", "Saw Down", "Square", "S&H", "Smooth"]
LFO_SYNC = ["Off", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2",
            "1/2D", "1 bar", "2 bar", "3 bar", "4 bar", "8 bar", "16 bar"]
L1_TRIG = ["Free", "Note", "Note+Random"]
MOD_SOURCES = ["None", "Amp Env", "Mod Env", "LFO 1", "LFO 2", "Source LFO", "Sweep", "Velocity", "Rel Velocity",
               "Key", "Poly AT", "Chan AT", "Pressure", "Mod Wheel", "Breath", "Expression", "Sustain", "Bend",
               "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Random", "Voice Index"]
MOD_SOURCE_SHORT = ["--", "AEnv", "MEnv", "LFO1", "LFO2", "SLFO", "Swp", "Vel", "RVel", "Key", "PAT", "CAT", "Prs",
                    "Whl", "Brth", "Expr", "Sus", "Bend", "M1", "M2", "M3", "M4", "Rnd", "Vox"]
MOD_DESTS = ["None", "Position", "Spray", "Size", "Density", "Jitter", "Scan", "Pitch", "Pitch Spray", "Reverse",
             "Spread", "Probability", "Level Spray", "Lock", "Timbre", "Fold", "Tone", "Source Pitch", "Osc B Level",
             "Cross-mod", "Cutoff", "Resonance", "Filter Drive", "Level", "Pan", "LFO 1 Rate", "LFO 2 Rate",
             "S.LFO Rate", "Crush", "Chorus Level", "Delay Feedback", "Reverb Level", "Reverb Size"]
MOD_DEST_SHORT = ["--", "Pos", "Spry", "Size", "Dens", "Jit", "Scan", "Ptch", "PSpr", "Rev", "Sprd", "Prob", "LvSp",
                  "Lock", "Timb", "Fold", "Tone", "SPit", "LvlB", "XMod", "Cut", "Res", "Drv", "Lvl", "Pan", "L1Rt",
                  "L2Rt", "SLRt", "Crsh", "ChLv", "DlFb", "RvLv", "RvSz"]
assert len(MOD_SOURCES) == len(MOD_SOURCE_SHORT) == 24
assert len(MOD_DESTS) == len(MOD_DEST_SHORT) == 33  # None + the 32 destinations of plan/08
MM_SLOTS = 12
CR_TYPES = ["Soft", "Hard", "Fold", "Bits"]
CHORUS_TYPES = ["Chorus 1", "Chorus 2", "Ensemble"]
DELAY_SYNC = ["Off", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
              "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/1T", "1/2D", "1 bar"]
DELAY_MODES = ["Stereo", "PingPong", "Mono"]
DELAY_RATIOS = ["1/1", "4/3", "3/4", "3/2", "2/3", "2/1", "1/2", "3/1", "1/3", "4/1", "1/4"]  # left : right time
REVERB_TYPES = ["Room", "Hall", "Plate", "Cloud"]

SHORT_OPT = {
    "Wavetable": "WT", "Analog": "Anlg", "Sample": "Smpl", "Pulse": "Pls", "Sine": "Sin", "Vintage": "Vint",
    "Clean": "Cln", "Bounce": "Bnce", "Square": "Sqr", "Smooth": "Smth", "Gauss": "Gaus", "Tukey": "Tuky",
    "Swell": "Swel", "Fifth": "5th", "Scale": "Scl", "Cloud": "Cld", "Tempo": "Tmpo", "Legato": "Leg",
    "First": "Frst", "Saw Up": "SawU", "Saw Down": "SawD", "Note+Random": "NRnd", "Chorus 1": "Cho1",
    "Chorus 2": "Cho2", "Ensemble": "Ens", "PingPong": "PngP", "Stereo": "Ster", "Plate": "Plt", "Room": "Room",
}
for _o in set(DELAY_SYNC[1:] + GR_DIVS + LFO_SYNC[1:] + SL_SYNC[1:]):
    if _o.endswith("bar"):
        SHORT_OPT[_o] = "Bar" if _o == "1 bar" else _o.split()[0] + "br"   # "4 bar" -> "4br"
    elif len(_o) > 4:
        SHORT_OPT[_o] = _o[2:]                                             # "1/16T" -> "16T"


def short_opts(opts):
    if opts is MOD_SOURCES:
        return list(MOD_SOURCE_SHORT)
    if opts is MOD_DESTS:
        return list(MOD_DEST_SHORT)
    return [SHORT_OPT.get(o, o[:4]).strip() for o in opts]


PARAMS = []


def P(key, name, short, kind, mn, mx, default, opts=None, unit=None, step=None, viz=None,
      cp=True, state=True, live=False):
    """cp: in chain_params (a knob or automation target on Move); state: saved in patches;
    live: the knob ring shows the modulated value through <key>:effective (plan/08 section 6)."""
    assert len(short) <= 5, (key, short)
    if kind == "enum":
        mn, mx = 0, len(opts) - 1
        if isinstance(default, str):
            default = opts.index(default)
    if step is None:
        step = 1 if kind in ("int", "enum") else 0.01
    PARAMS.append(dict(key=key, name=name, short=short, kind=kind, mn=mn, mx=mx, default=default,
                       opts=opts, unit=unit, step=step, viz=viz, cp=cp, state=state, live=live))


# --- global and buffer (plan/05) -----------------------------------------------------------------
P("patch_vol", "Volume", "Vol", "float", 0, 1, 0.5)  # gain 4 vol^2: 0.5 = unity, 1 = +12 dB
P("buf_mode", "Buffer Mode", "Mode", "enum", 0, 0, "Scan", opts=BUF_MODES)
P("buf_len", "Buffer Length", "Len", "float", 0, 1, 0.75)          # 0.5..8 s exp: 0.75 = 4 s
P("buf_freeze", "Freeze", "Frz", "enum", 0, 0, "Off", opts=OFF_ON, viz={"kind": "switch"})

# --- source, common (plan/04 section 1) ---------------------------------------------------------
P("src_type", "Source", "Src", "enum", 0, 0, "Wavetable", opts=SRC_TYPES)
P("src_root", "Source Root", "Root", "int", 24, 96, 48)
P("src_track", "Track Notes", "Track", "enum", 0, 0, "Off", opts=OFF_ON)
P("src_fold", "Fold", "Fold", "float", 0, 1, 0.0)
P("src_tone", "Tone", "Tone", "float", 0, 1, 1.0)
P("src_seed", "Source Seed", "Seed", "int", 0, 999, 0)

# --- Wavetable source (plan/04 section 2, M2) ---------------------------------------------------
P("wt_a_table", "Table A", "TblA", "int", 0, len(TABLE_NAMES) - 1, 0)
P("wt_a_wave", "Wave A", "WavA", "int", 0, 63, 0)
P("wt_b_table", "Table B", "TblB", "int", 0, len(TABLE_NAMES) - 1, 0)
P("wt_b_wave", "Wave B", "WavB", "int", 0, 63, 0)
P("wt_b_semi", "Osc B Semi", "Semi", "int", -24, 24, 0)
P("wt_b_fine", "Osc B Fine", "Fine", "float", -50, 50, 0.0, unit="ct")
P("wt_b_lvl", "Osc B Level", "LvlB", "float", 0, 1, 0.0)
P("wt_xmod", "Cross-mod", "XMod", "enum", 0, 0, "Off", opts=WT_XMODS)
P("wt_xamt", "Cross-mod Amount", "XAmt", "float", 0, 1, 0.0)
P("wt_q", "Quality", "Qual", "enum", 0, 0, "Vintage", opts=WT_QUALITY)

# --- Analog source (plan/04 section 3) ----------------------------------------------------------
P("an_a_wave", "Wave A", "WavA", "enum", 0, 0, "Saw", opts=AN_WAVES)
P("an_a_shape", "Shape A", "ShpA", "float", 0, 1, 0.0)
P("an_b_wave", "Wave B", "WavB", "enum", 0, 0, "Pulse", opts=AN_WAVES)
P("an_b_shape", "Shape B", "ShpB", "float", 0, 1, 0.5)
P("an_b_semi", "Osc B Semi", "Semi", "int", -24, 24, 0)
P("an_b_fine", "Osc B Fine", "Fine", "float", -50, 50, 7.0, unit="ct")
P("an_b_lvl", "Osc B Level", "LvlB", "float", 0, 1, 0.0)
P("an_dens", "Density", "Dens", "float", 0, 1, 0.0)
P("an_ddet", "Density Detune", "DDet", "float", 0, 1, 0.3)
P("an_fm", "FM B to A", "FM", "float", 0, 1, 0.0)
P("an_sync", "Sync", "Sync", "float", 0, 1, 0.0)
P("an_noise", "Noise", "Noise", "float", 0, 1, 0.0)

# --- source motion (plan/04 section 4) ------------------------------------------------------------
P("sw_time", "Sweep Time", "Time", "float", 0, 1, 0.5)             # Live: 0.1..60 s exp
P("sw_curve", "Sweep Curve", "Curve", "float", -1, 1, 0.0)
P("sw_mode", "Sweep Mode", "Mode", "enum", 0, 0, "One", opts=SW_MODES)
P("sw_timb", "Sweep to Timbre", "Timb", "float", -1, 1, 1.0)
P("sw_fold", "Sweep to Fold", "Fold", "float", -1, 1, 0.0)
P("sw_pitch", "Sweep to Pitch", "Pitch", "float", -24, 24, 0.0, unit="st")
P("sw_tone", "Sweep to Tone", "Tone", "float", -1, 1, 0.0)
P("sl_wave", "Source LFO Wave", "Wave", "enum", 0, 0, "Sine", opts=SL_WAVES)
P("sl_rate", "Source LFO Rate", "Rate", "float", 0, 1, 0.3)        # 0.01..40 Hz exp
P("sl_sync", "Source LFO Sync", "Sync", "enum", 0, 0, "Off", opts=SL_SYNC)
P("sl_timb", "S.LFO to Timbre", "Timb", "float", -1, 1, 0.0)
P("sl_fold", "S.LFO to Fold", "Fold", "float", -1, 1, 0.0)
P("sl_pitch", "S.LFO to Pitch", "Pitch", "float", -12, 12, 0.0, unit="st")
P("sl_tone", "S.LFO to Tone", "Tone", "float", -1, 1, 0.0)

# --- grains (plan/06) -----------------------------------------------------------------------------
P("gr_pos", "Position", "Pos", "float", 0, 1, 0.5, live=True)
P("gr_spray", "Spray", "Spray", "float", 0, 1, 0.1)
P("gr_size", "Size", "Size", "float", 0, 1, 0.55, live=True)        # Cloud 5 ms..2 s exp
P("gr_dens", "Density", "Dens", "float", 0, 1, 0.5, live=True)      # 0.5..250 /s exp
P("gr_jit", "Jitter", "Jit", "float", 0, 1, 0.5)
P("gr_scan", "Scan Speed", "Scan", "float", -4, 4, 0.0)
P("gr_win", "Window", "Win", "enum", 0, 0, "Hann", opts=GR_WINDOWS)
P("gr_lock", "Period Lock", "Lock", "float", 0, 1, 0.0)
P("gr_pitch", "Grain Pitch", "Pitch", "int", -24, 24, 0, unit="st")
P("gr_fine", "Grain Fine", "Fine", "float", -50, 50, 0.0, unit="ct")
P("gr_pspr", "Pitch Spray", "PSpr", "float", 0, 24, 0.0, unit="st")
P("gr_pq", "Pitch Quantise", "PQnt", "enum", 0, 0, "Oct", opts=GR_PQ)
P("gr_rev", "Reverse", "Rev", "float", 0, 1, 0.0)
P("gr_spread", "Stereo Spread", "Sprd", "float", 0, 1, 0.5)
P("gr_lspr", "Level Spray", "LvSpr", "float", 0, 1, 0.0)
P("gr_mode", "Grain Mode", "Mode", "enum", 0, 0, "Cloud", opts=GR_MODES)
P("gr_div", "Division", "Div", "enum", 0, 0, "1/16", opts=GR_DIVS)
P("gr_swing", "Swing", "Swing", "float", 0, 0.75, 0.0)
P("gr_prob", "Probability", "Prob", "float", 0, 1, 1.0)
P("gr_rpt", "Repeats", "Rpt", "int", 0, 16, 0)
P("gr_rnd", "Random", "Rand", "enum", 0, 0, "Free", opts=GR_RANDOM)
P("gr_seed", "Grain Seed", "Seed", "int", 0, 999, 0)
P("gr_relth", "Release Thinning", "RThin", "enum", 0, 0, "On", opts=OFF_ON)

# --- voice, filter, envelopes (plan/07, plan/08 section 1) ----------------------------------------
P("v_mode", "Voice Mode", "Mode", "enum", 0, 0, "Poly", opts=V_MODES)
P("v_voices", "Voices", "Voics", "int", 1, 8, 8)
P("v_glide", "Glide", "Glide", "float", 0, 1, 0.0)
P("v_bend", "Bend Range", "Bend", "int", 0, 24, 2, unit="st")
P("v_oct", "Octave", "Oct", "int", -2, 2, 0)
P("v_flav", "Flavour", "Flav", "float", 0, 1, 0.3)
P("v_pan", "Pan", "Pan", "float", -1, 1, 0.0)
P("flt_type", "Filter Type", "Type", "enum", 0, 0, "Off", opts=FLT_TYPES)
P("flt_cut", "Cutoff", "Cut", "float", 0, 1, 0.8, live=True)
P("flt_res", "Resonance", "Res", "float", 0, 1, 0.1)
P("flt_drive", "Filter Drive", "Drive", "float", 0, 1, 0.0)
P("flt_env", "Filter Env", "Env", "float", -1, 1, 0.0)
P("flt_key", "Key Track", "KeyTk", "float", 0, 1, 0.5)
P("flt_vel", "Filter Velocity", "Vel", "float", 0, 1, 0.0)
# Envelope knobs use Crest's laws (env.c): 0.5 ms * (tmax / 0.5 ms)^x, release bent by x^0.6.
P("ae_a", "Amp Attack", "Atk", "float", 0, 1, 0.604)   # 0.3 s
P("ae_d", "Amp Decay", "Dec", "float", 0, 1, 0.646)    # 0.5 s
P("ae_s", "Amp Sustain", "Sus", "float", 0, 1, 0.8)
P("ae_r", "Amp Release", "Rel", "float", 0, 1, 0.578)  # 1.2 s
P("ae_vel", "Amp Velocity", "AVel", "float", 0, 1, 0.5)
P("me_a", "Mod Attack", "Atk", "float", 0, 1, 0.783)   # 2 s
P("me_d", "Mod Decay", "Dec", "float", 0, 1, 0.776)
P("me_s", "Mod Sustain", "Sus", "float", 0, 1, 0.5)
P("me_r", "Mod Release", "Rel", "float", 0, 1, 0.642)
P("me_vel", "Mod Velocity", "MVel", "float", 0, 1, 0.0)
P("me_trig", "Mod Env Trigger", "MTrig", "enum", 0, 0, "Note", opts=ME_TRIG)
P("me_pos", "Mod Env to Pos", "Pos", "float", -1, 1, 0.0)
P("me_size", "Mod Env to Size", "Size", "float", -1, 1, 0.0)
P("me_dens", "Mod Env to Dens", "Dens", "float", -1, 1, 0.0)
P("me_pitch", "Mod Env to Pitch", "Pitch", "float", -24, 24, 0.0, unit="st")

# --- LFOs, pressure, macros (plan/08) -------------------------------------------------------------
for _n, _w, _r in ((1, "Sine", 0.4), (2, "Tri", 0.2)):
    P("l%d_wave" % _n, "LFO %d Wave" % _n, "Wave", "enum", 0, 0, _w, opts=LFO_WAVES)
    P("l%d_rate" % _n, "LFO %d Rate" % _n, "Rate", "float", 0, 1, _r)
    P("l%d_sync" % _n, "LFO %d Sync" % _n, "Sync", "enum", 0, 0, "Off", opts=LFO_SYNC)
P("l1_trig", "LFO 1 Trigger", "Trig", "enum", 0, 0, "Note", opts=L1_TRIG)
P("l2_phase", "LFO 2 Phase", "Phase", "float", 0, 1, 0.0)
P("l1_pos", "LFO 1 to Pos", "Pos", "float", -1, 1, 0.0)
P("l1_size", "LFO 1 to Size", "Size", "float", -1, 1, 0.0)
P("l1_pitch", "LFO 1 to Pitch", "Pitch", "float", -12, 12, 0.0, unit="st")
P("l1_cut", "LFO 1 to Cutoff", "Cut", "float", -1, 1, 0.0)
P("l2_pos", "LFO 2 to Pos", "Pos", "float", -1, 1, 0.0)
P("l2_dens", "LFO 2 to Dens", "Dens", "float", -1, 1, 0.0)
P("l2_spray", "LFO 2 to Spray", "Spray", "float", -1, 1, 0.0)
P("l2_pan", "LFO 2 to Pan", "Pan", "float", -1, 1, 0.0)
P("at_pos", "Pressure to Pos", "Pos", "float", -1, 1, 0.0)
P("at_dens", "Pressure to Dens", "Dens", "float", -1, 1, 0.3)
P("at_spray", "Pressure to Spray", "Spray", "float", -1, 1, 0.2)
P("at_size", "Pressure to Size", "Size", "float", -1, 1, 0.0)
P("at_pitch", "Pressure to PSpray", "PSpr", "float", -1, 1, 0.0)
P("at_cut", "Pressure to Cutoff", "Cut", "float", -1, 1, 0.0)
for _m in range(1, 5):
    P("mac%d" % _m, "Macro %d" % _m, "M%d" % _m, "float", 0, 1, 0.0)

# --- matrix slots (plan/08 section 4, M3): stored, edited through proxies that arrive with M3 ------
for _s in range(1, MM_SLOTS + 1):
    P("mm%02d_src" % _s, "Slot %d Source" % _s, "Src", "enum", 0, 0, "None", opts=MOD_SOURCES, cp=False)
    P("mm%02d_via" % _s, "Slot %d Via" % _s, "Via", "enum", 0, 0, "None", opts=MOD_SOURCES, cp=False)
    P("mm%02d_dst" % _s, "Slot %d Dest" % _s, "Dest", "enum", 0, 0, "None", opts=MOD_DESTS, cp=False)
    P("mm%02d_dep" % _s, "Slot %d Depth" % _s, "Depth", "float", -1, 1, 0.0, cp=False)

# the list page's selector and the Matrix Edit proxies of the selected slot (params.c): never saved
P("mm_sel", "Matrix Slot Select", "Sel", "int", 0, MM_SLOTS - 1, 0, cp=False, state=False)
P("mm_slot", "Matrix Slot", "Slot", "int", 1, MM_SLOTS, 1, state=False)
P("mm_src", "Matrix Source", "Src", "enum", 0, 0, "None", opts=MOD_SOURCES, state=False)
P("mm_via", "Matrix Via", "Via", "enum", 0, 0, "None", opts=MOD_SOURCES, state=False)
P("mm_dst", "Matrix Dest", "Dest", "enum", 0, 0, "None", opts=MOD_DESTS, state=False)
P("mm_dep", "Matrix Depth", "Depth", "float", -1, 1, 0.0, state=False)
P("mm_clear", "Matrix Clear", "Clear", "enum", 0, 0, "--", opts=["--", "Clear"], state=False)

# --- effects (plan/09; Crest's chain, the distortion renamed Crush) --------------------------------
P("cr_lvl", "Crush Level", "Level", "float", 0, 1, 0.0)
P("cr_type", "Crush Type", "Type", "enum", 0, 0, "Soft", opts=CR_TYPES)
P("cr_tone", "Crush Tone", "Tone", "float", 0, 1, 0.7)
P("cr_mix", "Crush Mix", "Mix", "float", 0, 1, 1.0)
P("cho_lvl", "Chorus Level", "Level", "float", 0, 1, 0.0)
P("cho_type", "Chorus Type", "Type", "enum", 0, 0, "Chorus 1", opts=CHORUS_TYPES)
P("cho_rate", "Chorus Rate", "Rate", "float", 0, 1, 0.3)
P("cho_depth", "Chorus Depth", "Depth", "float", 0, 1, 0.5)
P("cho_dly", "Chorus Delay", "Dly", "float", 0, 1, 0.3)
P("cho_fb", "Chorus Feedback", "Fb", "float", -1, 1, 0.0)
P("cho_lp", "Chorus LoPass", "LoPas", "float", 0, 1, 0.709, cp=False)
P("cho_hp", "Chorus HiPass", "HiPas", "float", 0, 1, 0.016, cp=False)
P("dly_lvl", "Delay Level", "Level", "float", 0, 1, 0.0)
P("dly_time", "Delay Time", "Time", "float", 0, 1, 0.5)
P("dly_sync", "Delay Sync", "Sync", "enum", 0, 0, "1/8D", opts=DELAY_SYNC)
P("dly_fb", "Delay Feedback", "Fb", "float", 0, 1, 0.35)
P("dly_mode", "Delay Mode", "Mode", "enum", 0, 0, "PingPong", opts=DELAY_MODES)
P("dly_lp", "Delay Low-pass", "LP", "float", 0, 1, 0.6)
P("dly_hp", "Delay High-pass", "HP", "float", 0, 1, 0.1)
P("dly_ratio", "Delay L/R Ratio", "Ratio", "enum", 0, 0, "1/1", opts=DELAY_RATIOS, cp=False)
P("dly_width", "Delay Width", "Width", "float", 0, 1, 1.0, cp=False)
P("dly_slew", "Delay Slew Rate", "Slew", "float", 0, 1, 0.252, cp=False)
P("rev_lvl", "Reverb Level", "Level", "float", 0, 1, 0.3)
P("rev_type", "Reverb Type", "Type", "enum", 0, 0, "Hall", opts=REVERB_TYPES)
P("rev_size", "Reverb Size", "Size", "float", 0, 1, 0.7)
P("rev_decay", "Reverb Decay", "Decay", "float", 0, 1, 0.6)
P("rev_damp", "Reverb Damping", "Damp", "float", 0, 1, 0.4)
P("rev_hp", "Reverb Low Cut", "HP", "float", 0, 1, 0.15)
P("rev_pre", "Reverb Pre-delay", "Pre", "float", 0, 1, 0.04)
P("rev_frz", "Reverb Freeze", "RFrz", "enum", 0, 0, "Off", opts=OFF_ON)
P("rev_lpf", "Reverb LoPass", "LoPas", "float", 0, 1, 0.583, cp=False)
P("rev_hpf", "Reverb HiPass", "HiPas", "float", 0, 1, 0.0, cp=False)
P("rev_mdep", "Reverb Mod Depth", "MDep", "float", 0, 1, 0.504, cp=False)
P("rev_mrate", "Reverb Mod Rate", "MRate", "float", 0, 1, 0.031, cp=False)

# --- sample source (plan/04 section 5; smp_file is a filepath entry gen.py adds, the path is in the blob)
P("smp_root", "Sample Root", "Root", "int", 0, 127, 60)
P("smp_start", "Sample Start", "Start", "float", 0, 1, 0.0)
P("smp_end", "Sample End", "End", "float", 0, 1, 1.0)
P("smp_norm", "Normalise", "Norm", "enum", 0, 0, "On", opts=OFF_ON)

# --- patch categories (plan/12 section 4) ----------------------------------------------------------
CATEGORIES = [("init", "Init"), ("pad", "Pad"), ("texture", "Texture"), ("drone", "Drone"),
              ("keys", "Keys & Bells"), ("motion", "Motion"), ("rhythm", "Rhythm & Glitch"), ("sample", "Orchestra & Choir")]

# --- Move pages (ui_hierarchy, plan/11) --------------------------------------------------------------
# (level id, label, knobs, milestone). knobs: key or (key, short override). The root is the Main page.
ROOT_LABEL = "Drizzle"
ROOT_KNOBS = ["gr_pos", "gr_spray", "gr_size", "gr_dens", "gr_pspr", "gr_scan", ("rev_lvl", "Space"), "buf_freeze"]
LEVELS = [
    ("perform", "Perform", ["mac1", "mac2", "mac3", "mac4", "buf_freeze", "rev_frz", "v_flav", "patch_vol"], 3),
    ("source", "Source", ["src_type", "buf_mode", "src_root", "src_track", "buf_len", "src_fold", "src_tone", "src_seed"], 1),
    ("wavetable", "Wavetable", ["wt_a_table", "wt_a_wave", "wt_b_table", "wt_b_wave", "wt_b_semi", "wt_b_fine", "wt_b_lvl", "wt_q"], 2),
    ("wavetable_mod", "Wavetable Mod", ["wt_xmod", "wt_xamt"], 2),
    ("analog", "Analog", ["an_a_wave", "an_a_shape", "an_b_wave", "an_b_shape", "an_b_semi", "an_b_fine", "an_b_lvl", "an_noise"], 1),
    ("analog_mod", "Analog Mod", ["an_dens", "an_ddet", "an_fm", "an_sync"], 1),
    ("sample", "Sample", ["smp_file", "smp_root", "smp_start", "smp_end", "smp_norm"], 5),
    ("sweep", "Sweep", ["sw_time", "sw_curve", "sw_mode", "sw_timb", "sw_fold", "sw_pitch", "sw_tone"], 1),
    ("source_lfo", "Source LFO", ["sl_wave", "sl_rate", "sl_sync", "sl_timb", "sl_fold", "sl_pitch", "sl_tone"], 2),
    ("grains", "Grains", ["gr_pos", "gr_spray", "gr_size", "gr_dens", "gr_jit", "gr_scan", "gr_win", "gr_lock"], 1),
    ("grain_pitch", "Grain Pitch", ["gr_pitch", "gr_fine", "gr_pspr", "gr_pq", "gr_rev", "gr_spread", "gr_lspr"], 1),
    ("rhythm", "Rhythm", ["gr_mode", "gr_div", "gr_swing", "gr_prob", "gr_rpt", "gr_rnd", "gr_seed"], 3),
    ("filter", "Filter", ["flt_type", "flt_cut", "flt_res", "flt_drive", "flt_env", "flt_key", "flt_vel"], 2),
    ("amp_env", "Amp Env", ["ae_a", "ae_d", "ae_s", "ae_r"], 1),
    ("mod_env", "Mod Env", ["me_a", "me_d", "me_s", "me_r", "me_pos", "me_size", "me_dens", "me_pitch"], 2),
    ("env_opts", "Env Options", ["ae_vel", "me_vel", "me_trig", "gr_relth"], 1),
    ("voice", "Voice", ["v_mode", "v_voices", "v_glide", "v_bend", "v_oct", "v_flav", "v_pan"], 1),
    ("lfo1", "LFO 1", ["l1_wave", "l1_rate", "l1_sync", "l1_trig", "l1_pos", "l1_size", "l1_pitch", "l1_cut"], 3),
    ("lfo2", "LFO 2", ["l2_wave", "l2_rate", "l2_sync", "l2_phase", "l2_pos", "l2_dens", "l2_spray", "l2_pan"], 3),
    ("pressure", "Pressure", ["at_pos", "at_dens", "at_spray", "at_size", "at_pitch", "at_cut"], 3),
    ("matrix", "Mod Matrix", {"items_param": "mm_items", "select_param": "mm_sel", "navigate_to": "matrix_edit"}, 3),
    ("matrix_edit", "Matrix Edit", ["mm_src", "mm_via", "mm_dst", "mm_dep", "mm_slot", "mm_clear"], 3),
    ("crush", "Crush", ["cr_lvl", "cr_type", "cr_tone", "cr_mix"], 1),
    ("chorus", "Chorus", ["cho_lvl", "cho_type", "cho_rate", "cho_depth", "cho_dly", "cho_fb"], 1),
    ("delay", "Delay", ["dly_lvl", "dly_time", "dly_sync", "dly_fb", "dly_mode", "dly_lp", "dly_hp"], 1),
    ("reverb", "Reverb", ["rev_lvl", "rev_type", "rev_size", "rev_decay", "rev_damp", "rev_hp", "rev_pre", "rev_frz"], 1),
]

# --- display curves for the web UI and the cards (plan/11) ---------------------------------------
# How a stored value reads in real units; each mirrors the DSP formula. A key with no entry reads as
# a percentage (0..1) or a signed percentage (-1..1).
#   ["exp", lo, hi, unit]  lo * (hi/lo)^x        ["lin", lo, hi, unit]     ["note"] MIDI note name
#   ["env", tmax, bend]    0.5 ms * (tmax / 0.5 ms)^(x^bend)               ["cubic", s, unit] s * x^3
DISP = {
    "buf_len": ["exp", 0.5, 8, "s"], "gr_size": ["exp", 5, 2000, "ms"], "gr_dens": ["exp", 0.5, 250, "/s"],
    "gr_scan": ["cubic", 4, "x"], "src_root": ["note"], "smp_root": ["note"], "src_tone": ["exp", 150, 20000, "Hz"],
    "sw_time": ["exp", 0.1, 60, "s"], "sl_rate": ["exp", 0.01, 40, "Hz"], "l1_rate": ["exp", 0.01, 40, "Hz"],
    "l2_rate": ["exp", 0.01, 40, "Hz"], "v_glide": ["exp", 0.001, 2, "s"], "flt_cut": ["cut"],
    "an_sync": ["exp", 1, 8, "x"], "wt_a_table": ["table"], "wt_b_table": ["table"], "an_ddet": ["lin", 0, 35, "ct"],
    "cr_tone": ["exp", 1500, 20000, "Hz"], "cho_rate": ["exp", 0.05, 8, "Hz"], "cho_dly": ["lin", 3, 30, "ms"],
    "dly_time": ["exp", 10, 2000, "ms"], "dly_lp": ["exp", 1000, 20000, "Hz"], "dly_hp": ["exp", 20, 2000, "Hz"],
    "rev_decay": ["exp", 0.2, 20, "s"], "rev_hp": ["exp", 20, 1000, "Hz"], "rev_pre": ["lin", 0, 500, "ms"],
    "flt_env": ["lin", 0, 96, "st"], "flt_vel": ["lin", 0, 48, "st"], "flt_key": ["pct"],
}
for _e in ("ae", "me"):
    DISP[_e + "_a"] = ["env", 20, 1]
    DISP[_e + "_d"] = ["env", 22, 1]
    DISP[_e + "_r"] = ["env", 25, 0.6]

# Knobs that raise an on-device card (src/cards.js) while held or turned: the values whose meaning
# needs more than the header's number. Each costs about 29 bytes of chain_params.
CARD_KEYS = ["gr_size", "gr_dens", "gr_scan", "gr_pspr", "gr_pitch", "buf_len", "src_root", "src_tone",
             "wt_a_table", "wt_b_table", "wt_a_wave", "wt_b_wave", "sw_time", "sl_rate", "l1_rate", "l2_rate",
             "flt_cut", "flt_env", "ae_a", "ae_d", "ae_r", "me_a", "me_d", "me_r", "v_glide", "dly_time",
             "rev_decay", "cho_rate", "an_sync", "an_ddet", "mm_dep", "me_pitch", "l1_pitch", "sw_pitch"]
