# Traspaso — port de Switch (rama `switch-arm64`), 2026-10-28

Estado para la próxima sesión o IA. Se sobrescribe al cerrar cada tanda de trabajo; el historial está en `git log -p docs/traspaso.md`.

## Dónde estamos

- **Rendimiento:** 59.9 fps a reloj stock en modo portátil (CPU 1020 / GPU 307 MHz) con el renderer deko3d, que ya es el predeterminado. Benchmark de 90 s: p50 16.67 ms, p95 ~17.1 ms.
- **Audio:** resuelto. El narrador va a tiempo (ticker de 5 ms a 200 despertares/s, 0 pasadas perdidas) y no hay huecos de salida aunque caigan los fps. El usuario lo confirmó de oído: "el audio va perfecto, ni lo tocaría".
- **Lo pendiente:** bajones de **un frame** (30–36 ms) al fildear y en la toma inicial que muestra a todos los jugadores. El usuario observa que *cuantos más fildeadores y corredores hay en pantalla, más caen los fps* (CPU ~80% al lanzar o batear, ~90–92% al fildear). Prueba 23: 39 picos de más de 30 ms en 16 570 frames de partido.

## Siguiente paso (acordado, todavía sin lanzar)

**Medir el costo por jugador en el fildeo** y atacar lo que domine con cambios exactos. El agente P10 se detuvo antes de cambiar nada; el árbol de build está limpio. Datos ya capturados:

- **Picos del usuario jugando:** `local/switch/logs/nxlink-run23.txt` (los del partido son los frames > 5000; símbolos en `elf-syms-run23.txt`). Analizar con:
  `python local/tmp/cpu/S8-test/prof_spikes.py local/switch/logs/elf-syms-run23.txt local/switch/logs/nxlink-run23.txt [--frames a,b] [--quiet]`
- **Perfil completo del benchmark:** `local/switch/logs/nxlink-run12.txt` (símbolos `elf-syms-run12.txt`). El script `prof2.py` (copia en el scratchpad de la sesión; también está `local/tmp/cpu/S8-test/prof_spikes.py`) acepta el argumento `late` para ver solo los frames que pasan de 17 ms.

Candidatos por jugador:
- matrices de huesos (`PSMTXConcat`, ya nativa);
- UV del reflejo por vértice `0x8000D770` (ya nativa, pero escala con los modelos);
- listas de dibujo y texturas por parte del modelo;
- IA y física de los fildeadores (código del juego: ingeniería inversa puntual + nativa bit-exacta, como `envmap_natives.cpp` y `reverb_natives.cpp`).

Otras palancas medidas:
- **Lecturas del disco en pleno juego:** voces del narrador, `sound/voiceN.bin`, de 2–3 ms cada una en el hilo del juego. Se podrían leer en trozos o en otro hilo.
- **Espera al hilo GX:** core 1, ~15% del tiempo de los picos.

## Cómo se trabaja (flujo probado)

- **Patch de Switch:** se genera desde `local/tmp/switch_edit.py`, con fases 1–55 de `sub()` / `write()`.
  - `bash local/tmp/mkswitch.sh` (desde MSYS2) regenera `tools/patches/wiicompiled-switch.patch` y `-2.patch` (dos archivos por el límite de 512 KiB del repo) y valida la cadena sobre un árbol limpio con rebind.
  - `bash local/tmp/treecheck.sh` + `diff -rq local/switch/work/treecheck/runtime local/switch/work/mlb/src/runtime`: el árbol de build debe coincidir 100% con el que generan los patches. Hacerlo antes de cada commit.
- **Agentes:** el brief compartido está en `local/tmp/cpu/BRIEF.md`.
  - Los agentes editan el árbol de build `local/switch/work/mlb/src` y dejan un snippet en `local/tmp/cpu/<stream>.py`, con `FOLD_N` para el texto de fases anteriores.
  - El líder integra el snippet como fase nueva al final de `switch_edit.py` y aplica los folds.
  - **Los agentes nunca compilan el juego completo** (la PC se queda sin RAM).
