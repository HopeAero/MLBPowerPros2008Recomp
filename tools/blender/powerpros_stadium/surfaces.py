# SPDX-License-Identifier: GPL-3.0-or-later
"""Collision surfaces in Blender: one object per surface id («Colisión · Vallas»), with a material named
and coloured as the stadium viewer draws it («Superficie 9 · Vallas»), so each kind can be shown, hidden,
edited or made from any mesh. The export joins them back into the table's one collision.
Surface ids: docs/ingenieria-inversa.md, «IDs de superficie»."""
import re

import bpy
import numpy as np

from . import fingerprint
from .i18n import T

# id -> (Spanish, English, 0xRRGGBB), as runtime/src/stadium_collision.cpp.
SURFACES = {
    0: ("Pared invisible (foul)", "Invisible wall (foul)", 0xFF9A1F),
    1: ("Pared invisible (home run)", "Invisible wall (home run)", 0xFF2A2A),
    2: ("Límite exterior", "Outer limit", 0xB04CFF),
    5: ("Césped", "Grass", 0x3CE03C),
    6: ("Tierra", "Dirt", 0xC8843C),
    7: ("Pista de aviso", "Warning track", 0xE8C070),
    8: ("Montículo", "Mound", 0xFFD9A0),
    9: ("Vallas", "Fences", 0xFFF030),
    11: ("Gradas", "Stands", 0x40A0FF),
    13: ("Techos y gradas altas", "Stand tops, roofs", 0x40F0F0),
    14: ("Postes de foul", "Foul poles", 0xFFFFFF),
    15: ("Backstop", "Backstop", 0xFF70C0),
}
DEFAULT = 11                     # a face with no surface: stands


def name(sid):
    es, en, _ = SURFACES.get(sid, (f"Superficie {sid}", f"Surface {sid}", 0))
    return T(es, en)


def colour(sid):
    rgb = SURFACES.get(sid, (None, None, 0xA0A0A0))[2]
    return ((rgb >> 16 & 255) / 255, (rgb >> 8 & 255) / 255, (rgb & 255) / 255, 1.0)


def material(sid):
    """The surface's material, made once: «Superficie N · name», its viewer colour for the viewport."""
    for m in bpy.data.materials:
        if m.get("pp_surface") == sid:
            return m
    m = bpy.data.materials.new(f"Superficie {sid} · {name(sid)}")
    m["pp_surface"] = sid
    m.diffuse_color = colour(sid)
    return m


def surface_of(mat, fallback):
    """Surface id of a face from its material: «Superficie 9 · Vallas», or older «Superficie 9»."""
    if mat is None:
        return fallback
    m = re.match(r"\s*Superficie\s+(\d+)", mat.name) or re.search(r"(\d+)\s*$", mat.name)
    return int(m.group(1)) if m else fallback


def collision_objects(coll):
    return sorted((o for o in coll.all_objects if o.get("pp_kind") == "collision" and o.type == "MESH"),
                  key=lambda o: o.name)


def make_objects(vertices, triangles, collection):
    """One collision object per surface id from Blender-space `vertices` and [(a, b, c, surface)]
    (the normal (b-a)x(c-a) is the side the ball hits); degenerate triangles left out."""
    by_surface = {}
    for a, b, c, sid in triangles:
        if len({a, b, c}) == 3:
            by_surface.setdefault(sid, []).append((a, b, c))
    objects = []
    for sid, faces in sorted(by_surface.items()):
        used = sorted({k for f in faces for k in f})
        index = {k: i for i, k in enumerate(used)}
        label = T("Colisión", "Collision") + " · " + name(sid)
        me = bpy.data.meshes.new(label)
        me.from_pydata([vertices[k] for k in used], [], [tuple(index[k] for k in f) for f in faces])
        me.validate()
        me.materials.append(material(sid))
        attr = me.attributes.new("surface_id", "INT", "FACE")
        attr.data.foreach_set("value", np.full(len(me.polygons), sid, np.int32))
        me.update()
        obj = bpy.data.objects.new(label, me)
        obj["pp_kind"] = "collision"
        obj["pp_surface"] = sid
        collection.objects.link(obj)
        objects.append(obj)
    return objects


