# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the stadium pack writer (docs/superpowers/plans/2026-10-18-stadium-pack-writer.md).

python tools/test_stadium_writer.py [project.bin]
Without project.bin only the synthetic tests run (no game data). With it, also the ones that read the
game's stadiums; their output stays in memory or under local/tmp/stadium-writer. Exits 1 on failure.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))

failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


def stadium_codes(archive):
    """Codes of the stadiums that have a table, in archive order."""
    return [m[1] for name, *_ in archive.entries() if (m := re.search(r"/stadium_([a-z0-9]+)tbl\.pack$", name))]


def packs(archive, code):
    return archive.read(f"/stadium_{code}.pack"), archive.read(f"/stadium_{code}tbl.pack")


# ---- Task 1: description and reader --------------------------------------------------------------

def test_reader_known_values(archive):
    from powerpros_stadium.stadium_read import read_stadium
    col = read_stadium(*packs(archive, "col"))
    check(len(col.placements) == 82, f"col: 82 placements ({len(col.placements)})")
    check([len(g.points) for g in col.unk_20] == [32, 50, 50, 25], f"col: unk_20 groups {[len(g.points) for g in col.unk_20]}")
    check([len(x) for x in col.unk_24] == [7, 7], f"col: unk_24 lists {[len(x) for x in col.unk_24]}")
    check(col.preload_order == list(range(8)) and len(col.preload) == 8, f"col: 8 preloaded textures in order 0-7")
    nyy = read_stadium(*packs(archive, "nyy"))
    check([len(g.points) for g in nyy.unk_20] == [150, 144], f"nyy: unk_20 groups {[len(g.points) for g in nyy.unk_20]}")
    bull = read_stadium(*packs(archive, "bullpen"))
    check(bull.unk_20 == [] and bull.unk_24 == [[]], f"bullpen: no unk_20 groups, one empty unk_24 list ({bull.unk_24})")
    words = {m.scene_word for code in stadium_codes(archive) for m in read_stadium(*packs(archive, code)).models}
    check(words == {0x01000000}, f"every scene word is 0x01000000 ({sorted(hex(w) for w in words)})")


def uncovered(data, spans):
    """Offsets of non-zero bytes outside every span, and spans that leave the file."""
    covered = bytearray(len(data))
    outside = [s for s in spans if s.start < 0 or s.end > len(data) or s.end < s.start]
    for s in spans:
        covered[max(0, s.start):min(len(data), s.end)] = b"\1" * (min(len(data), s.end) - max(0, s.start))
    return [i for i, b in enumerate(data) if b and not covered[i]], outside


def test_reader_covers_every_byte(archive):
    from powerpros_stadium.stadium_read import read_stadium
    bad = []
    for code in stadium_codes(archive):
        gfx, tbl = packs(archive, code)
        spans = {"gfx": [], "tbl": []}
        read_stadium(gfx, tbl, spans)
        for name, data in (("gfx", gfx), ("tbl", tbl)):
            loose, outside = uncovered(data, spans[name])
            if loose or outside:
                bad.append(f"{code} {name}: {len(loose)} loose bytes (first 0x{loose[0]:X})" if loose else
                           f"{code} {name}: {len(outside)} spans outside the file")
    check(not bad, "every non-zero byte of the 40 stadiums is read" + ("" if not bad else ": " + "; ".join(bad[:6])))


# ---- Task 2: graphics pack -----------------------------------------------------------------------

COLORS = ((0, 0, 0, 255), (204, 204, 204, 255), (0, 0, 0, 255))


