# SPDX-License-Identifier: GPL-3.0-or-later
"""An Out of the Park Baseball 3D ballpark (.zip, .rar or .obj) into a from-scratch stadium, in one step.

An OOTP park is an .obj + .mtl + textures, in metres, Y up, home plate at the origin; Blender's OBJ
import with forward -Z / up Y gives the game's frame except for the turn: some parks have centre field
straight ahead, others (SketchUp exports) on the diagonal. What this does, so a player only presses
«Importar parque OOTP», «Ver en el juego» and «Exportar mod»:
- a .zip (or .rar/.7z, through tar) is extracted next to itself, the .obj imported into the collection, its images packed into
  the .blend (a park whose textures lived in a deleted folder came back grey);
- turned so centre field is +Y: the direction of the foul poles, else of the field's flat ground;
- the seating OOTP draws from its own `attendance` folder (not in the park's zip) gets those images if
  that folder is next to the park, else a still picture of the game's own people (preload slot 6 of
  the base stadium, from the player's own project.bin) over the seat colour of the material's name;
  other images that are missing are dropped (the face keeps its material colour);
- the flat faces at height 0 go to a ground object (drawn first, bottom-up), nothing is deleted,
  and the collision is generated from what is within FAR of home plate, trimmed to a
  budget the game's tree takes.
"""
import math
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

from . import collision_gen, scratch, stadium_read
from .i18n import T

try:
    from . import packwii, projectbin
except ImportError:
    import packwii
    import projectbin

FAR = 170.0                 # metres: faces farther from home plate take no part in the collision
STAND_BUDGET = 9000         # collision triangles for the stands (loose truss parts can't be decimated)
SNAP_DEGREES = 6.0        # a turn this close to a multiple of 45° is taken as that multiple
CROWD_WORDS = ("attendance", "popularity", "crowd", "seating")
SEAT_COLOURS = (("blue", (0.16, 0.24, 0.55)), ("grey", (0.42, 0.42, 0.44)), ("gray", (0.42, 0.42, 0.44)),
                ("green", (0.13, 0.36, 0.17)), ("yellow", (0.75, 0.62, 0.16)), ("orange", (0.80, 0.38, 0.12)))
SEAT_DEFAULT = (0.55, 0.13, 0.10)       # red, the most common OOTP seating
PEOPLE_X, PEOPLE_Y = 2.0, 1.7           # metres of seating per repeat of the game's crowd picture (4 x 2 people)


class OotpFailure(Exception):
    pass


def _bsdf(mat):
    return next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if mat.use_nodes else None


def _image_node(mat):
    return next((n for n in mat.node_tree.nodes if n.type == "TEX_IMAGE"), None) if mat and mat.use_nodes else None


def _extract(path):
    """The .obj of a park: the file itself, or the biggest .obj of a .zip extracted next to it."""
    path = Path(path)
    if path.suffix.lower() == ".obj":
        return path
    kind = path.suffix.lower()
    if kind not in (".zip", ".rar", ".7z"):
        raise OotpFailure(T("Elige el .zip/.rar del parque o su .obj", "Pick the park's .zip/.rar or its .obj"))
    target = path.with_suffix("")

    def check(names):
        for name in names:
            out = (target / name).resolve()
            if target.resolve() not in out.parents and out != target.resolve():
                raise OotpFailure(T(f"Ruta peligrosa en el archivo: {name}", f"Unsafe path in the archive: {name}"))
    if kind == ".zip":
        with zipfile.ZipFile(path) as z:
            check(i.filename for i in z.infolist())
            z.extractall(target)
    else:
        # ponytail: .rar/.7z through the tar Windows 10+ ships (libarchive); add a library if Linux players need it
        windows = Path(os.environ.get("SystemRoot", "C:/Windows"), "System32", "tar.exe")
        tar = str(windows) if windows.is_file() else shutil.which("tar")    # Git's GNU tar can't read .rar
        listed = subprocess.run([tar, "-tf", str(path)], capture_output=True, text=True) if tar else None
        if listed is None or listed.returncode != 0:
            raise OotpFailure(T(f"No se pudo abrir {path.name}: descomprímelo y elige su .obj",
                                f"Could not open {path.name}: extract it and pick its .obj"))
        check(n for n in listed.stdout.splitlines() if n)
        target.mkdir(exist_ok=True)
        if subprocess.run([tar, "-xf", str(path), "-C", str(target)], capture_output=True).returncode != 0:
            raise OotpFailure(T(f"No se pudo descomprimir {path.name}", f"Could not extract {path.name}"))
    objs = sorted(target.rglob("*.obj"), key=lambda p: p.stat().st_size, reverse=True)
    if not objs:
        raise OotpFailure(T(f"{path.name} no tiene ningún .obj", f"{path.name} has no .obj"))
    return objs[0]


