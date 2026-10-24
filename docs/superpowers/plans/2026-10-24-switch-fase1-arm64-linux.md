# Switch, fase 1-2: el juego en Linux ARM64 (probado con QEMU) — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** compilar el juego para Linux aarch64 desde la PC (x86_64, WSL) y demostrar con QEMU que el código traducido corre bien en ARM64, el paso previo a la Switch.

**Architecture:** compilación cruzada con clang (`--target=aarch64-linux-gnu`) contra un sysroot de Ubuntu 24.04 arm64; el traductor (.NET) sigue corriendo en la PC porque su salida es C++ independiente de la arquitectura. `tools/linux/build.sh --arch aarch64` usa un toolchain de CMake y un árbol de build propio. Las pruebas corren con `qemu-aarch64` (modo usuario): primero las pruebas del runtime con `ctest`, después el juego entero con el benchmark de partido y un cuadro leído de la GPU (`AURORA_DUMP_PRESENT`).

**Tech Stack:** clang/lld 18 (WSL Ubuntu 24.04), CMake + Ninja, mmdebstrap, qemu-user-static, Dawn `linux-aarch64` prebuilt (`v20260603.191052`, hash ya fijado en `AuroraDawnProvider.cmake`), Mesa arm64 (llvmpipe / lavapipe).

**Spec:** [`docs/roadmap/switch.md`](../../roadmap/switch.md) (plan, paso 2: "Una versión ARM64 para Linux … como paso entre PC y Switch").

## Global Constraints

- Nada del juego en el repo ni en artefactos publicados: el sysroot, los builds y los cuadros capturados viven en `local/` (`tools/check_no_game_data.py` es el hook de pre-commit).
- Los cambios al código de Wiicompiled/aurora van como parche reproducible en `tools/patches/` y en la lista de `tools/wiicompiled-translate.ps1`; el código generado no se toca a mano.
- El build x86_64 de Linux y el de Windows no cambian de comportamiento: siguen pasando `tools/benchmark-match.ps1` (Windows) y `tools/linux/build.sh` sin `--arch`.
- Las pruebas visuales se verifican con un cuadro leído de la GPU (`AURORA_DUMP_PRESENT=<cuadro>:<archivo.ppm>`), nunca contando cuadros.
- Las pruebas largas paran en una condición verificada (cuadro escrito o escena alcanzada) o en un límite fijo; no se juega a mano.

## Review Focus

- **`char` sin signo:** en Linux aarch64 (y en la Switch, devkitA64) `char` es sin signo; en x86 y en macOS arm64, donde Wiicompiled se probó, es con signo. Código traducido o del runtime que guarda negativos en `char` se rompería en silencio. Task 3 fija `-fsigned-char` y lo prueba.
- **Orden de memoria más débil en ARM:** colas entre hilos (mezcla de audio, cola de presentación) que en x86 funcionan por TSO pueden fallar en ARM. Task 5 corre el partido completo y compara el audio y el cuadro; un fallo intermitente aquí se documenta, no se ignora.
- **Tamaño de página distinto de 4 KiB:** la Raspberry Pi 5 y otros ARM64 usan páginas de 16 KiB; la memoria plana del invitado mapea regiones con desplazamientos fijos. Task 4 añade una comprobación al arrancar que falla con un mensaje claro si una región no queda alineada a la página del sistema.
- **Código x86 en nuestros parches:** intrínsecos `_mm_*`, `__rdtsc` o `-march=x86-64-v3` en archivos que añadimos nosotros (overlay, benchmark) rompen el build ARM. Task 4 los aísla en `wiicompiled-aarch64` con una alternativa portable.
- **Bibliotecas del sysroot que faltan en el aparato real:** un binario que corre en QEMU con el sysroot puede no arrancar en una distro con otra glibc. Task 6 lista las dependencias dinámicas (`readelf -d`) en la documentación.

---

## Estructura de archivos

