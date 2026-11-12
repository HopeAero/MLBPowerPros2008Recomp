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
  An image layer is resampled from its untouched source each time (`layers.cpp:ResampleArea`: exact-area
  average per axis when shrinking, nearest when enlarging), then turned about its box centre by `rot`
  degrees with 4 × 4 supersampling. The canvas handles work in the turned box (`canvas.cpp:Frame`); the
  knob above the top side turns it.
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

## Layout

Header: New, Open, name, Save (`*` = unsaved), Undo / Redo. Left: tabs Colours (pieces, line type, other
bases), Logos & text (import, add text, the active part's layers), Number and Drawing. Centre: zone buttons
(`zones.cpp`), the layer toolbar and the mould (`canvas.cpp`, no panel of its own). Right: the figure and
the selected layer's properties. Undo / redo (`main.cpp` `TrackHistory`): a snapshot of the editable state
is recorded whenever it changed and the player is idle (no item active, no button down), 40 steps; checked
by `--undo-selftest`. Save also writes the editable project (`project.cpp`:
`UserData/arrange/uniforms/<name>/studio/project.txt`, one PNG per image layer, `thumb.png`), which Open
lists as cards and loads back. Uniforms saved before projects existed are listed too ("older", thumbnail
read from the .ppu in `project.cpp`) and open through the bridge's `readppu` op, each atlas as its part's base.

## Starting a uniform

- **Plantilla / Template** — an original (not game-derived) starter painted inside the body regions:
  plain white, pinstripes, two-tone, placket stripe (`layers.cpp:BuildTemplate`). Pick one and add logos
  and text on top.
- **Cargar de mi juego… / Load from my game…** — decode a team uniform from the player's own files as a
  base layer: pick a `PWM_mdl_uniform_MLB_*.pack`; the editor finds `PWM_mdl_main_Common.pack` nearby (the
  body C8 atlas is common; the pack only supplies the 256-colour palette) and the bridge's `pack` op decodes
  it. **The `.pack` files are the player's game data and are never written to the repo.** If no packs are
  present, only the original templates are available.

## Preview

The preview panel toggles between a software-rendered 3D figure wearing the composed uniform and the flat
composed **atlas**. With the game installed, the figure is the **game's own chibi** (`game_doll.py`: the
body, head-with-cap and hands PJ7M models of `PWM_mdl_main_Up.pack`, skinned with the body's 19-joint
skeleton in a standing pose), read from the player's `project.bin` by the bridge's `gamedoll` op into
`UserData/arrange/uniform/game_doll.bin`. Without the game it falls back to the editor's own mannequin
(`render3d.cpp:BuildMannequin`). The saved thumbnail (`.ppu` kind `thumb`, 64×64) is the same figure from
the front.

The game's model maps the left and right halves of the body to the same atlas pixels (sleeves, trousers,
cap sides: mirrored; only chest and back are unique), so the preview shows a sleeve logo on both arms, as
the game does. "Wristbands" hides the figure's wristbands (the body model's two 32x16 textures and their outline hulls,
`render3d.cpp:WithoutBands`): they are each player's equipment, not the uniform. Test flags: `--rotate <deg>`
(the selected layer), `--no-bands`.

## Colour by piece

"Start from scratch: colour by piece" paints the body atlas as the game's Arrange editor does: CAP, BRIM,
SLEEVES/ETC, UPPER, LOWER, LINE and LINE TYPE, each one of the game's 32 colours. `pieces.cpp` composes the
256-entry palette exactly as the game (`0x806BDE24`, checked entry by entry against the running game) over
the body atlas indices; the indices, the Arrange template palette and the 32 colours come from the player's
own game through the bridge's `gamebase` op (`UserData/arrange/uniform/game_base.bin`). The result is the
base layer; logos and text go on top. The backup colours follow the pieces. `--pieces` starts in this mode
and `--pieces-selftest` checks the port on synthetic colours.

None of the caches is ever committed or shipped; `build.ps1` copies `projectbin.py`, `game_doll.py` and
`pj7m.py` beside the exe so the bridge can read the player's disc.

## Drawing and free colours

The Drawing tab (tools above the mould, colour and layer on the left, patterns with a preview in the
right panel) paints into the part's drawing layer (`Layer::isPaint`: an atlas-sized image at (0, 0), scale 1,
found or made by `canvas.cpp:PaintLayer`) with `paint.cpp`: brush, line, rectangle, fill (over the composed
image's colour patch), eraser, picker and line patterns (stripes, diagonals, checks), optionally kept inside
the zone's mask. A piece can take any RGB (`Pieces::rgb`, -1 = the game colour): mid / dark shades use the
game colours' average brightness ratios (`pieces.cpp:PieceRgb`), the backup bytes the nearest game colour
(`NearestColor`). The number keeps the 32 game colours (the game paints it). `--paint-selftest` checks it all
on synthetic data; `--paint-demo`, `--free-rgb piece,RRGGBB` and `--tab 3` are for screenshots.

## Build

```
pwsh tools/uniform-studio/build.ps1   # -> local/build/uniform-studio/UniformStudio.exe
```

## Run / test

- `UniformStudio.exe [--userdata DIR] [--tooldir DIR] [--python EXE] [--lang es|en] [--name NAME]`
- `--demo` preloads a generated logo + number (no game data) for screenshots.
- `--shot FILE.png` renders a few frames and writes a screenshot, then exits (visual testing).
- `python test_roundtrip.py` — composes atlases, bakes through the bridge, and re-reads with `read_ppu`.
