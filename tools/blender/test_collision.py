# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless test of the collision in Blender (surfaces.py): one object per surface, named and coloured as
the stadium viewer draws it, shown or hidden by kind, made from any mesh, joined back on export.

blender --background --factory-startup --python tools/blender/test_collision.py -- <addon.zip> <project.bin> <temp dir>
Exits 1 on failure.
"""
import importlib
import math
import sys
from pathlib import Path

import bpy

zip_path, project_bin, temp_dir = sys.argv[sys.argv.index("--") + 1:][:3]
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))          # tools/: projectbin
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
surfaces = importlib.import_module(pkg + ".surfaces")
stadium_tbl = importlib.import_module(pkg + ".stadium_tbl")
import projectbin                                                    # noqa: E402
archive = projectbin.Archive(project_bin)
orig_tbl_bytes = archive.read("stadium_coltbl.pack")
orig = stadium_tbl.parse_table(orig_tbl_bytes)
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)

bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium="col", collision=True)
stadium = next(c for c in bpy.data.collections if c.get("pp_stadium"))
objs = surfaces.collision_objects(stadium)
in_table = sorted({t[3] for t in orig.triangles})
check(sorted(o["pp_surface"] for o in objs) == in_table, f"one collision object per surface: {[o.name for o in objs]}")
fences = next(o for o in objs if o["pp_surface"] == 9)
check(fences.name.startswith("Colisión · Vallas") or fences.name.startswith("Collision · Fences"),
      f"named by kind: {fences.name}")
mat = fences.data.materials[0]
check(len(fences.data.materials) == 1 and mat.name.startswith("Superficie 9 · ")
      and tuple(round(c, 3) for c in mat.diffuse_color[:3]) == (1.0, 0.941, 0.188),
      f"material named and coloured as in the viewer: {mat.name} {tuple(mat.diffuse_color)}")
proper = len({(tuple(sorted(t[:3])), t[3]) for t in orig.triangles if len(set(t[:3])) == 3})  # per kind, no repeats
check(sum(len(o.data.polygons) for o in objs) == proper, "all the table's triangles are there")

mods = str(Path(temp_dir) / "mods")
bpy.context.view_layer.objects.active = next(o for o in stadium.all_objects if o.get("pp_kind") == "model")
check(bpy.ops.export_scene.powerpros_stadium(mod_name="Sin cambios", mods_folder=mods, mode="replace", target="same")
      == {"FINISHED"}, "export without changes")
tbl_path = Path(mods) / "Sin cambios" / "project" / "stadium_coltbl.pack"
check(not tbl_path.exists() or tbl_path.read_bytes() == orig_tbl_bytes, "an untouched collision is not rewritten")

# Show/hide by kind: All / None.
check(bpy.ops.object.powerpros_collision_visibility(show=False) == {"FINISHED"}
      and all(o.hide_viewport for o in surfaces.collision_objects(stadium)), "None hides every kind")
check(bpy.ops.object.powerpros_collision_visibility(show=True) == {"FINISHED"}
      and not any(o.hide_viewport for o in surfaces.collision_objects(stadium)), "All shows every kind")

# A user's own collision: a 2 m box made a fence, the foul poles deleted.
bpy.ops.mesh.primitive_cube_add(size=2.0, location=(30.0, 60.0, 1.0))
box = bpy.context.active_object
for c in list(box.users_collection):
    c.objects.unlink(box)
stadium.objects.link(box)
bpy.context.view_layer.objects.active = box
for o in bpy.context.selected_objects:
    o.select_set(False)
box.select_set(True)
check(bpy.ops.object.powerpros_make_collision(surface="9") == {"FINISHED"}, "make collision from a box")
check(box.get("pp_kind") == "collision" and box["pp_surface"] == 9 and box.data.materials[0] == mat
      and any(c.name.endswith("Colisión") for c in box.users_collection), "the box is fence collision now")
poles = next(o for o in surfaces.collision_objects(stadium) if o["pp_surface"] == 14)
bpy.data.objects.remove(poles)
bpy.context.view_layer.objects.active = next(o for o in stadium.all_objects if o.get("pp_kind") == "model")
check(bpy.ops.export_scene.powerpros_stadium(mod_name="Propia", mods_folder=mods, mode="replace", target="same")
      == {"FINISHED"}, "export with the user's collision")
t = stadium_tbl.parse_table((Path(mods) / "Propia" / "project" / "stadium_coltbl.pack").read_bytes())
fence_tris = sum(1 for x in t.triangles if x[3] == 9)
check(fence_tris == sum(1 for x in orig.triangles if x[3] == 9 and len(set(x[:3])) == 3) + 12,
      f"the box adds its 12 fence triangles ({fence_tris})")
check(not any(x[3] == 14 for x in t.triangles), "deleted foul poles are gone from the table")
near = [v for v in t.vertices if abs(v[0] - 310.0) < 0.5 and abs(v[1] - 20.0) < 0.5 and abs(v[2] + 610.0) < 0.5]
check(bool(near), "the box is where it is in Blender (game frame: x 310, y 20, z -610 dm)")
check(len(t.vertices) < sum(len(o.data.vertices) for o in surfaces.collision_objects(stadium)),
      "shared points of the objects are welded")

# The generated collision is split the same way.
collision_gen = importlib.import_module(pkg + ".collision_gen")
made = collision_gen.generate(stadium)
check(made and all(o.get("pp_kind") == "collision" and len({p.material_index for p in o.data.polygons}) == 1 for o in made)
      and len({o["pp_surface"] for o in made}) == len(made), f"generated collision, one object per kind: {[o.name for o in made]}")


def wall_angles(sid):
    return [math.degrees(math.atan2(abs(w.x), w.y)) for o in made if o.get("pp_surface") == sid
            for w in (o.matrix_world @ v.co for v in o.data.vertices) if w.y > 5]


fair_a, foul_a = wall_angles(1), wall_angles(0)
check(fair_a and foul_a and max(fair_a) < 45.05 and min(foul_a) > 44.95,
      f"invisible walls meet at the foul lines (home run up to {max(fair_a or [0]):.2f}°, foul from {min(foul_a or [0]):.2f}°)")

# A curved wall made of short faces (0.3 m, like Marlins Park's) is the wall, not the one behind it;
# a lone thin pole in front of it is not.
from mathutils import Vector  # noqa: E402


def wall(r, n):
    pts = [Vector((r * math.sin(a), 45 + r * math.cos(a))) for a in (2 * math.pi * k / n for k in range(n))]
    out = []
    for a, b in zip(pts, pts[1:] + pts[:1]):
        out += [[a.to_3d(), b.to_3d(), b.to_3d() + Vector((0, 0, 3))], [a.to_3d(), b.to_3d() + Vector((0, 0, 3)), a.to_3d() + Vector((0, 0, 3))]]
    return out


pole = [[Vector((0.1, 80, 0)), Vector((0.3, 80, 0)), Vector((0.3, 80, 3))], [Vector((0.3, 80, 0)), Vector((0.1, 80, 0)), Vector((0.2, 80, 3))]]
outline = collision_gen.field_outline(wall(60, int(2 * math.pi * 60 / 0.3)) + wall(70, 64) + pole)
radii = [round((p.xy - Vector((0, 45))).length, 1) for p in outline]
check(outline and max(radii) < 60.5 and min(radii) > 59.5, f"curved wall of short faces followed (radii {min(radii)}-{max(radii)} m, wall at 60)")

print("RESULT: " + ("FAIL" if failures else "PASS"))
sys.exit(1 if failures else 0)