| Archivo | Qué hace |
|---|---|
| `tools/linux/aarch64/make-sysroot.sh` (nuevo) | Crea `local/sysroot/noble-arm64` con mmdebstrap e instala las cabeceras y bibliotecas que el build necesita |
| `tools/linux/aarch64/toolchain.cmake` (nuevo) | Toolchain de CMake para clang → aarch64 con ese sysroot y `qemu-aarch64` como emulador de pruebas |
| `tools/linux/build.sh` (modificar) | Opción `--arch aarch64`: toolchain, `build-aarch64`, sin Texture/Season Studio ni PowerUp+ |
| `tools/patches/wiicompiled-aarch64.patch` (nuevo) | Arreglos de ARM64 en nuestro runtime: `-fsigned-char`, sustitutos portables de código x86, comprobación de página |
| `tools/wiicompiled-translate.ps1` (modificar) | Añade `wiicompiled-aarch64` al final de la lista de parches |
| `tools/linux/aarch64/qemu-match.sh` (nuevo) | Corre el benchmark de partido aarch64 en QEMU hasta escribir el cuadro pedido o agotar el límite |
| `docs/roadmap/switch.md`, `docs/en/roadmap/switch.md`, `docs/linux.md`, `docs/en/linux.md` (modificar) | Estado y cómo reproducirlo |

---

### Task 1: Sysroot arm64 y toolchain

**Files:**
- Create: `tools/linux/aarch64/make-sysroot.sh`
- Create: `tools/linux/aarch64/toolchain.cmake`

**Interfaces:**
- Produces: sysroot en `$ROOT/local/sysroot/noble-arm64`; toolchain que lee la variable de caché `MLB_AARCH64_SYSROOT` (por defecto ese camino) y fija `CMAKE_SYSTEM_NAME=Linux`, `CMAKE_SYSTEM_PROCESSOR=aarch64`, `CMAKE_C/CXX_COMPILER_TARGET=aarch64-linux-gnu`, `CMAKE_SYSROOT`, linker `lld`, `CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=ONLY`, `CMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64;-L;<sysroot>` y las variables `PKG_CONFIG_SYSROOT_DIR` / `PKG_CONFIG_LIBDIR` hacia `<sysroot>/usr/lib/aarch64-linux-gnu/pkgconfig:<sysroot>/usr/share/pkgconfig`.

- [ ] **Step 1: Escribir la prueba de humo** `tools/linux/aarch64/smoke/CMakeLists.txt` + `smoke.cpp`: un ejecutable que enlaza `libpthread` y `libz` del sysroot, imprime `aarch64 ok` y devuelve 0; `add_test(NAME aarch64_smoke COMMAND smoke)`.
- [ ] **Step 2: Correrla sin sysroot y ver que falla**
  Run: `cmake -S tools/linux/aarch64/smoke -B local/build/aarch64-smoke -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/linux/aarch64/toolchain.cmake`
  Expected: FAIL (no existe el toolchain / el sysroot).
- [ ] **Step 3: Escribir `make-sysroot.sh`** — `mmdebstrap --arch=arm64 --variant=minbase --include=<lista> noble <dir> http://ports.ubuntu.com/ubuntu-ports`, con la lista de `docs/en/linux.md` (paquetes `-dev` de SDL: pipewire, pulse, asound2, xss, decor, wayland, xkbcommon, x11, xext, xrandr, xcursor, xi, xfixes, xtst, gbm, drm, udev, dbus, usb-1.0, png, zlib1g, freetype) más `libstdc++-13-dev`, `libegl-dev`, `libgles-dev`, `libgl1-mesa-dri`, `mesa-vulkan-drivers`, `libvulkan1`; requiere `qemu-user-static` y `binfmt-support` (los instala si faltan); deja enlaces simbólicos relativos (`symlinks -cr`) para que el sysroot funcione desde otra ruta. Y escribir `toolchain.cmake` con lo de Interfaces.
- [ ] **Step 4: Crear el sysroot y repetir la prueba**
  Run: `tools/linux/aarch64/make-sysroot.sh && cmake -S tools/linux/aarch64/smoke -B local/build/aarch64-smoke -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/linux/aarch64/toolchain.cmake && cmake --build local/build/aarch64-smoke && ctest --test-dir local/build/aarch64-smoke --output-on-failure`
  Expected: `aarch64_smoke ... Passed`; `file local/build/aarch64-smoke/smoke` dice `ARM aarch64`.
