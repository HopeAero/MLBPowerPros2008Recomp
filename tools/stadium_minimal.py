# SPDX-License-Identifier: GPL-3.0-or-later
"""A minimal stadium made from nothing (no data read from the game), written as a new-stadium mod.

python tools/stadium_minimal.py <mods folder> [--code minimo]

Writes <mods folder>/Estadio mínimo/ with mod.toml ([[stadium]] code, base col) and its two packs:
a checkered ground, a ring fence, a ring of stands and their collision. It is the proof that the
writer (tools/blender/powerpros_stadium/stadium_write.py) needs no template, and the test bench for
the fields still undecoded, which take their most common value in the game's stadiums.
Game units: decimetres, Y up, home plate at the origin, centre field towards -Z.
"""
import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))
import packwii                                                                   # noqa: E402
from powerpros_stadium.names import valid_code                                   # noqa: E402
from powerpros_stadium.stadium_model import (Material, Mesh, Model, Placement, Stadium, TexMap,  # noqa: E402
                                             Texture)
from powerpros_stadium.stadium_write import write_stadium                        # noqa: E402

# Most common values of the undecoded fields in the game's stadiums.
COLORS = ((0, 0, 0, 255), (204, 204, 204, 255), (0, 0, 0, 255))
KIND, UNK_32, PRELOAD_TAIL = 3, 1, 0x6EAC80
GROUND_FLAGS, FENCE_FLAGS, STAND_FLAGS = 0x04810000, 0x00001100, 0x00000000
SIDES = 32
FIELD_CENTRE = (0.0, -450.0)            # x, z of the fence ring's centre
FENCE_RADIUS, FENCE_HEIGHT, WALL_HEIGHT = 1100.0, 30.0, 200.0
STAND_INNER, STAND_OUTER, STAND_LOW, STAND_HIGH = 1150.0, 1600.0, 30.0, 250.0
GROUND_SIZE, GROUND_CENTRE_Z, GROUND_REPEAT = 2400.0, -600.0, 40
DRAW_VERTICES = 3 * 190                 # vertices per draw call, as the exporter


def f32(*values):
    """The values as the packs store them (32-bit floats), so the description reads back the same."""
    return tuple(struct.unpack(f">{len(values)}f", struct.pack(f">{len(values)}f", *values)))


