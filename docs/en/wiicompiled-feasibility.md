# Feasibility test: Wiicompiled with MLB Power Pros 2008

*Versión en español: [../wiicompiled-feasibility.md](../wiicompiled-feasibility.md)*

The goal is to find out whether the Wiicompiled translator and native runtime (the Mario Kart Wii recompilation, GPL-3.0, `6fa24737`) can serve as a base for this game. The alternative is to reimplement the same thing on top of DolRecomp and ModernGekko.

The current path remains intact: it is the reference and provides the lockstep verifier.

## Why it fits

- Wiicompiled supports a DOL plus a static REL at a fixed address (`inputs.rel.load_address`). MLB has that same format: `ovl_main.rel`, with the header at `0x80575580`, as `local/generated/dolrecomp/rel-linked/layout.json` already established.
- Its runtime covers the RVL SDK in a general way:
  - GX on top of aurora (Dawn/WebGPU);
  - OSThread as fibers;
  - native AX mixer;
  - DVD and NAND;
  - PAD, WPAD and KPAD;
  - VI.
- Its ~377 native hooks are tied to MKW addresses. For MLB they have to be rebound.

## Phase 1: translation (October 10, 2026)

`tools/wiicompiled-translate.ps1`:
- creates the working copy `local/wiicompiled-mlb/src`;
- applies `tools/patches/wiicompiled-rel-bss-address.patch`;
- builds the translator;
- converts the dtk inventory into `MAP.txt` (`tools/dtk_to_wiicompiled_map.py`);
- translates with `config/wiicompiled/rl8e54.yml`.

The .NET and NuGet caches, temporary files and outputs stay in `local/`. Nothing was written to C:.

| Measure | Value |
|---|---|
| Translator tests | 642 of 642 |
| Translated functions (DOL + REL) | 48,219 |
| Reached by the call graph from `__start` | 3,017; the rest are seeded from the dtk map |
| Unsupported instructions | 0 |
| Functions with invalid SSA | 0 |
| Full translation time | 57 s cold, 37 s with no changes |
| Generated C++ | 394 MB, packed into 72 shards with balanced content (1.04×) |
| Embedded data | 13.05 MB (DOL 2.30 MB and REL 11.38 MB) |

For comparison: our generator takes about 50 min for a full generation and about 22 min with the per-IR cache.

Adjustments that were needed:
- **REL BSS.** The game allocates the BSS at `0x8102B0C0`, `0x40` beyond where Wiicompiled puts it by default (right after the file). The `lis`/`addi` relocations into the BSS are fixed in the translated code, so the patch adds `inputs.rel.bss_address`, with overlap and alignment validation.
- **dtk `gap_*` entries.** They are not code; for example, the `MetroTRK` string in `.init`, at `0x80004338`. They produced the only "unsupported" instruction (`0x4D657472` = "Metr") and the converter now discards them.
- **L2CR.** `mfspr 1017` is translated as `PPC_ReadSpr(1017)`, which its runtime already keeps as a shadow register.

Scope: this is translated code, not validated behavior. The runtime does not build for MLB yet. *(Resolved: it builds and boots in phase 2, below.)*

## Name coverage for rebinding hooks

The runtime has 582 native registrations:
- 205 are `gx_stub_*`, which abort on purpose;
- 377 are real implementations.

Cross-referencing their normalized names (`GX__SetVtxDesc_8016d3a4` → `GXSetVtxDesc`) with the 361 names that dtk assigned in MLB, **only 61 already have a known address in MLB**:
- 11 of 13 are `PPCM*` helpers;
- `__init_hardware`;
- a few NAND, IOS, DVD and GX functions.

Without a known address in MLB are, for example:
- almost all of GX: 2 of 71 `GXSet*`, 0 of 9 `GXGet*`, 0 of 9 `GXLoad*`;
- WPAD: 0 of 14;
- SC: 0 of 6;
- the OS thread and alarm functions.

Conclusion: without identifying the SDK functions in MLB, the Wiicompiled runtime cannot be used, nor can native replacements be made on the current path. That identification is the next piece of work common to both paths.

There are two viable ways to do it without MKW:
- **Byte signatures with the relocations masked**, generated from the public decompilations of the RVL SDK that compile identically to the original. The SDK versions in MLB, according to the strings on the disc, are: OS from January 2008, GX and NAND from December 2007, and the rest from August to October 2007.
- **Propagation through the call graph** from already identified functions; for example, `GXInit` (0x800C25CC) calls the rest of the GX initializers in a known order.

## SDK identification in MLB (October 10, 2026)

Two tools, neither of which needs an MKW binary:

1. **`tools/identify_sdk_functions.py`** applies Dolphin's signature database (`Data/Sys/totaldb.dsy`, 10,549 entries, RVL SDK included). It reimplements `HashSignatureDB::ComputeCodeChecksum`, which only uses the opcode bits, over the DOL and the relocated REL.
   - A match is accepted only if the hash is unique in the game and in the database.
   - C++ names without a library are discarded, because they are classes from other games that collide on tiny functions.
   - If dtk already had a real name, dtk's wins.
   - Result: **1,375 functions**, with the libraries `gx.a` (108), `os.a` (105), `dvd.a` (63), `vi.a` (26), `wpad.a` (20), `sc.a` (20), `nand.a` (20) and others.
   - Where dtk already had a name, the signature matches in 173 of 175 cases. The 2 disagreements led to adding the filters above.
   - Output: `local/analysis/sdk-symbols.csv`.
2. **`tools/align_sdk_by_anchors.py`** aligns the link order with Wiicompiled's `projects/mkwii/MAP.txt`, which only contains addresses and names.
   - Between two common anchors in increasing order, it names the functions in between when the sizes match.
   - If the gaps have a different number of functions, it only pairs from each anchor inward.
   - It iterates using the new names as anchors and discards names assigned twice.
   - Result: **118 more functions**.
   - Precision in a hold-out test (30% of the known names hidden, 5 seeds): **97%** (234 of 241), with 36% recall.
   - Output: `local/analysis/sdk-symbols-aligned.csv`.

`tools/dtk_to_wiicompiled_map.py` merges both outputs into `MAP.txt` and leaves 1,681 named functions (dtk only had 361). Hook coverage is measured by `tools/wiicompiled_native_coverage.py`:

| Group | Before (dtk) | Now |
|---|---:|---:|
| GX | 2 of 71 `GXSet*` | 106 of 136 |
| OS | — | 73 of 99 |
| Storage | — | 25 of 53 |
| Input | 0 of 14 (WPAD) | 12 of 22 |
| Audio | — | 7 of 24 |
| **Total** | **61 of 377** | **241 of 377 (64%)** |

An important part of the SDK still has no address:
- VI: `VIInit`, `VIConfigure`, `VIGetRetraceCount` and the retrace callbacks;
- controllers: `PADInit`/`PADRead`, `KPADRead`, `WPADInit`;
- audio: AI/DSP and `OSInitAudioSystem`;
- NAND: open, close and read;
- OS: `__OSInterruptInit`, `__OSPSInit` and `OSGetCurrentThread`.

Some may not exist in MLB because the linker removed them (for example `GXDrawSphere`). For the rest there are two routes:
- references to MMIO registers (VI `0xCC002000`, AI `0xCD006C00`, DSP `0xCC005000`) and to text strings;
- our own signatures compiled from the public SDK decompilations.

Precision: the aligned names are 97% candidates. Every hook that is rebound must be validated with behavior (lockstep or a targeted test) before it is accepted.

## Phase 2: Wiicompiled runtime with MLB (October 10, 2026)

**Toolchain**, all in `local/`:
- LLVM-MinGW `20260616` (sha256 verified) in `local/tools/llvm-mingw-20260616-ucrt-x86_64`;
- Dawn and SDL3 downloaded by CMake at configure time;
- portable mode (`portable.txt`), with `UserData` inside the build directory.

**Work cycle:**
1. `tools/wiicompiled-translate.ps1 -Fresh -Groups os,audio,storage,vi,gx,input` does, in ~1 min:
   - clones the working copy and applies the three patches;
   - regenerates the SDK names;
   - rebinds the hooks;
   - translates and emits the shards.
2. `tools/wiicompiled-build.ps1 -RunSeconds N` builds (~2–4 min) and runs.

The full round trip is around 5 min. A runtime-only change builds in seconds.

**Patches** (against `6fa24737`):
- `wiicompiled-rel-bss-address.patch` (translator): REL BSS at its real address.
- `wiicompiled-masked-jump-tables.patch` (translator): jump tables whose index is bounded by the mask of an `rlwinm` rather than by a `cmplwi`.
  - MLB uses them, for example at `0x80031218`.
  - The 642 tests still pass.
- `wiicompiled-mlb-bringup.patch` (runtime):
  - `game_profile.h` with each game's constants (entry point, constructors, disc ID, ArenaLo, `OSLoadContext`);
  - the `*_UNBOUND` forms of the hooks;
  - `music_attenuation` empty outside MKW;
  - `game_aliases_rl8e54.cpp`, which forwards the HLE's direct calls to MKW functions;
  - a dump of guest threads with `WIICOMPILED_DEBUG_THREADS=<s>`.

