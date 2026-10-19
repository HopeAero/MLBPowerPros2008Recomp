# SPDX-License-Identifier: GPL-3.0-or-later
"""Stadiums from scratch: a collection whose objects carry a role becomes a whole stadium description
(stadium_model.Stadium), written with stadium_write, with no base stadium's pieces (but the game's
bases, plate and mound, which the players stand on).

The collection is in the game's frame of the add-on (metres, Z up, home plate at the origin, centre
field towards +Y) and has pp_scratch, pp_stadium (the data stadium: saves, menus, sky, sounds),
pp_project_bin and pp_game_folder. Objects with the same role (and «hide when batting and from above») become one piece,
split at MAX_TEXMAPS textures, but each flag is a piece of its own (the game's flag plane); pieces go in
draw order. docs/stadiums.md, «Estadio desde cero».
"""
import math
import struct
from contextlib import contextmanager
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

from . import collision_gen, convert, exporter, pj7m_write, stadium_read, surfaces
from .i18n import T
from .stadium_model import Material, Mesh, Model, Placement, PreloadEntry, Stadium, TexMap

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
    "additive": (0x000A0000, "Brillo aditivo", "Additive glow"),     # not 0x0A0800: type 0x08 (lights) is night only
    "crowd_lying": (0x00001600, "Público tumbado", "Lying crowd"),
    "crowd_upright": (0x00001700, "Público vertical", "Upright crowd"),
    "flags": (0x00000500, "Banderas", "Flags"),
}
CROWD_SLOTS = {"crowd_upright": 4, "crowd_lying": 6}   # preload slot whose texture the game animates
CROWD_SIZE = (128, 64)                  # what every game crowd texmap declares (over 8 x 8 textures in two stadiums)
DRAW_ORDER = ["ground", "fence", "stand", "crowd_lying", "crowd_upright", "flags", "transparent", "additive"]
HIDE_ABOVE = 0x80000000                 # NOUPCAM: hidden from the batter's view and high cameras
MAX_TEXMAPS = exporter.MAX_TEXMAPS
MAX_PIECES = 127                        # one-leaf tree A, which keeps the draw order
# Undecoded fields at their most common value in the game's stadiums (as tools/stadium_minimal.py).
COLORS = ((0, 0, 0, 255), (204, 204, 204, 255), (0, 0, 0, 255))
KIND, UNK_32 = 3, 1
GEOMETRY = {"MESH", "CURVE", "SURFACE", "FONT", "META"}
FLAT = 0.2                              # metres from height 0 for «auto» to call an object ground
FLAG_MIN = 1.0                          # decimetres: a flag thinner than this across or tall is refused
NOT_SOLID = ("flags", "transparent", "additive")   # left out of the collision and the menu photo
# The game's flags (DP_FLG_flag*): a 5-column, 2-row plane, pole at x = 0, cloth towards +x, which the
# flag system turns with the wind. Another vertex count in a flag piece makes it write out of memory.
FLAG_COLUMNS = (0.0, 0.2984, 0.5, 0.6903, 1.0)
FLAG_VERTICES = [(1, 0), (0, 0), (0, 1), (2, 1), (2, 0), (1, 1), (4, 0), (3, 1), (4, 1), (3, 0)]   # (column, top)
FLAG_DRAWS = ((0x98, (6, 9, 7, 4, 3, 0, 5, 2)), (0x90, (0, 1, 2, 6, 7, 8)))


class ScratchFailure(Exception):
    pass


def register_properties():
    items = [("auto", T("Automático", "Automatic"), T("Suelo si es plano a la altura 0; si no, grada",
                                                      "Ground if flat at height 0, else stand"))]
    items += [(k, T(es, en), "") for k, (_, es, en) in ROLES.items()]
    bpy.types.Object.powerpros_role = bpy.props.EnumProperty(name=T("Papel", "Role"), items=items, default="auto")
    bpy.types.Object.powerpros_hide_above = bpy.props.BoolProperty(
        name=T("Ocultar en bateo y desde arriba", "Hide when batting and from above"), default=False,
        description=T("No se ve desde la vista del bateador ni desde las cámaras altas, como los edificios de fuera "
                      "del juego (bit NOUPCAM)", "Not seen from the batter's view or from high cameras, like the "
                      "game's outside buildings (NOUPCAM bit)"))