- [ ] **Step 5: Commit** — `git add tools/linux/aarch64 && git commit -m "Linux aarch64: sysroot script and cross toolchain"`

### Task 2: `build.sh --arch aarch64` hasta configurar

**Files:**
- Modify: `tools/linux/build.sh` (parseo de opciones, variables `build`/`log_tag`, `configure=(...)`, pasos de Studio/PowerUp+)

**Interfaces:**
- Consumes: `tools/linux/aarch64/toolchain.cmake` (Task 1).
- Produces: `--arch aarch64` → `build=$work/build-aarch64`, `log_tag=-aarch64`, `-DCMAKE_TOOLCHAIN_FILE=…`, sin Texture Studio / Season Studio / PowerUp+ (`skip_studio=1`, `skip_roster=1`), instalación por defecto en `local/install-aarch64`; `--arch` con `--container` es un error ("--arch aarch64 is not supported with --container").

- [ ] **Step 1: Prueba que falla** — Run: `tools/linux/build.sh --arch aarch64 --help` · Expected: `unknown argument: --arch`.
- [ ] **Step 2: Implementar la opción** según Interfaces; la traducción (pasos 1-2 del script) es la misma que en x86_64 y reutiliza `local/wiicompiled-mlb/generated`.
- [ ] **Step 3: Configurar** — Run: `tools/linux/build.sh --arch aarch64 --disc "<iso>"` hasta "Configuring the native build" · Expected: el log `local/analysis/linux-aarch64-configure.log` muestra el paquete `dawn-linux-aarch64.tar.gz` con hash verificado y termina en "Generating done". Si la compilación del juego falla aquí, se sigue en Task 4 (es esperado).
- [ ] **Step 4: Prueba de no regresión** — Run: `tools/linux/build.sh --no-roster-editor` (x86_64, sin `--arch`) · Expected: "Done." como antes.
- [ ] **Step 5: Commit** — `git commit -m "Linux build: --arch aarch64 cross build"`

### Task 3: Pruebas del runtime en ARM64 y `char` con signo

**Files:**
- Create: `tools/patches/wiicompiled-aarch64.patch` (empieza aquí; Task 4 lo amplía)
- Modify: `tools/wiicompiled-translate.ps1` (añadir `'wiicompiled-aarch64'` al final de la lista)
- Test: `runtime/tests/aarch64_abi_tests.cpp` (dentro del parche)

**Interfaces:**
- Produces: en `runtime/CMakeLists.txt`, para `CMAKE_SYSTEM_PROCESSOR` aarch64/arm64 en Linux: `add_compile_options(-fsigned-char)` antes de definir los targets (afecta al código traducido, al runtime y a aurora); prueba `mkw_aarch64_abi_tests`.

- [ ] **Step 1: Escribir la prueba** `aarch64_abi_tests.cpp`:
  ```cpp
  static_assert(std::is_signed_v<char>, "translated code and the runtime assume signed char (x86, macOS arm64)");
  int main() {
      char c = static_cast<char>(0xF0);
      if (int(c) != -16) return 1;          // sign extension like x86
      if (sizeof(void*) != 8) return 2;
      return 0;
  }
  ```
  y `add_test(NAME mkw_aarch64_abi_tests COMMAND mkw_aarch64_abi_tests)`.
