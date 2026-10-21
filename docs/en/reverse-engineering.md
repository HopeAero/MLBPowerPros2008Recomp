# MLB Power Pros 2008 (Wii) — Reverse-engineering reference
*Versión en español: [../ingenieria-inversa.md](../ingenieria-inversa.md)*

What is known about the game's internals, ordered by system: addresses, structures, file formats, threads and the scene flow. It comes from the work of the native recompilation project and is meant for the modding community and for future developers.

- **Version:** `RL8E54` (USA, NTSC-U). All addresses are for this version.
  - `main.dol` SHA-256 `789c208f1611c4b150d272eb32cbd461d24aebbc5d7c11a420102d868ff1c5f7`
  - `ovl_main.rel` SHA-256 `dcc37a1a4824702feff5bcaa6aae7cefdedfc3b5d832fbf3de92b982a59b4edc`
- **What it is not:** it is not a decompilation. The whole game is recompiled (translated from PowerPC to C++), but almost all of its code is still unnamed (`func_8003A248`…). What is here was found while fixing specific bugs or building tools (mods, roster, benchmark).
- **Reliability:** each fact says where it comes from. "Disassembly" and "runtime" are checked in the binary or in the running game. "Observed" is behavior seen without reading the code that produces it.
- **No game content:** this document does not include game bytes, resources or code, only descriptions.
- **How to build the PDF:** `python tools/build_re_pdf.py` (it writes it to `local/docs/`).

## Quick access

What you are looking for and where it is:

