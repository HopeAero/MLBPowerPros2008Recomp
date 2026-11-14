# SPDX-License-Identifier: GPL-3.0-or-later
"""«Importar parque OOTP» as a player uses it: new stadium from scratch, import the park's .zip or .obj,
export the mod. OOTP parks are not in the repository (no licence to share them): pass a downloaded one.
blender --background --factory-startup --python test_ootp.py -- <zip> <project.bin> <park .zip/.obj> <turn degrees> <out dir>"""
import importlib
import math
import sys

import bpy

zip_path, project_bin, park, turn, out_dir = sys.argv[sys.argv.index("--") + 1:][:5]
turn = float(turn)
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
scratch = importlib.import_module(pkg + ".scratch")
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.object.powerpros_new_scratch(filepath=project_bin, stadium="col")
coll = next(c for c in bpy.data.collections if c.get("pp_scratch"))
bpy.context.view_layer.objects.active = next(o for o in coll.all_objects)

r = bpy.ops.import_scene.powerpros_ootp_park(filepath=park)
check(r == {"FINISHED"}, f"the import runs ({r})")
objs = [o for o in coll.all_objects if o.type == "MESH" and not o.get("pp_kind")]
ground = [o for o in objs if o.powerpros_role == "ground"]
check(ground, f"a ground object ({[o.name for o in ground]})")
check(abs(coll.get("pp_ootp_turn", 999) - turn) < 3, f"turned {coll.get('pp_ootp_turn')}° (the game checked {turn}°)")
check(all(i.packed_file or i.source != "FILE" for i in bpy.data.images if i.users), "every image packed in the .blend")
coll_objs = [o for o in coll.all_objects if o.get("pp_kind") == "collision"]
tris = sum(len(o.data.polygons) for o in coll_objs)
check(0 < tris < 30000, f"collision generated within budget ({tris} faces)")
st, hd, warnings = scratch.build_stadium(coll)
check(not warnings, f"stadium builds ({len(st.placements)} pieces, warnings {warnings})")
res = scratch.export_scratch(coll, out_dir, "OOTP test", "ootptst", "OOTP test", home_line="")
check(res["pieces"] >= 3 and not res["over_budget"], f"exports ({res})")
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
