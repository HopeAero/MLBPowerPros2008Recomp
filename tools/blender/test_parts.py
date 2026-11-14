# SPDX-License-Identifier: GPL-3.0-or-later
"""Game pieces (game_parts.py) in a from-scratch stadium: listing, roles, a whole stand section, vertex
colours kept, export. blender --background --factory-startup --python test_parts.py -- <zip> <project.bin> <out dir>"""
import importlib
import sys

import bpy

zip_path, project_bin, out_dir = sys.argv[sys.argv.index("--") + 1:][:3]
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
game_parts = importlib.import_module(pkg + ".game_parts")
scratch = importlib.import_module(pkg + ".scratch")
field_gen = importlib.import_module(pkg + ".field_gen")
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.object.powerpros_new_scratch(filepath=project_bin, stadium="col")
coll = next(c for c in bpy.data.collections if c.get("pp_scratch"))
pb = coll["pp_project_bin"]

found = game_parts.parts(pb, "nyy")
names = {n: (i, r, h) for i, n, r, h in found}
check(len(found) > 40, f"Yankee Stadium pieces listed ({len(found)})")
check(not any("BCL" in n or n.startswith("DP_ground") or "turf" in n for n in names), "no ground-layer pieces")
check(names["DP_stand_C"][1] == "stand" and names["DP_AudienceS_L1"][1] == "crowd_upright"
      and names["DP_AudienceF_L1"][1] == "crowd_lying" and names["DP_ALP_tree"][1] == "transparent"
      and names["DP_FLG_flag01"][1] == "flags", "roles from the placement flags")
check(names["DP_ALP_NOUPCAM_kage"][2], "NOUPCAM piece hidden from above")
check(not any("lumine" in n for n, _ in ((n, 0) for _, n, _, _ in game_parts.parts(pb, "col"))), "no night lights")

zone = game_parts.with_zone(pb, "nyy", names["DP_stand_R2"][0])
zone_names = sorted(n for i, n, _, _ in found if i in zone)
check({"DP_stand_R2", "DP_AudienceS_R2", "DP_AudienceF_R2"} <= set(zone_names), f"R2 section {zone_names}")

made = game_parts.insert(coll, "nyy", zone + [names["DP_stand_C"][0]])
check(len(made) == len(zone) + 1, f"pieces inserted ({len(made)})")
stand = next(o for o in made if o.name.endswith("DP_stand_C"))
check(stand["pp_part"] == "nyy:DP_stand_C" and stand.powerpros_role == "stand", "inserted piece keeps name and role")

field_gen.generate(coll, field_gen.FieldParams())
convert = importlib.import_module(pkg + ".convert")
with scratch.evaluated([stand]) as temps:
    check("pp_part" in temps[0], "the evaluated copy is still a game piece")
    cols = {tuple(round(c, 3) for c in col) for _, _, _, _, cs, _ in convert.face_copies(temps) for col in cs}
check(len(cols) > 4, f"its own vertex colours reach the export ({len(cols)} different)")

st, hd, warnings = scratch.build_stadium(coll)
check(not warnings and len(st.placements) >= 6, f"stadium builds ({len(st.placements)} pieces, warnings {warnings})")
check(all(len(m.textures) <= 64 for m in st.models), "no piece over 64 textures")
own = sum(1 for img in bpy.data.images if "pp_texture_offset" not in img and img.users)
check(len(hd) <= own, f"game textures get no HD copy ({len(hd)} HD, {own} own images)")
res = scratch.export_scratch(coll, out_dir, "Parts test", "prttest", "Parts test", home_line="")
check(res["pieces"] >= 6 and not res["over_budget"], f"exports ({res})")
bpy.context.view_layer.objects.active = stand
before = len(coll.objects)
r = bpy.ops.object.powerpros_add_game_part(stadium="col", part=str(dict((n, i) for i, n, _, _ in game_parts.parts(pb, "col"))["DP_stand_C"]), whole_zone=True)
added = [o for o in coll.objects][before:]
check(r == {"FINISHED"} and len(added) >= 2 and all(o.get("pp_part", "").startswith("col:") for o in added),
      f"panel button adds a section ({[o.name for o in added]})")
print("RESULT:", "FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