def synthetic_stadium():
    """Two models: one with an RGB5A3 and a C4 texture (16-colour palette), a textured and an untextured
    material, a strip mesh and a triangle mesh; one with no textures at all."""
    from powerpros_stadium.stadium_model import Material, Mesh, Model, Palette, Stadium, TexMap, Texture
    rgb = Texture(5, 8, 8, bytes(range(128)))
    c4 = Texture(8, 8, 8, bytes(range(32)))
    pal = Palette(16, bytes(range(100, 132)))
    quad = Mesh(0, [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 0.0, -10.0), (10.0, 0.0, -10.0)], [(0.0, 1.0, 0.0)],
                [(255, 255, 255, 255)], [(0.0, 0.0), (3.0, 0.0), (0.0, 2.0), (3.0, 2.0)],
                [(0x98, [(0, 0, 0, 0, 0), (0, 1, 0, 0, 1), (0, 2, 0, 0, 2), (0, 3, 0, 0, 3)])])
    tri = Mesh(1, [(0.0, 5.0, 0.0), (1.0, 5.0, 0.0), (0.0, 6.0, 0.0)], [(0.0, 0.0, 1.0)], [(90, 90, 90, 255)],
               [(0.0, 0.0)], [(0x90, [(0, 0, 0, 0, 0), (0, 1, 0, 0, 0), (0, 2, 0, 0, 0)])])
    a = Model("DP_test_a", "_Wmodel_DP_test_a", "pPlane1", [(0, None, 0), (1, 0, 0)], [TexMap(0, 3, 2), TexMap(1, 1, 1)],
              [Material(3, COLORS, [0], 1), Material(3, COLORS, [], 0)], [quad, tri])
    b = Model("DP_test_b", "_Wmodel_DP_test_b", "pPlane2", [], [], [Material(3, COLORS, [], 1)],
              [Mesh(0, tri.positions, tri.normals, tri.colors, tri.uvs, tri.draws)])
    return Stadium(textures=[rgb, c4], palettes=[pal], models=[a, b])


def test_graphics_synthetic():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import stadium_re
    from powerpros_stadium import pj7m
    from powerpros_stadium.stadium_write import write_graphics
    st = synthetic_stadium()
    gfx, layout = write_graphics(st)
    offsets = pj7m.find_models(gfx)
    check(offsets == layout.models and len(offsets) == 2, f"two models found where the layout says ({offsets})")
    for model, off in zip(st.models, offsets):
        m = pj7m.parse_model(gfx, off)
        check(m.name == model.name and m.wname == model.wname and m.node_name == model.node_name, f"{model.name}: names")
        check([d for x in m.meshes for d in x.draws] == [(op, [tuple(v) for v in vs]) for x in model.meshes for op, vs in x.draws],
              f"{model.name}: draws")
        check([pj7m.array(gfx, x, "pos") for x in m.meshes] == [x.positions for x in model.meshes], f"{model.name}: positions")
        check([len(x.texmaps) for x in m.materials] == [len(x.texmaps) for x in model.materials], f"{model.name}: material slots")
        check([(t.fmt, t.width, t.height) for t in m.textures] == [(st.textures[i].fmt, st.textures[i].width, st.textures[i].height)
                                                                    for i, _, _ in model.textures], f"{model.name}: textures")
        problems = []
        stadium_re.check_model(gfx, m, problems)
        check(not problems, f"{model.name}: stadium_re.check_model {problems[:3]}")
        aligned = all(x.dl % 32 == 0 and all(v % 32 == 0 for v in x.arrays.values() if v) for x in m.meshes)
        aligned &= all(t.pixels % 32 == 0 and (t.palette is None or t.palette % 32 == 0) for t in m.textures)
        check(aligned, f"{model.name}: display lists, arrays, pixels and colours on 32 bytes")


TEXTURE_KINDS = ("texture_header", "texture_pixels", "texture_tail", "palette_header", "palette_colors")
GRAPHICS_ONLY_SKIP = ("texture_tail", "palette_header", "palette_colors")    # compared with the table, Task 3


def masked(data, spans, skip=()):
    """{(kind, key): bytes with pointer fields zeroed} for model structures, and sorted lists of masked
    bytes per texture kind (texture order in the file may differ)."""
    by_key, by_kind = {}, {}
    for s in spans:
        if s.kind in skip:
            continue
        b = bytearray(data[s.start:s.end])
        for p in s.pointers:
            b[p:p + 4] = b"\0\0\0\0"
        if s.kind in TEXTURE_KINDS:
            by_kind.setdefault(s.kind, []).append(bytes(b))
        else:
            by_key[(s.kind, s.key)] = bytes(b)
    return by_key, {k: sorted(v) for k, v in by_kind.items()}


