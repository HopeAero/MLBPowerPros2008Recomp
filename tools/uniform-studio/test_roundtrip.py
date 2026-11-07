# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration test for the editor's save path: compose -> PNG -> studio_bridge.py bake -> read_ppu.

This exercises the same C++ -> Python bridge the editor uses (a JSON job through studio_bridge.py), so a
break in the baker, the bridge, or the .ppu format is caught without building the GUI. Run with the
project's Python: C:/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe -m unittest (from this dir),
or just `python test_roundtrip.py`.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXE = ROOT / "local" / "build" / "uniform-studio" / "UniformStudio.exe"
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
import packwii  # noqa: E402
from bake import read_ppu, write_ppu  # noqa: E402


def atlas(w, h, seed):
    rng = np.random.default_rng(seed)
    a = np.zeros((h, w, 4), np.uint8)
    # A handful of flat colour blocks -> well under 256 colours, and a transparent corner.
    for i in range(6):
        c = rng.integers(0, 255, 3)
        x0, y0 = rng.integers(0, w - 8), rng.integers(0, h - 8)
        a[y0:y0 + 8, x0:x0 + 8, :3] = c
        a[y0:y0 + 8, x0:x0 + 8, 3] = 255
    a[:4, :4] = 0
    return Image.fromarray(a, "RGBA")


class T(unittest.TestCase):
    def test_bridge_bake_roundtrip(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            body, cap = atlas(128, 128, 1), atlas(64, 32, 2)
            body.save(t / "body.png")
            cap.save(t / "cap.png")
            out = t / "u" / "uniform.ppu"
            backup = list(range(10))
            job = t / "job.json"
            job.write_text(json.dumps({
                "op": "bake", "out": str(out), "backup": backup,
                "atlases": {"body": str(t / "body.png"), "cap": str(t / "cap.png")},
            }), encoding="utf-8")

            r = subprocess.run([sys.executable, "-I", str(HERE / "studio_bridge.py"), str(job)],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(out.exists())

            got = read_ppu(out)
            self.assertEqual(got["version"], 1)
            self.assertEqual(got["backup"], backup)
            self.assertEqual(set(got["atlases"]), {"body", "cap"})
            self.assertEqual(got["atlases"]["body"][0:2], (128, 128))
            self.assertEqual(got["atlases"]["cap"][0:2], (64, 32))

            # The bridge must produce byte-identical output to calling write_ppu directly.
            direct = t / "direct.ppu"
            write_ppu(direct, {"body": body, "cap": cap}, backup)
            self.assertEqual(out.read_bytes(), direct.read_bytes())

    def test_decoded_content_survives(self):
        # A known colour on known pixels must survive bake -> read_ppu -> decode (palette entry + C8 index).
        color = (33, 198, 123)
        a = np.zeros((128, 128, 4), np.uint8)
        a[40:56, 40:56, :3] = color
        a[40:56, 40:56, 3] = 255
        with tempfile.TemporaryDirectory() as t:
            ppu = Path(t) / "u.ppu"
            write_ppu(ppu, {"body": Image.fromarray(a, "RGBA")}, [0] * 10)
            w, h, pal, px = read_ppu(ppu)["atlases"]["body"]
            self.assertEqual((w, h), (128, 128))
            dec = packwii.decode(px, w, h, 9, np.frombuffer(pal, ">u2"))  # (h, w, 4) RGBA
            self.assertEqual(int(dec[2, 2, 3]), 0)  # transparent corner stays index 0 -> alpha 0
            self.assertEqual(int(dec[48, 48, 3]), 255)  # the block is opaque
            for got, want in zip(dec[48, 48, :3], color):
                self.assertLessEqual(abs(int(got) - want), 10)  # within RGB5A3 rounding

    @unittest.skipUnless(EXE.exists(), "UniformStudio.exe not built")
    def test_cpp_compose_and_text(self):
        # Exercise the C++ Compose + RasterText via --compose-test (dumps the composed body atlas).
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / "composed.png"
            r = subprocess.run([str(EXE), "--tooldir", str(EXE.parent), "--userdata", t,
                                "--compose-test", str(out)], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = np.array(Image.open(out).convert("RGBA"))
            self.assertEqual(img.shape, (128, 128, 4))
            # The demo logo's centre (placed at x=40,y=18, 48x48) is the gold ring colour, fully opaque.
            px = img[42, 64]
            self.assertEqual(int(px[3]), 255)
            for got, want in zip(px[:3], (250, 210, 60)):
                self.assertLessEqual(abs(int(got) - want), 4)
            # RasterText drew the "24" layer: some opaque pixels in its lower region.
            self.assertGreater(int((img[66:110, 35:100, 3] > 0).sum()), 0)

    def test_bridge_import_png_is_handled_in_cpp(self):
        # PNG/DDS import needs no bridge (the editor loads them directly); the bridge only does .fsh/.iff.
        r = subprocess.run([sys.executable, "-I", str(HERE / "studio_bridge.py")],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)  # missing job argument -> usage error


if __name__ == "__main__":
    unittest.main()
