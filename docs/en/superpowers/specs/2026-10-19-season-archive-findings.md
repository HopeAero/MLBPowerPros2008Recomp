# Season mode without a cap: research findings (phase 1)

*Versión en español: [../../../superpowers/specs/2026-10-19-season-archive-findings.md](../../../superpowers/specs/2026-10-19-season-archive-findings.md)*

Date: October 19, 2026. Design: [2026-10-19-season-archive-design.md](2026-10-19-season-archive-design.md). Plan: [../../../superpowers/plans/2026-10-19-season-archive-research.md](../../../superpowers/plans/2026-10-19-season-archive-research.md). Technical detail: `docs/en/reverse-engineering.md`, «Season year».

## How it was researched

- Runtime probe (`wiicompiled-season-probe`): logs the year index, can force it, and lets a driver run the game live (buttons, memory writes, memory dumps).
- `tools/season_save.py` to compare saves and dumps.
- Test saves in a separate mod (`SeasonProbe`); the user's saves were not touched.
- No matches played: the date is moved in memory and saved.

## What is known

1. **The year rollover can be triggered without playing.** From the off-season menu, with the day at 12/25, «Daily Progress» goes to 1/1 and adds a year. Seen from 2008 to 2009 and from 2009 to 2010.
2. **The 10 cap is a variable** (`[0x8104BA2C]` = 10) read by five functions, plus about seven fixed compares with 9 or 10.
3. **End of season 10.** With the index at 9, moving into January takes it to 10 and the mode's ending plays (farewell, summary, credits, «Save as Arrange Team»). A finished save replays the ending when loaded. Index 10 stays in the save (file offset 0x47E8C), so a finished save can be recognised by it.
4. **Per-year data:** almost every Season mode table has room for **20 years** (postseason qualifiers and series, league standings, team per year). Only the **finances** have room for 10, and with the index at 10 the game already writes where it should not (into the first month's balance).
5. **Day of the week:** computed by formula from the year; years after 2017 come out right with no work.
6. **Not found:** a per-year history of awards or player stats. The career tabs seem to accumulate in the players themselves, not per year (unconfirmed).

## What this changes in the design

The approved design assumes the game only keeps 10 years and proposes a 10-year window with an archive. The data says otherwise: the game already has room for 20 years except for the finances. That opens a simpler option the design did not consider:

- **A. Raise the cap to 20 seasons** (2008–2027). Change the variable and the fixed compares, and give the finances room for 10 more years (a file of our own or a free area of the save). Nothing else in the save or the game changes format; no Archive screen is needed since the game's own screens would show the 20 years.
- **B. What was approved: window with archive and Archive screen**, with no end. Move the six tables down one slot each year. With 20 slots, the window could be 20 years instead of 10.
- **C. A first, B later:** 20 seasons now, and later the window with archive to go past 20.

Under the design's rule («if the scope changes, talk to the user again»), the choice is the user's. Phase 2 is planned once it is made.

## Risks and gaps

- The tested saves have no played matches: each table's room is confirmed, the exact meaning of some fields is not.
- The normal end of season 10 probably comes on 11/01 (per the code), not in January; not seen in the game.
- Jumping from 3/25 to 9/29 without playing, the «Improvement Notifications» screen crashed on A (call through a null object). Most likely caused by the date jump; still to check in a normal game.
- A whole season 11 has not been tested: beyond the tables, something may depend on the year without following the searched pattern.
