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
| Partido de prueba sin gráficos (`graphics_api = "null"`), núcleos A78 | arranque, disco, sonido, menús, preparación del partido y **primer lanzamiento en 25-32 s**, 5 de 5 veces, como en x86 |
| Lo mismo en los núcleos A55 | primer lanzamiento en 150 s; la intro va a unos 15 fotogramas por segundo |
| Gráficos | **sin probar en ARM**: el backend nulo de Dawn ejecuta todo el trabajo de GX en la CPU pero no dibuja |

Dos fallos que solo aparecieron en ARM, ya arreglados en `tools/patches/wiicompiled-aarch64.patch` (detalles en [ingeniería inversa](../ingenieria-inversa.md#3-hilos-y-ritmo)):
- **Espera activa del DSP:** el juego espera al DSP con un bucle vacío que en x86 borra el compilador. En ARM se conservaba por la comprobación del tamaño de página en tiempo de ejecución. Ahora ARM64 en Linux usa páginas fijas de 4 KiB, como x86, y el juego se niega a arrancar con otro tamaño. La Switch, los teléfonos y la mayoría de distribuciones usan 4 KiB; la Raspberry Pi 5 con su kernel por defecto usa 16 KiB (compilar con `-DMKW_AARCH64_4K_PAGES=OFF` y, de momento, se cuelga en esa espera).
- **Cargador sin turno:** el bucle de fotograma solo espera a VI si va adelantado. Por debajo de 60 FPS el hilo principal no se bloqueaba nunca y el cargador de arranque (prioridad 24) no llegaba a ejecutarse. Ahora `VIGetRetraceCount` espera un retrazo cuando un hilo de menos prioridad lleva 100 ms listo. No es propio de ARM: en Windows el partido de prueba sigue a 59,9 FPS.

Para la Switch esto significa que el código traducido funciona en ARM64 y que la CPU importa: a 1 GHz, los A57 de la Switch rinden, a ojo, como los A55 del teléfono o menos (sin medir), y esos A55 ya no llegan a 60 FPS en la intro con el backend nulo. Bibliotecas dinámicas de la compilación: libpng16, libz, libfreetype, libm, libstdc++, libgcc_s y libc (SDL3 y Dawn van enlazados).

## Casillas
- [ ] Backend OpenGL ES funcionando
- [x] Versión ARM64 funcionando (Linux ARM64, en un teléfono; ver arriba)
- [ ] El prototipo de Switch llega al menú principal
