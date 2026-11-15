/* Drizzle web UI (plan/11 section 8). It boots on a Move, inside Schwung's Remote UI through
 * window.schwungRemote, or standalone with the engine compiled to WebAssembly and played from this page,
 * the computer keyboard or Web MIDI. Every page and control comes from ui_meta.json, which
 * tools/gen/gen.py writes from tools/gen/params.py, so new parameters appear here by themselves.
 *
 * It is also the factory-patch authoring tool: "Patch JSON" gives the current sound as a
 * presets/factory/*.json file (only the values that differ from INIT). */
import { BrowseModel, presetHeader, storedFavourites } from "./presetbrowser.js";

const here = new URL(".", import.meta.url).href;
const $ = (sel, el = document) => el.querySelector(sel);

function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") el.className = v;
    else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v);
  }
  for (const k of kids) el.append(k);
  return el;
}

/* ------------------------------------------------------------------ value display (params.py DISP) */

const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function num(x, unit) {
  const a = Math.abs(x);
  const s = a >= 100 ? x.toFixed(0) : a >= 10 ? x.toFixed(1) : x.toFixed(2);
  return unit ? s + " " + unit : s;
}

function reading(p, v, meta, values) {
  if (p.t === "enum") return p.o[Math.round(v)] ?? String(v);
  const f = p.f;
  if (p.k === "gr_size" && values?.get("gr_mode") === 1) return num(0.0625 * Math.pow(64, v), "steps");
  if (!f) return p.t === "int" ? String(Math.round(v)) + (p.u ? " " + p.u : "") : num(v, p.u);
  switch (f[0]) {
    case "pct": return Math.round(v * 100) + "%";
    case "bip": return (v > 0 ? "+" : "") + Math.round(v * 100) + "%";
    case "exp": {
      const x = f[1] * Math.pow(f[2] / f[1], v);
      if (f[3] === "ms" && x >= 1000) return num(x / 1000, "s");
      if (f[3] === "Hz" && x >= 1000) return num(x / 1000, "kHz");
      return num(x, f[3]);
    }
    case "lin": return num(f[1] + (f[2] - f[1]) * v, f[3]);
    case "env": {
      const s = 0.0005 * Math.pow(f[1] / 0.0005, Math.pow(v, f[2]));
      return s < 1 ? num(s * 1000, "ms") : num(s, "s");
    }
    case "cubic": return num(f[1] * Math.pow(v / f[1], 3), f[2]);
    case "note": return NOTE[Math.round(v) % 12] + (Math.floor(Math.round(v) / 12) - 1);
    case "cut": {
      const hz = 440 * Math.pow(2, (20 + 115 * v - 69) / 12);
      return hz >= 1000 ? num(hz / 1000, "kHz") : num(hz, "Hz");
    }
    case "table": return meta.tables[Math.round(v)] ?? String(v);
    default: return num(v, p.u);
  }
}

/* ------------------------------------------------------------------ links */

/* The Move, through Schwung's bridge: values arrive as { "<component>:<key>": value }. */
class DeviceLink {
  constructor(remote) {
    this.remote = remote;
    this.kind = "device";
    this.prefix = (remote.component || "synth") + ":";
  }

  onValues(cb) {
    this.remote.onParamChange((params) => {
      const vals = {};
      for (const k in params) if (k.startsWith(this.prefix)) vals[k.slice(this.prefix.length)] = params[k];
      cb(vals);
    });
  }

  send(key, value) { this.remote.setParam(this.prefix + key, String(value)); }
  async get() { return null; } /* the bridge pushes; nothing is asked */
}

/* drizzle.wasm in this browser, the patch kept in localStorage between visits: the state blob, and
 * which factory patch it is and whether it was edited, so a reload shows the same patch by name. */
const SAVE_KEY = "drizzle.state.v1";
const WHERE_KEY = "drizzle.where.v1";

/** What the last visit left: { state, preset, edits }, or null. */
function savedPatch() {
  try {
    const state = localStorage.getItem(SAVE_KEY);
    if (!state) return null;
    const where = JSON.parse(localStorage.getItem(WHERE_KEY) || "{}");
    return { state, preset: Number.isInteger(where.preset) ? where.preset : -1, edits: where.edits | 0 };
  } catch (_) { return null; } /* storage blocked: start from INIT */
}

class LocalLink {
  constructor(engine, meta, where) {
    this.engine = engine;
    this.meta = meta;
    this.where = where; /* () => { preset, edits }: the page's view of the patch, saved with it */
    this.kind = "local";
    this.cb = () => {};
    this.saveTimer = 0;
  }

  /** Back to what the last visit (or the engine before this one) left: an unedited factory patch by
   * loading it, anything else from its state blob. Returns the edit count to show. */
  async init() {
    const saved = savedPatch();
    let edits = 0;
    if (saved && saved.preset >= 0 && !saved.edits) this.engine.set("preset", saved.preset);
    else if (saved) { this.engine.set("state", saved.state); edits = Math.max(1, saved.edits); }
    await this.pull();
    return { edits, preset: saved && saved.preset };
  }

  onValues(cb) { this.cb = cb; }

  /** Everything the page shows, read back after a patch load: the state blob in one call, then the keys
   * it does not hold (the matrix proxies, the browser's position). */
  async pull(keys) {
    const vals = {};
    if (!keys) {
      try {
        const st = JSON.parse(await this.engine.get("state"));
        for (const k in st) if (k !== "fmt" && k !== "v" && k !== "name") vals[k] = st[k];
        vals.preset_name = st.name;
      } catch (_) { /* no blob: the keys below still come */ }
      keys = [...this.meta.params.filter((p) => p.vol).map((p) => p.k), "preset", "pb_cat"];
    }
    for (const k of keys) {
      const s = await this.engine.get(k);
      if (s !== null) vals[k] = s;
    }
    this.cb(vals);
  }

