# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare Season mode saves (pm3usma.dat) to find what the game keeps per year.

python tools/season_save.py diff A.dat B.dat [--gap N] [--stride N]   ranges that differ, with sizes
python tools/season_save.py byte A.dat OFFSET                         one byte (OFFSET in hex or decimal)

Plan: docs/superpowers/plans/2026-10-19-season-archive-research.md. Saves stay under local/tmp/season/.
"""
import sys


def diff(a: bytes, b: bytes, gap: int = 16) -> list[tuple[int, int]]:
    """[start, end) ranges where a and b differ; ranges less than gap bytes apart are joined."""
    n = max(len(a), len(b))
    ranges = []
    i = 0
    while i < n:
        if i < len(a) and i < len(b) and a[i] == b[i]:
            i += 1
            continue
        start = i
        while i < n and not (i < len(a) and i < len(b) and a[i] == b[i]):
            i += 1
        if ranges and start - ranges[-1][1] < gap:
            ranges[-1] = (ranges[-1][0], i)
        else:
            ranges.append((start, i))
    return ranges


def strided(ranges, stride: int) -> list[tuple[int, int, int]]:
    """Ranges repeated every stride bytes with the same size: (start, end, repetitions) of the first."""
    known = set(ranges)
    taken = set()
    groups = []
    for start, end in ranges:
        if (start, end) in taken:
            continue
        reps = 1
        while (start + reps * stride, end + reps * stride) in known:
            taken.add((start + reps * stride, end + reps * stride))
            reps += 1
        if reps > 1:
            groups.append((start, end, reps))
    return groups


def main(argv):
    if len(argv) >= 3 and argv[0] == "diff":
        opts = dict(zip(argv[3::2], argv[4::2]))
        a, b = open(argv[1], "rb").read(), open(argv[2], "rb").read()
        ranges = diff(a, b, int(opts.get("--gap", 16)))
        for start, end in ranges:
            print(f"0x{start:06X}-0x{end:06X} {end - start:6d}")
        print(f"{len(ranges)} ranges, {sum(e - s for s, e in ranges)} bytes")
        if "--stride" in opts:
            for start, end, reps in strided(ranges, int(opts["--stride"], 0)):
                print(f"table 0x{start:06X}-0x{end:06X} x{reps}")
    elif len(argv) == 3 and argv[0] == "byte":
        data = open(argv[1], "rb").read()
        offset = int(argv[2], 0)
        print(f"0x{offset:06X}: 0x{data[offset]:02X} ({data[offset]})")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
