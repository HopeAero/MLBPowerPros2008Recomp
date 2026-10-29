# Traspaso — port de Switch (rama `switch-arm64`), 2026-10-28

Estado para la próxima sesión o IA. Se sobrescribe al cerrar cada tanda de trabajo; el historial está en `git log -p docs/traspaso.md`.

## Dónde estamos

- **Rendimiento:** 59.9 fps a reloj stock en modo portátil (CPU 1020 / GPU 307 MHz) con el renderer deko3d, que ya es el predeterminado. Benchmark de 90 s: p50 16.67 ms, p95 ~17.1 ms.
- **Audio:** resuelto. El narrador va a tiempo (ticker de 5 ms a 200 despertares/s, 0 pasadas perdidas) y no hay huecos de salida aunque caigan los fps. El usuario lo confirmó de oído: "el audio va perfecto, ni lo tocaría".
- **Lo pendiente:** bajones de **un frame** (30–36 ms) al fildear y en la toma inicial que muestra a todos los jugadores. El usuario observa que *cuantos más fildeadores y corredores hay en pantalla, más caen los fps* (CPU ~80% al lanzar o batear, ~90–92% al fildear). Prueba 23: 39 picos de más de 30 ms en 16 570 frames de partido.

## GX-F1 (2026-10-28): factibilidad, sin cambios de runtime

- Se siguió `local/tmp/codex-gx-brief.md` con tres subagentes **gpt-6-luna / max**.
  Informe integrado: [docs/gx-offload-feasibility.md](gx-offload-feasibility.md).
  Informes privados por ámbito: `local/tmp/cpu/GX-offload-{state,memory,order}-feasibility.md`.
- **Viable con condiciones:** cola ordenada y único dueño Aurora; conservar efectos
  guest/retornos y sombra síncrona. Se confirmó viewport consultado por dos callers
  y scissor inline que lee RAM guest; los anchors MKW `UNBOUND` no identifican targets
  MLB. No se resolvieron todos los getters anónimos/indirectos.
- Bloqueos antes del prototipo amplio: vida e instante de lectura de DL/arrays/texels/
  TLUT; G7 exige un solo hilo dueño; el servicio `sWaitService` asume hilo guest;
  `GX_HLE_MidCommand`, callbacks, CopyDisp y readback EFB necesitan conservar contexto
  y barreras. No basta pasar punteros ni alternar productores tras un drain.
- Primer paso conservador dejando objetos y scan en principal: presupuesto ~2,6 ms
  **brutos**, neto menor y no medido. Objetivo amplio 3–4 ms exige migrar más familias.
  No elegir core 1/2 usando los ~11 ms de decode: incluyen esperas, no CPU pura.
- Reanálisis P18b reproducible: `python local/tmp/cpu/GX-offload-profile.py` pasa
  self-check/asserts; 20 ventanas (70–260 s), media 13,46 ms ocupados/frame. Solo tres
  picos >30 ms tras 60 s; 0/98 muestras con `wait_processed`, sin W ni relojes. No
  atribuirlos a FIFO/GPU ni llamarlo benchmark stock verificado.
- **Sonda en consola completada tras «listo»:** NRO actual P18b, 90 s automáticos con
  PROFILE_GX=1, SPIKES=20/MAX256 y THREADS=5. CPU1020/GPU307/EMC1331 al inicio y final,
  portátil/deko3d. 59,4027 fps, p95 18,4216 / p99 21,7355 ms. 124 picos >20 ms, uno
  >30 (33,5 ms); 2.670 P / 2.669 W, sin agotar el límite. `wait_processed`: 247
  muestras en 90 picos, con decoder/estado/memoria activos en los W cercanos. Espera
  productor 0,274 ms/frame; decode elapsed 11,125. No es ahorro ni A/B de offload.
- Última ventana deko3d: 73 GPU frames >16 ms y p99 GPU 20 ms. Numeración y ventanas
  distintas de las host; no adjudicar cada pico por igualdad de IDs. El traslado HLE
  no asegura por sí solo 60 fps sin bajones. Audio sin cambios.
- Afinidad: 17 deltas útiles de períodos nominales de 5 s dan CPU media principal
  4,272 s (~85%), worker GX core1 2,587 s (~52%), suma de slots fijos core2 0,967 s
  (~19%). No mide toda la carga core2 ni prueba 81% libre; faltan sampler/dumper/OS y
  el principal puede migrar (mask0x7). **Core2 es el primer candidato para probar HLE**.
  Análisis en `local/tmp/cpu/GX-F1-threads.md`, recuentos/medias verificados por el líder.
