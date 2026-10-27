# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns the [shot] lines of an nxlink log (WIICOMPILED_DK_SHOT=<n>) into PNGs.

    python tools/switch/deko/shot.py <nxlink log> <output folder>

Each capture becomes <tag>-<frame>.png. Captures are of your own console's screen: keep them under local/.
"""
import base64, pathlib, re, struct, sys, zlib


def png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    cur = None
    for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
        if m := re.search(r"\[shot\] begin (\S+) frame=(\d+) (\d+)x(\d+)", line):
            cur = (m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), [])
        elif cur and (at := line.find("[shot] d ")) >= 0:
            # (another thread's message can share the line: the data is the base64 run after the marker)
            cur[4].append(re.match(r"[A-Za-z0-9+/=]*", line[at + 9:]).group(0))
        elif cur and line.startswith("[shot] end"):
            tag, frame, w, h, parts = cur
            rgb = base64.b64decode("".join(parts))
            if len(rgb) == w * h * 3:
                path = out / f"{tag}-{frame}.png"
                png(path, w, h, rgb)
                print(path)
            else:
                print(f"{tag} frame {frame}: {len(rgb)} bytes, expected {w * h * 3} (cut short)")
            cur = None


if __name__ == "__main__":
    main()
