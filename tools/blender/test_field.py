# SPDX-License-Identifier: GPL-3.0-or-later
"""Field generator (field_gen.py) on a from-scratch stadium: measurements, ground layers, collision, export,
regeneration. blender --background --factory-startup --python test_field.py -- <zip> <project.bin> <out dir>"""
import importlib
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

zip_path, project_bin, out_dir = sys.argv[sys.argv.index("--") + 1:][:3]
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
field_gen = importlib.import_module(pkg + ".field_gen")
scratch = importlib.import_module(pkg + ".scratch")
collision_gen = importlib.import_module(pkg + ".collision_gen")
surfaces = importlib.import_module(pkg + ".surfaces")
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.object.powerpros_new_scratch(filepath=project_bin, stadium="col")
coll = next(c for c in bpy.data.collections if c.get("pp_scratch"))
FT = field_gen.FT

params = field_gen.FieldParams(lf=330, lcf=375, cf=400, rcf=380, rf=325, wall_l=37, color_l=(0.1, 0.4, 0.2))
made = field_gen.generate(coll, params)
names = sorted(o.name for o in made)
check(len(made) >= 10, f"field objects made ({len(made)}): {names}")
roles = {o.name: o.powerpros_role for o in made}
check(all(r == "ground" for n, r in roles.items() if "pista" in n or "césped" in n or "tierra" in n),
      "ground layers have the ground role")
walls = [o for o in made if "jardín" in o.name and o.type == "MESH"]
wall = walls[0]
check(len(walls) == 4 and all(w.powerpros_role == "fence" for w in walls), "outfield wall: four sectors, fences")
spans = sorted((round(min(b), 2), round(max(b), 2)) for b in
               ([math.degrees(math.atan2(v.co.x, v.co.y)) for v in w.data.vertices] for w in walls))
check(spans == [(-45.0, -22.5), (-22.5, 0.0), (0.0, 22.5), (22.5, 45.0)], f"the sectors meet, no gap between them: {spans}")
pts = [w.matrix_world @ v.co for w in walls for v in w.data.vertices]
tops = {w.name: round(max(v.co.z for v in w.data.vertices) / FT, 1) for w in walls}
check(sorted(tops.values()) == [8.0, 8.0, 8.0, 37.0] and tops[walls[0].name] == 37.0,
      f"left sector 37 ft (a Green Monster), the others 8: {tops}")
check(walls[0].data.materials[0].diffuse_color[1] > 0.35, "left sector in its own colour")
cf = max(p.y for p in pts if abs(p.x) < 0.5)
check(abs(cf - 400 * FT) < 0.3, f"centre field wall at 400 ft ({cf / FT:.1f} ft)")
for side, feet in ((-1, 330), (1, 325)):
    pole = max(pts, key=lambda p: side * p.x * (1 if p.y > 0 else 0))
    d = Vector((pole.x, pole.y)).length / FT
    check(abs(d - feet) < 1.0, f"{'left' if side < 0 else 'right'} field pole at {feet} ft ({d:.1f})")
heights = {o.name: max(v.co.z for v in o.data.vertices) for o in made if o.powerpros_role == "ground"}
check(heights["Campo · césped"] < heights["Campo · tierra del cuadro"] < heights["Campo · césped del cuadro"]
      < heights["Campo · círculos de tierra"] < heights["Campo · líneas"], f"ground layers stacked bottom-up {heights}")
chalk = next(o for o in made if o.name == "Campo · líneas")
reach = max(v.co.xy.length for v in chalk.data.vertices) / FT
check(325 < reach < 331, f"chalk: the foul lines reach the poles ({reach:.1f} ft)")
near_plate = [v.co for v in chalk.data.vertices if v.co.xy.length < 6 * FT]
check(near_plate and all(abs(v.x) > 1.0 * FT for v in near_plate), "chalk: batter's boxes beside the plate, none over it")
grass = next(o for o in made if o.name == "Campo · césped")
up = all(p.normal.z > 0.99 for p in grass.data.polygons)
check(up, "grass faces up")
check(field_gen.FieldParams.from_json(coll["pp_field"]) == params, "parameters saved in the collection")
signs = [o for o in made if o.type == "FONT"]
with scratch.evaluated(signs) as temps:
    behind = -math.inf
    for t in temps:
        for v in t.data.vertices:
            w = t.matrix_world @ v.co
            bearing = math.degrees(math.atan2(w.x, w.y))
            behind = max(behind, w.xy.length - field_gen.wall_radius(params, bearing))