def _turn(objects):
    """Radians to turn about Z so centre field is +Y. A ballpark's field (its flat ground near home plate)
    is symmetric about the line from home plate to centre field: of every direction, the one whose
    mirror maps the field onto itself best, and of its two ends the one the field reaches farthest."""
    from mathutils.kdtree import KDTree
    pts = []
    for o in objects:
        mw = o.matrix_world
        for p in o.data.polygons:
            if abs(p.normal.z) < 0.99:
                continue
            for c in [mw @ p.center] + [mw @ o.data.vertices[i].co for i in p.vertices]:
                if abs(c.z) < 0.15 and 3.0 < c.xy.length < 130.0:
                    pts.append(c.xy.to_3d())
    if len(pts) < 20:
        return 0.0
    pts = pts[::max(1, len(pts) // 3000)]
    tree = KDTree(len(pts))
    for i, v in enumerate(pts):
        tree.insert(v, i)
    tree.balance()

    def asymmetry(a):
        d = Vector((math.sin(a), math.cos(a), 0.0))
        return sum(tree.find(2 * d * d.dot(v) - v)[2] for v in pts)
    best = min((math.radians(k) for k in range(0, 180, 2)), key=asymmetry)
    for step in (1.0, 0.25):
        best = min((best + math.radians(step * k) for k in range(-2, 3)), key=asymmetry)
    d = Vector((math.sin(best), math.cos(best), 0.0))
    if max(d.dot(v) for v in pts) < max(-d.dot(v) for v in pts):
        best += math.pi                       # the outfield is the long end
    best = math.atan2(math.sin(best), math.cos(best))
    # OOTP parks are drawn with centre field on an axis or a diagonal: an uneven field (Montaner came out
    # at 48.8°) gives a few degrees off, so near a multiple of 45° take it.
    snap = math.radians(45) * round(best / math.radians(45))
    return snap if abs(best - snap) < math.radians(SNAP_DEGREES) else best


def _seat_colour(name):
    name = name.lower()
    return next((c for word, c in SEAT_COLOURS if word in name), SEAT_DEFAULT)


def _game_people(project_bin, code):
    """RGBA (h, w, 4) uint8 of the base stadium's lying crowd (preload slot 6), or None."""
    archive = projectbin.Archive(project_bin)
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    if gfx is None or tbl is None:
        return None
    st = stadium_read.read_stadium(gfx, tbl)
    if len(st.preload) < 7 or st.preload[6].texture is None:
        return None
    entry = st.preload[6]
    tex = st.textures[entry.texture]
    pal = st.palettes[entry.palette] if entry.palette is not None else None
    palette = np.frombuffer(pal.colors, ">u2") if pal is not None and pal.colors else None
    return packwii.decode(tex.pixels, tex.width, tex.height, tex.fmt, palette)


def _crowd_image(people, colour, name):
    """The game's people over a seat colour, 4x nearest (they stay sharp), packed into the .blend."""
    seat = np.array([*colour, 1.0], np.float32) * 255.0
    a = people[..., 3:4].astype(np.float32) / 255.0
    rgb = people[..., :3].astype(np.float32) * a + seat[:3] * (1.0 - a)
    out = np.concatenate([rgb, np.full(a.shape, 255.0, np.float32)], axis=2).repeat(4, 0).repeat(4, 1)
    h, w = out.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=False)
    img.pixels.foreach_set((out[::-1] / 255.0).astype(np.float32).ravel())
    img.pack()
    return img


def _attendance_dir(obj_path):
    for d in (obj_path.parent, *obj_path.parents):
        cand = d / "attendance"
        if cand.is_dir():
            return cand
        if d == d.parent:
            break
    return None


def _missing(img):
    return img is None or (not img.packed_file and (img.source != "FILE" or not os.path.exists(bpy.path.abspath(img.filepath)))) \
        or img.size[0] == 0


def _split_off(coll, o, faces, suffix, role):
    """Move those bmesh faces of o to a new object with that role; returns it (or None)."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    keep = {f.index for f in faces(bm)}
    if not keep or len(keep) == len(bm.faces):
        bm.free()
        return None
    part = bm.copy()
    bmesh.ops.delete(part, geom=[f for f in part.faces if f.index not in keep], context="FACES")
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index in keep], context="FACES")
    me = bpy.data.meshes.new(o.name + suffix)
    part.to_mesh(me)
    part.free()
    for m in o.data.materials:
        me.materials.append(m)
    g = bpy.data.objects.new(o.name + suffix, me)
    coll.objects.link(g)
    g.powerpros_role = role
    bm.to_mesh(o.data)
    bm.free()
    return g


def _uv_scale(o):
    """Turn the UVs of o into repeats of the game's crowd picture (8 people per PEOPLE_X x PEOPLE_Y m)."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    uv = bm.loops.layers.uv.active
    if uv is None:
        bm.free()
        return
    uv_area = sum(abs(sum(f.loops[i][uv].uv.x * f.loops[i - 1][uv].uv.y - f.loops[i - 1][uv].uv.x * f.loops[i][uv].uv.y
                          for i in range(len(f.loops)))) / 2 for f in bm.faces)
    m_per_uv = math.sqrt(sum(f.calc_area() for f in bm.faces) / max(uv_area, 1e-9))
    for f in bm.faces:
        for loop in f.loops:
            loop[uv].uv.x *= m_per_uv / PEOPLE_X
            loop[uv].uv.y *= m_per_uv / PEOPLE_Y
    bm.to_mesh(o.data)
    bm.free()


def import_park(context, coll, path, game_crowd=False):
    """Import an OOTP park (.zip or .obj) into the from-scratch collection coll. Returns a report dict."""
    if not scratch.is_scratch(coll):
        raise OotpFailure(T("Primero crea un «Nuevo estadio desde cero»", "First make a «New stadium from scratch»"))
    obj_path = _extract(path)
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=str(obj_path), forward_axis="NEGATIVE_Z", up_axis="Y")
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if not new:
        raise OotpFailure(T("El .obj no tiene mallas", "The .obj has no meshes"))
    for o in new:
        for c in list(o.users_collection):
            c.objects.unlink(o)
        coll.objects.link(o)
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
    angle = _turn(new)
    turn = Matrix.Rotation(angle, 4, "Z")
    for o in new:
        o.data.transform(turn)
        o.data.update()
    coll["pp_ootp_turn"] = round(math.degrees(angle), 1)
    report = {"obj": str(obj_path), "turned": round(math.degrees(angle), 1), "crowd": 0,
              "missing": [], "ground": 0, "collision": 0}
    # Nothing of the model is deleted: what Blender shows is what the game shows (Montaner's valley and
    # town lie below the field and are part of the park).

    # Images: the seating's from OOTP's attendance folder, else the game's people; the rest dropped if missing.
    attendance = _attendance_dir(obj_path)
    people = None
    crowd_mats = set()
    for o in new:
        for m in o.data.materials:
            node = _image_node(m)
            if node is None or m in crowd_mats:
                continue
            img = node.image
            path_name = (bpy.path.abspath(img.filepath) if img else "").lower()
            is_crowd = any(w in m.name.lower() or w in path_name for w in CROWD_WORDS)
            if is_crowd and _missing(img) and attendance is not None and img is not None:
                cand = attendance / Path(img.filepath).name
                if cand.is_file():
                    node.image = bpy.data.images.load(str(cand))
                    continue
            if is_crowd and (_missing(img) or game_crowd):
                if people is None:
                    people = _game_people(coll["pp_project_bin"], coll.get("pp_stadium", "col"))
                if people is not None:
                    node.image = _crowd_image(people, _seat_colour(m.name), "pp_crowd_" + m.name)
                    crowd_mats.add(m)
                    continue
            if _missing(img):
                report["missing"].append(m.name)
                for link in list(m.node_tree.links):
                    if link.from_node == node:
                        m.node_tree.links.remove(link)
                m.node_tree.nodes.remove(node)
    for img in bpy.data.images:
        if img.source == "FILE" and not img.packed_file and not _missing(img):
            img.pack()

    # The game's people repeat every few metres, not over OOTP's 30 m per texture.
    for o in list(new):
        g = _split_off(coll, o, lambda bm: [f for f in bm.faces if o.data.materials[f.material_index] in crowd_mats],
                       "_publico", "stand")
        if g is not None:
            _uv_scale(g)
            report["crowd"] += len(g.data.polygons)
            new.append(g)

    # Flat faces at height 0 (grass, dirt, lines, track) -> the ground object, drawn first in layers.
    for o in list(new):
        g = _split_off(coll, o, lambda bm: [f for f in bm.faces if abs(f.normal.z) > 0.99
                                            and all(abs(v.co.z) < 0.06 for v in f.verts)], "_suelo", "ground")
        if g is not None:
            report["ground"] += len(g.data.polygons)
        if o.powerpros_role == "auto":
            o.powerpros_role = "stand"

    # Collision only from what is near the field (the ball never reaches the city).
    far = []
    for o in [o for o in coll.objects if o.type == "MESH" and not o.get("pp_kind")]:
        g = _split_off(coll, o, lambda bm: [f for f in bm.faces if all(v.co.xy.length > FAR for v in f.verts)],
                       "_lejos", o.powerpros_role)
        if g is not None:
            far.append(g)
    for g in far:
        coll.objects.unlink(g)
    try:
        made = collision_gen.generate(coll) or []
    finally:
        for g in far:
            coll.objects.link(g)
    stands = [c for c in made if c.get("pp_surface") in (11, 13) or "Stand" in c.name or "Grada" in c.name]
    for c in stands:
        bm = bmesh.new(); bm.from_mesh(c.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_area() < 1.0], context="FACES")
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.3)
        bm.to_mesh(c.data); bm.free()
    total = sum(len(c.data.polygons) for c in stands)
    if total > STAND_BUDGET:
        for c in stands:
            mod = c.modifiers.new("dec", "DECIMATE")
            mod.ratio = STAND_BUDGET / total
            dg = context.evaluated_depsgraph_get()
            me = bpy.data.meshes.new_from_object(c.evaluated_get(dg))
            c.modifiers.remove(mod)
            old, c.data = c.data, me
            bpy.data.meshes.remove(old)
    report["collision"] = sum(len(c.data.polygons) for c in made)
    return report

