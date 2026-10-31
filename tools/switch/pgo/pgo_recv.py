#!/usr/bin/env python3
"""PC side of the PGO profile transfer: receives the .gcda files the instrumented NRO streams
(pgo_core.cpp, MlbPgoStream) and writes them into a directory.

    python pgo_recv.py <out dir> [--port 28772] [--once]

Start it BEFORE pressing the combo (L+R+ZL+ZR+Minus, held 1 s) on the console, or before the game's exit.
Each connection replaces the files it carries (the profile on the SD card is cumulative: the game merges
every dump into it), so the last transfer is the complete profile. Windows asks once to allow Python
through the firewall (private network). Wire format: see pgo_core.cpp.
"""
import argparse
import pathlib
import socket
import struct
import sys
import time
import zlib
import re

SAFE_NAME = re.compile(r"[A-Za-z0-9_#+\-][A-Za-z0-9_#+.\-]*\.gcda")
MAX_FILE = 64 << 20  # one .gcda is a few hundred KB; the whole profile ~20 MB


def recv_exact(conn: socket.socket, n: int) -> bytes:
    parts = []
    while n:
        b = conn.recv(min(n, 1 << 20))
        if not b:
            raise ConnectionError("connection closed early")
        parts.append(b)
        n -= len(b)
    return b"".join(parts)


def receive(conn: socket.socket, out: pathlib.Path) -> tuple[int, int]:
    if recv_exact(conn, 8) != b"MLBPGO1\n":
        raise ValueError("not a PGO stream")
    files = total = 0
    while True:
        (name_len,) = struct.unpack("<H", recv_exact(conn, 2))
        if name_len == 0:
            return files, total
        name = recv_exact(conn, name_len).decode("utf-8", "replace")
        # Only flat .gcda names (no separators, drive letters, ':' streams or dot-files), and the
        # resolved destination must stay directly inside the output directory.
        if not SAFE_NAME.fullmatch(name):
            raise ValueError(f"unsafe name {name!r}")
        dest = (out / name).resolve()
        if dest.parent != out.resolve():
            raise ValueError(f"unsafe name {name!r}")
        (raw_size,) = struct.unpack("<Q", recv_exact(conn, 8))
        if raw_size > MAX_FILE:
            raise ValueError(f"{name}: {raw_size} bytes is over the {MAX_FILE} limit")
        z = zlib.decompressobj()
        data = bytearray()
        while True:
            (chunk,) = struct.unpack("<I", recv_exact(conn, 4))
            if chunk == 0:
                break
            data += z.decompress(recv_exact(conn, chunk), MAX_FILE + 1 - len(data))
            if len(data) > raw_size:
                raise ValueError(f"{name}: more than the announced {raw_size} bytes")
        data += z.flush()
        if len(data) != raw_size:
            raise ValueError(f"{name}: got {len(data)} bytes, expected {raw_size}")
        tmp = dest.with_name(dest.name + ".part")
        tmp.write_bytes(data)
        tmp.replace(dest)
        files += 1
        total += raw_size


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--port", type=int, default=28772)
    ap.add_argument("--once", action="store_true", help="exit after one complete transfer")
    ap.add_argument("--allow", help="only accept connections from this IP (the Switch)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(1)
    print(f"waiting for the console on port {args.port}, writing to {args.out}", flush=True)
    while True:
        conn, peer = srv.accept()
        if args.allow and peer[0] != args.allow:
            print(f"refused connection from {peer[0]} (only {args.allow} allowed)", flush=True)
            conn.close()
            continue
        t0 = time.time()
        with conn:
            try:
                files, total = receive(conn, args.out)
            except Exception as ex:  # keep listening: the user can press the combo again
                print(f"transfer from {peer[0]} failed: {ex}", flush=True)
                continue
        print(f"{peer[0]}: {files} files, {total / 1e6:.1f} MB in {time.time() - t0:.1f} s", flush=True)
        if args.once:
            return 0


if __name__ == "__main__":
    sys.exit(main())
