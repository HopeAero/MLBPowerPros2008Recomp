# Mods

*Versión en español: [../mods.md](../mods.md)*

**First time?** The [modding guide](modding-guide.md) explains it step by step; this is the reference.

Mods are enabled in the launcher: **MODS** in the main menu (with the MoonInning art) or the **MODS** tab in Settings (with the hand-drawn style). Each change is saved in `Config.toml` (`[mods] enabled = [...]`) and applied when you press PLAY (JUGAR), because the game reloads mods at startup.

## Where they go

In the `mods` folder next to the executable (`<installation>\Game\mods`). The **OPEN MODS FOLDER** (**ABRIR CARPETA DE MODS**) row opens it and leaves a `LEEME.txt` in it. Each mod is a folder:

```
mods\
  UniformesHD\
    mod.toml
    textures\      replacement textures
    disc\          files that replace the disc's files
    riivolution\   Riivolution patches (.xml)
    project\       project.bin packages (.pack/.dat/.arc) with their names
    roster\        a PowerUp / PowerUp+ roster (.dat)
    schedule\      Season mode schedules (<year>.csv)
```

`mod.toml` is optional; without it, the folder name is used:

```toml
name = "HD Uniforms"
author = "Your name"
version = "1.0"
description = "Uniforms redrawn at 4x."
```

## Content types

- **`textures/`**: texture replacements with Dolphin names, `tex1_<width>x<height>_<hash>[_<palette hash>]_<format>.png` or `.dds`. They can have a higher resolution than the original.
  - **The recommended way is Power Pros Wii 2008 Texture Studio** (`docs/en/texture-studio.md`): it shows all textures, exports them to PNG and saves your edited or HD versions with the correct name in your mod's `textures/`.
  - By hand: to get the names, set `texture_replacements = true` and `texture_dumps = true` under `[video]` in `UserData\Config.toml`. Play until you reach the screen you want to change: the game's textures are exported to `UserData\Cache\texture_dumps`.
  - Edit the one you want and save it in your mod's `textures/` with the same name.
  - Many textures are atlases, that is, one image with several pieces; only the areas the game uses are visible.
- **`disc/`**: files with the same path as inside the disc's `files` folder. For example, `disc/sound/SeData.bin` replaces `files/sound/SeData.bin` and `disc/project.bin` replaces the content file (see below).
- **`project/`**: `project.bin` entries with their exact name, for example `project/PWM_mdl_uniform_MLB_BOS_00.pack`.
  - At startup, the game generates a copy of `project.bin` with those files in `UserData\Cache\mods\disc\project.bin` and uses it instead of the disc's.
  - The copy is reused as long as neither the mods nor the disc change. Generating it takes less than a second on an SSD and it takes up 406 MB.
  - If two mods bring the same file, the first in alphabetical order wins.
  - It is the right way to distribute `project.bin` changes: the mod only carries its `.pack` files, never the whole file.
