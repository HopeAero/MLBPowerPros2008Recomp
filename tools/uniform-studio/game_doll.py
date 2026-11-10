# SPDX-License-Identifier: GPL-3.0-or-later
"""The game's chibi player ("pawapurokun") put together from its own models, for the 3D preview.

Everything is read at run time from the player's disc (``PWM_mdl_main_Up.pack`` inside
``files/project.bin``); this file holds only the decoder. Layout found by reading the pack and the
game (RL8E54), see docs/ingenieria-inversa.md:

  PJ7M root   +0x10 -> joint local translations (x, y, z, 1) x joints, +0x14 -> scales (same),
              +0x1C -> inverse bind matrices (3x4 f32) x joints, +0x20 -> envelopes (4 bytes each),
              +0x2C u32 joints (19 on the body), +0x30 u32 envelopes (16)
  scene       +0x08 u32 n, +0x0C -> n matrix lists {u8 count, u8 matrix id x count}
  node        mesh entry {u16 material, u16 sub-mesh count, -> sub-meshes}; sub-meshes are
              consecutive 0x1C records whose first u16 is the matrix list they use
  vertex      PNMTXIDX / 3 = position in the sub-mesh's matrix list -> matrix id; id < joints is a
              joint, otherwise envelope id - joints {u8 joint, s8 weight} x 2 (weights in percent,
              the second stored negated)

Positions of every vertex are in model space at the bind pose, so a joint j skins with
W[j] . IB[j] and an envelope with the weighted sum of those (identity at the bind pose). The bind
pose is a T-pose with the shoes hanging under the root; the game poses the menu figure with a motion.
Head and hands are separate rigid models; 0x806DAF80 places them at joint 17 (head), 11 (right hand)
and 16 (left hand) with W[j] . rot(IB[j]): at the bind pose they sit at the joint, axes as the body.
Materials whose second colour is black are the outline hulls (slightly larger copies, drawn black).
"""
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.append(str(Path(__file__).resolve().parents[1] / "blender" / "powerpros_stadium"))  # beside the exe, else the repo
import pj7m  # noqa: E402

PACK = "PWM_mdl_main_Up.pack"
BODY = "pM_U_pawapurokun"
HEAD_JOINT, HAND_R_JOINT, HAND_L_JOINT = 17, 11, 16
PARTS = (("head", "pM_U_head_cap", HEAD_JOINT), ("hand_R", "pM_U_hand_R", HAND_R_JOINT),
         ("hand_L", "pM_U_hand_L", HAND_L_JOINT))
# Arm chains (shoulder .. hand) and feet (ankle, toe) of the body skeleton; right is -x, the face +z.
ARM_R, ARM_L = (7, 8, 9, 10, 11), (12, 13, 14, 15, 16)
FOOT_R, FOOT_L = (1, 2), (3, 4)


def _u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def _u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def find_model(d, name):
    for off in pj7m.find_models(d):
        if pj7m.cstr(d, pj7m.ptr(d, off + 8)) == name:
            return off
    raise KeyError(f"{name} not in {PACK}")


def skeleton(d, off):
    """{'ib': (n, 4, 4) inverse bind matrices, 'local': (n, 3), 'parent': [int], 'env': [[(joint, w)]]}."""
    root = pj7m.ptr(d, off + 0x0C)
    n, ne = _u32(d, root + 0x2C), _u32(d, root + 0x30)
    if n == 0 or pj7m.ptr(d, root + 0x1C) is None:
        return None
    tr, ibp, envp = pj7m.ptr(d, root + 0x10), pj7m.ptr(d, root + 0x1C), pj7m.ptr(d, root + 0x20)
    ib = np.tile(np.eye(4), (n, 1, 1))
    for j in range(n):
        ib[j, :3] = np.array(struct.unpack_from(">12f", d, ibp + 48 * j)).reshape(3, 4)
    local = np.array([struct.unpack_from(">3f", d, tr + 16 * j) for j in range(n)])
    world = np.linalg.inv(ib)
    parent = []
    for j in range(n):          # the parent is the earlier joint whose frame turns local into world
        best, err = -1, np.linalg.norm(world[j, :3, 3] - local[j])
        for p in range(j):
            e = np.linalg.norm(world[p, :3, :3] @ local[j] + world[p, :3, 3] - world[j, :3, 3])
            if e < err - 1e-6:
                best, err = p, e
        parent.append(best)
    env = []
    for e in range(ne):
        b = d[envp + 4 * e:envp + 4 * e + 4]
        pairs = [(b[0], b[1] if b[1] < 0x80 else 0x100 - b[1]), (b[2], b[3] if b[3] < 0x80 else 0x100 - b[3])]
        total = sum(w for _, w in pairs) or 1
        env.append([(j, w / total) for j, w in pairs])
    return {"ib": ib, "bind": world, "local": local, "parent": parent, "env": env}


