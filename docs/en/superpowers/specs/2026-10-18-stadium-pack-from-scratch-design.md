# Stadium packs from scratch — design (phase 1: the writer)

*Versión en español: [../../../superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md](../../../superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md)*

Date: 18 October 2026. Status: approved by the user on 18 October 2026.

## Why

Today a new stadium goes into the game **inside an existing stadium** (AT&T Park, Coors Field…): the Blender add-on empties its pieces, puts the model's geometry in and copies from the original everything it does not understand (materials, texmaps, headers, the stadium table). What is inherited blindly breaks things silently: on 18 October the texture repeats copied from the first texmap left the Polo Grounds stands grey on AT&T Park and fine on Coors Field, by luck.

The user chose as the long-term route **generating both stadium packs entirely from scratch, with no template** (and set aside the other route, a renderer of our own for `.glb`). The field inventory of 18 October (`docs/en/reverse-engineering.md`, «Field inventory for writing packs from scratch») shows that almost all of the format is decoded: what varies between stadiums is mostly pointers, counts and sizes. The placement flags, three table blocks and a few model fields are missing.

The whole project has four phases, each with its own design:
1. **The writer (this design):** write `stadium_<code>.pack` and `stadium_<code>tbl.pack` from a description of the stadium, without starting from any existing pack, and test it by rebuilding the game's stadiums.
2. Decode with the viewer what is missing (flags, table blocks, loose fields) and document it.
3. «From scratch» mode in the Blender add-on: objects carry their role and the add-on writes the packs with the phase 1 writer.
4. The Polo Grounds made from scratch, in the viewer and in a match.

The order was proposed by the agent and accepted by the user («adelante»): a tested writer first, so that what gets decoded later is tested on something reliable.

## Phase 1 goals

1. **A stadium description** in Python with everything needed to write both packs: models (meshes, materials, texmaps, shared textures and palettes), placements, collision and the table blocks. Fields not decoded yet go in it **as named values** (for example `material.unk_32`), not as chunks of the original's bytes.
2. **A reader** that turns a game stadium (its two packs) into that description.
3. **A writer** that turns a description into both packs, starting from an empty buffer. It receives and reads no existing pack.
4. **Tests** that the writer is correct (see «How it is checked»):
   - round trip of the 40 stadiums that have a table;
   - a minimal invented stadium, reading nothing from the game, that the game loads;
   - game stadiums rebuilt that the game draws the same as the originals.

## Non-goals

- Decoding the unknown fields: that is phase 2. In phase 1 they are read, kept and written back.
- Changing the Blender add-on or its current export: that is phase 3. The current exporter stays as it is.
- Changing the runtime. No new patch is needed: rebuilt stadiums go in as new stadiums of a mod (`[[stadium]]`), the route that already exists.
- Byte-for-byte equality with the game's packs. What matters is that the game loads them and draws them the same.

## Anti-goals

- **A writer that secretly copies** blocks from the original pack. If it does, phase 1 proves nothing. The writer only receives the description, and the tests watch for it.
- **Game content in the repository:** rebuilt packs, descriptions read from the game's packs and screenshots are game content. They are generated in `local/` and never committed. The tests read the user's `project.bin`.
- A writer that only works with what is in the game's packs (for example, one that depends on the order of the objects in the file). It must serve stadiums that never existed.

## What already exists

- **Readers** (pure Python, no Blender), in `tools/blender/powerpros_stadium/`:
  - `pj7m.py` reads the models: header, root, scene, node, meshes, materials, texmaps, textures and display lists;
  - `stadium_tbl.py` reads the table: root of 11 pointers, placements, collision and trees.
- **Partial writers**, which today modify an existing pack:
  - `pj7m_write.py` (`PackWriter`): adds RGB5A3 textures and materials, replaces meshes, splits meshes and orders triangles;
  - `tbl_write.py`: builds and writes trees A and B and the collision.
- **Tools:** `tools/stadium_re.py` (`check` checks the invariants on the 47 packs), `tools/projectbin.py`, `tools/packwii.py` (texture encoding).
- **Formats:** `docs/en/reverse-engineering.md`, section 5 («`PACKWII` format», «Stadiums: `PJ7M` models and `tbl` table», «Field inventory…»).
- **Runtime:**
  - new stadiums by mod (`[[stadium]]` in `mod.toml`, patch `wiicompiled-new-stadiums`): the runtime gives their files free numbers and finds the root of their table by searching from the end;
  - the stadium viewer (`WIICOMPILED_STADIUM_VIEWER`, initial camera through `WIICOMPILED_STADIUM_VIEWER_CAMERA`) and the match benchmark (`WIICOMPILED_MATCH_BENCH_STADIUM`);
  - the agent's capture tools: `tlog.py` with `TLOG_SHOTS`.

## Format facts that shape the design

