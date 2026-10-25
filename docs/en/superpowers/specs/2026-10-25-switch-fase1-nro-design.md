# Switch, phase 1: the game boots on the console with graphics faithful to PC — design

*Versión en español: [../../../superpowers/specs/2026-10-25-switch-fase1-nro-design.md](../../../superpowers/specs/2026-10-25-switch-fase1-nro-design.md)*

Date: October 25, 2026. Status: approved by the user on October 25, 2026 ("continua"). Branch: `switch-arm64` (master is not touched).

## Why

The user wants to play MLB Power Pros 2008 on their homebrew Nintendo Switch, built from their own copy as on PC. They already ported another game (Need for Madness) to the Switch and know the workflow: test on the console, hand reports to the chat, keep everything in one folder on the SD card. The previous phase (branch `switch-arm64`) proved the translated code works on real ARM64: on a phone it reaches the first pitch with the null backend.

## What success means in this phase

The user opens the `.nro` from their Switch's homebrew menu and **the game boots, reaches the menu and enters a match, and looks the same as on PC**: no broken graphics, no wrong textures or colors. Running slowly is fine; speed is the next phase. In the user's words: "as long as it doesn't break graphically and looks faithful to the PC port, what's left is optimizing".

## Goals

1. Build the game as a Switch `.nro` on the developer's PC, from the user's ISO, with devkitPro (devkitA64 + libnx) inside Docker, as in the Need for Madness port.
2. The runtime (Wiicompiled + the MLB patches) working on Horizon: guest memory, threads and fibers, files on the SD card, controllers, audio.
3. Graphics with the same aurora as on PC on **Dawn + Vulkan with Mesa's NVK driver for Switch** (`nxvk`), i.e. the same drawing code as on PC.
4. A folder of its own on the SD card with the game, the disc data, saves, configuration, shader caches, reports and logs.
5. Reports for the chat:
   - the existing CPU-vs-CPU automatic match report (`WIICOMPILED_MATCH_BENCH`), written to the SD card and started without environment variables (for example from an option or a file on the SD card);
   - a detailed crash report written to the SD card when the game fails (runtime log and guest state, like the one it already produces on PC).
6. Fidelity check: captures of the same moment of the automatic match on the Switch and on PC, compared.

## Out of this phase (non-goals)

- Speed: neither 30 nor 60 fps is required. It is measured and reported, not optimized.
- The deko3d renderer (Twilight Princess / SwitchWaker's). It is the chosen path for the optimization phase; it is not touched here.
- The launcher adapted to the Switch (controller-driven, no keyboard). In this phase the game boots directly; the launcher comes in a later phase.
- The PC Setup generating the `.nro` and the SD data automatically. In this phase the developer produces them.
- Network, Discord, mods, Texture Studio, roster editor and the other PC tools.
- Handheld mode, dock and resolutions: whatever the system gives by default.

## What would make it a failure even if it "works"

- Looking different from PC: broken colors, textures, effects or geometry. Graphical fidelity is the main criterion.
- Publishing or shipping game data: the `.nro` contains the game's translated code and is never uploaded to GitHub or releases; the repository only carries scripts and patches (project rule).
- Breaking the Windows and Linux builds: every runtime change must keep Windows at 60 fps and Linux as is.

## Constraints

- All work goes on the `switch-arm64` branch.
- Runtime changes are reproducible patches in `tools/patches`, listed in `tools/wiicompiled-translate.ps1`, on the pinned Wiicompiled commit (`6fa2473`).
- devkitA64 is GCC only (no clang); the translated code and the runtime must build with GCC.
- Nothing from the ISO, the executable or game captures in git.
- Licenses: Wiicompiled and `wiicompiled-nx` are GPL-3.0 (like the project); Dawn BSD-3; Mesa/NVK MIT (statically linked, the `.nro` is GPL-covered); SDL3 zlib.
- Documentation in Spanish and English (`docs/` and `docs/en/`), and reverse-engineering findings in `docs/ingenieria-inversa.md`.

## Decisions made

- **Test on the user's real Switch** (Atmosphère + homebrew menu), not an emulator. The user copies the `.nro` and the data to the SD card and hands back reports.
- **Graphics: Dawn + Vulkan (NVK) first, deko3d in the optimization phase.** Twilight Princess's deko3d bridge depends on an internal aurora structure (`gfx/frame`, `recording`, `render_worker`, `resource_cache`) that our aurora (Wiicompiled's) does not have; adapting it is weeks with nothing on screen. Dawn + Vulkan works with our aurora as is, and it is the default and fallback renderer of Twilight Princess's aurora.
- **Reuse `nx-mod`'s work** (GPL-3.0): `wiicompiled-nx` already ported our same runtime to Horizon (Mario Kart Wii playable on the console, at ⅕ speed), `dawn-nx` ports Dawn to Horizon with a `VK_NN_vi_surface` surface, and `nxvk` is the NVK driver for Switch. Their notes (`local/research/wiicompiled-nx-switch-port-notes.md`) document the problems already solved: devkitA64 is GCC only, no memory alias on Horizon (the runtime uses the checked path), `thread_local` broken under libnx, the `.nro` needs an icon and NACP, Dawn's GL backend does not start.
- **Window, audio and controllers with devkitPro's SDL3 branch** (the one Twilight Princess uses), if it covers what aurora needs; otherwise `wiicompiled-nx`'s shims.
- **Build in Docker** with devkitPro's image (`devkitpro/devkita64`) plus what nxvk and Dawn need.