def submeshes(d, off):
    """[(material index, matrix list or None, pj7m.Mesh)] for every sub-mesh (pj7m reads only the first)."""
    root = pj7m.ptr(d, off + 0x0C)
    scene = pj7m.ptr(d, root + 4)
    node = pj7m.ptr(d, scene + 4)
    nlists, lists_off = _u32(d, scene + 8), pj7m.ptr(d, scene + 0x0C)
    lists = []
    for i in range(nlists if lists_off is not None else 0):
        p = pj7m.ptr(d, lists_off + 4 * i)
        lists.append(list(d[p + 1:p + 1 + d[p]]))
    out = []
    entries = pj7m.ptr(d, node + 4)
    for i in range(_u16(d, node)):
        e = entries + 8 * i
        material, count, first = _u16(d, e), _u16(d, e + 2), pj7m.ptr(d, e + 4)
        for k in range(count):
            mo = first + 0x1C * k
            arrays = dict(zip(("pos", "nrm", "clr", "tex"), (pj7m.ptr(d, mo + 8 + j * 4) for j in range(1, 5))))
            mesh = pj7m.Mesh(mo, material, pj7m.ptr(d, mo + 8), _u32(d, mo + 4), arrays)
            mesh.draws = pj7m.walk_display_list(d, mesh.dl, mesh.dl_size)
            for j, name in enumerate(("pos", "nrm", "clr", "tex")):
                mesh.counts[name] = 1 + max((v[j + 1] for _, vs in mesh.draws for v in vs), default=-1)
            lid = _u16(d, mo)
            out.append((material, lists[lid] if lid < len(lists) else None, mesh))
    return out


# ---- pose ------------------------------------------------------------------------------------------

def _children(parent, j):
    out, stack = [], [j]
    while stack:
        k = stack.pop()
        out.append(k)
        stack += [c for c, p in enumerate(parent) if p == k]
    return out