- **The two packs go together.** The graphics pack is a `PACKWII` header (with no entry table) followed by textures, palettes and models, with no root of its own. The game reaches everything from the table: each placement points to its model's `PJ7M` header in the graphics pack, and the table's texture list points to texture headers in the graphics pack. The writer has to write both at once and know the graphics pack's offsets when writing the table.
- **Pointers:** `0x40000000 | offset` within their file. Display lists and texture pixels aligned to 32 bytes.
- **Table root:** at the end of the file, and the placements start at `0x20`: the runtime finds a new stadium's root by searching from the end for a count followed by a pointer to `0x20`. For a game stadium, the root's offset is pointed to from the REL's stadium table (`0x80E9F9D0`); for a new stadium, the runtime searches for it from the end. That is why the in-game tests use new stadiums.
- **Shared textures:** several pieces of the pack use the same texture, and each model's texture table and palette table end with an 8-byte zero entry.
- **Known limits:** at most 30 texmaps per piece, tree leaves of at most 127 entries and 65,535 collision triangles.

## Design

**Stadium description** (the agent's call: Python dataclasses, no Blender dependencies):
- `Stadium`: texture list, model list, placements, collision and table blocks.
- `Texture`: GX format, width, height, pixels already encoded and palette (or none). Models reference it by index, so shared ones are written once.
- `Model`: name, Maya node name, meshes and materials, plus the scene and root fields not understood yet (for example the `{1, → ?}` block).
- `Mesh`: material and primitives as they are, each with its type (strip `0x98` or triangles `0x90`) and its vertices as indices into the mesh's position, normal, colour and UV arrays. That way a rebuilt stadium draws exactly the same triangles, and phase 3 can give triangles only.
- `Material` and `TexMap`: the known fields by name, and the unknown ones also with a provisional name (`unk_32`, colours with no checked meaning…).
- `Placement`: model by index, translation, rotation, scale, box, sphere and flags.
- `Collision`: vertices and triangles with surface.
- Table blocks: `unk_20`, `unk_24` and the preloaded texture list (with its TMEM values) as records with fields, not as bytes. The preloaded texture list references the description's textures by index.

**Reader:** reuses `pj7m.py` and `stadium_tbl.py`; adds what they lack so no field is lost (the scene block, the table blocks, the material fields).

**Writer** (the agent's call: a new module next to the current ones, reusing the existing encoding and tree building):
- starts from an empty buffer per file;
- writes the graphics pack: `PACKWII` header, textures and palettes, and each model with all its structures, counts and pointers worked out;
- writes the table: placements with pointers into the graphics pack, tree A and tree B **built anew** with `tbl_write.build_tree` (the game's are not copied), collision, table blocks and the root at the end;
- checks the known limits while writing and fails with a clear message if they are exceeded.

**Command-line tool** (the agent's call): `tools/stadium_rebuild.py <project.bin> <game code> <mods folder> [new code]`. It reads the game stadium, writes it with the writer and leaves a mod with `[[stadium]]` (for example `rb_col`, base `col`) ready for the viewer and the benchmark.

## How it is checked

1. **Round trip of the 40 stadiums with a table:** read, write, read back what was written and compare with the description, field by field: they must be equal. The written packs pass the `stadium_re.py check` checks.
2. **The writer does not copy:** its only input is the description. A test also writes the description, passes it through a deep copy with no reference to the bytes read and checks the result is the same.
3. **Minimal invented stadium** (reading nothing from the game): a textured ground, a fence, a stand and a simple collision. The viewer shows it and the benchmark plays a 2-minute match with it without errors. It is the «no template» proof.
   - Loose undecoded fields (`material.unk_32`, placement flags…) take their most common value in the game's packs, as constants written by hand in the code (numbers, not copied data).
   - Undecoded table blocks (`unk_20`, `unk_24`, the preloaded texture list and `unk_2c`) are **absent**, with their pointer at 0: that is how they are in several of the game's tables (`unk_20` is missing in 4, `unk_24` in 3 and the texture list in 3). The agent's call.
   - If the game still does not load it because of an undecoded field, phase 1 decodes **only that field**, documents it and carries on. The agent's call.
4. **Rebuilds in the game:** Coors Field, AT&T Park and Yankee Stadium as new stadiums.
   - Viewer screenshots of the original and the rebuild from the same views (behind the batter and two free cameras): the same to the eye, with a pixel difference under a threshold set by the builder.
   - A 2-minute benchmark match with each rebuild, with no memory errors in the log.
5. **The usual tests keep passing:** the add-on's (`test_import`, `test_export`, `test_convert`, `test_preview`, `selftest`).

## Left to the builder

- Names of modules, classes and functions, and how the code is split between the reader, the description and the writer.
- The order of the objects inside each file, as long as the alignment and the root at the end are respected.
- The screenshots' pixel difference threshold.
- Whether the reader keeps extra information for comparing, as long as the writer does not use it.
- **The description lives in memory** (reader → writer, and later Blender → writer). In phase 1 a readable dump for debugging, in `local/`, is enough. A saved, stable format, if needed, is decided in phase 3. The agent's call.

## After phase 1

Phase 2 uses the minimal stadium and the rebuilds as a test bench: change a flag or a block in the description, write, look in the viewer and document. Its design is written when phase 1 ends.
