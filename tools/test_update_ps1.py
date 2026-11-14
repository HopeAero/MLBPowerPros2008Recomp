# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/setup/Update.ps1 (apply, swap, rollback) on fake installs. -Phase build is not tested here."""
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from update_manifest import sha256_file  # noqa: E402

SCRIPT = Path(__file__).resolve().parent / "setup" / "Update.ps1"
OLD_TIME = 946684800  # 2000-01-01


def write(path: Path, text: str, mtime=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    if mtime:
        os.utime(path, (mtime, mtime))


class UpdateBase(unittest.TestCase):
    folder = "install"

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="upd-"))
        self.install = self.tmp / self.folder
        b = self.install / "Build"
        write(b / "workspace" / "VERSION", "0.1.7\n")
        write(b / "update-manifest.txt", "version 0.1.7\n")
        g = self.install / "Game"
        write(g / "MLBPowerPros2008.exe", "old exe")
        write(g / "launcher" / "l.txt", "old launcher")
        write(g / "portable.txt", "p")
        write(g / "version.txt", "0.1.7\n")
        write(g / "mods" / "y", "mod")
        write(g / "UserData" / "NAND" / "x", "save1")
        write(g / "UserData" / "ModSaves" / "m", "ms")
        write(g / "UserData" / "arrange" / "a", "ar")
        write(g / "UserData" / "Config.toml", "cfg")
        write(g / "UserData" / "Cache" / "c", "cache")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_ps(self, phase, *extra, downloads=None, relaunch_hook=None):
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(SCRIPT),
               "-Install", str(self.install), "-Phase", phase, "-Lang", "en"]
        if downloads:
            cmd += ["-Downloads", str(downloads)]
        cmd += list(extra)
        env = dict(os.environ)
        env.pop("MLBPP_UPDATE_TEST_RELAUNCH", None)
        if relaunch_hook:
            env["MLBPP_UPDATE_TEST_RELAUNCH"] = str(relaunch_hook)
        return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)

    def make_download(self, files, version="0.1.8", tamper=False):
        d = self.install / "Update" / "downloads"
        d.mkdir(parents=True, exist_ok=True)
        zp = d / f"MLBPP-{version}-core.zip"
        with zipfile.ZipFile(zp, "w") as z:
            for rel, text in files.items():
                z.writestr(rel, text)
        sha = sha256_file(zp)
        if tamper:
            with zipfile.ZipFile(zp, "w") as z:
                z.writestr("workspace/config/evil.txt", "x")
        (d / "update-manifest.txt").write_bytes(
            (f"version {version}\npiece core {zp.name} {sha} {zp.stat().st_size} 0 workspace/config;workspace/VERSION\n").encode())
        return d

    def stage(self):
        s = self.install / "Staging"
        write(s / "MLBPowerPros2008.exe", "new exe")
        write(s / "launcher" / "l.txt", "new launcher")
        write(s / "SDL3.dll", "sdl")
        write(s / "version.txt", "0.1.8\n")


class ApplyTests(UpdateBase):
    def test_apply_keeps_mtime_of_unchanged(self):
        cfg = self.install / "Build" / "workspace" / "config"
        write(cfg / "same.txt", "same", OLD_TIME)
        write(cfg / "changed.txt", "old", OLD_TIME)
        write(cfg / "gone.txt", "bye", OLD_TIME)
        keep = self.install / "Build" / "workspace" / "build" / "keep.o"
        write(keep, "obj", OLD_TIME)
        d = self.make_download({"workspace/config/same.txt": "same", "workspace/config/changed.txt": "new",
                                "workspace/VERSION": "0.1.8\n"})
        r = self.run_ps("apply", downloads=d)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(int((cfg / "same.txt").stat().st_mtime), OLD_TIME)
        self.assertEqual((cfg / "changed.txt").read_text(), "new")
        self.assertGreater(cfg.joinpath("changed.txt").stat().st_mtime, time.time() - 300)
        self.assertFalse((cfg / "gone.txt").exists())
        self.assertEqual(int(keep.stat().st_mtime), OLD_TIME)
        self.assertIn("version 0.1.8", (self.install / "Build" / "update-manifest.txt").read_text())

    def test_apply_rejects_bad_sha(self):
        cfg = self.install / "Build" / "workspace" / "config"
        write(cfg / "a.txt", "a", OLD_TIME)
        d = self.make_download({"workspace/config/a.txt": "b"}, tamper=True)
        r = self.run_ps("apply", downloads=d)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("@@ERROR", r.stdout)
        self.assertEqual((cfg / "a.txt").read_text(), "a")
        self.assertFalse((cfg / "evil.txt").exists())
        self.assertEqual((self.install / "Build" / "update-manifest.txt").read_text(), "version 0.1.7\n")


