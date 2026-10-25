# Nintendo Switch

*English: [../en/roadmap/switch.md](../en/roadmap/switch.md)*

**Estado:** en curso: ARM64 en Linux · [Roadmap](../../ROADMAP.es.md)

## Por qué
Power Pros es, en el fondo, un juego de consola. Una versión homebrew para Switch permitiría jugarlo en el sofá y fuera de casa, compilado desde la copia de cada uno como en PC.

## Qué hace falta
- **Gráficos:** Dawn no tiene backend para el sistema de Switch. Opciones: el backend OpenGL ES de Dawn sobre la Mesa de Switch (por eso OpenGL va primero) o un renderizador nativo en deko3d.
- **CPU y memoria:** el código traducido tiene que compilarse para ARM64 (la CPU de Switch) y caber en su memoria.
- **Sistema:** mandos, audio, archivos y fuentes con libnx (devkitPro).
- **Distribución:** como en PC, no se distribuye nada del juego; el jugador aporta su propia copia.

## Plan
1. OpenGL / OpenGL ES funcionando primero en PC (ver [OpenGL](opengl.md)).
2. Una versión ARM64 para Linux (por ejemplo en una Raspberry Pi o un TV box Android) como paso entre PC y Switch.
3. Un primer prototipo en Switch que llegue al menú.

## Versión ARM64 (octubre de 2026)

`tools/linux/build.sh --arch aarch64` compila el juego para Linux ARM64 desde un PC x86_64 (clang 18 contra un sysroot de Ubuntu 24.04 arm64; ver [Linux](../linux.md#arm64-experimental)). Probado el 25 de octubre en hardware ARM real: un Samsung Galaxy A54 (Exynos 1380: 4× Cortex-A78 a 2,4 GHz y 4× Cortex-A55 a 2,0 GHz; Android 14, páginas de 4 KiB), desde `adb shell` y sin root, con `tools/linux/aarch64/adb-match.sh`:

| Qué | Resultado |
|---|---|
| Pruebas del runtime (QEMU en el PC) | pasan, salvo tres que también fallan en Linux x86_64 |
| Partido de prueba sin gráficos (`graphics_api = "null"`, audio de SDL `dummy`), núcleos A78 | arranque, disco, motor de sonido, menús, preparación del partido y **primer lanzamiento en 28-29 s**, 6 de 6 veces, como en x86 |
| Lo mismo en los núcleos A55 | primer lanzamiento en 130-131 s, 3 de 3 veces; el juego va a unos 12 fotogramas por segundo |
| Compilación con `--16k-pages` (comprobación del tamaño de página en tiempo de ejecución), núcleos A78 | primer lanzamiento en 29 s |
| Gráficos | **sin probar en ARM**: el backend nulo de Dawn ejecuta todo el trabajo de GX en la CPU pero no dibuja |

Dos fallos que solo aparecieron en ARM, ya arreglados en `tools/patches/wiicompiled-aarch64.patch` (detalles en [ingeniería inversa](../ingenieria-inversa.md#3-hilos-y-ritmo)):
- **Espera activa del DSP:** el juego espera en un bucle vacío a que el DSP llame al callback de inicio de su tarea, y el `DSPAddTask` del runtime nunca lo llamaba. En x86 el compilador borra ese bucle y nadie lo notó; en ARM, con la comprobación del tamaño de página en tiempo de ejecución, el bucle se quedaba. Ahora `DSPAddTask` llama al callback como el DSP real. Además, ARM64 en Linux usa por defecto páginas fijas de 4 KiB, como x86 (el mismo código que se validó en x86, y más rápido), y se niega a arrancar con otro tamaño. La Switch, los teléfonos y la mayoría de distribuciones usan 4 KiB; para la Raspberry Pi 5 con su kernel por defecto (16 KiB) está `build.sh --arch aarch64 --16k-pages`, probado en el teléfono pero no en una Pi.
- **Cargador sin turno:** el bucle de fotograma solo espera a VI si va adelantado. Por debajo de 60 FPS el hilo principal no se bloqueaba nunca y el cargador de arranque (prioridad 24) no llegaba a ejecutarse. Ahora `VIGetRetraceCount` espera un retrazo cuando un hilo de menos prioridad lleva 100 ms listo sin ejecutarse. No es propio de ARM, así que se midió también en x86: no saltó ninguna vez en el partido de prueba de Windows (59,9 FPS, igual que antes) ni en Linux x86 a 14,7 FPS con gráficos por software; en el teléfono salta una vez por arranque.

Para la Switch esto significa que el código traducido funciona en ARM64 y que la CPU importa: a 1 GHz, los A57 de la Switch rinden, a ojo, como los A55 del teléfono o menos (sin medir), y esos A55 se quedan en unos 12 FPS con el backend nulo. Bibliotecas que necesita la compilación al arrancar: libpng16, libz, libfreetype (con libbz2 y libbrotlidec/libbrotlicommon), libm, libstdc++, libgcc_s y libc; SDL3 y Dawn van enlazados. SDL y Dawn cargan además, si existen, las de vídeo y sonido del sistema (Vulkan, EGL/GLES, Wayland/X11, PipeWire/PulseAudio/ALSA, udev, D-Bus).

## Casillas
- [ ] Backend OpenGL ES funcionando
- [x] Versión ARM64 funcionando (Linux ARM64, en un teléfono; ver arriba)
- [ ] El prototipo de Switch llega al menú principal