  send(key, value) {
    this.engine.set(key, value);
    if (key === "preset" || key === "state" || key === "pb_index") this.pull().then(() => this.onLoad?.());
    else if (key === "mm_slot" || key === "mm_sel" || key === "mm_clear") this.pull(["mm_src", "mm_via", "mm_dst", "mm_dep", "mm_slot", "mm_clear"]);
    clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => this.save(), 800);
  }

  /** Store the patch now (also when the page is hidden: a tablet may never come back to this tab). */
  async save() {
    clearTimeout(this.saveTimer);
    try {
      const st = await this.engine.get("state");
      if (!st) return;
      localStorage.setItem(SAVE_KEY, st);
      localStorage.setItem(WHERE_KEY, JSON.stringify(this.where()));
    } catch (_) { /* quota or blocked storage */ }
  }

  get(key) { return this.engine.get(key); }
}

/* ------------------------------------------------------------------ samples kept in this browser */

/* Sample mode in the browser: decoded files, by name, in IndexedDB (plan/04 section 5). */
const samples = {
  db: null,
  async open() {
    if (this.db || !window.indexedDB) return this.db;
    this.db = await new Promise((res) => {
      const rq = indexedDB.open("drizzle.samples", 1);
      rq.onupgradeneeded = () => rq.result.createObjectStore("s");
      rq.onsuccess = () => res(rq.result);
      rq.onerror = () => res(null);
    });
    return this.db;
  },
  async put(name, data) {
    const db = await this.open();
    if (!db) return;
    await new Promise((res) => { const t = db.transaction("s", "readwrite"); t.objectStore("s").put(data, name); t.oncomplete = t.onerror = res; });
  },
  async get(name) {
    const db = await this.open();
    if (!db) return null;
    return new Promise((res) => { const rq = db.transaction("s").objectStore("s").get(name); rq.onsuccess = () => res(rq.result || null); rq.onerror = () => res(null); });
  },
};

/* ------------------------------------------------------------------ the app */

class App {
  constructor(meta) {
    this.meta = meta;
    this.byKey = new Map(meta.params.map((p, i) => [p.k, { ...p, i }]));
    this.values = new Map(meta.params.map((p) => [p.k, p.d]));
    this.extra = {};          /* preset, preset_name, pb_cat, buf_view, grain_view */
    this.controls = new Map(); /* key -> [update functions] */
    this.link = null;
    this.engine = null;
    this.octave = 4;
    this.held = new Set();
    this.listeners = new Set();
  }

  /** Values from the synth (strings or numbers): update the controls and the screen. */
  receive(vals) {
    for (const [k, raw] of Object.entries(vals)) {
      if (this.byKey.has(k)) {
        const v = parseFloat(raw);
        if (!Number.isFinite(v)) continue;
        this.values.set(k, v);
        for (const fn of this.controls.get(k) || []) fn(v);
      } else {
        this.extra[k] = raw;
      }
    }
    for (const fn of this.listeners) fn(Object.keys(vals));
  }

  set(key, v) {
    this.values.set(key, v);
    this.edits = (this.edits || 0) + 1; /* the patch header says "edited" */
    this.link?.send(key, v);
    for (const fn of this.controls.get(key) || []) fn(v);
  }

  /* The Sample page's file: a picker here, the path the Move's own browser chose on a Move. */
  sampleControl() {
    const out = h("output", {});
    const lab = h("span", { class: "k-lab" }, "File");
    const file = h("input", { type: "file", accept: "audio/*,.wav", hidden: "" });
    const pick = h("button", { type: "button", class: "chip", onclick: () => file.click() }, "Load sample…");
    file.addEventListener("change", async () => {
      const f = file.files[0];
      file.value = "";
      if (!f || !this.engine) return;
      out.textContent = "Decoding " + f.name + "…";
      try {
        const data = await this.engine.decode(await f.arrayBuffer());
        await samples.put(f.name, new Float32Array(data));
        this.engine.loadSample(new Float32Array(data), f.name);
        this.extra.smp_file = f.name;
        if (this.values.get("src_type") !== 2) this.set("src_type", 2);
        out.textContent = f.name + " · " + (data.length / 44100).toFixed(1) + " s";
      } catch (err) { out.textContent = "Could not read " + f.name + ": " + err.message; }
    });
    this.listeners.add(() => { if (this.extra.smp_file !== undefined && !out.textContent.startsWith("Decoding")) out.textContent = this.extra.smp_file || "no sample"; });
    return h("div", { class: "ctl file-ctl" }, lab, this.link?.kind === "device" || window.schwungRemote ? h("span", {}) : pick, file, out);
  }

  /** After a patch or state load in the browser: hand the engine the sample the patch names, if this browser has it. */
  async restoreSample() {
    const name = this.extra.smp_file;
    if (!name || !this.engine) return;
    const data = await samples.get(name);
    if (data) this.engine.loadSample(new Float32Array(data), name);
  }

  /* -------------------------------------------------------------- controls */

  bind(key, update) {
    if (!this.controls.has(key)) this.controls.set(key, []);
    this.controls.get(key).push(update);
    update(this.values.get(key));
  }

  /** A control for `key`: a rotary knob for numbers, segmented buttons for short choices, a menu else. */
  control(key, label) {
    if (key === "smp_file") return this.sampleControl();
    const p = this.byKey.get(key);
    if (!p) return h("div", { class: "ctl missing" }, key);
    if (p.t === "enum") {
      const short = p.o.length <= 4 && p.o.every((o) => o.length <= 7);
      return short ? this.segmented(key, label) : this.menu(key, label);
    }
    return this.knob(key, label);
  }

