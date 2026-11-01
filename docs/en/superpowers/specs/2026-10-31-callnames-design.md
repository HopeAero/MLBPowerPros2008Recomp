# New callnames: design

Date: October 31, 2026. Status: awaiting approval.

## Why

The user wants to add names of current players (Ronald Acuña Jr., Jackson Chourio, Shohei Ohtani, Yoshinobu Yamamoto…) that the 2008 game does not have. They have to meet three requirements:

1. **They are heard in a match:** the main commentator says the surname (in a normal and an excited tone) and the colour commentator says the full name, the same as with the original names.
2. **They appear in the name list when creating or editing a player inside the game.**
3. **They can be assigned from PowerUp+**, in its voice picker.

It has to work on PC and on Switch, and in every mode that uses the commentary, MLB Life included.

## Where the audio comes from

The user chooses it: their own recording, generic TTS, a clip cut from another game, etc. The tool accepts any WAV and does not generate voices.

- The real commentators' voices are not cloned with AI.
- The repository never holds game audio or the user's audio.

## What is already known (reverse engineering)

Everything is in `docs/en/reverse-engineering.md`, section "Voice banks and player names (callnames)". The essentials:

- **Voice banks.** `sound/voiceN.bin` has a header sector 0, phrases (1 header sector + DSP-ADPCM mono 22,050 Hz data) and an index at the end (`u16 sector[]`, `u16 ms[]`).
- **Phrase id.** It is `bank_base + local_index`. `0x8003B398` (bank), `0x8003B530` (local) and `0x8003ACF0` (sector) resolve the id, with hard-coded ranges.
- **Each player's recordings.** Local index = `VoiceId − 0x800`. There are three per player:

| Bank | Base | Who | What it says |
|---|---|---|---|
| 14 | `0x11000` | main | surname, normal |
| 15 | `0x13000` | main | surname, excited |
| 17 | `0x16000` | colour | full name |

- **Editor list.** It is in `env_voice_data.pack` (file 3081 of `project.bin`). It has three lists (A: 1915 generic surnames; B: 970 MLB players grouped by letter; C: 61 legends) keyed by `0x8000 + VoiceId`. The REL functions `0x807EDF4C`-`0x807EE354` read it, with the list sizes hard-coded.
- **Constraint.** Bank 14 holds another 1523 phrases after the last name, so names cannot be added at the end of its id range without overwriting them.
- **Not located.** Where the game turns `VoiceId` into a phrase id is unknown. The design does not need it (see the `VoiceId` range).

## User decisions

- Own audio from any source; three variants per name (normal surname, excited surname, full name).
- Many names, not just a handful.
- They show up in the game's editor list and in PowerUp+.
- PC and Switch.

## Design (Claude's decisions, open to review)

### New `VoiceId` range: 7223-10239

If the game's conversion is linear (`id = base + VoiceId − 0x800`, checked with Jeter in bank 17), a `VoiceId` in this range lands on phrase ids that **no bank uses**:

| Bank | Resulting ids | Gap |
|---|---|---|
| 14 | `0x12437`-`0x12FFF` | between the end of bank 14 and the start of bank 15 |
| 15 | `0x14437`-`0x14FFF` | between the end of bank 15 and the start of bank 16 |
| 17 | `0x17437`-`0x17FFF` | after the end of bank 17 |

- That way the natives recognize a new name from the id alone, with no ambiguity against existing phrases, and the conversion code does not have to be found.
- 3017 new names fit. The field has 14 bits (maximum 16383), so they fit there too.
- Each new name carries a fixed `VoiceId` that the mod declares (see the format). A roster can point at it and keeps working when the mod order changes.
- If two enabled mods declare the same `VoiceId`, the first in load order wins and the log says so.

### Mod format

```
mods/<mod>/callnames/callnames.toml
mods/<mod>/callnames/*.wav
```

```toml
[[callname]]
voice = 7300                 # VoiceId, 7223-10239
list = "ACUNA"               # text in the editor list, at most 15 characters
normal = "acuna.wav"         # bank 14: surname, normal tone (required)
excited = "acuna_hype.wav"   # bank 15: excited surname (optional; normal is used when missing)
full = "acuna_full.wav"      # bank 17: full name (optional; normal is used when missing)
```

- **Accepted WAVs:** 16-bit PCM, mono or stereo (mixed down to mono), at any rate (resampled to 22,050 Hz).
- **Length:** at most 5 s per variant. Anything that does not meet these is rejected with a warning in the log, and the rest of the mod still loads.
- **Text:** the game's character table has no accents and no Ñ. Accented letters become the plain letter ("Acuña" becomes "Acuna") and any other missing character is dropped. The log shows the conversion.

### Runtime (PC and Switch, same code)

