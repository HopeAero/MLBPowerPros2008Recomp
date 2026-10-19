# SPDX-License-Identifier: GPL-3.0-or-later
"""A stadium as data: everything needed to write its two packs (stadium_<code>.pack and its tbl) from
scratch (stadium_write.py), read from the game's packs by stadium_read.py or built by hand.

Indices, never file offsets, tie the parts together. Fields whose meaning is not known yet keep a
provisional name (unk_*); docs/ingenieria-inversa.md, section 5, has the format.
Units are the game's: decimetres, Y up, home plate at the origin, centre field towards -Z.
"""
from dataclasses import dataclass, field


@dataclass
class Palette:
    entries: int                   # 16 (C4) or 256 (C8); 0xFFFF in the video board's placeholder
    colors: bytes                  # entries big-endian RGB5A3 colours; empty: no colours (pointer 0)
    format: int = 0x6000           # u16 +0x06: 0x6000, or 0x4000 in the video board's placeholder


@dataclass
class Texture:
    fmt: int                       # GX format: 5 RGB5A3, 6 RGBA8, 8 C4, 9 C8
    width: int
    height: int
    pixels: bytes                  # already in GX tile order
    tail: bytes = b""              # bytes right after the pixels, before the next structure: smaller mip
                                   # levels in the ones checked; the game reads only the first level


@dataclass
class TexMap:
    slot: int                      # index into Model.textures
    repeat_u: int                  # how many times the texture repeats (>= the meshes' largest |u|)
    repeat_v: int
    width: int = 0                 # size the texmap declares; 0: its texture's. Differs in 129 game
    height: int = 0                # texmaps (crowd pieces: an 8x8 texture declared 128x64)


@dataclass
class Material:
    kind: int                      # u16 +0x00: 3 (4 in six Oakland materials)
    colors: tuple                  # three RGBA tuples, meaning not checked
    texmaps: list                  # used texmap slots (indices into Model.texmaps); empty: untextured
    unk_32: int                    # u16 +0x32: 0 or 1


@dataclass
class Mesh:
    material: int
    positions: list                # (x, y, z)
    normals: list                  # (x, y, z)
    colors: list                   # (r, g, b, a) bytes
    uvs: list                      # (u, v)
    draws: list                    # [(opcode 0x98 strip / 0x90 triangles, [(mtx, pos, nrm, clr, tex), ...])]


@dataclass
class Model:
    name: str
    wname: str                     # "_Wmodel_<name>"
    node_name: str                 # Maya node name
    textures: list                 # [(index into Stadium.textures, index into Stadium.palettes or None,
                                   #   mip level: 0, or 1/2 in MIP pieces that use the smaller levels)]
    texmaps: list                  # [TexMap]
    materials: list                # [Material]
    meshes: list                   # [Mesh]
    scene_word: int = 0x01000000   # the word behind the scene's second list


@dataclass
class Placement:
    model: int                     # index into Stadium.models
    translation: tuple
    rotation: tuple                # radians
    scale: tuple
    bbox: tuple                    # min xyz, max xyz
    sphere: tuple                  # centre xyz, radius
    flags: int


@dataclass
class PointGroup:
    """unk_20: a group of points, each (x, y, z, rx, ry, rz), with its box (min xyz, max xyz)."""
    points: list
    box: tuple


@dataclass
class PreloadEntry:
    texture: int | None            # index into Stadium.textures (None: pointer 0, as in bullpen)
    palette: int | None            # index into Stadium.palettes
    tmem_a: int
    tmem_b: int


@dataclass
class Stadium:
    textures: list = field(default_factory=list)
    palettes: list = field(default_factory=list)
    models: list = field(default_factory=list)
    placements: list = field(default_factory=list)
    collision_vertices: list = field(default_factory=list)     # (x, y, z)
    collision_triangles: list = field(default_factory=list)    # (a, b, c, surface id)
    unk_20: list = field(default_factory=list)                 # [PointGroup]
    unk_24: list = field(default_factory=lambda: [[], []])     # one list of 7-float records per pair
    preload_order: list = field(default_factory=list)          # unk_2c: u32 per preloaded texture
    preload: list = field(default_factory=list)                # [PreloadEntry]
    preload_tail: int = 0          # u32 +8 of the zero entry closing the preload list (0x692800..0x703280)
    spare_textures: list = field(default_factory=list)         # [(texture, palette or None)] nothing uses


@dataclass
class Span:
    """Where the reader found one structure. key: indices in the description (never file offsets), so
    the original and a rewritten pack can be matched; pointers: offsets of pointer fields from start."""
    kind: str
    key: tuple
    start: int
    end: int
    pointers: tuple = ()
