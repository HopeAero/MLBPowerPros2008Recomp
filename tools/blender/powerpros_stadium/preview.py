# SPDX-License-Identifier: GPL-3.0-or-later
"""«View in game»: export the stadium as a throwaway mod and open the game's stadium viewer on it.

The mod goes to a temp folder (never the game's mods folder) as the new stadium pp_preview; the game
reads it through WIICOMPILED_STADIUM_VIEWER* and starts straight in that stadium, with a free camera
placed where the Blender view is.
"""
import math
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import bpy
from mathutils import Vector

from . import exporter, importer
from .i18n import T

PREVIEW_CODE = "pp_preview"
PREVIEW_MOD = "Vista previa"

_process = None     # the viewer launched from this Blender session


def preview_folder():
    return Path(tempfile.gettempdir()) / "powerpros_preview"


def camera_pose(view_matrix, lens_mm, sensor_mm=72.0, aspect=16 / 9):
    """(x, y, z, yaw, pitch, fovY) in the game frame (decimetres, Y up, second base towards -Z; yaw 0
    towards second base and positive towards first, radians; fovY in degrees) from a Blender 3D view's
    view matrix (metres, Z up, second base towards +Y). The viewport's lens covers its longer side."""
    inv = view_matrix.inverted()
    eye = importer.FROM_BLENDER @ inv.translation
    d = (importer.FROM_BLENDER.to_3x3() @ (inv.to_3x3() @ Vector((0.0, 0.0, -1.0)))).normalized()
    half = math.atan(sensor_mm / 2 / lens_mm)
    fov_y = 2 * math.atan(math.tan(half) / aspect) if aspect > 1 else 2 * half
    return (eye.x, eye.y, eye.z, math.atan2(d.x, -d.z), math.asin(max(-1.0, min(1.0, d.y))), math.degrees(fov_y))


def environment(mod_dir, pose):
    return {"WIICOMPILED_STADIUM_VIEWER": str(mod_dir),
            "WIICOMPILED_STADIUM_VIEWER_CODE": PREVIEW_CODE,
            "WIICOMPILED_STADIUM_VIEWER_CAMERA": ",".join(f"{v:.4f}" for v in pose)}


def launch(coll, pose, *, dry_run=False):
    """Export coll as the preview mod and start the game's viewer on it (closing the one this session
    opened before). dry_run: export only, and return what would be launched."""
    global _process
    game = Path(coll.get("pp_game_folder") or "")
    exe = game / "WiiCompiled.exe"
    if not coll.get("pp_game_folder") or not exe.is_file():
        raise exporter.ExportFailure(T(f"No encuentro WiiCompiled.exe en {game}: importa el estadio desde la carpeta del juego",
                                       f"WiiCompiled.exe not found in {game}: import the stadium from the game folder"))
    shutil.rmtree(preview_folder(), ignore_errors=True)
    bpy.context.view_layer.update()     # matrix_world of what was just moved, when called from a script
    stats = exporter.export_stadium(coll, preview_folder(), PREVIEW_MOD, PREVIEW_CODE, PREVIEW_MOD,
                                    hd=True, menu=False, home_line="")
    mod_dir = preview_folder() / PREVIEW_MOD
    env = environment(mod_dir, pose)
    result = {"exe": str(exe), "env": env, "mod_dir": str(mod_dir), "stats": stats}
    if dry_run:
        return result
    if _process is not None and _process.poll() is None:
        _process.terminate()
        try:
            _process.wait(5)
        except subprocess.TimeoutExpired:
            _process.kill()
    _process = subprocess.Popen([str(exe)], cwd=str(game), env={**os.environ, **env})
    return result