def layer_of(layer, collection):
    """The view layer's LayerCollection of `collection`, or None."""
    if layer.collection == collection:
        return layer
    for child in layer.children:
        found = layer_of(child, collection)
        if found is not None:
            return found
    return None


def show(context, coll, visible):
    """Show or hide every collision object (and the collision collection with them)."""
    for o in collision_objects(coll):
        o.hide_viewport = not visible
        o.hide_set(False)
    if visible:
        for c in {c for o in collision_objects(coll) for c in o.users_collection}:
            layer = layer_of(context.view_layer.layer_collection, c)
            if layer is not None:
                layer.hide_viewport = False
                layer.exclude = False


def collision_collection(coll):
    """The stadium's «… · Colisión» child collection (hidden in renders: its 70 m walls hide the stands)."""
    sub = next((c for c in coll.children if c.name.endswith("Colisión")), None)
    if sub is None:
        sub = bpy.data.collections.new(f"{coll.name} · Colisión")
        sub.hide_render = True
        coll.children.link(sub)
    return sub


def make_collision(obj, sid, coll):
    """Turn a mesh object into collision of surface `sid`: all its faces get that surface, and it moves
    to the stadium's collision collection. Its place in the scene stays as it is."""
    me = obj.data
    me.materials.clear()
    me.materials.append(material(sid))
    for p in me.polygons:
        p.material_index = 0
    attr = me.attributes.get("surface_id") or me.attributes.new("surface_id", "INT", "FACE")
    attr.data.foreach_set("value", np.full(len(me.polygons), sid, np.int32))
    me.update()
    obj["pp_kind"] = "collision"
    obj["pp_surface"] = sid
    if "pp_signature" in obj:
        del obj["pp_signature"]              # new collision: the export writes it
    sub = collision_collection(coll)
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    sub.objects.link(obj)


def remember(coll, objects):
    """Mark the collision objects as the stadium's own: the export writes the collision only if they change."""
    for o in objects:
        o["pp_signature"] = fingerprint.collision_signature(o)
    coll["pp_collision_objects"] = len(objects)


def changed(coll, objects):
    """Whether the collision objects differ from the ones imported (edited, added or deleted)."""
    if len(objects) != coll.get("pp_collision_objects", len(objects)):
        return True
    return any(o.get("pp_signature") != fingerprint.collision_signature(o) for o in objects)


def gather(objects, to_game):
    """(game-space vertices, triangles (a, b, c, surface)) of all collision objects, the same points
    welded (to 0.1 mm), as the table keeps them."""
    vertices, index, triangles = [], {}, []
    for obj in objects:
        me = obj.data
        me.calc_loop_triangles()
        attr = me.attributes.get("surface_id")
        ids = None
        if attr is not None and attr.domain == "FACE" and attr.data_type == "INT":
            ids = np.empty(len(me.polygons), np.int32)
            attr.data.foreach_get("value", ids)
        fallback_sid = obj.get("pp_surface", DEFAULT)
        mw = obj.matrix_world
        local = []
        for v in me.vertices:
            p = to_game(mw @ v.co)
            key = tuple(round(c, 3) for c in p)
            if key not in index:
                index[key] = len(vertices)
                vertices.append(p)
            local.append(index[key])
        for lt in me.loop_triangles:
            mat = me.materials[lt.material_index] if lt.material_index < len(me.materials) else None
            sid = surface_of(mat, int(ids[lt.polygon_index]) if ids is not None else fallback_sid)
            a, b, c = (local[k] for k in lt.vertices)
            if len({a, b, c}) == 3:
                triangles.append((a, b, c, sid))
    return vertices, triangles

