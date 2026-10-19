# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless round trip of the add-on: import Coors Field, edit it, export a mod, read the packs back.

blender --background --factory-startup --python tools/blender/test_export.py -- <addon.zip> <project.bin> <mods dir>
Edits: DP_stand_C raised 15 m (transform), DP_score geometry raised 10 m (mesh), one flag deleted,
DP_ground's texture tinted blue (image). Writes <mods dir>/BlenderTest; exits 1 on failure.
"""
import importlib
import struct
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

zip_path, project_bin, mods_dir = sys.argv[sys.argv.index("--") + 1:][:3]
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
pj7m = importlib.import_module(pkg + ".pj7m")
tbl_mod = importlib.import_module(pkg + ".stadium_tbl")
projectbin = importlib.import_module(pkg + ".projectbin")
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium="col", collision=False)

objs = {o.name: o for o in bpy.data.objects if o.get("pp_kind") == "model"}
offsets = {n: o["pp_model_offset"] for n, o in objs.items()}
objs["DP_stand_C"].location.z += 15.0
for v in objs["DP_score"].data.vertices:
    v.co.z += 10.0
flag = next(n for n in objs if n.startswith("DP_FLG_"))
bpy.data.objects.remove(objs.pop(flag))
ground_img = next(n.image for m in objs["DP_ground"].data.materials for n in m.node_tree.nodes
                  if n.type == "TEX_IMAGE")
px = np.empty(len(ground_img.pixels), np.float32)
ground_img.pixels.foreach_get(px)
px = px.reshape(-1, 4)
px[:, 0] *= 0.2
px[:, 1] *= 0.4
ground_img.pixels.foreach_set(px.ravel())
# Its texture repeated 40 times plus an offset of 1000 repeats (SketchUp-like UVs): the texmap must
# say how far it repeats, or the game stretches the edge (texmap +0x1C/+0x1E).
for loop_uv in objs["DP_ground"].data.uv_layers.active.data:
    loop_uv.uv = (loop_uv.uv[0] * 40 + 1000, loop_uv.uv[1] * 40 - 1000)

bpy.context.view_layer.objects.active = objs["DP_ground"]
result = bpy.ops.export_scene.powerpros_stadium(mod_name="BlenderTest", mods_folder=mods_dir, mode="replace",
                                                target="same")
check(result == {"FINISHED"}, "export finishes")

out = Path(mods_dir) / "BlenderTest" / "project"
gfx = (out / "stadium_col.pack").read_bytes()
tbl = (out / "stadium_coltbl.pack").read_bytes()
archive = projectbin.Archive(project_bin)
orig = archive.read("stadium_col.pack")
orig_tbl = tbl_mod.parse_table(archive.read("stadium_coltbl.pack"))
table = tbl_mod.parse_table(tbl)
check(len(gfx) > len(orig) and len(gfx) % 32 == 0, f"graphics pack grew {len(orig)} -> {len(gfx)}")
place = {p.model: p for p in table.placements}
old_place = {p.model: p for p in orig_tbl.placements}
stand = place[offsets["DP_stand_C"]]
check(abs(stand.translation[1] - old_place[offsets["DP_stand_C"]].translation[1] - 150.0) < 0.5,
      f"DP_stand_C placement raised 150 dm (y {stand.translation[1]:.1f})")
score = pj7m.parse_model(gfx, offsets["DP_score"])
old_score = pj7m.parse_model(orig, offsets["DP_score"])
ys = [p[1] for m in score.meshes for p in pj7m.array(gfx, m, "pos")]
old_ys = [p[1] for m in old_score.meshes for p in pj7m.array(orig, m, "pos")]
check(abs((max(ys) - max(old_ys)) - 100.0) < 0.5, f"DP_score geometry raised 100 dm (max y {max(old_ys):.0f} -> {max(ys):.0f})")
check(score.header["triangles"] == sum(pj7m.raw_triangle_count(m) for m in score.meshes), "DP_score header counts match")
emptied = pj7m.parse_model(gfx, offsets[flag])
check(len(emptied.meshes) == 0, f"deleted {flag} has no meshes")
ground = pj7m.parse_model(gfx, offsets["DP_ground"])
new_tex = [t for t in ground.textures if t.offset >= len(orig)]
check(len(new_tex) >= 1 and new_tex[0].fmt == 5, "DP_ground uses a new RGB5A3 texture")
for mesh in ground.meshes:
    tm = ground.texmaps[ground.materials[mesh.material].texmaps[0]]
    reach = struct.unpack_from(">HH", tm.raw, 0x1C)
    uv = pj7m.array(gfx, mesh, "tex")
    need = [max(abs(t[k]) for t in uv) for k in (0, 1)]
    check(all(reach[k] >= need[k] - 1e-3 for k in (0, 1)), f"texmap repeats {reach} cover the UVs {[round(n, 2) for n in need]}")
    check(max(need) < 1000, f"the 1000-repeat offset is gone: whole repeats taken off each face ({max(need):.1f})")
# Its HD copy: named after the pack texels as the runtime hashes them, full size, tinted.
hd_mod = importlib.import_module(pkg + ".hd_textures")
t = new_tex[0]
texels = gfx[t.pixels:t.pixels + t.width * t.height * 2]
hd_file = Path(mods_dir) / "BlenderTest" / "textures" / "stadium_col" / hd_mod.replacement_name(texels, t.width, t.height)
hd_png = bpy.data.images.load(str(hd_file)) if hd_file.is_file() else None
check(hd_png is not None and tuple(hd_png.size) == tuple(ground_img.size), f"HD texture {hd_file.name} at the image's size")
if hd_png is not None:
    hp = np.empty(len(hd_png.pixels), np.float32)
    hd_png.pixels.foreach_get(hp)
    hp = hp.reshape(-1, 4)
    check(hp[:, 2].mean() > hp[:, 0].mean(), "HD texture keeps the blue tint")
untouched = pj7m.parse_model(gfx, offsets["DP_MIP_turf"])
check(all(m.dl < len(orig) for m in untouched.meshes), "untouched DP_MIP_turf still points at the original data")
check(gfx[:len(orig)].count(b"PJ7M") == orig.count(b"PJ7M"), "original model headers kept in place")
check((Path(mods_dir) / "BlenderTest" / "mod.toml").is_file(), "mod.toml written")
# The same edits as a new stadium: own file names and a [[stadium]] entry based on Coors Field.
result = bpy.ops.export_scene.powerpros_stadium(mod_name="BlenderNuevo", mods_folder=mods_dir, mode="new",
                                                new_code="coors3", new_name="Coors Field 3")
check(result == {"FINISHED"}, "new-stadium export finishes")
new_dir = Path(mods_dir) / "BlenderNuevo"
check((new_dir / "project" / "stadium_coors3.pack").read_bytes() == gfx, "new stadium graphics = the edited pack")
check((new_dir / "project" / "stadium_coors3tbl.pack").is_file(), "new stadium tbl written")
toml = (new_dir / "mod.toml").read_text(encoding="utf-8")
check('[[stadium]]' in toml and 'code = "coors3"' in toml and 'base = "col"' in toml, "mod.toml declares the stadium")
# Its stadium select menu: Coors Field's layout (env_stadium_menu27) with a rendered photo, no «Home of» line.
menu = new_dir / "project" / "env_stadium_menu_coors3.pack"
base_menu = archive.read("env_stadium_menu27.pack")
packwii = importlib.import_module(pkg + ".packwii")
if menu.is_file():
    m = menu.read_bytes()
    texs = sorted(packwii.find_textures(m), key=lambda t: -t["h"])
    photo, line = (packwii.image_of(m, t) for t in texs)
    check(len(m) == len(base_menu) and photo.shape == (256, 512, 4) and photo[..., :3].std() > 20,
          "menu pack written with a rendered photo")
    check(line[..., 3].max() == 0, "no «Home of» line when none is given")
else:
    check(False, "menu pack written")
# A deleted flag is shrunk away: the game animates flags from the placement, not the piece's meshes.
flag_place = place[offsets[flag]]
check(max(flag_place.scale) < 0.01, f"deleted {flag} placement shrunk (scale {flag_place.scale[0]:g})")
try:  # in background mode an operator's ERROR report raises
    refused = bpy.ops.export_scene.powerpros_stadium(mod_name="BlenderNuevo", mods_folder=mods_dir, mode="new",
                                                     new_code="nyy", new_name="x") == {"CANCELLED"}
except RuntimeError as error:
    refused = "ya es un estadio del juego" in str(error) or "is already a stadium of the game" in str(error)
check(refused, "a code the game already has is refused")

# Collision: import with it, raise the field walls (surface 9) 3 m, make one of their faces a foul
# pole (material «Superficie 14»), export, read the tbl back.
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium="col", collision=True)
coll_obj = next(o for o in bpy.data.objects if o.get("pp_kind") == "collision")
me = coll_obj.data
ids = np.empty(len(me.polygons), np.int32)
me.attributes["surface_id"].data.foreach_get("value", ids)
walls = [p for p in me.polygons if ids[p.index] == 9]
for v in {v for p in walls for v in p.vertices}:
    me.vertices[v].co.z += 3.0
pole = next(m for m in me.materials if m.name.startswith("Superficie 14"))
walls[0].material_index = list(me.materials).index(pole)
me.update()
bpy.context.view_layer.objects.active = coll_obj
result = bpy.ops.export_scene.powerpros_stadium(mod_name="BlenderColision", mods_folder=mods_dir, mode="replace",
                                                target="same")
check(result == {"FINISHED"}, "collision export finishes")
ctbl = (Path(mods_dir) / "BlenderColision" / "project" / "stadium_coltbl.pack").read_bytes()
orig_bytes = archive.read("stadium_coltbl.pack")
ctable = tbl_mod.parse_table(ctbl)
check(ctable.root == orig_tbl.root and ctbl[:ctable.root] == orig_bytes[:ctable.root],
      "tbl root and everything before it unchanged (the game's stadium table points at the root)")
check(len(ctable.triangles) == len(me.polygons), f"collision has {len(ctable.triangles)} triangles")
wall_y = [ctable.vertices[k][1] for t in ctable.triangles if t[3] == 9 for k in t[:3]]
old_wall_y = [orig_tbl.vertices[k][1] for t in orig_tbl.triangles if t[3] == 9 for k in t[:3]]
check(abs(max(wall_y) - max(old_wall_y) - 30.0) < 0.5, f"walls raised 30 dm (max y {max(old_wall_y):.0f} -> {max(wall_y):.0f})")
check(sum(t[3] == 14 for t in ctable.triangles) == sum(t[3] == 14 for t in orig_tbl.triangles) + 1,
      "the face given «Superficie 14» became a foul pole")


def normal_y(t, table):
    a, b, c = (table.vertices[k] for k in t[:3])
    u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
    return u[2] * v[0] - u[0] * v[2]


check(all(normal_y(t, ctable) > 0 for t in ctable.triangles if t[3] == 5), "grass triangles still face up (winding kept)")
for tree, lists, n in (("planes_a", "lists_a", len(ctable.placements)), ("planes_b", "lists_b", len(ctable.triangles))):
    nodes = tbl_mod.parse_tree(ctbl, ctable.pointers[tree], ctable.pointers[lists])
    lo = ctable.pointers[lists]
    reached = set(tbl_mod.tree_members(ctbl, nodes, lo, 0, 0) + tbl_mod.tree_members(ctbl, nodes, lo, 0, 1))
    check(lo > orig_tbl.root and reached == set(range(n)), f"new {tree} reaches all {n}")
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
