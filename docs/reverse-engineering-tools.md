# Herramientas de ingeniería inversa

*English version: [en/reverse-engineering-tools.md](en/reverse-engineering-tools.md)*

## Instaladas (portables, en `local/tools/`)

| Componente | Versión | Ubicación |
|---|---|---|
| Ghidra | 12.1.4 | `local/tools/ghidra_12.1.4_PUBLIC/` |
| Java JDK x64, Eclipse Temurin | 21.0.12.1+1 | `local/tools/jdk-21.0.12.1+1/` |
| dtk (decomp-toolkit) | 1.8.4 | `local/tools`, vía `tools/bootstrap.ps1` |

Las descargas proceden de las publicaciones oficiales de [Ghidra 12.1.4](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build) y [Temurin 21.0.12.1+1](https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1), verificadas por SHA256:

- Ghidra ZIP: `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
- JDK ZIP: `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e`.

REA se usa por MCP y solo admite ejecutables PE x86/x64 (útil para los binarios del host, no para el juego). Rechaza el DOL y el REL PowerPC.

## PowerPC del juego

- Nombres: `config/wiicompiled/rl8e54-functions.map` (inventario de dtk más las funciones del SDK identificadas por `tools/identify_sdk_functions.py` y `tools/align_sdk_by_anchors.py`).
- `tools/dol_to_elf.py DOL OUT_ELF [MAP_TXT]` envuelve un DOL como ELF PowerPC big-endian con los símbolos del mapa, para Ghidra u `objdump`. Ghidra no trae un cargador de DOL.
- `tools/function_data_refs.py MAP_TXT MAIN_DOL LINKED_REL_DOL NAME_OR_ADDR...` lista las direcciones de datos que lee o escribe una función.
- `tools/ghidra/PowerProsFunctionProbe.java` desensambla y descompila una función importada como bytes sueltos, y antes comprueba que los bytes cargados coincidan con los extraídos:

```powershell
local\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat <proyecto> <nombre> -import <bytes.bin> `
  -loader BinaryLoader -loader-baseAddr <direccion> -processor 'PowerPC:BE:32:default' -noanalysis `
  -scriptPath tools\ghidra -postScript PowerProsFunctionProbe.java <direccion> <nombre> <bytes.bin> <salida> -deleteProject
```

- `tools/ghidra/PowerProsBatchDecompile.java` saca el pseudo-C de muchas funciones a la vez desde un volcado de MEM1 (el REL ya está reubicado, así que valen las direcciones del juego en marcha). Recibe una lista de `0xDIRECCIÓN nombre` y líneas `#range 0xDESDE 0xHASTA`: sigue las llamadas que caen en esos rangos y descompila también esas funciones (dtk fusiona algunas). Pone `r13`/`r2` del área de datos pequeños. Importar el volcado con `-loader BinaryLoader -loader-baseAddr 0x80000000 -processor PowerPC:BE:32:default -noanalysis` y pasar `<lista> <salida.c>`. Se usó para el framework de pantallas y Arrange (`docs/ingenieria-inversa.md`, «Framework de pantallas y menús de Arrange»); unas 175 de 4.250 funciones quedan a medias por las paired-single: para esas, `local/wiicompiled-mlb/generated/functions/func_X.cpp`.

Límite conocido: Ghidra 12.1.4 no trae una variante Gekko/Broadway (`ppc.ldefs` solo tiene `PowerPC:BE:32:default` y otras), así que las instrucciones paired-single (`psq_l`, `ps_*`) no se decodifican. El pseudocódigo es una inferencia del decompilador, no el código fuente original.

La prueba inicial (`memcpy` a `0x8000626C`, 668 bytes, 167 instrucciones) y su script de lanzamiento están en el tag de git `moderngekko-era`; la evidencia privada, en `local/analysis/ghidra-ppc-memcpy/`.
