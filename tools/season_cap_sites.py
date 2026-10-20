# SPDX-License-Identifier: GPL-3.0-or-later
"""Find the Season mode cap compares: cmpwi/cmplwi with 9 or 10 right after reading the season index.

python tools/season_cap_sites.py DUMP.mem1          (a MEM1 dump from the season probe, REL loaded)
python tools/season_cap_sites.py --years DUMP.mem1  (the year sites, for seasons past 2027)

The index getter 0x80B74BCC is a 5-instruction leaf, so the translator inlines it, but the machine
code still has `bl 0x80B74BCC`; the compare follows within two instructions (an `mr` may sit in
between). Prints the instruction addresses for translation.immediate_overrides.

--years: the instructions that turn the season index into a year: every 2007 / 2008 immediate
(addi, li, cmpwi) in the functions of YEAR_FUNCTIONS, and the calendar's clamp of the 2-digit year (index + 8) to 28
(`li rD, 28` and `cmpwi rA, 28`) in CLAMP_FUNCTIONS. The function list is local/fnlist.txt.
Plan: docs/superpowers/plans/2026-10-19-season-20-build.md
"""
import struct
import sys
from pathlib import Path

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


# 2008 + index: 0x80B4A730 (the season's year for a date: 2007 or 2008 + index, at least 2008), the UI objects at 0x8087B668 / 0x8087E224, and
# 0x80ADC620 (a year picker: 2008 + the picked index, against 0x80B4A888).
YEAR_FUNCTIONS = (0x80B4A730, 0x8087B668, 0x8087E224, 0x80ADC620)
# The calendar clamps index + 8 to 8..28 before its day-of-week formula.
CLAMP_FUNCTIONS = (0x80B4AEE4, 0x80B4B0FC, 0x80B4B2C0, 0x80B4B604, 0x80B4B798, 0x80B4BF28, 0x80B4C0AC,
                   0x80B4C87C, 0x80B4CA10, 0x80B4CB84)


def year_sites_in(base: int, data: bytes, wanted: tuple[int, ...]) -> list[tuple[int, str, int]]:
    """(address, mnemonic, immediate) of each addi (rA != 0), li or cmpwi with a wanted immediate."""
    found = []
    for i, w in enumerate(struct.unpack(f">{len(data) // 4}I", data[:len(data) // 4 * 4])):
        op, ra, imm = w >> 26, (w >> 16) & 31, w & 0xFFFF
        if imm & 0x8000:
            imm -= 0x10000
        if imm not in wanted:
            continue
        if op == 14:  # addi; rA = 0 is li
            found.append((base + 4 * i, "addi" if ra else "li", imm))
        elif op == 11 and not (w >> 21) & 1:
            found.append((base + 4 * i, "cmpwi", imm))
    return found


# Calls that return the season index: 0x80B74BCC, and 0x80B4A7DC (the season of a date, index or index - 1).
INDEX_CALLS = (GET_INDEX, 0x80B4A7DC)


def index_year_sites_in(base: int, data: bytes) -> list[tuple[int, str, int]]:
    """(address, "addi", imm) of each addi rD, r3, 8 / 2007 / 2008 within three instructions after a call
    in INDEX_CALLS: a 2-digit or 4-digit year made from the index on the spot."""
    words = struct.unpack(f">{len(data) // 4}I", data[:len(data) // 4 * 4])
    found = []
    for i, w in enumerate(words):
        at = base + 4 * i
        if w & 0xFC000003 != 0x48000001:
            continue
        disp = w & 0x03FFFFFC
        if disp & 0x02000000:
            disp -= 0x04000000
        if (at + disp) & 0xFFFFFFFF not in INDEX_CALLS:
            continue
        for j in (1, 2, 3):
            if i + j >= len(words):
                break
            nxt = words[i + j]
            if nxt >> 26 == 14 and (nxt >> 16) & 31 == 3 and nxt & 0xFFFF in (8, 2007, 2008):
                found.append((at + 4 * j, "addi", nxt & 0xFFFF))
                break
            if nxt >> 26 in (16, 18, 19) or (nxt >> 21) & 31 == 3:  # a branch, or r3 written
                break
    return found


def function_end(start: int, starts: list[int]) -> int:
    return min(a for a in starts if a > start)


def main(argv):
    years = argv[:1] == ["--years"]
    argv = argv[1:] if years else argv
    if len(argv) != 1:
        sys.exit(__doc__)
    data = open(argv[0], "rb").read()
    if years:
        fnlist = Path(__file__).resolve().parent.parent / "local" / "fnlist.txt"
        starts = sorted(int(line, 16) for line in fnlist.read_text().split())
        for functions, wanted in ((YEAR_FUNCTIONS, (2007, 2008)), (CLAMP_FUNCTIONS, (28,))):
            for start in functions:
                end = function_end(start, starts)
                for address, mnemonic, imm in year_sites_in(start, data[start - MEM1:end - MEM1], wanted):
                    print(f"0x{address:08X} {mnemonic} {imm}")
        start, end = 0x80575580, 0x81000000
        for address, mnemonic, imm in index_year_sites_in(start, data[start - MEM1:end - MEM1]):
            print(f"0x{address:08X} {mnemonic} {imm}")
        return
    start, end = 0x80575580, 0x81000000  # ovl_main.rel text, loaded
    for address, mnemonic, imm in sites_in(start, data[start - MEM1:end - MEM1]):
        print(f"0x{address:08X} {mnemonic} {imm}")


if __name__ == "__main__":
    main(sys.argv[1:])
