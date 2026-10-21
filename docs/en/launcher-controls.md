# Launcher and controls

*Versión en español: [../launcher-controles.md](../launcher-controles.md)*

The local build keeps the original launcher's background, colors and glowing buttons. Video, Audio, Controls and Interface open through their tabs; Mods and Tools keep their entry from the main menu.

## Adaptive layout

The settings panel uses the frame drawn on the background only when the window has an aspect ratio close to 16:9. With other ratios (narrow, tall or 4:3 windows) the panel fills the window over the blurred background, instead of being boxed in. Long lists scroll and pagination is recalculated when the size changes. Pop-up windows are centered when they open.

## Using a controller

- **Main menu:** D-pad or stick to choose, Cross/A to enter.
- **Settings lists (Video, Audio, Interface, Mods):** up/down choose the row (highlighted in orange); left/right or Cross change its value; L1/R1 switch tabs; L2/R2 change page; Circle/B goes back. In narrow windows, Triangle/Y opens the setting's explanation. The bottom bar shows these keys.
- **Controls:** the D-pad moves the focus around the grid; Cross assigns; L1/R1 and Circle as above.
- Accept and Back don't repeat when held, so entering a screen with Cross doesn't also change the first setting.
- The launcher listens to all connected controllers, not just the first one SDL lists (DS4Windows exposes the physical controller and its virtual Xbox twin).
- With a keyboard: arrows or WASD, Enter to change, Q/E for tabs, Esc to go back.

## Controls (Dolphin style)

`Settings > Controls` (`Ajustes > Controles`) is a single screen with groups, like Dolphin's GameCube controller dialog:

- **Header:** Player (1–4), Device (connected controllers or Keyboard and mouse), Button names (automatic, PlayStation or Xbox) and Default. It is laid out in 4, 2 or 1 columns depending on the width.
- **Groups:** Buttons (A, B, X, Y, Z, START), D-pad, Main stick, C stick, Triggers (L, R, analog L, analog R) and Options. They are laid out in 4, 3, 2 or 1 columns depending on the width; if they don't fit, the area scrolls.
- **Assign:** click (or Cross/A/Enter) on a binding. The button turns orange and waits for the previous input to be released; after that it accepts the new one. Esc, another click on the same button or 8 seconds without input cancel without changing anything. Right-click clears the binding of a button or key.
- Game buttons accept controller buttons, triggers or stick directions. Stick directions and analog triggers accept an axis or a button. With Keyboard and mouse, keys and mouse buttons too. Duplicates between main buttons are swapped.
- **Sticks:** each group shows an octagonal gate with the dead zone and the signal MLB receives, using `PADRead` and the port's same conversion to GameCube ranges (87/74, normalized by MLB between 88/64). Below, dead zone and sensitivity. A sensitivity below 1 softens the response near the center; one above 1 speeds it up. Both keep the full range of travel.
- **Test sticks** (**Probar sticks**) (in Options) stops the sticks from moving the menu while you test them; Esc or OPTIONS ends it.

### Diagonals while fielding

A modern stick is round: pushed fully on a diagonal it gives about `(60, 61)` on the GameCube scale, and with that the fielder walks. The keyboard (W + D, for example) gives `(87, 87)` and runs. While you control a fielder, the runtime reads that controller's main stick as a square: same direction, and the stick's full travel on each axis. A full diagonal then arrives just like the keyboard's, and the fielder runs. Straight directions and partial tilts do not change, so you can still walk.

It only applies while the game uses that controller's fielding control. When batting, pitching and in menus the stick stays the same, and **Test sticks** shows the usual signal. Patch `tools/patches/wiicompiled-mlb-fielding-diagonals.patch`; test `mlb_fielding_stick_tests`. Details in `docs/en/reverse-engineering.md` (section 9, "Controllers").

## Back and closing

Back (Esc, Backspace, K or Circle/B) only counts on the frame it is pressed and never repeats. That way, the press that closes a drop-down or cancels a capture doesn't also close the settings page. Before, holding it for a moment caused those chained closes.

## Language

The launcher is in Spanish or English. By default it follows Windows: Spanish when Windows is in Spanish, English otherwise. To change it: **SETTINGS > INTERFACE > IDIOMA / LANGUAGE** (Automatic, Español or English). The change shows right away and is saved in `Config.toml` (`[ui] language = "auto" | "es" | "en"`); the Setup and Texture Studio use the same setting.

## Enhancements

**SETTINGS > ENHANCEMENTS** holds what the Wii game does not have: changes to how it plays or to what it offers. Graphics (FSR, filtering, MSAA) stay in VIDEO, since they change how the game looks, not the game.

- Every enhancement starts as on the Wii: off, or at its original value, until you change it.
- The ones players are still trying are tagged **EXPERIMENTAL**, and their help asks you to report problems in GitHub Discussions or an issue. Once they have been tried enough, they lose the tag and stay here.

## Seasons (Season mode)

**SETTINGS > ENHANCEMENTS > SEASONS** (experimental) sets how long Season mode lasts: **10 (original)**, which ends after the 2017 season as on the Wii; **20**, which goes on to 2027; or **No limit**, which never ends. It is saved in `Config.toml` (`[season] max_seasons = 10 | 20 | 0`; 0 is no limit).

