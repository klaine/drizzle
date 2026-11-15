// Renders Drizzle's on-device scripts (src/cards.js, src/canvas.js) against a mock of the Schwung draw
// context, as Crest's test does: every card key with several values (and the null "no answer" case),
// the home page and the patch browser. Fails on a throw, a draw outside the frame, or an empty picture.
// With --png DIR it also writes 4x previews for eyeballing.
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const meta = JSON.parse(fs.readFileSync(path.join(ROOT, "build/ui/ui_meta.json"), "utf8"));
const cp = JSON.parse(fs.readFileSync(path.join(ROOT, "build/chain_params.json"), "utf8"));
const pngDir = process.argv.includes("--png") ? process.argv[process.argv.indexOf("--png") + 1] : null;
let fails = 0, checks = 0;
const check = (ok, msg) => { checks++; if (!ok) { fails++; console.log("  FAIL", msg); } };

// 5x7-ish font metrics: 6 px per glyph, like the device's proportional font at worst.
function makeCtx(w, h) {
  const px = new Uint8Array(w * h);
  let outside = 0;
  const set = (x, y, c) => {
    x = Math.round(x); y = Math.round(y);
    if (x < 0 || y < 0 || x >= w || y >= h) { outside++; return; }
    px[y * w + x] = c ? 1 : 0;
  };
  const ctx = {
    width: w, height: h,
    fillRect(x, y, ww, hh, c) { for (let j = 0; j < hh; j++) for (let i = 0; i < ww; i++) set(x + i, y + j, c); },
    print(x, y, text, c) { // a 5x7 box per glyph, with a dot pattern so text shows in previews
      const s = String(text);
      for (let k = 0; k < s.length; k++) if (s[k] !== " ") for (let j = 1; j < 6; j++) for (let i = 0; i < 4; i += 1) if ((i + j + s.charCodeAt(k)) % 3) set(x + k * 6 + i, y + j, c);
    },
    textWidth: (s) => String(s).length * 6,
    line(x0, y0, x1, y1, c) {
      const n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0), 1);
      for (let i = 0; i <= n; i++) set(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, c);
    },
  };
  return { ctx, px, outsideCount: () => outside };
}

function png(file, px, w, h, scale = 4) {
  const W = w * scale, H = h * scale;
  const raw = Buffer.alloc((W + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W + 1)] = 0;
    for (let x = 0; x < W; x++) raw[y * (W + 1) + 1 + x] = px[Math.floor(y / scale) * w + Math.floor(x / scale)] ? 255 : 0;
  }
  const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 0;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}

function load(file) {
  const g = {};
  vm.runInNewContext(fs.readFileSync(path.join(ROOT, "src", file), "utf8"), { globalThis: g, Math, Number, String, Object, isFinite, JSON, parseInt });
  return g;
}

if (pngDir) fs.mkdirSync(pngDir, { recursive: true });
const byKey = Object.fromEntries(meta.params.map((p) => [p.k, p]));
const defaults = Object.fromEntries(meta.params.map((p) => [p.k, String(p.d)]));

