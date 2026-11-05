# Modern postseason implementation plan

> Execute in this session using subagent-driven-development for independent work;
> keep runtime mutations sequential and review the complete patch before delivery.

**Goal:** Optional, persistent 12-team postseason in Season and MLB Life.

**Architecture:** Keep the seven existing playoff series indices for consumers.
Add native qualification and Wild Card state using existing runtime integration
and save hooks; retain legacy behavior for legacy saves.

**Tech Stack:** C++ runtime, existing native overrides, Python checks, pinned LLVM.

- [ ] Trace qualification, scheduling, results, UI and both modes' save lifecycle.
- [ ] Add `modern_postseason` configuration and `postseason` pack selection, with
  launcher wording explaining that selection applies to new games.
- [ ] Write and run failing synthetic tests for seeding, fixed bracket, home field,
  series completion and versioned state validation; implement the native model.
- [ ] Integrate creation/load/write lifecycle and bounded persistence, preserving
  old saves and shifted season archives.
- [ ] Integrate native game schedule/result consumers and display the additional round.
- [ ] Build targeted tests and runtime, replay the patch against the pinned tree,
  and probe copied saves in both modes. Record which integration checks passed.
- [ ] Update Spanish/English docs and roadmap with observed behavior and limitations.

Runtime implementation: `local/wiicompiled-mlb/src/runtime/include/modern_postseason.h`
and `src/hle/mlb/modern_postseason.cpp`, with tests under runtime `tests/`.
Deliver as `tools/patches/wiicompiled-modern-postseason.patch`, registered in
`tools/wiicompiled-translate.ps1`. Keep private test artifacts in `local/analysis/postseason`.
