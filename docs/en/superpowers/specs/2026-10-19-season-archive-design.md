# Season mode with no 10-season cap, with «Archive» — design

*Versión en español: [../../../superpowers/specs/2026-10-19-season-archive-design.md](../../../superpowers/specs/2026-10-19-season-archive-design.md)*

Date: October 19, 2026. Status: approved by the user on October 19, 2026 ("adelante").

## Why

Community players ask for Season mode to last more than 10 seasons. Today it ends with the tenth (2017). The user wants it to go on with no limit, without losing the old years, and wants them viewable inside the game on a screen that looks like Season mode's own.

What we know (`docs/en/reverse-engineering.md`, «Season year»; read in the translated code, not tested in the game):
- the year is 2008 + a signed byte at `[[0x8104B5A8] + 8]`; `0x80B4E564` adds 1 to it on the season change;
- the cap is comparisons of that index with 9 or 10 (`0x80BB4A40`, `0x80BF24B4`, two in `0x80BF4BD4`, `0x80C0CB54`);
- there is at least one per-year table, at `[0x8104B5C4]`, of 428 bytes per year with room for 10. Simply removing the comparisons would make season 11 write past that table and corrupt the save.

## What the user decided

1. **10-year window plus our own archive** (out of three options: window only, enlarge the save, or window with archive). The game keeps working with its 10 years; going from season 10 to 11, the oldest year is saved in an archive file of ours before it is dropped. Nothing is lost.
2. **A screen of our own for the old years**, inside the game, in Season mode, **very close to the game's own**: the same information MLB Data offers for a season and the same actions.
3. **It opens from one more option of the Season mode menu** (Daily Progress, Practice, Management, Commands, MLB Data, System, Exit), with the same look. A button in a corner (no room: the date and owner points are there) and the help bar were turned down.
4. **The option is called «Archive»**, in English. Not «History»: the game already uses it in the bottom bar (R).
5. **It has to work on Switch and other console ports**: everything with the controller, nothing that needs keyboard or mouse.
6. **Careers and honors:** if All-Time/Career and Honors only add up the window's 10 years, showing those 10 is accepted; whole-career totals are seen in Archive. The original screens are not touched.
7. **Finished saves:** a save that already reached the end of season 10 must be able to go on to 2018 and keep adding years, like one that has not got there yet.
8. **Calendar from 2018 on:** the game's game schedule is reused, but with the real year's weekdays.
9. **No MLB or team logos**: they are trademarks even when an image is under a CC licence. Teams are told apart by name, abbreviation and colours. CC0 or CC BY photos can be used (crediting the author when the licence asks).

## Goals

1. **Season mode goes past season 10.** After 2017, 2018 is offered, and so on with no end. The year on screen is the real one (2018, 2019…), on every screen that shows it, with that year's dates and weekdays. Saves that already finished season 10 leave that final state and go on.
2. **Season archive.** Before dropping the oldest year, the runtime saves everything the game kept of that year in an archive file of ours, next to the save (in the active mod's save folder, like `pm3usma.dat`). On Switch and other ports it goes with that platform's saves.
3. **«Archive» option** in the Season mode menu, between MLB Data and System, with a button drawn by us in the style of the game's (without copying its images).
4. **Archive screen:** it mirrors MLB Data:
   - it includes whole-history career totals (archived years plus the window's), since the game's tabs will only cover 10 years;
   - a year selector with L/R, only for the archived years;
   - MLB Data's sections for one year: Player Stats (Season Stats, Player Records), Team Stats (Team Stats, Team Records), Schedule (Standings, Matchup Chart, Postseason) and Roster (Player List, Transactions), as far as the game keeps that data per year;
   - it is driven with the controller: d-pad to move, A to enter, B to go back, L/R for the year.
   - If the archive is empty (no season past the 10th yet), the screen says so.

## Non-goals

- Showing old years on the original MLB Data screens (putting them into the game's slots was discussed: the user found it risky).
- Enlarging the game's save format.
- Changing draft classes, progression, a 15/15 league split, the schedule or the playoffs (other community requests, separate).
- Changing the game's All-Time/Career and Honors tabs (decision 6).

## What would make this a failure even if it works

- A broken save: crossed years (one year's data shown in another), lost stats or a save that does not load.
- A save of 10 seasons or fewer behaving differently from today.
- The Archive screen looking like a foreign add-on.
- Anything that only works with keyboard or mouse.

## Constraints

- No game content in anything we publish: the button, the lettering and the screen's drawings are ours (`tools/check_no_game_data.py`).
- Changes to the game are native function replacements in the runtime (reproducible patches on Wiicompiled), not edits to the translated code.
- The archive has to survive the game being closed at any moment, and a save without its archive (copied from another PC, for example) keeps working.
- Screen texts in English, like the rest of the game.

## First step (before building)

Find out, with a save of several seasons and in the code:
1. **all** the data the game keeps per year, where it is and in what format (without this, the sliding window leaves crossed years);
2. whether All-Time/Career and Honors already add up every year;
3. how the Season mode menu is made (number of options, cursor, button images) and what adding one costs;
4. every place that writes the year on screen.

The findings may change the screen's scope (sections that cannot be filled because the game does not keep that data per year). If so, it goes back to the user.

## Agent's calls (to review)

- **Where the real year lives:** the number of archived years is kept **inside the save** (in free bytes of `pm3usma.dat` the research finds), not only in the archive. A save without its archive (copied from another PC) still shows the right year; only the Archive screen comes up empty. If there are no safe free bytes, it goes back to the user.
- **Archive and save in step:** the archive is written when the game writes the save, not at the season change. Each archived year carries its real year, so archiving a year again does not duplicate it (the last write wins), and if the player goes back to an older save, the archive years that save still has in its window are ignored (the save rules).
- **Switch and other ports:** a design constraint (everything with the controller, save paths through the runtime), not a deliverable of this phase: there is no Switch build to test today.

## Scope change after the research (October 19, 2026)

The research ([findings](2026-10-19-season-archive-findings.md)) showed that almost every per-year table has room for 20 years; only the finances have 10. The user chose two steps:

1. **Phase 2: raise the cap to 20 seasons** (2008–2027). No Archive screen: the game's own screens show the 20 years. The finances of years 11 to 20 go in the area at the end of the save that is always zero, if it is confirmed that nothing uses it.
2. **Later: this design's window with archive and Archive screen**, to go past 20.

The rest of this design (finished saves continue, real-year weekdays, no logos, everything with the controller) still applies to phase 2.

## Left to the builder

- The archive's file format, as long as it is robust and allows adding years.
- How to draw the Archive screen (with the game's own drawing system or a runtime layer), as long as it looks like the game and works on the target platforms.
- How to test going from season 10 to 11 without playing 10 seasons (for example, forcing the year index in a save).
