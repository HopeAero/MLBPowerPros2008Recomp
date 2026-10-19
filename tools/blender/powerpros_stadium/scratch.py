# SPDX-License-Identifier: GPL-3.0-or-later
"""Stadiums from scratch: a collection whose objects carry a role becomes a whole stadium description
(stadium_model.Stadium), written with stadium_write, with no base stadium's pieces (but the game's
bases, plate and mound, which the players stand on).

The collection is in the game's frame of the add-on (metres, Z up, home plate at the origin, centre
field towards +Y) and has pp_scratch, pp_stadium (the data stadium: saves, menus, sky, sounds),
pp_project_bin and pp_game_folder. Objects with the same role (and «hide from above») become one piece,
split at MAX_TEXMAPS textures; pieces go in draw order. docs/stadiums.md, «Estadio desde cero».
"""
import math
import struct
from pathlib import Path

import bpy
from mathutils import Vector

from . import collision_gen, convert, exporter, pj7m_write, stadium_read
from .i18n import T
from .stadium_model import Material, Mesh, Model, Placement, Stadium, TexMap

try:
    from . import projectbin
except ImportError:
    import projectbin

# role -> (placement flags, Spanish label, English label); docs/ingenieria-inversa.md, «Qué hacen los campos».
ROLES = {
    "ground": (0x04810000, "Suelo", "Ground"),
    "stand": (0x00000000, "Grada", "Stand"),
    "fence": (0x00001100, "Valla", "Fence"),
    "transparent": (0x00060000, "Transparente", "Transparent"),
    "additive": (0x000A0800, "Brillo aditivo", "Additive glow"),
    "crowd_lying": (0x00001600, "Público tumbado", "Lying crowd"),
    "crowd_upright": (0x00001700, "Público vertical", "Upright crowd"),
    "flags": (0x00000500, "Banderas", "Flags"),
}
DRAW_ORDER = ["ground", "fence", "stand", "crowd_lying", "crowd_upright", "flags", "transparent", "additive"]
HIDE_ABOVE = 0x80000000                 # NOUPCAM: hidden when the camera is up
MAX_TEXMAPS = exporter.MAX_TEXMAPS
MAX_PIECES = 127                        # one-leaf tree A, which keeps the draw order
# Undecoded fields at their most common value in the game's stadiums (as tools/stadium_minimal.py).
COLORS = ((0, 0, 0, 255), (204, 204, 204, 255), (0, 0, 0, 255))
KIND, UNK_32, PRELOAD_TAIL = 3, 1, 0x6EAC80
GEOMETRY = {"MESH", "CURVE", "SURFACE", "FONT", "META"}
FLAT = 0.2                              # metres from height 0 for «auto» to call an object ground


class ScratchFailure(Exception):
    pass


def register_properties():
    items = [("auto", T("Automático", "Automatic"), T("Suelo si es plano a la altura 0; si no, grada",
                                                      "Ground if flat at height 0, else stand"))]
    items += [(k, T(es, en), "") for k, (_, es, en) in ROLES.items()]
    bpy.types.Object.powerpros_role = bpy.props.EnumProperty(name=T("Papel", "Role"), items=items, default="auto")
    bpy.types.Object.powerpros_hide_above = bpy.props.BoolProperty(
        name=T("Ocultar desde arriba", "Hide from above"), default=False,
        description=T("No se ve con las cámaras altas (edificios de fuera)", "Not seen from high cameras (outside buildings)"))


def unregister_properties():
    del bpy.types.Object.powerpros_role
    del bpy.types.Object.powerpros_hide_above


def is_scratch(coll):
    return coll is not None and bool(coll.get("pp_scratch"))


def role_of(obj):
    role = getattr(obj, "powerpros_role", "auto")
    if role != "auto":
        return role
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    return "ground" if max(abs(p.z) for p in pts) < FLAT else "stand"


def f32(*values):
    return tuple(struct.unpack(f">{len(values)}f", struct.pack(f">{len(values)}f", *values)))


