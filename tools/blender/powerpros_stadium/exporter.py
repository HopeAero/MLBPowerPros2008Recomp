# SPDX-License-Identifier: GPL-3.0-or-later
"""Write an imported (and edited) stadium back as mod packs: mods/<mod>/project/stadium_<code>[tbl].pack.

Only pieces whose mesh, materials or transform changed since the import are rewritten; the rest of
the original packs is kept byte-for-byte. Deleted pieces are emptied (no meshes). Edited or new
images become RGB5A3 textures. An edited collision mesh is written with its surfaces, and the
plane trees of the tbl are rebuilt for what changed. New pieces come in a later phase.
"""
import math
import re
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

from . import fingerprint, hd_textures, importer, names, packwii, pj7m, pj7m_write, stadium_tbl, tbl_write
from .i18n import T

try:
    from . import projectbin
except ImportError:
    import projectbin


class ExportFailure(Exception):
    pass


FIELD_CENTRE = (0.0, 0.0, -450.0)          # game space: 45 m in front of home plate


def to_game(v):
    """Blender metres, Z up -> game decimetres, Y up (inverse of importer.to_blender)."""
    return (10.0 * v[0], 10.0 * v[2], -10.0 * v[1])


def to_game_dir(v):
    return (v[0], v[2], -v[1])


def game_matrix(obj):
    return importer.FROM_BLENDER @ obj.matrix_world @ importer.TO_BLENDER


def image_of(mat):
    if mat is None or mat.node_tree is None:
        return None
    for node in mat.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None:
            return node.image
    return None


MAX_TEXTURE = 256      # the game's stadium textures are 32-256 px: bigger ones (no mipmaps) shimmer far away


def pow2(n):
    return 1 << max(2, min(MAX_TEXTURE.bit_length() - 1, round(math.log2(max(n, 1)))))


