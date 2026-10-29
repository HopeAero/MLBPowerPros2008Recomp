# Traspaso — port de Switch (rama `switch-arm64`), 2026-10-29

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

## GX HLE worker experimental, fase 73

`WIICOMPILED_GX_HLE_WORKER=1` sigue siendo optativo y debe quedar apagado para
jugar. La medición de 90 s en Switch dio 45,52 fps frente a 58,86 fps con el
worker apagado, con los mismos relojes 1020/307/1331 MHz y diagnóstico activo.
La NRO Q73 y los logs son privados bajo `local/switch/`; el parche reproducible
está en `tools/patches/wiicompiled-gx-offload-source.patch`. Los tests de cola,
despertar, capturas de matrices/listas, arrays y wrappers pasaron. La equivalencia
visual de un partido completo y 60 fps estables no están demostradas. Análisis,
tabla A/B y siguiente arquitectura en `docs/gx-offload-feasibility.md`.

### Q75: bloque BP retirado (2026-10-29)

La candidata agrupaba ráfagas BP completas de 2–79 registros con cola HLE vacía.
Se retiraron helper/toggle/diagnóstico: en Q76 no ejecutó ningún bloque. Los
cinco sitios estáticos BP puros están en tres funciones que el dispatch Switch
sustituye por HLE (`GXInvalidateTexAll`, `__GXSetTmemConfig`, `GXSetTevColor`).
`gx_fifo.cpp` volvió exactamente al baseline Q73/phase65; se verificó con diff.

Pasaron los dos harness host (cola/captura y orden BP→draw con cuerpos reales
extraídos) y `P11-verify.sh`: runtime y Aurora reconstruidos coinciden exactamente
con el árbol compilado. El test negativo detecta un draw que adelanta al BP si se
omite la barrera existente. Los informes y pruebas están bajo
`local/tmp/cpu/GX-Q75-*`; las auditorías de bloques, guest epoch y el prototipo
Q76 también son privados. Los subagentes Luna/max
agotaron su cuota durante la revisión; el líder terminó los checks locales.
La NRO Q75 compiló (48.223 archivos traducidos sin cambios), SHA-256
`91a30aecf6828ce60830963565959256c8529a57f87ee7a0bc79af1331f7543d`,
guardada como `local/switch/build/GX-Q75-bp.nro`. La captura stock de 90 s dio
45,54 fps, p50/p95/p99 21,97/26,06/28,94 ms, cero errores de decode: sin mejora
apreciable frente a 45,52 fps de Q73. No es un replay bit a bit. Log privado
`nxlink-GX-Q75-bp-20261029-111618.log`. Los contadores iniciales no salieron porque
el benchmark usa `_Exit`; se corrigió el reporte periódico para la siguiente NRO.

### Q76: publicar varios comandos por aviso al worker

La candidata `WIICOMPILED_GX_HLE_BATCH=1` agrupa hasta 16 slots existentes por
publicación. Default off; `WIICOMPILED_GX_HLE_BATCH_PROFILE=1` imprime slots y
publicaciones cada cinco segundos. No usa el primer prototipo de payload combinado:
mantiene el orden de Submit/reentrada y publica antes de esperar una cola llena.
Watermark incluye staged; worker y predicado de sueño solo ven published.
Flush/FlushAll/Stop publican también tails parciales y trabajo de callbacks.

