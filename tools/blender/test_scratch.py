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
mesh_object(coll, "publico_tumbado", [(-20, 112, 1), (20, 112, 1), (20, 118, 3), (-20, 118, 3)], [(0, 1, 2, 3)],
            material("publico2", (1, 1, 1), crowd), "crowd_lying")
glass = image("cristal", (0.6, 0.8, 1.0, 0.4))
mesh_object(coll, "cristal", [(-10, 110, 20), (10, 110, 20), (10, 110, 25), (-10, 110, 25)], [(0, 1, 2, 3)],
            material("cristal", (1, 1, 1), glass), "transparent")
mesh_object(coll, "brillo", [(-5, 130, 30), (5, 130, 30), (5, 130, 35), (-5, 130, 35)], [(0, 1, 2, 3)],
            material("brillo", (1, 1, 0.6)), "additive")
mesh_object(coll, "bandera", [(40, 120, 20), (42, 120, 20), (42, 120, 21), (40, 120, 21)], [(0, 1, 2, 3)],
            material("bandera", (0.8, 0.1, 0.1)), "flags")
mesh_object(coll, "bandera2", [(-42, 120, 20), (-40, 120, 20), (-40, 120, 22), (-42, 120, 22)], [(0, 1, 2, 3)],
            material("bandera2", (1, 1, 1), image("rayas", (0.9, 0.9, 0.9, 1.0))), "flags")
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
    hit = [nm for nm, f in by_name.items() if nm.startswith(f"DP_{role}_") and not nm.endswith("_up")]
    check(hit and all(by_name[nm] == flags for nm in hit), f"role {role}: pieces {hit} with flags 0x{flags:08X}")
up = [nm for nm in by_name if nm.endswith("_up")]
check(len(up) == 1 and by_name[up[0]] == scratch.ROLES["stand"][0] | scratch.HIDE_ABOVE,
      f"hide from above: {up} with bit 0x80000000")
stands = [nm for nm in by_name if nm.startswith("DP_stand_") and not nm.endswith("_up")]
check(len(stands) == 1, f"the stand objects (two boxes, Grada_Peñarol, the text) are one piece: {stands}")
check(scratch.ROLES["additive"][0] & 0xFF00 == 0, "additive glow has no piece type (type 0x08, lights, only shows at night)")
projectbin = importlib.import_module(pkg + ".projectbin")
archive = projectbin.Archive(project_bin)
col = stadium_read.read_stadium(archive.read("stadium_col.pack"), archive.read("stadium_coltbl.pack"))
check([(e.tmem_a, e.tmem_b) for e in st.preload] == [(e.tmem_a, e.tmem_b) for e in col.preload]
      and [st.textures[e.texture] for e in st.preload] == [col.textures[e.texture] for e in col.preload]
      and st.preload_order == col.preload_order and st.preload_tail == col.preload_tail,
      "the data stadium's 8 preloaded textures (scoreboard, video board, crowd) are copied")
for role, slot in (("crowd_upright", 4), ("crowd_lying", 6)):
    m = next(st.models[p.model] for p in st.placements if st.models[p.model].name.startswith(f"DP_{role}_"))
    check(m.textures and all((t, pal) == (st.preload[slot].texture, st.preload[slot].palette) for t, pal, _ in m.textures),
          f"{role} draws the preloaded crowd texture of slot {slot} (the game animates its people there)")
flag_pieces = [p for p in st.placements if st.models[p.model].name.startswith("DP_flags_")]
check(len(flag_pieces) == 2 and all([len(m.positions) for m in st.models[p.model].meshes] == [10] for p in flag_pieces),
      "each flag object is its own piece with the game's 10-vertex flag")
check(sorted(p.translation for p in flag_pieces) == [scratch.f32(-410.0, 210.0, -1200.0), scratch.f32(410.0, 205.0, -1200.0)],
      f"flag poles at the objects' centres: {[p.translation for p in flag_pieces]}")


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
if back != st:
    import dataclasses
    for f in dataclasses.fields(st):
        a, b = getattr(st, f.name), getattr(back, f.name)
        if a != b:
            print("DIFF", f.name, (len(a), len(b)) if isinstance(a, list) else (a, b))
import stadium_re                                                    # noqa: E402
problems = []
models, _ = stadium_re.load("scratch", gfx, tbl, problems)
for m in models:
    stadium_re.check_model(gfx, m, problems)
check(not problems, f"stadium_re finds no problems {problems[:3]}")

