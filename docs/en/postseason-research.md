# Postseason research

*Español: [../postemporada-investigacion.md](../postemporada-investigacion.md)*

Date: November 4, 2026. Game: Wii USA, RL8E54. Static investigation, later
implemented and verified in Season mode (see the end).

## Target rules

The field has 12 teams, six per league: three division winners and three wild cards.
Division winners occupy seeds 1–3 and wild cards 4–6, ordered by winning percentage
within each group. Seeds 1 and 2 receive byes.

| Round | Matchups per league | Wins required | Home pattern |
| --- | --- | --- | --- |
| Wild Card | 3 vs 6; 4 vs 5 | 2 | Higher seed hosts all |
| Division | 1 vs winner 4/5; 2 vs winner 3/6 | 3 | 2–2–1 |
| Championship | Division Series winners | 4 | 2–3–2 |
| World Series | League champions | 4 | 2–3–2 |

The bracket remains fixed. League rounds favor the higher seed; the World Series
favors regular-season winning percentage. Resolve division ties before wild cards.
Source: rules 34, 35, 38 and 39 of [MLB's rules](https://phuturephillies.com/wp-content/uploads/2025/09/2024-major-league-rules.pdf),
printed pp. 159–167, hosted by a third party.
See also MLB's current [format FAQ](https://www.mlb.com/news/mlb-playoff-format-faq)
and [tiebreaker explanation](https://www.mlb.com/news/mlb-playoff-tiebreaker-rules):
no automatic runner or Game 163. Multi-team ties require their own procedure.

## Original storage and code

Earlier research identified Season mode's tables; the reproducible patch
`tools/patches/wiicompiled-season-cap.patch` preserves them when shifting history.
This investigation additionally located accessors and consumers in translated code.

With `P = [0x8104B5AC]` and internal year index `y` (without display-year offset):

| Data | Address | Original capacity |
| --- | --- | --- |
| Qualifiers | `P + y × 32 + slot × 4` | Eight u32 team IDs per year, 20 years; 44 = empty |
| Series | `P + 648 + y × 160 + series × 20` | Eight records per year, 20 years |

Eight reserved records do not mean eight playoff series. The observed progression
uses 0–3 for Division Series, 4–5 for championships and 6 for the World Series.
Slot 7 receives special handling in `0x80B8F0E8`; its purpose remains unresolved.

| Function / data | Static evidence |
| --- | --- |
| `0x80B377FC`, `0x80B88788` | P getter and branch to getter |
| `0x80B88264(y, series)` | Explicit-year series address |
| `0x80B882B0(series)` | Current-year series address, using `0x80B4A7DC` |
| `0x80B88304`, `0x80B8836C` | Record writer and five-word copy |
| `0x80B888A4(y, slot)`, `0x80B884D4` | Explicit-year qualifier reader and current-year writer |
| `0x80B88458` | Membership search limited to eight qualifiers |
| `0x80B88658` | Eight-slot iteration returning an eight-bit mask |
| `0x80B88928`, `0x80B888EC` | Read / increment side's win byte at record +16 / +17 |
| `0x80B88BA4`, `0x80B88BDC` | Read side's team ID at record +8 / +12 |
| `0x80B8895C`, `0x80B88994` | Packed-state write / read; consumers use 0/1 as winning side, 2 pending |
| `0x80B8ED34` | Series 0/1 or 2/3 feed next round, 4 or 5 |
| `0x80B8EF78` | Series 4 and 5 feed series 6 |
| `0x80B8F0E8`, table `0x80D29E28` | Required wins: 3, 3, 3, 3, 4, 4, 4 for slots 0–6 |

Direct callers to review include `0x807DF90C`, `0x807DF928`, `0x809E782C`,
`0x809EB3D4`, `0x80B496A8`, `0x80B8CDA8`, `0x80B94EA0`, `0x80B94F94`,
and `0x80BA8410`–`0x80BA86C8`. The two `0x809E…` callers, in the previously
studied MLB Life area, explicitly request series 6. This proves an index dependency,
not full MLB Life behavior or coverage of indirect calls.

Private sources inspected without modification or publication:
`local/generated/dolrecomp/rel-linked/runtime_rel_chunks/` (especially chunks
0368, 0388 and 0390), `linked-analysis.dol`, and local runtime `hle/mlb/season_cap.cpp`.
The threshold table was read using the DOL section mapping and agrees with the
routine's accesses. These are analysis findings, not newly matching decompilations.

## Implementation implications

The modern format needs 11 actual series: four Wild Card, four Division, two
Championship and one World Series. Neither those records nor 12 qualifiers fit
the original rows. Enlarging loops alone would overwrite the next year, and the
eight-bit mask would discard additional slots.

The minimum scope includes qualification and tiebreakers, progression, scheduling
and rest days, home field, bracket UI, simulation, persistence, season archives,
and MLB Life consumers. History currently archives 32- and 160-byte rows and
must also preserve modern state.

Prefer retaining original structures and adding versioned modern state through
existing persistence mechanisms. This proposal still needs capacity and save/load
checks in both modes; spare space in a Season file proves nothing about MLB Life.
Bind the format to the save so a global option cannot reinterpret an active bracket.
Keep the eight-team option as required by the roadmap.

## Implementation (patch `wiicompiled-modern-postseason`)

**12-TEAM POSTSEASON** in ENHANCEMENTS (`[season] modern_postseason`), or
`[season] postseason = "modern"` / `"original"` in a pack's `mod.toml`, which overrides the
option. It is fixed when a game starts and travels in the save; original by default.

- **State:** the seven original series keep indices 0–6. The four Wild Card series are
  indices 8–11 at the IOS-reserved top of MEM2 (`0x97FFC000`, 80 bytes a year), which
  `0x80B88264`/`0x80B882B0` resolve there. Each league's top four seeds fill the original
  eight-team qualifier row; all six per league are in the modern state.
- **Qualification:** on day 274/275 (`0x80B4BB28`, `0x80B8E8B4`, `0x80B8F97C`) the regular
  season is read from the schedule, division winners and wild cards are seeded with the
  Rule 34 tiebreakers, and Wild Card games go on days 275–277 (`0x80B4BB28` returns phase 2).
- **Games:** scheduled with the same fields as `0x80B72928`, the original postseason game
  builder (kind 1 in flag bits 17–19). With the regular-season kind the CPU did not simulate
  the day's other matchups.
- **Results and advancement:** `0x80B739AC` redirects those days' results to series 8–11;
  when one ends, the fixed Division Series (1 vs 4/5, 2 vs 3/6) is set up and scheduled.
  Championship Series and World Series use the original indices; the World Series gives
  home field to the better record, with tiebreakers.
- **Bracket screen:** after each game the game animates the day's matchups as series
  "round's first + slot", and the stored round (`[P + 640]`) is 0; on Wild Card days it
  treated those games as Division Series whose teams are not on its eight-team bracket:
  `0x80B898FC` returns −1 and `0x80B89A84` calls through a garbage object (jump to `0x148`).
  `0x80B8AC38` and `0x80B8AD28` are skipped on those days.
- **12-team bracket** (MLB DATA › Postseason and the screen between games, in widescreen): the
  original eight-slot bracket stays and each Wild Card pair is drawn in the side margin next to
  the slot it feeds, with the game's own pictures read from MEM2 at run time (team tile 64×256
  C8, small logos 512×128 C8 in 36×42 cells by team code, digits 512×32 C8 with a blue and a red
  palette back to back, lines 512×256 C8 with the grey the game tints: yellow while the team is
  in, olive once it is out). They are found by the XXH64 of their texels; nothing leaves the
  game. `0x80B899B8` (scores) and `0x80B897F0` (tiles) put side 1 on the left: in the Division
  Series against seed 2 they are swapped so both Wild Card winners take the outer slots, and
  while those series are empty they show seeds 1 and 2 (the byes). MLB DATA's title
  (`0x80B52240`, by the stored round) reads POSTSEASON during the Wild Card round. In 4:3 the
  game's bracket is shrunk to 75 % about its centre (layout nodes: translation at `+64`, scale at
  `+96`; the bracket is `[screen + 0x7B8]` and the eight paths `[screen + 0x38 + 4i]`) and the Wild
  Card pairs go beside it. The earlier ImGui window was removed.
