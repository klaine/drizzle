// Load drizzle.wasm the way web/worklet.js does and play it: it must initialise, answer the
// contract keys, load a patch and make a finite, non-silent sound.   node tests/web/test_wasm.mjs build/drizzle.wasm
import { readFileSync } from "node:fs";

const path = process.argv[2] || "build/drizzle.wasm";
const mod = new WebAssembly.Module(readFileSync(path));
const x = new WebAssembly.Instance(mod, {}).exports;
x._initialize();
let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("  FAIL " + msg); } };
const mem = () => new Uint8Array(x.memory.buffer);
const put = (ptr, s) => { const m = mem(); for (let i = 0; i < s.length; i++) m[ptr + i] = s.charCodeAt(i); m[ptr + s.length] = 0; };
const set = (k, v) => { put(x.wa_key(), k); put(x.wa_val(), String(v)); x.wa_set(); };
const get = (k) => {
  put(x.wa_key(), k);
  const n = x.wa_get();
  return n < 0 ? null : Buffer.from(x.memory.buffer, x.wa_out(), n).toString("latin1");
};

check(x.wa_init(44100) === 1, "wa_init");
const cp = get("chain_params");
check(cp && JSON.parse(cp).length > 100, "chain_params parses");
check(get("ui_hierarchy")?.startsWith('{"levels":'), "ui_hierarchy");
set("preset", 0);
let peak = 0, finite = true;
const render = (blocks) => {
  for (let b = 0; b < blocks; b++) {
    x.wa_render(128);
    const pcm = new Int16Array(x.memory.buffer, x.wa_pcm(), 256);
    for (const v of pcm) { if (!Number.isFinite(v)) finite = false; peak = Math.max(peak, Math.abs(v)); }
  }
};
render(400); // the first tape
x.wa_midi(0x90, 48, 100);
render(400);
check(finite && peak > 300, "a note sounds (peak " + peak + ")");
check(get("buf_view")?.length === 6 + 256, "buf_view");
console.log(fails ? `wasm: ${fails} failed` : "wasm ok: init, contract, patch, sound");
process.exit(fails ? 1 : 0);
