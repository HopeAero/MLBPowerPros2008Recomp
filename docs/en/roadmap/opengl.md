# OpenGL

*Español: [../../roadmap/opengl.md](../../roadmap/opengl.md)*

**Status:** in progress (OpenGL ES on Linux, Direct3D 11 on Windows) · [Roadmap](../../../ROADMAP.md)

## Why
The game draws through Dawn (WebGPU), which today uses Direct3D 12 or Vulkan. Older GPUs and some drivers have neither working well. OpenGL / OpenGL ES is also the most direct road to the Switch, where Dawn has no native backend.

## Where we are
- **Linux: the game renders and plays a match on OpenGL ES** (patch `wiicompiled-opengles`). The prebuilt Dawn package for Linux already carries its GL/EGL backend; it was only switched off in aurora.
- **Windows: Direct3D 11 renders and plays a match** (patch `wiicompiled-d3d11`). The Windows Dawn package comes without GL but carries the Direct3D 11 backend, which covers GPUs without Direct3D 12 or Vulkan (for example Intel HD 2000/3000). Automatic selection tries it after Direct3D 12 and Vulkan.
  - Dawn compiles D3D11 shaders with FXC and only finds `d3dcompiler_47.dll` next to `webgpu_dawn.dll` (by bare name it fails with Windows error 87). Every Windows 10/11 has it in System32; the build copies the local one next to the game.
  - It is requested at the *Compatibility* level, like GL, so feature level 11_0 GPUs qualify too.
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

## Measurements (native Windows)
Ryzen 5 5500 + RX 5500, same benchmark match, 20 s at x4 (4x internal resolution).

| Backend | FPS | p99 | GPU 3D | Game CPU | VRAM |
|---|---|---|---|---|---|
| Direct3D 12 | 59.9 | 17.3 ms | 69.7 % | 4.7 % | 389 MB |
| Direct3D 11 | 60.0 | 17.3 ms | 71.0 % | 4.9 % | 329 MB |

Direct3D 11 performs like Direct3D 12 and uses 15 % less VRAM. Image checked with `AURORA_DUMP_PRESENT`: match and launcher complete.

## Plan
1. ~~Enable the OpenGL ES backend on Linux and play a match.~~
2. Measure on native Linux (Steam Deck or PC) against Vulkan, with the same benchmark.
3. Check menus, MLB Life and every stadium on OpenGL ES.
4. ~~Windows: Direct3D 11 for GPUs without Direct3D 12 or Vulkan.~~ Still to try on a GPU that only has Direct3D 11.
5. Ship OpenGL ES in the Linux and Steam Deck kits.

## How it is verified
The same benchmark and the same visual checks (menus, a match, every stadium) on OpenGL as on Vulkan.

## Checklist
- [x] Dawn with OpenGL ES (Linux: in the prebuilt package)
- [x] The game boots and plays a match on OpenGL ES (Linux, WSL)
- [x] OpenGL ES selectable in the launcher (Linux)
- [ ] Measured on native Linux
- [x] Direct3D 11 on Windows (plays a match, selectable in the launcher)
- [ ] Direct3D 11 tried on a GPU without Direct3D 12
