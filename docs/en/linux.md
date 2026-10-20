# Linux and Steam Deck

*Versión en español: [../linux.md](../linux.md)*

The game builds and runs natively on Linux x86_64: the same translated code and runtime as on Windows, with Vulkan through Dawn, SDL3 for the window, controllers and audio (PipeWire, PulseAudio or ALSA), and the same launcher, mods, Texture Studio and roster editor. There is no Linux Setup window yet: one script does what the Setup does.

As on Windows, **nothing from the game is distributed**: the script extracts, translates and compiles your own copy on your machine. That is also why there is no ready-made Linux download: the program it produces contains the game's translated code.

## Status (October 2026)

What was measured on one PC (Fedora 44, Ryzen 7 8840HS, Radeon 780M with RADV/Mesa 26.2, Wayland/KDE), October 18:

| Area | State |
|---|---|
| Build | `tools/linux/build.sh` from a clean clone to an installed game. The Windows patches apply unchanged; one more patch, `wiicompiled-linux`, adds the Linux parts |
| Boot → strap screens | works, 60 FPS |
| Launcher | works: fonts found through fontconfig, language from `LANG`, disc detected. Layout checked at 854x480 and 1280x800 |
| Match | the automated Yankees–Colorado test match (`WIICOMPILED_MATCH_BENCH=1`): 30 s at 2x, 16:9, Vulkan: **59.9 FPS**, frame time p99 17.0 ms, GPU 5.1 ms per frame |
| Audio | an SDL stream plays through PipeWire (seen with `pactl list sink-inputs`); not checked by ear |
| Texture Studio | builds and passes its self-test (`TextureStudio --selftest`: 204 checks); the window and in-game capture were not tried by hand |
| Portable build (`--container`) | runs on this PC (59.9 FPS in the same match) and on a Steam Deck (see below); needs glibc 2.35 and no libstdc++ |
| Steam Deck (LCD, SteamOS 3.8.28, glibc 2.41) | the portable build, copied from the PC: every library resolves; the test match in Desktop Mode at 2x, 16:9, Vulkan (RADV VANGOGH): **59.9 FPS**, frame time p50 16.7 ms, p99 24.4 ms, GPU 6.6 ms per frame |
| Steam Deck, by hand | Game Mode: fullscreen, the built-in controls and sound work (reported by the tester, October 18) |
| Saves | work after the fix in `wiicompiled-mlb-save-dir` (below): a save in the game writes `pm3usma.dat` (868 KB). Before it, every save said "Save failed" |
| Roster editor (PowerUp+) | builds for Linux (`linux-x64`, self-contained). Through its API, on this PC: the base roster loads from the player's `project.bin`, a roster exports as a 2008 mod into `mods/`, and the game enables it, converts it and rebuilds `PROJECT.BIN` (seen in its log). By hand: the launcher's EDIT ROSTER opens it in the browser and editing works (reported by the tester, October 18) |
| **Not tested** | building on the Deck; the roster editor on the Deck (it opens in a web browser: use Desktop Mode); X11 sessions; NVIDIA and Intel GPUs; season, league and MLB Life modes |
| Not ported | the benchmark's CPU/GPU/RAM counters read "n/d" (they come from Windows performance counters) |

## Building on a Linux PC

Requirements: git, Python 3, the .NET 8 SDK or newer (the .NET 10 SDK to also build the roster editor), CMake 3.24+, Ninja, Clang, pkg-config, and the development files SDL needs for sound and windows. About 3 GB in `local/` (4.5 GB when a GCZ has to be expanded) and 1.5 GB for the installed game; on the same filesystem as `local/`, the installed game's disc files are hard links and cost nothing extra.

```sh
# Fedora
sudo dnf install git python3 dotnet-sdk-10.0 cmake ninja-build clang lld pkgconf-pkg-config \
    pipewire-devel pulseaudio-libs-devel alsa-lib-devel libXScrnSaver-devel libdecor-devel \
    wayland-devel libxkbcommon-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
    libXi-devel libXfixes-devel libXtst-devel mesa-libgbm-devel libdrm-devel systemd-devel dbus-devel \
    libusb1-devel libpng-devel zlib-devel freetype-devel
# Debian / Ubuntu 24.04+
sudo apt install git python3 dotnet-sdk-10.0 cmake ninja-build clang lld pkg-config curl \
    libpipewire-0.3-dev libpulse-dev libasound2-dev libxss-dev libdecor-0-dev libwayland-dev \
    wayland-protocols libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxfixes-dev libxtst-dev libgbm-dev libdrm-dev libudev-dev libdbus-1-dev libusb-1.0-0-dev \
    libpng-dev zlib1g-dev libfreetype-dev
```

Then, from the repository:

```sh
tools/linux/build.sh --disc '/path/to/your copy.rvz' --install ~/Games/MLBPowerPros2008 --desktop-entry
~/Games/MLBPowerPros2008/MLBPowerPros2008
```