Pasó `GX-Q76-publish-test/run.ps1` con capacidades 16/64 y entorno batch on/off,
Q66 original y Q75-diff normal/`--staged-bp`. Las dos revisiones Luna/max no
encontraron un hueco de orden pendiente en esos casos. El build Switch y
`P11-verify.sh` pasaron; 48.223 archivos traducidos sin cambios. NRO privada
`local/switch/build/GX-Q76-publish.nro`, SHA-256
`444044c9717d02a9f1bd65bf10ec85660dd3fff462d34df6f5aa92e1fa71cf75`.
Batch-on en stock dio 45,9614 fps (4.137 frames/90,0103 s), p50/p95/p99
21,85/26,11/28,43 ms y cero errores GX. Se midieron 12,371 comandos por
publicación en 17 deltas completos dentro de grabación. `Flush` pasó a 18.792
muestras, `Submit` 12.566 de 88.483. La mejora frente a Q75 es pequeña y no se
atribuye causalidad. Log privado
`nxlink-GX-Q76-publish-20261029-113957.log`. Esta agrupación aún deja el scan
HLE, objetos y RAM guest en el principal. La NRO medida conserva el experimento
BP inactivo; el código final lo elimina e incluye `Idle()` solo en tests.
La limpieza recompiló y pasó Q66/Q76 y `P11-verify.sh`. NRO privada
`local/switch/build/GX-Q76-clean.nro`, SHA-256
`0493c0e5327528016e89c17fb9fb6f0ea1eb89ab74eb194ed5041f2f0b37ae8e`.
No se ha lanzado esa limpieza en Switch. El control batch-off usó la NRO
original `GX-Q76-publish.nro` y los mismos argumentos, cambiando solo
`WIICOMPILED_GX_HLE_BATCH=0`: 44,5163 fps, p50/p95/p99 22,55/26,46/29,38 ms,
cero errores GX, una publicación por slot. Log privado
`nxlink-GX-Q76-publish-20261029-115235.log`. La diferencia on/off es ~3,25%,
pero el perfil consulta reloj por publicación y sesga más el modo off; falta
medición sin diagnóstico y no es replay bit a bit. Worker/batch siguen default
off. Siguiente auditoría Luna/max: bloques textura/TLUT con identidad y estado
propios para eliminar barreras, no más ajustes pequeños de publicación.

### Q77: cargas de textura en la cola y contadores de barreras (2026-10-29, Claude + Sonnet) — veredicto: parar

`WIICOMPILED_GX_HLE_TEXTURES=1` encola `GXLoadTexObj` (snapshot por valor, `TrySubmit` con fallback) y `__GXSetSUTexRegs`; `WIICOMPILED_GX_HLE_FENCE_PROFILE=1` cuenta barreras por función y espera (armGetSystemTick). Tests de host Q66–Q77 pasan; árbol verificado. A/B 90 s stock 1020/307/1331, mismo NRO (`local/switch/build/GX-Q77.nro`):

| Config | fps | p50/p95/p99 ms |
|---|---|---|
| worker apagado | 59,35 | 16,67/18,13/22,82 |
| worker + batch | 46,64 | 21,58/25,52/28,67 |
| worker + batch + texturas | 46,64 | 21,54/25,78/28,36 |

Texturas: 100% aceptadas (1.310.720), su barrera desapareció, pero quedan 620–800 barreras/frame (`GXInitTlutObj` 240–300, `GXSetProjection` ~50, `GXSetCopyFilter` 40–60, `GXCopyTex` 20–30; `GXGetCPUFifo` 2/frame de 0,3–0,5 ms cada una) y la espera total en barreras es solo **1,9–2,6 ms/frame**, mientras el frame sube ~5 ms. Aun con cero barreras quedaría ~19 ms > 16,7 sin worker: **encolar llamada por llamada cuesta tanto como ejecutar el HLE** (miles de llamadas GX diminutas por frame). Mover el GX con esta granularidad no puede ganar; el worker queda apagado por defecto y no se debe seguir ajustando. Logs `nxlink-GX-Q77-20261029-1301*.log`, `-1304*`, `-1307*`.

## P20–P23 (2026-10-29, Claude + Sonnet): costo por jugador

