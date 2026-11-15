#!/usr/bin/env python3
"""Fold build/site into one self-contained HTML file (the shareable browser synth).

  scripts/build_single.py build/site build/drizzle.html

The wasm goes in as base64 and the worklet as source text (globalThis.DRZ_EMBED, read by
web/engine.js), the UI metadata as globalThis.DRZ_META, and engine.js + app.js as one inline module.
Fails if a build path would ship.
"""
import base64
import json
import os
import re
import sys

site, out = sys.argv[1], sys.argv[2]


def read(name, mode="r"):
    with open(os.path.join(site, name), mode, **({} if "b" in mode else {"encoding": "utf-8"})) as f:
        return f.read()


html = read("index.html")
css = read("style.css")
meta = json.loads(read("ui_meta.json"))
wasm = base64.b64encode(read("drizzle.wasm", "rb")).decode("ascii")
worklet = read("worklet.js")
engine = read("engine.js").replace("export class Engine", "class Engine")
app = read("app.js")
app, n = re.subn(r'^import \{ Engine \} from "\./engine\.js";\n', "", app, flags=re.M)
assert n == 1, "app.js no longer imports ./engine.js the way build_single.py expects"


def script_safe(s):
    return s.replace("</script", "<\\/script")


link = '<link rel="stylesheet" href="style.css">'
tag = '<script type="module" src="app.js"></script>'
assert link in html and tag in html, "index.html no longer has the tags build_single.py replaces"
html = html.replace(link, "<style>\n" + css + "</style>")
inline = ("<script>\nglobalThis.DRZ_META = " + json.dumps(meta, separators=(",", ":")) + ";\n"
          "globalThis.DRZ_EMBED = { wasm: \"" + wasm + "\", worklet: " + json.dumps(worklet) + " };\n</script>\n"
          "<script type=\"module\">\n" + script_safe(engine + "\n" + app) + "</script>")
html = html.replace(tag, inline)
for bad in ("/home/", "/Users/", "C:\\\\"):
    assert bad not in html, "a build path would ship in the single file: " + bad
with open(out, "w", encoding="utf-8") as f:
    f.write(html)
print("built %s (%d KB)" % (out, len(html) // 1024))