The script checks that the disc is a clean `RL8E54` (ISO, RVZ, WIA, WBFS, CISO, NKit, GCZ), extracts it, clones Wiicompiled at the pinned commit, applies the patches in the same order as `tools/wiicompiled-translate.ps1` and then `wiicompiled-linux`, translates, compiles and installs. Measured here, with 16 threads: translation 40 s, a first compile about 4 minutes (the runtime, SDL and aurora's dependencies; Dawn comes precompiled), a rebuild after a runtime change under a minute. Logs: `local/analysis/linux-*.log`.

- Old Dolphin GCZ files with 16 KiB blocks are rejected by nodtool; the script expands them to an ISO in `local/images/` first (`tools/gcz_to_iso.py`, which checks every block's checksum).
- The roster editor (PowerUp+) is built from its pinned commit with `tools/patches/powerupplus-*.patch`, as `tools/powerupplus.ps1 -Publish` does on Windows; Node.js 22 for its web UI is downloaded into `local/tools` and checked by SHA-256. Without the .NET 10 SDK the script skips it with a warning, and the launcher does not offer EDIT ROSTER. `--no-roster-editor` skips it on purpose.
- The script refuses to build without PipeWire, PulseAudio or ALSA development files: SDL would otherwise build a game without sound.
- **Without root:** if you cannot install the `-devel` packages, extract them into `local/sysdeps/` and the script uses them (Fedora: `dnf download <packages>`, then `rpm2cpio <rpm> | cpio -idm` inside `local/sysdeps`, and point the `prefix=`/`libdir=` lines of `local/sysdeps/usr/lib64/pkgconfig/*.pc` at that folder). Only headers are needed: SDL loads the sound libraries at run time. Or use `--container`.

The installed folder is self-contained and can be moved: `Config.toml` points at `../Data`. `portable.txt` keeps the configuration, saves (NAND) and caches in `UserData/` next to the game.

## Steam Deck

The normal build depends on the build machine's glibc (Fedora 44 here: 2.43), newer than SteamOS's, so it does not start on a Deck. `--container` builds inside `tools/linux/Containerfile` (Ubuntu 22.04: glibc 2.35, clang 20, with libstdc++ linked in statically, and the .NET 10 SDK). The result runs on SteamOS and on any current distribution, and the host needs only podman or docker, with no compilers or `-devel` packages.

**Option A: build on a Linux PC and copy the folder.**

```sh
tools/linux/build.sh --container --disc '/path/to/your copy.rvz' --install ~/Games/MLBPowerPros2008
rsync -a ~/Games/MLBPowerPros2008 deck@steamdeck:~/Games/     # or a USB stick or SD card
```

**Option B: build on the Deck** (Desktop Mode, Konsole). SteamOS 3.5 and later ship podman: check with `podman --version`. The build is slower on the Deck's 4 cores.

```sh
git clone https://github.com/HopeAero/MLBPowerPros2008Recomp.git && cd MLBPowerPros2008Recomp
tools/linux/build.sh --container --disc '/run/media/<SD card>/MLB Power Pros 2008.rvz'   # wherever your copy is
```

**Adding it to Game Mode:** in Desktop Mode open Steam → *Games* → *Add a Non-Steam Game to My Library* → *Browse* → `~/Games/MLBPowerPros2008/MLBPowerPros2008`. In its *Properties*, check that *Start In* is that folder (`~/Games/MLBPowerPros2008/`). Do **not** set a compatibility tool (Proton): this is a native Linux program. Back in Game Mode it starts fullscreen under gamescope. The Deck's controls reach the game as a standard gamepad through Steam Input; the launcher is controller-friendly.

**Measured on a Steam Deck** (LCD model, SteamOS 3.8.28, October 18), with the portable build from option A: the automated test match in Desktop Mode at 2x, 16:9, Vulkan ran at 59.9 FPS (p50 16.7 ms, p99 24.4 ms; GPU 6.6 ms of the 16.7 ms frame), so 2x is a safe default. The p99 is higher than on the PC (17.0 ms): a few long frames, possibly first-run shader compilation (the cache is kept in `UserData/Cache`); not investigated. Played by hand in Game Mode: fullscreen, the built-in controls and sound work. **Not tested on the Deck:** 3x and 4x, battery use, suspend and resume, building on the Deck (option B), and the roster editor, which opens in a web browser and so belongs in Desktop Mode.

A NAND made by an earlier build has a European `setting.txt` (see below); saving works either way, but moving `UserData/NAND` aside before the first start gives a fresh, USA one.

## What changed for Linux

- `tools/patches/wiicompiled-linux.patch` (applied after the others):
  - launcher fonts fall back to fontconfig (`fc-match`) when the DejaVu paths are missing (Fedora and SteamOS);
  - MODS → open folder uses `xdg-open`;
  - TOOLS → Texture Studio starts `TextureStudio` next to the game;
  - TOOLS → EDIT ROSTER starts the bundled PowerUp+ server once per session (it gets `SIGTERM` when the game exits, like the job object on Windows) and opens it with `xdg-open`; the option is shown only when `powerupplus/` is installed;
  - Steam's `LD_PRELOAD` is not passed to these tools.
- `tools/patches/wiicompiled-mlb-save-dir.patch` (not Linux-specific; in the shared list, not tested on Windows): the game lists its title data directory (`/title/00010000/524c3845/data`) before its first save and shows "Save failed" when it is missing; it never creates it, a console's system does. The runtime now creates it. New NANDs also get Dolphin's USA `setting.txt` (`AREA=USA`, `VIDEO=NTSC`) instead of Mario Kart's European one; an existing `setting.txt` is kept.
- Texture Studio (`tools/texture-studio`): POSIX branches for argument parsing, opening folders and running the game to capture textures (`posix_spawn` with `WIICOMPILED_TEXTURE_DUMPS=1`). The Windows code is unchanged.
- `tools/linux/build.sh`, `tools/linux/Containerfile` and `tools/gcz_to_iso.py` are new. Of the Windows scripts, only the patch list in `wiicompiled-translate.ps1` changed (`wiicompiled-mlb-save-dir`). The Linux script reads the patch list from `wiicompiled-translate.ps1`, so both platforms apply the same patches.

Wiicompiled itself already supported Linux at the pinned commit (System V libco fibers, Vulkan through Dawn, SDL3 statically linked); the MLB patches compile on it unchanged.
