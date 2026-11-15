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

/* drizzle.wasm in this browser, the patch kept in localStorage between visits. */
const SAVE_KEY = "drizzle.state.v1";

class LocalLink {
  constructor(engine, meta) {
    this.engine = engine;
    this.meta = meta;
    this.kind = "local";
    this.cb = () => {};
    this.saveTimer = 0;
  }

  async init() {
    try {
      const saved = localStorage.getItem(SAVE_KEY);
      if (saved) this.engine.set("state", saved);
    } catch (_) { /* storage blocked: start from INIT */ }
    await this.pull();
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
    if (key === "preset" || key === "state" || key === "pb_index") this.pull();
    else if (key === "mm_slot" || key === "mm_sel" || key === "mm_clear") this.pull(["mm_src", "mm_via", "mm_dst", "mm_dep", "mm_slot", "mm_clear"]);
    clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(async () => {
      try {
        const st = await this.engine.get("state");
        if (st) localStorage.setItem(SAVE_KEY, st);
      } catch (_) { /* quota or blocked storage */ }
    }, 800);
  }

  get(key) { return this.engine.get(key); }
}

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

  control(key, label) {
    const p = this.byKey.get(key);
    if (!p) return h("div", { class: "ctl missing" }, key);
    const out = h("output", {});
    const lab = h("label", {}, label || p.s);
    lab.title = p.n;
    let input;
    if (p.t === "enum") {
      input = h("select", { onchange: () => this.set(key, Number(input.value)) },
        ...p.o.map((o, i) => h("option", { value: String(i) }, o)));
    } else {
      const step = p.t === "int" ? 1 : (p.mx - p.mn) / 1000;
      input = h("input", { type: "range", min: String(p.mn), max: String(p.mx), step: String(step) });
      input.addEventListener("input", () => this.set(key, Number(input.value)));
      input.addEventListener("dblclick", () => this.set(key, p.d));
    }
    const update = (v) => {
      input.value = String(v);
      out.textContent = reading(p, v, this.meta, this.values);
    };
    if (!this.controls.has(key)) this.controls.set(key, []);
    this.controls.get(key).push(update);
    update(this.values.get(key));
    return h("div", { class: "ctl" }, lab, input, out);
  }

  page(pg) {
    const box = h("section", { class: "page" }, h("h2", {}, pg.label));
    const grid = h("div", { class: "knobs" });
    for (const k of pg.knobs) grid.append(Array.isArray(k) ? this.control(k[0], k[1]) : this.control(k));
    box.append(grid);
    return box;
  }

  /* -------------------------------------------------------------- buffer view (plan/11 section 4) */

  bufferView() {
    const c = h("canvas", { width: "1024", height: "200", class: "bufview" });
    const ctx = c.getContext("2d");
    const status = h("div", { class: "bufstatus" }, "");
    const draw = () => {
      const bv = this.extra.buf_view || "", gv = this.extra.grain_view || "";
      const W = c.width, H = c.height;
      const css = getComputedStyle(document.body);
      ctx.fillStyle = css.getPropertyValue("--panel") || "#111";
      ctx.fillRect(0, 0, W, H);
      if (bv.length >= 6) {
        const scan = bv[0] === "S", frozen = bv[1] === "1";
        const prog = parseInt(bv.slice(2, 4), 16);
        ctx.fillStyle = css.getPropertyValue("--wave") || "#6af";
        const n = (bv.length - 6) / 2, bw = W / n;
        for (let i = 0; i < n; i++) {
          const a = parseInt(bv.substr(6 + 2 * i, 2), 16) / 255;
          const hh = Math.max(1, a * (H * 0.45));
          ctx.fillRect(i * bw, H / 2 - hh, bw - 1, 2 * hh);
        }
        if (prog < 255) {
          ctx.fillStyle = css.getPropertyValue("--accent") || "#fc6";
          ctx.fillRect(0, H - 4, W * prog / 255, 4);
        }
        const pos = this.values.get("gr_pos"), spr = this.values.get("gr_spray");
        const x = scan || frozen ? pos : 1 - pos;
        ctx.strokeStyle = css.getPropertyValue("--accent") || "#fc6";
        ctx.lineWidth = 2;
        ctx.strokeRect((x - spr) * W, 2, Math.max(2, 2 * spr * W), H - 4);
        status.textContent = `${scan ? "Scan" : "Live"}${frozen ? " · frozen" : ""}${prog < 255 ? " · rendering tape" : ""} · ` +
          reading(this.byKey.get("buf_len"), this.values.get("buf_len"), this.meta);
      } else {
        status.textContent = this.link ? "" : "Press Start audio to play Drizzle in this browser.";
      }
      ctx.fillStyle = css.getPropertyValue("--grain") || "#fff";
      for (let i = 0; i + 4 <= gv.length; i += 4) {
        const gx = parseInt(gv.substr(i, 2), 16) / 255, ga = parseInt(gv.substr(i + 2, 2), 16) / 255;
        ctx.beginPath();
        ctx.arc(gx * W, H - 12 - ga * (H - 24), 3 + 4 * ga, 0, 2 * Math.PI);
        ctx.fill();
      }
    };
    /* the engine here is asked 25 times a second; the Move pushes these values itself */
    const loop = async () => {
      if (this.link?.kind === "local") {
        const [bv, gv] = await Promise.all([this.link.get("buf_view"), this.link.get("grain_view")]);
        this.extra.buf_view = bv || "";
        this.extra.grain_view = gv || "";
      }
      draw();
      setTimeout(() => requestAnimationFrame(loop), 40);
    };
    loop();
    return h("div", { class: "bufbox" }, c, status);
  }

  /* -------------------------------------------------------------- playing (standalone only) */

  noteOn(n, vel = 100) {
    if (this.held.has(n) || !this.engine) return;
    this.held.add(n);
    this.engine.midi([0x90, n, vel]);
    document.querySelector(`[data-note="${n}"]`)?.classList.add("on");
  }

  noteOff(n) {
    if (!this.held.delete(n)) return;
    this.engine?.midi([0x80, n, 0]);
    document.querySelector(`[data-note="${n}"]`)?.classList.remove("on");
  }

  keyboard() {
    const kb = h("div", { class: "keys" });
    for (let n = 48; n < 73; n++) {
      const black = [1, 3, 6, 8, 10].includes(n % 12);
      const k = h("div", { class: "key" + (black ? " black" : ""), "data-note": String(n) });
      k.addEventListener("pointerdown", (e) => { k.setPointerCapture(e.pointerId); this.noteOn(n, 100); });
      k.addEventListener("pointerup", () => this.noteOff(n));
      k.addEventListener("pointercancel", () => this.noteOff(n));
      kb.append(k);
    }
    const map = "awsedftgyhujkolp;'";
    window.addEventListener("keydown", (e) => {
      if (e.repeat || e.metaKey || e.ctrlKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
      if (e.key === "z") { this.octave = Math.max(1, this.octave - 1); return; }
      if (e.key === "x") { this.octave = Math.min(7, this.octave + 1); return; }
      if (e.key === " ") { e.preventDefault(); this.set("buf_freeze", this.values.get("buf_freeze") >= 0.5 ? 0 : 1); return; }
      const i = map.indexOf(e.key.toLowerCase());
      if (i >= 0) this.noteOn(12 * (this.octave + 1) + i, 100);
    });
    window.addEventListener("keyup", (e) => {
      const i = map.indexOf(e.key.toLowerCase());
      if (i >= 0) for (let o = 1; o <= 7; o++) this.noteOff(12 * (o + 1) + i);
    });
    return h("div", { class: "kbbox" }, kb,
      h("p", { class: "hint" }, "Keys A–L play (W E T Y U O P sharps), Z / X octave, Space freezes, [ ] step patches."));
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

  mount(root, device) {
    root.textContent = "";
    root.append(this.patchBar(), this.bufferView());
    if (!device) root.append(this.keyboard());
    const pages = h("div", { class: "pages" });
    for (const pg of this.meta.pages) pages.append(this.page(pg));
    root.append(pages);
  }

  async startLocal(button) {
    button.disabled = true;
    try {
      const { Engine } = await import("../engine/engine.js");
      this.engine = await Engine.start(new URL("../engine/", here).href);
      const link = new LocalLink(this.engine, this.meta);
      link.onValues((vals) => this.receive(vals));
      this.link = link;
      await link.init();
      this.midiIn();
      button.remove();
    } catch (err) {
      button.disabled = false;
      button.textContent = "Start failed: " + err.message;
    }
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
  const go = h("button", { class: "start", onclick: () => app.startLocal(go) }, "Start audio");
  root.prepend(go);
}

main();
