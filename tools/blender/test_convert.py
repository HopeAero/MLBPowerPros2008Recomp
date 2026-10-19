# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless check of «Convertir objetos en piezas» and «Generar colisión» on a made-up stadium.

blender --background --factory-startup --python tools/blender/test_convert.py -- <addon.zip> <project.bin> <mods dir>
Builds a small stadium out of primitives (green field, dirt infield, a round wall 75 m from the
middle of the field, sloped stands), spreads it over Coors Field's pieces, generates its collision,
exports it as the new stadium «sintetico» and reads the packs back. Exits 1 on failure.
"""
import collections
import importlib
import math
import sys
from pathlib import Path

import bmesh
import bpy
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
convert = importlib.import_module(pkg + ".convert")


def twins(first_away, textured_first):
    """Two faces with the same corners on a wall 80 m out: one plain (material 0), one textured (1).
    Returns the faces drop_back_twins keeps, as (material, normal y)."""
    bm = bmesh.new()
    quad = [(-5.0, 80.0, 0.0), (5.0, 80.0, 0.0), (5.0, 80.0, 5.0), (-5.0, 80.0, 5.0)]
    for textured in ((True, False) if textured_first else (False, True)):
        away = first_away if textured == textured_first else False
        corners = quad[::-1] if away else quad          # quad faces -Y, towards home
        f = bm.faces.new([bm.verts.new(c) for c in corners])
        f.material_index = 1 if textured else 0
    bm.normal_update()
    convert.drop_back_twins(bm, textured={1})
    kept = [(f.material_index, round(f.normal.y, 3)) for f in bm.faces]
    bm.free()
    return kept


def test_prepare_and_image_texture():
    """convert.prepare: the faces of some objects in one bmesh, one material index per image, colours
    baked, twins dropped (a textured face over a plain one). exporter.image_texture: RGB5A3 + HD."""
    exporter = importlib.import_module(pkg + ".exporter")
    img = bpy.data.images.new("prep_tex", 8, 8)
    img.pixels.foreach_set([0.2, 0.6, 0.2, 1.0] * 64)
    plain = bpy.data.materials.new("prep_plain")
    plain.use_nodes = True
    textured = bpy.data.materials.new("prep_textured")
    textured.use_nodes = True
    textured.node_tree.nodes.new("ShaderNodeTexImage").image = img
    quad = [(-5.0, 80.0, 0.0), (5.0, 80.0, 0.0), (5.0, 80.0, 5.0), (-5.0, 80.0, 5.0)]
    objs = []
    for name, mat in (("prep_a", plain), ("prep_b", textured)):
        me = bpy.data.meshes.new(name)
        me.from_pydata(quad, [], [(0, 1, 2, 3)])
        me.materials.append(mat)
        o = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(o)
        objs.append(o)
    bm, images = convert.prepare(objs)
    faces = list(bm.faces)
    check(len(faces) == 1 and images[faces[0].material_index] is img,
          f"prepare: twins over two objects keep the textured face ({len(bm.faces)} faces, images {images})")
    check("Color" in bm.loops.layers.float_color and "UVMap" in bm.loops.layers.uv, "prepare: colour and UV layers")
    bm.free()
    tex, hd_name, hd_rgba = exporter.image_texture(img, True)
    check((tex.fmt, tex.width, tex.height, len(tex.pixels)) == (5, 8, 8, 128), "image_texture: 8x8 RGB5A3")
    check(hd_name and hd_name.endswith("_5.png") and hd_rgba is not None, f"image_texture: HD replacement {hd_name}")
    check(exporter.image_texture(img, False)[1] is None, "image_texture without HD")
    for o in objs:
        bpy.data.objects.remove(o)


test_prepare_and_image_texture()


# A texture drawn over a plain face (SketchUp exports both): the textured one stays, facing the field.
check(twins(first_away=False, textured_first=False) == [(1, -1.0)], "twins facing the field: the textured one stays")
check(twins(first_away=True, textured_first=True) == [(1, -1.0)], "a textured twin facing away is kept, turned to the field")
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium="col", collision=False)
stadium = next(c for c in bpy.data.collections if c.get("pp_stadium"))


def material(name, rgb, two_sided=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1.0)
    m.use_backface_culling = not two_sided
    return m


def mesh_object(name, verts, faces, mat):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


centre, radius, n = Vector((0.0, 45.0)), 75.0, 64
ring = [centre + Vector((radius * math.sin(2 * math.pi * k / n), radius * math.cos(2 * math.pi * k / n))) for k in range(n)]
field = mesh_object("campo", [(p.x, p.y, 0.0) for p in ring] + [(centre.x, centre.y, 0.0)],
                    [(n, k, (k + 1) % n) for k in range(n)][::1], material("cesped", (0.05, 0.35, 0.05)))
field.data.flip_normals()                           # the fan above was wound clockwise
infield = mesh_object("tierra", [(0, 0, 0.02), (19.4, 19.4, 0.02), (0, 38.8, 0.02), (-19.4, 19.4, 0.02)],
                      [(0, 1, 2, 3)], material("tierra", (0.45, 0.25, 0.1)))   # counter-clockwise: faces up
wall = mesh_object("valla", [(p.x, p.y, z) for p in ring for z in (0.0, 3.0)],
                   [(2 * k, 2 * ((k + 1) % n), 2 * ((k + 1) % n) + 1, 2 * k + 1) for k in range(n)],
                   material("valla", (0.1, 0.3, 0.6), two_sided=True))
outer = [centre + (p - centre) * (100.0 / radius) for p in ring]
stands = mesh_object("gradas", [(p.x, p.y, 3.0) for p in ring] + [(p.x, p.y, 25.0) for p in outer],
                     [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)],
                     material("gradas", (0.5, 0.5, 0.5), two_sided=True))
for o in bpy.context.selected_objects:
    o.select_set(False)
for o in (field, infield, wall, stands):
    o.select_set(True)
bpy.context.view_layer.objects.active = field
pieces_before = sum(1 for o in stadium.all_objects if o.get("pp_kind") == "model")
check(bpy.ops.object.powerpros_convert(replace=True, bake_light=True) == {"FINISHED"}, "convert finishes")
kept = [o.name for o in stadium.all_objects if o.get("pp_kind") == "model" and "BCL" in o.name]
pieces = [o for o in stadium.all_objects if o.get("pp_kind") == "model" and "BCL" not in o.name]
check(not any(n in bpy.data.objects for n in ("campo", "tierra", "valla", "gradas")), "source objects removed")
check(len(pieces) == 2, f"the model is two pieces, field and the rest: {[o.name for o in pieces]}")
check(len(kept) == 3, f"the game's bases, plate and mound stay: {kept}")
ground = next((o for o in pieces if all(abs(v.co.z) < 0.1 for v in o.data.vertices)), None)
check(ground is not None, "the field went to its own (ground) piece")
greens = [c.color for c in ground.data.color_attributes["Color"].data]
check(any(c[1] > c[0] * 1.5 for c in greens), "material colour baked into the vertex colours")
# Blender shows the pieces as the game draws them: vertex colour (times the texture), unlit.
check(all(o.data.color_attributes.active_color_name == "Color" for o in pieces), "vertex colours are the active ones")
for m in {m for o in pieces for m in o.data.materials}:
    types = {n.type for n in m.node_tree.nodes}
    check({"VERTEX_COLOR", "EMISSION"} <= types, f"{m.name} shows vertex colour, unlit")

bpy.context.view_layer.objects.active = pieces[0]
check(bpy.ops.object.powerpros_generate_collision() == {"FINISHED"}, "collision generated")
coll_obj = next(o for o in stadium.all_objects if o.get("pp_kind") == "collision")
result = bpy.ops.export_scene.powerpros_stadium(mod_name="Sintetico", mods_folder=mods_dir, mode="new",
                                                new_code="sintetico", new_name="Estadio sintético")
check(result == {"FINISHED"}, "export finishes")
out = Path(mods_dir) / "Sintetico" / "project"
gfx = (out / "stadium_sintetico.pack").read_bytes()
tbl = (out / "stadium_sinteticotbl.pack").read_bytes()
table = tbl_mod.parse_table(tbl)
surfaces = collections.Counter(t[3] for t in table.triangles)
print("surfaces", sorted(surfaces.items()))
check(len(table.triangles) == len(coll_obj.data.polygons), f"tbl has the {len(table.triangles)} collision triangles")
for sid, what in ((5, "grass"), (6, "dirt"), (8, "mound"), (9, "field wall"), (0, "foul invisible wall"),
                  (1, "outfield invisible wall"), (14, "foul poles"), (11, "stands")):
    check(surfaces[sid] > 0, f"surface {sid} ({what}) present")


def tri_normal(t):
    a, b, c = (table.vertices[k] for k in t[:3])
    u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


check(all(tri_normal(t)[1] > 0 for t in table.triangles if t[3] in (5, 6, 8)), "floor triangles face up")
walls = [t for t in table.triangles if t[3] == 9]
facing = sum(1 for t in walls
             if -sum(tri_normal(t)[i] * sum(table.vertices[k][i] for k in t[:3]) for i in (0, 2)) > 0)
check(facing > 0.9 * len(walls), f"field walls face home ({facing}/{len(walls)})")
wall_dist = [math.hypot(table.vertices[k][0] / 10, -table.vertices[k][2] / 10 - 45) for t in walls for k in t[:3]]
check(all(abs(d - 75) < 1.5 for d in wall_dist), f"walls on the 75 m circle ({min(wall_dist):.1f}..{max(wall_dist):.1f} m)")
for tree, lists, count in (("planes_a", "lists_a", len(table.placements)), ("planes_b", "lists_b", len(table.triangles))):
    nodes = tbl_mod.parse_tree(tbl, table.pointers[tree], table.pointers[lists])
    lo = table.pointers[lists]
    reached = set(tbl_mod.tree_members(tbl, nodes, lo, 0, 0) + tbl_mod.tree_members(tbl, nodes, lo, 0, 1))
    leaves = [s[2] for _, h, l in nodes for s in (h, l) if s[0] == 0]
    check(reached == set(range(count)) and max(leaves) <= 127, f"{tree}: reaches all {count}, leaves <= 127 ({max(leaves)})")
models = [pj7m.parse_model(gfx, off) for off in pj7m.find_models(gfx)]
check(sum(1 for m in models if m.meshes and "BCL" not in m.name) == len(pieces), f"{len(pieces)} pieces with geometry in the pack (plus the kept BCL ones)")
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