- **Calendar:** days 275-277 (October 2-4) show WILD CARD like the other postseason days.
  `0x80B9C4C0(screen, month)` builds the month and `0x80B9B5F8(cell, label, rival, yellow)` shows
  label `[cell + 0xF4 + 4·label]` (3 = DIVISION); each label node draws the SURF at `[node + 0xDC]`.
  Those cells' DIVISION node gets a copy of DIVISION's SURF whose rectangle points at a free part
  of the same atlas (256×256 C8, x 123-255, y 164-193), where "WILD CARD" is composed from the
  atlas's own letters (W, R, L from WORLD; I, D and an O opened into a C from DIVISION; A from
  ALL-STAR) in DIVISION's colours. The original picture is put back before each month.
- **Standings:** `0x80B7B794` fills each row; DIVISION WINNER when `0x80B8D69C(league, division,
  team)` finds it in its slot of the qualifier row, WILD CARD WINNER when `0x80B8D6F4(league, team)`
  finds it in the single wild-card slot (2008 rules). With the modern format they answer by seed:
  1-3 division winners by their division, 4-6 wild cards. No wild-card badge before the regular
  season ends (the original logic would apply 2008 rules). The qualifier row is written in the
  game's order: East, Central and West winners, then the first wild card.
- **Saves:** Season uses the `PPSEASON` tail version 4 with an `MLBPOSTS` v1 block (6,144
  bytes, FNV-1a checksum, validated on read) at its end; MLB Life, the same block at file
  offset `0xF6000`. Saves without the block load in the original format. The No Limit history
  shift also archives the modern state.

