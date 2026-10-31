#!/usr/bin/env python3
"""Checks that a profile-use build can find and consume its profile (run it after configuring the use
build, before or after compiling).

    python pgo_check.py <use build dir> <profile dir> [--sample N] [--msys D:/msys64]

1. Coverage: every object the use build compiles with -fprofile-use must have its .gcda in the profile
   directory (name = the object path relative to the build dir, extension dropped, '/' -> '#').
   TUs the game never loaded have no file: -Wno-missing-profile hides that in the build, here it is a count.
2. --sample N: recompiles N of the biggest-profile TUs again (the object file is restored afterwards) with -w removed and -Wmissing-profile
   -Wcoverage-mismatch -Werror=coverage-mismatch, so a profile GCC discards (source changed since the
   training run: the translated shards compile with -w, which hides it) is reported.
Exit status 1 when a sampled TU has a mismatch or when more than 5 percent of the objects lack a profile.
"""
import argparse
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys

ap = argparse.ArgumentParser()
ap.add_argument("build", type=pathlib.Path)
ap.add_argument("profile", type=pathlib.Path)
ap.add_argument("--sample", type=int, default=0)
ap.add_argument("--msys", default="D:/msys64")
args = ap.parse_args()


def msys(cmd: str, cwd: str | None = None) -> subprocess.CompletedProcess:
    env_bash = [f"{args.msys}/usr/bin/env", "MSYSTEM=MSYS", "CHERE_INVOKING=1", f"{args.msys}/usr/bin/bash", "-lc"]
    return subprocess.run(env_bash + [cmd], capture_output=True, text=True)


def to_msys(p: pathlib.Path) -> str:
    s = str(p.resolve()).replace("\\", "/")
    return "/" + s[0].lower() + s[2:] if re.match(r"^[A-Za-z]:", s) else s


bld = to_msys(args.build)
r = msys(f"ninja -C {shlex.quote(bld)} -t compdb")
if r.returncode:
    sys.exit(f"ninja -t compdb failed: {r.stderr[:400]}")
db = json.loads(r.stdout)
objs = []
for e in db:
    cmd = e["command"]
    if "-fprofile-use" not in cmd:
        continue
    out = e["output"]
    stem = out.rsplit(".", 1)[0] if "." in pathlib.PurePosixPath(out).name else out
    objs.append((out, stem.replace("/", "#") + ".gcda", e))

have = {p.name: p for p in args.profile.glob("*.gcda")}
missing = [o for o, g, _ in objs if g not in have]
expected = {g for _, g, _ in objs}
extra = sorted(n for n in have if n not in expected)
print(f"objects compiled with -fprofile-use: {len(objs)}")
print(f"profile files: {len(have)} ({sum(p.stat().st_size for p in have.values()) / 1e6:.1f} MB)")
print(f"objects with a profile file: {len(objs) - len(missing)}; without: {len(missing)}")
for o in missing[:15]:
    print("  no profile:", o)
print(f"profile files no object of this build wants: {len(extra)}")
for n in extra[:10]:
    print("  unused:", n)
bad = len(missing) > 0.05 * max(1, len(objs))

if args.sample:
    by_size = sorted((e for e in objs if e[1] in have), key=lambda e: have[e[1]].stat().st_size, reverse=True)
    for out, gcda, e in by_size[: args.sample]:
        cmd = e["command"]
        cmd = re.sub(r"(^| )-w( |$)", r"\1\2", cmd)
        cmd = re.sub(r" -M[DTF]( [^ -][^ ]*)?", "", cmd)
        # the profile is looked up by the object's own name, so the compile writes the real object path;
        # the original (if any) is saved and put back, with its time stamp, so ninja sees no change
        objpath = args.build / out
        saved = objpath.read_bytes() if objpath.exists() else None
        stat = objpath.stat() if saved is not None else None
        cmd += " -Wmissing-profile -Wcoverage-mismatch -Werror=coverage-mismatch -fdiagnostics-plain-output"
        r = msys(f"cd {shlex.quote(bld)} && {cmd}")
        if saved is not None:
            objpath.write_bytes(saved)
            os.utime(objpath, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        elif objpath.exists():
            objpath.unlink()
        text = r.stderr
        mismatch = len(re.findall(r"coverage-mismatch", text))
        missing_w = len(re.findall(r"Wmissing-profile", text))
        print(f"sample {out}: {'MISMATCH' if mismatch or r.returncode else 'ok'} "
              f"(mismatch warnings {mismatch}, missing-profile {missing_w}, rc {r.returncode})")
        if mismatch or r.returncode:
            bad = True
            print("   ", text.strip().splitlines()[:3])
sys.exit(1 if bad else 0)
