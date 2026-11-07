<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Uniform Studio

An easy editor, in the look of MLB Power Pros' own menus, for the **imported-uniform** feature of Arrange
mode. The player imports art (PNG / `.fsh` / `.iff`), drags logos and text over the uniform mould, and saves
`UserData/arrange/uniforms/<name>/uniform.ppu`, which the game loads for an Arrange team.

It reuses the Texture Studio's shell: `core.h`, `ui.h`, `power_pros_ui.h`, and the `gx`, `images` and
`config` cores (compiled from `../texture-studio/` by `CMakeLists.txt`, not copied).

## How it works

- **C++ editor** (`main.cpp`, `canvas.cpp`, `layers.cpp`, `preview.cpp`) — SDL3 + Dear ImGui, static.
  Layers are composited on the CPU into an atlas-resolution RGBA image (`layers.cpp:Compose`). That image is
  both what the canvas shows and what gets baked, so the editor is WYSIWYG. The over-256-colours check
  (`CountColors`) runs on the same image.
- **Python bridge** (`studio_bridge.py`) — the baking (`bake.py`) and `.fsh`/`.iff` decoding
  (`import_sources.py`) already existed in Python (tasks 2 and 3) and are tested. Rather than port them to
  C++, the editor invokes the project's Python interpreter with a single JSON job file:
  - **Save:** composite each non-empty atlas to a PNG, then `studio_bridge.py` bakes `uniform.ppu` with
    `bake.write_ppu`.
  - **Import `.fsh`/`.iff`:** `studio_bridge.py` dumps one PNG per layer plus an `index.txt`
    (`filename<TAB>suggest`), which the editor loads. **PNG/DDS** import needs no Python — the editor reads
    them directly with `images.cpp`.

  This was the simplest, lowest-risk option (the spec flagged the C++↔Python seam as a risk): no duplicated
  baking logic, and the `.ppu` format lives in exactly one place (`bake.py`).

### Interpreter path

The editor uses `--python <exe>` when given; otherwise it falls back to the dev interpreter
(`C:/Users/david/.pyenv/.../3.12.0/python.exe`) if present, else `python` on `PATH`. The launcher/Setup
should pass `--python` pointing at the interpreter it ships. `build.ps1` copies `studio_bridge.py`,
`bake.py`, `import_sources.py` and `packwii.py` next to the exe so the bridge resolves its imports.

## Mould

The body canvas shows a faint **mould guide** (chest/back/sleeves/trousers/cap). The editor draws an
**authored, schematic** guide itself (`layers.cpp:BuildMould`) — its own data, not derived from the game, so
nothing is committed. The exact region map of the 128×128 atlas is **game-derived** (it comes from the
indices in `PWM_mdl_main_Common.pack`) and therefore must never be committed. A player (or the Setup) can
generate a precise map from their own install and drop it at `UserData/arrange/uniform/body_regions.png`;
the editor loads it in place of the schematic when present. No game content ships with this tool.

## Build

```
pwsh tools/uniform-studio/build.ps1   # -> local/build/uniform-studio/UniformStudio.exe
```

## Run / test

- `UniformStudio.exe [--userdata DIR] [--tooldir DIR] [--python EXE] [--lang es|en] [--name NAME]`
- `--demo` preloads a generated logo + number (no game data) for screenshots.
- `--shot FILE.png` renders a few frames and writes a screenshot, then exits (visual testing).
- `python test_roundtrip.py` — composes atlases, bakes through the bridge, and re-reads with `read_ppu`.
