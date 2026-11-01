# Bug fixes

*Español: [../../roadmap/fallos.md](../../roadmap/fallos.md)*

**Status:** ongoing · [Roadmap](../../../ROADMAP.md)

## Why
Every release is tested mostly on one PC. Players find what that PC can't: other GPUs, controllers, Windows versions, long saves. Their reports are the most valuable input the project has.

## How it works
- **Report:** [open an issue](https://github.com/HopeAero/MLBPowerPros2008Recomp/issues/new/choose) with the bug form: version, CPU, GPU, controller, what happened, and `UserData\Logs\<latest>\console.log`. For Season or MLB Life, the save helps a lot.
- **Triage:** each report gets a label (`bug`, `crash`, `controller`, `graphics`, `audio`, `season`, `mlb-life`, `modding`) and either a fix or a question back.
- **Reproduce first:** the fix starts with a short probe from a saved state that shows the bug (tools/probe), and the same probe proves the fix.
- **Ship:** urgent fixes go out as hotfix releases (like v0.1.4.1); the rest ride the next release. Every release is a clean install from the player's disc plus a controller test before it is published.

## Known open items
- Rare hang at startup (close and open again). Unexplained; seen in about 2 of 9 automated boots once, never since a fix in October.
- Password Copy / Paste / Import / Export not yet with the Wii Remote.
- New stadiums appear only in Exhibition.
- Windows Defender quarantined the Setup during an update (one report, v0.1.5). The next release drops the suspended-process start that antivirus rules flag and adds version information; the Setup is still not code-signed.

## Checklist
- [x] Bug report form and labels
- [ ] Reply to every new report within a few days
- [ ] Each fix has a probe that reproduces it