- **Compilar:** `MLB_SWITCH_JOBS=4 bash tools/switch/build-nro.sh --graphics vulkan`.
  - Con **ccache** (caché en `local/switch/ccache`), los cambios en `.cpp` tardan 2–4 min.
  - Tocar `runtime/include/abi_bridge.h` u otro header que incluya el código traducido recompila todo (~30 min).
- **Probar en la Switch:** el usuario pone el netloader (botón Y en hbmenu, IP 192.168.0.105) y se lanza:
  `python tools/switch/nxlink.py 192.168.0.105 local/switch/build/MLBPowerPros2008.nro --wait 300 <variables>`
  - Benchmark: `WIICOMPILED_MATCH_BENCH=1 WIICOMPILED_MATCH_SECONDS=90`.
  - Diagnóstico de audio: `WIICOMPILED_AUDIO_STATS=1 WIICOMPILED_SOUND_TICK_STATS=1`.
  - Perfil: `WIICOMPILED_SWITCH_PROFILE=1`.
  - Picos: `WIICOMPILED_SWITCH_SPIKES=30 WIICOMPILED_SWITCH_SPIKES_MAX=256` (sirve también jugando normal).
  - Archivos: `WIICOMPILED_SWITCH_IO_LOG=1`.
  - **Revisar siempre** `cpu_mhz` / `gpu_mhz` al inicio y al final: el usuario a veces tiene sys-clk con overclock.

## Interruptores de lo nuevo (todos activos por defecto; `=0` los apaga)

| Variable | Qué desactiva | Fase |
|---|---|---|
| `WIICOMPILED_COMPUTE_PREEMPT` | Puntos de revisión seguros en `PSMTXConcat`/`sqrt`/`fill_mem` | 51 |
| `WIICOMPILED_AI_PREEMPT` | Audio atendido cada 3 ms en esos puntos | 55 |
| `WIICOMPILED_AX_SPLIT` | Mezcla de audio repartida entre núcleos | 53 |
| `WIICOMPILED_GX_G7` | Atajos de texturas | 52 |
| `WIICOMPILED_GX_G6` | Listas de dibujo por referencia | 45 |
| `WIICOMPILED_GX_A3` | Ahorros GX de la ronda 3 | 37 |
| `WIICOMPILED_DK_TEV_FAST` / `_TEV_UNIT` / `_HW_CLEARS` / `_TILED_CACHE` | GPU deko3d | deko |

Además: `WIICOMPILED_SWITCH_RENDERER=vulkan` vuelve a Dawn/Vulkan, y `WIICOMPILED_IDLE_RETRACE=1` es opcional y sin probar.

## Trampas aprendidas

- **Heredocs:** en la herramienta Bash, un heredoc convierte `\\n` en un salto de línea real. Para texto con `\n` usar Edit, o `chr(92)` en Python.
- **Hilos en Horizon:** `std::thread::detach()` lanza una excepción y el juego cierra. Usar `threadCreate` de libnx.
- **Preempción:** no hacerla en la entrada de `InvokeIndirectCpu` (SelectThread pisa r3/r4 de la llamada en curso) ni dentro de comandos GX (corrompe el estado). Solo en llamadas `bl` del juego a nativas hoja, con el contexto completo guardado.
- **Caché de pipelines:** uno corrupto en la SD (`UserData/Cache/pipeline_cache.db`) puede tumbar cada arranque. La fase 50 valida las entradas.
- **Script de build:** no editar `build-nro.sh` mientras compila, porque bash lo lee en vivo. Tampoco pasar `-DCMAKE_*_COMPILER_LAUNCHER=` vacío: recompila todo.
- **Subagentes:** un subagente ejecutó `switch_edit.py` contra el árbol de build por accidente. `treecheck.sh` lo detecta.
- **Revertir un header global:** tarda 30 min sin ccache; con ccache, minutos.