- Log nuevo `nxlink-MLBPowerPros2008-20261028-170606.log`, símbolos `elf-syms-P18b.txt`;
  informe privado `local/tmp/cpu/GX-F1-console-report.md`. Parsers P11 usan labels
  separados `GX-F1-main` y `GX-F1-worker` porque ambos escriben el mismo prefijo.
  NRO/nxlink terminaron, sin monitores pendientes. No fase 66 ni build nuevo.
- Verificación: `mkswitch.sh` (74 parches + linux + Switch/rebind), `treecheck.sh` y
  ambos `diff -rq` pasan; árbol runtime/Aurora idéntico. Logs:
  `local/switch/logs/GX-offload-feasibility-verify.txt` y `GX-F1-console-verify.txt`.
  Sin push.

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

## Tanda P13 (2026-10-28): texturas recreadas, probada en consola

- Fases **60** (`auto_mips::build_chain` 2–3× más rápido, idéntico byte a byte) y **61** (caché de texturas por contenido + pool LRU de 96 MiB, solo Switch; `WIICOMPILED_SWITCH_TEXSTATS=1`). Detalle y causa en `docs/ingenieria-inversa.md` (picos de fildeo). Snippets `local/tmp/cpu/P13-*.py`, pruebas en `local/tmp/cpu/P13-mips/` y `P13-test/`.
- Benchmark 90 s stock portátil (1020/307/1331): 59,42 fps, p95 18,22, p99 22,63; **0 picos >30 ms** (base 4, P11 1). Entró en la misma escena final pesada que la base: 193 frames tardíos frente a 231. Esa escena está **limitada por GPU** (73 frames con GPU >16 ms, p99 GPU 20 ms): siguiente frente si se quiere p99 limpio.
- Falta: prueba jugando (fildeo, toma inicial) con `WIICOMPILED_SWITCH_SPIKES=30 WIICOMPILED_SWITCH_SPIKES_MAX=256 WIICOMPILED_SWITCH_TEXSTATS=1`. Vigilar en el log dk `descriptors: all N image slots in use` (el pool ocupa descriptores).
- NRO: `local/switch/build/P13-candidate.nro`; anterior `P12-baseline.nro`.
- **Jugando (P13):** picos >30 ms por ventana frente a run23: menús 62→8, toma inicial 46→31, juego 2/6/4→1/1/3. Lo que queda de la toma inicial es **GPU** (p50 15,75, p95 20,75 ms). P14: barreras descartadas (≤0,25 ms aun sin ninguna). Siguiente: `WIICOMPILED_DK_PASS_TIMES=60 WIICOMPILED_DK_DRAW_TIMES=1 WIICOMPILED_DK_DRAW_PIXELS=2` en la toma inicial para ver qué dibujos/shaders pesan (vértices o píxeles) en las pasadas 0, 57, 58 y las de 188×128.

## Tandas P15–P19 (2026-10-28, Claude con subagentes Sonnet)

- **Fase 62:** `WIICOMPILED_SWITCH_PROFILE_WINDOW=<s>` (con `WIICOMPILED_SWITCH_SPIKES=30` y `WIICOMPILED_SWITCH_PROFILE=1`) imprime cada `<s>` segundos de una partida jugada los histogramas PC/LR del hilo principal (`[pwin]`). Analizador: `python local/tmp/cpu/P15-windows.py <elf-syms> <log> [top]` (ventanas, ms ocupados por frame, crecimiento entre ventanas ligeras y pesadas).
- **Fase 63:** nativas NEON de las IDCT del video THP (`thp_natives.cpp`): la intro pasa de 10,8 a 2,25 ms/frame. **No era el fildeo** (solo aparece en la intro).
- **Fases 64–65:** front-end GX más ligero (`WIICOMPILED_GX_P17=0` lo apaga) y objetos de textura host perezosos (`WIICOMPILED_GX_TEXMEMO=0`). ~0,2–0,3 ms/frame medido (participación GX del hilo principal −4,5%).
- **Descartado:** barreras deko3d (P14, ≤0,25 ms aun sin barreras); nativas de estado gráfico/esqueleto P18 (0,05 ms; crashean al arrancar: `Generated indirect dispatch profile 'base' omits translated winner 0x80045bcc`), código en `local/tmp/cpu/P18-*`.
- **Estado del hilo principal jugando** (perfil por ventanas, stock 1020 MHz): 13–15 ms ocupados de 16,6; código del juego traducido ~5,7–6 ms (plano: 1.132 funciones, la más cara 0,19 ms), **HLE GX + aurora front-end ~4,2–4,7 ms** (repartido en ~100 funciones: display lists 0,94, setters 0,99, texturas 0,95, TLUT 0,32, vtx desc 0,80, stream inmediato 0,58, publicación FIFO 0,27), runtime/llamadas ~2,4, audio ~0,9 (no tocar), nativas ~0,4. Libre: 0,2–0,7 ms.
- **El usuario probó con overclock de CPU a 1224 MHz y aún tiene bajones al fildear.** Por tanto no es solo CPU bruta del hilo principal: sospechar esperas/sincronización entre el hilo del juego y el worker GX (core 1, ~11 ms/frame de decodificación) o el GPU (toma inicial: GPU 20 ms). Medir esto primero.
- Logs: `nxlink-MLBPowerPros2008-20261028-143009.log` (antes, símbolos `elf-syms-P16.txt`) y `-163615.log` (fases 64–65, `elf-syms-P18b.txt`).