def _turn(world, parent, j, axis, degrees):
    """Rotate joint j and everything under it about a model-space axis through the joint."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    x, y, z = np.asarray(axis, float) / np.linalg.norm(axis)
    r = np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                  [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                  [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = world[j, :3, 3] - r @ world[j, :3, 3]
    for k in _children(parent, j):
        world[k] = m @ world[k]


def _move(world, parent, j, delta):
    for k in _children(parent, j):
        world[k, :3, 3] += delta


def stand_pose(skel, arm_bend=22.0, toe_out=20.0):
    """Joint model-space matrices for a relaxed standing figure, built from the bind pose.

    Hand-made approximation of the menu figure (not the game's motion): each arm bends down by
    arm_bend degrees at its first three joints, the ankles come up to the root height, move back
    by the foot length so the shoes sit under the body, and the toes turn out by toe_out degrees.
    """
    world = skel["bind"].copy()
    parent = skel["parent"]
    for chain, sign in ((ARM_R, 1.0), (ARM_L, -1.0)):
        for j in chain[:3]:
            _turn(world, parent, j, (0, 0, 1), sign * arm_bend)
    for (ankle, toe), sign in ((FOOT_R, -1.0), (FOOT_L, 1.0)):
        length = np.linalg.norm(world[toe, :3, 3] - world[ankle, :3, 3])
        _move(world, parent, ankle, np.array([0.0, -world[ankle, 1, 3], -length]))
        _turn(world, parent, ankle, (0, 1, 0), sign * toe_out)
    return world


# ---- assembly --------------------------------------------------------------------------------------

def _part_triangles(d, off, part, skin, out):
    """skin(matrix id or None) -> 4x4 matrix; appends this model's triangles to out."""
    model = pj7m.parse_model(d, off)
    subs = submeshes(d, off)
    decal = None
    if part == "head":          # the cap letter: the smallest lit textured piece (a quad pair in front)
        small = [(m.counts["pos"], mat) for mat, _, m in subs
                 if model.materials[mat].texmaps and model.materials[mat].colors[1][:3] != (0, 0, 0)]
        decal = min(small)[1] if small else None
    for material, mlist, mesh in subs:
        pos, tex = pj7m.array(d, mesh, "pos"), pj7m.array(d, mesh, "tex") if mesh.counts["tex"] else None
        mat = model.materials[material]
        outline = mat.colors[1][:3] == (0, 0, 0)
        tag = None
        if mat.texmaps:
            t = model.textures[model.texmaps[mat.texmaps[0]].texture]
            if (t.width, t.height) == (128, 128):
                tag = "body"    # the game binds its composed 128x128 uniform atlas here (cap too)
            elif part == "head" and material == decal:
                tag = "cap"
            else:
                tag = f"{part}:{t.width}x{t.height}#{model.texmaps[mat.texmaps[0]].texture}"
        cache = {}
        for tri in pj7m.triangles(mesh):
            ps, uvs = [], []
            for v in tri:
                mid = mlist[v[0] // 3] if mlist else None
                if mid not in cache:
                    cache[mid] = skin(mid)
                p = cache[mid] @ np.array([*pos[v[1]], 1.0])
                ps.append(tuple(float(c) for c in p[:3]))
                uvs.append(tuple(tex[v[4]]) if tex else (0.0, 0.0))
            out.append({"pos": tuple(ps), "uv": tuple(uvs), "texture": tag, "outline": outline, "part": part})


def assemble(archive, pose=None):
    """The figure as {'triangles': [...], 'joints': (n, 3)} in one model space (decimetres, Y up,
    face towards +z, the character's right towards -x).

    Each triangle: {'pos': 3 xyz, 'uv': 3 uv (GX: v = 0 is the top row), 'texture': 'body' (the
    128x128 uniform atlas), 'cap' (the 64x32 cap letter) or '<part>:<w>x<h>#<texture index>',
    'outline': True for the black hull, 'part': 'body' | 'head' | 'hand_R' | 'hand_L'}.
    pose: None for stand_pose(), 'bind' for the T-pose, or n joint matrices (3x4 or 4x4, model
    space, the layout of the game's posed joint array) to copy a pose read elsewhere.
    archive: anything with read(name) -> bytes, e.g. tools/projectbin.py Archive on the player's disc.
    """
    d = archive.read(PACK)
    if d is None:
        raise KeyError(f"{PACK} not in the archive")
    body = find_model(d, BODY)
    skel = skeleton(d, body)
    if isinstance(pose, str) and pose == "bind":
        world = skel["bind"].copy()
    elif pose is None:
        world = stand_pose(skel)
    else:
        world = np.tile(np.eye(4), (len(skel["ib"]), 1, 1))
        for j, m in enumerate(pose):
            world[j, :3] = np.asarray(m, float).reshape(-1, 4)[:3]
    joints = len(skel["ib"])
    skin_j = world @ skel["ib"]

    def body_skin(mid):
        if mid is None:
            return np.eye(4)
        if mid < joints:
            return skin_j[mid]
        return sum(w * skin_j[j] for j, w in skel["env"][mid - joints])

    triangles = []
    _part_triangles(d, body, "body", body_skin, triangles)
    for part, name, joint in PARTS:
        rot = np.eye(4)
        rot[:3, :3] = skel["ib"][joint, :3, :3]
        attach = world[joint] @ rot
        _part_triangles(d, find_model(d, name), part, lambda _mid, m=attach: m, triangles)
    return {"triangles": triangles, "joints": world[:, :3, 3].copy()}