- **`roster/`**: a [PowerUp / PowerUp+](https://github.com/ogura-connection/PowerUpPlus) roster (the MLB Power Pros 2007 `.dat`). The game converts it to the 2008 format at startup and it works for all modes, **MLB Life included**. Details in `docs/en/roster-2008.md`.
- **`schedule/`**: Season mode game schedules, one per year: `schedule/2026.csv` replaces the games of the season the game calls 2026 when the game builds that year's schedule, that is, when the game starts (the first year) or on the year rollover. A season already under way does not change.
  - Format: one line per game, `date,home,away,time`. The date is `YYYY-MM-DD` (or `MM-DD`), teams go by code (`BOS`, `NYY`, `LAD`…; `MIA`/`FLA`, `ATH`/`OAK`, `TBR`, `CHW`, `WSN`… work too) and the time is the start time in Eastern time, `HH:MM` (19:05 when missing). Blank lines, lines starting with `#` and the `date,...` header are skipped.
  - The game's limits: March 1 to October 31 and 16 games a day; what does not fit is dropped and the log (`console.log`) says so. The game's regular season runs from March 19 to October 1. The game's All-Star Game stays on its day.
  - A year's real schedule comes from MLB's public API: `python tools/season_schedule.py mlb 2026 mymod/schedule/2026.csv`. To play the 2026 schedule in the first year, save it as `schedule/2008.csv`, or better make a season pack (below).
  - The schedule does not change divisions: for Houston to count in the AL West a season pack needs `[divisions]` (below). The DH in both leagues is separate (`dh = "both"` or the launcher option). Details in `docs/en/reverse-engineering.md`, "Game schedule".
- **`riivolution/`**: Riivolution XMLs (`<wiidisc>`) valid for `RL8E54`. The mod folder acts as the SD root.
  - File patches work.
  - **Memory** patches only work for data: the game code is already translated to native, so changing PowerPC instructions in memory has no effect. The same goes for Gecko/Action Replay codes that modify code.

If two active mods replace the same texture, the one in `UserData\texture_replacements` wins, then the first mod in alphabetical order.

## `project.bin`

Almost all MLB content (models, uniforms, menus, stadiums, data) is in `files/project.bin`, a custom 406 MB file with 2,424 entries (`.pack`, `.dat`, `.arc`). `tools/projectbin.py` reads and rebuilds it:

```
python tools/projectbin.py list    <project.bin>                      # position, size and path of each entry
python tools/projectbin.py extract <project.bin> <folder> [filter]    # e.g. filter "uniform_MLB_BOS"
python tools/projectbin.py build   <project.bin> <replacements> <output>
```

`build` copies the original and replaces the entries whose name (e.g. `PWM_mdl_uniform_MLB_CHC_00.pack`) is in the replacements folder, recalculating sizes and positions. Without replacements it reproduces the file byte for byte. You don't need it to play: just put the `.pack` files in the mod's `project/`, and the game does the same at startup (with a result identical to `build`'s). `extract` is for pulling out the original to edit it.

To find out which entry each screen uses, run the game with `WIICOMPILED_DEBUG_DVD=1`: the log shows each `project.bin` read with its position, which `projectbin.py list` translates into a name.

Format (verified on all 2,424 entries): header with version 2 and sector 0x800; 0x1C-byte records at 0x6000 (name, sectors, alignment, size); order table at 0x26000 (record, start sector); names at 0x2B000; data from 0x3D800. The internal format of the `.pack` files is in `docs/en/reverse-engineering.md`, section 5.

A mod must not distribute a full `project.bin` (it is game content): only its `.pack` files in `project/`.

### Textures inside a `.pack`

`tools/packwii.py` pulls the textures out of a `.pack` to PNG and puts back the ones you edit. It needs `pip install numpy pillow`.

```
python tools/projectbin.py extract <project.bin> pack env_mainmenu
python tools/packwii.py info   pack/env_mainmenu.pack                 # textures, format and palette
python tools/packwii.py export pack/env_mainmenu.pack png             # one PNG per texture + textures.json
#   edit the PNGs you want, without changing their size
python tools/packwii.py import pack/env_mainmenu.pack png mimod/project/env_mainmenu.pack
```

- Each PNG is named `<position>_<width>x<height>.png`. `import` only re-encodes the ones that changed. If none changed, the `.pack` comes out identical byte for byte.
- **Formats:** RGBA8, RGB5A3, I4, I8, C4 and C8.
- **Palettes (C4/C8):**
  - If the palette belongs only to that texture, it is recalculated with the new colors.
  - If several textures share it, or the palette was found only by its position in the file, the colors don't change: the game may draw the texture with another palette. Edited pixels take the closest color and the rest keep their exact index.
- **Textures that cannot be imported:**
  - About 400 have their palette in another pack: they come out gray and `import` rejects them.
- **Checked in the game:** a box painted over "Exhibition" in `env_mainmenu.pack` appears in the main menu and the rest of the buttons don't change.

Do not upload or share the exported PNGs or the game's `.pack` files. A mod distributes only the `.pack` files it modified, just like with uniforms.

## New stadiums

A mod can **add** stadiums: they appear as new entries in the Exhibition stadium selection, after the game's own, and don't remove any.

```toml
[[stadium]]
code = "truist"          # its files: project/stadium_truist.pack and project/stadium_truisttbl.pack
name = "Truist Park"
base = "atl"             # stadium whose game data it uses (dimensions, menu, saves)
```

- The easy way to make them is the Blender add-on, "New stadium" («Estadio nuevo») option when exporting (`docs/en/stadiums.md`).
- `project/env_stadium_menu_<code>.pack` is optional: a copy of the base stadium's menu pack (`env_stadium_menuNN.pack`, NN = stadium number) with a different image. Without it, the base stadium's image is shown.
- The game stores the base stadium's number in saves and settings, so disabling the mod never breaks anything: that game goes back to the base stadium.
- For now only in Exhibition.
- The name banner comes from `project/env_stadium_name_<code>.pack` if the mod brings it (format in `docs/en/reverse-engineering.md`). If not, the runtime draws it with the mod's `name` and a system font.
- The new files get free `project.bin` numbers (from 4388), which the game doesn't use.

## Season packs

A mod that brings a roster and some years' schedules can be a **season pack**: with `start_year` in its `mod.toml`, a new season starts in that year.

```toml
name = "Season 2026"
start_year = 2026
dh = "both"      # optional: the DH in both leagues, as since 2022

[divisions]      # optional: only the divisions that change
al_west = ["HOU", "LAA", "OAK", "SEA", "TEX"]
```

```
mods\
  Season2026\
    mod.toml
    roster\      the 2026 roster (a PowerUp+ .dat)
    schedule\    2026.csv, 2027.csv…
```

- When a new season starts with the pack enabled, the first year is 2026: the date, the days of the week, the calendar (Y) and the save list say so, and `schedule/2026.csv` is the one used. The following years are 2027, 2028…
- **Ages:** the roster keeps each player's age for its first season (PowerUp works out the birth year from the age). Starting in 2026, the roster's players are born 18 years later and keep that age; the birth date shown is the one that matches it. Players the game creates later (draft) already use the year shown.
- **DH:** with `dh = "both"` every Season mode game has the designated hitter, National League parks too (it is the launcher's DH IN BOTH LEAGUES option, turned on by the pack).
- **Divisions:** `[divisions]` with `al_east`, `al_central`, `al_west`, `nl_east`, `nl_central` and `nl_west` (lists of team codes). Only the ones that change are needed: a team in a list moves to that division and the others stay where they were. With the example, Houston moves to the AL West and the leagues are 15 and 15, as since 2013. It applies when a new season starts: standings, screens and each team's league (for the DH too) follow the new divisions. Each division takes 1 to 7 teams; if something does not fit (an unknown or repeated code), the game keeps the usual ones and the log says so.
  - With changed divisions the game does not use its official 2008 schedule and generates one. That generator is the game's, made for even leagues (its own Custom League screen does not allow odd ones): with 15 and 15 the pack should bring `schedule/<year>.csv` for each year, and then the generator is not used at all.
- **What does not change:** the game's economy (salaries, luxury tax) follows its seasons 1, 2, 3…, as from 2008. The postseason still has 8 teams.
- The start year is set when the game is created and travels inside the save: a 2026 game stays in 2026 even if the pack is disabled later, and saves without a pack do not change.
- With several packs enabled, the first one by name that has `start_year` wins.
- **MLB Life too:** a new MLB Life career with the pack enabled starts in the previous off-season (November 2025 for `start_year = 2026`), with the roster's ages, the pack's divisions and `schedule/2026.csv` for its first season. The year travels inside its save (`pm3usmy*.dat`) as in Season mode.
- Textures, uniforms and stadiums are better as separate mods: they combine with the pack when enabled together.

## Saves per roster

An MLB Life career, a season or a league started with a modified roster only makes sense with that roster. That's why mods that change the roster have their own saves. Those that bring `roster/` or a `game_data.pack` in `project/` count as such.
- With one or more of them active, the game's save folder (`title/00010000/524c3845/data` in the NAND) is read from and written to `<user data>\ModSaves\<mod+mod...>`. Each combination has its own.
- Without them, the normal saves come back.
- Texture mods, uniform mods (`project/` without `game_data.pack`), `disc/` or Riivolution mods use the normal saves.
- If a Riivolution mod defines its own `<savegame>`, that one takes priority.
- A new combination starts without saves.
- **In the launcher** (MODS), the first row, **SAVES IN USE**, says which saves the game will use ("Normal" or the active roster mods) and opens their folder when pressed. Switching a roster mod on or off warns that this set has its own saves and that the others stay saved. Patch `wiicompiled-launcher-saves`. A player thought the game did not save because their saves "vanished" when switching mods.
- Games played with a roster before this change stayed in the normal saves.
- Patch `wiicompiled-mod-saves`. It uses the same redirector as Riivolution's `<savegame>`, but takes the data folder from the disc title that appears in the path: the game code in low memory doesn't last once the game is running, and `CurrentNandDataDir` goes back to `RMCP`.
- Verified:
  - with Roster2025 active, the game looks for `pm3usma.dat` in `ModSaves\Roster2025\`;
  - with only uniforms and textures, in the normal NAND.
  - Creating a save inside that folder has not been tested yet.

## Rules

A mod must only bring its own files or differences, **never copies of the game** (no unmodified original textures, no `project.bin`, no executable).

## Status

- **Verified** (October 12, 2026):
  - enabling a mod on the MODS screen saves it and asks for a restart;
  - a mod with `textures/` replaces the Wii Remote strap screen (`tex1_512x512_fdbf0c48ec7ed2b1_25db17d8ad4d988a_9`);
  - the log shows `mods: enabled ...` and `Indexed N texture replacements from 2 folder(s)`.
  - a real Dolphin texture pack (2023 logos and uniforms, 206 textures) works unchanged as `textures/`;
  - a mod with only `project/PWM_mdl_uniform_MLB_CHC_00.pack` (the Yankees one, 6.9 KB) makes the Cubs appear with the "NY" cap in team selection. The game generates the copy in 656 ms, reuses it on the next startup and it is identical to the one from `projectbin.py build` (same SHA-256).
  - (October 13, 2026) an `env_mainmenu.pack` edited with `packwii.py` (box over "Exhibition") shows up in the main menu without altering the other buttons.
  - (October 14, 2026) a mod made with Texture Studio (main menu atlas and Coors Field fences and scoreboard, ×4) shows up in the menu and during a game.
- **Not tested yet:** `riivolution/` in MLB. It uses the same Wiicompiled code that already works with Mario Kart Wii.
- **Done:** dumping textures without editing `Config.toml`: Texture Studio's "Capture in game" («Capturar en juego») (launcher → TOOLS (HERRAMIENTAS) → TEXTURE EDITOR (EDITOR DE TEXTURAS)).
- **Pending:**
  - models inside `PACKWII` (textures can already be edited with Texture Studio or `packwii.py`). Stadiums can already be imported and exported with Blender, collision included, and can be made with your own models (`docs/en/stadiums.md`);
  - C++ code mods compiled by the Setup.