st2, _, _ = scratch.build_stadium(coll, game_bases=False)
check(not any("BCL" in m.name for m in st2.models), "without game bases there are no BCL pieces")
coll["pp_stadium"] = "ari"                                   # its crowd slots hold 8 x 8 textures
st_ari, _, _ = scratch.build_stadium(coll, game_bases=False)
coll["pp_stadium"] = "col"
m = next(m for m in st_ari.models if m.name.startswith("DP_crowd_upright_"))
sizes = {(st_ari.textures[m.textures[tm.slot][0]].width, tm.width or None, tm.height or None) for tm in m.texmaps}
check(sizes == {(8, 128, 64)}, f"crowd texmaps declare 128 x 64 over Arizona's 8 x 8 crowd slot: {sizes}")

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

# ---- Final review fixes ----------------------------------------------------------------------------
curved = scratch_collection("Valla curva")
mesh_object(curved, "campo3", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
cu = bpy.data.curves.new("valla_curva", "CURVE")
cu.dimensions, cu.extrude = "2D", 1.5                        # a wall 3 m high around the field
spline = cu.splines.new("POLY")
spline.points.add(n - 1)
for k, (x, y) in enumerate(ring):
    spline.points[k].co = (x, y, 1.5, 1.0)
spline.use_cyclic_u = True
fence_curve = bpy.data.objects.new("valla_curva", cu)
curved.objects.link(fence_curve)
fence_curve.powerpros_role = "fence"
try:
    st_c, _, _ = scratch.build_stadium(curved, game_bases=False)
    check(len(st_c.collision_triangles) > 0, f"a fence made with a curve counts for the collision: {len(st_c.collision_triangles)} triangles")
except scratch.ScratchFailure as e:
    check(False, f"a fence made with a curve counts for the collision: {e}")
check(not [o for o in bpy.data.objects if o.name.startswith("pp_tmp_")], "no temporary objects left after the collision")

menu_render = importlib.import_module(pkg + ".menu_render")
import numpy as np                                                   # noqa: E402
shot = menu_render.photo(coll).astype(np.float32)
sky = np.array(menu_render.SKY) * 255
not_sky = float((np.abs(shot[..., :3] - sky).max(-1) > 24).mean())
check(not_sky > 0.2, f"the menu photo of a from-scratch stadium shows it: {not_sky:.0%} of the pixels are not sky")
check(not [o for o in bpy.data.objects if o.name.startswith("pp_tmp_")], "no temporary objects left after the photo")

flat = scratch_collection("Bandera plana")
mesh_object(flat, "campo4", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
mesh_object(flat, "valla4", fence_v, [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)], grey, "fence")
mesh_object(flat, "bandera_plana", [(0, 120, 20), (3, 120, 20), (3, 122, 20), (0, 122, 20)], [(0, 1, 2, 3)],
            material("bandera3", (0.8, 0.1, 0.1)), "flags")
mesh_object(flat, "bandera_de_pie", [(5, 120, 20), (8, 120, 20), (8, 120, 22), (5, 120, 22)], [(0, 1, 2, 3)],
            material("bandera4", (0.8, 0.1, 0.1)), "flags")
try:
    scratch.build_stadium(flat, game_bases=False)
    check(False, "a flat flag is refused naming it")
except scratch.ScratchFailure as e:
    check("bandera_plana" in str(e), f"a flat flag is refused naming it: {e}")
check(not [o for o in bpy.data.objects if o.name.startswith("pp_tmp_")], "no temporary objects left after a refused flag")

import bmesh                                                         # noqa: E402
raised = mesh_object(scratch_collection("Editado"), "subido", [(0, 0, 0), (1, 0, 0), (1, 1, 0)], [(0, 1, 2)], grey)
bpy.context.view_layer.update()
bm = bmesh.new()
bm.from_mesh(raised.data)
for v in bm.verts:
    v.co.z += 10.0
bm.to_mesh(raised.data)
bm.free()
check(scratch.role_of(raised) == "stand", "«auto» sees a mesh edited by a script since the last update (raised: stand)")

layers = scratch_collection("Capas del suelo")
mesh_object(layers, "tierra", [(-15, 5, 0), (15, 5, 0), (15, 35, 0), (-15, 35, 0)], [(0, 1, 2, 3)],
            material("tierra", (0.6, 0.4, 0.2)))                      # on top, made first
mesh_object(layers, "cesped_base", [(-60, -20, -0.04), (60, -20, -0.04), (60, 130, -0.04), (-60, 130, -0.04)],
            [(0, 1, 2, 3)], grass)
mesh_object(layers, "valla6", fence_v, [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)], grey, "fence")
st_l, _, _ = scratch.build_stadium(layers, game_bases=False)
g = next(m for m in st_l.models if m.name.startswith("DP_ground_"))
heights = [round(sum(me.positions[c[1]][1] for c in cs[i:i + 3]) / 3, 3)
           for me in g.meshes for op, cs in me.draws for i in range(0, len(cs), 3)]
check(len(set(heights)) == 2 and heights == sorted(heights),
      f"ground layers drawn from the bottom up (the ground layer has no depth test): {heights}")

