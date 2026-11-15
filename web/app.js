/* Drizzle in the browser: the engine compiled to WebAssembly, played from this page, the computer
 * keyboard or Web MIDI (plan/11 section 8). Every page and control comes from build/ui/ui_meta.json,
 * which tools/gen/gen.py writes from tools/gen/params.py, so new parameters appear here by themselves.
 *
 * Also the factory-patch authoring tool: "Patch JSON" gives the current sound as a
 * presets/factory/*.json file (only the values that differ from INIT). */
import { Engine } from "./engine.js";

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

function reading(p, v) {
  if (p.t === "enum") return p.o[Math.round(v)] ?? String(v);
  const f = p.f;
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
    default: return num(v, p.u);
  }
}

/* ------------------------------------------------------------------ the app */

class App {
  constructor(meta) {
    this.meta = meta;
    this.byKey = new Map(meta.params.map((p, i) => [p.k, { ...p, i }]));
    this.values = new Map(meta.params.map((p) => [p.k, p.d]));
    this.controls = new Map(); // key -> [update functions]
    this.engine = null;
    this.octave = 4;
    this.held = new Set();
  }

  set(key, v) {
    this.values.set(key, v);
    this.engine?.set(key, v);
    for (const fn of this.controls.get(key) || []) fn(v);
    if (key === "mm_slot" || key === "mm_clear") this.refresh(["mm_src", "mm_via", "mm_dst", "mm_dep", "mm_slot", "mm_clear"]);
  }

  /** Read some keys back from the engine (the matrix proxies follow the selected slot). */
  async refresh(keys) {
    for (const k of keys) {
      const s = await this.engine?.get(k);
      if (s === null || s === undefined) continue;
      const v = parseFloat(s);
      this.values.set(k, v);
      for (const fn of this.controls.get(k) || []) fn(v);
    }
  }

  /** Read every parameter back from the engine (after a patch or state load). */
  async pull() {
    for (const p of this.meta.params) {
      const s = await this.engine.get(p.k);
      if (s === null) continue;
      const v = parseFloat(s);
      this.values.set(p.k, v);
      for (const fn of this.controls.get(p.k) || []) fn(v);
    }
    this.name.textContent = (await this.engine.get("preset_name")) || "";
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
      out.textContent = reading(p, v);
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
    const status = h("div", { class: "bufstatus" });
    const draw = async () => {
      if (!this.engine) return;
      const [bv, gv] = await Promise.all([this.engine.get("buf_view"), this.engine.get("grain_view")]);
      const W = c.width, H = c.height;
      const css = getComputedStyle(document.body);
      ctx.fillStyle = css.getPropertyValue("--panel") || "#111";
      ctx.fillRect(0, 0, W, H);
      if (bv && bv.length >= 6) {
        const mode = bv[0] === "S" ? "Scan" : "Live", frozen = bv[1] === "1";
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
        // Position and spray: where new grains start
        const pos = this.values.get("gr_pos"), spr = this.values.get("gr_spray");
        const x = mode === "Scan" ? pos : 1 - pos;
        ctx.strokeStyle = css.getPropertyValue("--accent") || "#fc6";
        ctx.lineWidth = 2;
        ctx.strokeRect((x - spr) * W, 2, Math.max(2, 2 * spr * W), H - 4);
        status.textContent = `${mode}${frozen ? " · frozen" : ""}${prog < 255 ? " · rendering tape" : ""} · ` +
          `${reading(this.byKey.get("buf_len"), this.values.get("buf_len"))}`;
      }
      if (gv) {
        ctx.fillStyle = css.getPropertyValue("--grain") || "#fff";
        for (let i = 0; i + 4 <= gv.length; i += 4) {
          const gx = parseInt(gv.substr(i, 2), 16) / 255, ga = parseInt(gv.substr(i + 2, 2), 16) / 255;
          ctx.beginPath();
          ctx.arc(gx * W, H - 12 - ga * (H - 24), 3 + 4 * ga, 0, 2 * Math.PI);
          ctx.fill();
        }
      }
    };
    const loop = async () => { await draw(); setTimeout(() => requestAnimationFrame(loop), 40); };
    loop();
    return h("div", { class: "bufbox" }, c, status);
  }