class Exporter:
    def __init__(self, gfx, tbl, hd=True):
        self.hd = hd
        self.hd_images = {}         # replacement file name -> (h, w, 4) image
        self.w = pj7m_write.PackWriter(gfx)
        self.tbl = bytearray(tbl) if tbl else None
        self.table = stadium_tbl.parse_table(self.tbl) if tbl else None
        self.textures = {}          # texture offset -> pj7m.Texture (original ones)
        for off in pj7m.find_models(gfx):
            for t in pj7m.parse_model(gfx, off).textures:
                self.textures.setdefault(t.offset, t)
        self.new_textures = {}      # image name -> texture offset
        self._image_checks = {}     # image name -> pixels differ from the import
        self.stats = {"pieces": 0, "emptied": 0, "moved": 0, "textures": 0, "collision": 0}

    # ---- textures ------------------------------------------------------------------------------

    def texture_for(self, img):
        """(texture offset, palette offset) for an image: the original if unchanged, else a new one."""
        if img is None:
            return None, None
        off = img.get("pp_texture_offset")
        if off is not None and img.get("pp_pixels_hash") == fingerprint.image_hash(img) and off in self.textures:
            return off, self._palette_header(off)
        if img.name not in self.new_textures:
            w, h = img.size
            if w == 0 or h == 0:
                raise ExportFailure(T(f"La imagen {img.name} está vacía", f"The image {img.name} is empty"))
            px = np.empty(w * h * 4, np.float32)
            img.pixels.foreach_get(px)
            full = (px.reshape(h, w, 4)[::-1] * 255 + 0.5).astype(np.uint8)        # top row first
            rgba = full
            tw, th = pow2(w), pow2(h)
            if (tw, th) != (w, h):
                ys = (np.arange(th) * h // th)
                xs = (np.arange(tw) * w // tw)
                rgba = rgba[ys][:, xs]
            texels = packwii.tile(packwii.to_rgb5a3(rgba), 5).astype(">u2").tobytes()
            self.new_textures[img.name] = self.w.add_texture_rgb5a3(texels, tw, th)
            self.stats["textures"] += 1
            if self.hd:          # full size and 8 bits per channel, keyed to the small RGB5A3 texture
                self.hd_images[hd_textures.replacement_name(texels, tw, th)] = hd_textures.fit(full)
        return self.new_textures[img.name], None

    def image_changed(self, img):
        if img is None or "pp_pixels_hash" not in img:
            return img is not None                     # a new image counts as a change
        if img.name not in self._image_checks:
            self._image_checks[img.name] = img["pp_pixels_hash"] != fingerprint.image_hash(img)
        return self._image_checks[img.name]

    def _palette_header(self, texture_offset):
        """The palette header an original model uses with this texture (palette tables are per model)."""
        if not hasattr(self, "_palettes"):
            self._palettes = {}
            d = self.w.d
            for off in pj7m.find_models(d):
                root = pj7m.ptr(d, off + 0x0C)
                textab, paltab = pj7m.ptr(d, root + 8), pj7m.ptr(d, root + 12)
                for i in range(pj7m.u32(d, off + 0x18)):
                    t, p = pj7m.ptr(d, textab + 8 * i), pj7m.ptr(d, paltab + 8 * i)
                    if p is not None:
                        self._palettes.setdefault(t, p)
        return self._palettes.get(texture_offset)

    # ---- materials -----------------------------------------------------------------------------

    def local_material(self, model, mat, cache):
        """Index of a material of this model matching the Blender material, adding one if needed."""
        img = image_of(mat)
        tex, pal = self.texture_for(img)
        raw = bytes.fromhex(mat["pp_material"]) if mat is not None and "pp_material" in mat else None
        key = (raw, tex)
        if key in cache:
            return cache[key]
        # Blender materials are shared between pieces at import, so the same texture is enough:
        # prefer the identical material, else any of this model's materials with that texture.
        candidates = []
        for i, m in enumerate(model.materials):
            mt = model.textures[model.texmaps[m.texmaps[0]].texture].offset if m.texmaps else None
            if mt == tex:
                candidates.append((m.raw != raw, i))
        if candidates and tex is not None:
            cache[key] = min(candidates)[1]
            return cache[key]
        if tex is None:                                        # untextured: plain white texture
            white = np.full((4, 4, 4), 255, np.uint8)
            tex = self.w.add_texture_rgb5a3(packwii.tile(packwii.to_rgb5a3(white), 5).astype(">u2").tobytes(), 4, 4)
        template = raw or model.materials[0].raw
        cache[key] = self.w.add_material(model, template, tex, pal)
        return cache[key]

    # ---- pieces --------------------------------------------------------------------------------

    def mesh_groups(self, obj, model):
        depsgraph = bpy.context.evaluated_depsgraph_get()
        ev = obj.evaluated_get(depsgraph)
        me = ev.to_mesh()
        try:
            me.calc_loop_triangles()
            normals = np.empty(len(me.loops) * 3, np.float32)
            if hasattr(me, "corner_normals"):                  # Blender 4.1+
                me.corner_normals.foreach_get("vector", normals)
            else:
                me.loops.foreach_get("normal", normals)
            normals = normals.reshape(-1, 3)
            uv = me.uv_layers.active
            uvs = np.zeros((len(me.loops), 2), np.float32)
            if uv is not None:
                uv.data.foreach_get("uv", uvs.reshape(-1))
            col = me.color_attributes.get("Color")
            colors = np.full((len(me.loops), 4), 1.0, np.float32)
            if col is not None:
                raw = np.empty(len(col.data) * 4, np.float32)
                col.data.foreach_get("color", raw)
                raw = raw.reshape(-1, 4)
                if col.domain == "CORNER":
                    colors = raw
                else:
                    vi = np.empty(len(me.loops), np.int32)
                    me.loops.foreach_get("vertex_index", vi)
                    colors = raw[vi]
            co = np.empty(len(me.vertices) * 3, np.float32)
            me.vertices.foreach_get("co", co)
            co = co.reshape(-1, 3)

            cache, groups = {}, {}
            for lt in me.loop_triangles:
                slot = obj.material_slots[lt.material_index].material if lt.material_index < len(obj.material_slots) else None
                mi = self.local_material(model, slot, cache)
                g = groups.setdefault(mi, {"pos": {}, "nrm": {}, "clr": {}, "uv": {}, "corners": []})
                v0, v1, v2 = lt.vertices
                l0, l1, l2 = lt.loops
                for v, l in ((v0, l0), (v2, l2), (v1, l1)):             # back to GX clockwise order
                    p = g["pos"].setdefault(tuple(co[v]), len(g["pos"]))     # same place, one position
                    n = g["nrm"].setdefault(tuple(np.round(normals[l], 4)), len(g["nrm"]))
                    c = g["clr"].setdefault(tuple(int(x * 255 + 0.5) for x in colors[l]), len(g["clr"]))
                    t = g["uv"].setdefault((round(float(uvs[l][0]), 5), round(1.0 - float(uvs[l][1]), 5)), len(g["uv"]))
                    g["corners"].append((p, n, c, t))
            out = []
            for mi, g in groups.items():
                out.append(pj7m_write.MeshData(
                    mi, [to_game(v) for v in g["pos"]], [to_game_dir(n) for n in g["nrm"]],
                    list(g["clr"]), list(g["uv"]), g["corners"]))
            return out
        finally:
            ev.to_mesh_clear()

    def placement(self, obj, record, local_points):
        mg = game_matrix(obj)
        loc, rot, scale = mg.decompose()
        euler = rot.to_euler("XYZ")
        pts = [mg @ Vector(p) for p in local_points]
        if not pts:
            return
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
        centre = Vector([(lo[i] + hi[i]) / 2 for i in range(3)])
        radius = max((p - centre).length for p in pts)
        pj7m_write.set_placement(self.tbl, record.offset, tuple(loc), tuple(euler), tuple(scale),
                                 (*lo, *hi), (*centre, radius))
        self.stats["moved"] += 1

    def export(self, coll):
        present = {o.get("pp_model_offset"): o for o in coll.all_objects if o.get("pp_kind") == "model"}
        records = {p.model: p for p in self.table.placements} if self.table else {}
        for off in pj7m.find_models(bytes(self.w.d)):        # the original models; appended data has no tags
            model = pj7m.parse_model(self.w.d, off)
            obj = present.get(off)
            if obj is None:
                self.w.set_meshes(model, [])
                self.stats["emptied"] += 1
                continue
            mesh_changed = (obj.get("pp_signature") != fingerprint.mesh_signature(obj) or bool(obj.modifiers)
                            or any(self.image_changed(image_of(s.material)) for s in obj.material_slots))
            moved = obj.get("pp_matrix") != fingerprint.matrix_signature(obj)
            if not (mesh_changed or moved):
                continue
            if mesh_changed:
                meshes = self.mesh_groups(obj, model)
                self.w.set_meshes(pj7m.parse_model(self.w.d, off), meshes, FIELD_CENTRE)
                self.stats["pieces"] += 1
                points = [p for m in meshes for p in m.positions]
            else:
                points = [to_game(v.co) for v in obj.data.vertices]
            if off in records and self.tbl is not None:
                self.placement(obj, records[off], points)
        self.w.d.extend(b"\0" * (-len(self.w.d) % 32))          # original packs end 32-byte aligned
        if self.tbl is None:
            return bytes(self.w.d), None
        collision = next((o for o in coll.all_objects if o.get("pp_kind") == "collision"), None)
        changed = collision is not None and collision.get("pp_signature") != fingerprint.collision_signature(collision)
        if not (self.stats["moved"] or changed):
            return bytes(self.w.d), bytes(self.tbl)
        tw = tbl_write.TblWriter(self.tbl)
        tw.set_model_tree()                       # the boxes of the pieces changed: tree A again
        if changed:
            vertices, triangles = collision_data(collision)
            tw.set_collision(vertices, triangles)
            self.stats["collision"] = len(triangles)
        return bytes(self.w.d), tw.finish()


def surface_of(mat, fallback):
    """Surface id of a collision face: the number its material name ends with («Superficie 9»)."""
    m = re.search(r"(\d+)\s*$", mat.name) if mat is not None else None
    return int(m.group(1)) if m else fallback


def collision_data(obj):
    """(game-space vertices, triangles (a, b, c, surface)) from the collision object."""
    me = obj.data
    me.calc_loop_triangles()
    attr = me.attributes.get("surface_id")
    ids = None
    if attr is not None and attr.domain == "FACE" and attr.data_type == "INT":
        ids = np.empty(len(me.polygons), np.int32)
        attr.data.foreach_get("value", ids)
    mw = obj.matrix_world
    vertices = [to_game(mw @ v.co) for v in me.vertices]
    triangles = []
    for lt in me.loop_triangles:
        mat = me.materials[lt.material_index] if lt.material_index < len(me.materials) else None
        sid = surface_of(mat, int(ids[lt.polygon_index]) if ids is not None else 11)
        a, b, c = lt.vertices
        triangles.append((a, b, c, sid))       # normal (b-a)x(c-a): the side the ball hits
    return vertices, triangles


def valid_code(code):
    return 2 <= len(code) <= 16 and all(c.isascii() and (c.isalnum() or c == "_") for c in code) and code.islower()


def add_stadium_to_toml(toml, code, name, base):
    """Append a [[stadium]] entry to mod.toml unless that code is already there."""
    text = toml.read_text(encoding="utf-8") if toml.exists() else ""
    if f'code = "{code}"' in text:
        return
    safe = name.replace("\\", "").replace('"', "'")
    block = f'\n[[stadium]]\ncode = "{code}"\nname = "{safe}"\nbase = "{base}"\n'
    toml.write_text(text.rstrip("\n") + "\n" + block, encoding="utf-8")


def export_stadium(coll, mods_folder, mod_name, target_code, new_name=None, hd=True):
    """Write the mod. target_code: the stadium the packs replace, or with new_name the code of a
    stadium the mod adds (files stadium_<code>.pack/tbl.pack, [[stadium]] in mod.toml, based on the
    imported one; the runtime shows it as an extra entry of the stadium select carousel).
    hd: also write each new image at full size as a replacement texture (hd_textures)."""
    code = coll.get("pp_stadium")
    project_bin = coll.get("pp_project_bin")
    if not code or not project_bin or not Path(project_bin).is_file():
        raise ExportFailure(T("Esta colección no es un estadio importado (o ya no encuentro su project.bin)",
                              "This collection is not an imported stadium (or its project.bin is gone)"))
    if new_name is not None:
        if not valid_code(target_code):
            raise ExportFailure(T("El código del estadio nuevo debe tener de 2 a 16 letras minúsculas, números o _",
                                  "The new stadium code must have 2 to 16 lowercase letters, digits or _"))
        if target_code in names.CODES or target_code in ("common", "debug", "hanabi") or target_code.startswith("sky_"):
            raise ExportFailure(T(f"«{target_code}» ya es un estadio del juego: elige otro código",
                                  f"'{target_code}' is already a stadium of the game: pick another code"))
    archive = projectbin.Archive(project_bin)
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    ex = Exporter(gfx, tbl, hd)
    gfx_out, tbl_out = ex.export(coll)
    folder = Path(mods_folder) / mod_name
    (folder / "project").mkdir(parents=True, exist_ok=True)
    (folder / "project" / f"stadium_{target_code}.pack").write_bytes(gfx_out)
    if tbl_out is not None:
        (folder / "project" / f"stadium_{target_code}tbl.pack").write_bytes(tbl_out)
    # This folder belongs to the export: images from an earlier export of the same stadium go first.
    hd_folder = folder / "textures" / f"stadium_{target_code}"
    for old in hd_folder.glob("tex1_*.png") if hd_folder.is_dir() else ():
        old.unlink()
    if ex.hd_images:
        hd_folder.mkdir(parents=True, exist_ok=True)
        for name, rgba in ex.hd_images.items():
            (hd_folder / name).write_bytes(hd_textures.png_bytes(rgba))
    ex.stats["hd"] = len(ex.hd_images)
    toml = folder / "mod.toml"
    if not toml.exists():
        description = T("Estadio hecho con el complemento de Blender", "Stadium made with the Blender add-on")
        toml.write_text(f'name = "{mod_name}"\ndescription = "{description}"\n', encoding="utf-8")
    if new_name is not None:
        add_stadium_to_toml(toml, target_code, new_name, code)
    ex.stats["size"] = len(gfx_out)
    ex.stats["over_budget"] = len(gfx_out) > pj7m_write.BUDGET
    ex.stats["folder"] = str(folder)
    return ex.stats
