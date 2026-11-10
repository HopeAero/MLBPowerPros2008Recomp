# Reverse engineering tools

*Versión en español: [../reverse-engineering-tools.md](../reverse-engineering-tools.md)*

## Installed (portable, in `local/tools/`)

| Component | Version | Location |
|---|---|---|
| Ghidra | 12.1.4 | `local/tools/ghidra_12.1.4_PUBLIC/` |
| Java JDK x64, Eclipse Temurin | 21.0.12.1+1 | `local/tools/jdk-21.0.12.1+1/` |
| dtk (decomp-toolkit) | 1.8.4 | `local/tools`, via `tools/bootstrap.ps1` |

The downloads come from the official releases of [Ghidra 12.1.4](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build) and [Temurin 21.0.12.1+1](https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1), verified by SHA256:

- Ghidra ZIP: `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
- JDK ZIP: `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e`.

REA is used through MCP and only supports x86/x64 PE executables (useful for the host binaries, not for the game). It rejects the PowerPC DOL and REL.

## The game's PowerPC

- Names: `config/wiicompiled/rl8e54-functions.map` (dtk inventory plus the SDK functions identified by `tools/identify_sdk_functions.py` and `tools/align_sdk_by_anchors.py`).
- `tools/dol_to_elf.py DOL OUT_ELF [MAP_TXT]` wraps a DOL as a big-endian PowerPC ELF with the map's symbols, for Ghidra or `objdump`. Ghidra doesn't ship a DOL loader.
- `tools/function_data_refs.py MAP_TXT MAIN_DOL LINKED_REL_DOL NAME_OR_ADDR...` lists the data addresses a function reads or writes.
- `tools/ghidra/PowerProsFunctionProbe.java` disassembles and decompiles a function imported as raw bytes, and first checks that the loaded bytes match the extracted ones:

```powershell
local\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat <project> <name> -import <bytes.bin> `
  -loader BinaryLoader -loader-baseAddr <address> -processor 'PowerPC:BE:32:default' -noanalysis `
  -scriptPath tools\ghidra -postScript PowerProsFunctionProbe.java <address> <name> <bytes.bin> <output> -deleteProject
```

- `tools/ghidra/PowerProsBatchDecompile.java` produces pseudo-C for many functions at once from a MEM1 dump (the REL is already relocated, so running-game addresses apply). It takes a list of `0xADDRESS name` plus `#range 0xFROM 0xTO` lines: it follows calls landing in those ranges and decompiles those functions too (dtk merges some). It sets the small-data `r13`/`r2`. Import the dump with `-loader BinaryLoader -loader-baseAddr 0x80000000 -processor PowerPC:BE:32:default -noanalysis` and pass `<list> <output.c>`. Used for the screen framework and Arrange (`docs/en/reverse-engineering.md`, "Screen framework and Arrange menus"); about 175 of 4,250 functions come out truncated because of paired-single instructions: for those, use `local/wiicompiled-mlb/generated/functions/func_X.cpp`.

Known limitation: Ghidra 12.1.4 doesn't ship a Gekko/Broadway variant (`ppc.ldefs` only has `PowerPC:BE:32:default` and others), so paired-single instructions (`psq_l`, `ps_*`) are not decoded. The pseudocode is an inference by the decompiler, not the original source code.

The initial test (`memcpy` at `0x8000626C`, 668 bytes, 167 instructions) and its launch script are in the git tag `moderngekko-era`; the private evidence, in `local/analysis/ghidra-ppc-memcpy/`.
