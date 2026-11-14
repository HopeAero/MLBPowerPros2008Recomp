# Working agreement

- Keep this recomp project on drive D in its dedicated directory. Preserve the original disc image and read-only synced references.
- User decision, 2026-10-08: prioritize recomp code, native replacements and measured performance work. Do not spend development turns manually playing each inning or repeatedly pitching merely to advance the score.
- User decision, 2026-10-09: use the reviewed recomp/decomp developer guidance to pursue larger measured improvements. Identify the major cost, compare preserved behavior and benchmark from the same saved state; do not equate small diagnostics changes with gameplay or completion progress.
- Use short, targeted integration probes from saved states. Automate control sequences when needed. A full match is a later integration check, not a prerequisite for each code improvement.
- Report linked/translated code separately from validated behavior. Match progress does not increase recomp coverage, and 100% experimental linking does not mean a complete native port.
- Preserve generated game code. Runtime fixes must remain reproducible patches against pinned dependencies; private binaries, assets, saves and traces stay under local/.
- Never commit or publish game content: no disc image, executable, assets (textures, models, audio, video, fonts), generated code, saves or byte dumps. User-facing features such as the launcher must not use game assets either. `tools/check_no_game_data.py` runs as the pre-commit hook; install it with `--install-hook` in a fresh clone.
- Handoff for the next session: `docs/traspaso.md`; environment pitfalls: `docs/trampas.md` (EN `docs/en/pitfalls.md`).

## What to run for a change

Python is 3.12, run isolated (`python -I`); runtime test targets build in `local\wiicompiled-mlb\build` and run from there.

| Change | Check |
|---|---|
| Anything to commit or publish | `python -I tools/check_no_game_data.py --all` |
| `tools/update_manifest.py`, the piece list, `Make-Payload.ps1` / `Make-Release.ps1` | `python -I tools/test_update_manifest.py` |
| `tools/setup/Update.ps1`, `LocalBuild.ps1`, `ProgramItems.ps1` | `python -I tools/test_update_ps1.py` |
| `tools/linux/update.sh`, `build.sh` (`--stage`, `--from-kit`), `make-kits.sh` | `bash tools/linux/test_update.sh` (Linux or WSL) |
| `runtime/src/launcher_update.cpp`, `runtime/include/launcher_update.h` | build and run `mkw_launcher_update_tests`, then build `WiiCompiled` |
| `runtime/src/launcher.cpp` (any launcher change) | `tools/test-launcher.ps1`, plus PrintWindow screenshots of what changed (`tools/probe/winshot.py`) |
| The update flow as a whole, before a release that changes it | the fake-release end-to-end run (`docs/traspaso.md`, «Actualizaciones desde el launcher»): Setup install, `update_manifest.py release` into a folder, launcher with `MLBPP_UPDATE_URL=file:///<folder>/` |
| A runtime patch | `git -C local\wiicompiled-mlb\src apply -R --check tools\patches\<patch>.patch` |