check(len(signs) == 5 and behind < 0, f"distance signs wholly in front of the wall (worst {behind:+.3f} m)")

objs = collision_gen.generate(coll)
kinds = {int(o.get("pp_surface", -1)) for o in objs or []}
print("collision kinds", sorted(kinds))
check(objs is not None and {5, 6, 9}.issubset(kinds), "collision: grass, dirt and field walls")
fence_top = max((o.matrix_world @ v.co).z for o in objs if o["pp_surface"] == 9 for v in o.data.vertices
                if (o.matrix_world @ v.co).x < -20)
hr_low = min((o.matrix_world @ v.co).z for o in objs if o["pp_surface"] == 1 for v in o.data.vertices
             if math.degrees(math.atan2((o.matrix_world @ v.co).x, (o.matrix_world @ v.co).y)) < -25)
check(fence_top > 37 * FT - 0.1 and hr_low > 37 * FT - 0.1,
      f"collision: the 37 ft wall is a wall to its top ({fence_top:.1f} m), the home run wall above it ({hr_low:.1f} m)")
check(all(not o.powerpros_separate_layers for o in made), "generated objects keep their place (no layer push)")
convert = importlib.import_module(pkg + ".convert")
with scratch.evaluated([wall]) as temps:
    bm, _ = convert.prepare(temps, separate=True)
    moved = max(min((v.co - p).length for p in pts) for v in bm.verts)   # each vertex still on an original one
    bm.free()
check(moved < 0.01, f"the outfield wall comes out of the converter where it was (moved {moved:.3f} m)")
st, hd, warnings = scratch.build_stadium(coll)
check(not warnings and len(st.placements) >= 4, f"stadium builds ({len(st.placements)} pieces, warnings {warnings})")
solid_before = len(scratch.solid_objects(coll))
check(field_gen.toggle_players(coll), "reference players put")
refs = [o for o in coll.all_objects if o.get("pp_kind") == "reference"]
labels = {o.name for o in refs}
check(len(refs) >= 30 and any("SS" in n for n in labels) and any("Banca" in n or "dugout" in n for n in labels),
      f"players, umpire, coaches, on-deck batter and dugouts ({len(refs)} objects)")
check(len(scratch.solid_objects(coll)) == solid_before, "reference players take no part in the collision")
res = scratch.export_scratch(coll, out_dir, "Field test", "fldtest", "Field test", home_line="")
check(res["pieces"] >= 4 and not res["over_budget"], f"exports ({res})")
st2, _, _ = scratch.build_stadium(coll)
check(len(st2.placements) == len(st.placements), "reference players are not exported")


def spot_centre(spots):
    return Vector((sum(x for x, *_ in spots) / len(spots) / 10, -sum(z for _, _, z, *_ in spots) / len(spots) / 10))


bench1 = next(o for o in coll.all_objects if o.get("pp_bench") == 1)
check([len(x) for x in st2.unk_24] == [7, 7] and (spot_centre(st2.unk_24[0]) - bench1.location.xy).length < 0.5
      and spot_centre(st2.unk_24[1]).x < 0, f"the base's 7 dugout spots a side, first base's on its dugout {st2.unk_24[0][0][:3]}")
bench1.location.x += 5.0
bench1.location.z = -0.8
bpy.context.view_layer.update()
moved = scratch.bench_spots(coll, scratch._data_stadium(project_bin, "col"))
check((spot_centre(moved[0]) - bench1.location.xy).length < 0.5 and all(abs(y + 8.0) < 0.01 for _, y, *_ in moved[0]),
      "the dugout spots follow the moved dugout, its floor too")
check(not field_gen.toggle_players(coll) and not any(o.get("pp_kind") == "reference" for o in coll.all_objects),
      "reference players taken away")
default = scratch.bench_spots(coll, scratch._data_stadium(project_bin, "col"))
_, c1, _ = field_gen.benches(params)[0]
check((spot_centre(default[0]) - c1).length < 0.5 and all(y == 0.0 for _, y, *_ in default[0]),
      "without reference players, the dugout spots go where the generator puts the dugouts")
old = field_gen.FieldParams.from_json('{"wall": 12, "wall_color": [0.5, 0.1, 0.1]}')
check(old.heights() == (12, 12, 12, 12) and old.colors()[3] == (0.5, 0.1, 0.1), "fields saved before the sectors load")

