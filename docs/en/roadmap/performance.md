# Performance on modest PCs

*Español: [../../roadmap/rendimiento.md](../../roadmap/rendimiento.md)*

**Status:** next up · [Roadmap](../../../ROADMAP.md)

## Why
The game should run at a steady 60 FPS on the PCs people actually have: laptops with integrated graphics, 4-core CPUs from a few years ago, the Steam Deck. Today it is tuned and measured on one desktop (Ryzen 5 5500 + Radeon RX 5500).

## Where we are
- GPU at native resolution (x1) in a match: about 2.9 ms per frame on the RX 5500; x2 with 16x filtering about 7.5 ms. Quality presets (Low / Medium / High), AMD FSR 1 upscaling, mipmaps and MSAA are already in the launcher.
- The game logic is translated ahead of time to native code, so there is no emulation cost, but the translated code and the runtime have not been profiled on slow CPUs yet.
- Memory: the game uses about 630 MB in a match (D3D12; about 100 MB less with Direct3D 11). The uniform upload buffer went from 24 to 8 MB per frame (measured peak 1 MB, 2.9 MB with 180 FPS interpolation): 44 MB less RAM and 22 MB less VRAM, no FPS change. The rest: the emulated Wii memory (24 + 128 MB), the translated code (94 MB), the graphics driver (~100 MB).
- Vulkan holds 60 FPS where Direct3D 12 dropped on AMD in heavy fielding views; the launcher picks the backend.

## Plan
1. **Measure on real low-end hardware:** a repeatable benchmark (the same saved match, same camera path) that reports CPU and GPU time per frame, run on integrated Intel and AMD graphics and an older 4-core CPU. Community results welcome.
2. **CPU:** profile the translated code and the runtime (audio mixing, texture decoding, draw submission) and fix the top costs.
3. **GPU:** cheaper full-screen effect passes at high resolutions, fewer state changes per draw, a "Low" preset tuned for integrated graphics.
4. **Steam Deck:** a measured profile and recommended settings.

## How it is verified
Before and after numbers from the same benchmark, on the same machines, in each release's notes.

## Checklist
- [ ] Benchmark that players can run and share
- [ ] Results from at least three low-end machines
- [ ] Top three CPU costs fixed
- [ ] "Low" preset holds 60 FPS on integrated graphics
