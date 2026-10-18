# In-game stadium preview — design

*Versión en español: [../../../superpowers/specs/2026-10-18-stadium-preview-design.md](../../../superpowers/specs/2026-10-18-stadium-preview-design.md)*

Date: 18 October 2026. Status: approved by the user on 18 October 2026.

## Why

Someone making a stadium with the Blender add-on (`tools/blender/powerpros_stadium`) cannot trust what Blender shows: the model looks fine there and, once in the game, shows faults Blender does not (flickering surfaces, pieces not drawn, too many textures, the base stadium's flags or crowd, an odd plate or colours). Today, to see them, you export, enable the mod in the launcher, open the game and start a match. It is slow debugging the user should not have to do.

What the user asked for: a button, while modelling and before really exporting, that shows the stadium **as the real game draws it**, without starting a match, and lets them move around it freely. They preferred the real game over an approximation inside Blender because it is the faithful option.

## Goals

**Phase 1 (this design):**
1. A **"View in game"** button in Blender's Power Pros panel.
2. The button exports the scene's stadium to a temporary preview mod and opens the installed game in a **viewer mode**: no launcher, no menus, no Wii Remote strap warning screen, straight into that stadium.
3. In the viewer, a **free camera** driven by keyboard and mouse, and keys to jump to match views (behind the batter, from the pitcher, aerial). Those views are **the game's real cameras** (user's decision): same position, framing and field of view as in a match, and with them the game shows or hides the camera-dependent pieces (placement flags `0x80000000`, `0x40000000`, `0x20000000`: `NOUPCAM`, `EVC`, `2F` stands), which is exactly a fault the preview must reveal.
4. The camera starts at the same position and orientation as Blender's active 3D view.
5. Only the stadium: no players, no scoreboard or HUD, with the match logic stopped (players and ball). The stadium keeps animating: crowd, flags and animated boards move as in a match, because their faults must show too.
6. If a viewer window is already open, the button closes it and opens a new one with the updated stadium. Time target: the stadium on screen about 8 seconds after pressing.

**Phase 2 (later, outside this plan except what prepares it):** the viewer stays open and the stadium updates by itself when something changes in Blender, within seconds and without restarting the game.

## Non-goals

- Nothing is edited inside the game: you model in Blender and the game only shows.
- It is not a new editor and does not replace Blender. An own editor or in-game editing remain a very distant idea, only if Blender does not fully solve the problem.
- It is not an approximate view inside Blender (dropped in favour of the real game).
- It does not change the normal game: with the viewer off, the match, the benchmark and the launcher behave as before.

## Anti-goals

- The preview touching the user's mods or settings (`UserData/Config.toml`, the `[mods] enabled` list, `build/mods`) in a way noticeable outside the preview.
- The viewer showing something other than what a real match with that mod would show (another loading path, other drawing). The viewer must load the stadium the same way a match does.
- The free camera leaving the game in a state that hangs or corrupts memory (the lesson of the 30-texture limit: a silent fault that only shows as "weird things").

## What already exists and is verified

**Runtime** (`local/wiicompiled-mlb/src/runtime`, changes as patches in `tools/patches/`, registered in `tools/wiicompiled-translate.ps1`):
- `match_benchmark.cpp` (enabled with `WIICOMPILED_MATCH_BENCH=1`) already takes the game straight to a Yankees–Rockies match: it skips scenes, prepares the configuration (`0x8104E1D8`), enters the match scene (409) and skips the match introduction.
- `WIICOMPILED_MATCH_BENCH_STADIUM=<code>` picks a new stadium from an enabled mod (`new_stadiums::ForceForMatch`).
- The test environment variables already keep the launcher from showing (`launcher.cpp`, `AutomatedRun`).
- Enabled mods come from `Config.toml` `[mods] enabled`; a stadium mod declares `[[stadium]]` in its `mod.toml` and brings `project/stadium_<code>.pack`, `stadium_<code>tbl.pack` and, optionally, `env_stadium_menu_<code>.pack` (`mods.cpp`, `hle/mlb/new_stadiums.cpp`).

**Measured on 18 October 2026** (test match, RX 5500):
- From opening the game to the first scene takes 23 s; 21 of them are the strap warning screen waiting for A. Pressing A automatically (`WIICOMPILED_PAD_SCRIPT=2500-2700:100,3500-3700:100`), the stadium is on screen at ~8 s. Loading the match itself takes ~3 s.
- Writing scene 3 into the scene manager (`0x81033268`: `+1028` next scene, `+1040` and `+1043` set to 1) from the match takes it out and the benchmark puts it back into 409 in ~0.1 s; the match stays stable at 60 FPS. But **the game does not read the stadium files again**: it keeps the one already loaded (no `[stadiums] loading` message). Asking for 409 while in 409 does nothing.
- The match camera is **not** built with the SDK's `C_MTXLookAt` (`0x800A8DC4`): only two DOL functions call it (`0x800A8D6C`, `0x80090AF8`). A probe wrapping `C_MTXLookAt` in the generated shard hung the boot right after GX started (cause not investigated). The game builds its camera with its own code, not located.