- **Overclock del usuario:** 1224 MHz aún tiene bajones al fildear; **1428 MHz no**. Hace falta ~30–40% menos CPU en los frames pesados para stock.
- **Fase 67:** `WIICOMPILED_SWITCH_PROFILE_DL=1` (con picos libres) agrupa las muestras del hilo principal por llamadas a `GX__CallDisplayList` del frame anterior; `python local/tmp/cpu/P20-buckets.py <syms> <log>`. Permite comparar builds a igual carga de dibujo.
- **Mapa del costo por jugador** (`local/tmp/cpu/P21-model-draw-RE.md`): dos pipelines de ~28% cada uno en frames pesados: dibujo de modelos `80060B68`→`800614A8`(vtable)→`80061150` (por material: luces, TLUT `800537B4`, bind `80053464`, TEV `800452E4` de 12 llamadas; por malla: matrices, arrays, DL; ~25–45 llamadas GX por lote, ~294 lotes/frame) y sprites/cuadros `8004A0B0`/`8004ABA4` + matrices `80046030`/`80045DF8` (floats crudos al FIFO: el costo de `HleFifoWrite`).
- **Fase 68:** la cola de Codex (fase 66) hacía pagar a cada llamada GX un `Intercept()` fuera de línea y a cada llamada con puntero un flush + relectura VCD **aun con el worker apagado**. Ahora inline / no-op con la cola inactiva. Picos >30 ms en las mismas escenas (frames 2k–7k): 240 → 62. Log `nxlink-MLBPowerPros2008-20261029-142345.log`.
- **Nativas del juego no pagan:** P23 (bind de textura `800613BC`/`800537B4`/`80053464`, exacta en 400.000 casos qemu) ahorra 0,02–0,05 ms: ~70% de sus instrucciones son accesos a memoria guest que la exactitud conserva, las llamadas HLE ya eran directas y el registro de nativas cuesta ~150–200 instrucciones por llamada. No integrada; código en `local/tmp/cpu/P23-*`. La memoria guest ya es plana en Switch (base + bswap).
- **Regla del perfil de dispatch indirecto** (causa del crash P18): el traductor lee los `REGISTER_NATIVE_FUNCTION[_AS]` con regex sin preprocesador; una registración bajo `#if` falso deja la tabla generada sin el ganador traducido (`omits translated winner`). Registrar siempre sin `#if`.
- **Dónde queda el costo real:** dentro de la capa HLE GX (texturas/TLUT ~1,3 ms a 600 DL) y la toma inicial es **GPU** (~20 ms a 307 MHz).

## HLE1: tanda 1 del pipeline de modelos (2026-10-29)

Candidata implementada en fases 69–71 de `local/tmp/switch_edit.py`, reproducida
en los dos parches Switch. No cambia registros nativos ni headers del traductor.

- `WIICOMPILED_GX_MODEL_TEV=0` desactiva el replay de `GXSetTevOp` cuando sus
  registros vivos ya coinciden con el resultado y la agrupación de canales
  combinados. Conserva todos los bytes BP/XF, `bpSent` y dirty flags; no omite
  escrituras. Grabación de listas usa el camino anterior.
- `WIICOMPILED_GX_MODEL_TEXTURE=0` desactiva el snapshot de descriptor en RAM
  plana y la caché de decodificación que separa el word de usuario. Compara los
  bytes, conserva el valor actual de usuario y no reutiliza texeles ni cargas.
  G7 apagado también desactiva este camino.
- `WIICOMPILED_GX_MODEL_TLUT=0` desactiva el memo por valor de la última identidad
  de paleta. Conserva el contrato A3 de generaciones, la creación de identidades
  al vaciar el mapa de 4096 entradas y la invalidación ante llamadas sin A3.
- `WIICOMPILED_GX_MODEL_PROFILE=1` imprime contadores opcionales de hit/miss
  acumulados, prefijo `[model-hle]`. Worker HLE permanece apagado.

