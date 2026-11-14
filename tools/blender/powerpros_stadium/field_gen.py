# SPDX-License-Identifier: GPL-3.0-or-later
"""Field generator: a whole baseball field for a from-scratch stadium from a few numbers, in the spirit of
OOTP's park generator: distances to the wall (feet), wall height, warning track, foul territory, grass or
turf, infield style and mowing pattern.

The ground is a stack of role «ground» objects a centimetre apart (grass, dirt skin and warning track,
infield grass, dirt circles): the game draws the ground without depth test, bottom up by height, so each
layer covers the one below. Outfield and foul walls are role «fence»; foul poles and the distance signs are
stands. «Generar colisión» then builds the collision from them as from any model. The game draws its own
bases, plate, mound and lines (the collection's BCL pieces). Frame: metres, home plate at the origin,
centre field +Y, first base +X. Textures are made here (no game data)."""
import json
import math
import os
from dataclasses import asdict, dataclass

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

from .i18n import T

FT = 0.3048
MOUND = Vector((0.0, 18.51))         # the game's mound (collision_gen.MOUND)
TAG = "field"                        # obj["pp_generated"] of the objects made here


@dataclass
class FieldParams:
    lf: float = 330.0                # feet to the wall: left field line, left-centre, centre, right-centre, right line
    lcf: float = 375.0
    cf: float = 400.0
    rcf: float = 375.0
    rf: float = 330.0
    wall_l: float = 8.0              # outfield wall height, feet, per sector: left (pole to left-centre),
    wall_lc: float = 8.0             # left-centre to centre, centre to right-centre, right-centre to the pole
    wall_rc: float = 8.0             # (a Green Monster: wall_l = 37)
    wall_r: float = 8.0
    foul_wall: float = 4.0           # wall around foul territory, feet
    track: float = 15.0              # warning track width, feet
    foul: float = 40.0               # foul territory: from the foul line to the wall by first/third base, feet
    backstop: float = 60.0           # home plate to the wall behind it, feet
    surface: str = "grass"           # grass | turf
    infield: str = "grass"           # grass (dirt skin, grass square) | dirt (all dirt) | turf (dirt cut-outs)
    pattern: str = "stripes"         # none | stripes | checker | diagonal | circles
    stripe: float = 15.0             # mowing stripe width, feet
    grass_color: tuple = (0.22, 0.48, 0.16)
    dirt_color: tuple = (0.55, 0.38, 0.24)
    color_l: tuple = (0.10, 0.22, 0.14)     # outfield wall colour per sector (as the heights), and the foul wall's
    color_lc: tuple = (0.10, 0.22, 0.14)
    color_rc: tuple = (0.10, 0.22, 0.14)
    color_r: tuple = (0.10, 0.22, 0.14)
    style_l: str = ""                # outfield wall look per sector: "" its colour, or a game stadium's code (its
    style_lc: str = ""               # fence's main texture, mapped as that stadium maps it: game_fence_style)
    style_rc: str = ""
    style_r: str = ""
    ground: str = ""                 # "" the generated ground, or a game stadium's code: its ground pieces as they are
    signs: bool = True               # distance numbers on the wall
    lines: bool = True               # chalk: foul lines, batter's, catcher's and coach's boxes, running lane, on-deck circles

    COLORS = ("grass_color", "dirt_color", "color_l", "color_lc", "color_rc", "color_r")

    def heights(self):
        return (self.wall_l, self.wall_lc, self.wall_rc, self.wall_r)

    def colors(self):
        return (self.color_l, self.color_lc, self.color_rc, self.color_r)

    def styles(self):
        return (self.style_l, self.style_lc, self.style_rc, self.style_r)

    def to_json(self):
        return json.dumps(asdict(self))

    @classmethod
    def from_json(cls, text):
        try:
            data = json.loads(text)
        except (TypeError, ValueError):
            return cls()
        for s in ("l", "lc", "rc", "r"):            # fields saved before the sectors: one height and colour
            if "wall" in data:
                data.setdefault(f"wall_{s}", data["wall"])
            if "wall_color" in data:
                data.setdefault(f"color_{s}", data["wall_color"])
        known = {k: v for k, v in data.items() if k in cls.__dataclass_fields__}
        for k in cls.COLORS:
            if k in known:
                known[k] = tuple(known[k])
        return cls(**known)


SECTORS = (-45.0, -22.5, 0.0, 22.5, 45.0)    # bearings bounding the four outfield wall sectors


