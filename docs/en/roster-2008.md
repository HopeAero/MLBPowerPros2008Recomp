# MLB Power Pros 2008 rosters and PowerUp / PowerUp+

*Versión en español: [../roster-2008.md](../roster-2008.md)*

## What PowerUp+ is

[PowerUp+](https://github.com/ogura-connection/PowerUpPlus) is a GPLv3-licensed fork of [PowerUp](https://github.com/CSho27/PowerUp), the save editor for **MLB Power Pros 2007** for Wii.
- It reads and writes `pm2maus.dat`, the 2007 save, and generates real rosters from the MLB Stats API.
- It comes with two pre-generated rosters: 2025/2026 and "All-Time Franchise".
- The whole 2007 save format was worked out by Christopher Shorter (PowerUp).

## Where the roster lives in 2008

In 2008 the base roster is **not** in the save, but in `project.bin` → `/game_data.pack` (278,176 bytes).
- That package is `PACKWII` + 0x20 header bytes + the **same roster block** that the 2007 save has at 0x68C74. It is 0x43E80 bytes:
  - 1,500 players of 0xB0 bytes (1,007 used in 2008);
  - lineups, free agents and 32 teams of 0x140 bytes (30 franchises + AL and NL All-Stars).
- Names use the same 16-bit character table as 2007 (PowerUp's `Character_Library.csv`).
- `game_data.pack` is read by every mode at startup. That's why a roster changed there shows up in Exhibition, in the demo and in **MLB Life** (verified: MLB Life → From Beginning → MLB Career Mode → player selection shows the 2025 Red Sox with Crochet, ERA 2.59, 98 mph, left-handed starter).
- What Edit mode saves in the save file doesn't reach MLB Life, because MLB Life starts from `game_data.pack`. Changing the package solves that problem.

## Player record differences 2007 → 2008

They were obtained by comparing the 729 players who are in the base roster of both games (`tools/roster/align_2007_2008.py`): for each PowerUp field, the offset with the most matches is chosen, counting only the players whose value is not the most common one.

| Area (2007 bits) | In 2008 |
|---|---|
| 0x00–0x5B (id, names, number, team, appearance) | same, except the face (see below) |
| batting and pitching form | go from 8 to 9 bits (0x5C.4 and 0x5D.5) |
| primary position and P and C abilities | +2 bits |
| 0x60–0x6E (fielding abilities, hot zones, batting side, ratings) | same |
| second "vs. lefties" (0x6F.5) | no longer exists |
| 0x70.0–0x77.6 (batting and fielding special abilities) | −3 bits |
| 0x77.7–0x81 (pivot man, pull hitter, pitcher block) | −2 bits; 2008 adds a new bit at 0x77.4 |
| date of birth | −12 bits; the year gets +1 because PowerUp calculates from 2007 opening day |
| years in the majors and stats | years at 0x85.2, AVG at 0x85.7, RBI at 0x88.0, HR at 0x89.2; ERA stays at 0x8C.0 |
| 0x8C–0x93 | same; the voice (0x92.2, 14 bits) stays in place but with a different numbering |
| repertoire (0x94–) | same |

Bits that PowerUp doesn't name travel with their area; the converter copies them as one-bit fields. For example, 0x76.0 and 0x76.1, 0x77.7, 0x80.7, 0x8D.6–0x8E.3 and 0x92.0: 2008 keeps them at the expected position, with φ ≈ 1.

**How it was confirmed:** `align_2007_2008.py --bits` calculates, for each 2008 bit, the φ correlation with the 2007 bits shifted by 0 to 4 positions. That clearly reveals the rare flags that the match vote can't tell apart:
- starter, reliever and closer are 3-bit fields (0x62.5, 0x63.0 and 0x63.3) and stay in place (φ 0.82, 0.83 and 0.70);
- the boundary between −3 and −2 is at 0x77.7. With a boundary at 0x78.2, "pull hitter" fell on 0x77.6, where 2008 has only one player flagged; at 0x77.7 there are 42.
- special abilities are checked against the players who have them in 2008:
  - "push hitter" (0x71.0): Ichiro, Joe Mauer, Todd Helton, Michael Young, Mark Loretta;
  - "free swinger" (0x71.7): Alfonso Soriano, José Guillén, Iván Rodríguez, Torii Hunter, Garret Anderson;
  - "pull hitter" (0x77.7): Jason Giambi, Alex Rodríguez, Gary Sheffield, Andruw Jones, Carlos Peña.
- no 2008 player has "table setter" (0x70.0), but the game still uses it: it has its read function, its write function in Edit mode and two gameplay functions that check it together with "rally hitter" (details in `docs/en/reverse-engineering.md`, section 6). The converter writes it in its place and it works if you give it to a player.

**Checked in the game code.** The game reads each record field with a small function, all of them consecutive and in record order. Their positions and widths match this table in every moved field. Some that PowerUp models as a single bit are 2 bits for the game. The other bit travels as an unnamed bit, so the full value is preserved:
- 0x75.4 (2 signed bits, "cannon arm" for PowerUp): +1 in 96 players and −1 in 11. It is not the cannon arm; see the next table.
- 0x78.4 (2 signed bits): a new 2008 special ability that PowerUp doesn't know: night game, good or poor.

**Checked on screen** (Arrange → a team → Fielder Select → C stick, "Player ABL"; screenshots of Cora, Varitek, Casey, J.D. Drew, Eckstein and Rios cross-checked with their bits):

| 2008 bits | What the game shows | In PowerUp |
|---|---|---|
| `0x73.5`/2 | HD 1st Slide (value 2) | WillSlideHeadFirst, correct |
| `0x73.7` | Tough Runner | IsToughRunner, correct |
| `0x74.4` | **Cannon Arm** (in MLB Life messages, "Laser Beam") | calls it CanBarehandCatch (fixed for 2008) |
| `0x75.2` | Gd Block | IsGoodBlocker, correct |
| `0x75.4`/2 signed | **nothing on any tab**: hidden trait. +1 on power pitchers and big-bodied hitters, −1 on small infielders; it could be body type (unconfirmed) | **wrong:** calls it HasCannonArm |
| `0x76.2`/2 | Slugger (+1) / Slap Hitter (−1) | SlugOrSlap, correct |
| `0x76.4`/2 | Aggressive (+1) / Patient Hitter (−1) | correct |
| `0x76.6`/2 | Aggressive (+1) / Cautious Stealer (−1) | correct |
| `0x77.0`/2 | Aggressive Runner (+1) | IsAggressiveBaserunner, correct |
| `0x77.2`/2 | Aggressive Fielder (+1) | IsAggressiveFielder, correct |
| `0x77.4` | **Gd Eye**, new 2008 bit | doesn't exist |
| `0x77.5` | **Barehand Catch** | unnamed (Bit_77_7); in 2008 CanBarehandCatch gets it |
| `0x78.0`/2, `0x78.2`/2 | Gd/Pr Day Game, Gd/Pr Rain | correct |
| `0x78.4`/2 | **Gd (+1) / Pr (−1) Night Game**, new in 2008 | doesn't exist |

- The same players have these bits in 2007, so in 2007 PowerUp also gets the name wrong.
- **For 2008 it is fixed by meaning, and 2007 is left as it is.** The rosters PowerUp generates already use the checkboxes by their name (its 2025 roster gives "Cannon arm" to Tatis, Arenado, Salvador Pérez and Betts). That's why the 2008 table (`MOVED` in `powerup_to_2008.py`) maps "Barehand catch" to `0x77.5` and "Cannon arm" to `0x74.4`. The hidden trait is kept from the 2008 record, or is 0 on a new player, and the leftovers from the 2007 base save are no longer copied.
- Result with the 2025 roster: before, 0 cannon arms, 117 random barehands and 100 garbage hidden traits; now, the 4 real cannon arms and nothing else.
- In PowerUp+, a 2008 roster imported again shows each checkbox with its real special ability (Rios: Cannon Arm; Eckstein: Barehand). The round trip stays identical byte for byte. 2008 rosters imported before this change have to be imported again.
- **Gd Eye and Night Game can be edited in PowerUp+** (General section, marked "(2008)"). A 2008 import reads them from the original record.
  - The 2008 export writes them into two gaps in the 2007 format that the conversion no longer uses (`MOD_2008`: `0x77.7` → `0x77.4` and `0x78.6`/2 → `0x78.4`/2).
  - The game's converter only takes them from a mod with `roster_ids = "2008"`. A generic 2007 `.dat` is converted the same as before.
  - The 2007 export leaves them empty, so a 2007 save is written bit for bit as before.
- Checked: Payton (Pr Night Game) changed in the editor to Gd Night Game and Gd Eye, saved, exported and converted. In the game's roster exactly those bits change; the runtime gives the same result as Python.
- When copying a player (necessary to edit an imported one), PowerUp didn't copy Day Game, Rain, date of birth, age, years in the majors or stats. Without a date, the export failed. Now they are copied.

**Faces:** 2008 inserted new faces, so the 2007 numbers shift in ranges:

| 2007 face | 0–27 | 28–105 | 106–160 | 161–176 | 177 and up (generic faces) |
|---|---|---|---|---|---|
| 2008 face | same | +1 | +2 | +7 | +12 |

With this table 707 of the 729 common players match; without it, only 26. The rest changed faces between games.

**Skin and eyes** (0x55.4, 4 bits): 2007 has 5 skin tones and adds 5 if the eyes are brown. 2008 added a tone at position 2 and adds 6. That's why 0–1 stay the same, 2–6 go up by 1 and 7–9 go up by 2. 695 of 729 match (272 without the table).

**2008's skin tone in PowerUp+** (October 20, 2026, asked for by a player on Reddit): the editor offers it as **2.5 (2008)**, between 2 and 3. Inside PowerUp+ it is kept as 10 (blue eyes) or 11 (brown eyes), values the 2007 layout leaves free; a 2008 export writes 2 and 8. An export for 2007, which has no such tone, writes 2 instead. The 2008 roster has 27 players with it (1 with blue eyes, 26 with brown): they used to come in as tone 2 and now keep theirs. Checked by importing the `project.bin`, exporting the mod (the game's own values) and switching two players to the new tone (they come out as 2 and 8).

**Review of the remaining fields:** all fields of the 729 players, already converted, were compared with the real 2008 roster. Only the face and skin/eyes had changed catalog numbers. Everything else that differs is new-season data:
- ratings, stats, hairstyles;
- 2008 marks most pitchers as "cautious" stealers.

PowerUp's birth year is calculated from the age (Crochet is listed as born in 1981, that is, 26 years old on the 2007 calendar), which is why 1 is added for 2008.

**Voices:** the field is in the same place, but 2008 renumbered some of the voices: Erstad still has 2051 and Accardo went from 2056 to 2057.
- 2008 keeps the 2007 order, but inserts and removes voices.
- `align_2007_2008.py --voices <Voice_Library.csv>` rebuilds the mapping: 810 of PowerUp's 968 voices, in 206 ranges (`VOICE_RUNS`).
- Method: between two players present in both games whose gaps are the same size, voices go one to one, unless a 2008 name contradicts it.
- Holding out 10% of those players, the 739 cases land on their real voice.
- The other 158 voices belong to players that 2008 removed or that can't be placed reliably: those players are left without a voice (0).

## Using it

**As a mod (the usual way):** a `mods\<name>\roster\` folder with the PowerUp / PowerUp+ `.dat` (for example `2025_MLB_Rosters.dat`) and an optional `mod.toml`.
- When you enable it in MODS and press PLAY (JUGAR), the game extracts `game_data.pack` from **your** disc, converts it with the roster from the `.dat` and serves it inside a modified `project.bin`.
- The conversion is saved in `UserData\Cache\mods\roster\<mod>\`.
  - It is redone if the `.dat`, the converter or `roster_ids` changes.
  - Old conversions are deleted.
- The mod never carries game files.
- A player in the `.dat` who is also in the original 2008 roster starts from their 2008 record.
  - They are recognized by first name, last name and birthday. If that repeats, the player number decides: 2008 has two "Chris Young", two "Tony Peña" and two "Great Harris".
  - That way it keeps what only exists in 2008 (their voice, the new bits) and their 2008 values in the bits PowerUp doesn't name. PowerUp can't edit those bits and fills them with whatever that slot had in its base save.
  - The other players start from zeros.
- Slots without a player get the 2008 empty record, which is not all zeros.
- Lineups, free agents and teams come from the `.dat`, with these adjustments:
  - empty entries carry the 2008 empty pattern;
  - entries for the same player keep the bits PowerUp doesn't model.
- A 2007 `.dat` (like `2025_MLB_Rosters.dat`) brings faces and voices with 2007 numbers.
  - Faces and skin/eyes are converted to 2008 with the tables above.
  - 2007 voices are converted to 2008 with `VOICE_RUNS`; players who are also in 2008 keep theirs. With PowerUp's 2025 roster, players without a voice drop from 1,280 of 1,281 to 199.
- If `mod.toml` says `roster_ids = "2008"` (PowerUp+'s export writes it), voice, face and skin/eyes already come in 2008 numbers and are copied as is.
  - Exports made before the PowerUp+ fix (see below) don't bring the bits PowerUp doesn't name. To keep them working, their new players get the value most 2008 players have. Without them (0x4E.1, 0x4E.2, 0x4F.0 and 0x92.0, which every 2008 player has set to 1), the Exhibition lineup screen showed empty bars and everyone as "P". With a new export the result is the same with or without this fill-in.
- **Native or modified players:** launcher → MODS → TOOLS (HERRAMIENTAS) → **ROSTER PLAYERS** (**JUGADORES DEL ROSTER**) (`[mods] native_roster` in `Config.toml`).
  - **Native** (**Nativos**) (default): all of them come in unedited (`Unedited` 0x50.3 at 1, `IsEdited` 0x50.4 at 0), as in the 2008 base roster.
  - **As in the editor** (**Como en el editor**): each player keeps PowerUp+'s *Custom Player* switch.
  - From what has been observed, the game leaves edited players out of MLB Life.
  - `powerup_to_2008.py --editor-flags` is equivalent to "As in the editor".
  - The PowerUp+ patch fixes its import: the condition was inverted and marked every imported player as *Custom Player*. Rosters imported before this fix still have all of them marked; you have to import them again for "As in the editor" to tell them apart.

**With PowerUp+ (editing the 2008 roster):** the `tools/patches/powerupplus-2008.patch` patch adds 2008 support to PowerUp+.
- **Open:** in *Import Roster → Game Save* you can choose the game's `project.bin` (`<installation>\Data\files\project.bin`) or a `game_data.pack`.
  - PowerUp+ converts it to the 2007 format and opens it like any other roster.
  - Face and skin/eyes switch to the 2007 numbering, the one its editor shows.
  - For the long names 2008 added (Saltalamacchia, Chamberlain…), the patch adds their codes to `SpecialSavedName_Library.csv`.
  - The free-agent list has room for 200 players; before, PowerUp read only 15, and 2008 uses 25.
  - The `game_data.pack` is kept in its data folder, next to the base save. It only lives on the player's PC.
- **Export:** *Export Roster → MLB Power Pros 2008 Mod* downloads a zip with `<name>\mod.toml` and `<name>\roster\<name>.dat`. You unzip it into `mods\` and enable it in MODS. Unlike the 2007 export:
  - **One record per person.** The All-Star teams point to the same player, as in the game, instead of creating copies. The copies that come in a roster imported from a 2007 export are merged with the player on their regular team.
  - **Original player numbers.** Players who come from the 2008 roster keep their player number and, with it, their slot and their salary. New ones take the number PowerUp picks by salary, or the first free one.
  - **Empty slots and order.** Leftover slots stay empty, instead of carrying players from the 2007 base save. Teams keep their order, with the major-league ones first; the 2007 export reordered them with a chained `OrderBy`.
  - **Untouched values.** PowerUp's model doesn't keep every exact value: skin and eyes according to the face, the color of hair that doesn't exist, fastball movement, "edited" flags. In each field whose value equals what a round trip without touching the original player would give, the original 2008 value is written.
  - **Unnamed bits.** The export writes the bits PowerUp doesn't model (`PowerPros2008Fields.Unmodeled`, generated with `--emit-cs`): a player who comes from 2008 carries their own, and a new one the value most 2008 players have. That way the `.dat` works on its own, without relying on the game to complete it.
  - **Faces, skin/eyes and voices.** They are written in 2008 numbers (`roster_ids = "2008"`). An untouched face or skin goes back to its exact 2008 value, and one chosen in the editor is converted with the table. The voice chosen in the editor is converted to 2008 with `VOICE_RUNS`; an untouched one goes back to its exact 2008 number, even if it is 0 (no voice). If the chosen voice is from 2007 and 2008 removed it, or if a new player has no voice, it gets the 2008 voice whose name sounds most similar, with the same method PowerUp+ uses for 2007.
  - **Voices in the editor.** On import, the voice switches to the 2007 numbering, the one the editor understands, just like faces. A voice that only exists in 2008 gets the number 10000 + its 2008 number and the name of the 2008 player who uses it. In 2008 mode the list only offers voices that exist in the game: 1,087. The 1,003 2008 players with a voice see the name of their own voice. A player without a voice shows "No voice" ("Sin voz").
  - Rosters imported before this change store the voice with the 2008 number: they export correctly if left untouched, but the editor shows them a different name. You have to import them again.
- **From the game (the usual way):** launcher → MODS → TOOLS tab → **EDIT ROSTER (PowerUp+)** (**EDITAR ROSTER (PowerUp+)**).
  - It opens PowerUp+ in the browser (`http://127.0.0.1:5123`) and closes together with the game.
  - The Setup installs it already compiled in `Game\powerupplus\` (self-contained, no need to install .NET). Its data (database, libraries) stays in `UserData\PowerUpPlus\`.
  - It runs in "2008 mode":
    - the base roster is the one from your `project.bin`;
    - it doesn't use or distribute the 2007 save (`BASE.pm2maus.dat`): it creates an empty container;
    - *Export → MLB Power Pros 2008 Mod* saves the mod directly in the `mods\` folder, ready to enable in MODS.
  - The mod's `.dat` carries only the roster block; the rest of the save is empty, so it doesn't drag in anything from a 2007 save.
  - An imported roster (the base one too) is read-only. To edit it, use *Copy Existing Roster* and work on the copy.
- **For development:**
  - `tools/powerupplus.ps1 -Ui` builds and opens the same interface with everything inside `local\`. It requires the .NET 10 SDK in `local\tools\dotnet10` and Node.
  - `-Publish <folder>` builds the version that goes into the Setup.
- **Command line:** the same script without `-Ui` runs PowerUp+'s commands. Examples:
  - `read-game-save --in-file <project.bin> --name "MLB 2008"`
  - `write-game-save --roster-id N --mlb2008-mod --out-dir <mods folder>`
- **Verified:**
  - **Exact round trip.** Importing the 2008 roster and exporting it unchanged gives, after the game's conversion, a `game_data.pack` **identical byte for byte** to the original. This was checked on the command line, with the interface's API started from a clean clone with the patch (`-Ui`), and in the runtime.
  - **In the game.** MLB Life shows the 2008 Red Sox (Beckett, ERA 3.27, 97 mph).
  - **Edits get through.** A change made in PowerUp+ (Beckett's ERA, from 3.27 to 1.23) reaches the game.
  - **2025 roster.** PowerUp+'s, exported as a 2008 mod, gives the same faces as the `.dat` directly (1,082 of 1,082), with no empty voices and no All-Star copies. It goes from 1,281 to 1,209 records.
    - Skin/eyes match in 792 players.
    - The other 288 have generic "no eye color" faces: PowerUp's model drops that bit because those faces don't show it.
  - **Runtime == Python.** The runtime's conversion is byte-for-byte identical to `powerup_to_2008.py`'s in all three cases: 2008 roster round trip, 2025 exported and 2025 as a direct `.dat`.
- **Limits:**
  - PowerUp+'s 2025 roster puts some traded players on two regular teams (Ramón Laureano on BAL and SD). Those stay as two records, just as in its data.
  - Choosing a voice by name similarity is approximate (Aaron Judge → Derek Jeter's). It is the same criterion PowerUp+ uses in 2007.
  - The PowerUp+ editor shows the 2007 face list. Faces that only exist in 2008 look like the previous 2007 one, but are exported with their exact number if left untouched.

**By hand:** `python tools/roster/powerup_to_2008.py <roster.dat> <original game_data.pack> <mod folder>` generates the mod with `project/game_data.pack`. The result is byte-for-byte identical to the runtime's and reads `roster_ids` from the `mod.toml` next to the `roster` folder. `tools/projectbin.py extract` extracts the original `game_data.pack`.

The runtime's field table (`runtime/src/roster_fields.inc`, patch `wiicompiled-mods`) comes from `powerup_to_2008.py --emit-cpp`, and PowerUp+'s (`PowerPros2008Fields.cs`) from `--emit-cs`. Both are generated from PowerUp's `GSPlayer.cs`.

## Pending

- Find out what the hidden trait `0x75.4` controls.
- A 2008 face list for the PowerUp+ editor.
