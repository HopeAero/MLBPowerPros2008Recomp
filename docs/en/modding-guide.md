# MLB Power Pros 2008 Modding Guide

*Versión en español: [../guia-mods.md](../guia-mods.md)*

This guide explains, step by step, how to make each kind of mod for the MLB Power Pros 2008 PC recomp: textures, uniforms and menus, stadiums and rosters. The technical details are in the reference documents linked at the end.

## What a mod is

- A mod is **a folder** inside `mods`, next to the installed game (`<install>\Game\mods`).
- **The game is never modified:** at start it reads the enabled mods and uses their files instead of the originals.
- Mods are enabled and disabled in the launcher: **MODS** in the main menu, or the **MODS** tab in Settings. The change applies when you press **PLAY**.
- Disabling a mod never breaks anything: the game goes back to the original.

## Before you start

1. Have the game installed with the Setup from your own copy of the disc.
2. In the launcher, **MODS** → **OPEN MODS FOLDER** opens the `mods` folder and leaves a `README.txt`.
3. The tools are in the launcher, **SETTINGS → TOOLS** (texture editor) and **MODS → TOOLS** (roster editors).

## A mod's folder

```
mods\
  MyMod\
    mod.toml       name, author and version (optional)
    textures\      replacement textures (PNG or DDS)
    project\       modified project.bin packages (.pack)
    roster\        a PowerUp / PowerUp+ roster (.dat)
    disc\          files that replace the disc's
    riivolution\   Riivolution patches (.xml)
```

Only the subfolder for the kind of mod you make is needed. `mod.toml` is optional; without it, the mod is named after its folder:

```toml
name = "My mod"
author = "Your name"
version = "1.0"
description = "What it changes."
```

## 1. Textures: Texture Studio

To change or improve any image in the game: menus, uniforms, logos, faces, stadium boards, skies.

1. Open **Power Pros Wii 2008 Texture Studio**: launcher → SETTINGS → TOOLS → **TEXTURE EDITOR**, or its shortcut.
2. **+ New mod** and give it a name.
3. Find what you want to change by category or with the search box. To find the textures of a given screen, use **Capture in game**: it marks with an eye the ones seen on that screen.
4. **Export PNG** (one texture) or **Export this view** (all of the category).
5. Edit the PNGs in Photoshop, GIMP or Krita, or upscale them with AI (chaiNNer, Upscayl). **Do not change the file names.**
   - You can make them bigger (HD), with the same aspect ratio: ×2, ×4 or ×8 is ideal, up to 4096 pixels.
6. **Import PNG...** or **Import folder...**. The report says what went in and what did not, and why.
7. **Enable** and open the game.
8. To share it: **Package .zip**.

More in [texture-studio.md](texture-studio.md).

## 2. Uniforms, menus and other packages

Almost all of the game's content is in `project.bin`, in `.pack` packages. A mod can carry modified packages in `project/`, under their exact name (for example `project/PWM_mdl_uniform_MLB_BOS_00.pack`), and the game uses them at start.

- **Change a package's textures** (without Texture Studio):

  ```
  python tools/projectbin.py extract <project.bin> pack env_mainmenu
  python tools/packwii.py export pack/env_mainmenu.pack png
  #   edit the PNGs without changing their size
  python tools/packwii.py import pack/env_mainmenu.pack png MyMod/project/env_mainmenu.pack
  ```

- **Swap one package for another**: copying one team's uniform under another team's name makes the second wear the first's.
- To find which package each screen uses, start the game with `WIICOMPILED_DEBUG_DVD=1`: the log shows each read and `projectbin.py list` turns it into a name.
- The mod ships **only** the `.pack` files it changed, never the whole `project.bin`.

More in [mods.md](mods.md).

## 3. Stadiums: Blender

With the Blender add-on you can edit the game's stadiums or bring in one of your own as a new stadium, which shows up as one more entry in the Exhibition stadium select.

**Setup**
1. Blender 4.2 or newer.
2. Install the add-on: **Edit > Preferences > Get Extensions** → ⌄ menu → **Install from Disk…** → `powerpros_stadium-<version>.zip`.
3. **Import a stadium** from the installed game folder (the one with `UserData`): File > Import > Power Pros 2008 stadium, or the sidebar (N key), **Power Pros** tab.

**Edit a game stadium**
- Move, rotate or scale pieces, edit their meshes or their images. The `pp_*` properties keep what the export needs: do not delete them.

**Bring in your own model** (SketchUp, glTF, OBJ…), from scratch
1. **Power Pros** panel → **New stadium from scratch**. You pick a **data stadium**, which only brings saves, menu, sky and sounds; the new collection already holds the game's bases, plate and mound.
2. Import your model (a `.skp` is turned into `.glb` first with OpenSKP) and put it in that collection, in metres, with home plate at the origin, the ground at height 0, second base towards +Y and 90 ft between bases, fitted to the game's bases.
3. Give each object its **role**: ground, stand, fence, crowd (the game puts its animated people), flags (they move with the wind), transparent or additive glow.
4. **Generate collision**: it comes as one object per kind (fences, stands, poles…), shown or hidden in the panel's Collision box; **Make collision** turns any mesh into collision.
5. The stadium is written entirely from your model. What is taken from the game, and the other route, putting the model inside a game stadium with "Convert objects to pieces": [stadiums.md](stadiums.md), «Your own model».

