# SPDX-License-Identifier: GPL-3.0-or-later
"""C++ <-> Python bridge for Uniform Studio.

The editor (UniformStudio.exe) invokes this with one argument: a JSON job file.  It reuses the tested
``bake.py`` and ``import_sources.py`` instead of porting them to C++.  Two jobs:

    {"op": "bake",   "out": "<uniform.ppu>", "backup": [10 ints 0..31],
                     "atlases": {"body": "<png>", "cap": "<png>", ...}}
    {"op": "import", "src": "<file.fsh|.iff>", "outdir": "<dir>"}
    {"op": "gamebase", "project": "<the player's project.bin>", "out": "<cache file>"}
    {"op": "gamedoll", "project": "<the player's project.bin>", "out": "<cache file>"}
    {"op": "readppu",  "ppu": "<uniform.ppu>", "outdir": "<dir>"}

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
    layout = job.get("layout")
    bake.write_ppu(job["out"], atlases, backup, int(job.get("flags", 0)) & 1, tuple(layout) if layout else None)


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


def do_readppu(job):
    """A saved uniform.ppu back to editable images (an older uniform without a Studio project):
    one PNG per atlas (body, cap, helmet, number, thumb) and meta.txt with its backup bytes, flags and
    number layout. Only the player's own saved file is read."""
    r = bake.read_ppu(job["ppu"])
    outdir = Path(job["outdir"])
    outdir.mkdir(parents=True, exist_ok=True)
    for kind, (w, h, pal, px) in r["atlases"].items():
        palette = np.frombuffer(pal, ">u2")
        rgba = packwii.decode(px, w, h, bake.C8, palette)
        Image.fromarray(np.ascontiguousarray(rgba, np.uint8), "RGBA").save(outdir / (kind + ".png"))
    layout = r.get("layout") or ()
    (outdir / "meta.txt").write_text(
        "backup\t" + "\t".join(str(b) for b in r["backup"]) + "\n" +
        "flags\t" + str(r.get("flags", 0)) + "\n" +
        ("layout\t" + "\t".join(str(v) for v in layout) + "\n" if layout else ""), encoding="utf-8")


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


# The pieces of an Arrange uniform, read from the PLAYER'S OWN game (project.bin) into a local cache under
# UserData: the editor colours the body atlas piece by piece as the game's Arrange editor does. Nothing of it
# is shipped or committed. Cache layout ("PPGB", version 2):
#   magic 4 "PPGB", version u32 BE = 2,
#   16384 B   body atlas C8 indices, row-major 128x128 (PWM_mdl_main_Common.pack, texels at 0xDA40, GX-tiled 8x4)
#   512 B     Arrange template palette, 256 x RGB5A3 BE (PWM_arrange_uniform.pack +0x20)
#   288 B     32 colours x {light, mid, dark} RGB (PWM_arrange_color.pack: "1UAP" at +0x20, 12-byte entries at +0x24)
#   4096 B    small (chest) number digits, C8 64x64 row-major (Common texels at 0x2AA00)
#   16384 B   back number digits, C8 128x128 row-major (Common texels at 0x267C0)
#   512 B     number template palette, 256 x RGB5A3 BE (PWM_arrange_uniform.pack +0x240)
GAMEBASE_MAGIC = b"PPGB"


def do_gamebase(job):
    sys.path.append(str(Path(__file__).resolve().parent.parent))  # beside the exe (build.ps1), else tools/
    from projectbin import Archive  # noqa: E402
    archive = Archive(job["project"])
    common = archive.read("PWM_mdl_main_Common.pack")
    uniform = archive.read("PWM_arrange_uniform.pack")
    colors = archive.read("PWM_arrange_color.pack")
    if colors[0x20:0x24] != b"1UAP":
        raise ValueError("PWM_arrange_color.pack: unexpected header")
    def untile_c8(off, w, h):
        tiled = common[off:off + w * h]
        out = bytearray(w * h)
        k = 0
        for by in range(0, h, 4):
            for bx in range(0, w, 8):
                for y in range(4):
                    for x in range(8):
                        out[(by + y) * w + bx + x] = tiled[k]
                        k += 1
        return bytes(out)

    index = untile_c8(0xDA40, 128, 128)
    table = bytearray()
    for i in range(32):
        e = colors[0x24 + i * 12:0x24 + i * 12 + 12]
        table += e[0:3] + e[4:7] + e[8:11]
    out = Path(job["out"])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(GAMEBASE_MAGIC + (2).to_bytes(4, "big") + index + uniform[0x20:0x220] + bytes(table) +
                    untile_c8(0x2AA00, 64, 64) + untile_c8(0x267C0, 128, 128) + uniform[0x240:0x440])


# The game's chibi figure for the 3D preview (game_doll.py), from the PLAYER'S OWN game into a local cache
# under UserData; nothing of it ships or is committed. Cache layout ("PPDL", version 1, little-endian):
#   magic 4 "PPDL", u32 version = 1, u32 texture count T,
#   T x {u16 w, u16 h, w*h*4 RGBA}       the figure's own small textures (face, hands, ...)
#   u32 triangle count N, N x {3 x (f32 x, y, z, u, v), u8 material}
#   material: 0 the uniform body atlas, 3 outline hull (black), 4 the cap letter, 5 + i texture i above
DOLL_MAGIC = b"PPDL"


def do_gamedoll(job):
    import struct
    here = Path(__file__).resolve().parent
    sys.path.append(str(here.parent))  # projectbin.py beside the exe (build.ps1), else tools/
    from projectbin import Archive  # noqa: E402
    import game_doll  # noqa: E402
    pj7m = game_doll.pj7m
    archive = Archive(job["project"])
    doll = game_doll.assemble(archive)
    d = archive.read(game_doll.PACK)
    models = {"body": game_doll.BODY}
    models.update({part: name for part, name, _ in game_doll.PARTS})
    tags, images = {}, []
    for tri in doll["triangles"]:
        tag = tri["texture"]
        if tag in (None, "body", "cap") or tag in tags:
            continue
        part, rest = tag.split(":")
        index = int(rest.split("#")[1])
        model = pj7m.parse_model(d, game_doll.find_model(d, models[part]))
        t = model.textures[index]
        palette = None
        if t.palette is not None:
            palette = np.frombuffer(d, ">u2", t.palette_size, t.palette)
        rgba = packwii.decode(d[t.pixels:], t.width, t.height, t.fmt, palette)
        tags[tag] = len(images)
        images.append((t.width, t.height, np.ascontiguousarray(rgba, np.uint8).tobytes()))
    out = bytearray(DOLL_MAGIC + struct.pack("<II", 1, len(images)))
    for w, h, px in images:
        out += struct.pack("<HH", w, h) + px
    out += struct.pack("<I", len(doll["triangles"]))
    for tri in doll["triangles"]:
        tag = tri["texture"]
        mat = 3 if tri["outline"] else 0 if tag in (None, "body") else 4 if tag == "cap" else 5 + tags[tag]
        for p, uv in zip(tri["pos"], tri["uv"]):
            out += struct.pack("<5f", *p, *uv)
        out += bytes([mat])
    path = Path(job["out"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


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
    elif op == "readppu":
        do_readppu(job)
    elif op == "gamebase":
        do_gamebase(job)
    elif op == "gamedoll":
        do_gamedoll(job)
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
