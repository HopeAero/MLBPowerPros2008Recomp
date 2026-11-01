# SPDX-License-Identifier: GPL-3.0-or-later
"""CLI integration tests with synthetic audio only; Python 3.11+ and ffmpeg."""
import argparse
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import tomllib
import wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--ffmpeg", default="C:/ffmpeg/bin/ffmpeg.exe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="callname-studio-cli-") as temp:
        root = Path(temp)
        mods, config = root / "mods", root / "Config.toml"
        normal = root / "siné.wav"
        with wave.open(str(normal), "wb") as out:
            out.setparams((1, 2, 22050, 0, "NONE", "not compressed"))
            out.writeframes(b"".join(struct.pack("<h", int(10000 * math.sin(i * 0.1))) for i in range(4410)))
        mp3, long = root / "sine.mp3", root / "long.mp3"
        subprocess.run([args.ffmpeg, "-v", "error", "-i", str(normal), str(mp3)], check=True)
        subprocess.run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=5.2", str(long)], check=True)

        def run(*options, ok=True):
            result = subprocess.run([str(args.exe.resolve()), "--mods", str(mods), "--config", str(config), *map(str, options)], capture_output=True)
            assert (result.returncode == 0) == ok, (options, result.stdout, result.stderr)
            return result

        run("--mod", "Prueba ñ", "--create-mod", "--add", "--list", "Acuña", "--normal", normal, "--full", mp3)
        file = mods / "Prueba ñ/callnames/callnames.toml"
        doc = tomllib.loads(file.read_text(encoding="utf-8"))
        assert doc["callname"][0]["voice"] == 7223
        assert "Prueba ñ" in tomllib.loads(config.read_text(encoding="utf-8"))["mods"]["enabled"]
        run("--mod", "Dos", "--create-mod", "--add", "--list", "Segundo", "--normal", mp3)
        assert tomllib.loads((mods / "Dos/callnames/callnames.toml").read_text())["callname"][0]["voice"] == 7224
        run("--mod", "Prueba ñ", "--validate")
        run("--mod", "Dos", "--update", 0, "--list", "O'Neil")
        assert tomllib.loads((mods / "Dos/callnames/callnames.toml").read_text())["callname"][0]["list"] == "O'Neil"
        before = file.read_bytes()
        run("--mod", "Prueba ñ", "--add", "--list", "TooLong", "--normal", long, ok=False)
        run("--mod", "Prueba ñ", "--add", "--list", "A" * 16, "--normal", normal, ok=False)
        assert file.read_bytes() == before
        run("--mod", "Dos", "--delete", 0)
        assert tomllib.loads((mods / "Dos/callnames/callnames.toml").read_text())["callname"] == []
        run("--mod", "../escape", "--create-mod", ok=False)
        print("PASS CLI: WAV/MP3, UTF-8 paths, two mods, allocation, update/delete, rejected MP3 duration/text, unchanged manifest on error")


if __name__ == "__main__":
    main()