- With 20 or no limit, a save the original game already finished (after season 10, which replays the ending on load) goes on to 2018.
- A save already past the chosen cap keeps going, so it is not damaged: one past 10 seasons keeps going with 20, and one past 2027 with no limit.
- The game keeps 10 years of finances and team records; years 11 to 20 go in the free part at the end of the save file (`pm3usse<slot>.dat`). With 10, the save stays byte for byte as the original writes it.
- **No limit:** the game's tables hold 20 years. After each season from 2027 on, the oldest year leaves them and goes to an archive of old seasons inside the same save (about 34 fit; after that the oldest is dropped). The game's screens show the latest 20 years, with the real year and that year's days of the week.
- The ending's farewell text says «10 years» even after 20.

Details in `docs/en/reverse-engineering.md` («Season year») and `docs/superpowers/plans/2026-10-19-season-20-build.md`.

## DH in both leagues (Season mode)

**SETTINGS > ENHANCEMENTS > DH IN BOTH LEAGUES** (experimental): the designated hitter in every Season mode game, as in the major leagues since 2022. The original game only uses it in American League parks; in National League parks the pitcher bats. It is saved in `Config.toml` (`[season] universal_dh = true`).

- It only changes Season mode. Exhibition has its own DH setting and keeps it.
- A season pack with `dh = "both"` in its `mod.toml` turns it on even when this says No (`docs/en/mods.md`).
- It applies to whatever game is being played: it is not stored in the save.

Details in `docs/en/reverse-engineering.md` («DH in both leagues»).

## 15 and 15 leagues (Season mode and MLB Life)

**SETTINGS > ENHANCEMENTS > 15 AND 15 LEAGUES** (experimental): Houston moves to the American League West, as since 2013, and both leagues have 15 teams. It is saved in `Config.toml` (`[season] modern_leagues = true`).

- It applies to new Season mode games and new MLB Life careers; games already started stay as they were.
- With 15 and 15 the game's schedule generator leaves uneven schedules (142 to 162 games a team), so the runtime uses its own with the major leagues' format since 2023: 162 games a team, 13 against each division rival (52), 6 or 7 against the rest of the league (64) and 46 interleague (3 against each team of the other league and 4 against the natural rival: Yankees–Mets, Cubs–White Sox, Dodgers–Angels…), in series of 2 to 4 games, about 81 at home, from March 25 to late September. Dates and matchups are not a real year's: for that, a season pack with `schedule/<year>.csv`.
- A season pack with `[divisions]` uses its divisions even when this says No (`docs/en/mods.md`).

## Real schedules (TOOLS)

**TOOLS > SCHEDULE** downloads a major league season's real schedule from MLB's public site (statsapi.mlb.com) and saves it in a mod as `schedule/<year>.csv`:

1. **SCHEDULE: YEAR**: 2002 to 2026. 2020 (60 games a team, from July) and the years before 2002 (their data on MLB's site does not add up to 162 a team) are not offered.
2. **SCHEDULE: SAVE IN**: «New: MLB Schedules» creates the `MLBSchedules` mod with that year as its first season (`start_year`) and switches it on; or one of your mods (your roster, for example), which gets the file. The game uses a year's schedule in the season that carries that year: if the mod had no start year, `start_year = <year>` is added at the end of its `mod.toml` (the rest is left as it is) and a new season with it starts in that year. If it had another, the message says in which season it will be used.
3. **DOWNLOAD SCHEDULE**: the result shows below, e.g. «2022: 2430 games (162 a team)… The game plays it 4 days earlier to end on October 1».

The game plays the regular season from March 19 to October 1 and at most 16 games a day. A season that started earlier or ended later is played the days needed earlier or later (same order, other dates; it happens in 2004, 2005, 2009, 2010, 2012, 2015, 2016, 2021, 2022 and 2025). A game on a day with more than 16 moves to the nearest day off of both teams. No year loses games. For a roster of another year, `roster_year` in `mod.toml` (`docs/en/mods.md`). Internet is only needed to download.

## Saving

The existing mapping files and the `[controller]` section of `Config.toml` are kept. The additional settings are saved under `UserData` in portable installations:

- `stick_response.txt`: sensitivity of both sticks per player; versioned file, with default values of 1 if it is missing or invalid.
- `launcher_prompts.txt`: chosen button-name family.

The binary format of the existing mappings doesn't change. A stick direction assigned to a button uses the `nativeButton` field that already existed in the axis mapping. The reproducible patch is `tools/patches/wiicompiled-launcher-controls.patch`, applied after the existing patches by `tools/wiicompiled-translate.ps1`.

## Verification

`tools/test-launcher.ps1` creates a private portable copy under `local/analysis/launcher-tests/` and runs a short test with an SDL virtual controller. It checks:

- button-name changes and rejection of small stick noise;
- sensitivity applied by `PADRead` with the full range of travel;
- capture on the button itself and swapping of duplicates;
- stick direction assigned to a button;
- cancelling with Esc;
- controller disconnection;
- reading the settings in a new process;
- in a third pass, the settings list with the controller: one row per press, changing with right, held Cross that changes only once, and R1 for the next tab.

It doesn't start a game or modify the settings of the user's installation.

The `mkw_stick_response_tests` target checks center, dead-zone edge, diagonals, monotonic response, sensitivity and extremes. The interface was reviewed at 640×480, 800×1061, 1280×720 and 1920×1061. This does not replace a physical check with the user's controller.