// ---- cards
const cards = load("cards.js");
check(typeof cards.drzl === "function", "cards.js exports drzl");
const cardKeys = cp.filter((e) => e.card_script).map((e) => e.key);
check(cardKeys.length > 20, "card keys declared (" + cardKeys.length + ")");
const printedFor = (key, raw, values) => {
  const { ctx, px, outsideCount } = makeCtx(96, 34);
  const out = [];
  const pr = ctx.print;
  ctx.print = (x, y, t, c) => { out.push(String(t)); pr(x, y, t, c); };
  cards.drzl(ctx, { name: byKey[key].n, value: raw, raw, values, w: 96, h: 34, nowMs: 0 });
  return { out, px, outsideCount };
};
for (const key of cardKeys) {
  const p = byKey[key];
  const samples = p.t === "enum" ? [0, p.o.length - 1] : [p.mn, (p.mn + p.mx) / 2, p.mx];
  for (const raw of [...samples.map(String), null]) {
    let r;
    try { r = printedFor(key, raw, { ...defaults, mm_src: "13", mm_dst: "1" }); }
    catch (err) { check(false, `card ${key}=${raw} threw ${err.message}`); continue; }
    check(r.outsideCount() === 0, `card ${key}=${raw} draws outside its frame (${r.outsideCount()} px)`);
    check(r.px.some((v) => v), `card ${key}=${raw} is empty`);
    if (pngDir && raw === String(samples[1] ?? samples[0])) png(path.join(pngDir, `card_${key}.png`), r.px, 96, 34);
  }
}
const read = (key, raw, values = defaults) => printedFor(key, String(raw), values).out.join(" | ");
check(read("gr_size", 0.55).includes("135 ms"), "Size reads ms in Cloud: " + read("gr_size", 0.55));
check(read("gr_size", 0.5, { ...defaults, gr_mode: "1" }).includes("st"), "Size reads grid steps in Tempo: " + read("gr_size", 0.5, { ...defaults, gr_mode: "1" }));
check(read("gr_dens", 0.5).includes("/s"), "Density reads grains/s: " + read("gr_dens", 0.5));
check(read("wt_a_table", 0).includes(meta.tables[0]), "Table reads its name: " + read("wt_a_table", 0));
check(read("src_root", 48).includes("C3"), "Root reads a note: " + read("src_root", 48));
check(read("l1_rate", 0.5, { ...defaults, l1_sync: "3" }).includes("synced"), "a synced LFO says so");
check(read("mm_dep", 0.5, { ...defaults, mm_src: "13", mm_dst: "1" }).includes("Whl>Pos"), "the matrix card names the route: " + read("mm_dep", 0.5, { ...defaults, mm_src: "13", mm_dst: "1" }));

// ---- pages
const canvas = load("canvas.js");
check(canvas.canvas_overlay && typeof canvas.canvas_overlay.drawPage === "function", "canvas.js exports drawPage");
const hex = (n) => n.toString(16).toUpperCase().padStart(2, "0");
const bins = Array.from({ length: 128 }, (_, i) => hex(Math.round(255 * Math.abs(Math.sin(i / 9)) * (0.3 + 0.7 * i / 127)))).join("");
const grains = [10, 60, 70, 200].map((x, i) => hex(x) + hex(80 + 50 * i)).join("");
for (const [w, h] of [[128, 40], [128, 34], [120, 44], [128, 64]]) {
  for (const bv of ["S0FF00" + bins, "L1FF00" + bins, "S040FF" + bins, "", null]) {
    const home = makeCtx(w, h);
    try {
      canvas.canvas_overlay.drawPage(home.ctx, { values: { ...defaults, preset_name: "Glass Tape", buf_view: bv, grain_view: grains }, nowMs: 0 });
    } catch (err) { check(false, `home page ${w}x${h} with ${String(bv).slice(0, 6)} threw ${err.message}`); continue; }
    check(home.outsideCount() === 0 && home.px.some((v) => v), `home page ${w}x${h} (${String(bv).slice(0, 6)}) draws inside its band`);
    if (pngDir && w === 128 && h === 40 && bv && bv.startsWith("S0")) png(path.join(pngDir, "page_home.png"), home.px, w, h);
  }
  const names = meta.patches.map((p) => p.name), n = names.length;
  for (const index of new Set([0, Math.floor(n / 2), n - 1])) {
    const br = makeCtx(w, h);
    canvas.canvas_overlay.drawPage(br.ctx, { values: { preset: String(index) }, preset: { name: names[index], index, count: n, entered: true } });
    check(br.outsideCount() === 0 && br.px.some((v) => v), `browser ${w}x${h} at ${index} draws inside its band`);
    if (pngDir && w === 128 && h === 40 && index === 0) png(path.join(pngDir, "page_browser.png"), br.px, w, h);
  }
}

console.log(fails ? `device ui: ${fails} of ${checks} failed` : `device ui ok: ${checks} checks`);
process.exit(fails ? 1 : 0);