Pruebas privadas en `local/tmp/cpu/HLE1-test/`; `run.ps1` compila cuerpos reales
extraídos y ejecuta la campaña. Pasaron 250.000 pasos TEV/canales comparando
estado y FIFO, 400.000 decodificaciones directas y 800.000 pasos de secuencias de
textura con P19/G7 on/off y dos políticas de mapeo. Las 180.000 operaciones TLUT
comparan identidades/versiones/digest/generación, contadores A3, objetos Aurora,
bytes FIFO y callbacks de reemplazos; incluyen casos dirigidos de límite de
capacidad y ruta legacy. Ocho mutantes deben fallar. Q77 pasó en capacidades
16/64. Son pruebas con entorno host simulado; no demuestran imagen GPU idéntica.

`P11-verify.sh` confirmó `runtime and aurora trees identical`. Encontró restos
privados de P23 en el árbol compilado (archivo de nativas y opciones CMake), que
se preservaron en `HLE1-test/unused-P23-*` y se retiraron del árbol para que el
build corresponda al parche. La compilación Horizon pasó con cuatro jobs y
48.223 archivos traducidos sin cambios. NRO privada:
`local/switch/build/HLE1-model.nro`, SHA-256
`c4734672f83a54380497b0867d8aadcec61084e8c676566f56f0359c1dac4584`.
Símbolos: `local/switch/logs/elf-syms-HLE1.txt`.

Se ejecutaron dos capturas de consola, activa y control TEV/texturas apagados.
TLUT siguió activo en el control pese al argumento; los contadores lo detectaron.
La comparación no demuestra mejora global de fildeo. Resultados, límites y
archivos exactos: [medición HLE1](hle-model-round1.md).

**Revisión posterior, fases 72–73:** se corrigió el hash de descriptores, que
descartaba los bits de dirección al elegir cubeta; las capturas de sombra en RAM
plana pasan de cuatro llamadas escalares a un snapshot big-endian equivalente.
La caché TLUT retiene 128 identidades por valor, conserva invalidación por
capacidad/legacy y toma la generación una sola vez en los fallos. Folds en
37/69/71; el resto queda como fases nuevas reproducibles.

Pasaron diferenciales y build Horizon, sin modificar los 48.223 archivos
traducidos. NRO privada `local/switch/build/HLE1-round2.nro`, SHA-256
`e81cd3c0622ac1822c1124d6b316bac4163794c6692cf7e14047c210973920fa`;
símbolos `local/switch/logs/elf-syms-HLE1-round2.txt`. No atribuirle los números
de consola de la versión inicial.
El usuario pidió priorizar más código y evitar seguir repitiendo pruebas
manuales; se usaron los diferenciales automáticos existentes para esta revisión.