## Verified

- **Synthetic tests** (18 ctest): seeding, multi-team ties, fixed bracket, 2–2–1 / 2–3–2
  home field, series completion, the save block (corrupt, truncated, versions) and the v4
  tail. On their own they do not show the game completing a postseason.
- **In game, Season mode** (November 4, 2026, isolated NAND copy under `local/`, regular
  season completed in RAM with synthetic results, test mod `postseason = "modern"`):
  `[postseason] year 0: 12 qualifiers`; Yankees as sixth seed at Oakland. Both Wild Card
  games played by Fast Game and the other three matchups simulated by the CPU: all four
  series ended 2–0 on day 276. Division Series complete (three 3–2, one 3–0), Championship
  Series 4–3 and 4–3 and World Series 4–3, finished on 10/31. Awards and off-season carried
  on. Saved on 12/25, game closed and loaded: the four Wild Card series are restored
  identically; the step to 1/1/2009 leaves the new row empty. Replay: all 83 patches apply
  on the pinned tree and the code matches what was tested.

- **In game, MLB Life** (November 5, 2026, S. Ohtani's Dodgers career on the isolated NAND copy;
  results in RAM through 9/24 and the game simulating 9/25-9/30 through the calendar's skip):
  qualification on the step to day 275, all four Wild Card series simulated (two ended 2-1 in game
  3), Division Series, Championship Series and World Series through 10/31; the calendar with WILD
  CARD, standings with 3 DIVISION WINNER and 3 WILD CARD WINNER per league and the 12-team bracket.
  Saved to slot 2 (`MLBPOSTS` block at `0xF6000`), game closed and loaded: the four series are
  restored identically.
- **Two fixes found with Life** (they apply to Season mode too):
  - **Postponed games, as MLB does:** the postseason never moves (in MLB the season ends on a
    Sunday, Monday is kept free and the Wild Card round starts on Tuesday). Once no game is left
    scheduled, each unplayed game is made up on a free day before day 275 (in 2008: 9/29, 9/30 and
    10/1) when its result can change the qualifiers or their seeds (seeds decide byes and home
    field); otherwise it is cancelled and those teams finish with fewer than 162 games, as the
    Nationals did in 2008. With no free day it is cancelled too and qualification counts only the
    games played. Verified in Season mode: 16 games postponed from 9/24 (two doubleheaders), 3 made
    up (two Orioles-Yankees and one Astros-Athletics) and played on 9/30 and 10/1, 13 cancelled; the
    10/2 qualification includes them.
  - **Free days by schedule** (November 5, 2026): 2008's official one ends 9/27 (9/28-10/1 free);
    the 2025 and 2026 mod schedules end 9/28 and 9/27 (3-4 free days). The game's generated ones
    fill almost all of September: 2009 ends 9/30 (only 10/1 left) and 2010 on 10/1 (none). In those
    years a decisive postponed game is cancelled for lack of a day.
  - Emptied schedule slots use the same fields as the game (`0x80BA0DAC`).
- **Testing note:** day 277 is event 55 and `0x80B8CC80` puts every roster back as it was on 8/31
  (the September mark, bit `0x02` of `+0x16`, set by `0x80B8CBD8` on the night of 9/1). A test game
  that jumps over that night has no marks and every team is left without MLB players, so the third
  Wild Card game fails. This comes from the test, not the patch: a played season goes through 9/1.
  `local/analysis/postseason/mark_sept.py` sets the marks for tests.

## Remaining

- **MLB Life:** the step to 2009 inside Life is untested (it is tested in Season mode).
- **Simulation view:** the animation between games still has no Wild Card round.
- **Standings during the season:** only qualifiers are marked once it ends; there is no
  wild-card race view while it is played.
- **Real season:** the probe started from results injected in RAM, not a season simulated
  by the game. The roadmap's full integration check is still due.
- **Ties of five or more clubs:** MLB Rule 34(c) only spells out ties of two, three and four
  clubs and leaves the rest to the Commissioner. The model applies the four-club steps to any
  number of clubs (user decision, 2026-11-06). When every league game has been compared and
  clubs are still tied, it draws one, seeded by the season's results, so a reloaded save draws
  the same club.