def unregister_properties():
    del bpy.types.Object.powerpros_role
    del bpy.types.Object.powerpros_hide_above


def is_scratch(coll):
    return coll is not None and bool(coll.get("pp_scratch"))


def role_of(obj):
    role = getattr(obj, "powerpros_role", "auto")
    if role != "auto":
        return role
    if obj.type == "MESH" and not obj.modifiers and len(obj.data.vertices):
        # The mesh itself: bound_box is stale after a script edits the mesh, until the next update.
        co = np.empty(len(obj.data.vertices) * 3, np.float32)
        obj.data.vertices.foreach_get("co", co)
        m = np.array(obj.matrix_world, np.float32)
        z = co.reshape(-1, 3) @ m[2, :3] + m[2, 3]
        return "ground" if float(np.abs(z).max()) < FLAT else "stand"
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    return "ground" if max(abs(p.z) for p in pts) < FLAT else "stand"


def f32(*values):
    return tuple(struct.unpack(f">{len(values)}f", struct.pack(f">{len(values)}f", *values)))


@contextmanager
def evaluated(objects):
    """Temporary mesh objects of the evaluated geometry (modifiers, curves, texts), in place; the ones
    with faces. Removed on leaving, also on errors."""
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    try:
        for o in objects:
            me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
            if not me.polygons:
                bpy.data.meshes.remove(me)
                continue
            tmp = bpy.data.objects.new("pp_tmp_" + o.name, me)
            tmp.matrix_world = o.matrix_world.copy()
            out.append(tmp)
        yield out
    finally:
        for t in out:
            me = t.data
            bpy.data.objects.remove(t)
            bpy.data.meshes.remove(me)


def add_game_base_objects(coll):
    """The data stadium's BCL pieces (bases, plate, mound) as locked reference objects (pp_kind
    game_base), so the model can be fitted to them: build_stadium exports the ones still in the
    collection, at the game's own place."""
    from . import importer, pj7m, stadium_tbl
    code = coll.get("pp_stadium", "col")
    archive = projectbin.Archive(coll["pp_project_bin"])
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    if gfx is None or tbl is None:
        raise ScratchFailure(T(f"project.bin no tiene el estadio «{code}»", f"project.bin has no stadium '{code}'"))
    images, materials = {}, {}
    for pl in stadium_tbl.parse_table(tbl).placements:
        model = pj7m.parse_model(gfx, pl.model)
        if "BCL" not in model.name:
            continue
        obj = bpy.data.objects.new(model.name, importer.build_mesh(gfx, model, images, materials))
        obj.matrix_world = importer.placement_matrix(pl)
        obj["pp_kind"] = "game_base"
        obj["pp_model"] = model.name
        obj.lock_location = obj.lock_rotation = obj.lock_scale = (True, True, True)
        coll.objects.link(obj)
    coll["pp_game_bases"] = 1                    # from now on, only the ones left are exported


def _layer_heights(bm):
    """Area-weighted mean height of the faces of each material."""
    acc = {}
    for f in bm.faces:
        a, z = f.calc_area(), f.calc_center_median().z
        s = acc.setdefault(f.material_index, [0.0, 0.0])
        s[0], s[1] = s[0] + a * z, s[1] + a
    return {i: s[0] / s[1] if s[1] else 0.0 for i, s in acc.items()}


def solid_objects(coll):
    """The objects of a from-scratch collection that the collision and the menu photo take."""
    return [o for o in coll.all_objects if o.type in GEOMETRY and o.get("pp_kind") != "collision"
            and role_of(o) not in NOT_SOLID]


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


def _texture(st, img, image_index, hd_images):
    """Index into st.textures of a Blender image, added once."""
    if img.name not in image_index:
        tex, hd_name, hd_rgba = exporter.image_texture(img, True)
        image_index[img.name] = len(st.textures)
        st.textures.append(tex)
        if hd_name:
            hd_images[hd_name] = hd_rgba
    return image_index[img.name]


