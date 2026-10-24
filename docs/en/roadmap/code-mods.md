# Code mods

*Español: [../../roadmap/mods-de-codigo.md](../../roadmap/mods-de-codigo.md)*

**Status:** research · [Roadmap](../../../ROADMAP.md)

## Why
Today a mod is data: textures, rosters, schedules, stadiums. Many ideas need logic: a tournament, a new rule, a new screen. Every gameplay feature so far (the Ohtani rule, the manager's role talk, the 15/15 schedule generator) was written as native code inside the runtime. The goal is to let modders do the same without rebuilding the runtime.

## What we already know
- Every function of the game has an address, and the runtime can replace any of them with native code (that is how every feature so far was built).
- MLB Life's conversations are scripts run by an interpreter, and a mod can supply its own script and text.
- The game's lineup, schedule, season and save structures are documented in the reverse engineering notes.

## Plan
1. **Design a mod API:** documented hook points (lineups, schedule, rules, events, dialogue, menus), what a mod can read and change, and how mods load and stay compatible across versions.
2. **Pick the language:** a scripting language (for example Lua) for safety and easy sharing, native plugins for heavy work, or both.
3. **Prove it with a real mod:** for example a tournament mode built entirely as a mod.

## How it is verified
The proof mod works on a clean install with only the mod added, and the API is documented for others.

## Checklist
- [ ] Hook points and API proposal written, open for comments
- [ ] Language chosen
- [ ] A tournament (or similar) built as a mod
