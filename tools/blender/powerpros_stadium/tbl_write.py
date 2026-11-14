# SPDX-License-Identifier: GPL-3.0-or-later
"""Rewrite the collision mesh and the plane trees of a stadium tbl (append and repoint).

Both trees (docs/ingenieria-inversa.md) are binary space partitions of 0x1C-byte nodes in preorder:
plane nx ny nz d (f32), then per side u16 child, u16 list start, u8 list count (H side at +0x10,
+0x14, +0x18; L side at +0x12, +0x16, +0x19). A side with child 0 is a leaf: `count` u16 entries
of the tree's list from `start` (the game reads `count` as a signed byte: at most 127). `start` is a
u16 in the game; the recomp's walker (hle/mlb/collision_tree.cpp) takes +0x1A (H) and +0x1B (L), always
0 in the game, as its high byte, so a list can pass 65535 entries (big collision meshes). H holds
what reaches n.p + d < 0, L what reaches > 0; anything crossing the plane is in both. Tree A
partitions the placements (by their box), tree B the collision triangles. The game walks them
recursively and marks every listed index in a flag array (0x80C860E4, 0x80C862EC): no fixed-size
buffer, so only the leaf count limits a leaf.

New data goes after the root, which stays where it is: the stadium table of the game (and the
runtime, which searches the root from the end of the file) still find it.
"""
import bisect
import struct

from . import stadium_tbl
from .i18n import T

P = 0x40000000
NODE_SIZE = 0x1C
MAX_LEAF = 127              # the game reads the leaf count as a signed byte (0x80C860E4)
MAX_LIST = 0xFFFFFF + MAX_LEAF        # u16 start + its high byte at +0x1A/+0x1B (runtime 0x80C860E4) + count


class TreeError(ValueError):
    pass