def compare_structures(code, orig, new, label):
    """Mismatch descriptions between two masked() results."""
    bad = []
    keys = set(orig[0]) | set(new[0])
    for k in sorted(keys, key=str):
        if orig[0].get(k) != new[0].get(k):
            a, b = orig[0].get(k), new[0].get(k)
            at = next((i for i in range(min(len(a or b""), len(b or b""))) if a[i] != b[i]), None)
            bad.append(f"{code} {label} {k[0]}{k[1]}: " + ("missing" if a is None or b is None else
                       f"differs at +0x{at:X}" if at is not None else f"size {len(a)} vs {len(b)}"))
    for kind in set(orig[1]) | set(new[1]):
        if orig[1].get(kind) != new[1].get(kind):
            bad.append(f"{code} {label} {kind}: {len(orig[1].get(kind, []))} vs {len(new[1].get(kind, []))} differ")
    return bad


def test_graphics_bytes_match_game(archive):
    from powerpros_stadium.stadium_read import read_graphics, read_stadium
    from powerpros_stadium.stadium_write import write_graphics
    bad = []
    for code in stadium_codes(archive):
        gfx, tbl = packs(archive, code)
        st = read_stadium(gfx, tbl)
        new, _ = write_graphics(st)
        a, b = {"gfx": []}, {"gfx": []}
        read_graphics(gfx, a)
        read_graphics(new, b)
        bad += compare_structures(code, masked(gfx, a["gfx"], GRAPHICS_ONLY_SKIP), masked(new, b["gfx"], GRAPHICS_ONLY_SKIP), "gfx")
    check(not bad, "graphics structures match the game's, pointers aside" + ("" if not bad else ": " + "; ".join(bad[:8])))


# ---- Task 3: table -------------------------------------------------------------------------------

def placement(model, x, z):
    from powerpros_stadium.stadium_model import Placement
    return Placement(model, (x, 0.0, z), (0.0, 0.5, 0.0), (1.0, 1.0, 1.0), (x - 5.0, 0.0, z - 5.0, x + 5.0, 6.0, z + 5.0),
                     (x, 3.0, z, 8.0), 0x00001100)


def table_stadium():
    from powerpros_stadium.stadium_model import PointGroup, PreloadEntry
    st = synthetic_stadium()
    st.placements = [placement(0, 0.0, 0.0), placement(1, 100.0, -50.0)]       # one per model, as in the game
    st.collision_vertices = [(-10.0, 0.0, 10.0), (10.0, 0.0, 10.0), (10.0, 0.0, -10.0), (-10.0, 0.0, -10.0), (0.0, 5.0, -10.0)]
    st.collision_triangles = [(0, 1, 2, 5), (0, 2, 3, 5), (2, 3, 4, 9), (3, 4, 0, 1)]
    st.unk_20 = [PointGroup([(1.0, 2.0, 3.0, 0.0, 0.5, 0.0), (4.0, 5.0, 6.0, 0.0, -0.5, 0.0)], (1.0, 2.0, 3.0, 4.0, 5.0, 6.0))]
    st.unk_24 = [[(240.0, -6.5, 37.5, 1.0, 1.0, 1.0, -1.75)], [(-240.0, -6.5, 37.5, 1.0, 1.0, 1.0, 1.75)]]
    st.preload_order = [0, 1]
    st.preload = [PreloadEntry(0, None, 0xAC800, 0), PreloadEntry(1, 0, 0xAB800, 0xB2540)]
    st.preload_tail = 0x6F2E80
    return st


def test_table_synthetic():
    import stadium_re
    from powerpros_stadium import stadium_tbl
    from powerpros_stadium.stadium_read import read_stadium
    from powerpros_stadium.stadium_write import write_stadium
    st = table_stadium()
    gfx, tbl = write_stadium(st)
    root = stadium_tbl.find_root(tbl)
    check(struct_u32(tbl, root) == 2 and struct_u32(tbl, root + 4) == 0x40000020, "root found from the end, placements at 0x20")
    back = read_stadium(gfx, tbl)
    check(back == st, "the written stadium reads back the same" + ("" if back == st else f": {first_difference(st, back)}"))
    problems = []
    stadium_re.load("synthetic", gfx, tbl, problems)
    check(not problems, f"stadium_re.load {problems[:3]}")


def struct_u32(d, o):
    import struct
    return struct.unpack_from(">I", d, o)[0]


def first_difference(a, b):
    from dataclasses import fields, is_dataclass
    if is_dataclass(a) and is_dataclass(b):
        for f in fields(a):
            x, y = getattr(a, f.name), getattr(b, f.name)
            if x != y:
                return f"{f.name}: " + first_difference(x, y)
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return f"length {len(a)} vs {len(b)}"
        i = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
        return f"[{i}] " + first_difference(a[i], b[i])
    return f"{a!r:.80} vs {b!r:.80}"


