# OpenGL

*Español: [../../roadmap/opengl.md](../../roadmap/opengl.md)*

**Status:** research · [Roadmap](../../../ROADMAP.md)

## Why
The game draws through Dawn (WebGPU), which today uses Direct3D 12 or Vulkan. Older GPUs and some drivers have neither working well. OpenGL / OpenGL ES is also the most direct road to the Switch, where Dawn has no native backend.

## Where we are
- The prebuilt Dawn package the project uses comes without its OpenGL and OpenGL ES backends.
- Dawn has an OpenGL ES backend in its source tree; enabling it means building Dawn from source.

## Plan
1. Build Dawn from source with the OpenGL / OpenGL ES backends and run the game on them on Windows and Linux.
2. Find what the game's rendering needs that those backends lack, and work around it.
3. Offer OpenGL as a choice in the launcher, next to Vulkan and Direct3D 12, with the benchmark from the performance plan.

## How it is verified
The same benchmark and the same visual checks (menus, a match, every stadium) on OpenGL as on Vulkan.

## Checklist
- [ ] Dawn built with OpenGL / OpenGL ES
- [ ] The game boots and plays a match on OpenGL
- [ ] OpenGL selectable in the launcher
