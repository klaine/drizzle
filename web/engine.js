/* Main-thread side of the browser engine: owns the AudioContext and the worklet node running
 * drizzle.wasm, and wraps the worklet's message protocol (web/worklet.js). Crest's web/engine.js.
 *
 * The assets come from files next to the page (build/site), or, in the single-file build
 * (build/drizzle.html), from globalThis.DRZ_EMBED = { wasm: base64, worklet: source }. */

function b64bytes(s) {
  const bin = atob(s);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out.buffer;
}

export class Engine {
  constructor(ctx, node) {
    this.ctx = ctx;
    this.node = node;
    this.pending = new Map();
    this.nextId = 1;
    this.onPeak = null;
    node.port.onmessage = (e) => {
      const d = e.data;
      if (d.t === "got") {
        const done = this.pending.get(d.id);
        this.pending.delete(d.id);
        if (done) done(d.val);
      } else if (d.t === "peak" && this.onPeak) this.onPeak(d.v);
    };
  }

  /** Must run inside a user gesture so the AudioContext may start. base = folder of the engine files. */
  static async start(base) {
    const ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    const embed = globalThis.DRZ_EMBED;
    let wasm;
    if (embed) {
      wasm = b64bytes(embed.wasm);
      const url = URL.createObjectURL(new Blob([embed.worklet], { type: "text/javascript" }));
      await ctx.audioWorklet.addModule(url);
    } else {
      [wasm] = await Promise.all([
        fetch(base + "drizzle.wasm").then((r) => {
          if (!r.ok) throw new Error("drizzle.wasm: HTTP " + r.status);
          return r.arrayBuffer();
        }),
        ctx.audioWorklet.addModule(base + "worklet.js"),
      ]);
    }
    const node = new AudioWorkletNode(ctx, "drizzle", {
      numberOfInputs: 0,
      outputChannelCount: [2],
      processorOptions: { wasm },
    });
    const info = await new Promise((resolve) => {
      node.port.onmessage = (e) => { if (e.data.t === "ready") resolve(e.data); };
    });
    if (!info.ok) throw new Error("engine failed to start: " + (info.error || "wa_init"));
    node.connect(ctx.destination);
    if (ctx.state !== "running") await ctx.resume();
    return new Engine(ctx, node);
  }

  set(key, val) { this.node.port.postMessage({ t: "set", key, val: String(val) }); }

  get(key) {
    const id = this.nextId++;
    return new Promise((resolve) => {
      this.pending.set(id, resolve);
      this.node.port.postMessage({ t: "get", id, key });
    });
  }

  midi(m) { this.node.port.postMessage({ t: "midi", m }); }

  tempo(bpm, run) { this.node.port.postMessage({ t: "tempo", bpm, run }); }
}