## Answers to the design review

- **`nx-mod` repositories:** all public on GitHub (2026-10-25): `wiicompiled-nx` (branch `switch`, last push 9-29), `dawn-nx` (`switch`, 10-22; also `switch-deko3d` and `switch-gl-20261007`), `nxvk` (`switch`, 10-6; branches per Mesa version), `sqlite-nx` (`switch`), `aurora-nx` (`switch`, 10-7) and `wii-nx` (`main`, 10-15). No credentials needed. One commit of each is pinned; which one drew on the console is found by reading their notes and history (builder's task).
- **Docker:** the image is built from scratch on `devkitpro/devkita64` *(my call)*; the Need for Madness port's files are not reused.
- **Fidelity reference** *(my call)*: the PC with Direct3D 12 at native resolution (x1), no enhancements (FSR, MSAA, anisotropic, CALIDAD, auto mipmaps and texture packs off), at the same moments of the automatic match. Switch and PC captures go side by side and **the user judges by eye**: pixel equality is not required (resolution and GPU precision differ).
- **PC layers (ImGui):** with Dawn + Vulkan, aurora's WebGPU ImGui backend works as on PC, so nothing needs porting. Fidelity is still judged on the game's picture; ImGui panels do not count, and the automatic match is enabled from a file on the SD card *(my call)*.
- **Automatic match report** *(my call)*: a bounded stretch (from the first pitch, N seconds as on PC with `WIICOMPILED_MATCH_SECONDS`), not a whole match. The report is written to the SD card incrementally, because HOME closes the process without going through `atexit`. Audio must play without hanging; sounding perfect is not required in this phase.
- **Console test loop (user's answer):** the user's Switch runs firmware 22.5.0 and Atmosphère 1.11.2 and does not use `nxlink`; it could go over the network, but that is cumbersome for them. Every test is a manual copy to the SD card. So the plan:
  - stages the test builds so each trip to the console answers one question (for example: a minimal `.nro` that boots and writes a log; Dawn presenting a color; the game without graphics; the full game);
  - writes every log to the SD card as it happens (with `fflush`), so it survives a crash or HOME;
  - tests in Docker and on the phone everything that does not need the console before asking for a console test;
  - gives the user short, exact steps per test: what to copy, where, how to open it and which file to send back.

  It is not known whether the user can open the homebrew menu "over a game" (holding R while opening a game), which gives full RAM; the plan gives it as the recommended way and measures whether applet mode is enough.
- **Relationship with master:** master is frozen at `a497b6f` (Direct3D 11) until the user decides to integrate, so `switch-arm64` does not follow it; integration is a separate task at the end.

## What we already have going for us

- The translated code works on ARM64 (phone, first pitch in 28-29 s with the null backend).
- The previous phase's fixes the Switch needs: the DSP init callback (without it the checked memory path hangs when audio starts) and the lower-priority thread guard (without it, a CPU below 60 fps starves the loader and the intro never ends).
- The CPU-vs-CPU automatic match and its report, and the guest-state dump on failure.

## Known risks

- **Memory:** Mario Kart Wii's `.nro` is about 101 MB; MLB's will be similar. The homebrew menu's applet mode gives little memory; opening it "over a game" (title override) may be needed for full RAM. To be measured.
- **Runtime divergence:** `wiicompiled-nx` starts from a 2026-09-07 Wiicompiled with 79 commits of its own; our pinned commit is 41 commits newer and carries 75 MLB patches. Their changes have to be brought into our tree, not the other way around.
- **First load:** in Mario Kart pipeline prewarm took almost two minutes; with a persistent shader cache (sqlite) the second run is fast.
- **Newer firmware:** the Mario Kart Wii port was tested on firmware 16.0.5; the user runs 22.5.0. libnx and nxvk have to support it, and the memory map may differ.
- **GCC:** parts of the runtime or the MLB patches may rely on clang extensions.

## Left to the builder

- How to bring `wiicompiled-nx`, `dawn-nx` and `nxvk`'s changes into our tree (cherry-pick, new patches, submodules or prebuilt packages in the Docker image), as long as it is reproducible.
- The exact layout and name of the SD card folder.
- How the automatic match is started on the Switch (an option, a file on the SD card or a button at boot).
- The internal order of the tasks and each one's tests (those that do not need the console, in Docker or on the phone; console ones, with steps the user can follow).