- [ ] **Step 2: Verla fallar** — Run: `cmake --build local/wiicompiled-mlb/build-aarch64 --target mkw_aarch64_abi_tests` · Expected: error del `static_assert` (char sin signo en aarch64 Linux).
- [ ] **Step 3: Añadir `-fsigned-char`** según Interfaces.
- [ ] **Step 4: Correr todas las pruebas del runtime bajo QEMU** — Run: `cmake --build local/wiicompiled-mlb/build-aarch64 --target $(grep -oP 'add_executable\((\K\w+_tests)' local/wiicompiled-mlb/src/runtime/CMakeLists.txt | tr '\n' ' ') && ctest --test-dir local/wiicompiled-mlb/build-aarch64 --output-on-failure` (solo los targets de prueba: el juego aún puede no compilar hasta Task 4; configurar con `-DMKW_BUILD_PSQ_TESTS=ON` para incluir `mkw_psq_helpers_tests` y `mkw_psq_reserved_tests`, que prueban los caminos `__aarch64__` de los cuantizados) · Expected: todas `Passed`; cualquier diferencia de PSQ/float entre x86 y ARM se arregla en este parche antes de seguir.
- [ ] **Step 5: Regenerar el parche sobre la cadena limpia** (patrón `local/tmp/mkd3d11.sh`: clon al pin + todos los parches, aplicar, `git diff`) y comprobar que la cadena con `wiicompiled-linux` aplica.
- [ ] **Step 6: Commit** — `git commit -m "aarch64: signed char and runtime tests under QEMU"`

### Task 4: El juego compila y enlaza para aarch64

**Files:**
- Modify: `tools/patches/wiicompiled-aarch64.patch`

**Interfaces:**
- Consumes: Tasks 2-3.
- Produces: `local/wiicompiled-mlb/build-aarch64/WiiCompiled` (ELF aarch64) instalado como `MLBPowerPros2008` en `local/install-aarch64`; en `guest_flat_memory.cpp` (Linux), al reservar: si `sysconf(_SC_PAGESIZE)` no divide el tamaño o el desplazamiento de alguna región, `throw std::runtime_error("guest memory region <nombre> is not aligned to the <N>-byte system page")`.

- [ ] **Step 1: Compilar y recoger errores** — Run: `tools/linux/build.sh --arch aarch64` · Expected (primera vez): lista de errores x86 en nuestros archivos (candidatos ya vistos: `match_benchmark_overlay.inc`, `settings_overlay.cpp`, `host_context.cpp`, `host_cpu_baseline.cpp`, `ax_mix_kernels.h`).
- [ ] **Step 2: Arreglar cada uno en `wiicompiled-aarch64`** con la rama `#elif defined(__aarch64__)` o una versión escalar portable; nada de `-march` x86 en targets aarch64. La comprobación de página va según Interfaces.
- [ ] **Step 3: Compilar hasta el final** — Expected: "Done." y `file local/install-aarch64/MLBPowerPros2008` → `ARM aarch64`.
- [ ] **Step 4: No regresión x86_64 y Windows** — Run: `tools/linux/build.sh --no-roster-editor` y en Windows `tools\wiicompiled-build.ps1` + `tools\benchmark-match.ps1 -Scale 1 -Seconds 15 -GraphicsApi d3d12` · Expected: ambos compilan; Windows 60 FPS.
- [ ] **Step 5: Regenerar el parche y commit** — `git commit -m "aarch64: the game builds for Linux ARM64"`

### Task 5: Prueba de integración en QEMU (partido + cuadro)

**Files:**
- Create: `tools/linux/aarch64/qemu-match.sh`

