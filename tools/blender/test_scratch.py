# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless test of stadiums from scratch (scratch.py): a collection of simple shapes, one per role,
built into a stadium description, written and read back.

blender --background --factory-startup --python tools/blender/test_scratch.py -- <addon.zip> <project.bin> <temp dir>
Exits 1 on failure.
"""
import importlib
import math
import sys
from pathlib import Path

import bpy

zip_path, project_bin, temp_dir = sys.argv[sys.argv.index("--") + 1:][:3]
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))          # tools/: stadium_re
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
scratch = importlib.import_module(pkg + ".scratch")
stadium_read = importlib.import_module(pkg + ".stadium_read")
stadium_write = importlib.import_module(pkg + ".stadium_write")
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)


def material(name, rgb, image=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1.0)
    if image is not None:
        m.node_tree.nodes.new("ShaderNodeTexImage").image = image
    return m


def image(name, rgba, size=8):
    img = bpy.data.images.new(name, size, size, alpha=True)
    img.pixels.foreach_set(list(rgba) * size * size)
    return img


def mesh_object(coll, name, verts, faces, mat, role="auto", hide=False):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.uv_layers.new(name="UVMap")
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me)
    coll.objects.link(o)
    o.powerpros_role = role
    o.powerpros_hide_above = hide
    return o


def box(x, y, z, sx, sy, sz):
    vs = [(x + dx * sx, y + dy * sy, z + dz * sz) for dz in (0, 1) for dy in (0, 1) for dx in (0, 1)]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    return vs, faces


def scratch_collection(name):
    coll = bpy.data.collections.new(name)
    coll["pp_scratch"] = 1
    coll["pp_stadium"] = "col"
    coll["pp_project_bin"] = project_bin
    bpy.context.scene.collection.children.link(coll)
    return coll


coll = scratch_collection("Desde cero")
grass = material("cesped", (0.05, 0.35, 0.05))
grey = material("gris", (0.5, 0.5, 0.5))
mesh_object(coll, "campo", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
n = 32
ring = [(75 * math.sin(2 * math.pi * k / n), 45 + 75 * math.cos(2 * math.pi * k / n)) for k in range(n)]
fence_v = [(x, y, 0.0) for x, y in ring] + [(x, y, 3.0) for x, y in ring]
mesh_object(coll, "valla", fence_v, [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)], grey, "fence")
for i, x in enumerate((-30.0, 30.0)):
    mesh_object(coll, f"grada{i}", *box(x, 125, 0, 20, 10, 15), grey, "stand")
mesh_object(coll, "Grada_Peñarol", *box(0, 128, 0, 15, 10, 18), grey)               # auto: not flat -> stand
crowd = image("publico", (0.8, 0.3, 0.3, 1.0))
mesh_object(coll, "publico", [(-20, 120, 2), (20, 120, 2), (20, 121, 12), (-20, 121, 12)], [(0, 1, 2, 3)],
            material("publico", (1, 1, 1), crowd), "crowd_upright")
glass = image("cristal", (0.6, 0.8, 1.0, 0.4))
mesh_object(coll, "cristal", [(-10, 110, 20), (10, 110, 20), (10, 110, 25), (-10, 110, 25)], [(0, 1, 2, 3)],
            material("cristal", (1, 1, 1), glass), "transparent")
mesh_object(coll, "brillo", [(-5, 130, 30), (5, 130, 30), (5, 130, 35), (-5, 130, 35)], [(0, 1, 2, 3)],
            material("brillo", (1, 1, 0.6)), "additive")
mesh_object(coll, "bandera", [(40, 120, 20), (42, 120, 20), (42, 120, 21), (40, 120, 21)], [(0, 1, 2, 3)],
            material("bandera", (0.8, 0.1, 0.1)), "flags")
mesh_object(coll, "edificio", *box(-150, 200, 0, 30, 30, 40), grey, "stand", hide=True)
text = bpy.data.objects.new("rotulo", bpy.data.curves.new("rotulo", "FONT"))
text.location = (0, 135, 20)
coll.objects.link(text)
coll.objects.link(bpy.data.objects.new("vacio", None))

st, hd, warnings = scratch.build_stadium(coll)
names = [m.name for m in st.models]
check(all(n.isascii() for n in names), f"piece names are ASCII: {names}")
by_name = {st.models[p.model].name: p.flags for p in st.placements}
for role, (flags, _, _) in scratch.ROLES.items():
    if role == "crowd_lying":
        continue
    hit = [nm for nm, f in by_name.items() if nm.startswith(f"DP_{role}_") and not nm.endswith("_up")]
    check(hit and all(by_name[nm] == flags for nm in hit), f"role {role}: pieces {hit} with flags 0x{flags:08X}")
up = [nm for nm in by_name if nm.endswith("_up")]
check(len(up) == 1 and by_name[up[0]] == scratch.ROLES["stand"][0] | scratch.HIDE_ABOVE,
      f"hide from above: {up} with bit 0x80000000")
stands = [nm for nm in by_name if nm.startswith("DP_stand_") and not nm.endswith("_up")]
check(len(stands) == 1, f"the stand objects (two boxes, Grada_Peñarol, the text) are one piece: {stands}")
def piece_role(nm):
    """DP_<role>_<n>[_up] -> role."""
    core = nm[3:-3] if nm.endswith("_up") else nm[3:]
    return core.rsplit("_", 1)[0]


placed = [st.models[p.model].name for p in st.placements]
ranks = [scratch.DRAW_ORDER.index(piece_role(nm)) for nm in placed if "BCL" not in nm]
bcl = [i for i, nm in enumerate(placed) if "BCL" in nm]
check(ranks == sorted(ranks) and bcl and min(bcl) == 1, f"placements in draw order, the bases right after the ground: {placed}")
check(any("BCL" in nm for nm in names), "the game's bases, plate and mound are there")
check(len(st.collision_triangles) > 0, f"collision: {len(st.collision_triangles)} triangles")
check(not warnings, f"no warnings: {warnings}")

gfx, tbl = stadium_write.write_stadium(st)
back = stadium_read.read_stadium(gfx, tbl)
back.tree_a = st.tree_a
check(back == st, "written and read back the same")
import stadium_re                                                    # noqa: E402
problems = []
models, _ = stadium_re.load("scratch", gfx, tbl, problems)
for m in models:
    stadium_re.check_model(gfx, m, problems)
check(not problems, f"stadium_re finds no problems {problems[:3]}")

st2, _, _ = scratch.build_stadium(coll, game_bases=False)
check(not any("BCL" in m.name for m in st2.models), "without game bases there are no BCL pieces")

big = scratch_collection("Muchos")
mesh_object(big, "campo2", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
mesh_object(big, "valla2", fence_v, [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)], grey, "fence")
imgs = [image(f"t{k}", (k / 40, 0.5, 0.5, 1.0)) for k in range(40)]
for k in range(300):
    mesh_object(big, f"cubo{k}", *box(-100 + (k % 30) * 7, 140 + (k // 30) * 7, 0, 5, 5, 5),
                material(f"m{k}", (1, 1, 1), imgs[k % 40]), "stand")
st3, _, w3 = scratch.build_stadium(big, game_bases=False)
check(len(st3.placements) < 127 and all(len(m.texmaps) <= 30 for m in st3.models),
      f"300 boxes with 40 textures: {len(st3.placements)} pieces of at most 30 texmaps")

print("RESULT: " + ("FAIL" if failures else "PASS"))
sys.exit(1 if failures else 0)