class SwapTests(UpdateBase):
    def test_swap_moves_program_only(self):
        self.stage()
        r = self.run_ps("swap")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        g, p, b = (self.install / n for n in ("Game", "Previous", "Backup"))
        self.assertEqual((g / "MLBPowerPros2008.exe").read_text(), "new exe")
        self.assertEqual((p / "MLBPowerPros2008.exe").read_text(), "old exe")
        self.assertEqual((p / "launcher" / "l.txt").read_text(), "old launcher")
        self.assertEqual((g / "UserData" / "NAND" / "x").read_text(), "save1")
        self.assertEqual((g / "mods" / "y").read_text(), "mod")
        self.assertTrue((g / "portable.txt").exists())
        for n in ("NAND", "ModSaves", "arrange", "Config.toml"):
            self.assertTrue((b / n).exists(), n)
        self.assertFalse((b / "Cache").exists())
        self.assertEqual((b / "version.txt").read_text().strip(), "0.1.7")
        self.assertEqual((p / "version.txt").read_text().strip(), "0.1.7")
        self.assertEqual((g / "version.txt").read_text().strip(), "0.1.8")
        self.assertFalse((self.install / "Staging").exists())

    def test_rollback_restore_saves(self):
        for restore in (True, False):
            with self.subTest(restore=restore):
                self.tearDown(); self.setUp()
                self.stage()
                self.assertEqual(self.run_ps("swap").returncode, 0)
                nand = self.install / "Game" / "UserData" / "NAND" / "x"
                nand.write_text("save2")
                r = self.run_ps("rollback", *(["-RestoreSaves"] if restore else []))
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertEqual((self.install / "Game" / "MLBPowerPros2008.exe").read_text(), "old exe")
                self.assertEqual((self.install / "Previous" / "MLBPowerPros2008.exe").read_text(), "new exe")
                self.assertEqual(nand.read_text(), "save1" if restore else "save2")


class SafetyTests(UpdateBase):
    def version(self, where):
        return (self.install / where / "version.txt").read_text().strip()

    def test_leftover_rollback_tmp_keeps_program(self):
        self.stage()
        self.assertEqual(self.run_ps("swap").returncode, 0)
        leftover = self.install / "Rollback.tmp"
        write(leftover / "MLBPowerPros2008.exe", "only copy")
        for phase in ("rollback", "swap"):
            r = self.run_ps(phase)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("@@ERROR", r.stdout)
        self.assertEqual((leftover / "MLBPowerPros2008.exe").read_text(), "only copy")
        self.assertEqual((self.install / "Previous" / "MLBPowerPros2008.exe").read_text(), "old exe")

    def test_version_travels_with_program(self):
        self.stage()
        self.assertEqual(self.run_ps("swap").returncode, 0)
        self.assertEqual((self.version("Game"), self.version("Previous")), ("0.1.8", "0.1.7"))
        self.assertEqual(self.run_ps("rollback").returncode, 0)
        self.assertEqual((self.version("Game"), self.version("Previous")), ("0.1.7", "0.1.8"))
        self.assertEqual(self.run_ps("rollback").returncode, 0)
        self.assertEqual((self.version("Game"), self.version("Previous")), ("0.1.8", "0.1.7"))

    def test_protected_root_is_case_insensitive(self):
        d = self.install / "Update" / "downloads"
        d.mkdir(parents=True)
        zp = d / "z.zip"
        with zipfile.ZipFile(zp, "w") as z:
            z.writestr("workspace/BUILD/x.o", "x")
        (d / "update-manifest.txt").write_bytes(
            f"version 0.1.8\npiece core z.zip {sha256_file(zp)} 1 0 workspace/BUILD\n".encode())
        r = self.run_ps("apply", downloads=d)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("@@ERROR", r.stdout)
        self.assertFalse((self.install / "Build" / "workspace" / "BUILD" / "x.o").exists())

    def test_waitpid_exited_process_proceeds(self):
        self.stage()
        proc = subprocess.Popen([sys.executable, "-c", "pass"])
        proc.wait()
        r = self.run_ps("swap", "-WaitPid", str(proc.pid))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual((self.install / "Game" / "MLBPowerPros2008.exe").read_text(), "new exe")