**Blender add-on** (version 0.6.0):
- `exporter.export_stadium(coll, mods_folder, mod_name, target_code, new_name, hd, menu, home_line)` writes a complete mod (packs, HD textures, menu pack, `mod.toml`) and raises `ExportFailure` with clear messages (for example, more than 30 textures in a piece).
- The imported stadium's collection stores `pp_game_folder` (the installed game folder) and `pp_project_bin`.
- Game frame in Blender: metres, Z up, plate at the origin, second base towards +Y (`importer.TO_BLENDER` / `FROM_BLENDER` convert to decimetres, Y up).

## How it works (decided)

**Blender:**
- A "View in game" button (new operator in `blender_ui.py`), visible when the scene has an imported stadium.
- It exports as a **new stadium** to its own preview folder, with a fixed code reserved for the preview, and with the same warnings as the normal export (if it fails, it shows the error and does not open the game).
- It opens `WiiCompiled.exe` from `pp_game_folder` in viewer mode, passing through the environment the stadium to show, the preview mod folder and the initial camera (position and orientation of the active 3D view, converted to the game frame).
- If a viewer launched by Blender is open, it closes it before opening the new one.

**Runtime, viewer mode:**
- It builds on the benchmark path (scenes, match preparation, forced new stadium) so the stadium loads exactly as in a match.
- It enables the preview mod **only for that run**, without writing `Config.toml` (the user's other mods are not loaded in the preview).
- It accepts only the strap screen and skips the rest, to get there in ~8 s.
- Free camera with Blender/Unreal-style controls: right click + mouse to look, WASD to move, Q/E down and up, wheel for speed, Shift to go faster. Keys for the match views (the game's real cameras).
- It hides players, ball, scoreboard and HUD and stops the match logic. If hiding the players proves hard, the first version may show them; the rest still applies.
- Without viewer mode, nothing changes.

**Phase 2, prepare without doing:** shape the way the stadium reaches the viewer (preview folder and code, process launched from Blender) so that "close and reopen" can later be replaced by "tell the open viewer and reload". Leads for the reload: alternate between two copies of the stadium so the game thinks it is another one, or free the loaded stadium before going back to scene 409; and have the DVD layer serve the new files (today `mods` builds `project.bin` at startup).

## Decisions taken in the review (the assistant's, not the user's)

- **Replacement stadiums:** the preview always loads the stadium as a new stadium based on the imported one, even if the user plans to export it as a replacement for another. The geometry and textures shown are the same; only the replaced stadium's game data (dimensions, saves) would differ, and those are not visual. Remembering the export options on the collection and a second "replace" path are out.
- **`project.bin` copy:** `mods::Apply` builds a full `project.bin` (~406 MB) in `ApplicationDataDirectory/Cache/mods/disc/` and rebuilds it when packs change (measured: ~0.8 s, within the ~8 s). The preview uses **its own cache folder**, so it does not invalidate the normal games' copy or clash with a normal game open at the same time; it costs ~400 MB more disk. Serving the packs without a copy is phase 2 work.
- **What stops:** only the match logic (players and ball). The guest game keeps running so the stadium keeps animating.

## Constraints

- No game data in the repository (`tools/check_no_game_data.py`). The preview writes to the user's local folders, never to the repo.
- Runtime changes as reproducible patches over the pinned Wiicompiled; each patch applies cleanly on the full stack.
- Interface in Spanish and English (`T(...)` in the add-on; the runtime already has `Tr(...)` in the launcher); documentation in `docs/` and its mirror `docs/en/`.
- The add-on keeps working without the recomp (the preview only needs `pp_game_folder` with a `WiiCompiled.exe`); if it does not find it, it says so.
- Tests: the headless Blender ones (`tools/blender/test_*.py`) and the runtime ones (`runtime/tests`) keep passing; new non-trivial logic leaves its test.

## Left to the builder

- Locating the game's camera (structure and when it is written), how to control it and how to ask it for its real match views; also how to hide players, ball and HUD.
- Names of environment variables, the reserved code and the preview folder, and the format the initial camera is passed in.
- How the runtime enables an extra mod for one run only.
- The exact keys for the match views and the camera's default speed.
- How Blender finds the open viewer to close it.
- Whether a new patch for the viewer is better or extending `match_benchmark`.
