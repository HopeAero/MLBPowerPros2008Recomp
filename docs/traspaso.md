# Traspaso — port de Switch (rama `switch-arm64`), 2026-10-28

Estado para la próxima sesión o IA. Se sobrescribe al cerrar cada tanda de trabajo; el historial está en `git log -p docs/traspaso.md`.

## Dónde estamos

- **Rendimiento:** 59.9 fps a reloj stock en modo portátil (CPU 1020 / GPU 307 MHz) con el renderer deko3d, que ya es el predeterminado. Benchmark de 90 s: p50 16.67 ms, p95 ~17.1 ms.
- **Audio:** resuelto. El narrador va a tiempo (ticker de 5 ms a 200 despertares/s, 0 pasadas perdidas) y no hay huecos de salida aunque caigan los fps. El usuario lo confirmó de oído: "el audio va perfecto, ni lo tocaría".
- **Lo pendiente:** bajones de **un frame** (30–36 ms) al fildear y en la toma inicial que muestra a todos los jugadores. El usuario observa que *cuantos más fildeadores y corredores hay en pantalla, más caen los fps* (CPU ~80% al lanzar o batear, ~90–92% al fildear). Prueba 23: 39 picos de más de 30 ms en 16 570 frames de partido.

## Tanda P11 (2026-10-28): probada en consola; pico GX pendiente

- Fases **56–58** de `local/tmp/switch_edit.py`, integradas en el texto de las fases 22/45/41 para respetar el orden de generación:
  - **56:** FIFO publica el segmento que espera el productor; el worker solo avisa cuando lo alcanza. Mantiene las barreras SC, el mutex y el callback cada 1 ms. Ensayo con código real: 512 segmentos pendientes, 512 avisos → 1; 40 ejecuciones de estrés pasan, con presión del ring, esperas anidadas y visibilidad de memoria.
  - **57:** `GXSetVtxDescRun` deja de comparar cada atributo cuando ya está forzado VCD; reconstruye los bits de normales una vez al final. 200.000 secuencias ordinarias y 500.000 mixtas iguales a las llamadas originales (estado, sondas y contadores); G6/A3 siguen sirviendo para desactivarlo.
  - **58:** handles de `ReadExact` sin búfer de stdio. La biblioteca newlib instalada demuestra rellenos auxiliares de `fseeko`/`fread`; no hay caché de bytes ni cambios de DMA/callbacks. 1.067 comprobaciones pasan. La lectura física sigue siendo síncrona; vigilar el coste de lecturas pequeñas.
- **Sin nativa nueva:** prototipo de cuaterniones `0x806EEDF4` idéntico en 200.000 casos de qemu, pero su ahorro estimado (~0,024 ms/frame) no justifica integrarlo para esta tarea. Queda solo en `local/tmp/cpu/P11-native-test/`.
- Análisis: `local/tmp/cpu/P11-profile-report.md` y `P11-profile-fifo-review.md`. Run23 >5000: 39 picos, 1.270 muestras; espera GX de fin de frame 26,2 %, GX propio 20,9 %, código traducido propio 26,5 %. No hay recuentos de jugadores para calcular ms/jugador. Run23 no imprimió relojes; run12 sí confirma CPU 1020/GPU 307 al inicio y al final. `gx_fifo_decode_ms` mide tiempo transcurrido, incluidas esperas; no CPU pura del worker.
- **Dos benchmarks de 90 s completados en consola**, portátil, CPU 1020/GPU 307/EMC 1331 al inicio y al final. Anterior: 59,3913 FPS, p95 18,6312 ms, p99 22,5669 ms, espera FIFO 0,309 ms/frame; P11: 59,8245 FPS, p95 17,1506 ms, p99 19,6526 ms, espera FIFO 0,187 ms/frame. Sin errores GX. **No es una prueba causal de ahorro:** el autoplay no repite cada frame; la anterior entró en escenas más pesadas al final (73 frames GPU >16 ms frente a 1 en P11). El mismo pico del frame 4115 persiste, 33,1 → 32,5 ms, con 19 muestras de espera GX en ambas y sin E/S/shaders. Logs: `nxlink-P11-baseline-20261028-113902.log` y `nxlink-MLBPowerPros2008-20261028-113447.log`; informes en `local/tmp/cpu/P11-console*`.
- Audio intacto. La NRO anterior está en `local/switch/build/P11-baseline.nro` y la candidata P11 en `P11-candidate.nro`. Enviar cualquiera con `--path mlbpowerpros2008/MLBPowerPros2008.nro` usa el mismo directorio de datos. El benchmark arranca un partido sembrado; no carga un estado guardado ni es una repetición idéntica de controles.
- Verificación: `mkswitch.sh` pasa (74 parches + linux + Switch y rebind), `treecheck.sh` y ambos `diff -rq` confirman runtime/Aurora idénticos. Compilación completa con `MLB_SWITCH_JOBS=4 bash tools/switch/build-nro.sh --graphics vulkan`: éxito, NRO de 114 MiB en `local/switch/build/MLBPowerPros2008.nro`; no cambió ninguno de los 48.223 archivos emitidos por el traductor. Checker de contenido y diff sin errores.
- Registros de esta tanda: `local/switch/logs/P11-{mkswitch,treecheck,build,wake-check,analysis-check}.txt`; símbolos nuevos en `elf-syms-P11.txt`. Ambas pruebas de consola usaron `WIICOMPILED_MATCH_BENCH=1 WIICOMPILED_MATCH_SECONDS=90 WIICOMPILED_SWITCH_PROFILE=1 WIICOMPILED_SWITCH_SPIKES=30 WIICOMPILED_SWITCH_SPIKES_MAX=256`.

## Siguiente paso: comparar desde el mismo estado

**Medir el efecto de P11 en los picos de fildeo y la toma inicial**, comparando el mismo estado y controles, a reloj stock. Primero benchmark de 90 s con picos; luego probe breve de fildeo. Si la espera GX sigue dominando, muestrear también el worker de core 1 antes de alterar barreras (propuesta en `P11-profile-fifo-review.md`). Datos anteriores:

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

- **Patch de Switch:** se genera desde `local/tmp/switch_edit.py`, con fases 1–59 de `sub()` / `write()` y folds de los cambios al texto de fases anteriores.
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
  - Perfil emparejado del worker GX (fase 59, opcional): `WIICOMPILED_SWITCH_PROFILE_GX=1` junto con picos. Añade líneas W de PC/LR con su instante, después de reanudar el principal; no escanea la pila GX. Descarta cruces de frame y cuenta errores. **Diagnóstico, no benchmark de rendimiento:** los SVC extra perturban los tiempos. Analizador: `python local/tmp/cpu/P11-console-worker.py <log> --syms <tabla> --label <salida>`.
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
- **Shell de verificación en esta PC:** MSYS2 no tiene `diff`; usar el de `C:/Program Files/Git/usr/bin` en el PATH, después de `/usr/bin:/bin` de MSYS2. Sin ese prefijo, `bash` puede resolver al de WSL. `local/tmp/P11-verify.sh` ejecuta `treecheck.sh` y los dos `diff -rq` con fallo real si hay diferencias.
