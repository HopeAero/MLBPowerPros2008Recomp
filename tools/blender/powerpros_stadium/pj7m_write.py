# SPDX-License-Identifier: GPL-3.0-or-later
"""Write edited PJ7M models back into a stadium pack (append and repoint).

The original pack stays byte-for-byte as it is; new display lists, vertex arrays, material
tables and textures go at the end, 32-byte aligned, and the model's structures are repointed
to them. Checked in the game (docs/ingenieria-inversa.md): appended data with fresh
0x40000000 pointers loads, and a graphics pack of 21 MB still loads in a match (31.5 MB does not).
"""
import struct
from dataclasses import dataclass, field

from . import pj7m

P = 0x40000000
BUDGET = 80 * 1024 * 1024          # graphics pack size known to load in a match (recomp, patch wiicompiled-mlb-mem2-heap)
MAX_INDEX = 0xFFFF
# The game's own stadium meshes never exceed 24,768 display-list bytes, 573 vertices per draw and
# 1,303 positions; a mesh far beyond that (500 KB, 58,000 vertices in one draw) is only partly
# drawn in a match. Bigger meshes are split into several meshes of the same material.
MESH_TRIANGLES = 600               # 16 KB of display list
MESH_POSITIONS = 1300
DRAW_VERTICES = 3 * 190


@dataclass
class MeshData:
    """One material's geometry in game space. corners: triangles as (pos, nrm, clr, tex) index
    tuples, three per triangle, in GX (clockwise) order."""
    material: int
    positions: list
    normals: list
    colors: list
    uvs: list
    corners: list = field(default_factory=list)


class PackWriter:
    def __init__(self, data):
        self.d = bytearray(data)

    def append(self, blob, align=32):
        self.d.extend(b"\0" * (-len(self.d) % align))
        off = len(self.d)
        self.d.extend(blob)
        return off

    def u32(self, off, value):
        struct.pack_into(">I", self.d, off, value)

    # ---- textures ------------------------------------------------------------------------------

    def add_texture_rgb5a3(self, pixels_be, width, height):
        """Texture header (model layout) for already tiled RGB5A3 texels; returns its offset."""
        pix = self.append(pixels_be)
        head = struct.pack(">HHBBH", height, width, 0x81, 0, 0x6000) + struct.pack(">I", P | pix)
        head += struct.pack(">4I", *[0x82000000] * 4) + b"\0" * 8
        return self.append(head, 4)

    # ---- materials -----------------------------------------------------------------------------

    def _pointers(self, model):
        d = self.d
        root = pj7m.ptr(d, model.offset + 0x0C)
        scene = pj7m.ptr(d, root + 4)
        return root, scene

    def add_material(self, model, template_raw, texture_offset, palette_offset=None, texmap_raw=None):
        """Give the model one more material (and texmap/texture slot) using a texture anywhere in
        the pack. Returns the new local material index. Tables are copied, extended and repointed."""
        d = self.d
        root, scene = self._pointers(model)
        ntex = pj7m.u32(d, model.offset + 0x18)
        textab, paltab = pj7m.ptr(d, root + 8), pj7m.ptr(d, root + 12)
        # Both tables end with an 8-byte zero entry: the game walks them up to it (moving a table
        # without it turns the model's paletted textures white in a match).
        tex_entries = bytes(d[textab:textab + 8 * ntex]) + struct.pack(">II", P | texture_offset, 0) + bytes(8)
        pal_entries = bytes(d[paltab:paltab + 8 * ntex]) + struct.pack(">II", (P | palette_offset) if palette_offset else 0, 0) + bytes(8)
        self.u32(root + 8, P | self.append(tex_entries, 8))
        self.u32(root + 12, P | self.append(pal_entries, 8))
        self.u32(model.offset + 0x18, ntex + 1)

        ntm, tm_off = pj7m.u32(d, scene + 0x10), pj7m.ptr(d, scene + 0x14)
        w, h = pj7m.u16(d, texture_offset + 2), pj7m.u16(d, texture_offset)
        tm = bytearray(texmap_raw or d[tm_off:tm_off + 0x4C])
        struct.pack_into(">H", tm, 0, ntex)
        struct.pack_into(">HH", tm, 0x0C, w - 1, h - 1)
        self.u32(scene + 0x10, ntm + 1)
        self.u32(scene + 0x14, P | self.append(bytes(d[tm_off:tm_off + 0x4C * ntm]) + tm, 4))

        nmat, mat_off = pj7m.u32(d, scene + 0x18), pj7m.ptr(d, scene + 0x1C)
        mat = bytearray(template_raw)
        struct.pack_into(">H", mat, 2, ntm)
        struct.pack_into(">8H", mat, 0x1C, ntm, *[0xFFFF] * 7)
        self.u32(scene + 0x18, nmat + 1)
        self.u32(scene + 0x1C, P | self.append(bytes(d[mat_off:mat_off + 0x34 * nmat]) + mat, 4))
        return nmat

    # ---- geometry ------------------------------------------------------------------------------

    def set_meshes(self, model, meshes, centre=None):
        """Replace all meshes of a model. Returns (positions, triangles) written. With `centre`
        (game space, a point of the field) the triangles are written in painter's order."""
        d = self.d
        root, scene = self._pointers(model)
        node = pj7m.ptr(d, scene + 4)
        entries, npos, ntri = b"", 0, 0
        if centre is None:
            meshes = [part for m in meshes for part in split_mesh(m)]
        else:
            meshes = painter_order(meshes, centre)
        for m in meshes:
            for name, values in (("positions", m.positions), ("normals", m.normals),
                                 ("colors", m.colors), ("uvs", m.uvs)):
                if len(values) > MAX_INDEX + 1:
                    raise ValueError(f"{model.name}: {len(values)} {name} in one material (max {MAX_INDEX + 1})")
            dl = bytearray()
            for i in range(0, len(m.corners), DRAW_VERTICES):
                chunk = m.corners[i:i + DRAW_VERTICES]
                dl += struct.pack(">BH", 0x90, len(chunk))
                for p, n, c, t in chunk:
                    dl += struct.pack(">BHHHH", 0, p, n, c, t)
            dl += b"\0" * (-len(dl) % 32)
            dl_off = self.append(dl)
            pos = self.append(b"".join(struct.pack(">3f", *v) for v in m.positions))
            nrm = self.append(b"".join(struct.pack(">3f", *v) for v in m.normals))
            clr = self.append(bytes(c for v in m.colors for c in v))
            tex = self.append(b"".join(struct.pack(">2f", *v) for v in m.uvs))
            mesh = self.append(struct.pack(">7I", 0, len(dl), P | dl_off, P | pos, P | nrm, P | clr, P | tex), 4)
            entries += struct.pack(">HHI", m.material, 1, P | mesh)
            npos += len(m.positions)
            ntri += len(m.corners) // 3
        struct.pack_into(">H", d, node, len(meshes))
        self.u32(node + 4, (P | self.append(entries, 4)) if entries else 0)
        self.u32(model.offset + 0x1C, npos)
        self.u32(model.offset + 0x20, ntri)
        return npos, ntri