def build_tree(boxes, leaf=8):
    """boxes: [(index, (min xyz), (max xyz))]. Returns (nodes, entries): nodes as
    (plane, (child, start, count) H side, (child, start, count) L side), entries as u16 indices."""
    nodes, entries = [], []

    def leaf_of(items):
        if len(items) > MAX_LEAF:
            raise TreeError(T(f"{len(items)} elementos que no se pueden separar (máximo {MAX_LEAF} por hoja)",
                              f"{len(items)} items that cannot be split apart (at most {MAX_LEAF} per leaf)"))
        start = len(entries)
        entries.extend(i for i, _, _ in items)
        return (0, start, len(items))

    def side(items, depth):
        if len(items) <= leaf or depth > 40:
            return leaf_of(items)
        found = split(items)
        # Stop where a plane no longer separates much (long walls cross every plane).
        if found is None or (found[0] > 0.8 * len(items) and len(items) <= MAX_LEAF):
            return leaf_of(items)
        return (node(items, found, depth), 0, 0)

    def split(items):
        """(cost, axis, position) of the plane that best halves the items without copying many."""
        n, best = len(items), None
        for axis in range(3):
            mins = sorted(b[1][axis] for b in items)
            maxs = sorted(b[2][axis] for b in items)
            centres = sorted((b[1][axis] + b[2][axis]) / 2 for b in items)
            for q in range(1, 16):
                at = centres[n * q // 16]
                neg = bisect.bisect_right(mins, at)                 # reach the negative side
                pos = n - bisect.bisect_left(maxs, at)              # reach the positive side
                if neg == n or pos == n:
                    continue
                cost = max(neg, pos) + 0.5 * (neg + pos - n)
                if best is None or cost < best[0]:
                    best = (cost, axis, at)
        return best

    def node(items, found, depth):
        if found is None:                       # nothing separates them: all on one side
            axis, at = 0, max(b[2][0] for b in items) + 1.0
        else:
            _, axis, at = found
        neg = [b for b in items if b[1][axis] <= at]
        pos = [b for b in items if b[2][axis] >= at]
        index = len(nodes)
        nodes.append(None)
        plane = [0.0, 0.0, 0.0, -at]
        plane[axis] = 1.0
        l_side = side(pos, depth + 1)           # the game's trees lay the L subtree first
        h_side = side(neg, depth + 1)
        nodes[index] = (tuple(plane), h_side, l_side)
        return index

    items = list(boxes)
    node(items, split(items), 0)                # the root is always a node
    if len(entries) > MAX_LIST:
        raise TreeError(T(f"El árbol necesita {len(entries)} entradas (máximo {MAX_LIST})",
                          f"The tree needs {len(entries)} entries (at most {MAX_LIST})"))
    return nodes, entries


def pack_tree(nodes, entries):
    blob = bytearray()
    for plane, (hc, hs, hk), (lc, ls, lk) in nodes:
        blob += struct.pack(">4f4H4B", *plane, hc, lc, hs & 0xFFFF, ls & 0xFFFF, hk, lk, hs >> 16, ls >> 16)
    return bytes(blob), struct.pack(f">{len(entries)}H", *entries)


def triangle_boxes(vertices, triangles):
    out = []
    for i, (a, b, c, _) in enumerate(triangles):
        pts = (vertices[a], vertices[b], vertices[c])
        out.append((i, tuple(min(p[k] for p in pts) for k in range(3)), tuple(max(p[k] for p in pts) for k in range(3))))
    return out


def placement_boxes(table):
    return [(i, tuple(p.bbox[:3]), tuple(p.bbox[3:])) for i, p in enumerate(table.placements)]


class TblWriter:
    def __init__(self, data):
        self.d = bytearray(data)
        self.root = stadium_tbl.find_root(self.d)

    def append(self, blob, align=32):
        self.d.extend(b"\0" * (-len(self.d) % align))
        off = len(self.d)
        self.d.extend(blob)
        return off

    def set_pointer(self, field, offset):
        struct.pack_into(">I", self.d, self.root + 4 + 4 * stadium_tbl.ROOT_FIELDS.index(field), P | offset)

    def set_model_tree(self):
        """Tree A again for the placements as they are now (after pieces moved or changed): one leaf
        with every placement in table order. The game seems to draw the pieces in the order it finds
        them in this tree: a spatial split put the crowd pieces (which do not write depth) before the
        stands, which then covered them. Only stadiums with more than 127 pieces get a split tree."""
        boxes = placement_boxes(stadium_tbl.parse_table(self.d))
        if len(boxes) <= MAX_LEAF:
            far = max(b[2][0] for b in boxes) + 1.0
            nodes, entries = [((1.0, 0.0, 0.0, -far), (0, 0, len(boxes)), (0, len(boxes), 0))], [i for i, _, _ in boxes]
        else:
            nodes, entries = build_tree(boxes)
        tree, lists = pack_tree(nodes, entries)
        self.set_pointer("planes_a", self.append(tree))
        self.set_pointer("lists_a", self.append(lists, 4))         # right after the nodes, like the game

    def set_collision(self, vertices, triangles, leaf=12):
        """vertices: game-space xyz; triangles: (a, b, c, surface id). Rebuilds tree B too."""
        if len(vertices) > 0xFFFF or len(triangles) > 0xFFFF:
            raise TreeError(T(f"Colisión demasiado grande: {len(triangles)} triángulos, {len(vertices)} vértices "
                              "(máximo 65535 de cada)",
                              f"Collision too big: {len(triangles)} triangles, {len(vertices)} vertices "
                              "(at most 65535 of each)"))
        head = struct.pack(">HHI", len(triangles), len(vertices), 0)
        head += b"".join(struct.pack(">4H", *t) for t in triangles)
        self.set_pointer("collision", self.append(head))
        self.set_pointer("coll_vertices", self.append(b"".join(struct.pack(">3f", *v) for v in vertices)))
        nodes, entries = build_tree(triangle_boxes(vertices, triangles), leaf)
        tree, lists = pack_tree(nodes, entries)
        self.set_pointer("planes_b", self.append(tree))
        self.set_pointer("lists_b", self.append(lists, 4))

    def finish(self):
        self.d.extend(b"\0" * (-len(self.d) % 32))
        if stadium_tbl.find_root(self.d) != self.root:
            raise TreeError(T("Los datos nuevos se confunden con la raíz de la tabla",
                              "The new data would be mistaken for the table root"))
        return bytes(self.d)
