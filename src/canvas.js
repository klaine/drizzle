/* Drizzle: module-drawn pages on the Move display (Schwung `canvas` params with `as_page`, plan/11).
 *
 *  home    the first page of the root level: the patch name and buffer mode, the buffer's peak
 *          picture (buf_view), the Position and Spray window new grains start in, and the grains
 *          playing now (grain_view), redrawn live while the root knobs turn.
 *  browse  the factory-patch browser: the category, where in it, and the patch name.
 *
 * drawPage gets values, never reads (Schwung docs, "Custom UI pages"). Crest's canvas.js is the model.
 */
(function () {
  /* BEGIN GENERATED META */
  var META = {"count":65,"cats":["Init","Pad","Texture","Drone","Keys & Bells","Motion","Rhythm & Glitch"]};
  /* END GENERATED META */

  function num(v) {
    if (v === null || v === undefined || v === "") return null;
    var x = Number(v);
    return isFinite(x) ? x : null;
  }

  function raw(values, key) {
    if (!values) return undefined;
    if (Object.prototype.hasOwnProperty.call(values, key)) return values[key];
    var suffix = ":" + key;
    for (var k in values) if (k.length > suffix.length && k.slice(-suffix.length) === suffix) return values[k];
    return undefined;
  }

  function val(values, key, d) {
    var x = num(raw(values, key));
    return x === null ? d : x;
  }

  function str(values, key) {
    var v = raw(values, key);
    return v === undefined || v === null ? "" : String(v);
  }

  function tw(ctx, s) {
    if (typeof ctx.measureText === "function") return ctx.measureText(s);
    return typeof ctx.textWidth === "function" ? ctx.textWidth(s) : s.length * 6;
  }

  function fit(ctx, s, maxW) {
    s = String(s);
    if (tw(ctx, s) <= maxW) return s;
    while (s.length > 1 && tw(ctx, s + ".") > maxW) s = s.slice(0, -1);
    return s + ".";
  }

  function hex2(s, i) {
    var v = parseInt(s.substr(i, 2), 16);
    return isFinite(v) ? v : 0;
  }

  /* ---------------------------------------------------------------- home */

  function drawHome(ctx, o) {
    var W = ctx.width || 128, H = ctx.height || 40;
    var values = o.values || {};
    var bv = str(values, "buf_view"), gv = str(values, "grain_view");
    var scan = bv.charAt(0) !== "L";
    var frozen = bv.charAt(1) === "1" || val(values, "buf_freeze", 0) >= 0.5;
    var prog = bv.length >= 4 ? hex2(bv, 2) : 255;
    var tag = (scan ? "SCAN" : "LIVE") + (frozen ? "*" : "");
    var name = str(values, "preset_name") || "Drizzle";
    ctx.print(0, 0, fit(ctx, name, W - tw(ctx, tag) - 4), 1);
    ctx.print(W - tw(ctx, tag), 0, tag, 1);
    ctx.fillRect(0, 9, W, 1, 1);
    var top = 11, bh = Math.max(6, H - top - 1), mid = top + Math.floor(bh / 2);
    /* the buffer: 128 peak bins, mirrored round the middle, scaled to the width */
    var nb = bv.length > 6 ? Math.floor((bv.length - 6) / 2) : 0;
    for (var x = 0; x < W && nb > 0; x++) {
      var a = hex2(bv, 6 + 2 * Math.min(nb - 1, Math.floor(x * nb / W))) / 255;
      var hh = Math.max(0, Math.round(a * (bh / 2 - 1)));
      if (hh > 0) ctx.fillRect(x, mid - hh, 1, 2 * hh, 1);
      else ctx.fillRect(x, mid, 1, 1, 1);
    }
    /* a tape still rendering: a dotted line along the part not ready yet */
    if (scan && prog < 255) for (var px = Math.round(prog / 255 * W); px < W; px += 3) ctx.fillRect(px, mid, 1, 1, 0);
    /* where new grains start: Position +- Spray, as brackets (Live counts back from the right edge) */
    var pos = val(values, "gr_pos", 0.5), spray = val(values, "gr_spray", 0.1);
    var cx = scan || frozen ? pos : 1 - pos;
    var x0 = Math.max(0, Math.round((cx - spray) * (W - 1))), x1 = Math.min(W - 1, Math.round((cx + spray) * (W - 1)));
    ctx.fillRect(x0, top, 1, bh, 1);
    ctx.fillRect(x1, top, 1, bh, 1);
    ctx.fillRect(x0, top, Math.min(3, x1 - x0 + 1), 1, 1);
    ctx.fillRect(Math.max(x0, x1 - 2), top, Math.min(3, x1 - x0 + 1), 1, 1);
    /* the grains playing now: a small hollow mark each, higher while the window is open */
    for (var i = 0; i + 4 <= gv.length; i += 4) {
      var gx = Math.round(hex2(gv, i) / 255 * (W - 3)), ga = hex2(gv, i + 2) / 255;
      var gy = top + 1 + Math.round((1 - ga) * (bh - 4));
      ctx.fillRect(gx, gy, 3, 3, 0);
      ctx.fillRect(gx + 1, gy + 1, 1, 1, 1);
    }
  }

  /* ---------------------------------------------------------------- browser */

  var noneAt = null; /* p.index when `preset` was first read as -1 (no factory patch is current) */

  function drawBrowser(ctx, o) {
    var W = ctx.width || 128, H = ctx.height || 40;
    var p = o.preset || {};
    var values = o.values || {};
    var count = META.count || 1;
    var pcount = num(p.count) || 0, pi = num(p.index);
    pi = pi === null ? -1 : Math.round(pi);
    var cat = val(values, "pb_cat", null);
    var all = cat === null ? pcount === count : Math.round(cat) <= 0;
    var cur = val(values, "preset", null);
    if (cur !== null && cur < 0) { if (noneAt === null) noneAt = pi; } else noneAt = null;
    var none = pi < 0 || (noneAt !== null && pi === noneAt);
    var label = all ? "DRIZZLE" : (META.cats[Math.round(cat) - 1] || "").toUpperCase();
    var pos = (none ? "-" : pi + 1) + "/" + (all ? count : pcount);
    ctx.print(0, 0, fit(ctx, label, W - tw(ctx, pos) - 4), 1);
    ctx.print(W - tw(ctx, pos), 0, pos, 1);
    ctx.fillRect(0, 9, W, 1, 1);
    var name = fit(ctx, p.name || "", W - 8);
    var ny = Math.max(12, Math.floor(H / 2) - 6);
    ctx.print(Math.max(0, Math.floor((W - tw(ctx, name)) / 2)), ny + 2, name, 1);
    if (p.entered) {
      ctx.fillRect(2, ny - 1, W - 4, 1, 1);
      ctx.fillRect(2, ny + 11, W - 4, 1, 1);
    }
    /* where in the list: a strip with a notch */
    var n = all ? count : Math.max(1, pcount), sy = H - 4;
    ctx.fillRect(0, sy + 1, W, 1, 1);
    if (!none) ctx.fillRect(Math.round(pi / Math.max(1, n - 1) * (W - 3)), sy, 3, 3, 1);
  }

  globalThis.canvas_overlay = {
    drawPage: function (ctx, o) {
      o = o || {};
      if (o.preset) drawBrowser(ctx, o);
      else drawHome(ctx, o);
    },
  };
})();
