# Nintendo Switch

*Español: [../../roadmap/switch.md](../../roadmap/switch.md)*

**Status:** in progress: ARM64 on Linux · [Roadmap](../../../ROADMAP.md)

## Why
Power Pros is a console game at heart. A homebrew Switch build would let people play it on the couch and on the go, built from their own copy like on PC.

## What it takes
- **Graphics:** Dawn has no backend for the Switch's system. Options: Dawn's OpenGL ES backend on the Switch's Mesa (which is why OpenGL comes first), or a native renderer on deko3d.
- **CPU and memory:** the translated code must build for ARM64 (the Switch's CPU) and fit in its memory.
- **System:** controllers, audio, files and fonts through libnx (devkitPro).
- **Distribution:** as on PC, nothing of the game is distributed; the player provides their own copy.

## Plan
1. OpenGL / OpenGL ES working on PC first (see [OpenGL](opengl.md)).
2. An ARM64 Linux build (for example on a Raspberry Pi or an Android TV box) as the step between PC and Switch.
3. A first Switch prototype that boots to the menu.

## ARM64 build (October 2026)

`tools/linux/build.sh --arch aarch64` builds the game for Linux ARM64 from an x86_64 PC (clang 18 against an Ubuntu 24.04 arm64 sysroot; see [Linux](../linux.md#arm64-experimental)). Tested on October 25 on real ARM hardware: a Samsung Galaxy A54 (Exynos 1380: 4× Cortex-A78 at 2.4 GHz and 4× Cortex-A55 at 2.0 GHz; Android 14, 4 KiB pages), from `adb shell` without root, with `tools/linux/aarch64/adb-match.sh`:

| What | Result |
|---|---|
| Runtime tests (QEMU on the PC) | pass, except three that also fail on Linux x86_64 |
| Automatic test match without graphics (`graphics_api = "null"`), A78 cores | boot, disc, sound, menus, match setup and **first pitch in 25-32 s**, 5 out of 5 runs, as on x86 |
| Same on the A55 cores | first pitch in 150 s; the intro runs at about 15 frames per second |
| Graphics | **untested on ARM**: Dawn's null backend runs all the GX work on the CPU but draws nothing |

Two bugs that only showed up on ARM, fixed in `tools/patches/wiicompiled-aarch64.patch` (details in [reverse engineering](../reverse-engineering.md#3-threads-and-timing)):
- **DSP busy-wait:** the game waits for the DSP with an empty loop that the compiler removes on x86. On ARM the runtime page-size check kept it. Linux ARM64 now uses fixed 4 KiB pages like x86, and the game refuses to start with another page size. The Switch, phones and most distributions use 4 KiB; the Raspberry Pi 5 with its default kernel uses 16 KiB (build with `-DMKW_AARCH64_4K_PAGES=OFF`; for now it hangs in that wait).
- **Starved loader:** the frame loop only waits for VI when it is early. Below 60 fps the main thread never blocked and the boot loader (priority 24) never ran. `VIGetRetraceCount` now waits one retrace when a lower-priority thread has been ready for 100 ms. This is not ARM-specific: on Windows the test match still runs at 59.9 fps.

For the Switch this means the translated code works on ARM64 and the CPU matters: at 1 GHz the Switch's A57s are roughly as fast as the phone's A55s or slower (not measured), and those A55s already miss 60 fps in the intro with the null backend. Dynamic libraries of the build: libpng16, libz, libfreetype, libm, libstdc++, libgcc_s and libc (SDL3 and Dawn are linked in).

## Checklist
- [ ] OpenGL ES backend working
- [x] ARM64 build running (Linux ARM64, on a phone; see above)
- [ ] Switch prototype boots to the main menu