def test_table_empty_blocks():
    from powerpros_stadium import stadium_tbl
    from powerpros_stadium.stadium_read import read_stadium
    from powerpros_stadium.stadium_write import write_stadium
    st = table_stadium()
    st.unk_20, st.unk_24, st.preload_order, st.preload = [], [[], []], [], []
    gfx, tbl = write_stadium(st)
    pointers = stadium_tbl.parse_table(tbl).pointers
    check(all(v is not None for v in pointers.values()), "empty blocks are written present, no null root pointer")
    check(read_stadium(gfx, tbl) == st, "empty blocks read back the same")
    st.unk_24 = [[]]                                  # bullpen: a single pair, the root right after it
    check(read_stadium(*write_stadium(st)).unk_24 == [[]], "a single unk_24 pair reads back the same")


def test_table_many_placements():
    import stadium_re
    from powerpros_stadium import stadium_tbl
    from powerpros_stadium.stadium_write import write_stadium
    st = table_stadium()
    st.placements = [placement(0, 30.0 * (i % 14), -30.0 * (i // 14)) for i in range(140)]
    gfx, tbl = write_stadium(st)
    t = stadium_tbl.parse_table(tbl)
    nodes = stadium_tbl.parse_tree(tbl, t.pointers["planes_a"], t.pointers["lists_a"])
    problems = []
    stadium_re.check_trees("many", tbl, t, problems)
    check(len(nodes) > 1 and not problems, f"140 placements: tree A split into {len(nodes)} nodes, {problems[:2]}")


def test_write_limits():
    from powerpros_stadium.stadium_model import TexMap
    from powerpros_stadium.stadium_write import StadiumWriteError, write_stadium
    cases = []
    st = table_stadium()
    st.models[0].texmaps = [TexMap(0, 1, 1)] * 31
    cases.append((st, "DP_test_a"))
    st = table_stadium()
    st.collision_triangles = [(0, 1, 2, 5)] * 65536
    cases.append((st, "65535"))
    st = table_stadium()
    st.placements = [placement(0, 0.0, 0.0)] * 4096
    cases.append((st, "4095"))
    for st, word in cases:
        try:
            write_stadium(st)
            check(False, f"limit with '{word}' refused")
        except StadiumWriteError as e:
            check(word in str(e), f"limit refused, naming '{word}': {e}")


TREES = ("tree_a", "tree_a_lists", "tree_b", "tree_b_lists")


def test_table_bytes_match_game(archive):
    from powerpros_stadium.stadium_read import read_stadium
    from powerpros_stadium.stadium_write import write_stadium
    bad = []
    for code in stadium_codes(archive):
        gfx, tbl = packs(archive, code)
        a, b = {"gfx": [], "tbl": []}, {"gfx": [], "tbl": []}
        st = read_stadium(gfx, tbl, a)
        new_gfx, new_tbl = write_stadium(st)
        read_stadium(new_gfx, new_tbl, b)
        bad += compare_structures(code, masked(gfx, a["gfx"]), masked(new_gfx, b["gfx"]), "gfx")
        bad += compare_structures(code, masked(tbl, a["tbl"], TREES), masked(new_tbl, b["tbl"], TREES), "tbl")
    check(not bad, "table and graphics structures match the game's, pointers and trees aside"
          + ("" if not bad else ": " + "; ".join(bad[:8])))


SYNTHETIC = [test_graphics_synthetic, test_table_synthetic, test_table_empty_blocks, test_table_many_placements,
             test_write_limits]
GAME = [test_reader_known_values, test_reader_covers_every_byte, test_graphics_bytes_match_game,
        test_table_bytes_match_game]


def main(argv):
    for test in SYNTHETIC:
        try:
            test()
        except Exception as e:
            check(False, f"{test.__name__} raised {type(e).__name__}: {e}")
    if len(argv) > 1:
        from projectbin import Archive
        archive = Archive(argv[1])
        for test in GAME:
            try:
                test(archive)
            except Exception as e:          # an error in one test must not hide the others
                check(False, f"{test.__name__} raised {type(e).__name__}: {e}")
    print("RESULT: " + ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