def _flag_pieces(st, objects, hide, bake_light, image_index, hd_images):
    """One piece per flag object: the game's flag plane, pole at the object's centre, as wide as its
    longest side across and as tall as it, with its first texture over the whole cloth."""
    flags = ROLES["flags"][0] | (HIDE_ABOVE if hide else 0)
    for obj in objects:
        with evaluated([obj]) as temps:
            bm, images = convert.prepare(temps, bake_light) if temps else (None, [])
        pts = [exporter.to_game(v.co) for v in bm.verts] if bm else []
        colour = (255, 255, 255, 255)
        if bm is not None and bm.faces:
            colour = tuple(int(x * 255 + 0.5) for x in next(iter(bm.faces)).loops[0][bm.loops.layers.float_color["Color"]])
        if bm is not None:
            bm.free()
        lo = [min((p[k] for p in pts), default=0.0) for k in range(3)]
        hi = [max((p[k] for p in pts), default=0.0) for k in range(3)]
        centre = f32(*((a + b) / 2 for a, b in zip(lo, hi)))
        w, h = max(hi[0] - lo[0], hi[2] - lo[2]), hi[1] - lo[1]
        if w < FLAG_MIN or h < FLAG_MIN:
            raise ScratchFailure(T(f"La bandera «{obj.name}» no tiene alto o ancho: modélala de pie",
                                   f"The flag '{obj.name}' has no height or width: model it standing up"))
        positions = [f32(FLAG_COLUMNS[c] * w, h / 2 if top else -h / 2, 0.0) for c, top in FLAG_VERTICES]
        uvs = [f32(FLAG_COLUMNS[c], 0.0 if top else 1.0) for c, top in FLAG_VERTICES]
        draws = [(op, [(0, i, 0, 0, i) for i in order]) for op, order in FLAG_DRAWS]
        img = next((i for i in images if i is not None), None)
        textures = [(_texture(st, img, image_index, hd_images), None, 0)] if img else []
        texmaps = [TexMap(0, 1, 1)] if img else []
        name = f"DP_flags_{sum(m.name.startswith('DP_flags_') for m in st.models)}" + ("_up" if hide else "")
        st.models.append(Model(name, "_Wmodel_" + name, "p" + name[3:], textures, texmaps,
                               [Material(KIND, COLORS, [0] if img else [], UNK_32)],
                               [Mesh(0, positions, [(0.0, 0.0, 1.0)], [colour], uvs, draws)]))
        x, y, z = centre
        st.placements.append(Placement(len(st.models) - 1, centre, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0),
                                       f32(x - w, y - h / 2, z - w, x + w, y + h / 2, z + w),
                                       f32(x, y, z, math.hypot(w, h / 2)), flags))


def _data_stadium(project_bin, code):
    archive = projectbin.Archive(project_bin)
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    if gfx is None or tbl is None:
        raise ScratchFailure(T(f"project.bin no tiene el estadio «{code}»", f"project.bin has no stadium '{code}'"))
    return stadium_read.read_stadium(gfx, tbl)


def _copy_texture(st, base, t, pal, maps):
    """(texture index, palette index or None) in st of the data stadium's texture t and palette pal."""
    tex_map, pal_map = maps
    if t not in tex_map:
        tex_map[t] = len(st.textures)
        st.textures.append(base.textures[t])
    if pal is not None and pal not in pal_map:
        pal_map[pal] = len(st.palettes)
        st.palettes.append(base.palettes[pal])
    return tex_map[t], pal_map.get(pal)


def _add_game_bases(st, base, maps, keep=None):
    """The data stadium's BCL pieces (bases, plate, mound, lines) with their textures and palettes;
    with `keep`, only the models named there."""
    for p in base.placements:
        m = base.models[p.model]
        if "BCL" not in m.name or (keep is not None and m.name not in keep):
            continue
        m.textures = [(*_copy_texture(st, base, t, pal, maps), level) for t, pal, level in m.textures]
        st.models.append(m)
        p.model = len(st.models) - 1
        st.placements.append(p)


def _add_preload(st, base, maps):
    """The data stadium's preloaded textures. Always 8, by role: 0-2 scoreboard, 3 video board, 4-5
    upright crowd, 6-7 lying crowd; the crowd pieces draw the game's animated people from slots 4-7,
    whatever their own texture."""
    for e in base.preload:
        t, pal = _copy_texture(st, base, e.texture, e.palette, maps) if e.texture is not None else (None, None)
        st.preload.append(PreloadEntry(t, pal, e.tmem_a, e.tmem_b))
    st.preload_order, st.preload_tail = list(base.preload_order), base.preload_tail


