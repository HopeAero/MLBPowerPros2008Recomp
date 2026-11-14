# SPDX-License-Identifier: GPL-3.0-or-later
"""Pieces of the game's own stadiums (stands, crowd, glass, fences, scoreboards, buildings) put into a
from-scratch stadium, read from the player's own project.bin. They come in at their place in their
stadium, with the role their placement flags give, and are exported like any other object (re-encoded
textures, their own vertex colours: see convert.face_copies)."""
import re
from functools import lru_cache

import bpy

from . import importer, pj7m, stadium_tbl

try:
    from . import projectbin
except ImportError:
    import projectbin

HIDE_ABOVE = 0x80000000
GROUND = 0x04000000     # ground layer pieces (grass, dirt, lines, bases): the field comes from field_gen or the model
NIGHT = 0x08            # piece kind of the lights, drawn only at night: as a plain piece they would show by day
ZONE = re.compile(r"_([LR]\d|C)(?=_|$)")


def role(flags):
    """The from-scratch role (scratch.ROLES) closest to a game placement's flags."""
    kind, blend = (flags >> 8) & 0xFF, (flags >> 16) & 0xFF
    if kind == 0x16:
        return "crowd_lying"
    if kind in (0x15, 0x17):     # ponytail: 0x15 (AudienceC) drawn as upright crowd; its own slot is not decoded
        return "crowd_upright"
    if kind == 0x05:
        return "flags"
    if blend == 0x0A:
        return "additive"
    if blend == 0x06:
        return "transparent"
    if kind == 0x11:
        return "fence"
    return "stand"


def zone(name):
    """Stand section of a piece name (L1, R3, C...): stands, crowd and glass of one section share it."""
    m = ZONE.search(name)
    return m.group(1) if m else None


@lru_cache(maxsize=8)
def _stadium(project_bin, code):
    archive = projectbin.Archive(project_bin)
    gfx, tbl = archive.read(f"stadium_{code}.pack"), archive.read(f"stadium_{code}tbl.pack")
    if gfx is None or tbl is None:
        return None, []
    return gfx, stadium_tbl.parse_table(tbl).placements


@lru_cache(maxsize=64)
def parts(project_bin, code):
    """(placement index, model name, role, hidden from above) of the stadium's usable pieces."""
    gfx, placements = _stadium(project_bin, code)
    out = []
    for i, pl in enumerate(placements):
        if pl.flags & GROUND or (pl.flags >> 8) & 0xFF == NIGHT:
            continue
        out.append((i, pj7m.parse_model(gfx, pl.model).name, role(pl.flags), bool(pl.flags & HIDE_ABOVE)))
    return tuple(out)


def insert(coll, code, indices):
    """Put the pieces at those placement indices of stadium `code` into coll; returns the new objects."""
    project_bin = coll["pp_project_bin"]
    gfx, placements = _stadium(project_bin, code)
    usable = {i: (r, h) for i, _, r, h in parts(project_bin, code)}
    images, materials, made = {}, {}, []
    for i in indices:
        if i not in usable:
            continue
        pl = placements[i]
        model = pj7m.parse_model(gfx, pl.model)
        obj = bpy.data.objects.new(f"{code} · {model.name}", importer.build_mesh(gfx, model, images, materials))
        obj.matrix_world = importer.placement_matrix(pl)
        obj["pp_part"] = f"{code}:{model.name}"
        obj.powerpros_role, obj.powerpros_hide_above = usable[i]
        obj.powerpros_separate_layers = False     # the game's pieces already sit where they should
        coll.objects.link(obj)
        made.append(obj)
    return made


def with_zone(project_bin, code, index):
    """index and every other piece of its stand section (only index when its name has none)."""
    found = parts(project_bin, code)
    z = next((zone(n) for i, n, _, _ in found if i == index), None)
    return [index] if z is None else [i for i, n, _, _ in found if zone(n) == z]