turf = field_gen.FieldParams(surface="turf", infield="turf", pattern="circles", cf=410, signs=False)
made2 = field_gen.generate(coll, turf)
left = [o for o in coll.all_objects if o.get("pp_generated") == "field"]
check(len(left) == len(made2), f"regenerating replaces the field ({len(left)} objects)")
check(not any("tierra del cuadro" in o.name for o in made2), "turf infield: no dirt skin")
check(not any(o.type == "FONT" for o in made2), "no signs when turned off")
metro = field_gen.game_fence_style(project_bin, "min")
check(metro is not None and metro["image"] is not None and 1.0 < metro["height"] < 6.0,
      f"the Metrodome's fence look: its main texture and band ({metro and metro['height']:.2f} m)")
made3 = field_gen.generate(coll, field_gen.FieldParams(style_l="min", wall_l=16))
left = next(o for o in made3 if "jardín" in o.name)
img = next((n.image for n in left.data.materials[0].node_tree.nodes if n.type == "TEX_IMAGE"), None)
vs = [d.uv.y for d in left.data.uv_layers["UVMap"].data]
us = [d.uv.x for d in left.data.uv_layers["UVMap"].data]
band = abs(metro["v_per_m"] * metro["height"])
length = sum((left.data.vertices[e.vertices[0]].co - left.data.vertices[e.vertices[1]].co).length
             for e in left.data.edges if abs(left.data.vertices[e.vertices[0]].co.z - left.data.vertices[e.vertices[1]].co.z) < 1e-6) / 2
aspect_src = abs(metro["u_per_m"] / metro["v_per_m"])
aspect_new = (max(us) - min(us)) / length / ((max(vs) - min(vs)) / (16 * FT))
check(img is metro["image"] and abs((max(vs) - min(vs)) - band) < 0.01 and abs(aspect_new / aspect_src - 1) < 0.02,
      f"left sector in the Metrodome's look: the band whole over the 16 ft wall ({max(vs) - min(vs):.3f} vs {band:.3f}), same proportions ({aspect_new:.3f} vs {aspect_src:.3f}), image {img and img.name} vs {metro['image'].name}")
sizes = field_gen.game_distances(project_bin, "min")
check(all(sizes) and 320 < sizes[0] < 380 and 390 < sizes[2] < 430, f"the Metrodome's wall distances from its collision {sizes}")
made4 = field_gen.generate(coll, field_gen.FieldParams(**dict(zip(("lf", "lcf", "cf", "rcf", "rf"), sizes)), ground="min"))
ground = [o for o in coll.all_objects if o.get("pp_kind") == "game_ground"]
check(any("DP_MIP_turf" in o.name for o in ground) and not any("BCL" in o.name for o in ground)
      and not any("tierra" in o.name or "pista" in o.name or "líneas" in o.name for o in made4),
      f"the Metrodome's ground pieces in, no generated dirt, track or chalk ({len(ground)} pieces)")
st4, _, warn4 = scratch.build_stadium(coll)
order = [st4.models[pl.model].name for pl in st4.placements]
check(not warn4 and order[0].startswith("DP_ground_") and order[1] == "DP_MIP_turf" and "DP_ALP_MIP_line" in order
      and order.index("DP_ALP_MIP_line") < order.index("DP_BCL_base"),
      f"exported as they are, over our grass and under the bases: {order[:10]}")
made5 = field_gen.generate(coll, field_gen.FieldParams())
check(not any(o.get("pp_kind") == "game_ground" for o in coll.all_objects), "back to the generated ground: the game's pieces go")

# The dialog: every saved value must be one of its choices (Blender enums have no "" item), and it runs.
ui = importlib.import_module(pkg + ".blender_ui")
rna = bpy.ops.object.powerpros_generate_field.get_rna_type().properties
saved = field_gen.FieldParams(style_r="min", ground="")
bad = [k for k in ui.PP_OT_generate_field.FIELDS if rna[k].type == "ENUM"
       and (getattr(saved, k) or ui.NONE) not in {i.identifier for i in rna[k].enum_items}]
check(not bad, f"the dialog takes every saved value (bad: {bad})")
r1 = bpy.ops.object.powerpros_generate_field("EXEC_DEFAULT")
r2 = bpy.ops.object.powerpros_generate_field("EXEC_DEFAULT", style_l="min", ground="min", ground_sizes=True)
after = field_gen.FieldParams.from_json(coll["pp_field"])
check(r1 == r2 == {"FINISHED"} and after.style_l == "min" and after.style_r == "" and after.ground == "min" and after.cf > 410,
      f"the dialog generates: colour and generated ground as '', a game look, that stadium's measurements ({after.cf} ft)")
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