1. **At startup**, the `callnames/` folders of the enabled mods are gathered, the WAVs are encoded to DSP-ADPCM (our own deterministic C++ encoder) and the result is cached. The cache is rebuilt only when the WAVs, the toml or the encoder version change, the way `project.bin.sources` works.
2. **Audio:** `voice14/15/17.bin` are served as a **virtual file = original + extension appended at the end**, range-mapped like Switch's virtual `PROJECT.BIN` (phase 102). The 170 MB are not copied. Every new phrase carries the same `0x800` header as the originals: duration at `+0x10` and the high byte of `+0x04` equal to that of its bank's phrases.
   - On PC, the DVD layer today only serves whole files (`disc/`, and the `project.bin` copy). It needs the same range mapping, limited to these files.
3. **Phrase lookup** (variant chosen in the plan, October 31):
   - The virtual bank has an **enlarged header and index**: the phrase count grows up to the local of the highest new `VoiceId`, the locals in between point to the filler phrase and the new ones to their sectors in the extension. The enlarged index goes at the end of the virtual file.
   - That way `0x8003ACF0` and the game's index loading work unchanged.
   - Only the hard-coded ranges need native replacements: `0x8003B398` (bank) and `0x8003B530` (local). They take the real phrase count of banks 14/15/17 and, for any other id, behave **bit for bit like** the original.
   - Sectors have to stay below `0x10000`: names that do not fit are dropped with a warning.
   - If the larger index does not fit in the game's heap, fall back to the other variant: a native for `0x8003ACF0` and the original index.
4. **Editor list:** `env_voice_data.pack` is regenerated with the new names inserted in alphabetical order into the list the editor shows (to be confirmed, see open questions). It is served through the existing `project/` mechanism. The hard-coded sizes in `0x807EDF4C`-`0x807EE354`, and anywhere else that uses them, become natives that read the real size.
5. **Without callname mods**, everything works as it does now: no virtual files and no active natives that change anything.

### PowerUp+

- With `--PowerPros2008ModsDirectory` (the launcher already passes it), PowerUp+ reads the `callnames.toml` of the enabled mods and adds their names to the voice picker with the declared `VoiceId`.
- The list name is written to the `.dat` record with the numbering PowerUp+ already uses for 2008-only voices (`10000 + id`).
- A new `VoiceId` has to reach the in-game player intact from the exported `.dat`. For that, the runtime's roster converter (`Voice2008`/`kVoiceRuns` in `mods.cpp` and `tools/roster/powerup_to_2008.py`) has to pass `VoiceId`s 7223-10239 through untouched. Today it turns voices it does not know into 0.
- Change in `tools/patches/powerupplus-2008.patch`.

### Tools and documentation

- `tools/callnames.py`: decode a game phrase to WAV (for listening locally), and encode and check a WAV with the same algorithm as the runtime, with a round-trip self-test.
- Documentation: `docs/mods.md` and its mirror `docs/en/mods.md` (mod format); new RE in `docs/ingenieria-inversa.md` and its mirror, with the PDF rebuilt; `docs/traspaso.md`.

## Goals, non-goals, anti-goals

- **Goals:** the three requirements above on PC and Switch, in every mode, MLB Life included.
- **Non-goals:**
  - generating or cloning voices;
  - changing the names in other game text (captions, written commentary);
  - a graphical callname editor (it can be added later under the launcher's TOOLS).
- **Anti-goals:**
  - any game file (banks, pack) ending up inside a mod or the repo;
  - any change in behaviour without callname mods;
  - an original name going silent or sounding like another;
  - copying whole banks at startup on Switch.

## Open questions (the builder resolves them)

1. **Which list the editor shows** when creating or editing a player (A, B or both) and how it walks the letters (the `+0xFE80` table). Found with a short probe in Edit mode, without playing matches, and it decides where the new names are inserted.
2. **That the game accepts a `VoiceId` in 7223-10239** in the editor and in a match (without clamping it or checking it against a table). If it clamps it, find the place and add it to the natives.
3. **That the conversion is linear** in banks 14 and 15 as well. One run with `WIICOMPILED_DEBUG_DVD=1` and a player with a new `VoiceId` is enough.

## How it is checked

- **Encoder self-test:** a synthetic test WAV, encode and decode, with bounded error. And the same result byte for byte in Python and C++.
- **Match test** with `WIICOMPILED_MATCH_BENCH=1` and a test mod under `local/` that gives Jeter a new `VoiceId`. The DVD reads in the log have to go to the extension of banks 14/15/17. Without the mod, a match's reads have to be identical to today's.
- **Editor test**, short, with `tools/probe/live.py`: the new name appears under its letter and, when chosen, the player gets the new `VoiceId`.
- **Switch:** the same match test on the NRO.
- **Final listening check by the user:** both commentators say the new name.