def _evaluated_copies(objects):
    """Temporary mesh objects of the evaluated geometry (modifiers, curves, texts), in place."""
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for o in objects:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
        if not me.polygons:
            bpy.data.meshes.remove(me)
            continue
        tmp = bpy.data.objects.new("pp_tmp_" + o.name, me)
        tmp.matrix_world = o.matrix_world.copy()
        out.append(tmp)
    return out


def _meshes(bm, indices):
    """Mesh descriptions (split by the exporter's limits) for the faces of `bm` with a material in
    `indices` (position in `indices` = the piece's material)."""
    uv_layer, col_layer = bm.loops.layers.uv["UVMap"], bm.loops.layers.float_color["Color"]
    groups = {}
    for f in bm.faces:
        if f.material_index not in indices:
            continue
        g = groups.setdefault(indices.index(f.material_index), {"pos": {}, "nrm": {}, "clr": {}, "uv": {}, "corners": []})
        loops = list(f.loops)
        uvs = [l[uv_layer].uv for l in loops]
        du, dv = math.floor(min(u.x for u in uvs)), math.floor(min(u.y for u in uvs))   # whole repeats off
        n = f32(*exporter.to_game_dir(f.normal))
        for k in range(1, len(loops) - 1):
            for l, uv in ((loops[0], uvs[0]), (loops[k + 1], uvs[k + 1]), (loops[k], uvs[k])):   # GX clockwise
                p = g["pos"].setdefault(f32(*exporter.to_game(l.vert.co)), len(g["pos"]))
                nn = g["nrm"].setdefault(n, len(g["nrm"]))
                c = g["clr"].setdefault(tuple(int(x * 255 + 0.5) for x in l[col_layer]), len(g["clr"]))
                t = g["uv"].setdefault(f32(uv.x - du, 1.0 - (uv.y - dv)), len(g["uv"]))
                g["corners"].append((p, nn, c, t))
    out = []
    for mi, g in sorted(groups.items()):
        md = pj7m_write.MeshData(mi, list(g["pos"]), list(g["nrm"]), list(g["clr"]), list(g["uv"]), g["corners"])
        for part in pj7m_write.split_mesh(md):
            corners = [(0, *c) for c in part.corners]
            draws = [(0x90, corners[i:i + pj7m_write.DRAW_VERTICES])
                     for i in range(0, len(corners), pj7m_write.DRAW_VERTICES)]
            out.append(Mesh(part.material, part.positions, part.normals, part.colors, part.uvs, draws))
    return out


def _placement(model_index, meshes, flags):
    pts = [p for m in meshes for p in m.positions]
    lo = tuple(min(p[k] for p in pts) for k in range(3))
    hi = tuple(max(p[k] for p in pts) for k in range(3))
    centre = tuple((a + b) / 2 for a, b in zip(lo, hi))
    radius = max(math.dist(centre, p) for p in pts)
    return Placement(model_index, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), f32(*lo, *hi),
                     f32(*centre, radius), flags)


def _add_game_bases(st, project_bin, code):
    """The data stadium's BCL pieces (bases, plate, mound, lines) with their textures and palettes."""
    archive = projectbin.Archive(project_bin)
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    if gfx is None or tbl is None:
        raise ScratchFailure(T(f"project.bin no tiene el estadio «{code}»", f"project.bin has no stadium '{code}'"))
    base = stadium_read.read_stadium(gfx, tbl)
    tex_map, pal_map = {}, {}
    for p in base.placements:
        m = base.models[p.model]
        if "BCL" not in m.name:
            continue
        textures = []
        for t, pal, level in m.textures:
            if t not in tex_map:
                tex_map[t] = len(st.textures)
                st.textures.append(base.textures[t])
            if pal is not None and pal not in pal_map:
                pal_map[pal] = len(st.palettes)
                st.palettes.append(base.palettes[pal])
            textures.append((tex_map[t], pal_map.get(pal) if pal is not None else None, level))
        m.textures = textures
        st.models.append(m)
        p.model = len(st.models) - 1
        st.placements.append(p)


def _collision(coll):
    obj = next((o for o in coll.all_objects if o.get("pp_kind") == "collision" and o.type == "MESH"), None)
    if obj is None:
        obj = collision_gen.generate(coll)
    if obj is None:
        raise ScratchFailure(T("No encuentro el campo para la colisión: hace falta suelo a la altura 0 y paredes "
                               "alrededor del plato", "Field not found for the collision: it needs ground at "
                               "height 0 and walls around home plate"))
    return exporter.collision_data(obj)