**Interfaces:**
- Consumes: `local/install-aarch64/MLBPowerPros2008` (Task 4).
- Produces: `qemu-match.sh <api: vulkan|opengles> <cuadro> <salida.ppm> [minutos=60]` — corre el juego con `qemu-aarch64 -L <sysroot>`, `WIICOMPILED_MATCH_BENCH=1 WIICOMPILED_MATCH_SECONDS=0 WIICOMPILED_MATCH_SCALE=1 WIICOMPILED_MATCH_API=<api> AURORA_DUMP_PRESENT=<cuadro>:<salida.ppm>`, Vulkan por lavapipe (`VK_ICD_FILENAMES=<sysroot>/usr/share/vulkan/icd.d/lvp_icd.json`) o GLES por llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`); termina el proceso en cuanto el log dice `Present dump: frame <cuadro>` o al agotar los minutos; sale con 0 solo si el cuadro se escribió; guarda el log en `local/analysis/qemu-match-<api>.log`.

- [ ] **Step 1: Prueba de que el script falla sin binario** — Run: `tools/linux/aarch64/qemu-match.sh vulkan 300 /tmp/x.ppm 1` con `local/install-aarch64` vacío · Expected: código ≠ 0 y "MLBPowerPros2008 (aarch64) not found".
- [ ] **Step 2: Implementar el script** según Interfaces.
- [ ] **Step 3: Arranque** — Run: `tools/linux/aarch64/qemu-match.sh vulkan 300 local/tmp/aarch64-boot.ppm 30` · Expected: código 0; el log muestra el banner `Revolution OS`, `[match-bench] scene=` y `Present dump: frame 300`; el PPM convertido a PNG muestra la pantalla de carga o el panel del benchmark, no negro.
- [ ] **Step 4: Partido** — elegir el cuadro según la velocidad medida en el Step 3 (registrada en el log) para caer en el campo; Run: `tools/linux/aarch64/qemu-match.sh vulkan <cuadro> local/tmp/aarch64-field.ppm 90` · Expected: código 0 y el cuadro muestra el estadio y el bateador como el de x86 (`local/tmp/d3d11-frame.png` sirve de referencia). Si QEMU es demasiado lento para llegar en 90 minutos, se documenta la escena alcanzada y se para ahí (un solo intento largo, no varios).
- [ ] **Step 5: GLES** — repetir el Step 3 con `opengles` · Expected: igual que con Vulkan.
- [ ] **Step 6: Commit** — `git commit -m "aarch64: QEMU match probe"`

### Task 6: Documentación y roadmap

**Files:**
- Modify: `docs/roadmap/switch.md`, `docs/en/roadmap/switch.md`, `docs/linux.md`, `docs/en/linux.md`, `ROADMAP.md`, `ROADMAP.es.md`

- [ ] **Step 1:** En `switch.md` (ES/EN): casilla "Versión ARM64 funcionando" con lo verificado en QEMU (y lo que no: rendimiento real, aparato ARM real), los riesgos de Review Focus que aparecieron, y la lista de dependencias dinámicas (`readelf -d local/install-aarch64/MLBPowerPros2008 | grep NEEDED`).
- [ ] **Step 2:** En `linux.md` (ES/EN): cómo compilar para ARM64 (`make-sysroot.sh`, `build.sh --arch aarch64`) marcado como experimental.
- [ ] **Step 3:** ROADMAP (ES/EN): la fila de Switch pasa a "En curso: ARM64 en Linux" solo si Task 5 pasó.
- [ ] **Step 4: Commit y push** — `git commit -m "Docs: Linux ARM64 build (Switch phase 1-2)" && git push origin master:main`

---

## Planes siguientes (uno por subsistema, se escriben cuando este termine)

1. **Runtime en Horizon (libnx):** memoria plana del invitado con las APIs de memoria de Horizon, hilos y fibras (cambio de contexto aarch64 propio), archivos en la SD/romfs, audio (audren), mandos (HID / SDL de devkitPro). Prueba: el juego llega al menú sin gráficos (backend nulo) en la consola.
2. **Gráficos en Switch:** Dawn con su backend OpenGL ES sobre la Mesa de devkitPro (EGL), pasando `eglGetProcAddress` a Dawn; alternativa: renderer deko3d. Prueba: cuadro leído de la GPU en la consola.
3. **Instalación:** el Setup de PC genera el `.nro` y los datos para la SD desde la ISO del jugador; nada del juego se distribuye.
4. **Rendimiento en el hardware:** medir CPU (4× A57 a 1 GHz) y GPU (Tegra X1) con el benchmark; decidir 60 o 30 FPS y qué optimizar.

Los planes 1-4 necesitan una Switch con homebrew para probar.