class HandoffTests(UpdateBase):
    """swap / rollback run after the launcher exited: failures must undo, be written down and reopen the game."""

    def text(self, *parts):
        return self.install.joinpath(*parts).read_text(encoding="utf-8")

    def last_error(self):
        f = self.install / "Update" / "last-error.txt"
        return f.read_text(encoding="utf-8").splitlines() if f.exists() else None

    def starts(self, hook):
        return hook.read_text(encoding="utf-8").splitlines() if hook.exists() else []

    def test_held_handle_in_staging_undoes_swap(self):
        self.stage()
        hook = self.tmp / "starts.txt"
        with open(self.install / "Staging" / "launcher" / "l.txt", encoding="utf-8"):  # a handle inside a moved folder
            r = self.run_ps("swap", "-Relaunch", relaunch_hook=hook)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("@@ERROR", r.stdout)
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "old exe")
        self.assertEqual(self.text("Game", "launcher", "l.txt"), "old launcher")
        self.assertEqual(self.text("Game", "version.txt").strip(), "0.1.7")
        self.assertEqual(self.text("Staging", "MLBPowerPros2008.exe"), "new exe")
        self.assertEqual(self.text("Staging", "launcher", "l.txt"), "new launcher")
        self.assertEqual(self.text("Game", "UserData", "NAND", "x"), "save1")
        err = self.last_error()
        self.assertIsNotNone(err)
        self.assertEqual(err[0], "swap")
        self.assertEqual(len(self.starts(hook)), 1, "the old program is started again, once")

    def test_held_handle_in_previous_undoes_rollback(self):
        self.stage()
        self.assertEqual(self.run_ps("swap").returncode, 0)
        hook = self.tmp / "starts.txt"
        with open(self.install / "Previous" / "launcher" / "l.txt", encoding="utf-8"):
            r = self.run_ps("rollback", "-Relaunch", relaunch_hook=hook)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "new exe")
        self.assertEqual(self.text("Game", "launcher", "l.txt"), "new launcher")
        self.assertEqual(self.text("Previous", "MLBPowerPros2008.exe"), "old exe")
        self.assertFalse((self.install / "Rollback.tmp").exists())
        self.assertEqual(self.last_error()[0], "rollback")
        self.assertEqual(len(self.starts(hook)), 1)

    def test_failed_start_does_not_hide_the_error(self):
        self.stage()
        with open(self.install / "Staging" / "launcher" / "l.txt", encoding="utf-8"):
            r = self.run_ps("swap", "-Relaunch", relaunch_hook="!fail")
        self.assertEqual(r.returncode, 1)
        errors = [l for l in r.stdout.splitlines() if l.startswith("@@ERROR")]
        self.assertEqual(len(errors), 1, r.stdout)
        self.assertNotIn("could not be started", errors[0])
        self.assertNotIn("could not be started", "\n".join(self.last_error()))
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "old exe")

    def test_guard_failure_reopens_the_game(self):
        hook = self.tmp / "starts.txt"
        r = self.run_ps("swap", "-Relaunch", relaunch_hook=hook)  # nothing in Staging
        self.assertEqual(r.returncode, 1)
        self.assertEqual(self.last_error()[0], "swap")
        self.assertIn("Staging", self.last_error()[1])
        self.assertEqual(len(self.starts(hook)), 1)
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "old exe")

    def test_launcher_that_never_exits(self):
        self.stage()
        hook = self.tmp / "starts.txt"
        launcher = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        try:
            r = self.run_ps("swap", "-WaitPid", str(launcher.pid), "-WaitSeconds", "2", "-Relaunch", relaunch_hook=hook)
        finally:
            launcher.kill()
            launcher.wait()
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("did not exit", self.last_error()[1])
        self.assertEqual(self.starts(hook), [], "no second copy while the launcher still runs")
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "old exe")
        self.assertEqual(self.text("Staging", "MLBPowerPros2008.exe"), "new exe")

    def test_program_running_from_game_blocks_swap(self):
        self.stage()
        studio = self.install / "Game" / "TextureStudio.exe"  # a tool the launcher started, still open
        shutil.copyfile(Path(os.environ["SystemRoot"]) / "System32" / "ping.exe", studio)
        proc = subprocess.Popen([str(studio), "-n", "60", "127.0.0.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(0.5)
            r = self.run_ps("swap", "-WaitSeconds", "2")
        finally:
            proc.kill()
            proc.wait()
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("Close TextureStudio", r.stdout)
        self.assertEqual(self.text("Game", "MLBPowerPros2008.exe"), "old exe")
        self.assertEqual(self.text("Staging", "MLBPowerPros2008.exe"), "new exe")
        self.assertFalse((self.install / "Previous" / "MLBPowerPros2008.exe").exists())

    def test_successful_swap_clears_an_old_error(self):
        write(self.install / "Update" / "last-error.txt", "swap\nold\n")
        self.stage()
        self.assertEqual(self.run_ps("swap").returncode, 0)
        self.assertIsNone(self.last_error())


class LeftoverTests(UpdateBase):
    """A full install (Setup / REINSTALAR) clears what an interrupted update left (LocalBuild.ps1)."""

    def clear(self):
        items = Path(__file__).resolve().parent / "setup" / "ProgramItems.ps1"
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
               f". '{items}'; Clear-UpdateLeftovers '{self.install}'"]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_reinstall_clears_rollback_tmp_and_unblocks_swap(self):
        write(self.install / "Rollback.tmp" / "launcher" / "l.txt", "half")
        write(self.install / "Previous" / "MLBPowerPros2008.exe", "maybe half")
        write(self.install / "Staging" / "x", "old staging")
        self.clear()
        for name in ("Rollback.tmp", "Previous", "Staging"):
            self.assertFalse((self.install / name).exists(), name)
        self.assertEqual((self.install / "Game" / "UserData" / "NAND" / "x").read_text(), "save1")
        self.stage()
        self.assertEqual(self.run_ps("swap").returncode, 0)

    def test_reinstall_keeps_previous_without_rollback_tmp(self):
        write(self.install / "Previous" / "MLBPowerPros2008.exe", "old exe")
        write(self.install / "Staging" / "x", "old staging")
        self.clear()
        self.assertFalse((self.install / "Staging").exists())
        self.assertEqual((self.install / "Previous" / "MLBPowerPros2008.exe").read_text(), "old exe")

    def test_localbuild_calls_it_after_a_full_install(self):
        text = (Path(__file__).resolve().parent / "setup" / "LocalBuild.ps1").read_text(encoding="utf-8-sig")
        stage_return = text.index("if ($Stage) {\n    # Only the program")
        self.assertGreater(text.index("Clear-UpdateLeftovers $Install"), stage_return)


class ManifestHardeningTests(UpdateBase):
    def manifest(self, line):
        d = self.install / "Update" / "downloads"
        d.mkdir(parents=True, exist_ok=True)
        zp = d / "z.zip"
        with zipfile.ZipFile(zp, "w") as z:
            z.writestr("workspace/config/a.txt", "x")
        (d / "update-manifest.txt").write_bytes(f"version 0.1.8\n{line.format(sha=sha256_file(zp))}\n".encode())
        return d

    def test_empty_root_is_refused(self):
        keep = self.install / "Build" / "workspace" / "build" / "keep.o"
        write(keep, "obj")
        r = self.run_ps("apply", downloads=self.manifest("piece core z.zip {sha} 1 0 workspace/config;"))
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("not allowed", r.stdout)
        self.assertTrue(keep.exists())

    def test_zip_name_with_a_path_is_refused(self):
        r = self.run_ps("apply", downloads=self.manifest("piece core ..\\..\\z.zip {sha} 1 0 workspace/config"))
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("not allowed", r.stdout)


class EnyeTests(ApplyTests, SwapTests, SafetyTests, HandoffTests):
    folder = "Pelota Ñ"


if __name__ == "__main__":
    unittest.main()
