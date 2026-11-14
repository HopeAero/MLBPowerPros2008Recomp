# SPDX-License-Identifier: GPL-3.0-or-later
"""stadium_<team>tbl.pack: where each stadium model goes, plus the collision mesh.

Root: the last object of the file, u32 model count followed by pointers; found as the last
u32 n (n < 0x1000) followed by the pointer to 0x20, where the placement records start.

  root +0x00  u32 model count
       +0x04  -> placements (n records of 0x54 bytes)
       +0x08  -> tree A: plane nodes partitioning the placements (by their box)
       +0x0C  -> u16 lists of tree A (placement indices)
       +0x10  -> tree B: plane nodes partitioning the collision triangles
       +0x14  -> u16 lists of tree B (triangle indices); node layout in tbl_write.py
       +0x18  -> collision vertices (f32 xyz)
       +0x1C  -> collision header: u16 triangle count, u16 vertex count, u32 0, then triangles
                 of four u16: a, b, c, surface id
       +0x20  -> ?  +0x24 -> ?  +0x28 -> texture list  +0x2C -> ?
  placement   -> PJ7M header in the graphics pack, translation xyz, rotation xyz (radians),
              scale xyz, bounding box (min xyz, max xyz), bounding sphere (xyz, radius), u32 flags
"""
import struct
from dataclasses import dataclass

PLACEMENT_SIZE = 0x54
NODE_SIZE = 0x1C
ROOT_FIELDS = ("placements", "planes_a", "lists_a", "planes_b", "lists_b", "coll_vertices",
               "collision", "unk_20", "unk_24", "textures", "unk_2c")


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def ptr(d, o):
    p = u32(d, o)
    return p & 0x3FFFFFFF if p >> 30 == 1 else None


class FormatError(ValueError):
    pass


@dataclass
class Placement:
    offset: int
    model: int            # PJ7M header offset in the graphics pack
    translation: tuple
    rotation: tuple
    scale: tuple
    bbox: tuple
    sphere: tuple
    flags: int


@dataclass
class Table:
    root: int
    pointers: dict        # ROOT_FIELDS -> file offset (or None)
    placements: list
    vertices: list        # collision vertices (x, y, z)
    triangles: list       # (a, b, c, surface id)


def find_root(d):
    for o in range(len(d) - 8, 0x1F, -4):
        if u32(d, o + 4) == 0x40000020 and 0 < u32(d, o) < 0x1000:
            return o
    raise FormatError("no table root (count + pointer to 0x20)")


def parse_tree(d, nodes_off, lists_off):
    """Plane tree A or B (layout in tbl_write.py): [(plane, (child, start, count) H, same for L)]."""
    out = []
    for o in range(nodes_off, lists_off - NODE_SIZE + 1, NODE_SIZE):
        plane = struct.unpack_from(">4f", d, o)
        hc, lc, hs, ls = struct.unpack_from(">4H", d, o + 0x10)
        hk, lk, hh, lh = struct.unpack_from(">4B", d, o + 0x18)     # +0x1A/+0x1B: high bytes of the starts
        hs, ls = hs | hh << 16, ls | lh << 16
        out.append((plane, (hc, hs, hk), (lc, ls, lk)))
    return out


def tree_members(d, nodes, lists_off, index, side):
    """Indices reachable from one side (0 = H, 1 = L) of a node."""
    found, stack = [], [nodes[index][1 + side]]
    while stack:
        child, start, count = stack.pop()
        if child:
            stack += [nodes[child][1], nodes[child][2]]
        else:
            found += struct.unpack_from(f">{count}H", d, lists_off + 2 * start)
    return found


def parse_table(d):
    root = find_root(d)
    count = u32(d, root)
    pointers = {name: ptr(d, root + 4 + i * 4) for i, name in enumerate(ROOT_FIELDS)}
    placements = []
    for i in range(count):
        o = pointers["placements"] + i * PLACEMENT_SIZE
        f = struct.unpack_from(">20f", d, o + 4)
        model = ptr(d, o)
        if model is None:
            raise FormatError(f"placement 0x{o:X} has no model pointer")
        placements.append(Placement(o, model, f[0:3], f[3:6], f[6:9], f[9:15], f[15:19], u32(d, o + 0x50)))
    vertices, triangles = [], []
    head = pointers["collision"]
    if head is not None:
        ntri, nvert = struct.unpack_from(">2H", d, head)
        triangles = [struct.unpack_from(">4H", d, head + 8 + i * 8) for i in range(ntri)]
        vo = pointers["coll_vertices"]
        vertices = [struct.unpack_from(">3f", d, vo + i * 12) for i in range(nvert)]
    return Table(root, pointers, placements, vertices, triangles)