| Looking for… | Go to |
|---|---|
| Season mode: date, year, seasons, per-year tables | [Season mode](#9-season-mode) |
| Game schedule (table, team numbers, generator) | [Game schedule](#game-schedule) |
| Season save (`.dat`) | [Season save file](#season-save-file) |
| Players and roster (`game_data.pack`) | [Roster: `game_data.pack` and player format](#7-roster-game_datapack-and-player-format) |
| Stadiums (models, collision, adding stadiums) | [Stadiums: `PJ7M` models and `tbl` table](#6-stadiums-pj7m-models-and-tbl-table) |
| Textures and packs (`PACKWII`) | [`PACKWII` format](#packwii-format) |
| Disc files (`project.bin`) | [Disc and `project.bin`](#5-disc-and-projectbin) |
| Menus and scenes | [Scenes, menus and setting up a match](#8-scenes-menus-and-setting-up-a-match) |
| Commentator and sound | [Sound engine and commentator](#4-sound-engine-and-commentator) |
| Match camera | [Match camera](#match-camera) |
| Controllers | [Controllers](#11-controllers) |
| Save data, NAND | [Save data, NAND, SYSCONF and ES](#12-save-data-nand-sysconf-and-es) |
| Porting the game: what must be respected | [Game quirks that matter when porting](#14-game-quirks-that-matter-when-porting) |
| Tools and how to keep investigating | [How to keep investigating](#15-how-to-keep-investigating) |

## Contents

1. [Executable and memory map](#1-executable-and-memory-map)
2. [Boot and operating system](#2-boot-and-operating-system)
3. [Threads and timing](#3-threads-and-timing)
4. [Sound engine and commentator](#4-sound-engine-and-commentator)
    [Threads](#threads) · [AX channels and voices](#ax-channels-and-voices) · [Commentator streams](#commentator-streams)
5. [Disc and `project.bin`](#5-disc-and-projectbin)
    [`PACKWII` format](#packwii-format)
6. [Stadiums: `PJ7M` models and `tbl` table](#6-stadiums-pj7m-models-and-tbl-table)
    [`PJ7M` model](#pj7m-model) · [`tbl` table](#tbl-table) · [Trees A and B](#trees-a-and-b) · [Writing the collision](#writing-the-collision) · [Writing stadiums](#writing-stadiums) · [Bringing in an external model](#bringing-in-an-external-model) · [Field inventory for writing packs from scratch](#field-inventory-for-writing-packs-from-scratch) · [Writing packs from scratch](#writing-packs-from-scratch) · [What the fields do, tested in the viewer](#what-the-fields-do-tested-in-the-viewer) · [Stadium from scratch in the game](#stadium-from-scratch-in-the-game) · [Game stadium list](#game-stadium-list) · [Pending](#pending) · [Tools](#tools)
7. [Roster: `game_data.pack` and player format](#7-roster-game_datapack-and-player-format)
8. [Scenes, menus and setting up a match](#8-scenes-menus-and-setting-up-a-match)
9. [Season mode](#9-season-mode)
    [Screens and menus](#screens-and-menus) · [State, date and year](#state-date-and-year) · [Game schedule](#game-schedule) · [Year rollover and number of seasons](#year-rollover-and-number-of-seasons) · [Per-year data](#per-year-data) · [Season save file](#season-save-file)
10. [Graphics (GX and VI)](#10-graphics-gx-and-vi)
    [Match camera](#match-camera)
11. [Controllers](#11-controllers)
12. [Save data, NAND, SYSCONF and ES](#12-save-data-nand-sysconf-and-es)
13. [Identified libraries and functions](#13-identified-libraries-and-functions)
14. [Game quirks that matter when porting](#14-game-quirks-that-matter-when-porting)
15. [How to keep investigating](#15-how-to-keep-investigating)

---

## 1. Executable and memory map

The game is a small DOL and a large REL module with almost the whole game.

| | File | Text | Data | BSS |
|---|---:|---:|---:|---:|
| `sys/main.dol` | 2,299,840 | 1,200,448 | 1,099,136 | 954,200 |
| `files/ovl_main.rel` | 11,229,928 | 7,820,936 | 1,806,784 | 154,196 |

- **Entry point:** `__start` at `0x8000403C`; `main` at `0x8005E4C4`.
- **Functions:** the REL has 87% of the code. There are 48,177 functions in the map: 5,065 in the DOL and 43,112 in the REL. Only 1,794 have a name (almost all from the SDK, in the DOL).
- **Small data areas:** `r13` (`_SDA_BASE_`) = `0x80322060` and `r2` (`_SDA2_BASE_`) = `0x80323940`. They are set by `__init_registers` (`0x800041A8`).
- **REL `ovl_main.rel`:**
  - Module 1, REL format version 3.
  - Loaded at `0x80575580`, with its BSS at `0x8102B0C0`.
  - `_prolog` `0x80AA0718`, `_epilog` `0x80AA0794`, `_unresolved` `0x80AA07DC`.
- **REL BSS details** (runtime):
  - The REL takes a whole free gap of `0xAB5B20` bytes in the main heap and ends at `0x8102B0A0`.
  - The BSS comes right after, behind a `0x20`-byte block header: at `0x8102B0C0`.
  - If the BSS ends up `0x20` lower, its last variables overwrite the header of the next block and break the heap's free list.
- **Arena top:** `0x817FFD40`. Dolphin places the FST (`0x2C0` bytes) at the very top of MEM1.
  - The game sizes its main heap with the whole arena and fills it almost to the byte.
  - With a lower arena, a 278 KB load returns NULL. The game then copies a `PACKWII` package to `0x80000000` and destroys the system's global variables.
- **Main heap** (disassembly of `0x80051474`):
  - Object at `0x802B57A8`; first-fit search through the free list (`+0x10`).
  - `0x20`-byte block header before the data:

| Offset | Field |
|---|---|
| `+0x00` | flags (`0x80000000` free, `0x40000000` used) |
| `+0x08` | data size |
| `+0x0C` / `+0x10` | free-list links |
| `+0x14` / `+0x18` | previous / next block in memory |

- **Game heaps** (created by `0x8005AF78` at boot; allocate with `0x8005A804` from the head, `0x8005A9DC` from the tail and free with `0x8005ABAC`, handle in `r3`):
  - `0x802B57A8`, MEM1: the whole MEM1 arena (from `0x8032EAE0`, which the game forces) up to `0x81800000`.
  - `0x802B57C8`, MEM2: a **fixed** `0x033E0000 - 0x800` bytes from the MEM2 arena start, i.e. `0x90000800`–`0x933E0000`, the whole retail arena. It ignores the arena top.
  - The MEM2 tail holds, among others, 1 and 2 MB buffers (`0x800329A4`) and the stadium graphics pack (`0x800434B8`). When an allocation fails, `0x800434B8` retries it forever: that is the hang on "Preparando partido" with a 32 MB pack (traced on 18 October 2026).
  - The recomp replaces `0x8005AF78` with a native version (`hle/mlb/game_heaps.cpp`, patch `wiicompiled-mlb-mem2-heap`) that gives the MEM2 heap the whole arena: 125.7 MB with the runtime's 128 MB of MEM2.
- **Game singleton objects**, pointers inside the REL BSS:
  - `0x81033268`: scene manager ([section 8](#8-scenes-menus-and-setting-up-a-match))
  - `0x8104E1D8`: match configuration
  - `0x8103D118`: common match data

Sources: `docs/en/phase0-report.md`, `config/wiicompiled/rl8e54.yml`, `tools/patches/wiicompiled-mlb-overlay-loader.patch`, `wiicompiled-rel-bss-address.patch`, `wiicompiled-match-benchmark.patch`.

## 2. Boot and operating system

**Sequence:**
1. `__start` calls `__init_registers` and `__init_cpp` (`0x800A8730`), and then `main`.
2. `main` loads `ovl_main.rel` from the DVD, links it (`OSLink`) and calls its `_prolog`.
3. Screens: Wii Remote strap warning (asks for A), "NOW LOADING" (while it loads the REL and reads more of `project.bin`), memory check ("The Wii system memory is being checked."), 2K Sports logo, intro video, main menu.

**REL loader, `0x80051474`** (rebuilt in `hle/mlb/rel_loader.cpp`):
- Takes the module index in `r4` and returns 1 if it loads it successfully.
- Module table at `0x8020B920`, `0x28`-byte entries:

| Offset | Field |
|---|---|
| `+0x0C` | pointer to the file name |
| `+0x10` | use counter |
| `+0x14` | module address; if not 0, it is already loaded and only one use is added |
| `+0x18` | size |
| `+0x1C` | BSS address |
| `+0x20` | BSS size |

- Load steps:
  1. `DVDOpen` `0x800A9764`
  2. allocation with `HeapAlloc` `0x8005A804` (aligned to `0x20`)
  3. `memset` + `DCFlushRange` `0x8009C488`
  4. `DVDReadPrio` `0x800A9CDC` with priority 2
  5. `DVDClose` `0x800A9A10`
  6. allocates the BSS
  7. `OSLink` `0x800A1678`
  8. calls `_prolog` (module header `+0x34`)
- If anything fails, it frees with `HeapFree` `0x8005A76C`.

**Low memory:** the disc header at `0x80000000` holds `RL8E` + `54`. The game overwrites it once running, so the game code cannot be read from there after boot.

**SYSCONF:**
- `OSInit` (`0x8009AA90`) waits while `SCCheckStatus` (`0x800D1AB0`) returns "busy"; the read is started by `SCInit` (`0x800D1A10`).
- Job state at `0x8031B788` (1 busy, 3 read finished, 0 ready).
- Control block at `0x802D3E40` and SYSCONF buffer at `0x802D3FE0` (`0x4000` bytes).
- If the SYSCONF is not valid (checked by `0x800D2064`), the game creates an empty one with the `SCv0` and `SCed` markers and boots with default values.

**STM:** `__OSInitSTM` (`0x800A5B6C`) stores its init flag at `r13-0x6DDC` (`0x8031B284`). At `+4` and `+8` are the descriptors for `/dev/stm/immediate` and `/dev/stm/eventhook`.

**Located SDK global variables:**

| Address | What it is |
|---|---|
| `0x802B6CD0` | `__OSErrorTable` (at `+0x40`, the floating-point one) |
| `0x802B7078` | `DefaultThread` |
| `0x802B7390` | `RunQueue` |
| `0x802B7490` | `IdleThread` |
| `0x8031B1F0` | alarm queue (`r13-0x6E70`) |
| `0x8031B248` | pointer to the interrupt table |
| `0x8031B268` | `OSDisableScheduler` counter |
| `0x8031A5D8` | `SwitchThreadCallback` |
| `0x8031B290` / `0x8031B298` | power button notice and callback |
| `0x8031B740`, `0x8031B744` | IPC: initialized, low/high buffer |
| `0x80308898` | `btm_cb` (Bluetooth) |

**Thread object (`OSThread`):** `+0x2C8` state (u16), `+0x2CC` suspend count, `+0x2D0` priority, `+0x2DC` wait queue.

Sources: `docs/en/wiicompiled-feasibility.md`, `config/wiicompiled/rl8e54-data.csv`, `hle/mlb/sc_check_status.cpp`, `wiicompiled-mlb-stm-sda.patch`, `wiicompiled-mlb-bringup.patch`.

## 3. Threads and timing

- **The game runs at 60 frames per second** and waits for the vertical retrace (`VIWaitForRetrace`, `0x800B327C`). It does not draw extra frames.
- **Sound threads:** the game has a timer thread that wakes the sound thread every 5 ms (section 4). It measures time in ticks of its loop: each loop is worth 5 ms.
- **The main thread almost never blocks:**
  - During "NOW LOADING" it spins in a loop that does not block. On the console, the other threads make progress because the VI interrupt hands over to the highest-priority ready thread.
  - At the start of a match, while the players walk to their positions, it computes for 17-20 ms in a row (a whole frame) without yielding.
- **Interrupts:** the game code often calls `OSDisableInterrupts` (`0x800A0944`), `OSEnableInterrupts` (`0x800A0958`) and `OSRestoreInterrupts` (`0x800A096C`). On the console, a pending interrupt (for example, an expired timer) is handled right when they are re-enabled.
- **SDK thread functions:**

| Function | Address |
|---|---|
| `OSCreateThread` | `0x800A4448` |
| `OSResumeThread` | `0x800A4B50` |
| `OSInitThreadQueue` | `0x800A3E90` |
| `SelectThread` | `0x800A41CC` |
| `OSSleepThread` | `0x800A4F7C` |
| `OSWakeupThread` | `0x800A5068` |
| `OSSleepTicks` | `0x800A526C` |

- **Mii library (RFL) wait:** `RFLWaitAsync` (`0x800876A0`) spins on `RFLiIsWorking` (`0x80087B94`), which reads the flag at `+0x1B34` of the manager (pointer at `0x8031B168`). Until October 14, 2026 the HLE of `RFLiIsWorking` read the pointer at the Mario Kart Wii address (`0x80386298`, free memory in MLB); it now uses the MLB one (patch `wiicompiled-mlb-disc-identity`).
- **IPC:** `IPCCltInit` (`0x800CC534`) only initializes once: it checks and sets to 1 the flag `r13-26876` (`0x8031B764`), calls `IPCInit` (`0x800CC038`) and allocates `0x1000` bytes from the low buffer (`r13-26908`; the high one is at `r13-26904`). Disassembly, `RL8E54`.
- **For porting:** the game assumes that timers arrive on time and that interrupts cut into any computation. With cooperative threads you have to provide switch points (after `GXCopyDisp` and when interrupts are re-enabled) and catch up on missed periods.

Sources: disassembly of `0x80027A54`/`0x80027BF8`; `wiicompiled-preemption-point.patch`, `wiicompiled-periodic-sleep.patch`, `docs/en/wiicompiled-feasibility.md`.

## 4. Sound engine and commentator

Investigated in October 2026 to fix the commentator crackle (disassembly, memory watchpoints and runtime). The unnamed AX functions were identified by their behavior, not by signature.

**Sound files on the disc:**

| File | Contents |
|---|---|
| `sound/SeData.bin` | "KSS SE Link Data" |
| `sound/TrSeData.bin` | "KSS BGM Link Data" |
| `sound/WaveData.bin` | "KSS Wave Link Data" |
| `sound/strbgm.bin` | streamed music (321,687,552 bytes) |
| `sound/voice0.bin` … `voice17.bin` | 18 commentator voice banks |

- "KSS" is the label that appears in the files; it is not confirmed which middleware it comes from.
- The REL also has JAudio2/JASystem (`JAS*`, `JAU*`).
- The game's output goes in 3 ms blocks at 32 kHz.

### Threads

| Address | What it is | Priority |
|---|---|---|
| `0x80027A54` | **Sound thread.** Loop: sound work and `OSSleepThread(r13-0x7378)` until the next wakeup. Thread object at `0x80242888` | 4 |
| `0x80027BF8` | **Sound timer.** Loop: `OSSleepTicks(5 ms)` and `OSWakeupThread(r13-0x7378)` | 3 |

- They are created together with `OSCreateThread`, `0x1000`-byte stacks, and started with `OSResumeThread`.
- The period comes from the bus clock: `[0x800000F8] / 4 / 1000 × 5` ticks = 5 ms (303,750 ticks at 243 MHz).
- **The game counts time in loops, not with a clock:** on the console there are 200 loops per second, and if they arrive late, the audio that depends on them gets longer.
- Each loop, in order:
  1. `0x800329BC`, `0x80027CD4`, `0x80028290`, `0x80027F60`, `0x8002809C`, `0x80027EAC`
  2. `0x8002F660`
  3. the stream manager `0x80038E50`
  4. `0x80035A3C`
  5. the channel processing `0x8002C604`
- Every 4 loops it also calls `0x80007360` and `0x800073B0`. The loop counter is at `r13-0x73C0`.

### AX channels and voices

- **55 channels** at `0x80236930`, 596 (`0x254`) bytes each. The pointer to the AX voice (`AXVPB`) is at `+584`.
- The commentator uses **channel 19**.
- **Sound command queue** at `0x80297C20`: 28-byte entries (type, channel, value).
  - Type 8 changes the resampling ratio; ratio 0 = pause.
  - Dispatcher: `0x8002D038`.
- `0x8002C480(channel, mode)`: 0 query, 1 acquire, 2 release. Stopping follows the chain `0x8002C480` → `0x8002C310` → `AXSetVoiceState`.
- **Located AX functions:**

| Function | Address |
|---|---|
| `AXSetVoiceSrc` | `0x800BBD38` |
| `AXSetVoiceSrcRatio` | `0x800BBDBC` |
| `AXSetVoiceState` | `0x800BB99C` |
| `AXSetVoiceAddr` | `0x800BBB08` |
| `AXAcquireVoice` | `0x800B8CC0` |
| `AXFreeVoice` | `0x800B8C44` |
| `__AXSetupDsp` | `0x800BA458` |

- AX DSP task at `0x802C12A0`; microcode image at `0x80225E00`.
- Task callbacks: init `0x800BA3E4`, resume `0x800BA3F0`, done `0x800BA444`, request `0x800BA454`.

### Commentator streams

- **Table of 4 stream objects** at `0x802A1A78`. `0x80038E50` walks the 4 and calls the handler `0x80038F08` for each valid one.
- An object is valid if it has `0x5A5A5A5A` at `+0x824` and `0x8A8A8A8A` at `+0x1960`. `+0x820` is its index.
- **State at `+0x828`.** The handler jumps through the table `0x801559E0` and stores what each case returns as the next state:

| State | Target | What it does |
|---|---|---|
| 0 | `0x80038F4C` | clears the stream (frees what `+0x898`, `+0x8A0`, `+0x89C`, `+0x894` point to) |
| 1 | `0x800398BC` | load; depends on `+0x848` (DVD request) through `0x8003B398` |
| 2 | `0x80039B94` | reads the phrase header and sets up its clock; returns 3 |
| 3 | `0x8003A048` | starts playback |
| 4 | `0x8003A248` | plays: advances the clock; returns 4 or 5 |
| 5 | `0x800392E8` | end of phrase |
| 6 | `0x800392E0` | exit without changes |
| 7 | `0x800391C0` | `+0x8C0 = 5000`, goes to 8 |
| 8 | `0x800391D4` | `+0x8C0 = 5000`, goes to 3 |

- **Voices:** mono DSP-ADPCM at 22,050 Hz. They are resampled to 32 kHz with linear interpolation; the AX ratio is the frequency × 2.048 (`0xB066` for 22,050 Hz).
- There are **two stream voices** that alternate, each with its own 64 KB ring buffer, and one voice is played per phrase or name.
- **Phrase header** (`0x800`-byte block before the data):

| Offset | Contents |
|---|---|
| `+0x00` | phrase data bytes |
| `+0x04` | `(0xB4 or 0x78) << 16 \| 22050`: flags and frequency |
| `+0x10` | **duration in milliseconds** (e.g. `0x0A0F` = 2,575 ms) |
| `+0x88` | length in ADPCM nibbles |
| `+0xB0` | initial predictor scale |

- **Clock of each phrase:**
  - `0x80039B94` (state 2) copies the duration to `+0x858`; if it is 0, it computes it with `0x80038430`. It adjusts it by `+0x8F8`/`+0x8FA` (`(x + 0x100) >> 8`) and stores it in `+0x874` (total duration) and `+0x85C`. Then it sets `+0x858 = 0` and `+0x8C0 = 5000`.
  - `0x8003A248` (state 4) **adds 5 to `+0x858` on every loop**. When it reaches `+0x874 − 10`, it stops the voice with `0x8002C7CC(+0x834)` and moves to state 5.
  - `0x8003A248` also advances the buffer positions (`+0x888`, `+0x8AC`, `+0x8B8`) according to `+0x904` and `+0x8FC`.
  - `0x80038358(index)` returns the ms left in the phrase (`max(0, +0x874 − +0x858)`) in state 4, `0xFFFF` in other states and 0 if the stream does not exist.
  - After each loop in state 4, the handler calls `0x8002670C` with `+0x854`. It is an empty stub (`blr`).
- **How the game stops a phrase:** pause (ratio 0) and then release and reassign the voice. It does this 2-4 blocks (6-12 ms) after the data runs out, while the ADPCM decoder holds its last level. That is why the original DSP applies a ramp to zero (*depop*).
- **Clipping:** the commentator phrases reach ±32767 in the disc data itself: 11 of 12 phrases decoded separately clip. It is not a recompilation bug.

**Bug found and its cause (for porters):** if the 5 ms timer does not wake up 200 times per second, the phrase clock runs slow. The phrase is then stopped between 1.2 and 1.7 times late: the zero padding and the beginning of the next phrase are heard, and then it is cut off. It sounds like clicks and clipped words.

**The same with a hidden window on Windows 11** (October 19, 2026): Windows 11 stops honouring `timeBeginPeriod(1)` while the program's window is minimized or fully covered, and timers go back to 15.6 ms. Measured with a `Sleep(5)` test program: 135 wakes/s with the window visible and 64/s minimized. At that pace the 5 ms timer runs at a third and the commentary breaks for as long as it lasts (a player: about 15 seconds in the seventh inning). The runtime asks for the opt-out with `SetProcessInformation(ProcessPowerThrottling)` (`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION` and `EXECUTION_SPEED`): 171-177/s minimized too. Patch `wiicompiled-timers-when-hidden`.

Sources: disassembly (`0x80027A54`, `0x80027BF8`, `0x80038E50`-`0x8003A41C`); `wiicompiled-periodic-sleep.patch`, `wiicompiled-audio-depop.patch`, `config/wiicompiled/rl8e54-data.csv`, `docs/en/wiicompiled-feasibility.md`.

## 5. Disc and `project.bin`

- **Disc:** `RL8E54`, internal title `MLB Power Pros`. The FST has 28 files, with no `.map`, `.sym` or `.elf`. The main ones:
  - `sys/main.dol`, `files/ovl_main.rel`
  - `files/project.bin` (406,388,736 bytes)
  - `files/sound/*`
  - `files/movie/*.thp` (3 videos)
  - `opening.bnr`

**`project.bin`** (all big-endian; verified over its 2,424 entries):

| Offset | Contents |
|---|---|
| `0x00` | version = 2 |
| `0x04` | sector size = `0x800` |
| `0x0C` | hash table slots = `0x1211` |
| `0x10` | hash table, at `0x40` |
| `0x14` | record table, at `0x6000` |
| `0x28` | order table, at `0x26000` |
| `0x2C` | name table, at `0x2B000` |
| `0x34` | data start, at `0x3D800` (the file ends exactly at `0x18390000`) |

- **Records** of `0x1C` bytes: 0, name offset, size in sectors, alignment (`0x800`), 0, 0, size in bytes.
- **Order table:** it is split into **buckets**. The game code confirms it: mount `0x80059E7C`, index `0x8005A334`, lookup `0x800599E4`.
  - The bucket positions inside the order table are in the table at `[0x18]` (`0x5800`); their count, at `[0x20]` (2).
  - Each bucket is: u32 entry count, u32 0 and the pairs (file number, start sector relative to `0x3D800`), sorted by sector.
  - The disc has an empty bucket at `+0` and another at `+8` with the 2,423 files.
  - `tools/projectbin.py` reads that bucket's header, (2423, 0), as an unnamed first pair. It is harmless for listing, extracting and rebuilding.
  - **Adding files requires raising the counter of the last bucket.** Without that, the game does not find the new file and reads from sector 0 (verified).
- **File number table** (`0x800`, [`0x0C`] = 4,625 slots):
  - Slot i holds the position of record i − 1 in the record table.
  - The game indexes it with number + 1, so file number n is record n.
  - The game requests files by number, never by name: there are no file names in the executable.
- **Names:** NUL-terminated paths starting with `/`.
- **Types:**
  - `.pack`: 2,356 start with `PACKWII ` and 2 with `PACKPS2` (`team_logo.pack`, little-endian). Format below.
  - `.dat`: 60 `PCD`, 2 `TCD` and 1 `SID`, little-endian: comments for the player and team cards, and for the shop. They start with the tag and a NUL, a `float` 1.0, the number of texts and their positions. The text is in u16 with its own character table, not decoded yet.
  - `.arc`: 2 Nintendo U8 files (`RFLRes01`, `SpeakerSe`).
- **Mounting:** `0x800431E8` mounts `/PROJECT.BIN` (string at `0x80155DD0`) by calling `0x80059E7C`.
- **DVD resource manager:** `0x80042C28` only processes its queue when `DVDGetDriveStatus` says the drive is idle. At boot, the game issues `DVDInquiryAsync` and then polls the status.
- **Known entries:**
  - `/game_data.pack` (roster, [section 7](#7-roster-game_datapack-and-player-format))
  - `/ClaimTheTitle.pack`, `team_logo.pack`
  - `env_title_logo`, `env_mainmenu`, `env_teamnameNN`
  - `PWM_mdl_uniform_MLB_<team>_<n>.pack`: uniforms. Swapping CHC's for NYY's puts the "NY" cap on the Cubs.
- **For mods:** a rebuilt `project.bin` only changes the record sizes (`+0x08` sectors, `+0x18` bytes) and the start sectors. The header, the hash table and the names do not change (`tools/projectbin.py`).

### `PACKWII` format

Big-endian, an object graph with pointers. Verified over the 2,356 entries.

- **Header:** `"PACKWII "`, u32 0, u32 entry count (`+0x0C`), pointer to the table (`+0x10`), u32 `0x20` (header size).
- **Pointer:** `0x40000000 | position in the file` (base 0).
- **Table:** 12-byte entries {u32 0, u32 id, pointer}. Only 19 packs have it; the rest reach their objects from a root structure at `+0x20`.
- **Tags at `+0x20`:**
  - `1MPP`: pitcher motions (263 packs).
  - `1MBP`: batter motions (249).
  - `PJ7M`: models (8).
  - Others: `1HMP`, `1UAP`, `1LMP`, `pM_U`.
  - 1,822 have no tag: `env_*` interfaces (`pM2`), models, stadiums, seasons.
- **`SURF`:** 2D layer of the interfaces. Size `0x2C`, name, scale 100/100 and pointers.

**Textures** (checked by decoding them; `tools/packwii.py`):

| Type | Header |
|---|---|
| Model | u16 **height**, u16 **width**, u8 type (`0x80` RGBA8, `0x81` RGB5A3, `0x82` C8, `0x84` C4), `+0x08` pointer to the pixels, four words `0x82000000`. In C4/C8 the palette header is at `+0x24`. |
| Interface | u16 **height**, u16 **width**, u32 GX format, `+0x08` pixels, `+0x14` 1, `+0x18` 1, `+0x24` u16 palette entries, `+0x28` u32 palette format (2 = RGB5A3), `+0x2C` pointer to the palette (0 if it is elsewhere). |
| Palette | u16 entries (16 or 256), u16 0, u8 `0x81`, u8 0, u16 `0x6000`, `+0x08` pointer to the RGB5A3 colors. |

- **Texture sets:** {u32 n, → n textures, u32 m, → m palettes}. There is one palette for all or one per texture.
- **Pixels:** in GX block order (C4 8×8, C8 8×4, RGB5A3 and RGBA8 4×4; RGBA8 stores AR and GB in two 32-byte halves per block).
- **Coverage:** 11,292 textures in 1,637 packs; 10,769 of the 11,200 with a palette end up paired.
  - By pointer: interface header, texture sets, model with palette at `+0x24`.
  - By position: the palette colors come right after the pixels. The palette header right before a texture belongs to the previous texture, not to it.
  - The rest get the palette from another pack.
- **Checked against the game** (October 14, 2026, runtime texture dump up to the main menu):
  - The XXH64 hash of the pixels computed from the pack matches the game's: the game uses the pack bytes as they are. With the orientation fixed (below), Texture Studio computes the exact name of 37 of the 39 dumps that come from packs.
  - Of 38 pack textures seen with a palette, 36 have the same palette as the one paired here. The other two: a loose palette in `env_common.pack` with no known pointer, and `env_mainmenu.pack` `0x163A0`, which the game draws with a palette different from the one its header points to (palette change at runtime).
  - The rest of the dump does not come from packs: THP video frames (I8 at 512×448 and 256×224) and a few non-power-of-two textures (text generated at runtime, to be confirmed).
- **Main menu label atlas:** `env_mainmenu.pack` `0x2A920` (512×512, C8) has the 12 labels in their colors; the top half is the normal one and the bottom half the selected one. A box painted over "Exhibition" shows up in the game.
- **On import**, `packwii.py` keeps the palette of the position-paired textures fixed and preserves the exact index of the unedited pixels, because the game may draw a texture with another palette.
- **Height before width** (October 14, 2026): the texture headers store the height first and then the width.
  - The 19 non-square menu dumps have their dimensions the other way round from how they were read (for example `env_mainmenu.pack` `0x12140`: the game uses it as 256×64).
  - Also measured by image smoothness on 1,600 non-square textures from packs: 1,585 look right when read as height and width, model ones included.
  - It was the cause of the "striped" textures in `env_mainmenu.pack` (`0x12140`, `0x9B6E0`, `0xBB940`): they now decode correctly (`0x9B6E0` is the Wii Remote menu art).

**DVD functions:**

| Function | Address |
|---|---|
| `DVDInit` | `0x800A9F7C` |
| `DVDOpen` | `0x800A9764` |
| `DVDClose` | `0x800A9A10` |
| `DVDReadPrio` | `0x800A9CDC` |
| `DVDLowInquiry` | `0x800B0F40` |
| `DVDLowRead` | `0x800B1840` |
| `DVDLowSeek` | `0x800B19F0` |

- Retail drive reply the game expects: `+0` = `0x00000002`, `+4` = `0x20060526`, `+8` = `0x41000000`.

Sources: `docs/en/phase0-report.md`, `docs/en/mods.md`, `tools/projectbin.py`, `tools/packwii.py`, `wiicompiled-dvd-low-completion.patch`.

## 6. Stadiums: `PJ7M` models and `tbl` table

Each stadium is two `project.bin` entries:
- `stadium_<code>.pack`: the graphics. Between 50 and 100 `PJ7M` models and their textures, about 1.1 MB.
- `stadium_<code>tbl.pack`: the placement of each model and the collision mesh.

The codes are the team ones (`col` = Coors Field, `nyy` = Yankee Stadium…), plus `org01`–`org06`, `ml01`, `bullpen`, `bullpen2` and `season`. There are also `stadium_sky_*` (skies, no table), `stadium_common`, `stadium_hanabi` and `stadium_debug`.

Verified (October 14, 2026):
- `tools/stadium_re.py check` walks the 47 stadium packs and they meet the invariants below.
- Four changes in a copy of Coors Field, loaded as a mod in a match, show up in the game (see "Verified in-game").

### `PJ7M` model

Big-endian, pointers `0x40000000 | position`.

| Structure | Contents |
|---|---|
| Header (`0x28`) | `'PJ7M'`, u32 `0x28`, → name (placed right before it), → root, 0, 0, u32 textures, u32 positions (sum over all meshes), u32 triangles, 0 |
| Root | u32 9, → scene, → texture table, → palette table, zeros, `+0x40` → `"_Wmodel_<name>"` |
| Scene | {1, → node}, {1, → ?}, {n, → texmaps}, {n, → materials}, u32 `0x37` |
| Node (`0x0C`) | u16 mesh count, u16 0, → mesh list, → Maya node name (`pPlane650`…) |
| Mesh list | 8-byte entries: u16 material, u16 1, → mesh |
| Mesh (`0x1C`) | u32 0, u32 display list size, → display list, → POS, → NRM, → CLR0, → TEX0 |
| Material (`0x34`) | u16 3, u16 first texmap (`0xFFFF` without texture), 3 RGBA8 colors, `+0x1C` 8 u16 texmap slots (`0xFFFF` = free; one is always used) |
| Texmap (`0x4C`) | u16 texture index, `+0x0C` u16 width − 1 and height − 1, `+0x1C` u16 and `+0x1E` u16 how many times the texture repeats in U and V; bytes `+2..+0x0C` are the same in the 11,281 of all stadiums |
| Texture and palette tables | 8-byte entries {→ header, 0}, in parallel. The headers are the model texture ones from the texture table above. Without a palette, the pointer is 0. |

- **Vertices:** not quantized.
  - POS and NRM in f32 xyz, CLR0 in RGBA8, TEX0 in f32 uv.
  - UVs go up to 32: textures repeat.
  - **Texmap repeats** (`+0x1C` U, `+0x1E` V): in the meshes of the 43 stadium packs, always `≥` the largest `|u|` and `|v|` of the meshes using it (usually rounded up; up to 280). With 1 the game does not repeat the texture in that direction and stretches the edge: flat colour with 1/1, stripes with 1 in one direction only. Seen on 18 October 2026 in the viewer with the Polo Grounds on AT&T Park (SketchUp UVs up to ±1,600, templates 1/1 and 1/2): grey stands and striped towers; with the fields set right, the same as in Blender.
  - Each array is as long as the highest index in its display list asks for; there is no size field.
  - A few normals are (0, 0, 0).
- **Display lists:**
  - Aligned to 32 bytes and padded with NOP.
  - They only use `0x98` (strip, 182,126) and `0x90` (triangles, 3,647), always with VAT 0.
  - Each vertex takes 9 bytes: u8 matrix index (`PNMTXIDX`: 0, or 3/6/9/12 only in skies and `common`) and u16 for POS, NRM, CLR0 and TEX0.
- **Header triangle count:** counts n − 2 per strip, degenerate ones included (450 of 450 models checked).
- **Triangle winding:** clockwise seen from the front (as in GX). The geometric normal is opposite to the stored one in 11,093 of the 11,109 triangles of Coors Field.
- **Units:** decimeters. Y up, home plate at the origin, −Z toward center field and +X toward right field.
  - The pitcher's rubber is 6 long (61 cm) and is at z ≈ −190.
- **The field is fixed; the stadium adapts to it** (October 14, 2026):
  - in the collision of every stadium except Toronto, the mound (surface 8) has its center 18.51 m from home plate and a 5.64 m diameter;
  - the stadium has no data with the position of the bases, the batter's box or the fielders: its graphics only draw them;
  - that is why a custom model has to be placed in that frame (home plate at the origin, bases 90 feet apart), so that what is drawn matches where the game puts the players;
  - unchecked: where the fielders get the distance to the fences from, whether from the collision or from the base stadium's data.
  - The left field fence is at about 1,085 (108 m).
- **Textures:** C4, C8, RGB5A3 and RGBA8.
  - They are shared between models of the same pack; Coors Field has 115.
  - The `*_ALP_*` pieces carry textures with alpha: patches on the grass, dirt and lights.
- **No transform in the model:** almost everything is in world coordinates. Repeated pieces (flags, fountains) are in local coordinates and are placed with the table.
- **Piece names:** `DP_ground`, `DP_MIP_turf`, `DP_stand_L1`…`R3`, `DP_fence_L/R`, `DP_score`, `DP_auroravision`, `DP_Audience*`, `DP_FLG_flagNN`, `DP_*_lumine_*`, `DP_EVC_NOUPCAM_outside_*`…

### `tbl` table

- **Root:** it is the last object in the file: u32 model count and 11 pointers. It is found by searching from the end for the count followed by the pointer to `0x20`.
- **Root pointers, in order:**
  1. placements;
  2. tree A: plane nodes that partition the placements (see "Trees A and B");
  3. u16 lists of tree A (placement indices);
  4. tree B: plane nodes that partition the collision triangles;
  5. u16 lists of tree B (triangle indices);
  6. collision vertices;
  7. collision header;
  8. position-plus-direction records (`0x18` bytes, unidentified; they come right after the collision vertices);
  9. two {→, 7} pairs, unidentified;
  10. texture list of the graphics pack;
  11. unidentified.
- **Placement** (`0x54` bytes, one per model): → `PJ7M` header in the graphics pack, xyz translation, xyz rotation in radians, xyz scale, box (min. and max.), sphere (center and radius), u32 flags.
  - Only 12 of the 2,925 placements across all stadiums rotate in X or Z. The axis order is not checked; the importer uses XYZ.
- **Collision:** u16 triangle count, u16 vertex count, u32 0; then triangles of four u16 (a, b, c, surface) and f32 xyz vertices.
  - Coors Field has 2,922 triangles and 2,133 vertices.
  - **Orientation:** the normal (b − a) × (c − a) points to the side the ball hits. All floors (5, 6, 8) face up and almost all walls (0, 1, 2, 9, 15) face home plate. In the stands (11) there are both orientations.

### Trees A and B

October 14, 2026; `tools/stadium_re.py check` checks them in the 47 packs.
- **`0x1C`-byte node**, in preorder and with node 0 as the root:

| Offset | Contents |
|---|---|
| `+0x00` | plane: nx, ny, nz, d (f32) |
| `+0x10` / `+0x12` | u16 child of side H / side L (0 = the side is a leaf) |
| `+0x14` / `+0x16` | u16 start of the list of side H / L |
| `+0x18` / `+0x19` | u8 entry count of the list of side H / L |
| `+0x1A` | u16 0 |

- **Side H:** what reaches n·p + d < 0. **Side L:** what reaches n·p + d > 0. Whatever crosses the plane is in both.
  - In the 47 packs, every triangle (by its vertices) and every placement (by its box and its sphere) is on the correct side of all the planes above it.
  - From the root, every triangle and every placement can be reached.
- **Tree A:** partitions the placements by their box. In AT&T Park, Coors Field and Yankee Stadium it uses diagonal planes; in all packs it has at most 32 per leaf. The game uses it to **cull by camera**: see «Writing packs from scratch».
- **Tree B:** partitions the collision triangles. It uses axis-aligned planes, depth ≤ 13, leaves of 11 to 15 triangles on average and at most 111.
- **Traversal:** `0x80C860E4` and `0x80C862EC` are recursive (index × 28). They call `0x80CCB438` with the plane to decide which sides they visit. At each leaf they set `flags[index]` to 1 in an array of one byte per element: there is no fixed-size buffer.
  - **They read the leaf count as signed** (`lbz` + `extsb`): a leaf with more than 127 entries is not traversed.

### Writing the collision

October 14, 2026, `tools/blender/powerpros_stadium/tbl_write.py`.
- **Where the data goes:** the collision, the vertices and the two new trees go after the root, and their fields are repointed. The root does not move: the game's stadium table points to it, and the runtime still finds it when searching from the end.
- **Building the trees:** axis-aligned planes and, at each node, the cut that splits best without duplicating much; leaves of at most 127.
  - With the Coors Field triangles this gives a tree of 240 nodes and 6,450 entries; the game's has 1,086 nodes and 13,047 entries.
- The Blender exporter rebuilds tree A when a piece changes its box, and writes the collision when its mesh is edited.
- **Checked outside the game:** round trip in Blender (`tools/blender/test_export.py`) and the same checks as the game packs.
- **Verified in-game** (October 14, 2026): new stadium "Polo Grounds", made from a SketchUp model converted with the add-on, based on AT&T Park, 7.8 MB of graphics and generated collision of 7,116 triangles.
  - The benchmark match loads and plays at 60 FPS.
  - The model's home plate, batter's box, bases and mound match the players.
  - A ground ball rolls across the new field normally, and no faces are missing nor are back faces visible.
  - Checked by the user (October 15): batted balls hit the fences and home runs are scored. But the generated collision did not follow the visible model everywhere: in some spots the ball should have landed in the stands that are drawn there and instead bounced off a wall that is not visible. What the player sees is not always what the game collides with; the collision generator needs work.
  - There are 3 degenerate or duplicate triangles.
- **Surface IDs** (by geometry in Coors Field and Yankee Stadium; their effect in the game is unchecked):

| ID | Geometry | Probably |
|---|---|---|
| 0, 1, 2 | 70 m vertical walls around the field | invisible boundaries |
| 5 | floor at height 0, from home plate to the back | grass |
| 6, 7 | floor at height 0 | dirt and warning track |
| 8 | 25 m² at 16–21 m from home plate, barely raised | mound |
| 9 | low walls (up to 4 m) | fences and field walls |
| 11 | the most numerous, 40 to 170 m from home plate, up to 34 m high | stands |
| 13 | raised horizontal surfaces | stands and roofs |
| 14 | vertical at about 108 m, up to 25 m high | foul poles |
| 15 | wall at 15–16 m (Coors) or 25 m (Yankee) from home plate | backstop |
| 17, 18 | few faces | unidentified (18 in Coors: plane at 138 m, the fountain?) |

**Verified in-game** (October 14, 2026): a modified copy of `stadium_col.pack` and `stadium_coltbl.pack` in a mod's `project/`, in a benchmark match at Coors Field.
- Raising the positions of `DP_stand_C` by 15 m raises the center stands.
- Making the vertex color of `DP_ground` blue turns the infield dirt blue.
- Changing the second color of the `DP_MIP_turf` material to red changes nothing: the stadium is drawn without material lighting.
- Changing the translation of the `DP_FLG_*` placements puts the flags in a row on the center field grass. The game uses the table, even though the placement's box and sphere stay in the old place.
- Flags (`0x500`) are cloth the game animates from the placement, not from the piece's mesh: with the piece emptied they keep waving in place. They are drawn without depth test: moved 100 m under the field they showed through the ground. With the placement scale at `1e-4` they disappear (18 October 2026).
- **Per-stadium menu pack:** `env_stadium_menuNN.pack`, NN being the stadium number (`kStadiumCodes` order, Coors Field = 27). Two C8 textures with their own 256-colour palette: the photo (512 × 256, pixels at `0x60`) and "Home of…" (512 × 64). A pack of the same size with both textures re-encoded works as a new stadium's menu.

### Writing stadiums

Verified in Coors Field matches, October 14, 2026.
- **New data at the end of the pack:** it accepts it, with new pointers (`0x40000000 | position`). A mesh repointed to a new display list of separate triangles (`0x90`) instead of strips, and to new arrays, draws fine. There is no relocation table to update.
- **Size:** a 9.5 MB and a 21 MB `stadium_col.pack` load and the match plays. A 31.5 MB or a 43 MB one gets stuck on "Preparando partido" (Preparing match): it does not fit in the MEM2 heap (section 1). With the enlarged heap (patch `wiicompiled-mlb-mem2-heap`), a 32 MB and an 80 MB one load and the match plays; at 96 MB the pack is read but the match does not start.
- **Texture and palette tables:** they end with an 8-byte zero entry, in the 82 models of Coors Field. The game walks them up to that entry: when copying them elsewhere without it, the model's palette textures come out white. Enlarging the texmap array or the material array (which use their count from the scene) works. **Texmaps per piece limit:** no piece of the 47 packs goes over 30. Each material holds at `+0` (s16) an index that `0x800613BC` passes to `0x800537B4`, which uses it in a table of `0x78`-byte slots; with 82 texmaps in one piece (a converted downloaded model, 18 October 2026) the indices ran past it: `GXLoadTexObj: invalid tex map id`, corrupt TLUT objects, fences not drawn and out-of-memory reads from `0x8069CF38` until the game closed. Spread over pieces of at most 30, the match runs fine. The table's exact capacity is not measured.
- **`tbl` texture list:** entries {→ texture, → palette, u32, u32}. The two u32 (`0xAC800`, `0xB2540`…) look like TMEM addresses: preloaded textures. Unchecked.

### Bringing in an external model

Polo Grounds from SketchUp, October 14 and 15, 2026; benchmark matches.
- **Sizes of the game's stadiums**, in the 47 packs:
  - at most 24,768 bytes of display list, 573 vertices per draw call, 1,303 positions per mesh and 29 meshes per model;
  - textures from 32 to 256 pixels, almost all single-level (`MIP` pieces, such as the turf, also use smaller levels: see «Field inventory…»): 8,135 CI4, 3,130 CI8, 77 RGBA8 and 33 RGB5A3 (`tools/stadium_re.py stats`). The recomp builds mipmaps for them on load (patch `wiicompiled-auto-mipmaps`).
  The exporter splits meshes at 600 triangles, 1,300 positions and 570 vertices per call, and reduces new textures to 256 pixels. Without splitting, what was missing on screen was later explained by the coplanar surfaces, not by the size: the splitting is a precaution.
- **Depth:** parallel surfaces 0–2 cm apart (windows on a wall, a seat texture on the stands, the two faces of a wall) flicker or hide each other at 100–150 m; in Blender they look fine. Separating them or leaving a single face fixes it. The recomp draws with `Depth32Float` reversed-Z depth (aurora, `UseReversedZ`), far more precise than the Wii's 24-bit one: faces at 0 cm will always flicker, but at 2 cm they should not. Not checked with an isolated test.
- **Back faces:** the model, with both faces of each wall in the same place, showed the back one covering the good one. With a single face facing the field it looks fine. It is not checked whether the game culls back faces in stadiums.
- **Large textures** (512 px or more) look noisy at 100 m: almost no stadium texture carries mipmaps (only those of `MIP` pieces).
- **`0x80047874`** does not draw models. It sets `GXSetZMode(1, GX_ALWAYS, …)`, changes the color and alpha writes and draws a square: it is a screen clear.
- **Stadium crowd:**
  - `DP_AudienceF_*` are almost horizontal faces over the stands (normal z ≈ 0.95), with 128 × 64 textures that repeat every 6 m of width and 4 m of height;
  - `DP_AudienceS_*` are vertical billboards that repeat every 6 × 3 m;
  - the stands use a 128 × 128 seat texture every 25 m.
  - New faces in those pieces, with their materials, show in Blender, but they did not appear in the test match. Unresolved.
- **A texture with a palette from another piece:** reusing it in a different piece (enlarged texture and palette tables) drew it black. Converted to RGB5A3 it shows.
- **Piece order:** rebuilding tree A as a single leaf in table order changed nothing visible.

### Field inventory for writing packs from scratch

18 October 2026; a survey of the 43 graphics packs and 40 tables, field by field in 2- or 4-byte steps.
- **`PJ7M` model:** everything that varies is a pointer, count or size, worked out when writing, except:
  - the scene's second list `{1, → ?}`: it points to a pointer to the word `0x01000000`, the same in the 2,925 stadium models that have a table (what was read after it was zero padding and the start of a display list). Only one `stadium_common` model has another word and fields at `+0x10..+0x3C` of the root;
  - the material: `+0x00` is 3 (11,239) or 4 (6); the three RGBA8 colours at `+0x04` are almost always `000000FF`, `CCCCCCFF`/`FFFFFFFF` and `000000FF` (no visible effect seen); `+0x32` is 1 (7,567) or 0 (3,678), not decoded;
  - the texmap: besides index, size and repeats, all constant;
  - the texture header: `+0x06` is `0x2000` for C4, `0x0000` for C8/RGBA8 and `0x6000` for RGB5A3;
  - the video board (`DP_auroravision`) uses an 8 × 8 RGB5A3 texture with a «placeholder» palette header: 0xFFFF entries, format `0x4000` and no colours (pointer 0); it looks like where the game puts the board's picture;
  - unused textures: several packs carry texture headers (with their palette at `+0x24`) that nothing points at, neither the models nor the table;
  - after the pixels of some textures come their **mipmaps**: for Arizona's 32 × 64 C4 texture, the 16 × 32 and 8 × 16 levels. The model's texture table says which level each entry uses: the second u32 of each entry {→ header, level} is 0, 1 or 2 (176 entries are not 0). `MIP` pieces repeat the same texture at levels 2, 1 and 0 (`DP_ALP_MIP_line` in Arizona);
  - the texmap has its own size (`+0x0C`): in 129 texmaps it is not its texture's. In crowd pieces, an 8 × 8 texture is declared 128 × 64: it looks like the game swaps it at run time for the crowd's frames.
- **The game's code does not look pieces up by name:** neither `main.dol` nor `ovl_main.rel` contains `DP_`, `Audience`, `FLG` or `auroravision`. Each piece's role comes from the **placement flags**.
- **Placement flags by piece** (from the names of the models carrying them; hypotheses to check in the game):
  - **byte `0x0000FF00`, piece type:** `0x01` scoreboard, `0x02` video board (`auroravision`), `0x04` mound and lines (`BCL`), `0x05` flags (no depth test), `0x06` shadows, `0x08` lights (`lumine`), `0x09` lit floodlights, `0x0A` floodlight parasols, `0x11` fences, `0x12` backstop net, `0x15` crowd C, `0x16` crowd F (lying), `0x17` crowd S (upright), `0x18` fountains, `0x19` `MIP` turf, `0x1A` turf with alpha; `0x00` stands and the rest;
  - **byte `0x00FF0000`, how it is drawn:** `0x06` alpha (`ALP`), `0x0A`/`0x0F` additive (`ADD`), `0x81`/`0x87`/`0x90`/`0x93` ground, `0x20` `LOF`;
  - **high bits:** `0x40000000` upper decks behind home (`2F`), `0x20000000` `EVC`, `0x80000000` `NOUPCAM`; `0x04000000`/`0x08000000` go with ground and turf.
- **Undecoded table blocks** (Coors Field):
  - `unk_20`: `0x20`-byte groups {→ point list, u32 n, box min xyz, max xyz} closed by a zero group; each point is `0x18` bytes {position xyz, rotation xyz}. Coors Field has 4 groups (32, 50, 50 and 25 points) about 50 m up and Yankee Stadium 2 (150 and 144); Arizona, `bullpen`, `bullpen2` and `season`, none. From the counts and the rotation, it looks like crowd placed in 3D;
  - `unk_24`: two pairs {→ list, u32 n} of `0x1C`-byte records {position xyz, scale xyz = 1, angle}; in Coors, 7 per list, 24 m to each side of home and 0.64 m below ground: they look like the dugout seats. `bullpen`, `bullpen2` and `season` have a single pair {0, 0} with the root right after it;
  - `textures` (pointer 10): `0x10`-byte entries {→ texture header, → palette, u32, u32}, the two u32 being TMEM addresses (preloaded textures);
  - `unk_2c`: {u32 n, n × u32 order} (always 8 and 0 to 7) followed by the texture list, which ends with an entry {0, 0, u32, 0}; that u32 goes from `0x692800` to `0x703280` depending on the stadium (not decoded). In `bullpen`, some entries point at no texture.

### Writing packs from scratch

18 October 2026; `tools/blender/powerpros_stadium/stadium_model.py`, `stadium_read.py` and `stadium_write.py`.
- **Description:** a stadium as data (textures, palettes, models, placements, collision, tree A and the table blocks), with indices instead of offsets. The reader takes it from the game's two packs; the writer writes both packs from empty buffers, reading no pack.
- **Checked outside the game** (`tools/test_stadium_writer.py`, 45 s):
  - the reader covers every non-zero byte of the 40 stadiums with a table;
  - every structure written is byte for byte the game's, with its pointers zeroed (except tree B, which is rebuilt);
  - read, write and read again gives the same for the 40, and what is written passes `stadium_re.py check`;
  - a description turned into lists, numbers and hex text writes the same bytes: the writer uses nothing else.
- **Checked in the game:**
  - an **invented minimal stadium** (`tools/stadium_minimal.py`: checkered ground, fence, stands and collision, reading nothing from the game, with the table blocks empty) shows in the viewer after 7.6 s and a 2-minute match plays in it, at 60 FPS with no out-of-memory accesses;
  - **Coors Field, AT&T Park and Yankee Stadium rebuilt** (`tools/stadium_rebuild.py`, as new stadiums) look in the viewer the same as their originals, loaded the same way, from the batter's view and two free cameras: mean pixel difference 0 to 0.003 out of 255. A 2-minute match with each, with no out-of-memory accesses.
- **Tree A culls by camera:** rebuilt as a single leaf (as the add-on's exporter does), Coors Field seen from outside drew the stands behind home and other pieces the original does not draw from there; from the batter's view nothing changed. The game walks that tree with the camera position and drops the placements on the sides it does not visit (`0x80CCB438` decides the sides). That is why the description keeps the game's tree A.
- **Pending** (phase 2): what each placement flag does, the `unk_20` and `unk_24` blocks, the preloaded textures, the material fields and how to build a tree A of our own that culls well.

### What the fields do, tested in the viewer

Phase 2, 18 October 2026. Method: Coors Field rebuilt with the writer, with a single value changed, compared in the viewer with the unchanged rebuild from the same cameras (the agent's `exp.py`, captures at 17 and 19 s to see what animates).
- **Placement flags, byte `+0x52` (piece type).** The code reads it in `0x80C600C8`, which treats 17 (fences) and 18 (the backstop net) apart. Removing the type (to 0):
  - `0x16` and `0x17` (lying and upright crowd): the crowd **stops animating** (the difference between two captures drops from 1.44 to 0.25 on a 0-255 scale) and is still drawn;
  - `0x01` (scoreboard): what the game draws on it live disappears (the score rows come out as black blocks);
  - `0x02` (video board): the board comes out black: the game paints its picture into the «placeholder» texture;
  - `0x05` (flags): the flags are drawn as normal, still pieces. With their type the stadium does not draw them; a flag system of their own does, which hangs from the match's state tree: **in the viewer, with the match frozen, they do not show** (those of a stadium written from scratch do show in the viewer: see «Stadium from scratch in the game»);
  - `0x11` (fences): no visible change: their effect is on play, not on drawing.
- **Placement flags, byte `+0x51` (draw group).** `0x80C600C8` looks it up in a list of groups of the scene object (`+1528`, entries with their number at `+4`). Changing it:
  - `0x06` (`ALP` pieces) to 0: the field's stains and lines go from soft transparency to hard-edged cut-outs: `0x06` is the alpha-blended group;
  - the ground's `0x81` to 0: the grass shows through the infield dirt: `0x81` is the background layer, drawn before the rest. Within it depth does not count, order does: in the Polo Grounds from scratch, the infield dirt at height 0 did not show because the grass plane 4 cm below was drawn after it; drawing the layers from the bottom up shows it (October 19, 2026);
  - `0x06` on a stand: the ads on its fence turn semi-transparent: the group affects the whole piece.
- **High bits:** without `0x80000000` (`NOUPCAM`), from a high camera outside the stadium the surrounding buildings and trees the original hides show up: it hides the piece when the camera is up. It also hides it from the batter's view (checked in phase 3, in the viewer and in a match). Removing `0x40000000` or `0x20000000` changed nothing from the views tested (batter, aerial, stands); they depend on other match cameras.
- **`unk_20`:** emptying it changes nothing by day. Its points lie inside the boxes of the `lumine` pieces, 46-57 m up and turned towards the field, one group per tower (Coors: 32, 50, 50 and 25): almost certainly the floodlights, one by one, for night games. Not checked in a night game.

### Stadium from scratch in the game

Phase 3, October 19, 2026; `tools/blender/powerpros_stadium/scratch.py`. A test collection with one object per role, written with no base stadium and opened in the viewer and in a match:
- **Flags (type `0x05`):** every flag in the game is the same 10-vertex plane (5 columns × 2 rows, pole at x = 0, cloth towards +x, 32.64 × 23.78 units before scaling), one piece per flag, with rotation 0: the flag system turns it with the wind. The four test flags joined in one 16-vertex piece made the system write out of memory (6 accesses per piece; `lr` `0x8005D5A0`, `0x8005BA60`, `0x8005D364`) and the stadium never got ready. One piece per flag loads fine, with the game's plane or a 4-vertex rectangle; with the plane the cloth folds.
- **Crowd (types `0x16`/`0x17`):** the game animates it through the **preloaded textures**. The preload list (`textures` and `unk_2c` of the table) always has 8 entries with a fixed role, in the 37 stadiums with a preload: 0 to 2 scoreboard (C4), 3 video board (the 8 × 8 RGB5A3 «slot»), 4 and 5 upright crowd, 6 and 7 lying crowd (C8 of 128 × 64, or 8 × 8 in two stadiums, always declared 128 × 64 in the texmap). Crowd pieces that use those textures show the game's people, changing pose every few seconds, whatever the texture's image (with our own textures in those entries the same people show). Without the preload, Coors Field's crowd stops animating; a crowd piece with a texture that is not preloaded is drawn still with its image.
- **`0x80C5DA4C`:** walks the placements and, by type (`0x13`/`0x17`, `0x14`/`0x16`, `0x15`), writes three different values to a per-piece visibility table (scene `+1720`); the piece with `0x40000000` uses another (scene `+1973`). It looks like the crowd amount by attendance.
- **Lights (type `0x08`, `lumine`):** with `0x000A0800` (as the game's `ADD_lumine` pieces) a test glow is not drawn by day; with `0x000A0000` it is drawn adding light (247 over a sky of 98). Type `0x08` is what lights up at night; not checked at night.
- **`NOUPCAM`:** a test building with `0x80000000` does not show even from the batter's view, in the viewer and in the match; without the bit, it does.
- **Unexplained:** with a free viewer camera right in front of the test building, 150 m from home plate, it is not drawn even without `NOUPCAM`; moved 150 m towards the field, it is.

### Game stadium list

October 14, 2026, from the REL data; for adding stadiums.
- **File numbers:** the game does not store file names. It requests `project.bin` entries by record number: Coors Field = 75, its table = 76. There are 4,681 records, of which 2,424 have a name: 2,256 remain free for new files.
- **Stadium table:** `ovl_main.rel` `+0x92A450` (in memory `0x80E9F9D0`, section 5). It has 40 entries of 12 bytes: {u32 graphics pack number, u32 `tbl` number, pointer to the `tbl` root (`0x40000000 | position`)}.
  - The game does not search for the `tbl` root: it reads it from here. A rebuilt `tbl` must leave it in the same place or update this table.
  - **Order:** that of the MLB teams by division: BAL, BOS, NYY, TB, TOR, CWS, CLE, DET, KC, MIN, LAA, OAK, SEA, TEX, ATL, FLA, NYM, PHI, WSH, CHC, CIN, HOU, MIL, PIT, STL, ARI, COL, LAD, SD, SF. Then come `org01`–`org06`, `ml01`, `bullpen`, `bullpen2` and `season`.
  - Right after it there is a vtable: the table cannot be extended in place.
- **`0x80C7AF48`** (`GetStadiumFiles`, our own name): copies `table[(number − 1) × 12]` to its first argument. The stadium number goes from 1 to 40 and is not checked. It is the only reference to the table in the code.
- **There are two stadium selection screens:**
  - **Exhibition:** constructor at `0x80CD29xx`, without the generic list widget.
    - Number list at `+0x296C`, entry count at `+0x2968` and cursor at `+0x2938`, with room for 38 entries. Fields in use come after it.
    - The list is built with the 30 MLB ones (via `0x807A8BE8(team)`) plus `org01`–`org06` and `ml01` if they are unlocked (`0x805C719C(0x2E..0x34)`) or are the home stadium.
    - The cursor moves inside the update function: +1 or −1, wrapping around with the entry count.
    - `0x80CD3F6C` (`ShowStadium`) shows the entry after each move:
      - requests the stadium's menu pack once (`0x80EA23D8[number]`, flag at `+0x2941 + number`);
      - when it is loaded it draws it with `0x807A8EBC` on the two view objects at `+0x2A18` (mode 1) and `+0x2A1C` (mode 2);
      - stores the shown stadium at `+0x293C`.
  - **The other screen** (vtable `0x80D3DE58`, constructor `0x805C6DC4`, list at `+0x5250` and list widget at `+0x5538`) is not the Exhibition one: it is not used when choosing a stadium in Exhibition (checked with a log). It is probably the Arrange or Season one.
- **`0x807A8EBC(view, mode, stadium)`:** draws the textures of the stadium's menu pack.
  - It uses the table `0x80D99E38` (28 bytes per stadium: menu pack and pointers to its textures).
  - It caches the last (mode, stadium) at `+0xDC`/`+0xE0`.
- **Stadium menu images:** `ovl_main.rel` `+0x7C8814` (`0x80D3DD94`) stores 37 file numbers `env_stadium_menu01`…`36` and `100`. They are used by functions from `0x805C6DDC` to `0x805C8660` (stadium selection screen), with the base at `0x80D3DD90`.
- **`env_order_stadiumNN`:** `ovl_main.rel` `+0x7DCD0C` (`0x80D5228C`), 20-byte entries {file number, four pointers into the pack}. Used by `0x80672F20` to `0x8067312C`.
- **New stadiums** (done on October 14, 2026; patch `wiicompiled-new-stadiums`, `runtime/src/hle/mlb/new_stadiums.cpp`):
  - A mod declares `[[stadium]]` in `mod.toml` and ships `project/stadium_<code>.pack` and `stadium_<code>tbl.pack`, and optionally `env_stadium_menu_<code>.pack`.
  - The runtime gives those files free numbers from 4388 (the game's highest is 4387) and adds them to the last bucket of the modded `project.bin`.
  - **`ShowStadium` replaced:**
    - it puts the new stadiums in the free slots of the list, as a scrolling window (going past the last slot shows the next one), so there is no limit;
    - the slot carries the number of the base stadium, so dimensions, saves and the rest of the game see a stadium they know;
    - it shows the new stadium's menu pack by briefly changing the `0x80D99E38` entry.
  - **`GetStadiumFiles` replaced:** it returns the new stadium's files when it was the one chosen and the match is an Exhibition one.
  - **Verified:**
    - in the Exhibition carousel, left from Fenway shows the new entry with its image;
    - when confirming it, the match loads "Coors Field 2" from its files 4389/4390;
    - with two stadium mods at once, the benchmark loads either of them (`WIICOMPILED_MATCH_BENCH_STADIUM=<code>`).
  - **Name banner** (done on October 14, 2026, `runtime/src/stadium_banner.cpp`):
    - the banner does not come from the stadium's menu pack, but from the common pack `env_stadium_menu.pack` (file `0x8E2`), through the tables `0x80EA2490` (texture) and `0x80EA2528` (palette);
    - the screen stores it at `+0x2A24` (texture) and `+0x2A28` (palette) on the frame of the move, and the sprite reads those slots;
    - the drawing table `0x80D99E38` has, per stadium: the menu pack, the banner (common `+4`, 512 × 64) with its palette (common `+8`), the photo (512 × 256) with its palette and "Home of…" (512 × 64) with its palette. These last two are in the stadium's pack;
    - resolvers: `0x8000C330(id, pointer)` gives the texture and `0x8000C3E0` the palette;
    - format of the game's banner:
      - header {u16 height 64, u16 width 512, u32 format 9 (C8), → pixels, 0, 0, 1, 1}, with the pixels at `+0x40` and the palette colors after them;
      - palette header: {256, 0, `0x81`, 0, `0x6000`, →};
      - the text takes rows 4 to 37;
    - a new stadium uses `project/env_stadium_name_<code>.pack` if the mod ships it. If not, the runtime draws one with a system font (never with game data) and stores it in the cache, `mods/banners/`;
    - on every frame, the screen writes that banner to `+0x2A24`/`+0x2A28` while the new stadium is in view;
    - **verified:** "Coors Field 2" and "Coors Field 3" show their own banner (first version of the drawing). The current version, closer to the game's style, has only been seen decoded.
  - **Pending:**
    - the "Home of the…" line is still the base stadium's;
    - the other stadium selection screen;
    - Season, League and MLB Life choose the stadium by team.
- **What was needed (done above except what is pending):**
  - being able to add new files to `project.bin`;
  - replacing `GetStadiumFiles` and the functions that read the other two tables with native versions with extra entries;
  - finding the number of stadiums in the selection screen (36 plus "random");
  - checking whether there are more per-stadium tables and whether saves store the stadium number.

### Pending

- the placement flags (`0x500`, `0x1700`, `0x40000000`…);
- what selects the additive blending of the `*_ADD_*` pieces;
- the texmap and material bytes that do not vary;
- what the game uses tree A for (which pieces are drawn?) and what `0x80CCB438` decides;
- the in-game effect of each surface ID. Hypothesis: 1 marks the home run, 0 the foul into the stands and 2 the outer boundary. It will be checked with the Polo Grounds.

### Tools

- `tools/stadium_re.py` (`info`, `check`, `stats`);
- the Blender add-on `tools/blender/powerpros_stadium` (`docs/en/stadiums.md`).

## 7. Roster: `game_data.pack` and player format

- **`/game_data.pack`** (inside `project.bin`, 278,176 bytes): `PACKWII ` + `0x20` header + **`0x43E80`-byte roster block**.
  - It is the same block that MLB Power Pros 2007 stores in its save (`pm2maus.dat`, offset `0x68C74`).
  - **All modes read it at boot**, MLB Life included. In 2008 the base roster is not in the save data.
  - What is changed in Edit mode does not reach MLB Life. Observed: the game leaves players marked as edited out of MLB Life.

**Roster block:**

| Offset | Contents |
|---|---|
| `0x00000` | 1,500 players × `0xB0` (2008 uses 1,007) |
| `0x40740` | 32 lineups × `0x48`: 18 entries of 4 bytes (u16 player; high nibble of byte 2 = position) |
| `0x41040` | 200 free agents × 8 bytes (2008 uses 25) |
| `0x41680` | 32 teams × `0x140` (30 franchises + AL and NL All-Star): 40 entries × 8 bytes |

- Empty entries and slots without a player: they carry a fixed "empty" record, not zeros.
- Free agents: empty = `00000000000f1400`; used = id + `000000001400`.
- The All-Stars point to the same record as the normal team.
- The player number decides their slot and their salary.

**Player record (`0xB0` bytes).** Bits are counted from the most significant one at the start. There are two notations: `0x50.3` is byte `0x50`, bit 3; `0x283` without a dot is the bit number (the same place: `0x283` = `0x50 × 8 + 3`). "`/ 9`" gives the width in bits.

| Field | Position |
|---|---|
| Player ID | u16 at `0x00` (0 = empty slot) |
| Special name ID | u16 at `0x02`; `≥ 0xF00` = long name, and the game does not read `0x04`-`0x15` |
| Last name / first name | `0x16` and `0x32`, 14 u16 characters each (2007 character table) |
| Needed for the lineup | bits `0x4E.1`, `0x4E.2`, `0x4F.0`, `0x92.0` = 1 in every 2008 player. Without them, the lineup screen comes out empty and everyone as "P" |
| Not edited / edited | bits `0x50.3` = 1 and `0x50.4` = 0 in the original players |
| Face | bit `0x2A4`, 8 bits |
| Skin and eyes | bit `0x2AC`, 4 bits |
| Batting / pitching form | `0x2E4` / 9 and `0x2ED` / 9 (they were 8 in 2007) |
| Main position, P/C capability | `0x2F6` / 4, `0x2FA` / 3, `0x2FD` / 3 |
| Pitcher roles (starter, reliever, closer) | `0x62.5`, `0x63.0`, `0x63.3`, 3 bits each |
| Date of birth | year `0x414` / 11 (2008 = 2007 year + 1), month `0x420` / 4, day `0x424` / 5 |
| Years in the majors, AVG, RBI, HR | `0x42A` / 5, `0x42F` / 10, `0x440` / 10, `0x44A` / 10 |
| ERA | `0x460` / 14 |
| Voice | bit `0x492`, 14 bits (same position as 2007, new numbering; see "Renumbered voices") |
| Repertoire | `0x4A0`-`0x4FF`: 12 pitches of 5 type bits + 3 movement bits |

**Changes from 2007:**
- Bytes `0x00`-`0x5B` and `0x60`-`0x6E` are the same, except for the face.
- The second "vs. lefties" field (`0x6F.5`) is gone: the 2007 bits from `0x380` to `0x3BE` move 3 positions back.
- 2008 adds a bit at `0x77.4`: the bits from `0x3BF` to `0x41A` move 2 back.
- The −3/−2 boundary is at `0x77.7` (pull hitter).
- Abilities confirmed with real players:
  - "push hitter" `0x71.0` (Ichiro, Mauer, Helton…)
  - "free swinger" `0x71.7` (Soriano, Guillén…)
  - "pull hitter" `0x77.7` (42 players: Giambi, A-Rod…)
- **Renumbered faces:** +0 / +1 / +2 / +7 / +12 for 2007 faces below 28 / 106 / 161 / 177 / the rest. 707 of 729 shared players match.
- **Skin and eyes:** 2008 inserts a tone at position 2, and brown eyes add 6 (in 2007, 5).
- **New long names** (special IDs): Van Benschoten 3867, Pignatiello 3893, E. Patterson 3895, Saltalamacchia 3896, Sonnanstine 3897, Chamberlain 3899, S. Rodriguez 3900, Lillibridge 3901.
- The full table of about 140 fields is in `kRosterFields` (`wiicompiled-mods.patch`) and in `PowerPros2008Fields.cs` (patched PowerUp+). The 127 bits that PowerUp does not name (including the hidden trait) are in `kRosterUnnamedRuns`, in runs.

**Record getters in the code (disassembly).** The game reads each field with a small function: `lwz rX, D(r3)` + field extraction + `blr`, one after another from `0x8082B000` (`ovl_main.rel` + `0x2B5A80`) in record order. It is the field list as the game sees it, with position, width and sign:
- The following match the table above: batting and pitching form (9 bits), clutch `0x6F.5`/3, mood `0x70.1`/2, all batting abilities from `0x70.3` to `0x73.0` (push hitter `0x71.0`, free swinger `0x71.7`…), the fielding ones and the pitcher ones up to `0x80.7`, and the birth date.
- **"Table setter" `0x70.0` is still alive in 2008:** getter `0x8082B068` (`srwi 31`), Edit mode setter at `0x8085EF74` (clamps to 0–1) and two gameplay functions that read it together with "rally hitter": `0x80985E4C` and `0x80A30C54`. No 2008 player has it, but it works if one is given it.
- Some fields that PowerUp treats as a single bit are **2 bits** for the game. The other bit travels as an "unnamed bit", so the converter keeps the full value:
  - `0x73.5`/2 (head-first slide): only 0 and 2 appear; the bit PowerUp models is the right one.
  - `0x75.4`/2 signed ("cannon arm" for PowerUp): +1 in 96 players (Carpenter, Randy Johnson, Dye…) and −1 in 11 (Furcal, Rollins, Eckstein…). It does not appear on the player card: it is a hidden trait.
  - `0x77.0`/2 and `0x77.2`/2 signed (aggressive runner and fielder): only 0 and +1.
  - `0x78.4`/2 signed: **new 2008 ability, night game** (Gd/Pr Night Game). +1 in 11 (Figgins, Cora, Easley…), −1 in 16 (Giambi, Kendall, Dellucci…).

**Verified on screen** (Arrange → a team → Fielder Select → C stick, "Player ABL"; screenshots of Cora, Varitek, Casey, J.D. Drew, Eckstein and Rios cross-checked with their bits):

| 2008 bits | What the game shows | In PowerUp |
|---|---|---|
| `0x73.5`/2 | HD 1st Slide (value 2) | WillSlideHeadFirst, correct |
| `0x73.7` | Tough Runner | IsToughRunner, correct |
| `0x74.4` | **Cannon Arm** (in MLB Life messages, "Laser Beam") | calls it CanBarehandCatch (fixed for 2008) |
| `0x75.2` | Gd Block | IsGoodBlocker, correct |
| `0x75.4`/2 signed | **nothing on any tab**: hidden trait. +1 in power pitchers and big hitters, −1 in small infielders; it could be the build (unconfirmed) | **wrong:** calls it HasCannonArm |
| `0x76.2`/2 | Slugger (+1) / Slap Hitter (−1) | SlugOrSlap, correct |
| `0x76.4`/2 | Aggressive (+1) / Patient Hitter (−1) | correct |
| `0x76.6`/2 | Aggressive (+1) / Cautious Stealer (−1) | correct |
| `0x77.0`/2 | Aggressive Runner (+1) | IsAggressiveBaserunner, correct |
| `0x77.2`/2 | Aggressive Fielder (+1) | IsAggressiveFielder, correct |
| `0x77.4` | **Gd Eye**, new 2008 bit | does not exist |
| `0x77.5` | **Barehand Catch** | unnamed (Bit_77_7); in 2008 CanBarehandCatch gets it |
| `0x78.0`/2, `0x78.2`/2 | Gd/Pr Day Game, Gd/Pr Rain | correct |
| `0x78.4`/2 | **Gd (+1) / Pr (−1) Night Game**, new in 2008 | does not exist |

- The same players have these bits in 2007, so in 2007 PowerUp also gets the names wrong.
- **For 2008 it is fixed by meaning, and 2007 is left as it is.** The rosters PowerUp generates already use the slots by their name (its 2025 roster marks "Cannon arm" on Tatis, Arenado, Salvador Pérez and Betts). That is why the 2008 table (`MOVED` in `powerup_to_2008.py`) takes "Barehand catch" to `0x77.5` and "Cannon arm" to `0x74.4`. The hidden trait is kept from the 2008 record, or is 0 for a new player, and the leftovers of the 2007 base save are no longer copied.
- Result with the 2025 roster: before, 0 cannon arms, 117 random barehand catches and 100 garbage hidden traits; now, the 4 real cannon arms and nothing else.
- In PowerUp+, a 2008 roster imported again shows each slot with its real ability (Rios: Cannon Arm; Eckstein: Barehand). The round trip is still byte-for-byte identical. 2008 rosters imported before this change have to be imported again.
- **Gd Eye and Night Game can be edited in PowerUp+** (General section, marked "(2008)"). A 2008 import reads them from the original record.
  - The 2008 export writes them into two slots of the 2007 format that the conversion no longer uses (`MOD_2008`: `0x77.7` → `0x77.4` and `0x78.6`/2 → `0x78.4`/2).
  - The game's converter only takes them from a mod with `roster_ids = "2008"`. A generic 2007 `.dat` is converted the same as before.
  - The 2007 export leaves them empty, so a 2007 save is written bit for bit as before.
- Verified: Payton (Pr Night Game) changed in the editor to Gd Night Game and Gd Eye, saved, exported and converted. In the game roster exactly those bits change; the runtime gives the same as Python.
- When copying a player (needed to edit an imported one), PowerUp did not copy Day Game, Rain, the date of birth, the age, the years in the majors or the stats. Without a date, the export failed. They are now copied.
- The card opens in the game from Arrange → create team → Fielder Select (or Pitcher Select) → C stick. To automate it, `WIICOMPILED_PAD_SCRIPT` accepts bits `0x10000`–`0x80000` as the C stick (patch `wiicompiled-pad-script-cstick`).

**Renumbered voices.** 2008 keeps the order of the 2007 voices, but inserts and removes some. Between two players present in both games whose gaps are the same size, the voices map one to one, unless a known 2008 name contradicts it. This places 810 of PowerUp's 968 voices, in 206 runs (`VOICE_RUNS`). Hiding 10% of the reference players, all 739 cases land on their real voice. Ryan Braun and two "Great" players have their voice re-recorded somewhere else.

Sources: `docs/en/roster-2008.md`, `tools/roster/powerup_to_2008.py`, `tools/roster/align_2007_2008.py --voices`, `wiicompiled-mods.patch`, `wiicompiled-roster-flags.patch`, `wiicompiled-roster-voices.patch`, disassembly of `ovl_main.rel`.

## 8. Scenes, menus and setting up a match

**Scene manager** (pointer at `0x81033268`; object with vtable `0x80D4EB88`):

| Offset | Field |
|---|---|
| `+0x400` | current scene object (its phase at `+0x38`, a control field at `+0x48`) |
| `+0x404` | next scene ID |
| `+0x408` | current scene ID |
| `+0x410` | u8: transition in progress |
| `+0x413` | u8: confirm the change |

**Known scene IDs:**

| ID | Scene |
|---|---|
| 1, 2 | boot and strap screens (phases 5 and 6 wait for a button press) |
| 3 | opening video (`+0x48 = 3` closes it cleanly) |
| 4 | title screen ("Press +"; with no input it starts the CPU vs. CPU demo) |
| 409 | the field: match in play |
| 419 | pre-boot scene (it appears in the logs before 1) |

**Setting up a match from code** (as the benchmark does: Yankees against Colorado at Coors Field, CPU vs. CPU):
1. Write `0x4D4C4208` ("MLB\x08") to `r13-32744`.
2. Call `0x80C8BAF0`, the game's match initializer.
3. `config = *(0x8104E1D8)`, `common = *(0x8103D118)`.
4. Teams: `config+19 = 3` (Yankees), `config+20 = 27` (Rockies).
5. For each side (0, 1): write the team ID to `common+1752+side·2204` and `common+1753+side·2204`, and call `0x80973828(side, team)`. Each side takes 2,204 (`0x89C`) bytes.
6. `0x807A8BE8(27)` returns the team's stadium (Coors Field); it is written to `config+31` and `common+4`.
7. `config+18 = 0`, `config+13 = 0` (Exhibition), `config+14 = 0` (no human controller), `config+26 = 3`, `config+27 = 3`, `common+976 = 0` (u16), `common+978 = 0` (u8).
8. Next scene = 409 and `+0x413 = 1`.

**Code points tied to menus:**
- **Home Run (Practice):** `0x8057BEEC`, 10-entry jump table bounded with `cmplwi r3, 9`.
- **Fielding view:** `0x8092F450`, 9-entry table indexed by a byte, with no prior comparison.
- **Exhibition → LINEUP:** `0x80663264` calls `0x80664C30` through `bctrl`.
  - At `0x80664C18` there are five consecutive functions that dtk took as a single one. `C20`, `C28` and `C30` are two-instruction getters called only from the vtable `0x80D5183C`.
  - About 450 similar candidates remain unreviewed.
- **Controllers per mode:** Exhibition, Home Run and other modes ask for a Nunchuk or Classic Controller. Without a Nunchuk, "Power Pros via Wii Remote" is offered.

**Season mode, walked with the controller** (October 19, 2026; scripted GameCube controller, `WIICOMPILED_PAD_SCRIPT`):
- **Boot to the main menu:** strap warning (two A) → «Loading data» → Konami, 2K Sports, licences and Power Pros Production logos (skipped with Start) → title around 32 s → controllers screen («You can use the following Controllers»), which waits for a press → main menu.
- **Main menu:** 12 options in 3 rows: Exhibition, Success, Home Run, Practice / MLB Life, Season, League, Arrange / Wii Remote, Shop, Baseball Cards, My Data. The cursor starts on Exhibition. Switching controller shows «1P Nintendo GameCube Controller detected».
- **Season mode screens** (entering, new season, menu, MLB Data): in [Season mode › Screens and menus](#screens-and-menus).
- **For scripts:** timings vary between boots (the intro talk started between 84 and 160 s), so a fixed-time script drifts; better press L repeatedly until the talk is over.

Sources: `wiicompiled-match-benchmark.patch` (`match_benchmark.cpp`), `docs/en/wiicompiled-feasibility.md`.

## 9. Season mode

Everything found about Season mode: date and year, the game schedule, the year rollover and number of seasons, per-year tables and the save file. Investigated on October 19 and 20, 2026 with the translated code, the runtime probe `wiicompiled-season-probe` (sets the date and year, presses buttons and dumps memory) and test saves in a separate mod.

- **Notation:** P = `[0x8104B5AC]`, X = `[0x8104B5A0]`, Q = `[0x8104B59C]` + 1344. They are pointers in the REL BSS to the mode's data block; X + n is a position inside that block.
- **Tools:** `tools/season_save.py` (compares saves and dumps), `tools/season_cap_sites.py` (finds the comparisons and additions with the year in the code).

### Screens and menus

- **Entering Season:** a black screen for a few seconds and a help window («Starting Play», closed with B). On entering, the game tries to open `pm3usse0.dat`, `pm3usse1.dat` and `pm3usse2.dat` in the save folder: in a new save they do not exist. They look like three Season mode save slots, apart from `pm3usma.dat`.
- **Screens of a new season:** From Beginning / Continue → Quick Start / Start → team select (30 teams, with 2007 stats and salary) → options (Game Setup, Controller Setup, Season Settings) → a warning that improvements, free agents, draft and trades can only be changed at certain points of the off-season → intro talk with the secretary (A advances, L skips, R talk history). It starts on Monday March 24, 2008.
- **Season mode menu:** on the left, seven buttons: Daily Progress, Practice, Management, Commands, MLB Data, System, Exit; on the right, the division standings. At the top, the team, the date, players in MLB and AAA, owner points and total salary. At the bottom, the help bar: Start (help), Y (schedule), R (history), Z (switch info).
- **MLB Data:** tabs «◄ 2008 ►» (the year, changed with L/R), All-Time/Career and Honors. Within a year: Player Stats (Season Stats, Player Records), Team Stats (Team Stats, Team Records), Schedule (Standings, Matchup Chart, Postseason) and Roster (Player List, Transactions).

### State, date and year

- **State:** `[0x8104B5A8]` points to the season state. +4 holds today (u16, day of the year from January 1: 0x54 = 3/25), +6 the previous day and +8 the **year index** (signed byte; year = 2008 + index). `0x80B74BCC` reads the index, `0x80B4A888` returns the year, `0x80B74BA4` sets it and `0x80B74BB4` adds 1.
- **Season mode or MLB Life:** both point `[0x8104B5A8]` and `[0x8104B59C]` at their own block. The first player id tells them apart, the u16 at `[0x8104B608]`: 7001 in Season mode (`0x80C48344`), 9001 in MLB Life (`0x80A556E0`). `0x80C0C50C` also reads the number of seasons (through `0x80BA0878`, into `[0x8104BA2C]`) when the mode starts, before the 7001 is written.
- **Year spans:** day ranges at `0x80D2A700` (8 bytes each: first and last day of the year). Ranges 17 to 28 are the months; `0x80B4AA38` returns today's month. Season ranges: regular season 3/19–10/01, postseason 10/02–10/31, off-season 11/01–2/14. Event days at `0x80D2A7F0` (u32 per event; event 57 is 11/01).
- **Day of the week:** `0x80B4DADC` computes it with a formula from year, month and day, so it also works for years after 2017.
- **The year on screen and the calendar:** `0x80B4A888` (2008 + index) is always called (53 sites). `0x80B4A730` (a date's year, at least 2008), `0x8087B668`, `0x8087E224` and `0x80ADC620` (2008 + the year picked in a selector) add 2008 or 2007 to the index without it. The calendar uses the 2-digit year, index + 8 (`0x80B74C00`), which the translator copied into 32 functions; ten of them (`0x80B4AEE4` to `0x80B4CB84`) clamp it to 8..28 before the day-of-week formula (`li 28` and `cmpwi 28`), and the clamp only guards the formula. A function replaced in the runtime is no longer copied into others (the translator does not inline what has a native replacement).
- **Event dates and the calendar's year** (October 20, 2026): the dates of the season's events (`0x80BAFB8C`, `0x80BAFCC4`, `0x80BAFE40`, `0x80BAFFE8`, and `0x80A38494`) use the season of a date (`0x80B4A7DC`) + 8, not `0x80B74C00`: 13 more year sites (`tools/season_cap_sites.py --years` finds them: `addi r3, 8/2007/2008` right after a call that returns the index). The calendar's title (Y) is a picture: `0x80B9D450` (screen, index, month) sets the year's from a table of 20 (`0x80E8EB40`, 2008–2027) and the month's (`0x80E8EB90`) through `0x8001DFB8` (pane, 1696 = `season_schedule.pack`, picture). Each picture is a `SURF` of `0x48` bytes in the pack, 2007 to 2028 in a row, with its 86 × 37 rectangle in a 512 × 512 C8 atlas. The pack is loaded into MEM2 when the calendar opens. With archived years a native sets the real year's picture up to 2028 and, past 2028, repaints 2007's (which Season mode never shows) with the year made of the game's pictures by cells: "20" from any year, the units digit from 2010 + d, and the same digit one cell (19 columns) to the left for the tens. Tested: April and May 2029, March 2030, and March 2008 with 10 seasons unchanged. Found with a read watchpoint on the index while the calendar opened (the only new read: `0x80B9C034`). The year tables at `0x80D29D18` (1998–2028) and `0x80E65DC4` are not the title.

### Game schedule

- **Game schedule** (October 20, 2026; translated code, memory dumps and saves): a table of 245 days × 16 slots of 16 bytes at X + 10192 (62,720 bytes), stored in the save (file 0x2B8C8). Day 0 is March 1 (day of year 60). `0x80BA0444` (base, day of year, slot) gives the slot: base + 10192 + (day − 60) × 256 + slot × 16, or 0 outside the 245 days. Each game: u32 home, u32 away, u32 start time in Eastern time (hours × 400 + minutes × 4: 13:05 = 0x1464; once played it holds other data), u32 flags (`0x8000` = night game: set on every game starting at 18:00 or later and on none before; in 2008 the rest is `0x00A06700`). Empty slot: teams 44 and 44. The 2008 All-Star Game (7/15, day 136) has teams 33 and 37.
  - **Team numbers** (by division, in 2007 final standings order; found by comparing each team's game days with the real 2008 schedule, 0.98 to 1.0 match for all 30): AL East 0 BOS, 1 NYY, 2 TOR, 3 BAL, 4 TB; AL Central 5 CLE, 6 DET, 7 MIN, 8 CWS, 9 KC; AL West 10 LAA, 11 SEA, 12 OAK, 13 TEX; NL East 14 PHI, 15 NYM, 16 ATL, 17 WSH, 18 FLA; NL Central 19 CHC, 20 MIL, 21 STL, 22 HOU, 23 CIN, 24 PIT; NL West 25 ARI, 26 COL, 27 SD, 28 LAD, 29 SF.
  - **2008 is the official schedule:** 2,431 games (162 per team plus the All-Star Game): the schedule published before the season, without the changes from postponements (212 of the 245 days have the same number of games as the schedule that was played; the rest differ by postponed games). It starts with Athletics–Red Sox in Tokyo on 3/25 and 3/26 (12 and 0; Oakland was the designated home team) and Nationals–Braves on 3/30 (17 and 16).
  - **Later years are generated by the game:** on the step to 1/1, `0x80B4E564` calls `0x80B97D68`, which empties all 245 × 16 slots (`0x80BA0DAC`). `0x80B97DC4` builds the schedule: it empties it, checks `0x80B97E08` and, when it returns 1, calls `0x80B97EA4` (loads the official schedule from REL data at `0x80E8E8B8`, which is not stored raw), and when it returns 0, `0x80B98320` (the generator, which uses the days of the week); then it calls `0x80B98294`, `0x80B73E2C` and `0x80B9890C`. `0x80B97E08` returns 1 only with year index 0 and all six divisions equal to the table at `0x80D29060`: the official schedule only applies to 2008 with the original divisions (a new 2008 game loads the official one, confirmed with the log of the patch below). Generated schedules have 2,491 games (166 per team; about 60 carry other flags, `0x00A40000` and similar) and change every year (2009 and 2027 share only 7 pairings). Skipping the generation day with the date leaves the year without a schedule (test saves from 2017 to 2020 with 0 games).
- **March exhibition games:** they are not in this table. The calendar (Y) marks March 1, 4, 13 and 18, 2008 as «EXHIBITION» although the table is empty on those days; they come from the season's events.
- **Schedule from a mod** (October 20, 2026; patch `wiicompiled-season-schedule`, `runtime/src/hle/mlb/season_schedule.cpp`, format in `docs/en/mods.md`): a native replacement of `0x80B97DC4` makes the same calls and, between the fill and `0x80B98294`, replaces the games with those of `schedule/<year>.csv` from the first enabled mod that has it. It keeps the game's All-Star Game on its day and the empty slots as the game left them. The new games carry 2008's flags (`0x00A06700`, plus `0x8000` from 18:00).
  - **Verified in-game:** a new 2008 season with the 2026 schedule as `2008.csv` (`tools/season_schedule.py`). On 3/25, Giants–Yankees is played at AT&T Park (2026's opening game), that day's «Game Results» has only that game, and in the March calendar the Yankees play on the 27th and 28th in San Francisco and on the 30th and 31st in Seattle, as in 2026. Log: `[season] schedule 2008: 2429 games from mod "…" replace the official one (1 dropped: more than 16 on a day)`; the one that does not fit is from 9/25/2026, which has 17 games.
  - **The tool reproduces the game's schedule:** with MLB's API response for 2008, `tools/season_schedule.py` gives 2,429 of the official table's 2,430 games on the same day (one moved by a day in the published version; the CSV also has the White Sox–Twins tiebreaker) and the same time for 2,249.

### Year rollover and number of seasons

- **Year rollover:** `0x80B4E564` runs when a day passes; when the new day falls in January it adds 1 to the index and sets up the new season (`0x80B962E8`, `0x80B71B1C`, `0x80B97D68`). It can be triggered without playing: from the off-season menu, day = 12/25 and «Daily Progress» goes to 1/1 of the next year («These are last year's financials»). Checked from 2008 to 2009 and from 2009 to 2010.
- **The 10-season cap is a variable:** `[0x8104BA2C]` = 10. It is read by `0x80B40580`, `0x80B4065C` (seasons left, on screen), `0x80B4DB78`, `0x80B50948` and `0x80B5C4D4`, which compare the index with 10 − 1. There are also fixed compares with 9 or 10: `0x80BB4A40` (at 9, message 141), `0x80BF24B4`, two in `0x80BF4BD4` (message 140), `0x80C0CB54`, `0x80BECF40` (year list) and `0x80C0D4F8` (10-year loop of the finances).
- **The fixed compares, instruction by instruction** (`tools/season_cap_sites.py`, which looks for `bl 0x80B74BCC` followed by a `cmpwi` with 9 or 10 in the loaded code): `0x80BB4A6C` (index == 9, message 141), `0x80BF2530` (index < 9, calls `0x80BF619C`), `0x80BF51BC` and `0x80BF52D0` (off-season events when index < 9), `0x80C0CC0C` (index < 10). There are also two 10-year loops on a counter: `0x80BECF8C` walks a UI list with 10 slots (`obj + 11184`, 4 bytes each; it cannot simply grow) and `0x80C0D584` initialises the 10 finance years.
- **End of season 10** (tested with the index set to 9 and the date to 12/25): moving to 1/1 takes the index to 10 (2018 on screen), then the farewell («Take care, now»), the summary of standings, awards and postseason (empty, since it reads year slot 10), the credits, «Save as Arrange Team», and a save. Loading that save replays the ending. It does not come earlier with 10 or 20: 11/01 of 2017 (index 9) only brings the awards and retirements, and the ending comes with the move to 1/1 (tested).
- **Finished save:** bit 0x20 of `[state + 0x104]` marks that the mode ended; a save with it replays the ending on load (tested by setting it in a 2019 save). `0x80C0D438` reads it, but the load-time check goes another way. The save has a checksum: changing that byte in the file makes it «corrupted».
- **Up to 20 seasons** (patch `wiicompiled-season-cap`, launcher option): `0x80C06534` (returns 10) gives the number of seasons, which feeds `[0x8104BA2C]`; the five fixed compares go through the runtime (the translator's `translation.immediate_overrides`); the per-year finance functions (`0x80C0D614`, `0x80C0D658`, `0x80C0D6D0`, `0x80C0D758`, `0x80C0D818`, `0x80C0D85C`) are replaced and keep years 11 to 20 apart; the finished flag of an earlier ending is cleared in memory once, right after a Season save is loaded. The number of seasons is fixed when a game starts or loads (the option, or a save already played with 20: it carries our tail or is past index 10), never from the live index, which passes 10 on its way into the original ending. With 10 the finances always go to the original addresses. Tested: 2016 → 2017 → 2018 without the ending, menu and weekdays of 2018 and 2019, ending after season 20, the original's finished save continued in 2018, and with 10 everything as before.
- **No season limit** (option "No limit", `[season] max_seasons = 0`): the game keeps its 20 years of tables. At the 1/1 rollover after season 20, `0x80B74BB4` (index + 1, called by `0x80B4E564`) does not add: every per-year table moves down one year, the oldest year goes to the save's tail (about 34 fit; then the oldest is dropped) and a year offset, also in the tail, makes `0x80B4A888`, `0x80B74C00` and the year sites give the real year. Weekdays follow the real year (tested: Saturday 1/1/2028, Monday 1/1/2029, Saturday 1/8/2028). Each table's new slot is left as an unplayed year: qualifiers at 44, series without teams or wins, the standings' row 20 (never played), the year's team that left, and finances and records at zero. "Annual Results" shows the last 10 years; title pictures exist only for 2008–2017 and other years have none.
- **One season simulated with "No limit"** (2029, from the 2027 save + 2): exhibition games by Fast Game (pause > FAST GAME > Z, about 12 s a game), the regular season by the calendar (Y, pick the month's last day, "Skip to the end of …?": a month in about 20 s), the postseason and World Series by Fast Game. Right: weekdays, weekly MVPs, All-Star voting, the draft, the final standings (162 games), the postseason bracket, the 1/1/2030 report (income 114,461 P, balance +37,281 P, no tax), 2029's "Annual Results" after the window moved (2010 archived), and save and load in 2030 (offset 3, 3 archived years).

- **A season pack's start year** (October 20, 2026; patch `wiicompiled-season-start-year`, format in `docs/en/mods.md`, "Season packs"): `start_year` in an enabled mod's `mod.toml`. When the schedule is built at index 0 (a new game), the replacement of `0x80B97DC4` sets a year shift of `start_year` − 2008, added to the "No limit" offset everywhere the year is shown (`0x80B4A888`, `0x80B74C00`, the year sites and the calendar pictures); the luxury tax threshold follows the index. It travels in the save's tail, version 3 ("PPSEASON", u32 3, "No limit" offset, start shift, u32 flags with 1 = past season 10, then what version 2 had); version 2 still reads.
  - **The save's players:** Season mode keeps its own copy of the players, 1,500 records of `0xB0` at `[0x8104B59C]` + `0x3564` (ids 7001 to 8500; 1,209 in a game), at the same place in every dump from 2008 to 2028. The age comes from the year shown and the birth date (`0x80B861B0`: `0x80B4A888`, and the birth year, 11 bits from bit 1 of word `+0x80`, read by `0x8082B544`; then `0x80B05E2C` subtracts and adjusts by month and day). So, starting in another year, those players' birth year moves by the same amount.
  - **Verified in-game:** with `start_year = 2026` and the 2026 schedule, the new season starts on «TUESDAY 3/24/2026», the 3/25 menu says «WEDNESDAY» and today's opponent, the Giants at 8:05 PM. Max Fried (31 in the test roster, made for 2025) shows as born 1/18/1995. Saved, game closed and loaded again, it is still 2026 (`[season] season 1 loaded; our tail, year offset 0, start shift 18`), the save list shows 2026 too, and an old 2018 save without a tail loads in 2018 without inheriting the shift.

- **DH in both leagues** (October 20, 2026; patches `wiicompiled-translator-store-overrides` and `wiicompiled-season-universal-dh`): the game rules are a block at `[0x8104E1D8]` (`0x8106D460` in the REL BSS); the DH is byte `+0x10` (getter `0x805F0F1C`, setter `0x805F0F0C`; many functions read it directly). Exhibition copies it from its settings screen. A Season mode game goes through `0x80B6ECA0`, which calls `0x80B6F00C`: it stores `(0x80BA1D2C(home team) == 0)` in `+0x10` with the `stb` at `0x80B6F210` (DH only when the home team is American League). `0x80B6EEAC`, another Season game setup, stores 1 or a `li r0, 0` at `0x80B6EF88`.
  - Found by comparing dumps with Exhibition's DH on, off and on again, and with watchpoints on the byte (`WIICOMPILED_DEBUG_WATCH`).
  - **The change:** the translator's `immediate_overrides` already covered `cmpwi`/`cmplwi`/`addi`/`li`; now a listed `stb` also stores its register OR the runtime's value (`expect: 0`, which keeps the original). With the option or with `dh = "both"`, both addresses give 1.
  - **Verified in-game:** Yankees at San Francisco on 3/25/2026 (AT&T Park). Without the option Fried and Verlander bat; with `dh = "both"` in the mod and, separately, with `[season] universal_dh = true` and no mod, Stanton and D. Smith are the DHs and the game plays to the end (Fast Game, 1–0). In Exhibition, with the DH off on its screen and the option on, the pitchers still bat.

- **A season pack's divisions** (October 20, 2026; patch `wiicompiled-season-divisions`): each team has a word at `X + 128 + team × 4` with its division in bits 31..28 (0 East, 1 Central, 2 West) and its league in bits 27..24 (0 American, 1 National, 2 the All-Star teams); `0x80BA1D2C` returns the league. `0x80B719EC` rebuilds the division lists from those words (`X + 256 + league × 4968 + division × 48`: u16 order keys, up to 7 u32 teams at `+16` and the team count in byte `+44`; read by `0x80BA1C64`). The per-year standings table (`X + 0x11CD0 + year × 336`; row 20 is the season under way) keeps 7 entries of 4 bytes per division and 21 per league, laid out by `0x80BAF2B4` (with `0x80BA2510`/`0x80BA25DC`) from those lists.
  - **Order matters:** a new game goes through `0x80BB0B78` (`0x80B86AE0`, `0x80B7AF8C`, `0x80BAF2B4`, `0x80BB0BE0`…) before building the schedule (`0x80B97DC4`). Changing the divisions in `0x80B97DC4` left the lists right but the standings with the old layout (the AL West showed an empty row, «Indians» with no logo). The native replacement of `0x80BB0B78` changes the words and calls `0x80B719EC` right before `0x80BAF2B4`.
  - **The game's editor:** Season › Start › Major League Mode › team leads to «Custom League», with 7 slots per division. It refuses odd leagues («There are an odd number of total teams in the League, please make it an even number»); on confirming, `0x80C14378` applies the layout with `0x80B719EC`. With divisions other than the original ones, `0x80B97E08` returns 0 and the game generates the schedule.
  - **Verified in-game:** a pack with `al_west = ["HOU", …]` and the 2026 schedule. Log: `divisions from mod "…": 15 American League teams, 15 National League`. The standings show the Astros in the AL West (5 teams) and the NL Central with 5. Simulated to 4/30/2026 from the calendar: the AL West reads Mariners 20–12, Astros 17–14, Athletics, Rangers and Angels, with no hangs. In the Yankees' calendar the games against Miami show as interleague and those against Houston do not.

- **Season packs in MLB Life** (October 21, 2026; patch `wiicompiled-mlb-life-packs`): MLB Life uses the same season block (`[0x8104B5A8]`, `[0x8104B5A0]`…) and the same new-game setup: its vtable (`0x80E7A4C8`) has `0x80BB0B78` directly in slot 8, where Season mode's (`0x80E9C180`) has `0x80C48100`, which calls it. The career is created at index 0 and goes back to −1 right away: it starts on 11/8 of the previous year, in the off-season.
  - **MLB Life's players:** its own (ids from 9001, `[0x8104B608]`) are at `[0x8104B59C]` + `0x68E4`, not at `+ 0x3564` like Season mode's; the runtime finds the table by its first three ids.
  - **Its save:** `pm3usmy<slot>.dat`, `0xF8000` bytes, also a copy of the block with a `0x8C` header: the year index is at file `0x47EE8` and the data ends at `0xF22EB` (a load reads up to the 16 KB chunk ending at `0xF4000`). The start year goes in a tail of its own at `0xF7FC0`: "PPLIFE01" and a u32 shift. The game accepts it on load (checked by writing it by hand into an old save) and keeps it when saving.
  - **Verified in-game:** a new career (Dream Player, Crochet) with the `start_year = 2026` pack: «SAT. 11/8/2025», age 25 as in the roster, 1,210 players adjusted, 15/15 divisions and the 2026 schedule; in February, «WED. 2/18/2026». Saved to slot 2, the save list shows it as «WED. 2/18/2026» and, after closing the game, it loads in 2026 (`start shift 18`). A new Season mode game still works the same with the change. **DH in MLB Life** (Freeman's career, Tigers at Dodgers exhibition): with `dh = "both"` the Dodgers play with a DH and Yamamoto outside the batting order; without it, Snell and Mize bat ninth. It goes through the same game setup as Season mode.

- **Roster year and MLB Life's family** (October 21, 2026; patch `wiicompiled-season-roster-year`): `roster_year` in `mod.toml` makes the players born `roster_year` − 2008 years later (default `start_year`): with `roster_year = 2025` and `start_year = 2026`, Ohtani (30 in the roster) shows 31. MLB Life's family (wife and children) are records of `0x20` bytes from `[0x8104B59C]` + `0x678C`; the first word holds the birth date: bits 31..24 year − 1900, 23..20 month, 19..15 day. It is created with 2007 dates (the wife born 7/29/1983, age 24; the children from 2003 to 2007): the pack moves them by the start year's shift. Found by comparing dumps before and after the «Family Settings» screen. Verified: «MLB Life: 6 family birth dates moved 18 years», the wife born in 2001 in memory.

- **15 and 15 leagues as an enhancement, and a schedule for odd leagues** (October 21, 2026; patch `wiicompiled-season-modern-leagues`): the `[season] modern_leagues` option moves Houston to the AL West in new games (when no pack brings `[divisions]`). With leagues of 15, the game's generator (`0x80B98320`) gives uneven schedules: in a test, 2,286 games and 142 to 162 a team, while with 14 and 16 it gives exactly 162 (2,430). So when there is no `schedule/<year>.csv` and the leagues have 15 teams, the runtime puts its own schedule (`GenerateOddLeagues`): 54 rounds of 3-game series, 3/26 to 9/24, an off day every 3 rounds and the All-Star break around the day the game chose. Verified: 2,430 games, 162 a team, home between 78 and 84; the Yankees' calendar with 3-game series, off days and interleague marked; a month simulated with no hangs.

### Per-year data

- **Season mode per-year data** (P = `[0x8104B5AC]`, X = `[0x8104B5A0]`, Q = `[0x8104B59C]` + 1344; confirmed in memory dumps):

  | Data | Where | Per year | Room |
  |---|---|---|---|
  | Postseason qualifiers (8 teams; 44 = empty) | P + year × 32 | 32 bytes | 20 years |
  | Postseason series (8 series of 20 bytes) | P + 648 + year × 160 | 160 bytes | 20 years |
  | League standings | X + 0x11CD0 + year × 336 | 336 bytes | 21 years |
  | One team per year, drawn when the save is created | X + 0x159A0 + year × 2 | 2 bytes | 20 years |
  | Yearly finance value | Q + 4 + year × 4 | 4 bytes | **10 years** |
  | Yearly finance summary (10 items and balance) | Q + 44 + year × 44 | 44 bytes | **10 years** (the monthly table follows) |
  | Team records (8 × 16 bytes) | `[0x8104B59C]` + 60 + year × 128 | 128 bytes | **10 years** (the finances follow) |

  Almost everything has room for 20 years; only the finances and the team records have 10. With the index at 10, `0x80C0D658` writes into the first month's balance. The exact meaning of some fields is a guess: the tested saves have no played results.
- **Team records per year, another 10-year table** (October 20, 2026): `[0x8104B59C]` + 60 + year × 128 + k × 16 (8 records of 16 bytes of bit fields per year), right before the finances (`+ 1344`). `0x80C41BA8` (year, k) gives the address; `0x80C417A8` writes (called by `0x80C342B4`, from `0x80BB4A40`, with the index) and twelve functions read (`0x80C41800` to `0x80C41B5C`, integers or times the `float` at `0x80D2F7D0`), used by the year-picker screen `0x80ADC620`. A new game has all 10 years filled. With 20 seasons, from season 11 on it wrote over the finances: years 11 to 20 now live apart, like the finances. `0x80BB4A40` did not run with the date jumps tried (10/31, 12/25, February–March).
- **Luxury tax, a fixed 10-year table** (October 20, 2026): `0x80C4764C` works out the month's tax: (payroll, `0x80C475A4`) − (the year's threshold), times a rate from `0x80D2F9E0` (0.225, 0.30 or 0.40 by `0x80C41CFC`, 0 to 2; it looks like the years in a row over the threshold); `0x80C47720` books it as expense 9 ("Luxury Tax"). The threshold comes from the leaf `0x80C4762C`, with today's season (`0x80B4A7DC`), out of a table of 10 in the executable, `0x80D2F9B8`: 155,000, 162,000, 170,000, then +8,000 a year up to 226,000 (2017). Past 2017 the game reads what follows the table (the rates as integers, then other data): no tax in 2018–2020 and, from 2021, a threshold of 0 or below. Tested: the 2/1/2029 report took 480,304,640 P of tax. With more than 10 seasons the threshold goes on by the table's last step (+8,000 a year); with 10, as in the original. `0x80BC3C3C` and `0x80C4113C` (twice) read it too. A sweep of fixed-table reads with the season index (`0x80B74BCC`, `0x80B4A7DC`, `0x80B4A888`, `0x80B74C00`) found no other.
- **"Annual Results" (Management):** built by `0x80BECE70`, with 10 pages (`[screen + 11184]`, one per year) and each year's title as a picture (a table of 10 at `0x80E98850`, 2008 to 2017, set by `0x80C10424`). `0x80BECF40` fills the past years and puts the cursor on the index's page, which after season 10 falls past the 10 (the game crashed). `0x80BECFD0` changes page. "Monthly Results" (`0x80BCEF7C`) also sets the index's year picture.
- **Not Season mode:** the 428-byte-per-year table at `[0x8104B5C4]` and the 176-byte one belong to MLB Life (their pointers are 0 in Season mode). (year + 1) × 10 + 390 in `0x80B3C99C` and (year + 1) × 3 in `0x80BF54BC` are calculations, not tables.
- **Not found yet:** a per-year history of awards or player stats.

### Season save file

- **The Season save** (`pm3usse<slot>.dat`, 1,179,648 bytes) is a copy of a memory block; a file offset is the position inside the block + 0x8C. The year index is at file offset 0x47E8C. Changing the day in memory and saving moves the save's date.
- **Free room in the file, not in memory:** the save data is 0x119910 bytes; the file is padded with zeros up to 0x120000 (26,352 free bytes). On load the game reads the file into a separate buffer and copies only that data, so whatever is in the file's tail never reaches memory and does not stop the load (tested with a marker at 0x11F000). The memory right after the data is used by something else.
- **Loading and saving a Season save:** a load reads the file in 16 KB chunks up to the chunk past the end of the data (0x11C000), short of the end of the file. Saving reads the slot's old file whole, writes the new one and reads it back whole to check it. So the runtime reads the tail itself when a read reaches the end of the data, and drops it if the reads go on past it.

Sources: `wiicompiled-season-probe.patch`, `tools/season_save.py`, dumps in `local/tmp/season/`, `docs/en/superpowers/specs/2026-10-19-season-archive-findings.md`.

## 10. Graphics (GX and VI)

- **Native widescreen:** the game queries `SCGetAspectRatio` (`0x800D3374`) from `0x8004D994`, `0x80055928`, `0x80062D24` and, in the REL, `0x807AB5D4` and `0x80CCAE3C`, and adapts the image to 16:9 (round objects stay round). Translated code and observation, `RL8E54`. **Until October 17, 2026 the recomp did not turn it on:** Wiicompiled's own answer to `SCGetAspectRatio` carries Mario Kart Wii's address and was not bound, and the original function reads `IPL.AR` from a SYSCONF the install does not have, so it returned 4:3 and "Force 16:9" only stretched a 4:3 image (a player noticed). Fix: the native `hle/mlb/sc_aspect_ratio.cpp` at `0x800D3374` (patch `wiicompiled-mlb-widescreen`), which answers from the video settings. Checked in the test match: the game logs `SCGetAspectRatio -> 16:9`, the ball is round and the HUD text keeps its proportions.
- **Black border around the image:** the game sets up the VI at 640x480 (`VIConfigure` `0x800B37FC`, `viXOrigin` 40, `viYOrigin` 0) and its display copy takes all 480 lines, but it draws inside a black border 16 lines tall at the top and at the bottom (448 useful lines, menus too); the line next to it is half dark. Measured on captures of the test match and the stadium menu, `RL8E54`. On a TV the overscan hid it; on a monitor it was two black strips (a player asked about it). The "Fill screen" option (`[video] crop_black_borders`, on by default): the display copy leaves 17 lines out at the top and at the bottom (`aurora_set_display_crop_lines`, patch `wiicompiled-mlb-fill-screen`) and the image fills the window; on 16:9 it ends up 8% taller (480/446). Checked: no strips, no dark edge, and the HUD stays whole.
- `GXInit` `0x800C25CC`, `GXCopyDisp` `0x800C61F8`.
- GX global variables:
  - `FifoObj` `0x802D2AC0`
  - `gxData` `0x802D2B40` (the `__GXData` pointer is at `0x8031C398`)
  - GPU FIFO `0x802D3140`, CPU FIFO `0x802D3164`, display list FIFO `0x802D3188`
  - `GXDrawDone` queue `0x8031B6F8`
- VI global variables:
  - initialized `0x8031B400`
  - TV format `0x8031B470`
  - post/pre retrace callbacks `0x8031B47C` / `0x8031B480`
  - `VIWaitForRetrace` queue `0x8031B488`
  - retrace counter `0x8031B4AC`
  - `HorVer` `0x802BD070` (`+0x30` = next framebuffer)
- **How it draws:**
  - All 3D goes through display lists; the interface writes directly to the FIFO.
  - Skinned models use direct `GX_VA_PNMTXIDX`, with POS/NRM/CLR0/TEX0 in `GX_INDEX16` (9-byte vertices).
- **Per-frame load:**
  - Menus: 200-450 draws.
  - Match: from 208 to 1,012.
  - Fielding view: 650-990.
  - About 35 EFB copies per frame in a match; about 17 go to textures already read in the same frame.
- **Strap screen:** it composes the image, copies it with `GXCopyTex` to `0x90000840` at 512×480 and draws it as a 512×512 texture on a full-screen quad. The hardware does not check the size, so the copy fills the top rows.
- **TLUT:** the game reinitializes the same TLUT object on every draw, without destroying it.
- **Masked jumps:** the game uses jump tables bounded with `rlwinm` instead of `cmplwi` (e.g. `0x80031218`).

Sources: `config/wiicompiled/rl8e54-data.csv`, `docs/en/wiicompiled-feasibility.md`, `wiicompiled-aurora-partial-efb-copy.patch`, `wiicompiled-tlut-reinit-cache.patch`.

### Match camera

Found on 18 October 2026 for the stadium preview (`runtime/src/hle/mlb/game_camera.cpp`, patch `wiicompiled-stadium-viewer`). Methods: MEM1 dumps every 3 s (`WIICOMPILED_DEBUG_MEM1`) searched for plausible eye/target pairs; a write watchpoint on the eye (`WIICOMPILED_DEBUG_WATCH`) resolved with `llvm-nm`; temporary wrappers in the shards.

- **Camera object** (DOL class, vtable `0x801568AC`, `0x2C0`-byte blocks in the MEM1 heap; its address changes between runs):

| Offset | Field |
|---|---|
| `+0x40` | eye (x, y, z, 1), in decimetres, Y up, second base towards −Z |
| `+0x60` | target (x, y, z, 1) |
| `+0x70` | up vector (0, 1, 0, 1) |
| `+0x8C` | pointer to its view function (`0x80055D24`) |
| `+0x90` | pointer to its projection function (`0x80054B1C`) |
| `+0x94` | cot of half the projection's **horizontal** angle: 2.206 in the batter view (48.8° wide; 28.6° tall at 16:9). Checked by drawing the collision in the viewer: read as the vertical angle, everything came out 16/9 times closer to the centre of the picture |
| `+0x154` | 3×4 view matrix |
| `+0x184` | viewing direction |

- **`0x80055D24`** rebuilds the view every frame: the game's own `LookAt` (`0x80055620`, signature like `C_MTXLookAt`: matrix, eye, up, target) into `+0x154`; then `target − eye` (`0x8006327C`) stored through `0x80060588` into `+0x184`. The match does **not** use the SDK's `C_MTXLookAt` (`0x800A8DC4`); the Mii renderer does (`0x80090AF8`).
- **Match camera director:** object pointed to from `0x81050810` (BSS). `0x80C779D4(director, source)` copies a source camera into the main one with `0x80055300` (camera copy, 633 bytes from `+0x40`); the main camera comes from `0x80061CF8([[director+60]+732], 0)`; it then sets bit 1 of byte `+128`, calls the camera's virtual `+24`, `0x80C6217C([director+60]+64)` and the virtual `+116` of `[director+60]`. It only acts when `[director+64] == 5`.
- **Shots** (source cameras, one per shot, each with its own controller; seen in 2 minutes of an automatic match):

| Caller of the director | Shot | Sample eye → target |
|---|---|---|
| `0x80CC5BE8` | behind the batter (switches side with the batter's hand) | (±2.1, 15.5, 59.6) → (∓9, −1.7, −229) |
| `0x808F1CD8` | play, high and to the side after contact (cot 3.51 ≈ 31.8°) | (±328.6, 184.8, 102.7) → (0, 0, −120.1) |
| `0x80AA0BA4` | wide; varies: (−570.5, 200, −560.5) → (−4.8, 5, −189.1) or (0, 51.3, 154) | — |
| `0x808962AC` | introduction (at the start) | (0, 50, 0) → (0, 699, −1200) |

  No separate pitching camera showed up: with the AI, pitches are seen from the batter's.
  At the start, an opening fly-over moves the main camera around the stadium without going through the director (writing its eye and target before the `LookAt` does not stop it). Applying a shot with `0x80C779D4` ends it.
- **What does not follow this camera:** players, ball and the strike zone cursor are drawn with the source cameras' view matrix (`+0x154`), not the main camera's; the HUD is orthographic. The stadium, crowd and flags do follow the main one.
- **Per-frame match update:** `0x808816C8(match)` calls `0x8088175C(match)` (its own state machine through `0x80882E84`) and then `0x808661BC(match)`, which jumps to `0x80866EA4`: the match's state tree. It is a hierarchical state machine: `0x8086C980(node, ptmf, event)` pushes the state (`[node+16]` is the depth; `+4`, `+20`, `+32` and `+44` per level) and calls the handler through `0x80129758` (pointer-to-member call). Pitching, batting, fielding and the camera director hang from the tree: the stack of a batter shot is `0x80CC5BE8` ← `0x80CAD398` ← `0x80867A74` ← `0x8086C980` ← … ← `0x80866EA4` ← `0x808816C8` ← `0x80022438`. The stadium's animated crowd and flags update outside that tree: without it they keep moving (checked in the original stadium).
  Slot `+0x18` (`0x808816FC`) of vtable `0x80E5E350` was not called even in 60 s of a normal match: it is not this update.
- **The recomp** replaces `0x80055D24` and `0x80C779D4` with identical native versions. With the preview on, the first writes the free camera's eye and target into the main camera before the `LookAt`, and the second learns each source camera by its caller, to show those shots. Normal match checked unchanged: same shots in the same order.
  With the free camera, the source cameras look away (from 10 km under the field) to take players, ball and cursor off screen; their eye, target and up vector are saved and put back when a shot is chosen. `0x808816C8` is native too: in the preview, once the stadium is on screen, `0x808661BC` is skipped and the match stands still (2 minutes without out-of-memory accesses; the normal match keeps playing).

## 11. Controllers

- **GameCube controller:**
  - `PADRead` `0x800B76BC`; the game's calls to `PADInit`/`PADRead` are at `0x80040B14` / `0x80040BC8`.
  - The game divides the main stick by **88** and the C-stick by **64**. The real stick gates reach ~87 and ~74, so a full ±127 saturates at ~69% of the travel.
- **Fielder movement (checked in the game, October 15):**
  - Each port's input packet is at `common+992+port·368` (`common = *(0x8103D118)`); `+12`/`+14` are the stick axes as s16.
  - The fielding decoder `0x8088540C` (called from `0x80884FE4`) only accepts input while a human moves a fielder. It copies those axes to `field+656`/`+658` (`field = *(0x8103E708)`) and returns `field`; early exits return the packet.
  - `0x8096A690` reads that vector: it stores the angle in `field+676` and sets `field+672 = 1` when either axis is above 8 (stick tilted).
  - A full diagonal of a round stick, `(60, 61)` after the GameCube gate, makes the fielder walk; the keyboard, `(87, 87)`, makes him run. Widening only `+12`/`+14` during the decoder, and restoring them afterwards, was not enough: the fielder kept walking.
  - **Fix:** the runtime marks the port when the decoder accepts the input, and on the next `PADRead` it turns that port's main stick into a square: same direction and the radius on each axis. So an unscaled `(90, 90)` becomes `(127, 127)`, like the keyboard. With captures every 1.5 s in a test match with manual fielding, the mark only turns on in the fielding view; it stays off while pitching and batting.

- **Input objects:** they are C++ classes with a vtable. With `base` = `PADRead` buffer − 1648: GC controller at `base+1104`, KPAD at `base+560`, combined input at `base+16`. Each keeps a ring of samples.
- **Integer to floating point:** `0x80040268` converts with the CodeWarrior trick. It uses the constant `0x4330000080000000` from `r2-31664` (`0x8031BD90`); if that constant gets corrupted, all signed conversions fail.
- **KPAD/WPAD:** linked the same as in Mario Kart Wii.
  - `KPADRead` `0x80104C4C`, `KPADInitEx` `0x80105378`
  - sampling callback `0x801056A4`
  - `WPADiInitSub` `0x80108ABC`, `WPADInit` `0x80108CCC`
- **Bits:** Wii Remote A = `0x800`; Nunchuk C = `0x4000`, Z = `0x2000`; GC A = `0x100`, A+Start = `0x1100`.

Sources: `wiicompiled-mlb-pad-stick-range.patch`, `config/wiicompiled/rl8e54-data.csv`, `docs/en/wiicompiled-feasibility.md`.

## 12. Save data, NAND, SYSCONF and ES

- **Title:** `00010000/524c3845` (disc title). The save is `title/00010000/524c3845/data/pm3usma.dat`.
- **IOS:** the disc TMD asks for IOS33 (`tmd.bin` `+0x184` = `0x0000000100000021`). The game prints the version in its boot `OSReport` ("Firmware: 33.14.24").
- **Disc header in low memory:** `0x80000000` = `RL8E`, `0x80000004` = `0x35340001` (maker "54", disc 0, version 1). The runtime used to write Mario Kart Wii's maker "01" and disc 1, and used "RMCP" as the fallback code; since the patch `wiicompiled-mlb-disc-identity` it uses MLB's.
- MLB Life, Season and League write save data.
- **NAND:** the game uses the SDK's NAND/ISFS libraries on top of IOS.
  - `NANDInit` `0x800D0640`, `libState` `0x8031B778`.
  - It asks `ESP_GetDataDir` (`0x80064044`) for its own directory (`0x802D3AE0`).
- **ES requests (`/dev/es`):**
  - ioctlv 22 `ES_GetConsumption`: a disc title has no entries.
  - ioctlv 29 `ES_GetTitleDir`: `"/title/%08x/%08x/data"`.
  - ioctlv 27 `ES_DIGetTicketView`: `0xD8` bytes. `+0x0C` console ID, `+0x10`/`+0x14` title ID, `+0x18` access mask `0xFFFF`.
- **SYSCONF:** section 2.
- **Season mode save** (`pm3usse<slot>.dat`): format, free space, loading and saving in [Season mode › Season save file](#season-save-file).
Section sources: `wiicompiled-mlb-nand-es.patch`, `hle/mlb/sc_check_status.cpp`, `docs/en/mods.md`.

## 13. Identified libraries and functions

**Build dates of the SDK modules** (tags in the binary):

| Modules | Date |
|---|---|
| THP, EXI, SI, DVD, VI, AI, AX, DSP, SC, ENC, KPAD | Aug 8, 2007 |
| RFL | Sep 5, 2007 |
| PAD | Oct 3, 2007 |
| GX, NAND | Dec 11, 2007 |
| OS | Jan 30, 2008 |
| HBM (HOME Menu) | Feb 7, 2008 |

- **Compiler:** Metrowerks CodeWarrior (there are identifiers from its runtime and from MetroTRK); the exact version is not identified.
- **Libraries present:**
  - Nintendo's JSystem: J3D, J2D, JAudio2/JAS, JStudio, JMessage, JGadget
  - HOME Menu (`homebutton::*` from `0x80064248`, `nw4hbm`)
  - Broadcom Bluetooth stack
  - MetroTRK
  - RVL SDK
- **Absent:** there is no NHTTP, DWC/GameSpy or sockets (their strings were not found), nor AXFX reverb.
- **How the 1,794 functions were named** (`config/wiicompiled/rl8e54-functions.map`, format `<address> <name>`):
  1. Signatures from the Dolphin database (`totaldb.dsy`), accepting only unique matches: 1,375 functions, among them `gx.a` 108, `os.a` 105 and `dvd.a` 63.
  2. Alignment of the link order with the Mario Kart Wii map between common anchors: 118 more (≈97% accuracy in hold-out tests).
  3. Names from dtk.
- **Equivalences with Mario Kart Wii** (useful for porting MKW code or hooks): `InsertAlarm` `0x8009B448`, `OSSaveContext` `0x8009CCD8`, `__OSThreadInit` `0x800A3C0C`, `__OSTimeToSystemTime` `0x800A53A4`, `OSLoadContext` `0x8009CD58`. `config/wiicompiled/rl8e54-data.csv` translates 65 data addresses from MKW to MLB, each with the instruction that proves it.
- **No address yet:** `VIInit`/`VIConfigure`, `PADInit`, `OSInitAudioSystem`, `__OSInterruptInit`, `OSGetCurrentThread` and a few more. Some may not exist because the linker stripped them.

## 14. Game quirks that matter when porting

| Quirk | Consequence |
|---|---|
| Sound measures time in 5 ms loops | If the timer is late, the commentator gets cut off and clicks are heard (section 4) |
| The main thread does not yield during long computations nor in "NOW LOADING" | With cooperative threads, the others get no turn; switch points are needed |
| The heap takes the whole arena almost to the byte | The arena top has to be `0x817FFD40`, and the REL BSS has to end up at `0x8102B0C0` |
| The game erases the game code from low memory | It cannot be used to tell which game is running after boot |
| 512×480 copy onto a 512×512 texture | A partial copy is valid; the emulator must not reject it |
| Jump tables without a prior comparison | A static translator must recognize the index bounded with `rlwinm` |
| Getters inside other functions, called through a vtable (`0x80664C20`-`0x80664C38`) | They have to be added to the function map or the indirect jump fails |
| GC stick divided by 88/64 | A modern controller needs its travel scaled |
| `OSInit` waits for the SYSCONF and `RFLWaitAsync` spins in a loop | IOS callbacks must arrive while the game waits |

## 15. How to keep investigating

- **Function map:** `config/wiicompiled/rl8e54-functions.map`. Adding a name to the map carries it into the translated code the next time it is translated.
- **SDK data from MKW to MLB:** `config/wiicompiled/rl8e54-data.csv`, with the proof for each row.
- **Tools** (`docs/en/reverse-engineering-tools.md`):
  - `tools/identify_sdk_functions.py`
  - `tools/align_sdk_by_anchors.py`
  - `tools/function_data_refs.py`
  - `tools/projectbin.py` (reads and rebuilds `project.bin`)
  - `tools/packwii.py` (exports and imports the textures of a `.pack`)
  - `tools/stadium_re.py` (checks the stadiums: `PJ7M` models and `tbl` table) and the Blender add-on `tools/blender/powerpros_stadium`
  - `tools/roster/` (converts rosters)
- **Watchpoints in the recompiled game:** `WIICOMPILED_DEBUG_WATCH=<address>` reports every write to that address, with the call stack. That is how the phrase clock was found.
- **Audio diagnostics:** `WIICOMPILED_AUDIO_DUMP=<file>` saves the final mix to measure clicks.
- **Still to investigate:**
  - `.pack` objects other than textures and stadiums: player models, `SURF`/`SURG` layers and motions
  - what is pending for the stadiums (placement flags, plane trees, effect of each surface)
  - text of the `PCD`/`TCD`/`SID` `.dat` files
  - structure of the teams and lineups beyond what is mapped
  - MLB Life logic
  - the ~450 getter candidates in the middle of functions
  - names of the REL functions (43,088 unnamed)

When adding something to this document, state where it comes from (disassembly, runtime or observation) and the version `RL8E54`.
