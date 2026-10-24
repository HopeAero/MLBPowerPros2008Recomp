# Decisions

*Español: [../decisiones.md](../decisiones.md)*

The decisions that shape the project, with the reason and when to revisit them. A new decision is added here; one that changes is struck through and the new one written next to it.

| Date | Decision | Why | Revisit when |
|---|---|---|---|
| 2026-10-08 | Static recompilation (Wiicompiled), not an emulator or a decomp | Native performance and a road to other platforms without waiting years for a decompilation | If a decomp of 2008 appears |
| 2026-10-08 | No game files are ever distributed: every player builds the game from their own disc (Setup) | Legality and respect for the game's owners | — |
| 2026-10-09 | Faithful to the original by default; anything that changes the game goes in ENHANCEMENTS and is opt-in | Players choose; the original stays intact | — |
| 2026-10-11 | Vulkan before Direct3D 12 on AMD | Direct3D 12 dropped in heavy fielding views; Vulkan holds 60 FPS | After the performance plan |
| 2026-10-13 | Runtime changes are reproducible patches on a pinned Wiicompiled | Anyone can rebuild a release from source | — |
| 2026-10-14 | Texture mods use Dolphin's names | Compatibility with community packs (checked with Dolphin 2609) | — |
| 2026-10-21 | Everything that works in Season mode must also work in MLB Life | MLB Life is the most played mode | — |
| 2026-10-22 | In-game UI uses the game's own pictures and font, not look-alikes | New features should look like part of the game | — |
| 2026-10-24 | AI upscaling downloads the engine and model from their authors; they don't ship with the game | Licenses (AnimeSharp is CC BY-NC-SA) | If an equally good free model appears |
| 2026-10-24 | The roadmap lives in `ROADMAP.md`; bugs in Issues; ideas in Discussions | One public source of truth | — |