# ---- Task 3: operators, export, View in game ----------------------------------------------------
exporter = importlib.import_module(pkg + ".exporter")
preview = importlib.import_module(pkg + ".preview")
before = set(bpy.data.collections)
check(bpy.ops.object.powerpros_new_scratch(filepath=project_bin, stadium="col") == {"FINISHED"}, "New stadium from scratch")
made = [c for c in bpy.data.collections if c not in before]
check(len(made) == 1 and bpy.context.view_layer.active_layer_collection.collection == made[0],
      "the new from-scratch collection is the active one: new objects go into it")
bases = [o for o in made[0].all_objects if o.get("pp_kind") == "game_base"]
check(len(bases) == 3 and all("BCL" in o.name and all(o.lock_location) for o in bases),
      f"the data stadium's bases, plate and mound are shown, locked: {[o.name for o in bases]}")
mesh_object(made[0], "campo5", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
mesh_object(made[0], "valla5", fence_v, [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)], grey, "fence")
mound = next(o for o in bases if "maund" in o.name and "plate" not in o.name)
mound_name = mound.name
bpy.data.objects.remove(mound)
st_b, _, _ = scratch.build_stadium(made[0])
placed_b = [st_b.models[p.model].name for p in st_b.placements]
check(mound_name not in placed_b and sum("BCL" in nm for nm in placed_b) == 2 and not any(nm.startswith("DP_ground_") and "BCL" in nm for nm in placed_b),
      f"a deleted game piece is not exported, the others are, and they are not taken as model geometry: {placed_b}")
check(len(made) == 1 and made[0].get("pp_scratch") and made[0].get("pp_stadium") == "col"
      and Path(made[0].get("pp_project_bin", "")).is_file(), f"the new collection is a from-scratch one ({[dict(c) for c in made]})")

mods = Path(temp_dir) / "mods"
bpy.context.view_layer.objects.active = bpy.data.objects["campo"]
result = bpy.ops.export_scene.powerpros_stadium(mod_name="Desde cero", mods_folder=str(mods), mode="new",
                                                new_code="desdecero", new_name="Desde cero")
check(result == {"FINISHED"}, "export operator finishes")
project = mods / "Desde cero" / "project"
toml = (mods / "Desde cero" / "mod.toml").read_text(encoding="utf-8") if (mods / "Desde cero" / "mod.toml").is_file() else ""
check((project / "stadium_desdecero.pack").is_file() and (project / "stadium_desdecerotbl.pack").is_file(),
      "the two packs are written")
check('code = "desdecero"' in toml and 'base = "col"' in toml, "mod.toml declares the new stadium on col")
if (project / "stadium_desdecero.pack").is_file():
    back = stadium_read.read_stadium((project / "stadium_desdecero.pack").read_bytes(),
                                     (project / "stadium_desdecerotbl.pack").read_bytes())
    check(back.models[0].name == "DP_ground_0", "the exported packs read back as the scratch stadium")

game = Path(temp_dir) / "game"
(game / "mods").mkdir(parents=True, exist_ok=True)
(game / "WiiCompiled.exe").write_bytes(b"")
coll["pp_game_folder"] = str(game)
r = preview.launch(coll, (0.0, 150.0, 600.0, 0.0, -0.2, 45.0), dry_run=True)
pv = Path(r["mod_dir"]) / "project" / f"stadium_{preview.PREVIEW_CODE}.pack"
check(pv.is_file() and stadium_read.read_stadium(pv.read_bytes(), pv.with_name(pv.stem + "tbl.pack").read_bytes())
      .models[0].name == "DP_ground_0", "View in game exports the from-scratch stadium")

lost = scratch_collection("Sin juego")
del lost["pp_project_bin"]
mesh_object(lost, "suelo3", [(-60, -20, 0), (60, -20, 0), (60, 130, 0), (-60, 130, 0)], [(0, 1, 2, 3)], grass)
try:
    scratch.export_scratch(lost, str(mods), "x", "sinjuego", "x")
    check(False, "a collection without project.bin is refused")
except exporter.ExportFailure as e:
    check("project.bin" in str(e), f"a collection without project.bin is refused: {e}")

real = scratch.build_stadium


def broken(c, *a, **k):
    st_, hd_, w_ = real(c, *a, **k)
    st_.models[0].texmaps = st_.models[0].texmaps * 31 or [scratch.TexMap(0, 1, 1)] * 31
    return st_, hd_, w_


scratch.build_stadium = broken
try:
    scratch.export_scratch(coll, str(mods), "Roto", "roto", "Roto")
    check(False, "a description the writer refuses is reported")
except exporter.ExportFailure as e:
    check("DP_ground_0" in str(e), f"a description the writer refuses is reported naming the piece: {e}")
finally:
    scratch.build_stadium = real

print("RESULT: " + ("FAIL" if failures else "PASS"))
sys.exit(1 if failures else 0)