**Rebinding** (`tools/wiicompiled_rebind.py` + `config/wiicompiled/rl8e54-data.csv`):
- Each hook in an enabled group receives the MLB address of the same function, matched by name between the MKW map and the MLB map.
- MKW names repeated at several addresses are not bound.
- Data addresses are translated by **verified blocks**: in each SDK object, several anchors read from the MLB code confirm the same offset. The evidence for each row is in the CSV.
- Whatever could not be verified is explicitly discarded. For example, four DVD HLE state writes would land on different MLB variables and are replaced with `(void)0`.
- `tools/function_data_refs.py` and `tools/disasm_guest.py` (disassembly taken from DolRecomp's C output; retired, in the `moderngekko-era` tag) are the tools for this targeted decompilation.

With `os,audio,storage,vi,gx,input` active: **316 hooks bound, 302 unbound, 94 literals translated**, none unresolved.

**Boot progress**, each enabled group moved the boot forward to the next point:

| Active groups | Where it stops |
|---|---|
| none | `__start` → MMIO write `0xCC003004` (PI) |
| os | `__OSInitAudioSystem` (DSP MMIO) |
| + audio | low-level DVD from `OSInit` |
| + storage | `VIInit` (MMIO `0xCC002002`) |
| + vi | `GXInit` (MMIO `0xCC000002`) |
| + gx | `main` → `WPADInit` (Hollywood GPIO) |
| + input | indirect jump to an unrecognized table, fixed with the translator patch |
| all of the above | **runs 60 s without failing** |

In that last run:
- `OSInit`, VI, GX, AI and the AX mixer thread are active.
- There are 5 guest threads as fibers.
- The game prints its `OSReport` output (SDK OS, SC, KPAD, AX).
- **It does not draw yet.** The window shows the WiiCompiled placeholder at ~50 FPS. *(Resolved in "Render", October 11.)*
- The active threads are from the DOL; the REL `ovl_main.rel` has not been loaded or linked yet.
- The main thread is in the loader's frame loop (`0x800472B4`): GX token synchronization and `VIWaitForRetrace`.

**Next step (resolved in "Loading `ovl_main.rel`"):** find out what the REL load is waiting on. Most likely it is the DVD or NAND path of the game's loader (thread `0x80242888`, entry `0x80027A54`). The fiber dump only stores the initial context, so the DVD and NAND calls need to be traced.

Scope: this is observed boot behavior, not validated against Dolphin. Each rebound hook remains a candidate until it is compared with the reference (lockstep or targeted tests).

## Render (October 11, 2026)

**Fixes** (all in `wiicompiled-mlb-bringup.patch` and `tools/wiicompiled_rebind.py`):
- **Runtime boot screen.** It covered the image until MKW's strap hook (`StrapScene`) accepted input. Outside MKW it is treated as accepted.
- **`dynamic_aspect.cpp`.** It wrote EGG screen registers at MKW addresses, which in MLB are game data, and called MKW functions by address. It is now disabled outside MKW.
- **`__GX::X` names from the MKW map.** They are now normalized to `__GXX`, which binds 7 internal GX functions: dirty state, textures, TMEM, PE and revision initialization.
- **Fatal GX stubs (`gx_stub_*`).** They are never bound. In MKW they abort because that game never reaches them; in MLB it is better to run the translated original, whose register writes to the FIFO are applied by Wiicompiled's parser.
- Groups `ios`, `esp`, `c_stdio` and `trk` enabled. `sc` is still not enabled.

**Optional diagnostics:**
- `WIICOMPILED_DEBUG_THREADS=<s>`: guest threads, native stack of the main thread with game function names, `GXCopyDisp`/`GXBegin` counters and VI presentation state.
- `WIICOMPILED_TRACE_NATIVES=<frame>`: natives invoked in that frame. Only covers the typed path.
- `WIICOMPILED_DEBUG_DVD=1`: every file read.

**Status:**
- The game runs its frame loop: ~50–60 `GXCopyDisp`/s and ~70 primitives per frame.
- It reads the sound banks and `project.bin` from the disc.
- It waits on the strap screen, which requires pressing A; in the reference that screen appears at ~15 s.
- **The output is black.**

**Ruled out** with temporary instrumentation:
- Textures: they reach aurora and their content is the strap screen's (DDS dump).
- Colors: material `ffffffff` and TEV register C0 `ffffffff`.
- Projection: correct 640×480 orthographic.
- Viewport and scissor: valid values.
- `CULL_NONE`.
- Indexed position array: at 0x8054E1C0, byte-for-byte identical to the Dolphin reference.
- `PSMTXConcat`: a native C++ version gives the same results. The input matrices are zero in the first frames and identity afterwards.
- Depth test: disabling it changes nothing.
- Depth range: compressing it changes nothing.

**Resolved (October 11).** The cause was a partial EFB→texture copy that aurora did not recognize.

How it was isolated:
- With a red clear color and without the game's draws, the screen comes out red, so presentation works.
- One draw was drawn at a time. The last of the frame's 8 is a full-screen quad that paints opaque black (blend `NONE`).
- That quad samples a 512×512 RGBA8 `GXTexObj` at `0x90000840`. RAM there is zero, also in Dolphin's MEM2 dump at second 16.
- The game composes the screen and, every frame, copies it with `GXCopyTex(0x90000840)` at **512×480**. It then draws it as a 512×512 texture. Dolphin keeps that copy on the GPU only, which is why its RAM is also zero.
- Aurora's `find_copy_texture_for_texobj` required an exact height. When it did not match, it read RAM (zeros) and the result was black.

Fix, in `tools/patches/wiicompiled-aurora-partial-efb-copy.patch` (4th patch, applied by `wiicompiled-translate.ps1`):
- The copy is accepted when width and format match and the copy is shorter than the `texObj`.
- It is sampled with the copy's real height, so that row *n* of the texture is row *n* of the copy.

**Verification:**
- Clean cycle with `-Fresh`: 4 patches, translation in 44 s, and build.
- At 16 s the strap screen is visible with the content of the Dolphin reference.
- At 20 s it has already moved on to the second panel. It runs at 60 FPS.
- This is observed behavior. There is no pixel-by-pixel comparison; the horizontal scaling of the 512 px copy to the window is not validated.

**Next step (resolved below):** get past the strap screen (automated A press) so that loading of `ovl_main.rel` continues.

## Loading `ovl_main.rel` (October 11, 2026)

**Automated input.** `tools/patches/wiicompiled-scripted-kpad.patch` adds a virtual Wii Remote on channel 0. With `WIICOMPILED_KPAD_SCRIPT="inicioMs-finMs:botonesHex,..."` it holds the `WPAD_BUTTON_*` bits during each `SDL_GetTicks` window. For example, `8000-8150:800` presses A. Without the variable the runtime behaves as before. With A, the game gets past the strap screen and shows "NOW LOADING".

To reach the REL load, four bugs had to be fixed, in this order:

1. **`DVDLowInquiry`.** Wiicompiled's HLE treated the first argument as a command block and never called the callback. `DVDInquiryAsync` remained as the SDK's executing command and `DVDGetDriveStatus` returned "busy" forever. The game's resource manager (`fn_80042C28`) only processes its queue when the drive is idle, so it read nothing. The HLE now fills `DVDDriveInfo` with Dolphin's values and signals end of transfer.
2. **`DVDLowSeek`.** It had no HLE. The real SDK touched DI registers and waited for an interrupt that never comes. The HLE now completes the command instantly. Points 1 and 2 are in `tools/patches/wiicompiled-dvd-low-completion.patch`.
3. **MEM1 arena high.** The runtime used MKW's, `0x817F0520`. Dolphin places MLB's FST, 0x2C0 bytes, at the top of MEM1 and leaves the arena high at `0x817FFD40`. It is now `GameProfile::kMem1ArenaHi`. With the MKW value the game's main heap was 62 KB smaller than in Dolphin. The heap was identical to Dolphin's on the strap screen, checked with MEM1 dumps.
4. **Placement of the REL and its BSS.** This was the bug that froze the window:
   - The translated code has the module (`0x80575580`) and BSS (`0x8102B0C0`) addresses fixed, but the game allocates both in its heap.
   - In Dolphin, the REL block fills an entire free gap of `0xAB5B20` bytes. Checked with a Dolphin probe with automated A and a memory dump (`boot-probe-20261011-183222-065`).
   - In the runtime, the time sharing between threads left that gap split differently, and the BSS landed at `0x8102B0A0`. The last BSS variables overwrote the header of the next heap block.
   - With the free list broken, a 278 KB allocation returned NULL and the game copied a `PACKWII` package over `0x80000000`. That clobbered the OS globals (`__OSCurrentThread` = `0xDC`): `OSSleepThread` could not park the thread, `VIWaitForRetrace` spun forever and the window stopped responding.
   - The fix is a native decompilation of the game's overlay loader, `fn_80051474`, in `runtime/src/hle/mlb/rel_loader.cpp`. Before allocating, it splits the free block so that the game's own allocator places the module and the BSS at the translated addresses.
   - It is in `tools/patches/wiicompiled-mlb-overlay-loader.patch`. `tools/wiicompiled_rebind.py` respects the `MLB_*` hooks, which already carry MLB addresses.

**Observed result.** After pressing A, the game:
- reads the resources;
- loads and links `ovl_main.rel`;
- runs its `_prolog`;
- reads many more resources from `project.bin`;
- shows "The Wii system memory is being checked." at 60 FPS.

This is observed behavior, not validated pixel by pixel or against Dolphin beyond the heap layout.

**Methods that worked.**
- MEM1 dump from the diagnostics thread, compared with Dolphin's.
- Walking the game's heap block list. The header takes 0x20 bytes: `+0` flags, `+8` size, `+0xC/+0x10` free-list links, `+0x14/+0x18` neighboring blocks.
- Windows hardware watchpoint: DR0–DR3 on all threads, over the host view and over the guest view `0x100000000000 + address`. Translated code writes through the guest view. The watchpoint prints the host stack symbolized with `llvm-symbolizer`.

This instrumentation was temporary and is not in any patch.

**Next step (resolved below):** check how far it gets after the system memory check (NAND and save data) and measure the boot up to the menu.

## Memory check, NAND and main menu (October 11, 2026)

A long Dolphin probe with automated A (`boot-probe-20261011-191321-242`, Vulkan graphics and a screenshot every 30 s) provided the reference. The sequence is "NOW LOADING", the 2K Sports logo, the intro video, the main menu and "Game Setup". The memory check passes right away.

Bugs fixed, in order:

1. **Mixed NAND layer.**
   - Wiicompiled had HLE for `NANDInit`, `NANDRead`/`Write`/`Seek`, `NANDCreate`/`Delete`/`CreateDir`, their asynchronous versions, `NANDSafeCloseAsync` and `ISFS_OpenLib`. On the other hand, MLB's `NANDOpen`/`SafeOpen` ran translated.
   - In addition, `ISFS_OpenLib_Initialize` writes `__fsFd` and the IPC heap with MKW `r13` offsets. In MLB `__fsFd` is at `0x8031A730`, so the write landed on another game variable.
   - Fix: a new `unbind` row type in `config/wiicompiled/rl8e54-data.csv`, which the rebind respects. Those 12 hooks stay unbound and MLB runs the real NAND and ISFS libraries of the SDK on top of the IOS HLE layer, which is shared.
2. **Asynchronous IOS calls.**
   - `IOS_OpenAsync`, `IoctlAsync` and the rest are in `hle/ios.cpp`, the `ios` group, which was disabled.
   - Fix: the default groups of `tools/wiicompiled-translate.ps1` are now `os audio storage vi gx input ios`.
3. **ES** (`tools/patches/wiicompiled-mlb-nand-es.patch`):
   - `ES_DIGetTicketView` (27), with the title's ticket view in Dolphin's TicketView format;
   - `ES_GetConsumption` (22), with 0 entries, like Dolphin;
   - `ES_GetTitleDir` (29), which needs the real `NANDInit`;
   - title ID `00010000/524c3845`, as in Dolphin; the runtime used MKW's `00010004` (`GameProfile::kTitleIdHi`);
   - IOS trace that can be enabled with `WIICOMPILED_DEBUG_NAND`.
4. **`SCCheckStatus`** (`tools/patches/wiicompiled-mlb-sc-status.patch`).
   - `OSInit` busy-waits in a loop on the asynchronous SYSCONF read.
   - On the console, the IPC interrupt delivers the callbacks during that loop. The runtime queues them and only delivers them from the scheduler.
   - Fix: a native decompilation of `SCCheckStatus` (`0x800D1AB0`) that first delivers the pending callbacks.
   - Without SYSCONF in the managed NAND, the game uses default values and boots anyway (checked).
5. **`RFLiIsWorking`.** `RFLWaitAsync` (`fn_800876A0`) spins on `fn_80087B94`, identical to MKW's `RFLiIsWorking`. Wiicompiled already had an HLE that pumps alarms and IOS callbacks. It is bound with a `code` row and a `range` row for the manager pointer (`0x8031B168`).
6. **Menu performance: from 2 to 60 FPS** (`tools/patches/wiicompiled-tlut-reinit-cache.patch`).
   - Stack sampling pointed to palette texture conversion and pipeline compilation (`amdxc64.dll`) every frame.
   - Counting in `resolve_static_palette_texture` showed that every palette use was accompanied by an `evict_tlut_object`. MLB reinitializes its TLUT object on every draw, and `CreateHostTlutObj` destroyed the previous one. That destruction emitted `DESTROY_TLUT` with an ID that in aurora is content-based and shared, so it threw away the conversions that the new initialization was going to reuse.
   - Fix: on reinitialization, the object is replaced without destroying it. If the content changes, `resolve_tlut_identity` already bumps the version.

**Other changes.**
- `tools/patches/wiicompiled-debug-tools.patch` adds `runtime/src/debug_tools.cpp`, which is only enabled by environment variable:
  - `WIICOMPILED_DEBUG_MEM1=<prefijo>[,s]` dumps MEM1;
  - `WIICOMPILED_DEBUG_WATCH=<addr>` and `WIICOMPILED_DEBUG_WATCH_VALUE` set a hardware watchpoint on all threads and on the host and guest views, and print the stack with offsets for `llvm-symbolizer`.
- `rea` does not support PowerPC: it rejects the DOL and the ELF produced by `tools/dol_to_elf.py`.

**Observed result after a `-Fresh` with 11 patches** (312 hooks bound, 309 unbound):
- the boot goes through the strap screen, the REL load, the memory check, the main menu and the "Power Pros via Wii Remote" submenu with the automated presses;
- the menus run at 60 FPS.

This is observed behavior, not validated against Dolphin pixel by pixel. Audio and actual gameplay remain unchecked.

**Next step (resolved in "In game" and the following sections):** drive the presses to Exhibition and a match, and check rendering and input in game.

## In game: Home Run mode (October 11, 2026)

- **Virtual Nunchuk.** `WIICOMPILED_KPAD_EXT=nunchuk` connects a Nunchuk to the scripted remote; C is `0x4000` and Z is `0x2000`. Exhibition, Home Run and the other modes require it ("This mode requires the Nunchuk or the Classic Controller"). Without a Nunchuk, the menu offers the "Power Pros via Wii Remote" mode.
- **Translator bug in jump tables.**
  - Symptom: the runtime stopped with "Missing indirect jump target 0x8057BFAC".
  - Cause: in `func_8057BEEC`, r0 is first `r4 - 13` (compared with 2) and then `slwi r0, r3, 2` indexes a 10-entry table, bounded by `cmplwi r3, 9`. `JumpTableDetector.TryFindUpperBound` searched backwards for a comparison on the index register without stopping when it reached its redefinition, so it took the old `cmplwi r0, 2` and generated only 3 cases.
  - Fix: on finding the shift that forms the index, the search follows the source register, and any other write cuts it off.
  - The translation of 67 functions changed. There is a regression test, `IgnoresCompareOnAnOlderValueOfTheIndexRegister`, and all 643 translator tests pass. It is included in `tools/patches/wiicompiled-masked-jump-tables.patch`.
- **Observed result** with A every 6 s and the Nunchuk connected:
  - it goes through the title and the menu and enters Practice/Home Run;
  - team and stadium selection;
  - "NOW LOADING" and Home Run mode in game: pitches, "STRIKE 70 mph", pitch counter, records and batter (Lugo);
  - all at 60 FPS.
- **Open problem: the 3D geometry of the stadium and the players comes out broken** (polygons in splinters and spikes). The 2D HUD and the menus look fine. Resolved in the next section.

## 3D geometry fixed (October 11, 2026)

**Diagnosis.** Bisection with temporary switches in `GX__CallDisplayList`, since removed:
1. Skipping all display lists leaves only the HUD. All the 3D goes through display lists; the HUD goes through direct writes to the FIFO.
2. Forcing the HLE parser (`ParseDisplayList`) for all lists draws the 3D correctly. The bug is in the direct path, in which aurora decodes the list.
3. The list scan cache has no influence.
4. Forcing the parser for only one vertex format isolated the culprit: direct `PNMTXIDX` with POS, NRM, CLR0 and TEX0 in `GX_INDEX16`. It is the format of skinned models.
5. Only the broken runs log about 1.9 million aurora warnings `XF: PosMtx sub-copy unsupported`, with lengths such as 1, 17 or 23. Aurora got out of sync while walking the list and read vertex bytes as XF loads.

**Cause.** `ApplyAuroraVtxDesc` (`gx_dl.cpp`) keeps a mirror of the vertex descriptor already published to aurora. It only checked the indexed attributes against aurora's real state (`GXGetVtxDesc`) and skipped the matrix indices. Other paths change aurora's descriptor without going through the mirror. With `PNMTXIDX` disabled in aurora but marked as published, aurora computed 8-byte vertices instead of 9.

**Fix.** `tools/patches/wiicompiled-dl-vtxdesc-live-check.patch`, patch number 12: it compares all attributes up to `GX_VA_TEX7` with the real state. It is one shadow register read per attribute.

**Observed result** after a clean rebuild:
- in Home Run mode the stadium (stands, scoreboard, grass, dirt), the batter, the pitcher and the ball look correct;
- 60 FPS;
- 0 `sub-copy` warnings.

It is not validated pixel by pixel against Dolphin.

**Reusable technique.** Skipping or diverting by path and by format is a cheap bisection when the HLE has two paths for the same thing. The aurora warning log gave away the desynchronization.

## GameCube controller, PS5 controller and keyboard (October 11, 2026)

**The game supports the GameCube controller.**
- The game code calls `PADInit` and `PADRead` from 0x80040B14/0x80040BC8.
- The initial screen says "If you prefer using the Nintendo GameCube Controller, please connect it while this screen is displayed".

**PS5 controller.** The HLE `PAD__Read_HLE` uses aurora's `PADRead`, which reads any SDL controller. A PS5 through DS4Windows arrives as an Xbox 360 or DualSense and is mapped to GameCube:

| PS5 | GameCube |
|---|---|
| ✕ | A |
| ○ | B |
| □ | X |
| △ | Y |
| Options | Start |
| R1 | Z |
| L2/R2 | Analog L/R |
| Sticks | Main stick and C-stick |
| D-pad | D-pad |

**Keyboard.** Patch number 13, `tools/patches/wiicompiled-mlb-keyboard-gcpad.patch`:
- **Activation:** aurora shipped the keyboard with no keys assigned and, if it was enabled, it overrode the controller on that port. The keyboard now comes enabled on port 1 and is added to the controller: buttons are combined with OR and axes are summed.
- **Default keys:**

| Key | GameCube |
|---|---|
| J | A |
| K | B |
| L | X |
| I | Y |
| Enter | Start |
| U | Z |
| Q | L |
| E | R |
| WASD | Main stick |
| TFGH | C-stick |
| Arrows | D-pad |

- They are remapped in the F10 menu. If `keyboard_bindings.dat` exists, it is used instead of the defaults.
- **Script for automated tests:** `WIICOMPILED_PAD_SCRIPT="ms-ms:hexbotones,..."` simulates a GameCube controller on port 0.

**Verified:**
- with only A presses from the PAD script, without a Wiimote, the main menu is reached with the GameCube icon active;
- the same with only the J key, sent to the game window;
- not tested with a physical PS5.

## Phantom input: int→float constant overwritten by the STM HLE (October 11, 2026)

**Symptom.** The menu cursor moved on its own and screens advanced without pressing anything, with or without a controller.

**Diagnosis:**
1. The `PADRead` log showed the controller neutral.
2. The game's input objects are classes with a vtable: GC pad at `base+1104`, KPAD at `base+560` and combined at `base+16`, where `base` = `PADRead` buffer − 1648. The GC object reported up, right and L/R pressed continuously.
3. Its sample ring had all axes at 1.0.
4. `func_80040268` converts s8/u8 to float with the CodeWarrior trick: it writes `0x43300000` and the integer to the stack, reads them with `lfd` and subtracts a magic constant from `.sdata2`. A temporary probe in the generated shard showed the stack was correct, but the constant at `r2-31664` = `0x8031BD90` was `0x4330000000000001` instead of `0x4330000080000000`.
5. A MEM1 dump compared with the DOL gave only 3 differing words in `.sdata2`, `0x8031BD94..9C`, with the values `1`, `0x00535401` and `0x00535402`.

**Cause.** The `__OSInitSTM` HLE (`hle/os/os_init.cpp`) wrote the STM variables at MKW r13 offsets (`r13-0x62cc/-0x62c8/-0x62c4`). In MLB those addresses fall on that constant. In MLB, `__OSInitSTM` uses `r13-0x6DDC/-0x6DD8/-0x6DD4`.

**Fix.** Patch number 14, `tools/patches/wiicompiled-mlb-stm-sda.patch`, which adds `GameProfile::kStmSdaOffset`.

**Scope.** Every signed int → float conversion that used that constant returned garbage. It probably also explains the "erratic" navigation seen earlier with the virtual Wiimote.

**Verified:**
- the constants stay intact;
- the controller ring stays at 0 at rest;
- the menu does not move;
- the title screen waits for "Press +" and, if it is not pressed, starts the demo, just like the original.

## Crash when fielding, stick response and FPS (October 11, 2026)

**Crash when fielding.**
- Error: `missing_jump_target 0x8092F488`.
- Cause: `func_8092F450` does a `switch` on an `lbz` without a `cmplwi`. `JumpTableDetector` took as its bound a nearby `cmpwi r0, 0` that checks something else, and generated 1 case out of 9.
- Fix: patch number 15, `wiicompiled-byte-index-jump-tables`. If the index comes from a byte load, it reads the table up to the first entry that is not a target.
- 644 tests pass, including `ReadsByteIndexedTableUntilFirstNonTarget`. The retranslation changed 38 files.
- Audit: of the 538 generated `switch` statements, the only single-case ones really do have one entry.

**Stick.**
- Patch number 16, `wiicompiled-mlb-pad-stick-range`: scales full deflection to the real GameCube gate, 87 on the main stick and 74 on the C-stick (Dolphin's values). MLB divides by 88 and by 64, so with ±127 it saturated at 69% of travel.
- Patch number 17, `wiicompiled-pad-radial-deadzone`: aurora's deadzone was per axis, 8000, and jumped from 0 to ~24%. It is now radial, rescaled and 3000 by default.

**FPS when fielding.**
- Measurement: the title's automatic demo (without pressing Start) plays the scene. A temporary log of `aurora_get_present_timing` and `aurora_get_stats` gave:
  - with D3D12, 60 FPS at 200-450 draws and 17-30 FPS at 660-990 draws (fielding view), with no pipeline compilation;
  - the game thread was sleeping in `VI_HLE_WaitForNextRetracePoll`, the render thread was blocked in the driver (`amdxc64`) and the GPU was at 20-35%.
- With Vulkan, the same 985-draw scene runs at a locked 60 FPS.
- Fix: patch number 18, `wiicompiled-prefer-vulkan`. `auto` mode prefers Vulkan on Windows and falls back to D3D12 if there is no Vulkan.
- Tools:
  - `tools/sample_host_thread.cpp` accepts a `RANK` argument (the Nth busiest thread) and must be compiled with `-static`;
  - the histogram and call chain scripts are in the scratchpad.

## D3D12 at 60 FPS and hang on "NOW LOADING" (October 11, 2026)

**Measurement method.**
- `tools/wiicompiled-bench.ps1` launches the game with a scripted A from the GameCube controller and lets the title demo run.
- The runtime, with `WIICOMPILED_BENCH=minDraws,segundos,esperaMax`, waits for a frame with at least `minDraws` draws and measures only those frames: p50, p95, p99, maximum and % of slow frames.
- It also prints aurora counters per frame (`aurora_get_frame_waits`): staging wait, *splits*, encoding, acquisition, `Submit`, pacing wait, `Present`, synchronous EFB reads and textures created. It writes `bench_result.txt` and exits.
- It retries if the scene does not appear and only captures the window for failed attempts.
- Measuring timed from outside with screenshots did not work: `PrintWindow` caused stutters and the scene took a variable amount of time to arrive.

**Hang on "NOW LOADING"** (reproduced in 4 of 4 attempts).
- **Threads:** the loader thread (`0x80027A54`) sleeps on a queue woken by a clock thread (`0x80027BF8`, `OSSleepTicks` of 5 ms and then `OSWakeupThread`). The clock's timer had been expired for 67 s and unprocessed: `ProcessSleepTimers` only runs in `SelectThread`.
- **Why it was not processed:** during loading, the main thread paces itself inside `GXCopyDisp` (`VI_HLE_PresentFrame`, a host `sleep`) and never enters the scheduler. The deferred VI, alarm and audio paths leave threads READY but never switch threads.
- **Fix:** patch `wiicompiled-preemption-point`. At the end of `GXCopyDisp`, `OS_HLE_PreemptionPoint()` calls `SelectThread(0)` if the game's interrupts are enabled, the scheduler is enabled and a retrace is not being advanced. It is the return from the hardware VI interrupt. Result: 0 hangs in subsequent runs.

**D3D12 when fielding: from 27 FPS to 59.9.**
- **Diagnosis:**
  - with D3D12 the game thread spent 26% of its time in `NtGdiDdDDIWaitForSynchronizationObjectFromCpu`, plus `CreateAllocation`;
  - the render thread spent 18% in `DestroyAllocation2` (`win32u` exports mapped with `dllexports.py` in the scratchpad);
  - the counter gave 32 textures created per frame, 188×128 "Copy Conv" and "Resolved".
- **Cause:** `GXCopyTex` needs a new texture per copy when the destination has already been sampled in that frame, and MLB does about 17 per frame. The pool only kept 3 per destination, so the rest were created and destroyed every frame. On D3D12 each one goes down to the kernel; on Vulkan it is cheap.
- **Fix:** patch `wiicompiled-copy-texture-pool`, which raises `kCopyTexturePoolPerKey` to 64.
- **Measured:**

| Backend | FPS | p95 | Textures created per frame | Encoding |
|---|---|---|---|---|
| D3D12 (`auto`) | 58.6-59.9 | 17.1 ms | 0.4 | 1.7 ms (previously 8.8) |
| Vulkan | 60 | 17.1 ms | 0.2 | 1.4 ms |

- The patch that gave priority to Vulkan was removed: `auto` goes back to D3D12 and Vulkan is still selectable in `Config.toml`.
- **Other:**
  - patch `wiicompiled-perf-instrumentation`: the counters, the benchmark mode, `AURORA_DAWN_ENABLE_TOGGLES` / `AURORA_DAWN_DISABLE_TOGGLES` and a thread dump that includes the state, suspensions, queue and sleep timer of each `OSThread`;
  - `tools/sample_host_thread.cpp` accepts `RANK`;
  - `tools/wiicompiled-build.ps1` no longer overwrites `Config.toml` on every build, it only updates `dvd_root`.

**Pending:** the `IPCCltInit` HLE uses `r13-26908`, which in MLB coincides with `IPCInit`. The other HLEs that use MKW r13 or r2 offsets remain to be audited. *(Resolved on October 14: see "Mario Kart Wii leftovers in the runtime".)*

## Launcher at startup (October 11, 2026)

- Patch 21, `wiicompiled-launcher`: a pre-game screen in the style of the Power Pros menus. Blue background with rays, glossy pills (JUGAR / AJUSTES / SALIR), outlined title, info box and button hints.
- **No game assets:** everything is drawn with ImGui primitives. The fonts are system fonts (Bahnschrift, Segoe UI Bold and Arial Black on Windows; DejaVu on Linux). If none is available, ImGui's font is used. The default font of the F10 overlay does not change.
- **Settings,** in four tabs:
  - VIDEO: graphics API (only those this build can create), display, internal resolution, force 16:9 and show FPS;
  - AUDIO: volume and mute;
  - CONTROLES: Wii Remote and the keyboard map;
  - INTERFAZ: show at startup.
- **Saving and restarting:** each change is saved to `Config.toml` immediately. When JUGAR is pressed, if anything changed, the executable relaunches itself with `WIICOMPILED_SKIP_LAUNCHER=1`. The parent closes aurora and the process transcript before relaunching.
- **Launcher controls:**
  - Keyboard: arrows or WASD, Enter/Space/J to accept, Esc/K to go back, Q/E to switch tabs.
  - Controller: D-pad or stick, A, B, L1 and R1.
  - Mouse: left and right click.
- **Input only with the window focused:** SDL delivers controllers even when the window has no focus, and in one test that changed settings. `WIICOMPILED_LAUNCHER_UNFOCUSED_INPUT=1` accepts the keyboard without focus, only for test scripts that send keys with PostMessage.
- **It does not appear** with `WIICOMPILED_SKIP_LAUNCHER`, `WIICOMPILED_BENCH`, controller scripts or `[launcher] show_on_startup = false`. It can be re-enabled in F10 with "Show launcher at startup".
- **Test variables:** `WIICOMPILED_LAUNCHER_SELECT=<api>` chooses the API and presses JUGAR; `WIICOMPILED_LAUNCHER_PAGE=settings` opens the settings.
- **Verified with keys sent only to the window:**
  - route AJUSTES → MOSTRAR FPS = No → Esc → JUGAR: saves, relaunches and the child starts the game without FPS;
  - tabs with E;
  - selecting SALIR;
  - `LAUNCHER_SELECT=vulkan`: relaunches and the game runs at 59 FPS.
- **Switch (homebrew):** Dawn has no backend for Horizon. Options: Dawn's GLES backend on devkitPro's mesa (switch-mesa/EGL), or our own aurora backend on deko3d. OpenGL ES is not offered yet because the Dawn package used comes without GL; Dawn has to be built from source. On Switch the system font will have to be loaded (`plGetSharedFont`).

## Setup for players: builds on the player's PC (October 12, 2026)

**User decision:** it is distributed like Wiicompiled, not like skate3recomp. The release ships the Setup and the tools; the game code is translated and compiled on the player's PC from their own ISO. Neither the repository nor the releases contain anything from the game.

**Pieces (`tools/setup/`):**
- `setup.cpp` + `CMakeLists.txt`: the Setup window, with static SDL3 and ImGui (6 MB exe with no DLLs).
  - It uses the same style as the launcher: `power_pros_ui.h`, which is now part of patch 21 and shared by both.
  - Screens: choose disc, install folder, shortcut, progress with bar and log, "¡Listo!" with JUGAR, and error with REINTENTAR and VER REGISTRO.
  - Dragging the ISO onto the exe preselects it.
  - `MLBPP_SETUP_AUTOINSTALL=1` installs without interaction (for tests).
- `LocalBuild.ps1`: what the Setup runs, in this order:
  1. `nodtool info`: checks that the disc is RL8E54.
  2. `nodtool extract`.
  3. Compares the SHA256 of `main.dol` and `ovl_main.rel` with the pinned ones.
  4. Translates with self-contained `Translator.Cli`.
  5. Configures and builds with the functions of Wiicompiled's `NativeBuildFlags.ps1` and the prebuilt aurora and dependencies.
  6. Installs to `<carpeta>\Game` (`MLBPowerPros2008.exe` in portable mode, with `dvd_root` pointing to `<carpeta>\Data`).
  7. Deletes the temporary files.
  - It reports progress with `@@ <porcentaje> <mensaje>` lines and errors with `@@ERROR <mensaje>`.
  - The file is UTF-8 with BOM, because PowerShell 5.1 reads scripts without a BOM as ANSI.
- `Make-Payload.ps1`: assembles the package from a patched and rebound tree, and rejects game files. A release uses `Make-Release.ps1`, which prepares that tree from a clean clone (see below).
  - Its `Launcher\artifacts` come from Wiicompiled's scripts, reused unchanged: `Prepare-PortableTools.ps1`, `Prepare-Dependencies.ps1` and `Prepare-NativePrebuilt.ps1` (the last one builds aurora and the dependencies with our patches).
  - It trims from llvm-mingw the non-x86_64 sysroots, the bundled Python and the Linux runtime: the package goes from 1,150 MB to 839 MB.
- `config/wiicompiled/rl8e54-functions.map` (addresses and names) is committed to the repo. That way the player does not need dtk, DolRecomp or Dolphin's signature database. `tools/wiicompiled-translate.ps1 -RegenerateMap` regenerates it.
- `check_no_game_data.py` accepts this map only if every line is "address name" and if a hexadecimal name matches its own address.

**Measured (12 threads, the user's NKit ISO):**

| Step | Time |
|---|---|
| Extraction | 2 s |
| Translation | 88 s |
| Configuration with the prebuilt | about 20 s (without it: 367 s, due to the SDL checks) |
| Build | 117 steps instead of 814 |
| **Full installation** | **285 s** |

- The clean build without the prebuilt took 274 s; half of the CPU went to dependencies.
- **Verified:**
  - installation without a window and with the Setup window (progress, final screen and temporary files deleted);
  - the installed game opens the launcher and reaches the strap screen at 60 FPS.
- **Window:** the game's title becomes "MLB Power Pros 2008", in `base_product.cpp` inside patch 21.

**Pending:**
- Test on another real PC (done on this one, clean: see below).
- Publish the release (`v0.1.0`, beta, Windows only). The repository has been on GitHub (private) since October 13.
- Linux and Switch.

**Package from a clean clone (October 15):** `tools/setup/Make-Release.ps1 [-Ref <commit>] [-Zip]` does it in one step, using only what is in git:
1. clones the repository into `local\rc` (uncommitted changes are left out);
2. recreates the Wiicompiled tree from the pinned commit with every patch and the rebinding (`wiicompiled-translate.ps1 -Fresh -PrepareOnly`, no game files);
3. runs `Prepare-PortableTools`, `Prepare-Dependencies` and `Prepare-NativePrebuilt` in it, so the aurora prebuilt is never stale;
4. runs the clone's `Make-Payload.ps1`.
- What is not in git (the Wiicompiled and PowerUp+ clones, the .NET 10 SDK, nodtool, caches) comes from `local\vendor`, `local\tools` and `local\cache` through directory junctions; each is pinned by commit or hash. `powerupplus.ps1` now checks out commit `eb1a2c2`.
- **Measured:** about 20 minutes; 902 MB package, 298 MB zip.
- **Clean install verified** with that package, copied to a new folder, the Setup run with a minimal PATH (no dotnet, Python, git or project tools) from the user's ISO:
  - in Spanish and in English: 4.5 and 4.3 minutes end to end, no errors; the Setup window and the progress messages are in the chosen language;
  - the installed game (1.5 GB) opens the launcher and, without it, reaches the KONAMI logo at 60 FPS with D3D12.
- Not tested yet: another PC (another GPU, without the tools installed system-wide) and picking the language with the Setup's pill, the only thing that writes it to `Config.toml`.

## Launcher with MoonInning art (October 12, 2026)

- **Origin of the art:** MoonInning's UI pack, contributed by the user. It is original art, not game content. It comes with two flat 1672x941 mockups with sample texts and states already drawn.
- **Preparation:** `tools/launcher/prepare_mooninning_art.py` erases those parts and generates `assets/launcher/main.png` and `settings.png`:
  - in the menu, the "selected" sparkles next to JUGAR, with a diffusion limited to their yellow pixels, and the info box text;
  - in settings, the tabs, the rows and the help, interpolating from the smoothed edges.
- **What `launcher.cpp` draws on top:** if it finds `launcher/*.png` next to the exe, it fits the art to the window and fills the bars with the same art enlarged and darkened.
  - **Selected button:** redrawn enlarged and with a pulsing glow, with the sparkles next to it.
  - **Other buttons:** dimmed. MODS stays disabled and its line says "Próximamente". *(No longer: MODS works since "Mods", October 12.)*
  - **Info box:** shows the real state (disc, ID, API in use, restart notice).
  - **Settings:** tabs, rows, values and help are drawn with the mockup's colors; the close button is the one from the art.
  - **Without the PNGs:** the previous procedural style is kept (for example on Switch).
- **Installing the PNGs:**
  - `wiicompiled-build.ps1` copies them to `build/launcher`;
  - `Make-Payload.ps1` puts them in the package and `LocalBuild.ps1` installs them to `Game/launcher`.
  - `check_no_game_data.py` only allows binaries in `assets/launcher/*.png`, up to 4 MiB.
- **Verified with keys sent only to the window:**
  - the sparkles follow the selection and MODS shows "Próximamente" (at that time);
  - entering settings and switching tabs work;
  - the bars are filled in a 1920x1009 window.
- **The Setup uses the same art:** `prepare_mooninning_art.py` also generates `setup.png`, which is the menu background with the four buttons blank.
  - The icon and text are erased with a row-wise blend and the arrow is kept. The remains of the ball outline are removed with diffusion.
  - The Setup places its actions by color: orange for the primary action (ELEGIR DISCO, INSTALAR, JUGAR or REINTENTAR), blue for the secondary one (CAMBIAR DISCO, REINSTALAR or VER REGISTRO), green for CARPETA and purple for SALIR or CANCELAR. Unused buttons are dimmed.
  - The info box holds the disc, the folder (trimmed from the left if long), the shortcut checkbox, the progress and the result. The build log appears in a panel on the right.
  - Without `workspace\launcher\setup.png`, the Setup keeps the procedural style.
  - **Verified:** full installation with the Setup window (start, progress and "¡Listo!").

## Mods (October 12, 2026)

- **Patch 22, `wiicompiled-mods`:**
  - `runtime/src/mods.cpp` looks for `<exe>/mods/<carpeta>` (with optional `mod.toml`) and applies the active mods from `[mods] enabled` before starting aurora:
    - `disc/` and `riivolution/` are registered with `RecompMod::RegisterDvdOverlayRoot`, the Wiicompiled mechanism that Retro Rewind already used;
    - `textures/` is passed to aurora as extra replacement folders.
  - aurora: `AuroraConfig.textureReplacementRoots` and `textureReplacementRootCount`. `build_index` indexes the user's folder and then each mod's. This changes the ABI of `AuroraConfig`, so **`native_prebuilt` must be regenerated** (`Prepare-NativePrebuilt.ps1`) before `Make-Payload.ps1`.
  - Launcher: the MODS button (art) or the MODS tab (drawn style) shows a toggle per mod and the ABRIR CARPETA DE MODS row (creates `LEEME.txt`). Each change asks to relaunch the game.
- **Verified:**
  - the texture dump exports 32 textures at boot;
  - a local test mod with the strap screen texture inverted shows up in game with the mod active;
  - the MODS screen activates the mod and saves it in `Config.toml`.
- Guide for mod makers: `docs/en/mods.md`.

## Discord identity and messages (October 12, 2026)

- **Patch 23, `wiicompiled-mlb-identity`:** Discord, the header of the new configuration and the disc, panic, halt and exit errors use `RuntimeProduct::Active().displayName`, whose MLB provider returns `MLB Power Pros 2008`.
- The `RL8E54` profile does not reuse WiiCompiled's Discord application ID. Without its own ID, basic presence does not connect; the inherited ID is also ignored if an old configuration saved it explicitly. Custom IDs and `enabled = false` are preserved.
- The disc path example uses `./DATA`. The technical references to the original Mario Kart addresses and functions, as well as WiiCompiled's licenses and credits, are kept.
- Checks: the 23 patches apply in order on the pinned revision; an isolated runtime test verifies the new configuration, the default/inherited/custom IDs, disabling, and the JSON content of the Discord activity. It does not require connecting to the user's account or playing a match.
- Release build completed. The executable no longer contains the old visible Mario Kart messages. The six runtime files were updated in the Setup's local payload and in its ZIP; their content, absence of duplicate entries and CRC integrity were checked. No release was published and a real Discord connection with an own ID was not tested.

## `project.bin` decoded (October 12, 2026)

- **Format:** header (version 2, sector 0x800, 0x1211 hash slots); 0x1C records at 0x6000 (name offset, sectors, 0x800 alignment, bytes); order table at 0x26000 (record, sector relative to 0x3D800, sorted); names at 0x2B000; data at 0x3D800.
  - Verified on all 2,424 entries: consecutive sizes, exact end at 0x18390000, 2,356 `PACKWII` and 2 `PACKPS2`.
  - rea does not open the file (unsupported format). The analysis was done with Python and with the translated code: `func_800431E8` mounts `/PROJECT.BIN` (0x80155DD0) by calling `func_80059E7C`.
- **`tools/projectbin.py`:** list, extract and build.
  - `build` without replacements reproduces the file byte for byte (same SHA-256).
  - With `PWM_mdl_uniform_MLB_CHC_00.pack` replaced by the NYY one and served as `mods/<x>/disc/project.bin`, the game reads the mod's file (`WIICOMPILED_DEBUG_DVD`) and the Cubs appear in Yankees uniforms in team selection.
- **`WIICOMPILED_DEBUG_DVD=1` plus `projectbin.py list`:** they tell which pack each screen loads. For example `env_title_logo`, `env_mainmenu`, `PWM_mdl_uniform_MLB_<equipo>_<n>` and `env_teamnameNN`.
- **The user's Dolphin texture pack** (2023 logos and uniforms, 206 PNGs): works unchanged as a mod's `textures/`; 32 of its textures are used in the demo.
- **`project.bin` packages in mods (patch 22):** the `project/` folder of each active mod carries `.pack` files by name.
  - `mods::Apply` generates `UserData/Cache/mods/disc/project.bin` with a C++ port of `projectbin.py build` and registers it as a disc overlay.
  - It is rebuilt only if the original or any pack changes (size and date in `project.bin.sources`).
  - **Measured:** 656 ms the first time and reused afterwards. The SHA-256 is identical to the Python tool's, and the Cubs appear in Yankees uniforms.

## Saves per roster (October 12, 2026)

- **Patch 24, `wiicompiled-mod-saves`:** mods that change the roster (`roster/` or `project/game_data.pack`) move the game's save folder to `ModSaves\<mods>`. That way an MLB Life career started with one roster does not get mixed with the normal saves. Details in `docs/en/mods.md`.
- It was found that `CurrentNandDataDir()` stops being valid once the game is running: it reads the game code at 0x80000000, which the game overwrites, and falls back to `RMCP`. The mod redirector takes the title folder from the path. The Riivolution one keeps its behavior.

## Roster editor in the launcher (October 12, 2026)

- **Patch 25, `wiicompiled-roster-editor`:**
  - The MODS screen has two tabs: **MODS** (only the mods, with their toggle) and **HERRAMIENTAS** (open the mods folder and the roster editor). Q/E switch tabs.
  - HERRAMIENTAS → **EDITAR ROSTER (PowerUp+)** starts the PowerUp+ installed in `powerupplus\` in "2008 mode", with the player's `project.bin` and the `mods\` folder, inside a *job object* so that it closes with the game. It opens a local page that waits for the server and then shows it.
  - The launcher's long lists are paged in 6 rows, with "Página 1 de 2" and clickable arrows in the header. Page Up / Page Down change page, and so does moving down from the last row. The mouse wheel moves the selection. Before, with many mods, the rows overflowed the panel.
  - Long help text shrinks its font so it does not overflow the panel.
  - In windows with a different aspect ratio than the art, the edges show a blurred and darkened copy, with a soft shadow. Before, a repeated piece of the art was visible. The Setup does the same, with the blur computed on the CPU.
  - Hooks for tests without a keyboard: `WIICOMPILED_LAUNCHER_ROW`, `WIICOMPILED_LAUNCHER_PAGE=tools` and `WIICOMPILED_LAUNCHER_ACTION=roster-editor`. Keys simulated with `WIICOMPILED_LAUNCHER_UNFOCUSED_INPUT` also read the real keyboard.
- **Setup:** `Make-Payload.ps1` includes compiled PowerUp+ (`tools/powerupplus.ps1 -Publish`, about 113 MB) with its license, the commit and the patch. `LocalBuild.ps1` installs it in `Game\powerupplus\`. The game content check also rejects `game_data.pack` and roster `.dat` files.

## JUGAR without switching windows (October 13, 2026)

- **Patch 26, `wiicompiled-launcher-live-settings`:** before, changing any setting in the launcher made JUGAR close the window and open another process, because the runtime reads the configuration before creating the window.
  - Display, internal resolution, force 16:9, show FPS, volume and mute no longer restart: `main.cpp` and `settings_overlay::InitializeRuntimeSettings` read them again from `Config.toml` after the launcher, just as the F10 menu changes them live. The display mode is applied immediately, in the launcher itself.
  - The graphics API (another renderer is needed), the Wii Remote (SDL hints before its initialization) and the mods (aurora indexes the textures and the disc at startup) still restart.

## Crash in Exhibition → LINEUP (October 13, 2026)

- Error: `missing_jump_target 0x80664C30`, from `func_80663264` (call through a pointer, `bctrl`).
- Cause: at `0x80664C18` there are five consecutive functions that the dtk map had as a single one. `C18` adds 4 and jumps to `C38`; `C20`, `C28` and `C30` are two-instruction *getters* (`lwz`/`addi` + `blr`) that are only called from a *vtable* at `0x80D5183C`. Since they were not in the map, they were not translated as functions.
- Fix: `0x80664C20`, `0x80664C28`, `0x80664C30` and `0x80664C38` added to `config/wiicompiled/rl8e54-functions.map`. The retranslation goes from 48,219 to 48,223 functions.
- The screen was reached with a mod's roster that now shows names (before, the lineup came out empty).
- Pending: there may be more functions like this in other *vtables*. A scan for pointers into the middle of functions found about 450 candidates, but mixed with `switch` tables. Splitting functions at `switch` labels would break the code, so they were not added in bulk.

## GPU load at x4 (October 13, 2026)

- **Patch `wiicompiled-native-efb-snapshot`:** with interpolation disabled, a color copy that cleared the EFB first copied the source to an intermediate texture and then did the requested copy. The encoder already orders the requested copy before the next pass that clears the EFB; that extra snapshot is redundant. It is skipped on the native path and kept with interpolation enabled. The resolution, filtering, effects, mods and game pacing are not changed.
- Cumulative counters `efbSnapshotCopies`/`efbSnapshotBytes` were added to `AuroraFrameWaits`. The real graphics test requires zero extra snapshots on the native path and their preservation with interpolation. It first failed with the old code; then it passed the independent pixel check and the whole existing suite: copies with partial clear, synchronous/asynchronous reads, offscreen, staging splits and worker with interpolation.
- **Local measurement, Radeon RX 5500, Direct3D 12, x4 and same mods/configuration:** average of the last 20 samples of the process's 3D activity, taken roughly every 1.3 s: 75.85% before and 69.60% after. At x1, the reference was 34.15%. These are windows of the game's demo; they are not a comparison of an identical camera nor a guarantee for every match. The load is still considerable.
- The tests use an isolated executable and user data in `local/analysis/gpu-x4`. Logs and CSVs stay there. The updated executable for playing is in `local/wiicompiled-mlb/build/WiiCompiled.exe`; the user's configuration stays at x4 and automatic API.
- The hypothesis of uncapped rendering was not confirmed: the presentation counters indicate approximately 60 presentations/s. The benchmark that filters by 600 draws collected too few frames to interpret its percentiles/FPS as match performance; its logs are kept, but not used for that conclusion.
- The launcher text "4x (1440p)" does not describe Aurora's internal resolution: its EFB workspace includes 640×528 and 16:9 mode can widen it. x4 was not lowered to reach 1440p.
- **Distribution:** this change modifies Aurora and extends `AuroraFrameWaits`; before creating a new Setup, `native_prebuilt` and the payload must be regenerated. The previous Setup/ZIP was not updated in this work.

## Direct match test to measure the GPU (October 13, 2026)

- **Patch `wiicompiled-match-benchmark`:** with `WIICOMPILED_MATCH_BENCH=1`, the runtime skips menus and videos and starts a CPU vs CPU match (Yankees vs Colorado at Coors Field) with the game's original initialization routines, called from the `VIWaitForRetrace` hook. Without that variable the game does not change.
  - On-screen panel: x1 to x4, force 16:9 and F10, FPS, GPU time and "Empezar a medir" / "Detener y guardar" (the measurement lasts as long as the user wants; before, it stopped at 30 s). Below it, and at the start of the `.txt` report, goes the hardware: CPU and threads, RAM, GPU and OS (patch `wiicompiled-benchmark-device-info`). Also live usage, sampled every 0.5 s on a separate thread: game CPU (over all threads, like Task Manager), game RAM (working set), GPU 3D and VRAM (Windows GPU counters, `pdh.dll` loaded on the fly) and system RAM in use. The report stores average and peak; the CSV, one column per value. Checked against the script's monitor: GPU 3D 34.5% vs 34.4% (x1) and 75.6% vs 76.1% (x4). Game RAM is the working set (~0.67 GB); the private memory logged by the script (~5.2 GB) includes memory reserved for the game that is not in use. The changes are temporary: in this mode `Config.toml` is not written.
  - Report in `UserData/Benchmarks`: per-frame CSV (draws, GPU time, CPU encoding, EFB copies and their pixels, reads, presentations) and a summary with FPS and percentiles.
  - GPU time: WebGPU *timestamp* queries around the sealed render stream. It does not include the presenter. If the adapter does not support them, "not available" is reported.
- **Usage:** `tools/benchmark-match.ps1 -Scale 1,4 -Force169 on -Seconds 30` measures without intervention and also saves the process's 3D utilization according to Windows. `tools/benchmark-match.cmd` opens the mode with the panel.
- **Measurement, Radeon RX 5500, Direct3D 12, 16:9, 30 s on the field** (isolated folder `local/analysis/gpu-x4`):

  | | x1 | x4 |
  |---|---|---|
  | FPS | 59.5 | 60.0 |
  | GPU per frame | 3.6 ms | 11.7 ms |
  | 3D utilization (Windows, last 20 samples) | 31% | 73% (maximum 80%) |
  | EFB copies per frame | 34.6 | 34.6 |
  | Pixels copied per frame | 1.4 M | 22.9 M |

  - The game does not draw extra frames: it runs at 60 per second. At x4 the GPU works 70% of each frame. About 35 EFB copies per frame scale with resolution, 16 times more pixels at x4.
  - Both sessions reached the field on their own, with the same teams and stadium, and the draws varied between 208 and 1,012 (the match moves). It is automatic CPU play, not an exact frame-by-frame replay.
- **Pending:** find out how much of the 11.7 ms is those copies and how much is the drawing itself. *(Resolved in "Why x4 loads the GPU so much": almost all of it is drawing.)*

## Why x4 loads the GPU so much (October 13, 2026)

- **Per-pass breakdown** (`WIICOMPILED_GPU_BREAKDOWN=1`, part of `wiicompiled-match-benchmark`): a timestamp at the start and end of each render pass. The time inside the pass is drawing; the gap until the next one is the EFB copy or conversion that separates them. Every 300 frames it writes the per-frame average and the most expensive passes to the log.
- **Result in the match, RX 5500, D3D12, 16:9:**

  | | x1 | Según la ventana (1920×1009) | x4 |
  |---|---|---|---|
  | Internal resolution | 939×528 | 1794×1009 | 3755×2112 |
  | GPU per frame | 4.0 ms | 8.8 ms | 12.3 ms |
  | Drawing | 2.7 ms | 8.0 ms | 11.1 ms |
  | EFB copies and conversions (~32) | 0.9 ms | 1.0 ms | 1.1 ms |
  | 3D utilization (Windows) | 35% | 60% | 78% |

- **Cause:** x4 is not 1440p. The EFB has 528 lines and in 16:9 it widens to 939, so x4 draws 3755×2112: 7.9 million pixels, almost 4K and 2.1 times 1440p. It is then scaled down to the window. 90% of the time is drawing; the EFB copies weigh little.
- On the loading screens, three passes with full-screen effects (15, 7 and 3 draws) cost 9-10 ms at x4 and 0.75 ms at x1: 13 times more for 15 times more pixels. It scales with the pixels.
- The EFB formats are normal (8-bit color, 32-bit depth) and MSAA is disabled.
- Windows' percentage and the milliseconds are not proportional to the work: the GPU lowers its clock when it has time to spare (which is why "Según la ventana" does not lower the percentage as much). Temperature depends on power, not on the percentage.
- **Fix, patch `wiicompiled-resolution-labels`:** the launcher said "2x (720p)", "3x (1080p)" and "4x (1440p)". It now says "2x (~1080p)", "3x (~1600p)" and "4x (~4K)", and the help recommends "Según la ventana", which draws at the window's resolution.
- `tools/benchmark-match.ps1 -Scale 0` measures the "Según la ventana" mode.

## Audio with crackles and dropouts (October 13, 2026)

- **Symptoms:** crackles, commentators cutting out and menu audio that sometimes starts late.
- **Measurement** (`WIICOMPILED_AUDIO_STATS=1`, every 2 s: blocks produced, gaps between ticks, SDL queue level, drops and underruns), in the 60 s direct match:
  - The game produces exactly the audio it should (2 s every 2 s, 3 ms blocks at 32 kHz).
  - The game thread stalls for 40-93 ms during loads and moments of heavy work. During those stalls the queue emptied almost completely (28 bytes, 0.2 ms): an audible dropout.
  - After each stall the game catches up all at once and that delay accumulated (the queue went from ~30 to ~105 ms). On reaching the 120 ms limit, blocks were dropped: the "output queue full" warning appears in 38 sessions.
  - At startup, the queue started almost empty.
- **Fix, patch `wiicompiled-audio-queue`** (`AudioBackend`):
  - The device is opened paused and starts playing when there are 80 ms queued.
  - To keep the queue near 80 ms, the playback speed is adjusted by at most ±2% with `SDL_SetAudioStreamFrequencyRatio`, without cutting audio. In the test it moved between 0.998 and 1.002.
  - The limit goes from 120 to 250 ms: it only curbs runaway growth.
  - If the queue empties, it pauses until it has 80 ms again: a short silence instead of crackles.
- **Result, same test:** the queue stays at 65-80 ms. During the match's 40-50 ms stalls it does not go below 16-40 ms. There are no drops. Only an 88 ms stall when loading the match left it almost empty.
- It was not checked by ear at that time. *(Confirmed later: see the end of "Commentator crackles".)*

## Commentator crackles (October 13, 2026)

- After the queue fix, the crackle remained in the commentator's voice, especially when reading the lineup and the names. The queue no longer emptied: the defect was in the mix.
- **Measurements** (local diagnostics: `WIICOMPILED_AUDIO_DUMP=archivo` records the output; the per-voice dump and the voice event trace are not kept):
  - In 119 s of a match there are about 27 sharp jumps in the output. Without the mixing thread there are the same number (28), so it is not a race. With a single block per tick too (29), so it is not the recovery after stalls.
  - The commentator uses two *stream* voices (`stream=1`) with two alternating buffers, one per phrase or name. The data of each buffer does not change while it plays: the disc loading is fine.
  - The game stops each phrase with `running=0`, 2-4 frames (6-12 ms) after its data runs out. Meanwhile the ADPCM decoder reads zeros and stays at the last level (for example 16614); when stopped, it drops abruptly to 0. In one of the two voices this happened 38 times.
  - The commentator voices reach ±32767 (113 and 57 times); the music does not. It may be the game's original mix: a reference is missing to know.
- **Patch `wiicompiled-audio-depop`:** the mixer stored the last value of each voice per bus (`dpop`) but did not use it, just like Dolphin. Now, when the game stops a voice that was playing, that value ramps down to zero over one frame (3 ms) instead of being cut. The `WIICOMPILED_AUDIO_DUMP` dump is included for future checks.
- It could not be shown with numbers that what is heard disappears: in the mixed output, the crowd and the music mask that pattern. It still needs to be listened to, and the commentator's saturation compared with Dolphin.
- **Follow-up, same day:** with the depop, the crackle could still be heard in the direct test. More was ruled out (local diagnostics, not kept):
  - **Real output:** what reaches the device was captured with `SDL_SetAudioPostmixCallback` (44.1 kHz, 8 channels). With speed control, 15 jumps in 65 s; without it, 18: the same rate as before SDL. There are no silences. SDL and the speed control add nothing.
  - **Jumps inside the voice:** they all fall in contiguous data, with no address changes. The voice is at 22,050 Hz (`ratio 0xb066`) and is converted to 32 kHz with linear interpolation (`src_type 1`), like the DSP. Many "jumps" are speech consonants.
  - **Saturation:** 12 raw commentator clips were saved with their coefficients and decoded separately with the standard DSP-ADPCM algorithm. 11 go out of range on their own (10-390 samples each). It is in the game's data, not in our decoder. The final output does not saturate (peak 26,521).
  - Pending: compare by ear with Dolphin or with the console, to know whether what is heard is how the original sounds.
- **Cause found (reverse engineering), patch `wiicompiled-periodic-sleep`:** the user's Dolphin recording has 3.1 jumps/min; ours, 13-15. The game stopped each commentator phrase late (factor 1.2-1.7), so the voice played the zero padding and the start of the next phrase from its buffer, and was then cut.
  - Each phrase's clock is in the stream object: the phrase header carries its duration in ms (`+0x10`), `func_80039B94` copies it to `[obj+0x874]`, and `func_8003A248` adds 5 to `[obj+0x858]` on each iteration of the sound thread; on reaching duration − 10 the phrase is stopped.
  - The sound thread (`0x80027A54`, priority 4) does one iteration per `OSWakeupThread` from a timer thread (`0x80027BF8`, priority 3) that does `OSSleepTicks(5 ms)` in a loop. On the console that is 200 iterations per second.
  - Measured in our runtime: 110-170 sleeps of 5 ms per second. The game threads are cooperative fibers and the timer is only serviced when some thread reaches the scheduler, milliseconds late; each sleep counted from "now" and that delay was lost.
  - Fix: a repeated `OSSleepTicks` with the same duration is anchored to the previous deadline (maximum 50 ms of catch-up); other sleeps start from now, as in the SDK. The delay is recovered while the other threads wait. The anchored deadline is never less than a quarter of the period from now: an already expired deadline could fire before the thread finishes parking (there was a hang at boot in 1 of ~12 tests without that limit; with it, 9 of 9 fine and 2.5 jumps/min).
  - **Start of the match:** the user still heard crackles briefly at the beginning, when the players go to their positions. Measured second by second: the timer dropped to 130-180 wakeups/s between seconds 34 and 47, because the main thread computes for up to 17-20 ms straight (a whole frame) without yielding. In addition, the quarter-period limit moved the anchor and threw away the accumulated debt.
  - Fix: (1) the logical anchor keeps the full debt; only the real timer respects the quarter period. (2) When the game code re-enables interrupts (`OSRestoreInterrupts`/`OSEnableInterrupts`) and there is an expired timer, it yields right there, as the console does when it takes the pending interrupt (`OS_HLE_PreemptionPoint`, the same mechanism `GXCopyDisp` already uses). Only in calls from the game; the runtime's C++ keeps using the normal versions. An atomic holding the nearest deadline avoids locking on every call.
  - Result: 200-203 wakeups/s all the time (only ~15 are lost in the second when the field loads), 0 crackles in 69 s, 1.5/min in 90 s with none at the start, 60 FPS at x1 and x4, 8 of 8 boots fine.
  - Result, same direct match: 200-201 sleeps/s, **3.0-3.4 jumps/min** (Dolphin 3.1), 59.96 FPS at x1.
  - **Confirmed by ear** by the user in normal play on October 13, 2026: the commentator crackle can no longer be heard. The comparison with Dolphin of the data's own saturation has still not been done.

## Texture Studio (October 14, 2026)

- **Power Pros Wii 2008 Texture Studio** (`tools/texture-studio`, guide in `docs/en/texture-studio.md`): a separate app for creating texture mods. It reads the player's `project.bin`, shows the 9,657 distinct textures by category, exports them to PNG and imports the edited ones (also in HD) as `tex1_…` in `mods/<mod>/textures/`.
- **Integration:** the launcher opens it from HERRAMIENTAS → EDITOR DE TEXTURAS (patch `wiicompiled-texture-studio`), the Setup installs it alongside the game and `wiicompiled-build.ps1` places it next to `WiiCompiled.exe`.
- **In-game capture:** `WIICOMPILED_TEXTURE_DUMPS=1` enables texture dumping for that run only, without touching `Config.toml`.
- **Verified in game:** the main menu label atlas and two Coors Field textures (fences and scoreboard) scaled ×4 with the Studio show up in the menu and during a match.
- **Reverse engineering:** the texture headers in the `.pack` files store the height before the width (see `docs/en/reverse-engineering.md`, section 5).

## Mario Kart Wii leftovers in the runtime (October 14, 2026)

Audit of the whole runtime looking for Mario Kart Wii addresses, `r13`/`r2` offsets and identity that are still active in MLB.

- **Fixed (patch `wiicompiled-mlb-disc-identity` and `config/wiicompiled/rl8e54-data.csv`):**
  - `RFLiIsWorking` read the Mii manager pointer at `0x80386298` (Mario Kart; free memory in MLB). An `ignore` row in the CSV won over the correct `range` row; it was removed and it now reads `0x8031B168`.
  - The disc was identified with Mario Kart's maker "01" and disc 1 over MLB's `0x35340001`; and if `0x80000000` did not have a valid code, NAND, ES and Riivolution fell back to "RMCP". They now use `GameProfile::kDiscGameCode` and `kDiscMakerId`.
  - Deferred network calls used Mario Kart's default thread (`0x80347498`); now MLB's (`0x802B7078`).
  - `IPCCltInit` did not set the "already initialized" flag (`r13-26876`): a second call would have advanced the buffer again. It now checks and sets it, like the original.
- **Checked:** 8 of 8 boots without crashing, `IPCCltInit` only once, NAND only in `title/00010000/524c3845`, the automatic match gets to play and the launcher tests pass. Previously 2 silent crashes had been seen in 9 boots; whether the Mii pointer was the cause is not proven.
- **No effect in MLB** (code not bound or disabled): audio mixing, ISFS, EGG/GX, SC, ES, Mario Kart system threads, Retro Rewind mod loader.
- **Reviewed afterwards, same day:**
  - **IOS:** the disc's TMD asks for IOS33 (`0x21`); the runtime declared IOS36. Now `GameProfile::kIosVersion` gives `0x00210E18` (Dolphin's value for IOS33; same date and memory as IOS36). The game confirms it at boot: "Firmware: 33.14.24" (previously 36.14.24).
  - **Widescreen:** MLB supports it natively: it queries `SCGetAspectRatio` (`0x800D3374`) in 5 places, 2 of them in its REL, and in 16:9 round objects stay round. 16:9 is kept as the default. *(Correction, October 17: it was not being turned on; see "Native widescreen" in `docs/en/reverse-engineering.md`. It is now, with the patch `wiicompiled-mlb-widescreen`.)*
  - **Volumes:** the music, effects, interface and voice volumes in the on-screen menu depend on the Mario Kart sound manager and do nothing in MLB. For MLB they are hidden and only the master volume remains (the launcher already offered only that one).
  - **Frames:** MLB runs at 60 FPS natively: each 16.7 ms frame carries its full drawing (346-357 calls, none empty) and the logic runs at normal speed; the runtime does not interpolate (`frame_interpolation_fps = 0`). *(October 17: it is now optional, "Frame interpolation" in the launcher. Measured in the test match at 1x on an RX 5500: at 120 it presents 119.6 FPS with the game at 60, and the GPU goes from 3.1 to 7.6 ms per game frame (29% to 54%); at 180, 10.8 ms (65%). `tools/benchmark-match.ps1 -Interpolation 120|180`; at 180 its present counter reads 0 although the screen shows 180.)*

## Graphics options for modest hardware (October 14, 2026)

Patch `wiicompiled-graphics-options` (aurora and runtime). VIDEO tab of the launcher; also in the F10 menu.

- **ESCALADO = AMD FSR 1.** Ported to WGSL from the algorithm published by AMD (MIT): EASU (12 samples, edge direction, no halos) and RCAS (adaptive sharpening, 0.2 stops). It runs in the final presentation, at the same point as the Mac MetalFX scaler, and only acts if the game image is smaller than the window. Checked in the menu at x1 in a 1920×1009 window: letter edges clearly sharper than bilinear scaling.
- **FILTRADO DE TEXTURAS** (Original, 2×, 4×, 8×, 16×). Aurora only applied anisotropic filtering to textures that request it in GX (`GX_ANISO_2/4`); "Original" stays that way. A forced level is applied to every texture whose filters allow it, like Dolphin's "force anisotropic". In a match, the dirt and grass seen at an angle gain detail.
- **CALIDAD:** Baja (1x + FSR 1 + original filtering), Media (2x + FSR 1 + 4×), Alta (Según la ventana + 16×) or Personalizada.
- **Measured in a match** (RX 5500, Direct3D 12, 1920×1009 window): x1 2.88 ms of GPU (32%); x1 + FSR 1 + 16× 4.92 ms (40%); x2 + 16× 7.54 ms (51%).
- **Ruled out: frame skipping.** MLB makes about 35 EFB copies per frame (it draws to textures it uses later); not drawing a frame would leave those textures stale or empty. FSR 1 with a low internal resolution gives the savings without that risk. Dropping to 30 FPS is not an option either: the game logic runs at 60 Hz and would go in slow motion.
- **F10 menu:** "Disable bloom" was hidden outside Mario Kart Wii (it pointed to Mario Kart code that does not exist in MLB).

## Generated mipmaps and antialiasing (18 October 2026)

Patch `wiicompiled-auto-mipmaps` (aurora and runtime). Launcher VIDEO tab (page 2): **MIPMAPS** (Yes by default) and **ANTIALIASING** (No, MSAA 2x, 4x, 8x). Mipmaps also in F10.

- **Problem:** stadium textures ship without mipmaps. The 47 packs hold 8,135 CI4, 3,130 CI8, 77 RGBA8 and 33 RGB5A3 textures, all single-level. From afar each screen pixel picks a lone texel: crowd and signs look noisy and shimmer when the camera moves. Forced anisotropy did not act on them either: aurora turns it off when sampling uses no mipmaps.
- **Fix:** when aurora creates a static RGBA texture without mipmaps (palette textures too, which aurora already converts to RGBA on the CPU), it builds the full chain (`lib/gfx/auto_mips.hpp`) and samples it trilinearly if the game asked for `GX_LINEAR`; `GX_NEAR` stays unfiltered.
  - Colour is averaged weighted by alpha, so transparent black texels don't darken edges.
  - On cut-out textures (crowd, trees, flags: nearly all opaque or transparent) each level's alpha is rescaled to keep level 0's alpha-test coverage; without it the distant crowd thins out and disappears.
  - LOD bias of -0.5 for those textures: with plain averaging the signs went soft.
- **Checked in the test match** (Coors Field, RX 5500, Direct3D 12, 1.5x, 16x filtering): the crowd and distant stands look clean, signs stay readable. GPU 4.78 ms with and without mipmaps; each texture's memory grows by a third.
- **MSAA:** aurora already supported it (`AuroraConfig::msaa`); it now comes from `[video] msaa`. MSAA 4x draws correctly, EFB copies included, and smooths railings and lines, but raises the GPU from 4.78 to 9.11 ms. That is why it ships off. It needs a game restart.
- **Test:** `runtime/tests/auto_mips_tests.cpp` (checkerboard to grey, red edge not darkened, sparse crowd that doesn't vanish).
