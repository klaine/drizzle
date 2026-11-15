#!/usr/bin/env python3
"""Fold the browser synth into ONE self-contained html file, easy to share (Crest's build_single.py).

    python3 scripts/build_single.py [--out build/drizzle.html]

scripts/build_web.sh runs it after building build/site. The page carries the engine, the worklet,
the UI and its data, so it can be mailed, dropped in a chat or opened from disk. Needs WebAssembly,
AudioWorklet, import maps and DecompressionStream (any current Chrome, Edge, Firefox or Safari);
Web MIDI is Chromium-only.
"""
import argparse
import base64
import gzip
import json
import os
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
UI = ROOT / "src" / "web"
SITE = ROOT / "build" / "site"
HERE = "drizzle:/assets/ui/"  # where the page's app.js thinks it lives (web/embed.js serves it)


def ui_module(path):
    src = path.read_text(encoding="utf-8")
    if path.name == "app.js":
        old = 'const here = new URL(".", import.meta.url).href;'
        assert src.count(old) == 1, "app.js no longer derives `here` from import.meta.url: update build_single.py"
        src = src.replace(old, 'const here = "%s";' % HERE)
    # The modules load from blob: URLs, so relative imports go through the import map instead.
    src = src.replace('import("../engine/engine.js")', 'import("drizzle:/assets/engine/engine.js")')
    src = re.sub(r'(from\s+|import\()"\./([\w-]+\.js)"', lambda m: '%s"%s%s"' % (m[1], HERE, m[2]), src)
    assert not re.search(r'"\.\.?/[\w/-]+\.js"', src), path.name + ": unhandled relative import"
    return src.encode()


def payload_files():
    files = {}
    for p in sorted(UI.glob("*.js")):
        files["assets/ui/" + p.name] = ui_module(p)
    files["assets/ui/ui_meta.json"] = (SITE / "assets" / "ui" / "ui_meta.json").read_bytes()
    for name in ("engine.js", "worklet.js"):
        files["assets/engine/" + name] = (WEB / name).read_bytes()
    files["assets/engine/drizzle.wasm"] = (SITE / "assets" / "engine" / "drizzle.wasm").read_bytes()
    for p in sorted((ROOT / "samples" / "factory").glob("*.mp3")):  # the factory samples (CC0)
        files["assets/samples/" + p.name] = p.read_bytes()
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "drizzle.html"))
    args = ap.parse_args()
    if not (SITE / "assets" / "engine" / "drizzle.wasm").is_file():
        raise SystemExit("build/site is missing: run scripts/build_web.sh")

    files = payload_files()
    blob = b"".join(files.values())
    assert str(ROOT).encode() not in blob and b"/Users/" not in blob, "a build path would ship"

    index = json.dumps([[k, len(v)] for k, v in files.items()]).encode()
    packed = struct.pack("<I", len(index)) + index + blob
    payload = base64.b64encode(gzip.compress(packed, 9, mtime=0)).decode()

    page = (ROOT / "src" / "web_ui.html").read_text(encoding="utf-8")
    css = (UI / "style.css").read_text(encoding="utf-8") + (UI / "presetbrowser.css").read_text(encoding="utf-8")
    embed = (WEB / "embed.js").read_text(encoding="utf-8")
    assert "</style" not in css.lower() and "</script" not in embed.lower()
    link = '<link rel="stylesheet" href="assets/ui/style.css">\n<link rel="stylesheet" href="assets/ui/presetbrowser.css">'
    scripts = ('<script src="/static/schwung-remote-api.js"></script>\n'
               '<script type="module" src="assets/ui/app.js"></script>')
    assert page.count(link) == 1 and page.count(scripts) == 1, "web_ui.html changed: update build_single.py"
    page = page.replace(link, "<style>\n%s</style>" % css)
    page = page.replace(scripts, '<script id="drizzle-payload" type="application/octet-stream">%s</script>\n'
                        '<script>\n%s</script>' % (payload, embed))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    print("wrote %s (%.2f MB, %d embedded files)" % (os.path.relpath(out, ROOT), out.stat().st_size / 1e6, len(files)))


if __name__ == "__main__":
    main()
