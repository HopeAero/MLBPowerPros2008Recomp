# Stadiums from scratch in Blender — design (phase 3)

*Versión en español: [../../../superpowers/specs/2026-10-19-stadium-from-scratch-blender-design.md](../../../superpowers/specs/2026-10-19-stadium-from-scratch-blender-design.md)*

Date: 19 October 2026. Status: approved by the user on 19 October 2026 («adelante»).

## Why

Today a custom stadium goes into the game **inside an existing stadium**: the add-on empties the base's pieces and puts the model's geometry in them. What is inherited from the base without being understood has broken things silently (textures the game stretched, tree A, the base's crowd and flags). The user chose the route of **generating the whole packs from scratch**.

Phase 1 built the writer: it writes both packs from a description of the stadium, tested with the game's 40 stadiums and with an invented minimal stadium that plays (`docs/en/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md`). Phase 2 found out with the viewer what the placement flags do (`docs/en/reverse-engineering.md`, «What the fields do, tested in the viewer»). This phase 3 brings all that to Blender: modelling a stadium with no base and taking it to the game. Phase 4 will be the Polo Grounds made this way.

The user answered «continua» to the proposal of moving on to phase 3. The decisions in this design are the agent's unless stated otherwise, and are marked for review.

## Goals

1. **«From scratch» collection.** An action in the Power Pros panel creates an empty from-scratch stadium collection. Each Blender object carries its role.
   - **Grouping:** the exporter merges into one piece of the pack the objects with the same role and the same «Hide from above» box, and splits pieces at 30 textures and by size (the current exporter's mesh limits).
   - That way a model with hundreds or thousands of objects (phase 4's Polo Grounds) ends up as few pieces.
   - Pieces are written in draw order (ground, opaque, transparent, additive) and stay under 127, to use the one-leaf tree A, which keeps that order. If they still exceed 127, the exporter warns.
   - The agent's call.
2. **Role of each piece.** In the panel, each object has a **role** the add-on turns into the game's flags:

   | Role | Flags | What it does in the game (phase 2) |
   |---|---|---|
   | Ground | `0x04810000` | background layer, drawn before the rest |
   | Stand (opaque) | `0x00000000` | normal piece |
   | Fence | `0x00001100` | fence type (gameplay effect) |
   | Transparent | `0x00060000` | soft alpha blending (glass, nets, cut-out boards) |
   | Additive glow | `0x000A0800` | lights that add up (floodlights) |
   | Lying crowd | `0x00001600` | crowd animated by the game |
   | Upright crowd | `0x00001700` | crowd animated by the game |
   | Flags | `0x00000500` | drawn by the game's flag system |

   In addition, a **«Hide from above»** box adds bit `0x80000000` (`NOUPCAM`) for what must not be seen from high cameras (outside buildings).
   Default role (the agent's call): «Ground» for flat objects at height 0, «Stand» for the rest.
3. **Export from scratch:** the add-on builds the stadium's description (`stadium_model.Stadium`) from the scene and writes it with `stadium_write.write_stadium`. It reads or copies no piece of a game stadium, with one exception: the bases, plate, mound and their lines.
   - **Game bases:** a «Game bases» box, on by default, copies from the player's `project.bin` the `BCL` pieces of the chosen data stadium (the agent's call). Players always stand in the same place, and those pieces fit it.
   - **Data stadium:** the new stadium still needs a game stadium number for saves, menus, sky and sounds (`base` in `mod.toml`). It is chosen at export, as today.
   - **«New stadium» only:** a from-scratch stadium does not replace a game one. The game stadiums' table root is fixed in the REL, and the runtime only searches it for added stadiums. Replacing would require changing the runtime. The agent's call.
4. **Conversion of each object**, with the rules that already work in «Convert objects to pieces»:
   - material colour and a soft light baked into the vertex colours;
   - of two faces in the same place one stays, the textured one first and turned to the field;
   - separation of stacked surfaces;
   - textures to RGB5A3 up to 256 pixels, plus their HD version;
   - texmap repeats worked out.

   An object with more than 30 textures is split over several pieces automatically.
5. **Collision:** the collection's «Colisión» mesh if there is one; otherwise it is generated with `collision_gen` as today.
6. **«View in game»** works with a from-scratch collection just as with an imported one.
7. **Menu photo and line** as in the current export.

## Non-goals

- Night floodlights (`unk_20`), dugouts (`unk_24`), preloaded textures and camera culling with a tree A of our own. They stay empty or with a single leaf, as in the minimal stadium. More than 127 pieces use the writer's spatial tree.
- Scoreboard with a live score and a video board with a picture: they need texture conventions not decoded yet. In this phase a scoreboard or a board are normal pieces («Stand» or «Transparent»).
- Flags showing in the viewer: their system stops with the frozen match (pending separately).
- Changing the current flow of stadiums based on a game one: it stays as it is.

## Anti-goals

- A role promising something the game does not do. Each role in the table is checked in the game during implementation. One that does not work as described is removed or corrected before delivery.
- Game data in the repository or in the mod beyond the `BCL` pieces of the player's own `project.bin`, written to their mods folder as today.
- A silent failure. What the writer refuses (`StadiumWriteError`) shows in Blender as a clear message naming the object.

## Known risks

- **Animated crowd:** the game animates it through its type, but it is not known yet what it does with the textures of a custom crowd (the frames may come from the preloaded textures, which will be empty). If it does not look right in the game, the «Crowd» role falls back to the stand type (still) and this is documented.
- **Flags:** the flag system may need the flag in local coordinates and with a given shape. If it does not work with arbitrary objects, the role is removed and this is documented.

## How it is checked

1. **Headless Blender test** (`tools/blender/test_scratch.py`): a from-scratch collection made of simple shapes, one object per role.
   - It exports and reads the packs back with `stadium_read`.
   - Each piece carries its role's flags.
   - The game bases are there.
   - The collision exists.
   - `stadium_re` finds no problems.
   - The mod declares the new stadium.
2. **In the game:** that collection in the viewer and in a 2-minute match with no errors. Each role is looked at in the viewer: crowd, flags, transparency, additive, ground, fence and «Hide from above».
3. **The usual tests keep passing:** the add-on's and `tools/test_stadium_writer.py`.

## Left to the builder

- How the role and the box are stored on the object (custom properties) and shown in the panel.
- How the code is split: a new module that builds the description from the scene, reusing what is needed from `convert.py`, `exporter.py` and `collision_gen.py`.
- Piece names in the pack (the object's, in ASCII).
- The interface in Spanish and English with `T(es, en)`, and the documentation in `docs/stadiums.md` and its mirror.