def painter_key(positions, tri, centre):
    """Draw order for surfaces the depth buffer cannot separate (coplanar or a few cm apart at
    stadium distances): field-level floor first, lowest first (lines over dirt over grass), then
    the rest from far to near the field, the side facing the field last."""
    pts = [positions[c[0]] for c in tri]
    y = sum(p[1] for p in pts) / 3
    if max(p[1] for p in pts) < 10.0:                       # within 1 m of the ground (decimetres)
        return (0, y)
    dx = sum(p[0] for p in pts) / 3 - centre[0]
    dz = sum(p[2] for p in pts) / 3 - centre[2]
    # Same place (the two sides of a wall): the side facing the field last. Visible side of a GX
    # (clockwise) triangle: -(b - a) x (c - a).
    u = [pts[1][k] - pts[0][k] for k in range(3)]
    v = [pts[2][k] - pts[0][k] for k in range(3)]
    nx, nz = -(u[1] * v[2] - u[2] * v[1]), -(u[0] * v[1] - u[1] * v[0])
    facing = 1 if nx * -dx + nz * -dz > 0 else 0
    return (1, -round((dx * dx + dz * dz) ** 0.5, 2), facing)


def painter_order(meshes, centre):
    """Split meshes and order every triangle and part by painter_key."""
    parts = []
    for m in meshes:
        tris = [m.corners[i:i + 3] for i in range(0, len(m.corners), 3)]
        keys = [painter_key(m.positions, t, centre) for t in tris]
        order = sorted(range(len(tris)), key=keys.__getitem__)
        sorted_mesh = MeshData(m.material, m.positions, m.normals, m.colors, m.uvs,
                               [c for i in order for c in tris[i]])
        for part in split_mesh(sorted_mesh):
            parts.append((painter_key(part.positions, part.corners[:3], centre), part))
    parts.sort(key=lambda kp: kp[0])
    return [p for _, p in parts]


def split_mesh(m, max_triangles=MESH_TRIANGLES, max_positions=MESH_POSITIONS):
    """MeshData within the game's sizes: consecutive triangles, each part with its own arrays."""
    if len(m.corners) // 3 <= max_triangles and len(m.positions) <= max_positions:
        return [m]
    parts, part, maps = [], None, None
    for t in range(0, len(m.corners), 3):
        tri = m.corners[t:t + 3]
        new_pos = len({c[0] for c in tri} - maps[0].keys()) if part else 3
        if part is None or len(part.corners) // 3 >= max_triangles or len(part.positions) + new_pos > max_positions:
            part, maps = MeshData(m.material, [], [], [], []), ({}, {}, {}, {})
            parts.append(part)
        for c in tri:
            idx = []
            for k, (src, dst) in enumerate(((m.positions, part.positions), (m.normals, part.normals),
                                            (m.colors, part.colors), (m.uvs, part.uvs))):
                if c[k] not in maps[k]:
                    maps[k][c[k]] = len(dst)
                    dst.append(src[c[k]])
                idx.append(maps[k][c[k]])
            part.corners.append(tuple(idx))
    return parts


def set_placement(table, record_offset, translation, rotation, scale, bbox, sphere):
    """Rewrite one tbl placement record in place (bytearray), keeping its model pointer and flags."""
    struct.pack_into(">15f", table, record_offset + 4, *translation, *rotation, *scale, *bbox)
    struct.pack_into(">4f", table, record_offset + 0x40, *sphere)
