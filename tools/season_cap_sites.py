# SPDX-License-Identifier: GPL-3.0-or-later
"""Find the Season mode cap compares: cmpwi/cmplwi with 9 or 10 right after reading the season index.

python tools/season_cap_sites.py DUMP.mem1   (a MEM1 dump from the season probe, REL loaded)

The index getter 0x80B74BCC is a 5-instruction leaf, so the translator inlines it, but the machine
code still has `bl 0x80B74BCC`; the compare follows within two instructions (an `mr` may sit in
between). Prints the instruction addresses for translation.immediate_overrides.
Plan: docs/superpowers/plans/2026-10-19-season-20-build.md
"""
import struct
import sys

GET_INDEX = 0x80B74BCC
CAP_VALUES = (9, 10)
MEM1 = 0x80000000


def sites_in(base: int, data: bytes) -> list[tuple[int, str, int]]:
    """(address, mnemonic, immediate) of each cap compare in data, loaded at base."""
    words = struct.unpack(f">{len(data) // 4}I", data[:len(data) // 4 * 4])
    found = []
    for i, w in enumerate(words):
        at = base + 4 * i
        if w & 0xFC000003 != 0x48000001:  # bl
            continue
        disp = w & 0x03FFFFFC
        if disp & 0x02000000:
            disp -= 0x04000000
        if (at + disp) & 0xFFFFFFFF != GET_INDEX:
            continue
        for j in (1, 2):
            if i + j >= len(words):
                break
            nxt = words[i + j]
            op = nxt >> 26
            if op in (10, 11) and not (nxt >> 21) & 1:  # cmpli / cmpi, 32-bit (L = 0), any CR field
                imm = nxt & 0xFFFF
                if op == 11 and imm & 0x8000:
                    imm -= 0x10000
                if imm in CAP_VALUES:
                    found.append((at + 4 * j, "cmpwi" if op == 11 else "cmplwi", imm))
                break
            if nxt >> 26 == 18 or nxt & 0xFC000001 == 0x48000001:
                break
    return found


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    data = open(argv[0], "rb").read()
    start, end = 0x80575580, 0x81000000  # ovl_main.rel text, loaded
    for address, mnemonic, imm in sites_in(start, data[start - MEM1:end - MEM1]):
        print(f"0x{address:08X} {mnemonic} {imm}")


if __name__ == "__main__":
    main(sys.argv[1:])
