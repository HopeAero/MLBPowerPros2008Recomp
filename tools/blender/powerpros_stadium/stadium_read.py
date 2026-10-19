# SPDX-License-Identifier: GPL-3.0-or-later
"""Read a game stadium (stadium_<code>.pack and stadium_<code>tbl.pack) into a stadium_model.Stadium.

With `spans`, every structure read is also recorded as a stadium_model.Span (spans["gfx"] and
spans["tbl"]): the tests use them to prove every byte is understood and to compare a rewritten pack
with the original, structure by structure. The writer never sees them.
"""
import struct

from . import pj7m, stadium_tbl
from .stadium_model import (Material, Mesh, Model, Palette, Placement, PointGroup, PreloadEntry, Span, Stadium,
                            TexMap, Texture)

BITS = {5: 16, 6: 32, 8: 4, 9: 8}                    # GX format -> bits per texel
TYPE_FMT = pj7m.MODEL_FMT                            # texture type byte -> GX format


class ReadError(ValueError):
    pass


def u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def floats(d, o, n):
    return struct.unpack_from(f">{n}f", d, o)


def cstring_end(d, o):
    return d.index(b"\0", o) + 1


class _Reader:
    def __init__(self, gfx, tbl):
        self.gfx, self.tbl = gfx, tbl
        self.spans = {"gfx": [], "tbl": []}
        self.st = Stadium()
        self.texture_index, self.palette_index = {}, {}

    def span(self, where, kind, key, start, end, pointers=()):
        self.spans[where].append(Span(kind, key, start, end, tuple(pointers)))

    # ---- textures ------------------------------------------------------------------------------

    def texture(self, head):
        """Index of the texture whose header is at `head` in the graphics pack (None for None)."""
        if head is None:
            return None
        if head not in self.texture_index:
            d = self.gfx
            h, w, kind = u16(d, head), u16(d, head + 2), d[head + 4]
            if kind not in TYPE_FMT:
                raise ReadError(f"texture 0x{head:X}: unknown type 0x{kind:02X}")
            fmt = TYPE_FMT[kind]
            pix = pj7m.ptr(d, head + 8)
            size = w * h * BITS[fmt] // 8
            i = len(self.st.textures)
            self.st.textures.append(Texture(fmt, w, h, bytes(d[pix:pix + size])))
            self.texture_index[head] = i
            self.span("gfx", "texture_header", (i,), head, head + 0x20, (8,))
            self.span("gfx", "texture_pixels", (i,), pix, pix + size)
        return self.texture_index[head]

    def spare_textures(self):
        """Texture headers (with the palette header at +0x24 for C4/C8) that no model or preload entry
        points at: found by their signature, kept so nothing of the pack is dropped."""
        d = self.gfx
        for head in range(0x20, len(d) - 0x30, 4):
            if (head in self.texture_index or d[head + 4] not in TYPE_FMT or d[head + 5]
                    or any(u32(d, head + 0x0C + 4 * k) != 0x82000000 for k in range(4))
                    or pj7m.ptr(d, head + 8) is None):
                continue
            tex = self.texture(head)
            pal = head + 0x24
            fmt = self.st.textures[tex].fmt
            placeholder = u16(d, pal) == 0xFFFF and u32(d, pal + 8) == 0
            palette = (self.palette(pal) if d[pal + 4] == 0x81 and u16(d, pal + 2) == 0 and u16(d, pal + 6) in (0x6000, 0x4000)
                       and (placeholder or (fmt in (8, 9) and pj7m.ptr(d, pal + 8) is not None)) else None)
            self.st.spare_textures.append((tex, palette))

    def texture_tails(self):
        """Non-zero bytes between a texture's pixels and the next structure go to Texture.tail."""
        starts = sorted(s.start for s in self.spans["gfx"])
        for s in [s for s in self.spans["gfx"] if s.kind == "texture_pixels"]:
            nxt = next((x for x in starts if x >= s.end), len(self.gfx))
            tail = self.gfx[s.end:nxt].rstrip(b"\0")
            if tail:
                self.st.textures[s.key[0]].tail = tail
                self.span("gfx", "texture_tail", s.key, s.end, s.end + len(tail))

    def palette(self, head):
        if head is None:
            return None
        if head not in self.palette_index:
            d = self.gfx
            entries, colors = u16(d, head), pj7m.ptr(d, head + 8)
            data = bytes(d[colors:colors + 2 * entries]) if colors is not None else b""
            i = len(self.st.palettes)
            self.st.palettes.append(Palette(entries, data, u16(d, head + 6)))
            self.palette_index[head] = i
            self.span("gfx", "palette_header", (i,), head, head + 0x0C, (8,))
            if colors is not None:
                self.span("gfx", "palette_colors", (i,), colors, colors + 2 * entries)
        return self.palette_index[head]

    # ---- models --------------------------------------------------------------------------------

    def model(self, mi, off):
        d = self.gfx
        m = pj7m.parse_model(d, off)
        root = pj7m.ptr(d, off + 0x0C)
        if any(u32(d, root + 0x10 + 4 * k) for k in range(12)):
            raise ReadError(f"{m.name}: root fields +0x10..+0x3C in use (only stadium_common has them)")
        scene, textab, paltab = pj7m.ptr(d, root + 4), pj7m.ptr(d, root + 8), pj7m.ptr(d, root + 12)
        node = pj7m.ptr(d, scene + 4)
        q = pj7m.ptr(d, scene + 0x0C)
        word_at = pj7m.ptr(d, q)
        name_at, wname_at, node_name_at = pj7m.ptr(d, off + 8), pj7m.ptr(d, root + 0x40), pj7m.ptr(d, node + 8)
        self.span("gfx", "pj7m_header", (mi,), off, off + 0x28, (8, 0x0C))
        self.span("gfx", "root", (mi,), root, root + 0x44, (4, 8, 0x0C, 0x40))
        self.span("gfx", "scene", (mi,), scene, scene + 0x24, (4, 0x0C, 0x14, 0x1C))
        self.span("gfx", "scene_list", (mi,), q, q + 4, (0,))
        self.span("gfx", "scene_word", (mi,), word_at, word_at + 4)
        self.span("gfx", "node", (mi,), node, node + 0x0C, (4, 8))
        for kind, at in (("name", name_at), ("wname", wname_at), ("node_name", node_name_at)):
            self.span("gfx", kind, (mi,), at, cstring_end(d, at))

        textures = []
        for i in range(m.header["textures"]):
            head = pj7m.ptr(d, textab + 8 * i)
            pal = pj7m.ptr(d, paltab + 8 * i) if paltab is not None else None
            textures.append((self.texture(head), self.palette(pal), u32(d, textab + 8 * i + 4)))
        if textab is not None:
            n = m.header["textures"]
            self.span("gfx", "texture_table", (mi,), textab, textab + 8 * (n + 1), [8 * i for i in range(n)])
            self.span("gfx", "palette_table", (mi,), paltab, paltab + 8 * (n + 1), [8 * i for i in range(n)])

        texmaps = []
        for ti, tm in enumerate(m.texmaps):
            tex = m.textures[tm.texture]
            w, h = u16(d, tm.offset + 0x0C) + 1, u16(d, tm.offset + 0x0E) + 1
            size = (0, 0) if (w, h) == (tex.width, tex.height) else (w, h)
            texmaps.append(TexMap(tm.texture, u16(d, tm.offset + 0x1C), u16(d, tm.offset + 0x1E), *size))
            self.span("gfx", "texmap", (mi, ti), tm.offset, tm.offset + 0x4C)
        materials = []
        for ai, mt in enumerate(m.materials):
            materials.append(Material(u16(d, mt.offset), tuple(mt.colors), list(mt.texmaps), u16(d, mt.offset + 0x32)))
            self.span("gfx", "material", (mi, ai), mt.offset, mt.offset + 0x34)

        meshes = []
        mesh_list = pj7m.ptr(d, node + 4)
        if m.meshes:
            self.span("gfx", "mesh_list", (mi,), mesh_list, mesh_list + 8 * len(m.meshes),
                      [8 * i + 4 for i in range(len(m.meshes))])
        for xi, me in enumerate(m.meshes):
            arrays = {name: (pj7m.array(d, me, name) if me.counts[name] else []) for name in ("pos", "nrm", "clr", "tex")}
            meshes.append(Mesh(me.material, arrays["pos"], arrays["nrm"], arrays["clr"], arrays["tex"],
                               [(op, list(verts)) for op, verts in me.draws]))
            self.span("gfx", "mesh", (mi, xi), me.offset, me.offset + 0x1C, (8, 0x0C, 0x10, 0x14, 0x18))
            self.span("gfx", "display_list", (mi, xi), me.dl, me.dl + me.dl_size)
            for name, size in (("pos", 12), ("nrm", 12), ("clr", 4), ("tex", 8)):
                at = me.arrays[name]
                if at is not None and me.counts[name]:
                    self.span("gfx", "array_" + name, (mi, xi), at, at + size * me.counts[name])
        model = Model(m.name, m.wname, m.node_name, textures, texmaps, materials, meshes, u32(d, word_at))
        self.st.models.append(model)

    # ---- table ---------------------------------------------------------------------------------

    def table(self, model_index):
        d, st = self.tbl, self.st
        t = stadium_tbl.parse_table(d)
        P = t.pointers
        self.span("tbl", "packwii", (), 0, 0x20)
        self.span("tbl", "root", (), t.root, t.root + 4 + 4 * len(stadium_tbl.ROOT_FIELDS),
                  [4 + 4 * i for i in range(len(stadium_tbl.ROOT_FIELDS))])
        for i, p in enumerate(t.placements):
            if p.model not in model_index:
                raise ReadError(f"placement {i} points at 0x{p.model:X}, not a model")
            st.placements.append(Placement(model_index[p.model], tuple(p.translation), tuple(p.rotation),
                                           tuple(p.scale), tuple(p.bbox), tuple(p.sphere), p.flags))
            self.span("tbl", "placement", (i,), p.offset, p.offset + stadium_tbl.PLACEMENT_SIZE, (0,))

        for tree in ("a", "b"):
            nodes_at, lists_at = P["planes_" + tree], P["lists_" + tree]
            nodes = stadium_tbl.parse_tree(d, nodes_at, lists_at)
            entries = max((s + k for _, *sides in nodes for _, s, k in sides), default=0)
            if tree == "a":
                st.tree_a = (nodes, list(struct.unpack_from(f">{entries}H", d, lists_at)))
            self.span("tbl", "tree_" + tree, (), nodes_at, lists_at)
            self.span("tbl", "tree_" + tree + "_lists", (), lists_at, lists_at + 2 * entries)

        st.collision_vertices = [tuple(v) for v in t.vertices]
        st.collision_triangles = [tuple(x) for x in t.triangles]
        self.span("tbl", "collision", (), P["collision"], P["collision"] + 8 + 8 * len(t.triangles))
        self.span("tbl", "collision_vertices", (), P["coll_vertices"], P["coll_vertices"] + 12 * len(t.vertices))

        o, gi = P["unk_20"], 0
        while any(d[o + 0x20 * gi:o + 0x20 * (gi + 1)]):
            g = o + 0x20 * gi
            at, n = pj7m.ptr(d, g), u32(d, g + 4)
            points = [floats(d, at + 0x18 * k, 6) for k in range(n)] if n else []
            st.unk_20.append(PointGroup(points, floats(d, g + 8, 6)))
            if n:
                self.span("tbl", "unk_20_points", (gi,), at, at + 0x18 * n)
            gi += 1
        self.span("tbl", "unk_20", (), o, o + 0x20 * (gi + 1), [0x20 * k for k in range(gi)])

        o = P["unk_2c"]
        n = u32(d, o)
        st.preload_order = [u32(d, o + 4 + 4 * k) for k in range(n)]
        self.span("tbl", "unk_2c", (), o, o + 4 + 4 * n)
        e = P["textures"]
        for k in range(n):
            head, pal = pj7m.ptr(d, e + 0x10 * k), pj7m.ptr(d, e + 0x10 * k + 4)
            st.preload.append(PreloadEntry(self.texture(head), self.palette(pal), u32(d, e + 0x10 * k + 8),
                                           u32(d, e + 0x10 * k + 12)))
        st.preload_tail = u32(d, e + 0x10 * n + 8)
        self.span("tbl", "preload", (), e, e + 0x10 * (n + 1), [p for k in range(n) for p in (0x10 * k, 0x10 * k + 4)])

        o = P["unk_24"]
        pairs = 2 if o + 16 <= t.root else (t.root - o) // 8
        st.unk_24 = []
        for k in range(pairs):
            at, n = pj7m.ptr(d, o + 8 * k), u32(d, o + 8 * k + 4)
            st.unk_24.append([floats(d, at + 0x1C * r, 7) for r in range(n)] if n else [])
            if n:
                self.span("tbl", "unk_24_list", (k,), at, at + 0x1C * n)
        self.span("tbl", "unk_24", (), o, o + 8 * pairs, [8 * k for k in range(pairs)])


def read_stadium(gfx, tbl, spans=None):
    """Both packs of a game stadium as a Stadium (see the module docstring for `spans`)."""
    return _read(gfx, tbl, spans)


def read_graphics(gfx, spans=None):
    """The graphics pack alone (no table): models and textures; textures only the table points at
    come out as spare_textures. For comparing a written graphics pack before its table exists."""
    return _read(gfx, None, spans)


def _read(gfx, tbl, spans):
    r = _Reader(bytes(gfx), bytes(tbl or b""))
    r.span("gfx", "packwii", (), 0, 0x20)
    offsets = pj7m.find_models(r.gfx)
    for mi, off in enumerate(offsets):
        r.model(mi, off)
    if tbl is not None:
        r.table({off: mi for mi, off in enumerate(offsets)})
    r.spare_textures()
    r.texture_tails()
    if spans is not None:
        for where in spans:
            spans[where].extend(r.spans[where])
    return r.st