def build_stadium(coll, game_bases=True, bake_light=True):
    """(Stadium, HD images {file name: image}, warnings) for a from-scratch collection."""
    project_bin = coll.get("pp_project_bin")
    if not project_bin or not Path(project_bin).is_file():
        raise ScratchFailure(T("Esta colección no tiene el project.bin del juego: créala con «Nuevo estadio desde cero»",
                               "This collection has no game project.bin: create it with «New stadium from scratch»"))
    objects = [o for o in coll.all_objects if o.type in GEOMETRY and o.get("pp_kind") != "collision"]
    groups = {}
    for o in objects:
        groups.setdefault((role_of(o), bool(getattr(o, "powerpros_hide_above", False))), []).append(o)
    st = Stadium(unk_20=[], unk_24=[[], []], preload_order=[], preload=[], preload_tail=PRELOAD_TAIL)
    hd_images, warnings, image_index = {}, [], {}
    keys = sorted(groups, key=lambda k: (DRAW_ORDER.index(k[0]), k[1]))
    for key in keys:
        role, hide = key
        temps = _evaluated_copies(groups[key])
        if not temps:
            continue
        try:
            bm, images = convert.prepare(temps, bake_light, separate=role != "ground")
        finally:
            for t in temps:
                me = t.data
                bpy.data.objects.remove(t)
                bpy.data.meshes.remove(me)
        textured = [i for i, img in enumerate(images) if img is not None]
        plain = [i for i, img in enumerate(images) if img is None]
        chunks = [textured[i:i + MAX_TEXMAPS] for i in range(0, len(textured), MAX_TEXMAPS)] or [[]]
        chunks[0] = chunks[0] + plain
        for n, chunk in enumerate(chunks):
            meshes = _meshes(bm, chunk)
            if not meshes:
                continue
            textures, texmaps, materials = [], [], []
            for i in chunk:
                img = images[i]
                if img is None:
                    materials.append(Material(KIND, COLORS, [], UNK_32))
                    continue
                if img.name not in image_index:
                    tex, hd_name, hd_rgba = exporter.image_texture(img, True)
                    image_index[img.name] = len(st.textures)
                    st.textures.append(tex)
                    if hd_name:
                        hd_images[hd_name] = hd_rgba
                textures.append((image_index[img.name], None, 0))
                mi = len(materials)
                reach = [max(1, math.ceil(max((abs(v[k]) for m in meshes if m.material == mi for v in m.uvs), default=1) - 1e-4))
                         for k in (0, 1)]
                texmaps.append(TexMap(len(textures) - 1, *reach))
                materials.append(Material(KIND, COLORS, [len(texmaps) - 1], UNK_32))
            name = f"DP_{role}_{n}" + ("_up" if hide else "")
            st.models.append(Model(name, "_Wmodel_" + name, "p" + name[3:], textures, texmaps, materials, meshes))
            flags = ROLES[role][0] | (HIDE_ABOVE if hide else 0)
            st.placements.append(_placement(len(st.models) - 1, meshes, flags))
        bm.free()
    if not st.models:
        raise ScratchFailure(T("La colección no tiene objetos con caras", "The collection has no objects with faces"))
    if game_bases:
        _add_game_bases(st, project_bin, coll.get("pp_stadium", "col"))
        # Tree A keeps the placement order as the draw order: the bases' marks right after the ground.
        rank = lambda p: 0 if st.models[p.model].name.startswith("DP_ground_") else 1 if "BCL" in st.models[p.model].name else 2
        st.placements.sort(key=rank)
    st.collision_vertices, st.collision_triangles = _collision(coll)
    st.collision_vertices = [f32(*v) for v in st.collision_vertices]
    if len(st.placements) > MAX_PIECES:
        warnings.append(T(f"{len(st.placements)} piezas: con más de {MAX_PIECES} el orden de dibujo no está asegurado",
                          f"{len(st.placements)} pieces: past {MAX_PIECES} the draw order is not guaranteed"))
    return st, hd_images, warnings
