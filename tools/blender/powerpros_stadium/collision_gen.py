# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the collision of a stadium from its pieces (for converted models; game stadiums bring theirs).

Surfaces as in the game's own stadiums (docs/ingenieria-inversa.md): floor 5 grass / 6 dirt / 8 mound,
9 field walls, 15 backstop, 0 invisible wall over the walls in foul territory, 1 over the outfield
wall, 2 outer limit behind the outfield, 11 stands, 13 their flat parts, 14 foul poles. Triangles
are wound so that their normal is the side the ball hits. Frame: metres, home plate at the origin,
centre field towards +Y (Blender), like an imported stadium.
"""
import math

import bmesh
import bpy
import numpy as np
from mathutils import Vector

TOP = 102.8                         # the game's invisible walls reach 102.8 m
MOUND = Vector((0.0, 18.51, 0.0))   # centre of the mound (surface 8) in every game stadium but Toronto
MOUND_RADIUS = 2.82
FIELD_POINT = Vector((0.0, 45.0))   # a point of every field: the outline is searched from here


def _mean_colour(img, cache):
    if img.name not in cache:
        px = np.empty(len(img.pixels), np.float32)
        img.pixels.foreach_get(px)
        cache[img.name] = tuple(float(v) for v in px.reshape(-1, 4)[:, :3].mean(axis=0))
    return cache[img.name]


def _image(mat):
    if mat is None or mat.node_tree is None:
        return None
    return next((n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)


def gather(objects):
    """Floor triangles ([3 Vector], green?) and the rest ([3 Vector]) in world space."""
    floor, rest, cache = [], [], {}
    for o in objects:
        me = o.data
        me.calc_loop_triangles()
        col = me.color_attributes.get("Color")
        mw = o.matrix_world
        for lt in me.loop_triangles:
            vs = [mw @ me.vertices[i].co for i in lt.vertices]
            u, v = vs[1] - vs[0], vs[2] - vs[0]
            n = u.cross(v)
            if n.length < 1e-9:
                continue
            n.normalize()
            c = (vs[0] + vs[1] + vs[2]) / 3
            if n.z > 0.95 and c.z < 1.0 and c.xy.length < 160 and lt.area > 0.05:
                mat = o.material_slots[lt.material_index].material if lt.material_index < len(o.material_slots) else None
                img = _image(mat)
                rgb = list(_mean_colour(img, cache)) if img else [1.0, 1.0, 1.0]
                if col is not None:
                    vc = [col.data[li if col.domain == "CORNER" else me.loops[li].vertex_index].color for li in lt.loops]
                    for k in range(3):
                        rgb[k] *= sum(c4[k] for c4 in vc) / 3
                if not img and col is None and mat is not None:
                    rgb = list(mat.diffuse_color[:3])
                floor.append((vs, rgb[1] > rgb[0] * 1.1 and rgb[1] > rgb[2]))
            elif n.z < -0.95 and c.z < 1.0:
                continue                                  # undersides of the field
            else:
                rest.append(vs)
    return floor, rest


def field_outline(rest, height=0.6, step=1.0):
    """The nearest wall in every direction from a point of the field, cut at `height` (a model's
    floor usually goes on under its stands, so the floor's own outline is not the field's)."""
    segs = []
    for vs in rest:
        pts = []
        for a, b in zip(vs, vs[1:] + vs[:1]):
            if (a.z - height) * (b.z - height) < 0:
                t = (height - a.z) / (b.z - a.z)
                pts.append((a + (b - a) * t).xy)
        if len(pts) == 2 and (pts[0] - pts[1]).length > 0.6:       # thin poles and flags do not count
            segs.append((pts[0], pts[1]))
    dists = []
    for k in range(int(360 / step)):
        th = math.radians(k * step)
        d = Vector((math.sin(th), math.cos(th)))
        best = None
        for a, b in segs:
            e = b - a
            den = d.x * e.y - d.y * e.x
            if abs(den) < 1e-12:
                continue
            w = a - FIELD_POINT
            t = (w.x * e.y - w.y * e.x) / den
            u = (w.x * d.y - w.y * d.x) / den
            if 0 <= u <= 1 and t > 1.0 and (best is None or t < best):
                best = t
        dists.append((d, best))
    known = [b for _, b in dists if b is not None]
    if len(known) < len(dists) // 2:
        return None
    fill = sorted(known)[len(known) // 2]
    radius = [b if b is not None else fill for _, b in dists]
    n, half = len(radius), 4                       # median of 9: openings and stairs make spikes
    outline = []
    for k, (d, _) in enumerate(dists):
        r = sorted(radius[(k + j) % n] for j in range(-half, half + 1))[half]
        p = FIELD_POINT + d * r
        outline.append(Vector((p.x, p.y, 0.0)))
    outline.reverse()                              # the sweep was clockwise
    return outline


def wall_height(mid, rest):
    tops = [max(v.z for v in vs) for vs in rest
            if min(v.z for v in vs) < 0.5 and max(v.z for v in vs) < 8.0
            and min((v.xy - mid.xy).length for v in vs) < 1.2]
    if not tops:
        return 2.5
    tops.sort()
    return min(5.0, max(1.0, tops[len(tops) // 2]))


def angle(p):
    return math.degrees(math.atan2(p.x, p.y))      # 0 = centre field, +-45 = the foul lines


def quad(out, a, b, z0, z1, sid):
    """Vertical quad over a-b facing the left of a->b."""
    p = [Vector((a.x, a.y, z0)), Vector((b.x, b.y, z0)), Vector((b.x, b.y, z1)), Vector((a.x, a.y, z1))]
    out += [((p[0], p[2], p[1]), sid), ((p[0], p[3], p[2]), sid)]


def box(out, centre, half, height, sid):
    x, y = centre.x, centre.y
    c = [Vector((x - half, y - half)), Vector((x + half, y - half)), Vector((x + half, y + half)), Vector((x - half, y + half))]
    for a, b in zip(c, c[1:] + c[:1]):
        quad(out, b, a, 0.0, height, sid)          # outward
    top = [Vector((v.x, v.y, height)) for v in c]
    out += [((top[0], top[1], top[2]), sid), ((top[0], top[2], top[3]), sid)]


def _near(v, walls, dist):
    p = v.xy
    for a, b, _ in walls:
        e = (b - a).xy
        t = max(0.0, min(1.0, (p - a.xy).dot(e) / e.length_squared))
        if (a.xy + e * t - p).length < dist:
            return True
    return False


def _inside(v, poly):
    x, y, inside = v.x, v.y, False
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if (a.y > y) != (b.y > y) and x < a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y):
            inside = not inside
    return inside


def _hull(points):
    pts = sorted(set((round(p.x, 3), round(p.y, 3)) for p in points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return [Vector(p) for p in lower[:-1] + upper[:-1]]           # counter-clockwise


def build(objects, stand_target=5000):
    """[(three Vector, surface id)] for the stadium made of `objects`, or None without a field."""
    floor, rest = gather(objects)
    outline = field_outline(rest)
    if not floor or outline is None:
        return None
    tris = []
    for vs, green in floor:                        # the floor, with the game's mound in place of the model's
        if not all((v.xy - MOUND.xy).length < 3.0 for v in vs):
            tris.append((tuple(vs), 5 if green else 6))
    ring = [MOUND + Vector((MOUND_RADIUS * math.sin(a), MOUND_RADIUS * math.cos(a), 0.01)) for a in
            (2 * math.pi * k / 16 for k in range(16))]
    for a, b in zip(ring, ring[1:] + ring[:1]):
        tris.append(((MOUND + Vector((0, 0, 0.01)), b, a), 8))
    walls = []
    for a, b in zip(outline, outline[1:] + outline[:1]):           # walls, invisible walls over them
        if (b - a).xy.length < 1e-3:
            continue
        mid = (a + b) / 2
        h = wall_height(mid, rest)
        fair = abs(angle(mid)) <= 45.0 and mid.y > 0
        quad(tris, a, b, 0.0, h, 15 if (mid.y < 0 and abs(mid.x) < 25) else 9)
        quad(tris, a, b, h, TOP, 1 if fair else 0)
        walls.append((a, b, h))
    for side in (-1, 1):                           # foul poles where the foul lines meet the walls
        d = Vector((side * math.sqrt(0.5), math.sqrt(0.5)))
        best = None
        for a, b, _ in walls:
            e = (b - a).xy
            den = d.x * e.y - d.y * e.x
            if abs(den) < 1e-9:
                continue
            t = (a.x * e.y - a.y * e.x) / den
            s = (a.x * d.y - a.y * d.x) / den
            if 0 <= s <= 1 and t > 20 and (best is None or t < best):
                best = t
        if best is not None:
            box(tris, d * (best + 0.3), 0.2, 25.0, 14)
    bm = bmesh.new()                               # stands: everything else, simplified
    for vs in rest:
        if max(v.z for v in vs) < 6.0 and any(_near(v, walls, 0.8) for v in vs):
            continue                               # the model's own field walls: replaced by the ones above
        if max(v.z for v in vs) < 1.0 and all(_inside(v, outline) for v in vs):
            continue                               # bases, plate, mound slopes: the floor covers them
        try:
            bm.faces.new([bm.verts.new(v) for v in vs])
        except ValueError:
            pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.05)
    me = bpy.data.meshes.new("pp_stands_tmp")
    bm.to_mesh(me)
    bm.free()
    tmp = bpy.data.objects.new("pp_stands_tmp", me)
    bpy.context.scene.collection.objects.link(tmp)
    try:
        if len(me.polygons) > stand_target:
            mod = tmp.modifiers.new("dec", "DECIMATE")
            mod.ratio = stand_target / len(me.polygons)
            dg = bpy.context.evaluated_depsgraph_get()
            simple = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
        else:
            simple = me
        simple.calc_loop_triangles()
        for lt in simple.loop_triangles:
            vs = tuple(simple.vertices[i].co.copy() for i in lt.vertices)
            flat = abs(lt.normal.z) > 0.9 and min(v.z for v in vs) > 0.5
            tris.append((vs, 13 if flat else 11))
        if simple is not me:
            bpy.data.meshes.remove(simple)
    finally:
        bpy.data.objects.remove(tmp)
        bpy.data.meshes.remove(me)
    pts = [v for vs, _ in tris for v in vs] + [v for vs in rest for v in vs]
    hull = _hull([p.xy for p in pts])              # outer limit beyond the outfield, 5 m out
    centre = sum(hull, Vector((0, 0))) / len(hull)
    hull = [p + (p - centre).normalized() * 5.0 for p in hull]
    for a, b in zip(hull, hull[1:] + hull[:1]):
        mid = (a + b) / 2
        if mid.y > 0 and abs(angle(Vector((mid.x, mid.y, 0)))) < 70:
            quad(tris, Vector((b.x, b.y, 0)), Vector((a.x, a.y, 0)), 0.0, TOP, 2)
    return tris


def make_object(tris, collection):
    """Collision object like the importer's: materials «Superficie N» and a surface_id attribute."""
    verts, index, faces, ids = [], {}, [], []
    for vs, sid in tris:
        f = []
        for v in vs:
            key = (round(v.x, 3), round(v.y, 3), round(v.z, 3))
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            f.append(index[key])
        if len(set(f)) == 3:
            faces.append(f)
            ids.append(sid)
    me = bpy.data.meshes.new("Colisión")
    me.from_pydata(verts, [], faces)
    used = sorted(set(ids))
    for i in used:
        me.materials.append(bpy.data.materials.get(f"Superficie {i}") or bpy.data.materials.new(f"Superficie {i}"))
    slot = {i: k for k, i in enumerate(used)}
    me.polygons.foreach_set("material_index", np.asarray([slot[i] for i in ids], np.int32))
    attr = me.attributes.new("surface_id", "INT", "FACE")
    attr.data.foreach_set("value", np.asarray(ids, np.int32))
    me.update()
    obj = bpy.data.objects.new("Colisión", me)
    obj["pp_kind"] = "collision"
    collection.objects.link(obj)
    return obj


def generate(coll):
    """Replace the stadium's collision with one generated from its pieces (in a from-scratch collection,
    all its meshes). Returns the object or None."""
    pieces = [o for o in coll.all_objects if o.type == "MESH" and (o.get("pp_kind") == "model" or
              (coll.get("pp_scratch") and o.get("pp_kind") != "collision"))]
    tris = build(pieces)
    if tris is None:
        return None
    sub = next((c for c in coll.children if c.name.endswith("Colisión")), None)
    if sub is None:
        sub = bpy.data.collections.new(f"{coll.name} · Colisión")
        sub.hide_render = True
        coll.children.link(sub)
    for o in list(sub.objects):
        if o.get("pp_kind") == "collision":
            bpy.data.objects.remove(o)
    return make_object(tris, sub)