def sector(bearing):
    """0..3: left, left-centre, right-centre, right."""
    return min(3, max(0, int((bearing + 45.0) // 22.5)))


# ---------------------------------------------------------------- outline

def _catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def wall_radius(params, bearing):
    """Distance (m) from home to the outfield wall at `bearing` degrees (0 centre field, +45 right field
    line), a smooth curve through the five distances."""
    knots = [-45.0, -22.5, 0.0, 22.5, 45.0]
    values = [params.lf, params.lcf, params.cf, params.rcf, params.rf]
    ext = [2 * values[0] - values[1]] + values + [2 * values[-1] - values[-2]]
    b = max(-45.0, min(45.0, bearing))
    i = min(3, int((b + 45.0) // 22.5))
    t = (b - knots[i]) / 22.5
    return _catmull(ext[i], ext[i + 1], ext[i + 2], ext[i + 3], t) * FT


def _at(bearing, r):
    a = math.radians(bearing)
    return Vector((r * math.sin(a), r * math.cos(a)))


def outline(params, step=1.5):
    """(points, outfield count): the field's edge counter-clockwise as 2D Vectors, the outfield wall
    from the right field pole to the left one first, then the foul walls around behind home."""
    n = int(round(90.0 / step))
    outfield = [_at(45.0 - 90.0 * k / n, wall_radius(params, 45.0 - 90.0 * k / n)) for k in range(n + 1)]
    pole_l, pole_r = outfield[-1], outfield[0]
    side = 120.0 * FT                                    # where the side walls turn towards home
    out_l = Vector((-math.sqrt(0.5), -math.sqrt(0.5)))   # away from the left foul line
    out_r = Vector((math.sqrt(0.5), -math.sqrt(0.5)))
    q_l = _at(-45.0, side) + out_l * params.foul * FT
    q_r = _at(45.0, side) + out_r * params.foul * FT
    back = params.backstop * FT
    ring = [pole_l, q_l, _at(-135.0, back), _at(180.0, back), _at(135.0, back), q_r, pole_r]
    foul = [q_l]
    for i in range(1, len(ring) - 2):                     # smooth from q_l round behind home to q_r
        for k in range(1, 9):
            foul.append(_catmull(ring[i - 1], ring[i], ring[i + 1], ring[i + 2], k / 8))
    return outfield + foul, len(outfield)


def inset(poly, d):
    """The closed CCW polygon moved d metres inwards (miter joins, capped)."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b, c = poly[i - 1], poly[i], poly[(i + 1) % n]
        e1, e2 = (b - a).normalized(), (c - b).normalized()
        n1, n2 = Vector((-e1.y, e1.x)), Vector((-e2.y, e2.x))      # left = inside for CCW
        m = (n1 + n2)
        if m.length < 1e-6:
            m = n1
        m.normalize()
        scale = 1.0 / max(0.5, m.dot(n1))
        out.append(b + m * d * scale)
    return out


def clip(poly, planes):
    """Convex polygon clipped to the half-planes {p : p . n >= -offset} (Sutherland-Hodgman); n points
    into the kept side."""
    for n, offset in planes:
        out = []
        for i, b in enumerate(poly):
            a = poly[i - 1]
            da, db = a.dot(n) + offset, b.dot(n) + offset
            if da >= 0:
                out.append(a)
            if (da >= 0) != (db >= 0):
                out.append(a + (b - a) * (da / (da - db)))
        poly = out
    return poly


def circle(centre, r, segments=48):
    return [centre + Vector((r * math.cos(2 * math.pi * k / segments), r * math.sin(2 * math.pi * k / segments)))
            for k in range(segments)]


# ---------------------------------------------------------------- chalk and where people stand
# Rule-book sizes in feet, home plate's back point at the origin. The game's own lines piece
# (DP_ALP_MIP_line) is drawn for its stadium's distances, so the generated field brings its own.

LINE = 4 / 12 * FT                      # chalk width
PLATE_CENTRE = 0.7 * FT                 # the plate's centre, in front of its back point
BOX_IN, BOX_OUT, BOX_HALF = 1.21 * FT, 5.21 * FT, 3.0 * FT   # batter's boxes: 6 in off the plate, 4 x 6 ft
CATCHER_HALF, CATCHER_DEPTH = 43 / 24 * FT, 8.0 * FT
COACH_FROM, COACH_TO, COACH_OFF, COACH_DEPTH = 70 * FT, 90 * FT, 15 * FT, 10 * FT
ON_DECK_DIST, ON_DECK_BEARING, ON_DECK_R = 37 * FT, 100.0, 2.5 * FT


def _stroke(a, b, w=LINE):
    """Rectangle of width w along a-b."""
    d = (b - a).normalized()
    n = Vector((-d.y, d.x)) * (w / 2)
    return [a + n, b + n, b - n, a - n]


def _ring(centre, r, segments=24):
    pts = circle(centre, r, segments)
    return [_stroke(pts[k], pts[(k + 1) % segments]) for k in range(segments)]


def _foul_axes(side):
    """(along the line, out into foul ground) for the first (+1) or third (-1) base line."""
    d = Vector((side * math.sqrt(0.5), math.sqrt(0.5)))
    return d, Vector((side * d.y, -side * d.x))


def on_deck(side):
    return _at(side * ON_DECK_BEARING, ON_DECK_DIST)


def chalk(params):
    """The field's chalk as flat polygons."""
    polys = []
    c = Vector((0.0, PLATE_CENTRE))
    for side in (-1, 1):
        d, out = _foul_axes(side)
        start = (c.y + BOX_HALF) * math.sqrt(2)                 # from the front of the batter's box to the pole
        polys.append(_stroke(d * start, d * (wall_radius(params, side * 45.0) - 0.05)))
        x0, x1 = side * BOX_IN, side * BOX_OUT                  # batter's box
        box = [Vector((x0, c.y - BOX_HALF)), Vector((x1, c.y - BOX_HALF)), Vector((x1, c.y + BOX_HALF)),
               Vector((x0, c.y + BOX_HALF))]
        polys += [_stroke(box[k], box[(k + 1) % 4]) for k in range(4)]
        back = c.y - BOX_HALF                                   # catcher's box side
        polys.append(_stroke(Vector((side * CATCHER_HALF, back)), Vector((side * CATCHER_HALF, back - CATCHER_DEPTH))))
        a, b = d * COACH_FROM + out * COACH_OFF, d * COACH_TO + out * COACH_OFF   # coach's box, open at the back
        polys += [_stroke(a, b), _stroke(a, a + out * COACH_DEPTH), _stroke(b, b + out * COACH_DEPTH)]
        polys += _ring(on_deck(side), ON_DECK_R)
    polys.append(_stroke(Vector((-CATCHER_HALF, c.y - BOX_HALF - CATCHER_DEPTH)),
                         Vector((CATCHER_HALF, c.y - BOX_HALF - CATCHER_DEPTH))))
    d, out = _foul_axes(1)                                       # running lane: the second half to first, 3 ft out
    polys += [_stroke(d * 45 * FT + out * 3 * FT, d * 90 * FT + out * 3 * FT), _stroke(d * 45 * FT, d * 45 * FT + out * 3 * FT)]
    return polys


def positions(params):
    """[(label, 2D point)] where the players, umpire and coaches stand at the start of a play (a usual
    defence; the game moves them), and the on-deck batter. Outfielders stay 12 m off the wall."""
    def deep(bearing, feet):
        return _at(bearing, min(feet * FT, wall_radius(params, bearing) - 12.0))
    spots = [("P", MOUND), ("C", Vector((0.0, -2.5 * FT))), (T("Árbitro", "Umpire"), Vector((0.0, -5.5 * FT))),
             ("1B", _at(38.0, 105 * FT)), ("2B", _at(13.0, 148 * FT)), ("SS", _at(-13.0, 148 * FT)),
             ("3B", _at(-38.0, 105 * FT)), ("LF", deep(-28.0, 290)), ("CF", deep(0.0, 320)), ("RF", deep(28.0, 290)),
             (T("En espera", "On deck"), on_deck(-1))]
    for side, name in ((1, T("Coach de 1ª", "1st base coach")), (-1, T("Coach de 3ª", "3rd base coach"))):
        d, out = _foul_axes(side)
        spots.append((name, d * (COACH_FROM + COACH_TO) / 2 + out * (COACH_OFF + COACH_DEPTH / 2)))
    return spots


def benches(params):
    """(label, centre, along) of the two dugouts: behind the foul wall, between home and the bases."""
    out = []
    for side, name in ((1, T("Banca de 1ª", "1st base dugout")), (-1, T("Banca de 3ª", "3rd base dugout"))):
        d, o = _foul_axes(side)
        out.append((name, d * 70 * FT + o * (params.foul + 8) * FT, d))
    return out


# ---------------------------------------------------------------- textures

def _noise(size, seed, amount):
    rng = np.random.default_rng(seed)
    n = rng.normal(0.0, amount, (size, size))
    k = np.ones(3) / 3                                   # soften: blades of grass, grains of dirt
    n = np.apply_along_axis(lambda r: np.convolve(r, k, "same"), 0, n)
    return np.apply_along_axis(lambda r: np.convolve(r, k, "same"), 1, n)


def _image(name, rgb):
    """A packed Blender image from an (h, w, 3) float array, top row first."""
    h, w, _ = rgb.shape
    img = bpy.data.images.get(name)
    if img is not None:
        bpy.data.images.remove(img)
    img = bpy.data.images.new(name, w, h, alpha=False)
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((h, w, 1))], axis=2)[::-1]
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.pack()
    return img


def grass_image(params, name="pp_field_grass"):
    """256 x 256: two shades of the grass colour laid out for the pattern's UV mapping (_grass_uv)."""
    size = 256
    base = np.array(params.grass_color, dtype=np.float64)
    turf = params.surface == "turf"
    light, dark = base * (1.10 if not turf else 1.05), base * (0.86 if not turf else 0.95)
    u = np.arange(size)[None, :] >= size // 2
    v = np.arange(size)[:, None] >= size // 2
    if params.pattern in ("stripes", "circles"):
        mask = np.broadcast_to(u, (size, size))
    elif params.pattern in ("checker", "diagonal"):
        mask = u ^ v
    else:
        mask = np.zeros((size, size), bool)
    rgb = np.where(mask[..., None], dark, light)
    rgb = rgb * (1.0 + _noise(size, 7, 0.05 if not turf else 0.02))[..., None]
    return _image(name, rgb)


def dirt_image(params, name="pp_field_dirt"):
    size = 128
    rgb = np.ones((size, size, 3)) * np.array(params.dirt_color)
    rgb = rgb * (1.0 + _noise(size, 11, 0.08))[..., None]
    return _image(name, rgb)


def _grass_uv(params, p):
    w = 2 * params.stripe * FT                              # one light and one dark stripe per repeat
    if params.pattern == "stripes":
        return (p.x / w, p.y / w)
    if params.pattern == "checker":
        return (p.x / w, p.y / w)
    if params.pattern == "diagonal":
        return ((p.x + p.y) / w, (p.y - p.x) / w)
    if params.pattern == "circles":
        return (p.length / w, 0.25)
    return (p.x / 8.0, p.y / 8.0)


# ---------------------------------------------------------------- objects

def _material(name, image=None, color=None):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    for n in [n for n in nt.nodes if n.type == "TEX_IMAGE"]:
        nt.nodes.remove(n)
    if image is not None:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = image
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if color is not None:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        mat.diffuse_color = (*color, 1.0)
    return mat


def _object(coll, name, bm, mat, role):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    coll.objects.link(obj)
    obj.powerpros_role = role
    obj["pp_generated"] = TAG
    # The walls are a row of nearly parallel quads touching each other: the add-on's anti-flicker push
    # (convert.separate_layers) would move every other one 25 cm and open gaps; the signs sit 6 cm in front.
    obj.powerpros_separate_layers = False
    return obj


def _flat(polygons, z, uv_of, max_edge=4.0):
    """bmesh of filled flat polygons at height z (each a list of 2D Vectors, CCW or not), triangulated and
    subdivided so no edge is longer than max_edge (the UV mapping may be non-linear), UVs from uv_of(p)."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    for poly in polygons:
        try:
            f = bm.faces.new([bm.verts.new((p.x, p.y, z)) for p in poly])
        except ValueError:
            continue
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for _ in range(8):
        long = [e for e in bm.edges if e.calc_length() > max_edge]
        if not long:
            break
        bmesh.ops.subdivide_edges(bm, edges=long, cuts=1, use_grid_fill=False)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    for f in bm.faces:
        for loop in f.loops:
            loop[uv].uv = uv_of(loop.vert.co.xy)
    return bm


def _band(outer, inner, z, uv_scale=4.0):
    """bmesh of the ring between two closed polygons with the same number of points."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    n = len(outer)
    vo = [bm.verts.new((p.x, p.y, z)) for p in outer]
    vi = [bm.verts.new((p.x, p.y, z)) for p in inner]
    for i in range(n):
        j = (i + 1) % n
        try:
            f = bm.faces.new((vo[i], vo[j], vi[j], vi[i]))
        except ValueError:
            continue
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
        for loop in f.loops:
            loop[uv].uv = (loop.vert.co.x / uv_scale, loop.vert.co.y / uv_scale)
    return bm


def _wall(points, height, closed=False, uv_of=None):
    """bmesh of a vertical wall along the points, facing the left of their direction (the field, for the
    CCW outline). uv_of(s, z): UVs from the distance s along the wall, left to right as seen from the
    field, and the height z."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    bottom = [bm.verts.new((p.x, p.y, 0.0)) for p in points]
    top = [bm.verts.new((p.x, p.y, height)) for p in points]
    along = [0.0]
    for a, b in zip(points, points[1:]):
        along.append(along[-1] + (b - a).length)
    count = len(points) if closed else len(points) - 1
    for i in range(count):
        j = (i + 1) % len(points)
        f = bm.faces.new((bottom[j], bottom[i], top[i], top[j]))
        if uv_of is not None:
            for loop, k in zip(f.loops, (j, i, i, j)):
                loop[uv].uv = uv_of(along[-1] - along[k], loop.vert.co.z)   # the points run right to left
    return bm


# ---------------------------------------------------------------- looks of the game's stadiums
# A wall can take the look of a game stadium's fence: its main texture (the one covering most of it),
# read from the player's own project.bin and mapped the way that stadium maps it, so the picture keeps
# its proportions: the band scaled whole to the wall's height. Game content stays in the .blend and the
# player's mod. (A game's turf is no tiled picture: Metrodome's rings round the mound are its geometry.)

_STYLE_CACHE = {}


def _game_meshes(project_bin, code, prefix):
    """([(mesh, world matrix)], materials) of the stadium's pieces whose model name starts with prefix
    (temporary: _drop them)."""
    from . import game_parts, importer, pj7m
    gfx, placements = game_parts._stadium(project_bin, code)
    if gfx is None:
        return [], []
    images, materials, out = {}, {}, []
    for pl in placements:
        model = pj7m.parse_model(gfx, pl.model)
        if model.name.startswith(prefix):
            out.append((importer.build_mesh(gfx, model, images, materials), importer.placement_matrix(pl)))
    return out, list(materials.values())


def _drop(meshes, materials):
    for me, _ in meshes:
        bpy.data.meshes.remove(me)
    for mat in materials:
        if mat.users == 0:
            bpy.data.materials.remove(mat)


def _image_of(mat):
    if mat is None or not mat.use_nodes:
        return None
    return next((n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)


def _main_texture(meshes, keep_face):
    """(image, [(world corners, uvs, colours)]) of the texture covering most area among the faces
    keep_face(normal) accepts."""
    area, faces = {}, {}
    for me, mw in meshes:
        me.calc_loop_triangles()
        uv, col = me.uv_layers["UVMap"].data, me.color_attributes.get("Color")
        rot = mw.to_3x3()
        for lt in me.loop_triangles:
            n = (rot @ lt.normal).normalized()
            img = _image_of(me.materials[lt.material_index] if lt.material_index < len(me.materials) else None)
            if img is None or not keep_face(n):
                continue
            corners = [mw @ me.vertices[v].co for v in lt.vertices]
            area[img] = area.get(img, 0.0) + lt.area
            faces.setdefault(img, []).append((corners, [Vector(uv[li].uv) for li in lt.loops],
                                              [tuple(col.data[li].color) for li in lt.loops] if col else []))
    if not area:
        return None, []
    best = max(area, key=area.get)
    return best, faces[best]


def _tint(faces):
    """The faces' mean vertex colour (the game draws texture x vertex colour)."""
    cols = [c for _, _, cs in faces for c in cs]
    return tuple(sum(c[k] for c in cols) / len(cols) for k in range(3)) if cols else (1.0, 1.0, 1.0)


def game_fence_style(project_bin, code):
    """{image, tint, u_per_m, v_per_m, v0, height} of the stadium's main fence texture, or None: u per
    metre along the fence (left to right seen from the field), v per metre up, v at the foot of the
    band, and the band's height."""
    key = ("fence", os.path.normcase(os.path.abspath(project_bin)), code)
    if key in _STYLE_CACHE:
        try:
            if _STYLE_CACHE[key] is None or _STYLE_CACHE[key]["image"].name in bpy.data.images:
                return _STYLE_CACHE[key]
        except ReferenceError:              # the image was deleted from the .blend
            pass
    meshes, mats = _game_meshes(project_bin, code, "DP_fence")
    image, faces = _main_texture(meshes, lambda n: abs(n.z) < 0.3)
    style = None
    dus, dvs, zs = [], [], []
    for corners, uvs, _ in faces:
        n = (corners[1] - corners[0]).cross(corners[2] - corners[0])
        if n.length < 1e-9:
            continue
        right = Vector((0.0, 0.0, 1.0)).cross(n.normalized())     # left to right for one facing the wall
        if right.length < 1e-6:
            continue
        right.normalize()
        st = [Vector((c.dot(right), c.z)) for c in corners]
        e = Matrix(((st[1].x - st[0].x, st[2].x - st[0].x), (st[1].y - st[0].y, st[2].y - st[0].y)))
        if abs(e.determinant()) < 1e-6:
            continue
        d = Matrix(((uvs[1].x - uvs[0].x, uvs[2].x - uvs[0].x), (uvs[1].y - uvs[0].y, uvs[2].y - uvs[0].y)))
        j = d @ e.inverted()                                       # d(u, v) / d(s, z)
        dus.append(j[0][0])
        dvs.append(j[1][1])
        zs += [c.z for c in corners]
    if dus:
        dus.sort()
        dvs.sort()
        zs.sort()
        u_per_m, v_per_m = dus[len(dus) // 2], dvs[len(dvs) // 2]
        z0, z1 = zs[len(zs) // 50], zs[-1 - len(zs) // 50]          # the band, without stray faces
        v0 = sorted(uv.y - v_per_m * (c.z - z0) for corners, uvs, _ in faces for c, uv in zip(corners, uvs))
        style = {"image": image, "tint": _tint(faces), "u_per_m": u_per_m, "v_per_m": v_per_m,
                 "v0": v0[len(v0) // 2], "height": max(0.3, z1 - z0)}
    _drop(meshes, mats)
    _STYLE_CACHE[key] = style
    return style


def _styled_material(name, style):
    """The style's image times its mean vertex colour, as the game draws it."""
    mat = _material(name, style["image"])
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    for n in [n for n in nt.nodes if n.type == "MIX"]:
        nt.nodes.remove(n)
    tex = next(n for n in nt.nodes if n.type == "TEX_IMAGE")
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type, mix.blend_type = "RGBA", "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["B"].default_value = (*style["tint"], 1.0)
    nt.links.new(tex.outputs["Color"], mix.inputs["A"])
    nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
    mat.diffuse_color = (*style["tint"], 1.0)
    return mat


def _fence_uv(style, height):
    """UVs of a wall of that height in a game fence's look: the band scaled whole to the height, the
    same proportions along it."""
    if style is None:
        return lambda s, z: (s / 4.0, z / 4.0)
    k = style["height"] / height
    return lambda s, z: (s * style["u_per_m"] * k, style["v0"] + z * style["v_per_m"] * k)


def _pole(centre, height=20.0, half=0.15):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x = centre.x + v.co.x * 2 * half
        v.co.y = centre.y + v.co.y * 2 * half
        v.co.z = (v.co.z + 0.5) * height
    return bm


SIGN_GAP = 0.08     # metres in front of the wall


def _sign(coll, text, params, bearing, mat):
    """Distance number on the wall's face, read from home. Laid along the wall, not across the line from
    home: between two distances the wall runs at an angle, and a number square to home went half into it."""
    wall_height = params.heights()[sector(bearing)] * FT
    curve = bpy.data.curves.new(f"pp_field_sign_{text}", "FONT")
    curve.body = text
    curve.align_x, curve.align_y = "CENTER", "CENTER"
    curve.size = min(1.4, wall_height * 0.55)
    wall_height = min(wall_height, 2.7)                 # on a tall wall (a Green Monster) the number stays low
    curve.extrude = 0.01
    obj = bpy.data.objects.new(f"Campo · {text}", curve)
    a, b = (_at(d, wall_radius(params, d)) for d in (bearing - 1.0, bearing + 1.0))
    along = (b - a).normalized()                        # left to right as seen from home
    p = _at(bearing, wall_radius(params, bearing)) + Vector((along.y, -along.x)) * SIGN_GAP
    obj.location = (p.x, p.y, wall_height * 0.55)
    obj.rotation_euler = (math.radians(90.0), 0.0, math.atan2(along.y, along.x))
    curve.materials.append(mat)
    coll.objects.link(obj)
    obj.powerpros_role = "stand"
    obj["pp_generated"] = TAG
    obj.powerpros_separate_layers = False
    return obj


def game_distances(project_bin, code):
    """(lf, lcf, cf, rcf, rf) in feet: where the stadium's own field walls (collision surface 9) are, at the
    poles and the left-centre, centre and right-centre bearings, 60 cm up; None where it has none."""
    from . import importer, scratch
    st = scratch._data_stadium(project_bin, code)
    verts = [Vector(importer.to_blender(v)) for v in st.collision_vertices]
    segs = []
    for a, b, c, sid in st.collision_triangles:
        if sid != 9:
            continue
        pts = []
        for p, q in ((verts[a], verts[b]), (verts[b], verts[c]), (verts[c], verts[a])):
            if (p.z - 0.6) * (q.z - 0.6) < 0:
                pts.append((p + (q - p) * ((0.6 - p.z) / (q.z - p.z))).xy)
        if len(pts) == 2:
            segs.append(pts)
    out = []
    for bearing in SECTORS:
        d, best = _at(bearing, 1.0), None
        for p, q in segs:
            e = q - p
            den = d.x * e.y - d.y * e.x
            if abs(den) < 1e-12:
                continue
            t = (p.x * e.y - p.y * e.x) / den
            u = (p.x * d.y - p.y * d.x) / den
            if 0 <= u <= 1 and t > 40.0 and (best is None or t < best):
                best = t
        out.append(None if best is None else round(best / FT))
    return tuple(out)


def remove(coll):
    for obj in [o for o in coll.all_objects if o.get("pp_generated") == TAG]:
        data = obj.data
        bpy.data.objects.remove(obj)
        if data is not None and data.users == 0:
            if isinstance(data, bpy.types.Mesh):
                bpy.data.meshes.remove(data)
            elif isinstance(data, bpy.types.Curve):
                bpy.data.curves.remove(data)


def generate(coll, params):
    """Replace the collection's generated field with a new one. Returns the new objects. With
    params.ground, the ground is that game stadium's own (scratch.add_game_ground_objects): only a grass
    layer under it is made here, for whatever its pieces do not cover."""
    remove(coll)
    from . import scratch
    if params.ground:
        scratch.add_game_ground_objects(coll, params.ground)
    else:
        scratch.remove_game_ground(coll)
    coll["pp_field"] = params.to_json()
    poly, n_out = outline(params)
    # One image per layer: the exporter stacks the ground by image, so layers sharing one would mix.
    grass = _material("pp_field_grass", grass_image(params))
    infield_grass = _material("pp_field_infield_grass", grass_image(params, "pp_field_infield_grass"))
    dirt = _material("pp_field_dirt", dirt_image(params))
    dirt_top = _material("pp_field_dirt_circles", dirt_image(params, "pp_field_dirt_circles"))
    project_bin = coll.get("pp_project_bin", "")
    fence_styles = [game_fence_style(project_bin, code) if code and project_bin else None for code in params.styles()]
    walls = [_styled_material(f"pp_field_wall_{k}", st) if st else _material(f"pp_field_wall_{k}", color=c)
             for k, (c, st) in enumerate(zip(params.colors(), fence_styles))]
    grass_uv = lambda p: _grass_uv(params, p)
    yellow = _material("pp_field_pole", color=(0.95, 0.80, 0.10))
    white = _material("pp_field_sign", color=(0.95, 0.95, 0.95))
    made = []
    max_edge = 2.5 if params.pattern == "circles" else 4.0
    # 1. grass (or turf) over the whole field
    made.append(_object(coll, "Campo · césped", _flat([poly], 0.0, grass_uv, max_edge),
                        grass, "ground"))
    if not params.ground:                   # else the game stadium's own ground lies over the grass
        # 2. warning track along every wall, and the infield's dirt skin
        inner = inset(poly, params.track * FT)
        layer2 = _band(poly, inner, 0.01)
        made.append(_object(coll, "Campo · pista de aviso", layer2, dirt, "ground"))
        second, first, home = Vector((0.0, 90 * FT * math.sqrt(2))), _at(45.0, 90 * FT), Vector((0.0, 0.0))
        third = _at(-45.0, 90 * FT)
        if params.infield in ("grass", "dirt"):
            # the skin: the 95 ft arc round the mound, between the foul lines (15 ft beyond them); behind home
            # plate the foul ground stays grass
            skin = clip(circle(MOUND, 95 * FT, 64), [(Vector((-1.0, 1.0)).normalized(), 15 * FT),
                                                     (Vector((1.0, 1.0)).normalized(), 15 * FT)])
            made.append(_object(coll, "Campo · tierra del cuadro", _flat([skin], 0.01, lambda p: (p.x / 4, p.y / 4)),
                                dirt, "ground"))
        # 3. infield grass: the diamond less the base paths (3 ft each side of the lines)
        if params.infield in ("grass", "turf"):
            centre = Vector((0.0, 90 * FT * math.sqrt(0.5)))
            half = 90 * FT * math.sqrt(0.5) - 3 * FT * math.sqrt(2)
            diamond = [centre + Vector((0.0, -half)), centre + Vector((half, 0.0)), centre + Vector((0.0, half)),
                       centre + Vector((-half, 0.0))]
            made.append(_object(coll, "Campo · césped del cuadro",
                                _flat([diamond], 0.02, grass_uv, max_edge), infield_grass, "ground"))
        # 4. dirt circles: home plate, and the bases on turf
        circles = [circle(home, 13 * FT, 40)]
        if params.infield == "turf":
            circles += [circle(b, 9 * FT, 32) for b in (first, second, third)]
        made.append(_object(coll, "Campo · círculos de tierra",
                            _flat(circles, 0.03, lambda p: (p.x / 4, p.y / 4)), dirt_top, "ground"))
        # 5. chalk on top of everything
        if params.lines:
            made.append(_object(coll, "Campo · líneas", _flat(chalk(params), 0.04, lambda p: (0.5, 0.5)),
                                _material("pp_field_lines", _image("pp_field_lines", np.full((8, 8, 3), 0.96))), "ground"))
    # walls: outfield by sector (its own height and colour; the outline runs from the right pole to the
    # left one, a point every 1.5°, so the sector bounds are outline points), foul walls, poles, signs
    names = (T("izquierdo", "left"), T("izquierdo-central", "left-centre"), T("derecho-central", "right-centre"),
             T("derecho", "right"))
    for k in range(4):
        lo, hi = SECTORS[k], SECTORS[k + 1]
        pts = [p for p in poly[:n_out] if lo - 1e-3 <= math.degrees(math.atan2(p.x, p.y)) <= hi + 1e-3]   # mathutils is single precision
        height = params.heights()[k] * FT
        made.append(_object(coll, f"Campo · valla del jardín ({names[k]})",
                            _wall(pts, height, uv_of=_fence_uv(fence_styles[k], height)), walls[k], "fence"))
    foul = poly[n_out - 1:] + poly[:1]
    looks = list(zip(params.colors(), params.styles()))
    common = max(range(4), key=lambda k: looks.count(looks[k]))       # the foul wall: not the odd one out
    made.append(_object(coll, "Campo · valla de foul",
                        _wall(foul, params.foul_wall * FT, uv_of=_fence_uv(fence_styles[common], params.foul_wall * FT)),
                        walls[common], "fence"))
    for name, p in (("izquierdo", poly[n_out - 1]), ("derecho", poly[0])):
        made.append(_object(coll, f"Campo · poste de foul {name}", _pole(p * 1.004), yellow, "stand"))
    if params.signs:
        for bearing, feet in ((-43.0, params.lf), (-22.5, params.lcf), (0.0, params.cf), (22.5, params.rcf),
                              (43.0, params.rf)):
            made.append(_sign(coll, str(int(round(feet if abs(bearing) != 43.0 else
                                                      wall_radius(params, bearing) / FT))), params, bearing, white))
    return made


# ---------------------------------------------------------------- reference players

PLAYERS = "pp_players"      # coll.children[...][PLAYERS]: the sub-collection of reference players


def players_collection(coll):
    return next((c for c in coll.children if c.get(PLAYERS)), None)


def _figure(p, height=1.8):
    """A plain standing figure (body and head) at p, 1.8 m tall."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.24, radius2=0.2, depth=height - 0.32,
                          matrix=Matrix.Translation((p.x, p.y, (height - 0.32) / 2)))
    bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.14,
                              matrix=Matrix.Translation((p.x, p.y, height - 0.16)))
    return bm


def _locked(obj, sub, mat):
    if mat is not None:
        obj.data.materials.append(mat)
    obj["pp_kind"] = "reference"            # neither exported nor in the collision (scratch.solid_objects)
    sub.objects.link(obj)
    obj.hide_select = True
    return obj


def toggle_players(coll):
    """Put the reference players into coll (where they stand at the start of a play, the dugouts behind
    the foul walls) or take them away. They are never exported. Returns True when they were put."""
    sub = players_collection(coll)
    if sub is not None:
        for obj in list(sub.objects):
            data = obj.data
            bpy.data.objects.remove(obj)
            if data is not None and data.users == 0:
                (bpy.data.meshes if isinstance(data, bpy.types.Mesh) else bpy.data.curves).remove(data)
        bpy.data.collections.remove(sub)
        return False
    params = FieldParams.from_json(coll.get("pp_field", ""))
    sub = bpy.data.collections.new(T("Jugadores (referencia)", "Players (reference)"))
    sub[PLAYERS] = True
    coll.children.link(sub)
    team = _material("pp_ref_player", color=(0.15, 0.35, 0.85))
    others = _material("pp_ref_other", color=(0.12, 0.12, 0.12))
    bench = _material("pp_ref_bench", color=(0.45, 0.30, 0.18))
    label_mat = _material("pp_ref_label", color=(1.0, 1.0, 1.0))

    def label(text, p, z, parent=None):
        curve = bpy.data.curves.new(f"pp_ref_{text}", "FONT")
        curve.body, curve.size = text, 0.7
        curve.align_x, curve.align_y = "CENTER", "CENTER"
        obj = bpy.data.objects.new(f"{T('Ref', 'Ref')} · {text} ·", curve)
        obj.location = (p.x, p.y, z)
        obj.rotation_euler = (math.radians(90.0), 0.0, -math.atan2(p.x, p.y) if p.y > 3.0 else 0.0)   # behind home: read from behind
        _locked(obj, sub, label_mat)
        if parent is not None:              # moves with its dugout
            obj.parent = parent
            obj.matrix_parent_inverse = parent.matrix_world.inverted()

    fielders = {"P", "C", "1B", "2B", "SS", "3B", "LF", "CF", "RF"}
    for text, p in positions(params):
        me = bpy.data.meshes.new(f"pp_ref_{text}")
        bm = _figure(p)
        bm.to_mesh(me)
        bm.free()
        _locked(bpy.data.objects.new(f"{T('Ref', 'Ref')} · {text}", me), sub, team if text in fielders else others)
        label(text, p, 2.4)
    for (text, centre, along), side in zip(benches(params), (1, -1)):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:                  # 18 m long (local X), 2.5 m deep, 1.2 m high, floor at the origin
            v.co = (v.co.x * 18.0, v.co.y * 2.5, (v.co.z + 0.5) * 1.2)
        me = bpy.data.meshes.new(f"pp_ref_{text}")
        bm.to_mesh(me)
        bm.free()
        obj = _locked(bpy.data.objects.new(f"{T('Ref', 'Ref')} · {text}", me), sub, bench)
        obj.location = (centre.x, centre.y, 0.0)
        obj.rotation_euler = (0.0, 0.0, math.atan2(along.y, along.x))
        obj["pp_bench"] = side              # the game's dugout spots go here (scratch.bench_spots)
        obj.hide_select = False             # the one reference the player moves: to their own dugout, floor included
        bpy.context.view_layer.update()
        label(text, centre, 2.0, obj)
    return True
