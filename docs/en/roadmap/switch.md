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
| Automatic test match without graphics (`graphics_api = "null"`, SDL `dummy` audio), A78 cores | boot, disc, sound engine, menus, match setup and **first pitch in 28-29 s**, 6 out of 6 runs, as on x86 |
| Same on the A55 cores | first pitch in 130-131 s, 3 out of 3 runs; the game runs at about 12 frames per second |
| `--16k-pages` build (runtime page-size check), A78 cores | first pitch in 29 s |
| Graphics | **untested on ARM**: Dawn's null backend runs all the GX work on the CPU but draws nothing |

Two bugs that only showed up on ARM, fixed in `tools/patches/wiicompiled-aarch64.patch` (details in [reverse engineering](../reverse-engineering.md#3-threads-and-timing)):
- **DSP busy-wait:** the game waits in an empty loop for the DSP to call its task's init callback, and the runtime's `DSPAddTask` never called it. On x86 the compiler removes that loop and nobody noticed; on ARM, with the runtime page-size check, the loop stayed. `DSPAddTask` now calls the callback like the real DSP. On top of that, Linux ARM64 uses fixed 4 KiB pages by default like x86 (the same code that was validated on x86, and faster) and refuses to start with another page size. The Switch, phones and most distributions use 4 KiB; for the Raspberry Pi 5 with its default kernel (16 KiB) there is `build.sh --arch aarch64 --16k-pages`, tested on the phone but not on a Pi.
- **Starved loader:** the frame loop only waits for VI when it is early. Below 60 fps the main thread never blocked and the boot loader (priority 24) never ran. `VIGetRetraceCount` now waits one retrace when a lower-priority thread has been ready for 100 ms without running. It is not ARM-specific, so it was also measured on x86: it never fired in the Windows test match (59.9 fps, as before) nor on Linux x86 at 14.7 fps with software graphics; on the phone it fires once per boot.

For the Switch this means the translated code works on ARM64 and the CPU matters: at 1 GHz the Switch's A57s are roughly as fast as the phone's A55s or slower (not measured), and those A55s run at about 12 fps with the null backend. Libraries the build needs to start: libpng16, libz, libfreetype (with libbz2 and libbrotlidec/libbrotlicommon), libm, libstdc++, libgcc_s and libc; SDL3 and Dawn are linked in. SDL and Dawn also load the system's video and audio libraries when present (Vulkan, EGL/GLES, Wayland/X11, PipeWire/PulseAudio/ALSA, udev, D-Bus).

## Checklist
- [ ] OpenGL ES backend working
- [x] ARM64 build running (Linux ARM64, on a phone; see above)
- [ ] Switch prototype boots to the main menu
