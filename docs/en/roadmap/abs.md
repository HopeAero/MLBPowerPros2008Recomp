# ABS: ball-strike challenges

*Español: [../../roadmap/abs.md](../../roadmap/abs.md)*

**Status:** planned, not started (phase 1 research done on 31 October 2026) · [Roadmap](../../../ROADMAP.md)

Requested by the project owner (2026-10-31). MLB's challenge system (Automated Ball-Strike, 2026 rules) as an **ENHANCEMENTS** option, off by default and tagged EXPERIMENTAL, in Exhibition, Season and MLB Life.

## Why
It's the new major-league rule: the human umpire makes the call, and the batter, pitcher or catcher can ask for a review right after the pitch. The automated system confirms or overturns it. Each team has 2 challenges and keeps the ones it wins. It adds a decision to every at-bat and brings the game closer to today's MLB, like the Ohtani rule or the DH in both leagues.

## What we know (reverse engineering, phase 1)
Full detail with addresses: [reverse engineering, section 21](../reverse-engineering.md#21-ball-or-strike-count-and-the-window-after-the-call-abs-phase-1-2026-10-31).

- **The game's umpire never errs at random.** `0x80CA9A14` compares where the ball crosses the zone with a fixed rectangle: inside is a strike; outside and not swung at, a ball. No randomness, difficulty or settings.
- **But its edges are uneven:** the outside edge has a ~1.03 margin (probably the ball radius); inside, top and bottom have none. The game also flags close calls (`0x37714`), and the commentary uses a symmetric zone without the margin. That's where the feeling that "the umpire misses the lines" comes from.
- **There is a natural challenge window:** after the call, at-bat state 0x11 (`0x80CAFAA4`) waits ~1.07 s (frame 0x40) before applying a strikeout or a walk. Before that only the count bytes have changed.
- **Simulated at-bats have no location** (`0x80C95980` decides at random): ABS doesn't apply there.
- With the option off, nothing touches the game's global random generator (`0x8031A078`).

## Design
- **Options (ENHANCEMENTS):** ABS on/off; 2 challenges per team, kept when successful; ABS zone "rulebook" (the ball touches the zone on any side, values the game already has in `0x80D32F9C`) or "game"; umpire error off / low / realistic.
- **Without umpire error**, winning challenges are almost always the pitcher's (balls just inside, high or low that the rulebook would call strikes). **With error**, the umpire occasionally misses on the edges and the batter can win too. The error uses its own random generator, never the game's.
- **Challenging:** in the window after the call, a dedicated button (read in the runtime's controller layer). The CPU decides from the distance to the ABS edge, the count, outs, inning and challenges left, plus some noise.
- **Reveal:** the replay uses what the game already draws: the ball marker (`0x8060AADC`) and the zone, with text in repainted game atlases (never ImGui look-alikes).
- **Overturn:** before frame 0x41, swap strike and ball, fix the pitcher's ball counter, the history, the pitch log and the location class, and replay the umpire's gesture and voice. The game itself then applies the strikeout or walk.

## Phases
- **ABS-a · Probe (0.5 days):** from a saved state, log the 2D point, the 3D ball at the plate, the batter's side and the call, to confirm the edge rule and that the marker matches.
- **ABS-b · Hooks (2–3 days):** call, window and overturn with a correct count, strikeout and walk. Automated tests from saved states.
- **ABS-c · Gestures and voices (1 day):** replay the right gesture and voice after an overturn.
- **ABS-d · Challenge UI (2–4 days):** prompt and challenge counter with the game's graphics.
- **ABS-e · Replay (2–3 days):** the zone and the ball point.
- **ABS-f · CPU (1 day):** when it challenges.
- **ABS-g · Umpire error (1 day):** optional, with its own random generator.
- **ABS-h · Launcher and modes (1.5–2.5 days):** ES/EN options in ENHANCEMENTS; Exhibition, Season and MLB Life.

Estimated total: **10–15 development days**.

## Risks
- Wild pitches, passed balls, uncaught third strikes and hit-by-pitch skip the window: they can't be challenged.
- Some effects already happened at the call and must be undone or replayed: ball-four / strike-three voices, counters, reactions, the bases-loaded check and the history.
- The count display (`0x8063ABCC`) doesn't refresh when a ball and a strike are swapped (the sum is the same).
- Challenges left in a mid-game save (Season, MLB Life) haven't been studied yet.

## Pending decisions (owner)
- **Umpire error** so batters can win challenges too, or the rulebook zone only?
- Which button challenges on controller, Wii Remote and keyboard?

## Checklist
- [x] Phase 1: where the call happens, the count, the window and reusable graphics (2026-10-31)
- [ ] ABS-a probe
- [ ] ABS-b hooks with tests
- [ ] ABS-c gestures and voices
- [ ] ABS-d UI
- [ ] ABS-e replay
- [ ] ABS-f CPU
- [ ] ABS-g umpire error
- [ ] ABS-h launcher and modes
