# SPDX-License-Identifier: GPL-3.0-or-later
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_manifest as um


def fake_payload(base: Path):
    """One file under every root of every piece."""
    for roots in um.PIECES.values():
        for r in roots:
            p = base / r
            if Path(r).suffix:  # file root
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b"x" + r.encode())
            else:
                p.mkdir(parents=True, exist_ok=True)
                (p / "f.txt").write_bytes(b"x" + r.encode())
    (base / "update-manifest.txt").write_text("version 0\n")


class UpdateManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name) / "payload"
        self.base.mkdir()
        fake_payload(self.base)
        (self.base / "workspace/src/runtime").mkdir(parents=True, exist_ok=True)
        (self.base / "workspace/src/runtime/a.cpp").write_bytes(b"abc")

    def tearDown(self):
        self.tmp.cleanup()

    def hashes(self):
        return {n: um.content_hash(self.base, r) for n, r in um.PIECES.items()}

    def test_hash_ignores_mtime_and_order(self):
        before = self.hashes()
        os.utime(self.base / "workspace/src/runtime/a.cpp", (1, 1))
        self.assertEqual(before, self.hashes())

    def test_hash_changes_with_content(self):
        before = self.hashes()
        (self.base / "workspace/src/runtime/a.cpp").write_bytes(b"abd")
        after = self.hashes()
        changed = [n for n in before if before[n] != after[n]]
        self.assertEqual(changed, ["core"])

    def test_every_payload_file_in_one_piece(self):
        (self.base / "stray.bin").write_bytes(b"1")
        with self.assertRaises(ValueError) as cm:
            um.check_coverage(self.base)
        self.assertIn("stray.bin", str(cm.exception))

    def test_file_in_two_pieces_is_error(self):
        saved = {k: list(v) for k, v in um.PIECES.items()}
        try:
            um.PIECES["deps"].append("workspace/src")  # overlaps core's workspace/src/runtime
            with self.assertRaises(ValueError) as cm:
                um.check_coverage(self.base)
            self.assertIn("a.cpp", str(cm.exception))
        finally:
            um.PIECES.clear()
            um.PIECES.update(saved)

    def test_release_manifest_lines(self):
        out = Path(self.tmp.name) / "out"
        um.release(self.base, out, "0.1.8", "https://example.invalid/d/1")
        lines = (out / "update-manifest.txt").read_text(encoding="utf-8").splitlines()
        self.assertIn("version 0.1.8", lines)
        self.assertIn("discussion_es https://example.invalid/d/1", lines)
        self.assertIn("discussion_en https://example.invalid/d/1", lines)
        pieces = [l for l in lines if l.startswith("piece ")]
        self.assertEqual(len(pieces), 9)
        for l in pieces:
            self.assertEqual(len(l.split(" ")), 7)  # 'piece' + 6 fields
        self.assertTrue((out / "MLBPP-0.1.8-core.zip").exists())

    def test_kit_lines(self):
        out = Path(self.tmp.name) / "out"
        kits = Path(self.tmp.name) / "kits"
        kits.mkdir()
        (kits / "MLB-Power-Pros-2008-Linux-0.1.8.tar.gz").write_bytes(b"l")
        (kits / "MLB-Power-Pros-2008-SteamDeck-0.1.8.tar.gz").write_bytes(b"s")
        cf = Path(self.tmp.name) / "Containerfile"
        cf.write_bytes(b"FROM x\n")
        um.release(self.base, out, "0.1.8", "d", kits=kits, containerfile=cf)
        lines = (out / "update-manifest.txt").read_text(encoding="utf-8").splitlines()
        kit = [l for l in lines if l.startswith("kit ")]
        self.assertEqual(len(kit), 2)
        self.assertEqual(len(kit[0].split(" ")), 6)

    def test_kits_of_another_version_are_an_error(self):
        out = Path(self.tmp.name) / "out"
        kits = Path(self.tmp.name) / "kits"
        kits.mkdir()
        (kits / "MLB-Power-Pros-2008-Linux-0.1.7.tar.gz").write_bytes(b"l")
        (kits / "MLB-Power-Pros-2008-SteamDeck-0.1.7.tar.gz").write_bytes(b"s")
        (kits / "MLB-Power-Pros-2008-Linux-0.1.8.tar.gz").write_bytes(b"l")  # Steam Deck one missing
        with self.assertRaises(ValueError) as cm:
            um.release(self.base, out, "0.1.8", "d", kits=kits)
        self.assertIn("MLB-Power-Pros-2008-SteamDeck-0.1.8.tar.gz", str(cm.exception))
        self.assertFalse((out / "update-manifest.txt").exists())


if __name__ == "__main__":
    unittest.main()
