# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless check of the add-on inside Blender: install the zip, import a stadium, count, render.

blender --background --factory-startup --python tools/blender/test_import.py -- <addon.zip> <project.bin> <code> <out dir>
Run with BLENDER_USER_RESOURCES pointing at a scratch folder so the player's Blender is untouched.
Also checks the language preference (Spanish/English texts).
Prints PASS/FAIL lines and exits 1 on failure; renders <code>-top.png and <code>-plate.png.
"""
import importlib
import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Vector

zip_path, project_bin, code, out_dir = sys.argv[sys.argv.index("--") + 1:][:4]
out_dir = Path(out_dir)
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
check(hasattr(bpy.ops.import_scene, "powerpros_stadium"), "extension installs and registers the import operator")
pkg = "bl_ext.user_default.powerpros_stadium"
pj7m = importlib.import_module(pkg + ".pj7m")
tbl_mod = importlib.import_module(pkg + ".stadium_tbl")
projectbin = importlib.import_module(pkg + ".projectbin")
i18n = importlib.import_module(pkg + ".i18n")

# Language preference: labels, enum items and reports follow it at once (classes registered again).
prefs = bpy.context.preferences.addons[pkg].preferences
op = bpy.ops.import_scene.powerpros_stadium
for lang, label, prop, org, report in (("EN", "Import stadium", "Stadium", "Original stadium 1", "project.bin not found"),
                                       ("ES", "Importar estadio", "Estadio", "Estadio original 1", "No encuentro project.bin")):
    prefs.language = lang
    rna = op.get_rna_type()
    items = {i.identifier: i.name for i in rna.properties["stadium"].enum_items}
    check((rna.name, rna.properties["stadium"].name, items["org01"]) == (label, prop, org),
          f"{lang}: operator label {rna.name!r}, property {rna.properties['stadium'].name!r}, item {items['org01']!r}")
    try:  # in background mode an operator's ERROR report raises
        op(filepath=str(out_dir / "no-such-folder" / "project"), stadium="col")
        error = ""
    except RuntimeError as e:
        error = str(e)
    check(report in error, f"{lang}: error report {error.strip()!r}")
prefs.language = "AUTO"
auto = "Importar estadio" if i18n.blender_is_spanish() else "Import stadium"
check(op.get_rna_type().name == auto, f"AUTO follows Blender's interface language (locale {bpy.app.translations.locale!r})")
view = bpy.context.preferences.view
blender_language = view.language
for lang, label in (("es", "Importar estadio"), ("en_US", "Import stadium")):
    view.language = lang
    prefs.language, prefs.language = "EN", "AUTO"          # register again under the new locale
    check(op.get_rna_type().name == label, f"AUTO with Blender in {lang}: {op.get_rna_type().name!r}")
view.language = blender_language

for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
result = bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium=code, collision=True)
check(result == {"FINISHED"}, f"import of {code} finishes")

archive = projectbin.Archive(project_bin)
gfx = archive.read(f"stadium_{code}.pack")
table = tbl_mod.parse_table(archive.read(f"stadium_{code}tbl.pack"))
models = [pj7m.parse_model(gfx, p.model) for p in table.placements]
want_faces = sum(len(pj7m.triangles(mesh)) for mod in models for mesh in mod.meshes)
pieces = [o for o in bpy.data.objects if o.get("pp_kind") == "model"]
faces = sum(len(o.data.polygons) for o in pieces)
textures = {t.offset for mod in models for t in mod.textures}
images = [i for i in bpy.data.images if "pp_texture_offset" in i]
coll = [o for o in bpy.data.objects if o.get("pp_kind") == "collision"]
check(len(pieces) == len(table.placements), f"{len(pieces)} pieces == {len(table.placements)} placements")
check(abs(faces - want_faces) <= want_faces // 200, f"{faces} faces ~ {want_faces} triangles from the display lists")
used = {m.textures[m.texmaps[mat.texmaps[0]].texture].offset for m in models for mat in m.materials if mat.texmaps}
check(len(images) == len(used), f"{len(images)} images == {len(used)} textures in use ({len(textures)} listed)")
proper = len({(tuple(sorted((a, b, c))), s) for a, b, c, s in table.triangles if len({a, b, c}) == 3})  # no degenerate/repeat
got = sum(len(o.data.polygons) for o in coll)
check(len(coll) == len({t[3] for t in table.triangles}) and got == proper,
      f"collision: {len(coll)} kinds, {got} faces of {proper} ({len(table.triangles)} with degenerate)")
check(all(i.packed_file is not None for i in images), "images are packed into the .blend")

# Renders to compare with the game (EEVEE, unlit materials as imported).
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.eevee.taa_render_samples = 8
scene.render.resolution_x, scene.render.resolution_y = 1280, 720
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam)
scene.camera = cam
views = {
    "top": dict(loc=(0, 45, 250), rot=(0, 0, 0), ortho=170),
    "plate": dict(loc=(0, -6, 3), rot=(math.radians(94), 0, 0), ortho=None),
}
for name, v in views.items():
    cam.location = Vector(v["loc"])
    cam.rotation_euler = Euler(v["rot"])
    cam.data.type = "ORTHO" if v["ortho"] else "PERSP"
    if v["ortho"]:
        cam.data.ortho_scale = v["ortho"]
    cam.data.lens = 24
    cam.data.clip_end = 2000
    scene.render.filepath = str(out_dir / f"{code}-{name}.png")
    bpy.ops.render.render(write_still=True)
    check((out_dir / f"{code}-{name}.png").is_file(), f"render {name}")

print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
