# SPDX-License-Identifier: GPL-3.0-or-later
"""Sends an NRO to a Switch's homebrew menu netloader over Wi-Fi, launches it, and prints what it
writes to stdout here (also to local/switch/logs/nxlink-<name>-<time>.log) until it exits.

On the Switch: homebrew menu (holding R over a game gives it full memory), press Y; the screen
shows the console's IP. Then, on the PC:

    python tools/switch/nxlink.py 192.168.0.105 local/switch/hello/hello.nro [--path mlbpowerpros2008/x.nro] [args...]

--path is relative to sdmc:/switch/ (default mlbpowerpros2008/<file name>): the netloader (hbmenu or
Sphaira) puts a relative name under /switch/ and launches it; Sphaira saves but does not launch a
name it cannot open, such as sdmc:/... (and Git Bash rewrites a leading /switch/... into a Windows path).

The protocol of devkitPro's nxlink (switch-tools src/nxlink.c), in plain Python so it runs on
Windows without Docker's network address translation in the way: name and size, the file as a
zlib stream in length-prefixed chunks, the command line, then the NRO connects back to port 28771
with its output. Windows may ask once to let Python accept that connection. A VPN that tunnels
local traffic blocks it: allow local network access in the VPN, or pause it while testing.
"""
import argparse
import datetime
import pathlib
import socket
import struct
import sys
import zlib

SERVER_PORT = 28280  # the netloader on the Switch
CLIENT_PORT = 28771  # where the NRO sends its stdout back
CHUNK = 16 * 1024


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise ConnectionError("the Switch closed the connection")
        data += part
    return data


def recv_int(sock: socket.socket) -> int:
    return struct.unpack("<i", recv_exact(sock, 4))[0]


def send_nro(address: str, path: pathlib.Path, remote_name: str, args: list[str]) -> None:
    payload = path.read_bytes()
    with socket.create_connection((address, SERVER_PORT), timeout=15) as sock:
        name = remote_name.encode()
        sock.sendall(struct.pack("<i", len(name)) + name + struct.pack("<i", len(payload)))
        response = recv_int(sock)
        if response != 0:
            reasons = {-1: "failed to create the file", -2: "insufficient space", -3: "insufficient memory"}
            raise RuntimeError(f"the Switch refused the upload: {reasons.get(response, response)}")
        print(f"Sending {remote_name}, {len(payload)} bytes", flush=True)
        compressor = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION)
        sent = 0
        for offset in range(0, len(payload), CHUNK):
            block = compressor.compress(payload[offset:offset + CHUNK])
            for start in range(0, len(block), CHUNK):
                piece = block[start:start + CHUNK]
                sock.sendall(struct.pack("<i", len(piece)) + piece)
                sent += len(piece)
        tail = compressor.flush()
        for start in range(0, len(tail), CHUNK):
            piece = tail[start:start + CHUNK]
            sock.sendall(struct.pack("<i", len(piece)) + piece)
            sent += len(piece)
        print(f"{sent} sent ({sent * 100 / len(payload):.2f}%)", flush=True)
        recv_int(sock)  # the Switch acknowledges the whole file
        command = b"".join(arg.encode() + b"\0" for arg in args)
        sock.sendall(struct.pack("<i", len(command)) + command)
        sock.shutdown(socket.SHUT_WR)


def serve(log_path: pathlib.Path, wait_seconds: float) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", CLIENT_PORT))
        listener.listen(1)
        listener.settimeout(wait_seconds)
        print("waiting for the NRO's output ...", flush=True)
        try:
            conn, peer = listener.accept()
        except socket.timeout:
            print(f"no connection back in {wait_seconds:.0f} s (the NRO may not send its output over nxlink)")
            return
    with conn, log_path.open("ab") as log:
        conn.settimeout(None)
        while True:
            data = conn.recv(4096)
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
            log.write(data)
            log.flush()
    print("\n[the NRO closed its output]", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("address", help="the Switch's IP, shown on the netloader screen")
    parser.add_argument("nro", type=pathlib.Path)
    parser.add_argument("--path", help="where it goes under sdmc:/switch/ (default: mlbpowerpros2008/<file name>)")
    parser.add_argument("--no-server", action="store_true", help="send and launch only")
    parser.add_argument("--wait", type=float, default=30, help="seconds to wait for the NRO to connect back")
    parser.add_argument("args", nargs="*", help="arguments passed to the NRO")
    options = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    logs = root / "local" / "switch" / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / f"nxlink-{options.nro.stem}-{datetime.datetime.now():%Y%m%d-%H%M%S}.log"
    try:
        remote = options.path or f"mlbpowerpros2008/{options.nro.name}"
        send_nro(options.address, options.nro, remote, options.args)
    except (OSError, RuntimeError, ConnectionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if not options.no_server:
        serve(log_path, options.wait)
        print(f"log: {log_path.relative_to(root)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
