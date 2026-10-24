# Modern postseason

*Español: [../../roadmap/postemporada.md](../../roadmap/postemporada.md)*

**Status:** research · [Roadmap](../../../ROADMAP.md)

## Why
The gameplay updates so far bring Season mode and MLB Life closer to today's baseball: real schedules, the DH in both leagues, 15 and 15 leagues, two-way players. The postseason is the piece still missing. The game plays the 2008 format (4 teams per league); since 2022 MLB has 12 teams.

## Today's format
- 6 teams per league: the 3 division winners and 3 wild cards.
- The two best division winners get a bye.
- Wild Card Series (best of 3, all at the better seed), Division Series (best of 5), League Championship Series and World Series (best of 7).

## Plan
1. **Research:** how the game builds and stores its postseason (bracket, seeding, series length, the schedule it writes, what the save keeps).
2. **Build it as an option** (ENHANCEMENTS, and a season pack key), so the original format stays the default.
3. **MLB Life too:** everything that works in Season mode must work in MLB Life.

## How it is verified
A full season simulated to the postseason with both formats, on test saves; bracket, series lengths and home field checked against the rules above.

## Checklist
- [ ] Postseason structures found and documented
- [ ] 12-team bracket as an option
- [ ] Works in MLB Life
