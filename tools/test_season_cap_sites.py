# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of the Season cap compare finder (docs/superpowers/plans/2026-10-19-season-20-build.md, Task 2).

python tools/test_season_cap_sites.py   (synthetic code only). Exits 1 on failure.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from season_cap_sites import GET_INDEX, sites_in

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

sys.exit(1 if failures else 0)
