# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless check of «View in game» (preview.py): camera pose, the preview mod and how the game is launched.

blender --background --factory-startup --python tools/blender/test_preview.py -- <addon.zip> <project.bin> <temp dir>
Nothing is launched: launch() runs with dry_run. Exits 1 on failure.
"""
import importlib
import math
import shutil
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

zip_path, project_bin, temp_dir = sys.argv[sys.argv.index("--") + 1:][:3]
failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


def look_at(eye, target):
    """Blender view matrix (world to view) of a viewer at eye looking at target, Z up."""
    forward = (Vector(target) - Vector(eye)).normalized()
    rot = forward.to_track_quat("-Z", "Y").to_matrix().to_4x4()
    return (Matrix.Translation(eye) @ rot).inverted()


bpy.ops.extensions.package_install_files(filepath=zip_path, repo="user_default", enable_on_install=True)
pkg = "bl_ext.user_default.powerpros_stadium"
preview = importlib.import_module(pkg + ".preview")
exporter = importlib.import_module(pkg + ".exporter")
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj)
bpy.ops.import_scene.powerpros_stadium(filepath=project_bin, stadium="col", collision=False)
coll = next(c for c in bpy.data.collections if "pp_stadium" in c)

# 1. Camera: 90 m behind the plate, 30 m up, looking at it.
x, y, z, yaw, pitch, fov = preview.camera_pose(look_at((0.0, -90.0, 30.0), (0.0, 0.0, 0.0)), 50.0)
check(abs(x) < 0.5 and abs(y - 300.0) < 0.5 and abs(z - 900.0) < 0.5, f"eye in game decimetres ({x:.1f}, {y:.1f}, {z:.1f})")
check(abs(yaw) < 1e-3, f"yaw 0 looking towards second base ({yaw:.4f})")
check(abs(pitch + math.atan2(30.0, 90.0)) < 1e-3, f"pitch looks down ({pitch:.4f})")
check(10.0 < fov < 90.0, f"vertical field of view in degrees ({fov:.1f})")
_, _, _, yaw1b, _, _ = preview.camera_pose(look_at((0.0, 0.0, 2.0), (10.0, 0.0, 2.0)), 50.0)
check(abs(yaw1b - math.pi / 2) < 1e-3, f"looking towards first base is +pi/2 ({yaw1b:.4f})")

# 2. Dry run: the preview mod is written to the temp folder and the game would start from its folder.
game = Path(temp_dir) / "game"
shutil.rmtree(game, ignore_errors=True)
(game / "mods").mkdir(parents=True)
(game / "WiiCompiled.exe").write_bytes(b"")
coll["pp_game_folder"] = str(game)
pose = (0.0, 300.0, 900.0, 0.0, -0.3, 45.0)
result = preview.launch(coll, pose, dry_run=True)
mod_dir = preview.preview_folder() / preview.PREVIEW_MOD
check(Path(result["exe"]) == game / "WiiCompiled.exe", f"launches {result['exe']}")
check((mod_dir / "project" / f"stadium_{preview.PREVIEW_CODE}.pack").is_file(), "preview packs written")
check(f'code = "{preview.PREVIEW_CODE}"' in (mod_dir / "mod.toml").read_text(encoding="utf-8"), "mod.toml declares pp_preview")
env = result["env"]
check(Path(env.get("WIICOMPILED_STADIUM_VIEWER", "")) == mod_dir, "viewer points at the preview mod")
check(env.get("WIICOMPILED_STADIUM_VIEWER_CODE") == preview.PREVIEW_CODE, "viewer code")
cam = [float(v) for v in env.get("WIICOMPILED_STADIUM_VIEWER_CAMERA", "").split(",")]
check(len(cam) == 6 and all(abs(a - b) < 1e-3 for a, b in zip(cam, pose)), f"camera variable {cam}")
check(list((game / "mods").iterdir()) == [], "the game's mods folder is untouched")

# 3. No game executable: the error names the folder.
(game / "WiiCompiled.exe").unlink()
try:
    preview.launch(coll, pose, dry_run=True)
    check(False, "missing WiiCompiled.exe is reported")
except exporter.ExportFailure as e:
    check(str(game) in str(e), f"missing WiiCompiled.exe is reported: {e}")
(game / "WiiCompiled.exe").write_bytes(b"")

# The button: with no 3D view it uses the view from behind the plate; an empty WiiCompiled.exe cannot start,
# which it reports instead of failing.
check(bpy.ops.view3d.powerpros_preview_in_game.poll(), "button available with an imported stadium")
try:      # a reported error raises in background mode
    reported = bpy.ops.view3d.powerpros_preview_in_game() == {"CANCELLED"}
except RuntimeError as e:
    reported = "193" in str(e)
check(reported, "button reports a game that cannot start")

# 4. A piece the game cannot take (31 textures): the exporter's error, nothing launched.
ground = next(o for o in coll.all_objects if o.name == "DP_ground")
me = ground.data
for k in range(31):
    img = bpy.data.images.new(f"extra{k}", 8, 8)
    img.pixels.foreach_set([k / 31.0, 0.5, 0.2, 1.0] * 64)
    mat = bpy.data.materials.new(f"extra{k}")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = img
    me.materials.append(mat)
    me.polygons[k].material_index = len(me.materials) - 1
try:
    preview.launch(coll, pose, dry_run=True)
    check(False, "a piece with 31 textures stops the preview")
except exporter.ExportFailure as e:
    check("30" in str(e), f"a piece with 31 textures stops the preview: {e}")

print("RESULT: " + ("FAIL" if failures else "PASS"))
sys.exit(1 if failures else 0)