  /* A rotary knob: drag up or right to raise it (Shift for fine), wheel, arrow keys, double-click or
   * double-tap for the default. The arc grows from the bottom, or from the top for a bipolar range. */
  knob(key, label, size = "") {
    const p = this.byKey.get(key);
    const span = p.mx - p.mn, bip = p.mn < 0 && p.mx > 0;
    const NS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("viewBox", "0 0 64 64");
    svg.setAttribute("class", "knob-svg");
    const mk = (cls) => { const e = document.createElementNS(NS, "path"); e.setAttribute("class", cls); svg.append(e); return e; };
    const track = mk("k-track"), arc = mk("k-arc"), tick = mk("k-tick");
    const R = 25, C = 32, A0 = 135, A1 = 405;
    const pt = (a, r = R) => [C + r * Math.cos(a * Math.PI / 180), C + r * Math.sin(a * Math.PI / 180)];
    const path = (a, b) => {
      if (Math.abs(b - a) < 0.5) return "";
      const [x0, y0] = pt(Math.min(a, b)), [x1, y1] = pt(Math.max(a, b));
      return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${R} ${R} 0 ${Math.abs(b - a) > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
    };
    track.setAttribute("d", path(A0, A1));
    const out = h("output", { class: "k-val" });
    const lab = h("span", { class: "k-lab" }, label || p.s);
    const el = h("div", { class: "knob" + (size ? " " + size : ""), tabindex: "0", role: "slider", title: p.n,
      "aria-label": p.n, "aria-valuemin": String(p.mn), "aria-valuemax": String(p.mx) }, lab, svg, out);
    const put = (v) => {
      v = Math.min(p.mx, Math.max(p.mn, v));
      if (p.t === "int") v = Math.round(v);
      if (v !== this.values.get(key)) this.set(key, v);
    };
    this.bind(key, (v) => {
      const t = (v - p.mn) / span, a = A0 + t * (A1 - A0);
      arc.setAttribute("d", path(bip ? 270 : A0, a));
      const [x0, y0] = pt(a, 13), [x1, y1] = pt(a, 21);
      tick.setAttribute("d", `M${x0.toFixed(2)} ${y0.toFixed(2)}L${x1.toFixed(2)} ${y1.toFixed(2)}`);
      out.textContent = reading(p, v, this.meta, this.values);
      el.setAttribute("aria-valuenow", String(v));
      el.setAttribute("aria-valuetext", out.textContent);
    });
    let drag = null, lastTap = 0;
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      el.focus({ preventScroll: true });
      const now = performance.now();
      if (now - lastTap < 300) { put(p.d); lastTap = 0; return; } /* double tap: the default */
      lastTap = now;
      el.setPointerCapture(e.pointerId);
      drag = { x: e.clientX, y: e.clientY, v: this.values.get(key) };
      el.classList.add("active");
    });
    el.addEventListener("pointermove", (e) => {
      if (!drag) return;
      const d = (e.clientX - drag.x) - (e.clientY - drag.y);
      put(drag.v + d / (e.shiftKey ? 900 : 180) * span);
    });
    const end = () => { drag = null; el.classList.remove("active"); };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
    el.addEventListener("wheel", (e) => {
      e.preventDefault();
      put(this.values.get(key) - Math.sign(e.deltaY) * (p.t === "int" ? 1 : span * (e.shiftKey ? 0.002 : 0.01)));
    }, { passive: false });
    el.addEventListener("keydown", (e) => {
      const step = p.t === "int" ? 1 : span * (e.shiftKey ? 0.002 : 0.01);
      const d = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: span / 10, PageDown: -span / 10 }[e.key];
      if (d !== undefined) { e.preventDefault(); put(this.values.get(key) + d); }
      else if (e.key === "Home") put(p.mn);
      else if (e.key === "End") put(p.mx);
      else if (e.key === "Delete" || e.key === "Backspace") put(p.d);
    });
    return el;
  }

  segmented(key, label) {
    const p = this.byKey.get(key);
    const buttons = p.o.map((o, i) => h("button", { type: "button", class: "seg-b", onclick: () => this.set(key, i) }, o));
    this.bind(key, (v) => buttons.forEach((b, i) => {
      b.classList.toggle("on", i === Math.round(v));
      b.setAttribute("aria-pressed", String(i === Math.round(v)));
    }));
    return h("div", { class: "ctl seg-ctl" + (p.o.length > 2 ? " wide" : ""), title: p.n }, h("span", { class: "k-lab" }, label || p.s),
      h("div", { class: "seg", role: "group", "aria-label": p.n }, ...buttons));
  }

  menu(key, label) {
    const p = this.byKey.get(key);
    const sel = h("select", { "aria-label": p.n, onchange: () => this.set(key, Number(sel.value)) },
      ...p.o.map((o, i) => h("option", { value: String(i) }, o)));
    this.bind(key, (v) => { sel.value = String(Math.round(v)); });
    return h("div", { class: "ctl menu-ctl", title: p.n }, h("span", { class: "k-lab" }, label || p.s), sel);
  }

  /* An envelope's shape from its four knobs, on the env.c time laws (log time, so short and long both read). */
  envPreview(pre) {
    const NS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("viewBox", "0 0 200 60");
    svg.setAttribute("class", "env-svg");
    svg.setAttribute("preserveAspectRatio", "none");
    const fill = document.createElementNS(NS, "path"), line = document.createElementNS(NS, "path");
    fill.setAttribute("class", "env-fill");
    line.setAttribute("class", "env-line");
    svg.append(fill, line);
    const t = (x, max, bend = 1) => 0.0005 * Math.pow(max / 0.0005, Math.pow(x, bend));
    const draw = () => {
      const g = (k) => this.values.get(pre + "_" + k);
      const w = (s) => 0.15 + Math.log10(1 + s * 20); /* log-ish width per stage */
      const a = w(t(g("a"), 20)), d = w(t(g("d"), 22)), s = g("s"), hold = 0.6, r = w(t(g("r"), 25, 0.6));
      const tot = a + d + hold + r, X = (x) => 4 + x / tot * 192, Y = (y) => 56 - y * 50;
      const pts = [[0, 0], [a, 1], [a + d, s], [a + d + hold, s], [tot, 0]].map(([x, y]) => `${X(x).toFixed(1)},${Y(y).toFixed(1)}`);
      line.setAttribute("d", "M" + pts.join("L"));
      fill.setAttribute("d", "M" + pts.join("L") + "Z");
    };
    for (const k of ["a", "d", "s", "r"]) this.bind(pre + "_" + k, draw);
    return svg;
  }

  panel(pg) {
    const body = h("div", { class: "panel-body" });
    for (const k of pg.knobs) body.append(Array.isArray(k) ? this.control(k[0], k[1]) : this.control(k));
    const box = h("section", { class: "panel", "data-page": pg.id }, h("h3", {}, pg.label));
    if (pg.id === "amp_env") box.append(this.envPreview("ae"));
    if (pg.id === "mod_env") box.append(this.envPreview("me"));
    box.append(body);
    return box;
  }

  /* The matrix as one table (standalone): every slot's source, via, destination and depth. On a Move the
   * slots reach the page only through the Matrix Edit proxies, so it shows that page instead. */
  matrixPanel() {
    const rows = h("div", { class: "mm-rows" });
    for (let n = 1; n <= this.meta.mmSlots; n++) {
      const pre = "mm" + String(n).padStart(2, "0") + "_";
      const sel = (k, opts) => {
        const s = h("select", { "aria-label": `Slot ${n} ${k}`, onchange: () => this.set(pre + k, Number(s.value)) },
          ...opts.map((o, i) => h("option", { value: String(i) }, i === 0 ? "–" : o)));
        this.bind(pre + k, (v) => { s.value = String(Math.round(v)); });
        return s;
      };
      const dep = h("input", { type: "range", min: "-1", max: "1", step: "0.01", class: "mm-dep", "aria-label": `Slot ${n} depth` });
      const dv = h("output", { class: "mm-dv" });
      dep.addEventListener("input", () => this.set(pre + "dep", Number(dep.value)));
      dep.addEventListener("dblclick", () => this.set(pre + "dep", 0));
      this.bind(pre + "dep", (v) => { dep.value = String(v); dv.textContent = (v > 0 ? "+" : "") + Math.round(v * 100) + "%"; });
      const row = h("div", { class: "mm-row" }, h("span", { class: "mm-n" }, String(n)),
        sel("src", this.meta.modSources), h("span", { class: "mm-x" }, "×"), sel("via", this.meta.modSources),
        h("span", { class: "mm-x" }, "→"), sel("dst", this.meta.modDests), dep, dv);
      const used = () => row.classList.toggle("used", this.values.get(pre + "src") > 0 && this.values.get(pre + "dst") > 0);
      this.bind(pre + "src", used);
      this.bind(pre + "dst", used);
      rows.append(row);
    }
    return h("section", { class: "panel wide", "data-page": "matrix" }, h("h3", {}, "Mod Matrix"),
      h("div", { class: "mm-head" }, h("span", {}, "#"), h("span", {}, "Source"), h("span", {}), h("span", {}, "Via"), h("span", {}),
        h("span", {}, "Destination"), h("span", {}, "Depth")), rows);
  }

  /* -------------------------------------------------------------- buffer view (plan/11 section 4) */

  bufferView() {
    const c = h("canvas", { width: "1024", height: "200", class: "bufview" });
    const ctx = c.getContext("2d");
    const z = h("canvas", { width: "320", height: "200", class: "zoomview" });
    const zctx = z.getContext("2d");
    const status = h("div", { class: "bufstatus" }, "");
    const hexAt = (s, i) => parseInt(s.substr(i, 2), 16);
    /* "#rrggbb" -> [r, g, b] (the theme's tones), and a mix of two */
    const rgb = (v, d) => { const m = /#([0-9a-f]{6})/i.exec(v || ""); const n = parseInt(m ? m[1] : d, 16); return [n >> 16, (n >> 8) & 255, n & 255]; };
    const mix = (a, b, t) => `rgb(${a.map((x, i) => Math.round(x + (b[i] - x) * t)).join(",")})`;
    const draw = () => {
      const bv = this.extra.buf_view || "", gv = this.extra.grain_view || "", wv = this.extra.wave_view || "";
      const W = c.width, H = c.height;
      const css = getComputedStyle(document.body);
      const panel = css.getPropertyValue("--panel").trim() || "#111", accent = css.getPropertyValue("--accent").trim() || "#fc6";
      const dark = rgb(css.getPropertyValue("--dark-tone"), "3b4cc0"), bright = rgb(css.getPropertyValue("--bright-tone"), "f6c445");
      ctx.fillStyle = panel;
      ctx.fillRect(0, 0, W, H);
      let still = true;
      if (bv.length >= 6) {
        const mode = bv[0], frozen = bv[1] === "1";
        still = mode !== "L" || frozen;
        const prog = hexAt(bv, 2);
        const n = Math.min(128, (bv.length - 6) / 2), bw = W / n;
        /* level as height, brightness as colour, stretched over this buffer's own range (a timbre
         * sweep shows even where the level is flat) */
        const hasB = bv.length >= 6 + 512;
        let lo = 255, hi = 0;
        if (hasB) for (let i = 0; i < n; i++) { const b = hexAt(bv, 6 + 256 + 2 * i); lo = Math.min(lo, b); hi = Math.max(hi, b); }
        if (hi - lo < 64) { const m = (hi + lo) / 2; lo = m - 32; hi = m + 32; }
        for (let i = 0; i < n; i++) {
          const a = hexAt(bv, 6 + 2 * i) / 255;
          const hh = Math.max(1, a * (H * 0.45));
          const t = hasB ? Math.min(1, Math.max(0, (hexAt(bv, 6 + 256 + 2 * i) - lo) / (hi - lo))) : 0.5;
          ctx.fillStyle = mix(dark, bright, t);
          ctx.fillRect(i * bw, H / 2 - hh, bw - 1, 2 * hh);
        }
        if (prog < 255) {
          ctx.fillStyle = accent;
          ctx.fillRect(0, H - 4, W * prog / 255, 4);
        }
        const pos = this.values.get("gr_pos"), spr = this.values.get("gr_spray");
        const x = still ? pos : 1 - pos;
        ctx.strokeStyle = accent;
        ctx.lineWidth = 2;
        ctx.strokeRect((x - spr) * W, 2, Math.max(2, 2 * spr * W), H - 4);
        status.textContent = `${mode === "M" ? "Sample" : mode === "S" ? "Scan" : "Live"}${frozen ? " · frozen" : ""}` +
          `${prog < 255 ? " · rendering tape" : ""}` + (mode === "M" ? "" : " · " + reading(this.byKey.get("buf_len"), this.values.get("buf_len"), this.meta)) +
          (hasB ? " · colour: dark to bright timbre" : "");
      } else {
        status.textContent = this.link ? "" : "Press Start audio to play Drizzle in this browser.";
      }
      ctx.fillStyle = css.getPropertyValue("--grain") || "#fff";
      for (let i = 0; i + 4 <= gv.length; i += 4) {
        const gx = hexAt(gv, i) / 255, ga = hexAt(gv, i + 2) / 255;
        ctx.beginPath();
        ctx.arc(gx * W, H - 12 - ga * (H - 24), 3 + 4 * ga, 0, 2 * Math.PI);
        ctx.fill();
      }
      /* the zoom: about two periods of the material at Position (wave_view) */
      const ZW = z.width, ZH = z.height;
      zctx.fillStyle = panel;
      zctx.fillRect(0, 0, ZW, ZH);
      zctx.strokeStyle = css.getPropertyValue("--line").trim() || "#333";
      zctx.lineWidth = 1;
      zctx.beginPath();
      zctx.moveTo(0, (ZH - 30) / 2);
      zctx.lineTo(ZW, (ZH - 30) / 2);
      zctx.stroke();
      if (wv.length >= 128) {
        zctx.strokeStyle = css.getPropertyValue("--wave").trim() || "#6af";
        zctx.lineWidth = 3;
        zctx.beginPath();
        for (let i = 0; i < 64; i++) {
          const v = (hexAt(wv, 2 * i) - 128) / 127;
          const px = 8 + i * (ZW - 16) / 63, py = (ZH - 30) / 2 - v * ((ZH - 30) / 2 - 8);
          if (i) zctx.lineTo(px, py); else zctx.moveTo(px, py);
        }
        zctx.stroke();
      }
      zctx.fillStyle = css.getPropertyValue("--muted").trim() || "#999";
      zctx.font = "22px system-ui, sans-serif";
      zctx.fillText("the wave at Position", 10, ZH - 9);
    };
    /* the engine here is asked 25 times a second; the Move pushes these values itself */
    const loop = async () => {
      if (this.link?.kind === "local") {
        const [bv, gv, wv] = await Promise.all([this.link.get("buf_view"), this.link.get("grain_view"), this.link.get("wave_view")]);
        this.extra.buf_view = bv || "";
        this.extra.grain_view = gv || "";
        this.extra.wave_view = wv || "";
      }
      draw();
      setTimeout(() => requestAnimationFrame(loop), 40);
    };
    loop();
    return h("div", { class: "bufbox" }, h("div", { class: "bufrow" }, c, z), status);
  }

  /* -------------------------------------------------------------- playing (standalone only) */

  noteOn(n, vel = 100) {
    if (this.held.has(n) || !this.engine) return;
    this.held.add(n);
    this.engine.midi([0x90, n, vel]);
    document.querySelector(`.key[data-note="${n}"]`)?.classList.add("on");
  }

  noteOff(n) {
    if (this.hold && this.held.has(n)) { this.latched.add(n); return; }
    if (!this.held.delete(n)) return;
    this.engine?.midi([0x80, n, 0]);
    document.querySelector(`.key[data-note="${n}"]`)?.classList.remove("on");
  }

  setHold(on) {
    this.hold = on;
    if (!on) for (const n of [...this.latched]) { this.latched.delete(n); this.noteOff(n); }
  }

  /* The on-screen keyboard: 25 keys from the octave's C. Touch, pen or mouse; slide across keys to play
   * them in turn; several fingers at once; lower on a key plays louder. Nothing on it can be selected,
   * zoomed or long-pressed into a callout (the page's CSS and the handlers below). */
  keyboard() {
    this.latched = new Set();
    const kb = h("div", { class: "keys", "aria-label": "Keyboard" });
    const label = h("span", { class: "oct-label" });
    const build = () => {
      for (const n of [...this.held]) this.noteOff(n);
      kb.textContent = "";
      const base = 12 * (this.octave + 1);
      const whites = [];
      for (let n = base; n <= base + 24; n++) if (![1, 3, 6, 8, 10].includes(n % 12)) whites.push(n);
      const ww = 100 / whites.length;
      whites.forEach((n, i) => {
        const k = h("div", { class: "key white", "data-note": String(n), style: `left:${i * ww}%;width:${ww}%` });
        if (n % 12 === 0) k.append(h("span", { class: "key-c" }, "C" + (n / 12 - 1)));
        kb.append(k);
      });
      whites.forEach((n, i) => {
        if ([0, 2, 5, 7, 9].includes(n % 12) && n + 1 <= base + 24)
          kb.append(h("div", { class: "key black", "data-note": String(n + 1), style: `left:${(i + 1) * ww - ww * 0.32}%;width:${ww * 0.64}%` }));
      });
      label.textContent = "C" + this.octave + "–C" + (this.octave + 2);
    };
    build();
    const pointers = new Map(); /* pointerId -> note */
    const at = (e) => {
      const el = document.elementFromPoint(e.clientX, e.clientY);
      const k = el && el.closest && el.closest(".key");
      if (!k || !kb.contains(k)) return null;
      const r = k.getBoundingClientRect();
      return { n: Number(k.dataset.note), vel: Math.round(40 + 87 * Math.min(1, Math.max(0, (e.clientY - r.top) / r.height))) };
    };
    kb.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      kb.setPointerCapture(e.pointerId);
      const k = at(e);
      if (!k) return;
      pointers.set(e.pointerId, k.n);
      if (this.hold && this.latched.has(k.n)) { this.latched.delete(k.n); this.hold = false; this.noteOff(k.n); this.hold = true; return; }
      this.noteOn(k.n, k.vel);
    });
    kb.addEventListener("pointermove", (e) => {
      if (!pointers.has(e.pointerId)) return;
      const k = at(e), was = pointers.get(e.pointerId);
      if (!k || k.n === was) return;
      this.noteOff(was);
      pointers.set(e.pointerId, k.n);
      this.noteOn(k.n, k.vel);
    });
    const up = (e) => {
      if (!pointers.has(e.pointerId)) return;
      this.noteOff(pointers.get(e.pointerId));
      pointers.delete(e.pointerId);
    };
    kb.addEventListener("pointerup", up);
    kb.addEventListener("pointercancel", up);
    kb.addEventListener("lostpointercapture", up);
    for (const t of ["touchstart", "touchmove", "touchend", "gesturestart"]) kb.addEventListener(t, (e) => e.preventDefault(), { passive: false });
    kb.addEventListener("contextmenu", (e) => e.preventDefault());

    const map = "awsedftgyhujkolp;'";
    window.addEventListener("keydown", (e) => {
      if (e.repeat || e.metaKey || e.ctrlKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
      if (e.key === "z") { this.octave = Math.max(1, this.octave - 1); build(); return; }
      if (e.key === "x") { this.octave = Math.min(6, this.octave + 1); build(); return; }
      if (e.key === " ") { e.preventDefault(); this.set("buf_freeze", this.values.get("buf_freeze") >= 0.5 ? 0 : 1); return; }
      const i = map.indexOf(e.key.toLowerCase());
      if (i >= 0) this.noteOn(12 * (this.octave + 1) + i, 100);
    });
    window.addEventListener("keyup", (e) => {
      const i = map.indexOf(e.key.toLowerCase());
      if (i >= 0) for (let o = 1; o <= 7; o++) this.noteOff(12 * (o + 1) + i);
    });

    const octDown = h("button", { type: "button", class: "tool", "aria-label": "Octave down", onclick: () => { this.octave = Math.max(1, this.octave - 1); build(); } }, "–");
    const octUp = h("button", { type: "button", class: "tool", "aria-label": "Octave up", onclick: () => { this.octave = Math.min(6, this.octave + 1); build(); } }, "+");
    const hold = h("button", { type: "button", class: "tool toggle", "aria-pressed": "false" }, "Hold");
    hold.onclick = () => { this.setHold(!this.hold); hold.classList.toggle("on", this.hold); hold.setAttribute("aria-pressed", String(this.hold)); };
    /* the internal transport: Tempo-mode grains and synced LFOs follow it */
    const bpm = h("input", { type: "number", min: "40", max: "240", value: "120", class: "bpm", "aria-label": "Tempo (BPM)" });
    const play = h("button", { type: "button", class: "tool toggle", "aria-pressed": "false", "aria-label": "Transport" }, "▶");
    let running = false;
    const tempo = () => this.engine?.tempo(Math.min(240, Math.max(40, Number(bpm.value) || 120)), running);
    play.onclick = () => { running = !running; play.classList.toggle("on", running); play.setAttribute("aria-pressed", String(running)); tempo(); };
    bpm.addEventListener("change", tempo);
    const tools = h("div", { class: "kb-tools" },
      h("div", { class: "tool-group" }, octDown, label, octUp), hold,
      h("div", { class: "tool-group" }, play, bpm, h("span", { class: "unit" }, "BPM")),
      h("span", { class: "hint" }, "Keys A–L · Z/X octave · Space freeze · [ ] patches"));
    return h("div", { class: "kbbox" }, tools, kb);
  }

  async midiIn() {
    if (!navigator.requestMIDIAccess) return;
    try {
      const access = await navigator.requestMIDIAccess();
      const hook = (inp) => {
        inp.onmidimessage = (e) => {
          const [s, d1 = 0, d2 = 0] = e.data;
          const t = s & 0xf0;
          if (t === 0x90 && d2 > 0) this.noteOn(d1, d2);
          else if (t === 0x80 || (t === 0x90 && d2 === 0)) this.noteOff(d1);
          else if (t >= 0xa0 && t <= 0xe0) this.engine?.midi([s, d1, d2]);
        };
      };
      access.inputs.forEach(hook);
      access.onstatechange = (e) => { if (e.port.type === "input" && e.port.state === "connected") hook(e.port); };
    } catch { /* MIDI declined: the page and the keyboard still play */ }
  }

  /* -------------------------------------------------------------- patches (the shared browser) */

  patchBar() {
    const meta = this.meta;
    const cats = meta.categories.filter((c) => meta.patches.some((p) => p.cat === c.id));
    const library = { v: 1, categories: cats, banks: [{ id: "drizzle", label: "Drizzle" }], presets: meta.patches.map((p) => ({ name: p.name, cat: p.cat })) };
    const model = new BrowseModel(library, storedFavourites("drizzle.favourites.v1"));
    const current = () => {
      const i = parseInt(this.extra.preset ?? "-1", 10);
      return i >= 0 && i < meta.patches.length ? i : -1;
    };
    const host = {
      noun: "patch",
      nouns: "patches",
      current,
      edited: () => (this.edits || 0) > 0,
      name: () => this.extra.preset_name || "",
      load: (i) => {
        this.edits = 0;
        this.extra.preset = String(i);
        this.link?.send("preset", i);
      },
      onChange: (fn) => this.listeners.add(() => fn()),
      /* the drawer auditions: Esc puts back the sound it opened with, edits included (standalone) */
      begin: async () => {
        visit = { i: current(), edits: this.edits || 0, blob: this.link?.kind === "local" ? await this.link.get("state") : null };
      },
      keep: () => { visit = null; },
      revert: () => {
        if (!visit) return;
        if (visit.i >= 0 && !visit.edits) host.load(visit.i);
        else if (visit.blob) {
          this.link.send("state", visit.blob);
          this.edits = visit.edits;
        }
        visit = null;
      },
    };
    let visit = null;
    // the browse category is the Move's too (pb_cat: 0 = all, n = categories[n - 1])
    model.onChange((what) => {
      if (what !== "scope") return;
      const n = String(cats.findIndex((c) => c.id === model.scope.cat) + 1);
      if (String(this.extra.pb_cat ?? "0") !== n) {
        this.extra.pb_cat = n;
        this.link?.send("pb_cat", n);
      }
    });
    const nav = presetHeader(model, host);
    nav.classList.add("patch-nav");
    const dump = h("button", { type: "button", class: "chip", onclick: () => this.dump() }, "Patch JSON");
    return h("div", { class: "bar" }, h("strong", { class: "brand" }, "Drizzle"), nav, dump);
  }

  dump() {
    const values = {};
    for (const p of this.meta.params) {
      if (p.vol) continue;
      const v = this.values.get(p.k);
      if (Math.abs(v - p.d) < 1e-6) continue;
      values[p.k] = p.t === "enum" ? p.o[Math.round(v)] : p.t === "int" ? Math.round(v) : Math.round(v * 1e4) / 1e4;
    }
    const json = JSON.stringify({ name: this.extra.preset_name || "New Patch", category: "pad", values }, null, 2);
    const box = $("#dump") || document.body.appendChild(h("textarea", { id: "dump", rows: "12", spellcheck: "false" }));
    box.value = json;
    box.scrollIntoView({ behavior: "smooth" });
    navigator.clipboard?.writeText(json).catch(() => {});
  }

  /* -------------------------------------------------------------- boot */

  /* The performance area: the root page's knobs, a big Freeze, the macros and the volume. */
  perform() {
    const root = this.meta.pages.find((p) => p.id === "root");
    const main = h("div", { class: "perf-knobs" });
    for (const k of root.knobs) {
      const key = Array.isArray(k) ? k[0] : k, lab = Array.isArray(k) ? k[1] : undefined;
      if (key === "buf_freeze") continue;
      main.append(this.knob(key, lab, "big"));
    }
    const frz = h("button", { type: "button", class: "freeze", "aria-pressed": "false", title: "Freeze the buffer (Space, CC 69)",
      onclick: () => this.set("buf_freeze", this.values.get("buf_freeze") >= 0.5 ? 0 : 1) }, h("span", { class: "frz-icon" }, "❄"), "Freeze");
    this.bind("buf_freeze", (v) => { frz.classList.toggle("on", v >= 0.5); frz.setAttribute("aria-pressed", String(v >= 0.5)); });
    const macros = h("div", { class: "perf-knobs macros" }, ...["mac1", "mac2", "mac3", "mac4"].map((k) => this.knob(k)), this.knob("patch_vol"));
    return h("div", { class: "perform" },
      h("div", { class: "perf-group" }, h("div", { class: "group-lab" }, "Grains"), main),
      h("div", { class: "perf-group frz-group" }, frz),
      h("div", { class: "perf-group" }, h("div", { class: "group-lab" }, "Macros"), macros));
  }

  /* The sections, as tabs mirroring the Move's section picker. */
  sections(device) {
    const byId = new Map(this.meta.pages.map((p) => [p.id, p]));
    const TABS = [
      ["source", "Source", ["source", "wavetable", "wavetable_mod", "analog", "analog_mod", "sample", "sweep", "source_lfo"]],
      ["grains", "Grains", ["grains", "grain_pitch", "rhythm"]],
      ["voice", "Voice", ["filter", "amp_env", "mod_env", "env_opts", "voice"]],
      ["mod", "Modulation", ["lfo1", "lfo2", "pressure", "perform", device ? "matrix_edit" : "matrix"]],
      ["fx", "Effects", ["crush", "chorus", "delay", "reverb"]],
    ];
    const SRC_PAGES = { wavetable: 0, wavetable_mod: 0, analog: 1, analog_mod: 1, sample: 2 };
    let saved = "source";
    try { saved = localStorage.getItem("drizzle.tab") || saved; } catch { /* no storage */ }
    const bar = h("div", { class: "tabs", role: "tablist" });
    const views = h("div", { class: "tab-views" });
    const show = (id) => {
      for (const b of bar.children) { const on = b.dataset.tab === id; b.classList.toggle("on", on); b.setAttribute("aria-selected", String(on)); }
      for (const v of views.children) v.hidden = v.dataset.tab !== id;
      try { localStorage.setItem("drizzle.tab", id); } catch { /* no storage */ }
    };
    for (const [id, label, pages] of TABS) {
      bar.append(h("button", { type: "button", class: "tab", role: "tab", "data-tab": id, onclick: () => show(id) }, label));
      const grid = h("div", { class: "panels", role: "tabpanel", "data-tab": id });
      for (const pid of pages) {
        if (pid === "matrix") { grid.append(this.matrixPanel()); continue; }
        const pg = byId.get(pid);
        if (!pg) continue;
        const panel = this.panel(pg);
        if (pid in SRC_PAGES) panel.dataset.src = String(SRC_PAGES[pid]);
        grid.append(panel);
      }
      views.append(grid);
    }
    /* only the pages of the source in use */
    this.bind("src_type", (v) => {
      for (const p of views.querySelectorAll("[data-src]")) p.hidden = Number(p.dataset.src) !== Math.round(v);
    });
    show(TABS.some((t) => t[0] === saved) ? saved : "source");
    return h("div", { class: "sections" }, bar, views);
  }

  mount(root, device) {
    root.textContent = "";
    const stage = h("div", { class: "stage" }, this.bufferView());
    root.append(this.patchBar(), stage, this.perform());
    if (!device) root.append(this.keyboard());
    root.append(this.sections(device));
    this.stage = stage;
  }

  async startLocal(button) {
    button.disabled = true;
    try {
      await this.bootEngine();
      this.midiIn();
      button.remove();
      this.stage?.classList.add("running");
      this.keepAwake();
    } catch (err) {
      button.disabled = false;
      button.querySelector(".start-big").textContent = "Start failed: " + err.message;
    }
  }

  /** A new AudioContext and worklet, back on the stored patch. Runs inside a tap (the context may start). */
  async bootEngine() {
    const { Engine } = await import("../engine/engine.js");
    const engine = await Engine.start(new URL("../engine/", here).href);
    const link = new LocalLink(engine, this.meta, () => ({ preset: parseInt(this.extra.preset ?? "-1", 10), edits: this.edits || 0 }));
    link.onValues((vals) => this.receive(vals));
    link.onLoad = () => this.restoreSample();
    const old = this.engine;
    this.engine = engine;
    this.link = link;
    old?.close();
    this.onEngine?.(engine);
    const { edits, preset } = await link.init();
    this.edits = edits;
    if (edits && preset >= 0) this.extra.preset = String(preset); /* "Bit Rain, edited", not "–" */
    for (const fn of this.listeners) fn([]);
    this.restoreSample();
  }

  /* Tablets suspend the page's audio when the screen locks or the tab goes to the background, and do
   * not always resume it. The patch is saved on the way out; on the way back, the next tap resumes the
   * context, or, if it stays silent (a context the browser has closed or left stuck), builds a new one
   * on the saved patch. Until then a "Tap to resume" overlay says why there is no sound. */
  keepAwake() {
    const resumeBtn = h("button", { class: "start resume", hidden: "" }, h("span", { class: "start-big" }, "Tap to resume audio"),
      h("span", { class: "start-small" }, "The browser paused Drizzle while the screen was off"));
    this.stage.append(resumeBtn);
    let busy = false;
    const alive = () => this.engine && this.engine.ctx.state === "running";
    const advancing = async () => {
      const t = this.engine.ctx.currentTime;
      await new Promise((r) => setTimeout(r, 250));
      return this.engine.ctx.currentTime > t;
    };
    const wake = async () => {
      if (busy || !this.engine) return;
      busy = true;
      try {
        const ctx = this.engine.ctx;
        if (ctx.state !== "closed" && ctx.state !== "running") await Promise.race([ctx.resume(), new Promise((r) => setTimeout(r, 400))]);
        if (!alive() || !(await advancing())) {
          for (const n of [...this.held]) this.noteOff(n);
          await this.bootEngine();
        }
      } catch (_) { /* the overlay stays: the next tap tries again */ }
      busy = false;
      resumeBtn.hidden = alive();
    };
    const check = () => { if (document.visibilityState === "visible" && this.engine && !alive()) resumeBtn.hidden = false; };
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "hidden") this.link?.save?.();
      else { check(); wake(); } /* may work without a tap (desktop); else the overlay asks for one */
    });
    window.addEventListener("pagehide", () => this.link?.save?.());
    window.addEventListener("pageshow", () => { check(); });
    /* any tap is a chance to resume: iPad browsers only allow it inside one */
    for (const t of ["pointerup", "touchend", "keydown"]) document.addEventListener(t, () => { if (!alive()) wake(); }, true);
    resumeBtn.addEventListener("click", wake);
    this.engine.ctx.addEventListener("statechange", check);
    this.onEngine = (e) => e.ctx.addEventListener("statechange", check);
  }
}

async function main() {
  const root = $("#app");
  const meta = await fetch(here + "ui_meta.json", { cache: "no-cache" }).then((r) => r.json());
  const app = new App(meta);
  window.drizzle = app; /* for the console and the browser tests */
  const remote = window.schwungRemote || null;
  app.mount(root, !!remote);
  if (remote) {
    const link = new DeviceLink(remote);
    link.onValues((vals) => app.receive(vals));
    app.link = link;
    return;
  }
  /* until Start: the patch the last visit left, so the page does not claim Init */
  const saved = savedPatch();
  if (saved) {
    try {
      const st = JSON.parse(saved.state), vals = {};
      for (const k in st) if (k !== "fmt" && k !== "v" && k !== "name") vals[k] = st[k];
      vals.preset_name = st.name;
      vals.preset = String(saved.preset);
      app.edits = saved.edits;
      app.receive(vals);
    } catch (_) { /* a broken blob: Start loads INIT */ }
  }
  const go = h("button", { class: "start", onclick: () => app.startLocal(go) }, h("span", { class: "start-big" }, "Start audio"),
    h("span", { class: "start-small" }, "Drizzle runs in this browser: tap to hear it"));
  app.stage.append(go);
}

main();
