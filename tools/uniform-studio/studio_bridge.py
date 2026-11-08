# SPDX-License-Identifier: GPL-3.0-or-later
"""C++ <-> Python bridge for Uniform Studio.

The editor (UniformStudio.exe) invokes this with one argument: a JSON job file.  It reuses the tested
``bake.py`` and ``import_sources.py`` instead of porting them to C++.  Two jobs:

    {"op": "bake",   "out": "<uniform.ppu>", "backup": [10 ints 0..31],
                     "atlases": {"body": "<png>", "cap": "<png>", ...}}
    {"op": "import", "src": "<file.fsh|.iff>", "outdir": "<dir>"}

``import`` writes one PNG per layer into ``outdir`` and an ``index.txt`` of ``filename<TAB>suggest``
lines, which the editor reads back.  Errors go to stderr with a non-zero exit code.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops it, so re-add for bake/import_sources

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import bake  # noqa: E402
import import_sources  # noqa: E402
import packwii  # noqa: E402


def do_bake(job):
    atlases = {kind: Image.open(path).convert("RGBA") for kind, path in job["atlases"].items()}
    backup = [int(c) for c in job["backup"]]
    Path(job["out"]).parent.mkdir(parents=True, exist_ok=True)
    bake.write_ppu(job["out"], atlases, backup)


def do_import(job):
    src = Path(job["src"])
    outdir = Path(job["outdir"])
    outdir.mkdir(parents=True, exist_ok=True)
    ext = src.suffix.lower()
    if ext == ".fsh":
        layers = import_sources.layers_from_fsh(str(src))
    elif ext == ".iff":
        layers = import_sources.layers_from_iff(str(src))
    else:
        raise ValueError("unsupported source type: %s" % ext)
    lines = []
    for i, layer in enumerate(layers):
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", layer["name"]) or "layer"
        name = "%02d_%s.png" % (i, safe)
        layer["rgba"].convert("RGBA").save(outdir / name)
        lines.append("%s\t%s" % (name, layer.get("suggest", "")))
    (outdir / "index.txt").write_text("\n".join(lines), encoding="utf-8")


def do_pack(job):
    # Decode a team uniform as a base layer: the body C8 atlas is common (PWM_mdl_main_Common.pack at
    # 0xDA40, 128x128), each PWM_mdl_uniform_*.pack supplies only the 256 RGB5A3 palette at +0x20.
    common = Path(job["common"]).read_bytes()
    pal = np.frombuffer(Path(job["pack"]).read_bytes(), ">u2", 256, 0x20)
    rgba = packwii.decode(common[0xDA40:], 128, 128, 9, pal)
    outdir = Path(job["outdir"])
    outdir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(outdir / "uniform_from_pack.png")
    (outdir / "index.txt").write_text("uniform_from_pack.png\tbody/chest\n", encoding="utf-8")


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: studio_bridge.py <job.json>\n")
        return 2
    job = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    op = job.get("op")
    if op == "bake":
        do_bake(job)
    elif op == "import":
        do_import(job)
    elif op == "pack":
        do_pack(job)
    else:
        sys.stderr.write("unknown op: %r\n" % op)
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:  # surfaced in the editor's toast
        sys.stderr.write("%s: %s\n" % (type(e).__name__, e))
        sys.exit(1)