  /* -------------------------------------------------------------- playing */

  noteOn(n, vel = 100) {
    if (this.held.has(n)) return;
    this.held.add(n);
    this.engine?.midi([0x90, n, vel]);
    document.querySelector(`[data-note="${n}"]`)?.classList.add("on");
  }

  noteOff(n) {
    if (!this.held.delete(n)) return;
    this.engine?.midi([0x80, n, 0]);
    document.querySelector(`[data-note="${n}"]`)?.classList.remove("on");
  }

  keyboard() {
    const kb = h("div", { class: "keys" });
    const base = 48;
    for (let n = base; n < base + 25; n++) {
      const black = [1, 3, 6, 8, 10].includes(n % 12);
      const k = h("div", { class: "key" + (black ? " black" : ""), "data-note": String(n) });
      k.addEventListener("pointerdown", (e) => { k.setPointerCapture(e.pointerId); this.noteOn(n, 100); });
      k.addEventListener("pointerup", () => this.noteOff(n));
      k.addEventListener("pointercancel", () => this.noteOff(n));
      kb.append(k);
    }
    const map = "awsedftgyhujkolp;'";
    window.addEventListener("keydown", (e) => {
      if (e.repeat || e.target.tagName === "INPUT" || e.target.tagName === "SELECT" || e.target.tagName === "TEXTAREA") return;
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
      h("p", { class: "hint" }, "Keys A–L play (W E T Y U O P sharps), Z / X octave, Space freezes."));
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

  /* -------------------------------------------------------------- patches */

  patchBar() {
    const sel = h("select", { class: "patches", onchange: async () => {
      this.engine?.set("preset", sel.value);
      await this.pull();
    } }, ...this.meta.patches.map((p, i) => h("option", { value: String(i) }, `${p.name} (${p.cat})`)));
    this.name = h("span", { class: "name" }, "Init");
    const dump = h("button", { onclick: () => this.dump() }, "Patch JSON");
    return h("div", { class: "bar" }, h("strong", {}, "Drizzle"), this.name, sel, dump);
  }

  dump() {
    const values = {};
    for (const p of this.meta.params) {
      if (p.vol) continue;
      const v = this.values.get(p.k);
      if (Math.abs(v - p.d) < 1e-6) continue;
      values[p.k] = p.t === "enum" ? p.o[Math.round(v)] : p.t === "int" ? Math.round(v) : Math.round(v * 1e4) / 1e4;
    }
    const json = JSON.stringify({ name: this.name.textContent || "New Patch", category: "pad", values }, null, 2);
    const box = $("#dump") || document.body.appendChild(h("textarea", { id: "dump", rows: "12", spellcheck: "false" }));
    box.value = json;
    box.scrollIntoView({ behavior: "smooth" });
    navigator.clipboard?.writeText(json).catch(() => {});
  }

  /* -------------------------------------------------------------- boot */

  mount(root) {
    root.textContent = "";
    root.append(this.patchBar(), this.bufferView(), this.keyboard());
    const pages = h("div", { class: "pages" });
    for (const pg of this.meta.pages) pages.append(this.page(pg));
    root.append(pages);
  }

  async start(button) {
    button.disabled = true;
    try {
      this.engine = await Engine.start(here);
      for (const [k, v] of this.values) if (v !== this.byKey.get(k).d) this.engine.set(k, v);
      await this.pull();
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
  const meta = globalThis.DRZ_META || await fetch(here + "ui_meta.json").then((r) => r.json());
  const app = new App(meta);
  app.mount(root);
  const go = h("button", { class: "start", onclick: () => app.start(go) }, "Start audio");
  root.prepend(go);
}

main();