**See how it looks in the game, without playing a match**
1. Put the 3D view where you want to start.
2. **Power Pros** panel → **View in game**. In about 8 seconds the game opens in your stadium, with the camera at your view, no players or scoreboard.
3. Move with W/A/S/D, Q/E (down and up), Shift (fast), right mouse button + mouse (look) and the wheel (speed). 1, 2 and 3 show the match cameras (batter, play, wide), F goes back to the free camera, C shows the collision (by colour, through everything) and H hides the help.
4. Change something in Blender and press again: the previous game closes and opens with the change. Your `mods` folder and your settings are left alone.

**Export**
1. **Export stadium mod**, with a short code (for example `polo`) and a name. A stadium from scratch always comes out as a new stadium; one imported from the game, as **New stadium** or **Replace a stadium**.
2. Pick the game's `mods` folder. Optional: **Menu photo** and the line under the photo («Home of …»).
3. Enable it in MODS and pick it in Exhibition.

Tips for a good look in the game, limits and known issues: [stadiums.md](stadiums.md).

## 4. Rosters: PowerUp+

1. Launcher → MODS → TOOLS → **EDIT ROSTER (PowerUp+)**.
2. Edit players and teams, or generate a real roster from the MLB Stats API.
3. **Export Roster → MLB Power Pros 2008 Mod** downloads a zip with the finished mod (`mod.toml` and `roster\<name>.dat`).
4. Unzip it into `mods` and enable it in MODS.

- The roster applies to every mode, **MLB Life included**.
- A roster mod has **its own saves**: a career or a season started with it only makes sense with that roster, so it is saved separately. The normal saves are not touched.

More in [roster-2008.md](roster-2008.md).

## Success players: copy, paste, export and import passwords

Players you make in Success are shared with a 156-character password. In the original game you had to copy it from a photo and type it with the controller; here it goes as text:

- **Copy:** open My Data › View or Delete Custom Players › the player › Password Display and press the orange **Copy password** button (on the bottom bar) or **Ctrl+C**. The password is on the clipboard as text, in groups of 3 and rows of 18 as on the screen, to paste in a message, a forum or a file.
- **Paste:** open My Data › Password Input, press the **Paste password** button (under OK / INSERT / EXIT) or **Ctrl+V** and then OK. The player is saved as if you had typed it.
- **Export to a file:** on Password Display, the blue **Export file** button opens your system's save window, with the player's name (`S. Ohtani.txt`) in the `Players` folder of your user data; you can save it anywhere.
- **Import a file:** on Password Input, the blue **Import file** button opens a list of the `.txt` files in `Players` (newest first); pick one and then OK. **Browse...** opens the open window for a file in another folder. It also works without a clipboard.
- **With a controller:** on those screens **X** presses the first button (Copy / Paste) and **Y** the second (Export / Import). With a controller, Export saves straight into `Players` with no window, and the Import list is driven with the D-pad, **A** to import and **B** to cancel. (Not with the Wii Remote yet.)

When pasting, spaces and line breaks are skipped, and stand-ins are taken for the symbols that are not on the keyboard: `-` (−), `/` (÷), `!=` (≠), `@` (◎), `^` (△), `*` (☆), `->` (⇒), `<->` (⇔), `■` (□), `▼` (▽), `◆` (◇), `0` (O). A notice at the bottom shows the result. A Success player also has the right age in MLB Life with a season pack.

## 5. Other kinds

- **`disc/`**: files with the same path as in the disc's `files` folder, for example sounds.
- **`riivolution/`**: Riivolution patches for `RL8E54`. File patches work; memory patches only work for data, because the game's code is already native. Not tested on MLB yet.

## Testing your mod

- Enable only your mod and press PLAY. If you do not see the change, check that the mod is enabled and that the file names are exact.
- **Conflicts:** if two enabled mods carry the same file, the first in alphabetical order wins.
- For stadiums, test with **View in game** first and play a match at the end.

## Sharing your mod

- Zip the mod's folder. Whoever uses it unzips it into their `mods` folder.
- **Never include game content:** no `project.bin`, no executable, no unmodified original textures, no packages you did not change. A mod carries only its own files or differences.
- `.blend` files with imported stadiums contain the game's models and textures: use them to make your mod, but do not share them.
- If you use other people's models (Sketchfab, 3D Warehouse…), respect their licence.

## Common problems

| Problem | Fix |
|---|---|
| The mod does not show up in MODS | It must be a folder directly inside `mods`, not a folder inside another. |
| The changes do not show | Mods apply when you press PLAY. Check that the mod is enabled and the file names. |
| The first match with a mod takes longer | The game prepares its copy of `project.bin` (406 MB) the first time; then it reuses it. |
| Stadium with grey stands or striped textures | Update the Blender add-on to 0.7.3 or later and export again. |
| In Blender the converted stadium looks grey | Use the **Material** view (Z > Material Preview). |
| In «View in game» the camera moves by itself | The active game window gets your keys: switch windows before typing. |
| My saves vanished when I enabled a mod | Roster mods have their own saves; the others stay saved. In MODS, **SAVES IN USE** says which ones the game uses and opens their folder (v0.1.2). |

## More information

- [mods.md](mods.md): mod reference (folders, `project.bin`, per-roster saves).
- [texture-studio.md](texture-studio.md): Texture Studio.
- [season-studio.md](season-studio.md): Season Editor (start year, DH, leagues, real schedules).
- [stadiums.md](stadiums.md): stadiums in Blender.
- [roster-2008.md](roster-2008.md): rosters and PowerUp+.
- [reverse-engineering.md](reverse-engineering.md): the game's formats, for those who want to go further.
