# OpenGL

*Español: [../../roadmap/opengl.md](../../roadmap/opengl.md)*

**Status:** in progress (Linux) · [Roadmap](../../../ROADMAP.md)

## Why
The game draws through Dawn (WebGPU), which today uses Direct3D 12 or Vulkan. Older GPUs and some drivers have neither working well. OpenGL / OpenGL ES is also the most direct road to the Switch, where Dawn has no native backend.

## Where we are
- **Linux: the game renders and plays a match on OpenGL ES** (patch `wiicompiled-opengles`). The prebuilt Dawn package for Linux already carries its GL/EGL backend; it was only switched off in aurora.
- **Windows:** the Windows Dawn package comes without GL. For GPUs without Direct3D 12 or Vulkan the natural choice is Dawn's Direct3D 11 backend, which the package does include (aurora has it switched off).
- What aurora needed:
  - Dawn only exposes GL adapters at the *Compatibility* feature level.
  - A GL context can be current on one thread at a time (`eglMakeCurrent` fails with `EGL_BAD_ACCESS`), so on GL frame preparation and presentation stay on the calling thread, without the helper threads.
  - Dawn's EGL swapchain does not support Wayland surfaces: with OpenGL ES the window goes through X11 / XWayland.
  - aurora called `SDL_SyncWindow` every frame. On X11 servers that never confirm a window's requested position (XWayland under WSLg) every frame waited out the whole timeout. It now syncs only after aurora changes the window. This affects any backend on X11, not just GL.
- `AURORA_DUMP_PRESENT=<frame>:<file.ppm>` saves the presented frame read back from the GPU, before the window: it tells a black window apart from a black render.

## Measurements (WSL, not representative)
WSL on a Ryzen 5 5500 + RX 5500, Yankees–Colorado benchmark match at x1. **This is not real performance**: under WSL, Vulkan has no GPU driver and GL goes through a translation to Direct3D 12 and WSLg's remote desktop. They only compare the backends with each other.

| Backend | Draws on | FPS | p50 / p95 |
|---|---|---|---|
| OpenGL ES | GPU (Mesa d3d12) | 40.7 | 23.8 / 29.9 ms |
| OpenGL ES | CPU (llvmpipe) | 16.7 | 57.9 / 65.8 ms |
| Vulkan | CPU (lavapipe) | 15.0 | 29.1 / 169.5 ms |

Under WSLg the window of any GL program on X11 stays black (Mesa's own `es2gears` too: `Failed to attach to x11 shm`). The frame read back with `AURORA_DUMP_PRESENT` shows the whole match.

## Plan
1. ~~Enable the OpenGL ES backend on Linux and play a match.~~
2. Measure on native Linux (Steam Deck or PC) against Vulkan, with the same benchmark.
3. Check menus, MLB Life and every stadium on OpenGL ES.
4. Windows: Direct3D 11 for GPUs without Direct3D 12 or Vulkan.
5. Ship OpenGL ES in the Linux and Steam Deck kits.

## How it is verified
The same benchmark and the same visual checks (menus, a match, every stadium) on OpenGL as on Vulkan.

## Checklist
- [x] Dawn with OpenGL ES (Linux: in the prebuilt package)
- [x] The game boots and plays a match on OpenGL ES (Linux, WSL)
- [x] OpenGL ES selectable in the launcher (Linux)
- [ ] Measured on native Linux
- [ ] Direct3D 11 on Windows
