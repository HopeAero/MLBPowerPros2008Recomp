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
sys.path.insert(0, str(HERE))
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

    def test_bridge_import_png_is_handled_in_cpp(self):
        # PNG/DDS import needs no bridge (the editor loads them directly); the bridge only does .fsh/.iff.
        r = subprocess.run([sys.executable, "-I", str(HERE / "studio_bridge.py")],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)  # missing job argument -> usage error


if __name__ == "__main__":
    unittest.main()