**Medición posterior:** log `nxlink-HLE1-round2-20261029-182140.log`, seis
volcados completos, cierre normal. Usuario confirmó mismos relojes sin
overclock. Aciertos textura/TLUT: 45,9%/75,5% frente a 28,0%/13,4% iniciales.
En cubetas comunes 240–479, CPU ocupada muestreada estandarizada 9,541 →
8,992 ms (−0,549 ms); resultados heterogéneos entre escenas, sin replay idéntico
ni garantía de ahorro causal. No hay errores GX registrados. El usuario nota
una mejoría leve, pero el fildeo sigue sin 60 fps estables. Detalle y límites en
[la actualización del informe](hle-model-round1.md#medición-de-la-revisión-posterior-a-0918d42).

## GX batch2: listas y FIFO, fases 74–76

Implementados y compilados tres atajos: F32 DIRECT escalar/por ráfagas, append de
payload XF incompleto y uso de tablas de scan por referencia para listas simples
cacheadas. Mantienen llamadas/orden, scratch del parser y fallbacks; no se
registran nativas nuevas ni se modifica audio o código generado.

`WIICOMPILED_GX_BATCH_FIFO=0` y `WIICOMPILED_GX_BATCH_DL=0` los apagan por separado;
`WIICOMPILED_GX_BATCH_PROFILE=1` habilita los contadores opcionales `[gx-batch]`.
Detalles y evidencia en [GX batch2](gx-batch2.md). Pasaron diferenciales on/off,
siete controles negativos, regeneración del parche, P11 y build con cuatro jobs.
NRO privada `local/switch/build/GX-batch2.nro`, SHA-256
`4f5031436d6331bc671a4764a78a08b300b9d0d903b58051402b86fe0926fe7c`;
símbolos `local/switch/logs/elf-syms-GX-batch2.txt`.

Pendiente de medición en consola; usar como referencia HLE1-round2 y su log
`nxlink-HLE1-round2-20261029-182140.log`. No se ha enviado esta candidata a la
consola. Mantener las sondas cortas y no repetir partidos para avanzar marcador.

## GX batch3: arrays y VAT, fases 77–78

El usuario pidió continuar programando y probar después; posteriormente confirmó
que estaba en netloader y se envió la candidata. Se mantiene la condición de
medir sin overclock. Esta captura basta para la primera revisión; evitar
repetir partidos para ampliar muestras.

Se añadió resolución de arrays de vértices/XF dentro de páginas RAM con bias
writable válido, evitando Contains más GuestToHostPtr en ese caso. Cruces,
páginas parciales, bias ausente y spans vacíos o con wrap mantienen la ruta
anterior. No retiene punteros entre llamadas. VAT aprovecha la igualdad de filas
ya mantenida por G6/A3, independiente de cambios de generación solo por VCD.
No cambia las publicaciones que emitía la comparación anterior.

`WIICOMPILED_GX_BATCH_PREP=0` desactiva ambos cambios; los contadores opcionales
de batch2 ahora incluyen arrays directos/fallback y filas VAT omitidas.
Pasaron diferenciales activos/apagados, cinco controles negativos, regeneración,
P11 y build Horizon con cuatro jobs. Los 48.223 archivos traducidos siguen sin
cambios. Evidencia y límites en [preparación GX](gx-preparation.md).

Candidata privada `local/switch/build/GX-batch3.nro`, SHA-256
`1a0c2df102e0a3c715623937e997e8153e98234946e52502b5febdcf99433789`;
símbolos `local/switch/logs/elf-syms-GX-batch3.txt`. Incluye batch2. Para una
comparación futura completa, desactivar FIFO, DL y PREP en la misma build;
cada interruptor controla su parte por separado.

**Primera captura GX-batch3:** log `nxlink-GX-batch3-20261029-191939.log`.
El análisis fija una instantánea de las 19:24:44, con siete volcados completos
y otro iniciado. En las cubetas compartidas 240–479, CPU ocupada muestreada
HLE1-round2 → batch3: 8,764 → 8,104 ms (−0,660 ms), heterogénea entre escenas,
sin replay ni ahorro causal probado. FIFO y VAT bajan; arrays no mejora en esa
comparación. Los contadores opcionales añaden aproximadamente 0,194 ms. DK
atribuye los picos a CPU; ninguna ventana de la instantánea tiene GPU >16 ms.
Siguen picos de 30–40 ms o más. Detalles, pesos y limitaciones en
[la medición GX](gx-preparation.md#primera-medición-en-consola).

El usuario recibió indicación de cerrar tras una sonda corta; al tomar la
instantánea la captura seguía abierta y la confirmación visual estaba pendiente.
No confundir esta instantánea con el log completo posterior ni afirmar 60 fps
estables. Archivos de análisis bajo `local/tmp/cpu/GX-prep-test/`.

**Cierre posterior:** conexión terminada normalmente. Doce ventanas DK,
19.519 presents y cero frames GPU >16 ms en todo el log. El análisis CPU
conservador usa once de doce inicios de volcado; `cpu-priorities.txt` conserva
la distribución. Priorizar preparación/envío GX por modelo, despacho indirecto
y origen de asignaciones/copias; el coste está repartido entre muchas funciones.
Los contadores activos tienen coste propio y no cuentan como progreso de juego.
El usuario identificó la CPU como prioridad. Sigue pendiente su confirmación
visual; no se inició otra prueba de consola.