## Siguiente paso: sacar el GX entero del hilo del juego (decisión del usuario)

Objetivo: el mayor ahorro posible en el hilo principal (core 0) moviendo la capa HLE GX (lo que hoy hace el juego al llamar a la API GX: setters, display lists, texturas/TLUT, vtx desc, stream inmediato) a otro núcleo, como hacía la Wii con su FIFO por hardware. El juego solo debe encolar registros compactos; un worker (core 2, casi ocioso, o core 1 si cabe) ejecuta la HLE. Ahorro potencial ~3–4 ms/frame.

1. **Factibilidad primero:** qué estado GX lee el juego de vuelta de forma síncrona (`GXGet*`, sombra `__gx`, `GXGetVtxDesc`, escaneo de display lists que necesita el estado actual, `GXReadDrawSync`/tokens, callbacks, `GXDrawDone`, copias EFB que el juego lee, DCFlush/DMA de texturas y TLUT), y qué escrituras guest hace la HLE (objetos `GXTexObj`/`GXTlutObj` en memoria guest). Esas partes deben quedarse síncronas o resolverse con una sombra en el hilo principal; el resto se encola.
2. **Medir antes:** con 1224 MHz siguen los bajones; capturar con `WIICOMPILED_SWITCH_PROFILE_GX=1` (fase 59) y picos a 20 ms cuánto espera el hilo principal al worker GX en los frames de fildeo.
3. Implementar por partes detrás de interruptores, con pruebas diferenciales de host (como `local/tmp/cpu/P17-test/`, `P19-test/`) que comparen los bytes de FIFO/estado aurora generados, y A/B en consola con el perfil por ventanas.
4. Fidelidad intacta: mismo orden de comandos y mismo resultado en pantalla; audio sin tocar.

## GX fase 66: candidata de worker lista para prueba de consola

- `WIICOMPILED_GX_HLE_WORKER=1` activa la cola libnx en core 2. Sin la variable,
  conserva el camino anterior. La NRO Vulkan está en
  `local/switch/build/MLBPowerPros2008.nro` (SHA-256 `93bda05ab39c64953a09a8858a2a555c8a17c919a3266a50e578efc69f8e6a0a`);
  la anterior, `GX-Q66-baseline.nro` (SHA-256 `6d0b1ab24d2082cba079fe13b1577b52fa449a8c03ef9ee1d707f79008e597f2`).
- Encola 91 emisores escalares y tres snapshots de matrices/luz con valor propio;
  mantiene guest stores, parser de listas, objetos G7 y operaciones con punteros o
  resultados en el principal con fence. Es una fase del traslado, no el GX entero.
- Tres revisiones Luna/max cubrieron inventario, cola, snapshots y diferencial.
  El líder corrigió dos wake perdidos y la omisión de `GXEnd`, revisó los resultados
  y repitió las pruebas. Host: cola, wake determinista, negativos de copia y
  diferenciales de RAM/VCD/FIFO/`--wrap`; todos pasan. Build y enlace Switch
  completos con cuatro trabajos, 48.223 archivos traducidos sin cambios.
- `mkswitch.sh`, rebind, `treecheck.sh` y los dos `diff -rq` pasan. Fuente de fase 66:
  `tools/patches/wiicompiled-gx-offload-source.patch`; aplicación integrada en los
  dos parches Switch. El checker de contenido pasa. Los parches generados conservan
  26 líneas de espacios finales heredadas del árbol previo; `git diff --check`
  las señala aunque no son cambios de ejecución.
- Falta ejecutar A/B en hardware con el mismo camino de datos, primero worker=0,
  luego =1, registrando relojes, frames, p95/p99, esperas y GPU. Requiere que el
  usuario vuelva a poner netloader y avise «listo». No prometer 60 fps estables;
  la ventana GPU previa también excedió 16,6 ms en algunas escenas.

## Cómo se trabaja (flujo probado)

- **Patch de Switch:** se genera desde `local/tmp/switch_edit.py`, con fases 1–65 de `sub()` / `write()` y folds de los cambios al texto de fases anteriores.
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