def _collision(coll):
    objects = surfaces.collision_objects(coll) or collision_gen.generate(coll)
    if not objects:
        raise ScratchFailure(T("No encuentro el campo para la colisión: hace falta suelo a la altura 0 y paredes "
                               "alrededor del plato", "Field not found for the collision: it needs ground at "
                               "height 0 and walls around home plate"))
    return surfaces.gather(objects, exporter.to_game)


def build_stadium(coll, game_bases=True, bake_light=True):
    """(Stadium, HD images {file name: image}, warnings) for a from-scratch collection."""
    project_bin = coll.get("pp_project_bin")
    if not project_bin or not Path(project_bin).is_file():
        raise ScratchFailure(T("Esta colección no tiene el project.bin del juego: créala con «Nuevo estadio desde cero»",
                               "This collection has no game project.bin: create it with «New stadium from scratch»"))
    objects = [o for o in coll.all_objects if o.type in GEOMETRY and o.get("pp_kind") not in ("collision", "game_base")]
    groups = {}
    for o in objects:
        groups.setdefault((role_of(o), bool(getattr(o, "powerpros_hide_above", False))), []).append(o)
    st = Stadium(unk_20=[], unk_24=[[], []])
    base, maps = _data_stadium(project_bin, coll.get("pp_stadium", "col")), ({}, {})
    hd_images, warnings, image_index = {}, [], {}
    keys = sorted(groups, key=lambda k: (DRAW_ORDER.index(k[0]), k[1]))
    for key in keys:
        role, hide = key
        if role == "flags":
            _flag_pieces(st, groups[key], hide, bake_light, image_index, hd_images)
            continue
        with evaluated(groups[key]) as temps:
            if not temps:
                continue
            bm, images = convert.prepare(temps, bake_light, separate=role != "ground")
        textured = [i for i, img in enumerate(images) if img is not None]
        plain = [i for i, img in enumerate(images) if img is None]
        order = None
        if role == "ground":
            # The ground layer is drawn without depth test: its layers (base grass, infield dirt over
            # it) must go from the bottom up, in material and in face order.
            heights = _layer_heights(bm)
            order = lambda i: heights.get(i, 0.0)
            textured.sort(key=order)
            bm.faces.sort(key=lambda f: f.calc_center_median().z)
        chunks = [textured[i:i + MAX_TEXMAPS] for i in range(0, len(textured), MAX_TEXMAPS)] or [[]]
        chunks[0] = chunks[0] + plain
        if order is not None:
            chunks[0].sort(key=order)
        slot = CROWD_SLOTS.get(role)
        crowd = base.preload[slot] if slot is not None and slot < len(base.preload) else None
        if crowd is not None and crowd.texture is None:
            crowd = None
        for n, chunk in enumerate(chunks):
            meshes = _meshes(bm, chunk)
            if not meshes:
                continue
            textures, texmaps, materials = [], [], []
            for i in chunk:
                img = images[i]
                if crowd is not None:          # the game's animated people, whatever the object's own texture
                    entry = (*_copy_texture(st, base, crowd.texture, crowd.palette, maps), 0)
                elif img is None:
                    materials.append(Material(KIND, COLORS, [], UNK_32))
                    continue
                else:
                    entry = (_texture(st, img, image_index, hd_images), None, 0)
                if entry not in textures:
                    textures.append(entry)
                mi = len(materials)
                reach = [max(1, math.ceil(max((abs(v[k]) for m in meshes if m.material == mi for v in m.uvs), default=1) - 1e-4))
                         for k in (0, 1)]
                tex = st.textures[entry[0]]
                size = CROWD_SIZE if crowd is not None and (tex.width, tex.height) != CROWD_SIZE else (0, 0)
                texmaps.append(TexMap(textures.index(entry), *reach, *size))
                materials.append(Material(KIND, COLORS, [len(texmaps) - 1], UNK_32))
            name = f"DP_{role}_{n}" + ("_up" if hide else "")
            st.models.append(Model(name, "_Wmodel_" + name, "p" + name[3:], textures, texmaps, materials, meshes))
            flags = ROLES[role][0] | (HIDE_ABOVE if hide else 0)
            st.placements.append(_placement(len(st.models) - 1, meshes, flags))
        bm.free()
    if not st.models:
        raise ScratchFailure(T("La colección no tiene objetos con caras", "The collection has no objects with faces"))
    if game_bases:
        keep = {o.get("pp_model") for o in coll.all_objects if o.get("pp_kind") == "game_base"}             if coll.get("pp_game_bases") else None        # older collections: all of them
        _add_game_bases(st, base, maps, keep)
        # Tree A keeps the placement order as the draw order: the bases' marks right after the ground.
        rank = lambda p: 0 if st.models[p.model].name.startswith("DP_ground_") else 1 if "BCL" in st.models[p.model].name else 2
        st.placements.sort(key=rank)
    _add_preload(st, base, maps)                 # after the models: textures in the order the reader numbers them
    st.collision_vertices, st.collision_triangles = _collision(coll)
    st.collision_vertices = [f32(*v) for v in st.collision_vertices]
    if len(st.placements) > MAX_PIECES:
        warnings.append(T(f"{len(st.placements)} piezas: con más de {MAX_PIECES} el orden de dibujo no está asegurado",
                          f"{len(st.placements)} pieces: past {MAX_PIECES} the draw order is not guaranteed"))
    return st, hd_images, warnings


