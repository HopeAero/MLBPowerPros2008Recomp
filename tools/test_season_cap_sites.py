# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the Season cap compare finder (docs/superpowers/plans/2026-10-19-season-20-build.md, Task 2).

python tools/test_season_cap_sites.py   (synthetic code only). Exits 1 on failure.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from season_cap_sites import GET_INDEX, index_year_sites_in, sites_in, year_sites_in

failures = []


def check(ok, label):
    print(("PASS: " if ok else "FAIL: ") + label)
    if not ok:
        failures.append(label)


def bl(at, target):
    return 0x48000001 | ((target - at) & 0x03FFFFFC)


def code(base, *words):
    return base, b"".join(struct.pack(">I", w) for w in words)


B = 0x80BF51B8
check(sites_in(*code(B, bl(B, GET_INDEX), 0x2C030009)) == [(B + 4, "cmpwi", 9)], "bl get-index; cmpwi r3, 9")
check(sites_in(*code(B, bl(B, GET_INDEX), 0x7C7F1B78, 0x2C1F000A)) == [(B + 8, "cmpwi", 10)],
      "mr r31, r3 in between; cmpwi r31, 10")
check(sites_in(*code(B, bl(B, GET_INDEX), 0x2803000A)) == [(B + 4, "cmplwi", 10)], "cmplwi r3, 10")
check(sites_in(*code(B, bl(B, GET_INDEX), 0x2C030000)) == [], "compare with 0 is not the cap")
check(sites_in(*code(B, bl(B, 0x80B74BA4), 0x2C030009)) == [], "another call before cmpwi 9")
check(sites_in(*code(B, bl(B, GET_INDEX), 0x38600000, 0x38600000, 0x38600000, 0x2C030009)) == [],
      "compare too far from the read")

# Year sites: addi rD, rA, 2008 (rA != 0) adds the season index to 2008, li/cmpwi 2008 clamp it; the calendar clamps the
# 2-digit year to 28 with li rD, 28 and cmpwi rA, 28.
F = 0x80B4B300
check(year_sites_in(*code(F, 0x38030000 | 2008), wanted=(2008,)) == [(F, "addi", 2008)], "addi r0, r3, 2008")
check(year_sites_in(*code(F, 0x38000000 | 2008, 0x2C000000 | 2008), wanted=(2007, 2008)) == [(F, "li", 2008), (F + 4, "cmpwi", 2008)],
      "li r0, 2008; cmpwi r0, 2008 (the at-least-2008 clamp)")
check(year_sites_in(*code(F, 0x3800001C, 0x2C03001C), wanted=(28,)) == [(F, "li", 28), (F + 4, "cmpwi", 28)],
      "li r0, 28; cmpwi r3, 28")
check(year_sites_in(*code(F, 0x2C03001C), wanted=(2008,)) == [], "only the wanted immediates")

# Inline years: a call that returns the season index (0x80B4A7DC, the season of a date, or 0x80B74BCC), then
# addi rD, r3, 8 (the 2-digit year) or 2007 / 2008, within three instructions.
S = 0x80BAFBE0
check(index_year_sites_in(*code(S, bl(S, 0x80B4A7DC), 0x57800E38, 0x3BC30008)) == [(S + 8, "addi", 8)],
      "bl 0x80B4A7DC; rlwinm; addi r30, r3, 8")
check(index_year_sites_in(*code(S, bl(S, GET_INDEX), 0x38030000 | 2008)) == [(S + 4, "addi", 2008)],
      "bl get-index; addi r0, r3, 2008")
check(index_year_sites_in(*code(S, bl(S, 0x80B4A7DC), 0x3BC40008)) == [], "addi from another register")
check(index_year_sites_in(*code(S, bl(S, 0x80B4A888), 0x3BC30008)) == [], "after another call")
check(index_year_sites_in(*code(S, bl(S, 0x80B4A7DC), 0x60000000, 0x60000000, 0x60000000, 0x3BC30008)) == [],
      "too far from the call")

sys.exit(1 if failures else 0)
