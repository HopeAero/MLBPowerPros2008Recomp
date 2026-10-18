# SPDX-License-Identifier: GPL-3.0-or-later
"""Renders for the stadium select menu of a new stadium (menu_pack.py writes them into the pack).

Photo (512x256): the stadium from high behind home plate, with a faded overhead view on the left like
the game's own photos. Line (512x64): the «Home of ...» text in green with a dark outline, drawn with
Blender's built-in font (never a game asset); empty text gives a transparent line.
"""
import math
import os
import tempfile

import bpy
import numpy as np

SKY = (0.42, 0.62, 0.92)


def _render(scene, w, h, transparent):
    scene.render.resolution_x, scene.render.resolution_y = w, h
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = transparent
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    fd, path = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True, scene=scene.name)
    img = bpy.data.images.load(path)
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    os.remove(path)
    return (px.reshape(h, w, 4)[::-1] * 255 + 0.5).astype(np.uint8)        # top row first


def _scene(name, color_type):
    scene = bpy.data.scenes.new(name)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = color_type
    scene.view_settings.view_transform = "Standard"
    scene.world = bpy.data.worlds.new(name)
    scene.world.color = SKY
    cam = bpy.data.objects.new(name + "_cam", bpy.data.cameras.new(name + "_cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.clip_end = 5000
    return scene, cam


def _drop(scene):
    for o in list(scene.collection.objects):
        if o.name.startswith(scene.name):
            data = o.data
            bpy.data.objects.remove(o)
            if isinstance(data, bpy.types.Camera):
                bpy.data.cameras.remove(data)
            elif isinstance(data, bpy.types.Curve):
                bpy.data.curves.remove(data)
    world = scene.world
    bpy.data.scenes.remove(scene)
    if world:
        bpy.data.worlds.remove(world)


def photo(coll):
    # Alpha and additive pieces (turf patterns, light glows) render opaque white in Workbench: left out.
    pieces = [o for o in coll.all_objects if o.get("pp_kind") == "model" and o.type == "MESH"
              and not any(k in o.name for k in ("ALP", "ADD", "MIP", "lumine"))]
    scene, cam = _scene("pp_menu_photo", "TEXTURE")
    for o in pieces:
        scene.collection.objects.link(o)
        attrs = o.data.color_attributes
        if len(attrs):                  # converted meshes leave theirs inactive: the vertex pass came out black
            attrs.active_color = attrs.get("Color") or attrs[0]
    def shaded(w, h, transparent):
        # The game draws stadiums as texture x vertex colour: one pass of each, multiplied.
        scene.display.shading.color_type = "TEXTURE"
        tex = _render(scene, w, h, transparent).astype(np.float32)
        scene.display.shading.color_type = "VERTEX"
        vcol = _render(scene, w, h, transparent).astype(np.float32)
        out = tex.copy()
        out[..., :3] = tex[..., :3] * vcol[..., :3] / 255.0
        return out

    try:
        cam.data.lens = 20
        cam.location = (0.0, -38.0, 24.0)
        cam.rotation_euler = (math.radians(74), 0.0, 0.0)
        main = shaded(512, 256, False)
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = 250.0
        cam.location = (0.0, 50.0, 500.0)
        cam.rotation_euler = (0.0, 0.0, math.radians(45))
        top = shaded(256, 256, True)
    finally:
        _drop(scene)
    # The overhead view on the left, fading out towards the middle like the game's photos.
    fade = np.clip(1.0 - np.arange(256) / 230.0, 0.0, 1.0)[None, :, None] * 0.7
    a = top[..., 3:4] / 255.0 * fade
    main[:, :256, :3] = main[:, :256, :3] * (1 - a) + top[..., :3] * a
    main[..., 3] = 255
    return np.clip(main, 0, 255).astype(np.uint8)


def line(text):
    if not text.strip():
        return np.zeros((64, 512, 4), np.uint8)
    scene, cam = _scene("pp_menu_line", "OBJECT")
    try:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = 16.0                # 512 px wide -> 1 unit = 32 px
        cam.location = (8.0, -1.0, 10.0)
        cam.rotation_euler = (0.0, 0.0, 0.0)
        for name, offset, colour, z in (("pp_menu_line_back", 0.06, (0.02, 0.18, 0.02, 1), 0.0),
                                        ("pp_menu_line_front", 0.0, (0.55, 0.95, 0.25, 1), 0.1)):
            curve = bpy.data.curves.new(name, "FONT")
            curve.body = text
            curve.size = 0.9
            curve.shear = 0.2
            curve.offset = offset
            obj = bpy.data.objects.new(name, curve)
            obj.color = colour
            obj.location = (0.9, -1.35, z)
            scene.collection.objects.link(obj)
        img = _render(scene, 512, 64, True)
    finally:
        _drop(scene)
    return img