def export_scratch(coll, mods_folder, mod_name, new_code, new_name, hd=True, menu=True, home_line=""):
    """Write a from-scratch collection as a new-stadium mod: mods/<mod>/project/stadium_<code>.pack and
    its table, textures/stadium_<code>/ (HD), [[stadium]] in mod.toml (base: the data stadium) and,
    with `menu`, its stadium select picture. Errors come as exporter.ExportFailure."""
    from . import menu_pack, menu_render, names, stadium_write, tbl_write
    if not names.valid_code(new_code):
        raise exporter.ExportFailure(T("El código del estadio nuevo debe tener de 2 a 16 letras minúsculas, números o _",
                                       "The new stadium code must have 2 to 16 lowercase letters, digits or _"))
    if new_code in names.CODES or new_code in ("common", "debug", "hanabi") or new_code.startswith("sky_"):
        raise exporter.ExportFailure(T(f"«{new_code}» ya es un estadio del juego: elige otro código",
                                       f"'{new_code}' is already a stadium of the game: pick another code"))
    try:
        st, hd_images, warnings = build_stadium(coll)
        gfx, tbl = stadium_write.write_stadium(st)
    except (ScratchFailure, stadium_write.StadiumWriteError, tbl_write.TreeError) as e:
        raise exporter.ExportFailure(str(e)) from e
    base = coll.get("pp_stadium", "col")
    folder = Path(mods_folder) / mod_name
    (folder / "project").mkdir(parents=True, exist_ok=True)
    (folder / "project" / f"stadium_{new_code}.pack").write_bytes(gfx)
    (folder / "project" / f"stadium_{new_code}tbl.pack").write_bytes(tbl)
    hd_folder = folder / "textures" / f"stadium_{new_code}"
    for old in hd_folder.glob("tex1_*.png") if hd_folder.is_dir() else ():
        old.unlink()
    if hd and hd_images:
        from . import hd_textures
        hd_folder.mkdir(parents=True, exist_ok=True)
        for name, rgba in hd_images.items():
            (hd_folder / name).write_bytes(hd_textures.png_bytes(rgba))
    toml = folder / "mod.toml"
    if not toml.exists():
        description = T("Estadio hecho desde cero con el complemento de Blender", "Stadium made from scratch with the Blender add-on")
        toml.write_text(f'name = "{mod_name}"\ndescription = "{description}"\n', encoding="utf-8")
    exporter.add_stadium_to_toml(toml, new_code, new_name, base)
    if menu and base in menu_pack.CODES:
        base_menu = projectbin.Archive(coll["pp_project_bin"]).read(menu_pack.base_menu_name(base))
        if base_menu:
            (folder / "project" / f"env_stadium_menu_{new_code}.pack").write_bytes(
                menu_pack.build(base_menu, menu_render.photo(coll), menu_render.line(home_line)))
    return {"folder": str(folder), "pieces": len(st.placements), "textures": len(st.textures),
            "hd": len(hd_images) if hd else 0, "collision": len(st.collision_triangles), "size": len(gfx),
            "over_budget": len(gfx) > pj7m_write.BUDGET, "warnings": warnings}
