# Roadmap

*Español: [ROADMAP.es.md](ROADMAP.es.md)*

**This file is the source of truth** for what is done, in progress and planned. Each line of work has a plan in [`docs/en/roadmap/`](docs/en/roadmap/) with its phases, how it is verified and a checklist. Bugs live in [Issues](https://github.com/HopeAero/MLBPowerPros2008Recomp/issues); ideas and questions in [Discussions](https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions). When something is finished it moves to **Done** with the release that shipped it.

Talk about it: [roadmap discussion](https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions/7).

Updated: October 24, 2026 · latest release [v0.1.5](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.5).

Two rules hold for everything below: the game stays **faithful to the Wii original by default** (anything that changes gameplay is opt-in from the launcher), and **no game files are ever distributed**: every player builds the game from their own disc.

## Now

| Line of work | What | Status |
|---|---|---|
| [Bug fixes](docs/en/roadmap/bugs.md) | Reports from players, triaged and fixed in hotfix releases | Ongoing |
| [Performance on modest PCs](docs/en/roadmap/performance.md) | Measure and lower the CPU and GPU cost so the game holds 60 FPS on integrated graphics and older CPUs | Next up |
| [OpenGL](docs/en/roadmap/opengl.md) | An OpenGL / OpenGL ES graphics backend for GPUs without Vulkan or Direct3D 12, and as the road to Switch | In progress: OpenGL ES on Linux and Direct3D 11 on Windows play a match |

## Next

| Line of work | What | Status |
|---|---|---|
| [Modern postseason](docs/en/roadmap/postseason.md) | The 12-team postseason (Wild Card Series) in Season mode and MLB Life, opt-in | Research |
| [Modding tools](docs/en/roadmap/modding-tools.md) | Better Texture Studio, Season Studio, roster and stadium tools; custom stadiums in Season and MLB Life | Ongoing |

## Research

| Line of work | What | Status |
|---|---|---|
| [Code mods](docs/en/roadmap/code-mods.md) | Mods that change the game's logic (tournaments, gameplay rules, new screens) through a documented mod API, not only data | Research |
| [Nintendo Switch](docs/en/roadmap/switch.md) | A homebrew Switch build, from the player's own copy, like the PC one | In progress: ARM64 on Linux reaches the first pitch on a phone (no graphics yet) |

## Done

| Release | Highlights |
|---|---|
| [v0.1.5](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.5) | Two-way players (Ohtani rule) in every mode; MLB Life role talk with the manager; Texture Studio AI upscaling and Dolphin packs |
| [v0.1.4.1](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4.1) | Controller hotfix |
| [v0.1.4](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4) | Real MLB schedules (2002–2026), start year, DH in both leagues, 15/15 leagues, Season Editor, player passwords as text |
| [v0.1.3](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.3) | Season mode past 10 seasons; Linux and Steam Deck kits |
| [v0.1.2](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.2) | Commentary fix, saves in use, stadiums from scratch in Blender |
| v0.1.0–v0.1.1 | First public builds: native PC port, launcher, mods, 2026 rosters, widescreen, Texture Studio |

Decisions behind all of this, with the reason and when to revisit them: [docs/en/decisions.md](docs/en/decisions.md).
