# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn any model (glTF, OBJ, SketchUp through a glTF converter...) into pieces of an imported stadium.

The game draws a stadium as a fixed list of pieces (PJ7M models) placed by the tbl, with a depth
buffer far less precise than Blender's: surfaces a few cm apart flicker (separate_layers). The
field goes to the ground piece and the rest of the model into one stand piece. The game draws
stadiums unlit (texture x vertex colour), so the material colour and a simple sun are baked into
the vertex colours. Each face is written once, turned to the field: coplanar front/back pairs
fight in the game.
The model must already be in the game's frame: metres, home plate at the origin, second base on +Y.
"""
import math

import bmesh
import bpy
from mathutils import Vector

from . import importer
from .i18n import T

# Pieces that receive the model: stands, fences and ground, never the special ones (alpha, additive,
# camera-dependent, crowd, lights, the video board the game may draw into, bases, z-test pieces).
TARGET = ("stand", "fence", "ground", "turf")
SKIP = ("ALP", "ADD", "EVC", "NOUPCAM", "FLG", "Audience", "lumine", "LIGHT", "BCL", "ANT", "ZTEST",
        "score", "aurora", "board")
SUN = Vector((0.35, -0.45, 0.82)).normalized()
MAX_TEXMAPS = 30        # most textures one stadium piece has in the game; 82 overran a game table and crashed
FIELD = Vector((0.0, 45.0))                          # a point of every field (home at the origin)


class ConvertFailure(Exception):
    pass


def srgb(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def material_info(mat):
    """(image or None, sRGB colour): the colour only matters for untextured materials."""
    if mat is None or mat.node_tree is None:
        return None, (tuple(srgb(v) for v in mat.diffuse_color[:3]) if mat else (1.0, 1.0, 1.0))
    img = next((n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if img is not None or bsdf is None:
        return img, (1.0, 1.0, 1.0)
    return None, tuple(srgb(v) for v in bsdf.inputs["Base Color"].default_value[:3])


def flags(obj):
    try:
        return int(obj.get("pp_placement_flags", "0"), 16)
    except ValueError:
        return 0


def camera_dependent(obj):
    """Placement flags 0x80000000 (pieces named NOUPCAM), 0x20000000 (EVC) and 0x40000000 (the upper
    decks behind home, «2F»): the game seems to hide them in some cameras. Converted geometry put
    there vanished in the pop-up camera behind the catcher."""
    return flags(obj) & 0xE0000000 != 0


def targets(coll):
    """(ground piece, other opaque pieces) of an imported stadium."""
    pieces = [o for o in coll.all_objects if o.get("pp_kind") == "model" and not any(s in o.name for s in SKIP)
              and any(t in o.name.lower() for t in TARGET) and not camera_dependent(o)]
    if not pieces:
        raise ConvertFailure(T("El estadio no tiene gradas, vallas ni suelo donde poner el modelo",
                               "The stadium has no stands, fences or ground to put the model on"))

    def footprint(o):
        pts = [o.matrix_world @ Vector(c) for c in o.bound_box]
        return (max(p.x for p in pts) - min(p.x for p in pts)) * (max(p.y for p in pts) - min(p.y for p in pts))
    grounds = [o for o in pieces if "ground" in o.name.lower() or "turf" in o.name.lower()]
    ground = max(grounds or pieces, key=footprint)
    others = [o for o in pieces if o is not ground]
    plain = [o for o in others if flags(o) == 0]          # stands without special flags, when there are
    return ground, plain or others


def box_centre(o):
    pts = [o.matrix_world @ Vector(c) for c in o.bound_box]
    return sum(pts, Vector()) / 8


def face_copies(sources, bake_light=True):
    """Each face of `sources` (mesh objects in the game's frame) as the game will get it: yields
    (image or None, flat ground face, corners, UVs, vertex colour). One copy per face, turned to the
    field: two coplanar faces (a two-sided face drawn twice, or the front and back faces SketchUp
    exporters write) fight in the game, and the one facing away can win (the stadium showed the green
    backs of its stands). The material colour and a soft sun go into the vertex colour."""
    info = {}
    for src in sources:
        me = src.data
        mw = src.matrix_world
        normal_mw = mw.to_3x3().inverted().transposed()
        uv = me.uv_layers.active
        for p in me.polygons:
            mat = src.material_slots[p.material_index].material if p.material_index < len(src.material_slots) else None
            if mat not in info:
                info[mat] = material_info(mat)
            img, colour = info[mat]
            vs = [mw @ me.vertices[i].co for i in p.vertices]
            c = sum(vs, Vector()) / len(vs)
            n = (normal_mw @ p.normal).normalized()
            flat = abs(n.z) > 0.95 and c.z < 1.0 and c.xy.length < 160
            uvs = [tuple(uv.data[li].uv) for li in p.loop_indices] if uv else [(0.0, 0.0)] * len(vs)
            double = mat is not None and not mat.use_backface_culling
            towards = (n.z > 0) if flat else (n.xy.dot(FIELD - c.xy) > 0)
            sides = [not towards] if double else ([False] if towards or not flat else [])
            for flip in sides:
                normal = -n if flip else n
                light = 0.55 + 0.45 * max(0.0, normal.dot(SUN)) if bake_light else 1.0
                col = (*(min(1.0, v * light) for v in colour), 1.0)
                order = list(range(len(vs)))[::-1] if flip else list(range(len(vs)))
                yield img, flat, [vs[i] for i in order], [uvs[i] for i in order], col


def _add_face(bm, corners, uvs, col, material_index):
    try:
        f = bm.faces.new([bm.verts.new(v) for v in corners])
    except ValueError:
        return
    f.material_index = material_index
    for loop, uv in zip(f.loops, uvs):
        loop[bm.loops.layers.uv["UVMap"]].uv = uv
        loop[bm.loops.layers.float_color["Color"]] = col


def prepare(sources, bake_light=True, separate=True):
    """The faces of `sources` in one bmesh, ready for the game (face_copies), with twins dropped (a
    textured face wins over a plain one with the same corners) and, with `separate`, stacked layers
    pulled apart. Returns (bmesh, images): face.material_index indexes images (None: untextured)."""
    bm = bmesh.new()
    bm.loops.layers.uv.new("UVMap")
    bm.loops.layers.float_color.new("Color")
    images = []
    for img, _, corners, uvs, col in face_copies(sources, bake_light):
        if img not in images:
            images.append(img)
        _add_face(bm, corners, uvs, col, images.index(img))
    drop_back_twins(bm, {i for i, img in enumerate(images) if img is not None})
    if separate:
        separate_layers(bm)
    return bm, images


def convert(coll, sources, replace=True, bake_light=True):
    """Spread the faces of `sources` (mesh objects in the game's frame) over the stadium's pieces.
    replace: the pieces lose their own geometry and the pieces left without any are deleted.
    Returns {piece name: faces}. The source objects are removed."""
    # The field to the ground piece, the rest to the stand pieces. A piece can't take more than
    # MAX_TEXMAPS textures (its original ones stay in the pack), so each texture goes to the first
    # stand piece, biggest first, that still has room.
    ground, others = targets(coll)
    stands = sorted(others or [ground], key=lambda o: -len(o.data.polygons))
    room = {o.name: MAX_TEXMAPS - len(o.data.materials) for o in [ground] + stands}
    pieces = {o.name: o for o in [ground] + others}
    buckets, slots, plain, tex_mats, home = {}, {}, None, {}, {}

    def piece_for(gm, flat):
        if flat:
            return ground.name
        if gm.name not in home:
            name = next((o.name for o in stands if room[o.name] > 0), None)
            if name is None:
                raise ConvertFailure(T("El modelo tiene demasiadas texturas distintas para las piezas del estadio: "
                                       "junta texturas o elige un estadio base con más gradas",
                                       "The model has too many different textures for the stadium's pieces: "
                                       "merge textures or pick a base stadium with more stands"))
            room[name] -= 1
            home[gm.name] = name
        return home[gm.name]

    def bucket(name):
        if name not in buckets:
            bm = bmesh.new()
            bm.loops.layers.uv.new("UVMap")
            bm.loops.layers.float_color.new("Color")
            buckets[name], slots[name] = bm, []
        return buckets[name]

    def game_material(img):
        nonlocal plain
        if img is None:
            if plain is None:
                plain = bpy.data.materials.new("pp_color")       # untextured: colour in the vertices
                importer.shade(plain)
            return plain
        if img.name not in tex_mats:
            m = bpy.data.materials.new("pp_" + img.name)
            importer.shade(m, img)
            tex_mats[img.name] = m
        return tex_mats[img.name]

    for img, flat, corners, uvs, col in face_copies(sources, bake_light):
        gm = game_material(img)
        name = piece_for(gm, flat)
        bm = bucket(name)
        if gm not in slots[name]:
            slots[name].append(gm)
        _add_face(bm, corners, uvs, col, slots[name].index(gm))
    if not buckets:
        raise ConvertFailure(T("Los objetos elegidos no tienen caras", "The selected objects have no faces"))

    result = {}
    if replace:                                 # the rest of the original stadium goes too
        # Pieces marked pp_keep stay as they are, and so do the game's own bases, plate and mound (BCL):
        # the players stand on them, and a model's own (Mario Stadium's plate was 1.2 m) rarely match.
        for obj in [o for o in coll.all_objects if o.get("pp_kind") == "model" and o.name not in buckets
                    and not o.get("pp_keep") and "BCL" not in o.name]:
            pieces.pop(obj.name, None)
            bpy.data.objects.remove(obj)
    for name, obj in pieces.items():
        bm = buckets.get(name)
        if bm is None:
            continue
        drop_back_twins(bm, {i for i, m in enumerate(slots[name]) if m is not plain})
        if name != ground.name:                 # the field keeps its layers on the ground
            separate_layers(bm)
        bm.transform(obj.matrix_world.inverted())
        if replace:
            me = bpy.data.meshes.new(name)
            bm.to_mesh(me)
            me.color_attributes.active_color_name = "Color"     # Solid view, Attribute colour: as in the game
            obj.data = me
            for m in slots[name]:
                me.materials.append(m)
        else:
            old = bmesh.new()
            old.from_mesh(obj.data)
            offset = len(obj.data.materials)
            for m in slots[name]:
                obj.data.materials.append(m)
            _append(old, bm, offset)
            old.to_mesh(obj.data)
            obj.data.color_attributes.active_color_name = "Color"
            old.free()
        bm.free()
        result[name] = len(obj.data.polygons)
    for src in sources:
        bpy.data.objects.remove(src)
    return result


def drop_back_twins(bm, textured=frozenset()):
    """Of faces with the same corners keep one: a textured one (material index in `textured`) over a
    plain one, as SketchUp exports a texture painted over a plain face, then the one facing the field.
    A kept face that faces away from the field is turned to it."""
    seen, kill, turn = {}, [], set()
    for f in bm.faces:
        key = tuple(sorted((round(v.co.x, 3), round(v.co.y, 3), round(v.co.z, 3)) for v in f.verts))
        c = f.calc_center_median()
        towards = f.normal.z > 0 if abs(f.normal.z) > 0.95 else f.normal.xy.dot(FIELD - c.xy) > 0
        rank = (f.material_index in textured, towards)
        other = seen.get(key)
        if other is None:
            seen[key] = (f, rank)
            continue
        if rank > other[1]:
            kill.append(other[0])
            seen[key] = (f, rank)
        else:
            kill.append(f)
        turn.add(key)
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    away = [seen[k][0] for k in turn if not seen[k][1][1]]
    if away:
        bmesh.ops.reverse_faces(bm, faces=away)
    return len(kill)


def separate_layers(bm, gap=0.25, reach=0.25):
    """Faces stacked a few cm in front of another one facing the same way (windows on a wall, a
    sign, seats on a slope) flicker in the game: at 150 m its depth buffer cannot tell 2 cm apart
    (z-fighting; Blender can). Move the front face out to `gap` from the one behind it."""
    import collections
    planes = collections.defaultdict(list)
    for f in bm.faces:
        if f.calc_area() < 1e-4:
            continue
        n = f.normal.copy()
        planes[(round(n.x * 10), round(n.y * 10), round(n.z * 10))].append(
            (n.dot(f.verts[0].co), f, n, f.calc_center_median()))
    moved = 0
    for group in planes.values():
        group.sort(key=lambda t: t[0])
        for i, (d0, f0, n0, c0) in enumerate(group):
            lo0 = [min(v.co[k] for v in f0.verts) for k in range(3)]
            hi0 = [max(v.co[k] for v in f0.verts) for k in range(3)]
            for d1, f1, n1, c1 in group[i + 1:]:
                if d1 - d0 > reach:
                    break
                if d1 - d0 < 1e-4 or n0.dot(n1) < 0.995 or not all(
                        min(v.co[k] for v in f1.verts) <= hi0[k] + 0.01 and max(v.co[k] for v in f1.verts) >= lo0[k] - 0.01
                        for k in range(3)):
                    continue
                push = gap - (d1 - d0)
                if push > 0:
                    for v in f1.verts:
                        v.co += n1 * push
                    moved += 1
    return moved


def _append(dst, src, material_offset):
    """Copy the faces of bmesh `src` into `dst` (UV and Color layers created when missing)."""
    uv_d = dst.loops.layers.uv.active or dst.loops.layers.uv.new("UVMap")
    col_d = dst.loops.layers.float_color.get("Color") or dst.loops.layers.float_color.new("Color")
    uv_s, col_s = src.loops.layers.uv["UVMap"], src.loops.layers.float_color["Color"]
    for f in src.faces:
        try:
            nf = dst.faces.new([dst.verts.new(v.co) for v in f.verts])
        except ValueError:
            continue
        nf.material_index = f.material_index + material_offset
        for ld, ls in zip(nf.loops, f.loops):
            ld[uv_d].uv = ls[uv_s].uv
            ld[col_d] = ls[col_s]


def check_frame(objects):
    """Rough check that a model is in the game's frame (metres, home at the origin)."""
    pts = [o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]
    if not pts:
        return None
    size = max(max(p[i] for p in pts) - min(p[i] for p in pts) for i in range(2))
    if size > 2000:
        return T(f"El modelo mide {size:.0f} m: ¿está en milímetros? Escálalo a metros primero",
                 f"The model is {size:.0f} m across: is it in millimetres? Scale it to metres first")
    if size < 20:
        return T(f"El modelo mide {size:.1f} m: demasiado pequeño para un estadio",
                 f"The model is {size:.1f} m across: too small for a stadium")
    return None