def checker_texture():
    """64 x 64 RGB5A3, two greens in 8-pixel squares."""
    y, x = np.mgrid[0:64, 0:64]
    light = ((x // 8 + y // 8) % 2).astype(bool)
    rgba = np.zeros((64, 64, 4), np.uint8)
    rgba[...] = (40, 120, 40, 255)
    rgba[light] = (60, 150, 55, 255)
    return Texture(5, 64, 64, packwii.tile(packwii.to_rgb5a3(rgba), 5).astype(">u2").tobytes())


def ring(radius, y):
    cx, cz = FIELD_CENTRE
    return [f32(cx + radius * math.sin(2 * math.pi * k / SIDES), y, cz - radius * math.cos(2 * math.pi * k / SIDES))
            for k in range(SIDES)]


def band(inner, outer):
    """Quads between two rings of SIDES points, as triangles, both windings (seen from both sides)."""
    tris = []
    for k in range(SIDES):
        a, b = k, (k + 1) % SIDES
        quad = (a, b, SIDES + b, SIDES + a)
        for t in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
            tris += [t, t[::-1]]
    return inner + outer, tris


def mesh(positions, tris, color, uvs=None):
    """One untextured (or textured with `uvs`) mesh drawn as triangle lists."""
    corners = [(0, i, 0, 0, i if uvs else 0) for t in tris for i in t]
    draws = [(0x90, corners[i:i + DRAW_VERTICES]) for i in range(0, len(corners), DRAW_VERTICES)]
    return Mesh(0, positions, [(0.0, 1.0, 0.0)], [color], uvs or [(0.0, 0.0)], draws)


def placement(model, positions, flags):
    lo = tuple(min(p[k] for p in positions) for k in range(3))
    hi = tuple(max(p[k] for p in positions) for k in range(3))
    centre = tuple((a + b) / 2 for a, b in zip(lo, hi))
    radius = max(math.dist(centre, p) for p in positions)
    return Placement(model, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), f32(*lo, *hi), f32(*centre, radius), flags)


def minimal_stadium():
    h, z0 = GROUND_SIZE / 2, GROUND_CENTRE_Z
    ground_pos = [(-h, 0.0, z0 + h), (h, 0.0, z0 + h), (h, 0.0, z0 - h), (-h, 0.0, z0 - h)]
    ground_uvs = [f32((x + h) / GROUND_SIZE * GROUND_REPEAT, (z0 + h - z) / GROUND_SIZE * GROUND_REPEAT) for x, _, z in ground_pos]
    ground_tris = [(0, 1, 2), (0, 2, 3), (2, 1, 0), (3, 2, 0)]
    fence_pos, fence_tris = band(ring(FENCE_RADIUS, 0.0), ring(FENCE_RADIUS, FENCE_HEIGHT))
    stand_pos, stand_tris = band(ring(STAND_INNER, STAND_LOW), ring(STAND_OUTER, STAND_HIGH))

    def model(name, meshes, textured):
        return Model(name, "_Wmodel_" + name, name.replace("DP_", "p"),
                     [(0, None, 0)] if textured else [], [TexMap(0, GROUND_REPEAT, GROUND_REPEAT)] if textured else [],
                     [Material(KIND, COLORS, [0] if textured else [], UNK_32)], meshes)

    models = [model("DP_ground", [mesh(ground_pos, ground_tris, (255, 255, 255, 255), ground_uvs)], True),
              model("DP_fence", [mesh(fence_pos, fence_tris, (110, 110, 110, 255))], False),
              model("DP_stand", [mesh(stand_pos, stand_tris, (170, 170, 175, 255))], False)]
    placements = [placement(0, ground_pos, GROUND_FLAGS), placement(1, fence_pos, FENCE_FLAGS),
                  placement(2, stand_pos, STAND_FLAGS)]

    # Collision: (b - a) x (c - a) points to the side the ball hits: the ground up, the walls inwards.
    verts = list(ground_pos)
    tris = [(0, 2, 1, 5), (0, 3, 2, 5)]
    for radius_ring, top, surface in ((0.0, FENCE_HEIGHT, 9), (FENCE_HEIGHT, WALL_HEIGHT, 1)):
        base = len(verts)
        verts += ring(FENCE_RADIUS, radius_ring) + ring(FENCE_RADIUS, top)
        for k in range(SIDES):
            a, b = base + k, base + (k + 1) % SIDES
            tris += [(a, b + SIDES, b, surface), (a, a + SIDES, b + SIDES, surface)]
    return Stadium(textures=[checker_texture()], models=models, placements=placements, collision_vertices=verts,
                   collision_triangles=tris, unk_20=[], unk_24=[[], []], preload_order=[], preload=[],
                   preload_tail=PRELOAD_TAIL)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mods")
    ap.add_argument("--code", default="minimo")
    a = ap.parse_args(argv)
    if not valid_code(a.code):
        sys.exit(f"'{a.code}': a stadium code has 2 to 16 lowercase letters, digits or _")
    gfx, tbl = write_stadium(minimal_stadium())
    mod = Path(a.mods) / "Estadio mínimo"
    (mod / "project").mkdir(parents=True, exist_ok=True)
    (mod / "project" / f"stadium_{a.code}.pack").write_bytes(gfx)
    (mod / "project" / f"stadium_{a.code}tbl.pack").write_bytes(tbl)
    (mod / "mod.toml").write_text(f'name = "Estadio mínimo"\n\n[[stadium]]\ncode = "{a.code}"\nname = "Estadio mínimo"\n'
                                  f'base = "col"\n', encoding="utf-8")
    print(f"{mod}: stadium {a.code}, {len(gfx)} + {len(tbl)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
