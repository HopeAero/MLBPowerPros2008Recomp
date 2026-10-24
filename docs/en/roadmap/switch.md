# Nintendo Switch

*Español: [../../roadmap/switch.md](../../roadmap/switch.md)*

**Status:** research · [Roadmap](../../../ROADMAP.md)

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

## Checklist
- [ ] OpenGL ES backend working
- [ ] ARM64 build running
- [ ] Switch prototype boots to the main menu
