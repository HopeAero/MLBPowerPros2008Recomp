# Switch fase 1: el `.nro` arranca en la consola con gráficos fieles al PC — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** que MLB Power Pros 2008 compilado como `.nro` arranque en la Switch del usuario, llegue al menú y a un partido, y se vea como en el PC.

**Architecture:** el mismo runtime (Wiicompiled + parches de MLB) y la misma aurora, compilados con devkitA64 (GCC) en Docker. La capa Horizon (hilos, pila, `thread_local`, memoria con comprobaciones, SD) se trae de `nx-mod/wiicompiled-nx` como un parche nuevo. Los gráficos van por Dawn con el port Horizon de `nx-mod/dawn-nx` aplicado sobre nuestra versión de Dawn, y por Vulkan con el driver NVK precompilado de `nxvk`. Las pruebas en consola van por etapas: cada viaje del usuario a la SD responde una pregunta.

**Tech Stack:** devkitPro (devkitA64 GCC, libnx), Docker Desktop, CMake + Ninja, Dawn `v20260603.191052` + diff Horizon de `dawn-nx`, `nxvk` 26.2.2 (portlib precompilado), SDL3 de devkitPro (rama `switch-sdl-3.4`), Wiicompiled `6fa2473` + `tools/patches/*`.

**Spec:** [`docs/superpowers/specs/2026-10-25-switch-fase1-nro-design.md`](../specs/2026-10-25-switch-fase1-nro-design.md)

## Global Constraints

- Todo en la rama `switch-arm64`; master (`a497b6f`) no se toca.
- Cambios del runtime: parches en `tools/patches/`, listados en `tools/wiicompiled-translate.ps1` (y por tanto aplicados también por `tools/linux/build.sh`); generados con un script reproducible como `local/tmp/mkaarch64.sh`.
- devkitA64 es solo GCC: nada de flags ni extensiones exclusivas de clang en lo que compila para Switch.
- Nada del juego en git: ni la ISO, ni `.nro`, ni código generado, ni capturas. Salidas en `local/switch/` (ignorado por git). El hook `tools/check_no_game_data.py` debe pasar en cada commit.
- Windows debe seguir a 60 FPS (`tools/benchmark-match.ps1`, D3D12 x4, ≥59,8 FPS) y Linux x86 y aarch64 deben seguir llegando al primer lanzamiento (`gameplay phase=4`).
- Licencias: GPL-3.0 (Wiicompiled, `wiicompiled-nx`); Dawn BSD-3; Mesa/NVK MIT; SDL3 zlib. Cada componente de terceros, con commit fijado y hash en `config/tools.lock.json` o junto al Dockerfile.
- Firmware del usuario 22.5.0, Atmosphère 1.11.2; sin `nxlink`: cada prueba es copia manual a la SD.
- Documentación ES/EN (`docs/` y `docs/en/`); hallazgos de ingeniería inversa en `docs/ingenieria-inversa.md` con el PDF regenerado (`tools/build_re_pdf.py`).
- Respuestas al usuario siempre en español.

## Carpeta en la SD (fija para todas las tareas)

```
sdmc:/switch/mlbpowerpros2008/
├── MLBPowerPros2008.nro
├── Data/            los archivos del disco del usuario (sys/, files/), copiados de local/disc/DATA
├── UserData/        Config.toml, NAND, Cache (shaders), Benchmarks (informes del partido automático)
├── logs/            mlb.log (registro del runtime, vaciado línea a línea), crash-<fecha>.txt
└── autobench.txt    si existe, arranca el partido automático (contenido: segundos a medir, p. ej. 60)
```

## Review Focus

1. **HOME o apagado a mitad de partida:** `logs/mlb.log` y el informe parcial deben quedar legibles hasta la última línea escrita (sin `atexit`). Prueba en la Tarea 4 (matar el proceso de Linux y leer los archivos).
2. **Datos del disco ausentes o incompletos en la SD:** el juego debe decir qué falta (en pantalla y en `logs/mlb.log`) y salir, no colgarse ni crashear. Prueba en la Tarea 4.
3. **Modo applet sin memoria suficiente:** mensaje claro de que hay que abrirlo «sobre un juego» (manteniendo R), no un crash a mitad de carga. Prueba en las Tareas 1 (medición) y 4 (comprobación al arrancar).
4. **Primera carga con la caché vacía** (en Mario Kart, unos 2 minutos de precompilación de pipelines): debe verse progreso o un aviso, para que el usuario no lo tome por un cuelgue. Prueba en la Tarea 5.
5. **Regresión en PC:** el parche Switch no debe cambiar nada en Windows ni Linux. Prueba en las Tareas 3 y 5 (benchmark de Windows y primer lanzamiento en Linux).

---

### Tarea 1: imagen Docker, `.nro` mínimo y paquete de la primera prueba en consola

**Files:**
- Create: `tools/switch/Dockerfile`, `tools/switch/docker-build.sh` (entra en la imagen y ejecuta un comando con el repo montado en `/work`), `tools/switch/hello/{CMakeLists.txt,main.cpp,icon.jpg (generado, sin arte del juego)}`, `tools/switch/package-trip.sh`
- Modify: `config/tools.lock.json` (nxvk y la imagen de devkitPro, con hash)

**Interfaces:**
- Produce: imagen `mlb-switch-dev:1`; `tools/switch/docker-build.sh <cmd...>`; `local/switch/trip1/` con los `.nro` y un `LEEME.txt` de pasos.

- [ ] **Paso 0:** pedir al usuario que abra Docker Desktop. Comprobación: `docker version` muestra `Server`.
- [ ] **Paso 1: la prueba que falla.** `tools/switch/docker-build.sh cmake -S tools/switch/hello -B local/switch/hello -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake && cmake --build local/switch/hello` falla porque no existe la imagen.
- [ ] **Paso 2: Dockerfile** sobre `devkitpro/devkita64` (tag fijado por digest), con `dkp-pacman` para `switch-dev` y SDL3 de Switch (o la rama `switch-sdl-3.4` de `devkitPro/SDL` compilada en la imagen si no hay paquete), el portlib `nxvk-switch-portlib-26.2.2.tar.xz` de la release `v26.2.2-nx-mod-v1` (sha256 comprobado), python3, .NET 8, cmake y ninja.
- [ ] **Paso 3: `hello.nro`** (C++ con libnx): escribe en `sdmc:/switch/mlbpowerpros2008/logs/hello.log`, con `fflush` línea a línea: versión del firmware (`setsysGetFirmwareVersion`), si corre en modo applet (`appletGetAppletType`), memoria total y disponible (`svcGetInfo` con `InfoType_TotalMemorySize` y `InfoType_UsedMemorySize`), y la hora. También lo muestra en pantalla con `consoleInit` y sale con +. Se empaqueta con icono y NACP (`nacptool`, `elf2nro --icon --nacp`), porque un `.nro` sin ellos no arranca.
- [ ] **Paso 4:** el comando del Paso 1 produce `local/switch/hello/hello.nro`; `nxlink`/`elf2nro` dicen que trae icono y NACP (sección `ASET`).
- [ ] **Paso 5: `package-trip.sh trip1`** reúne en `local/switch/trip1/switch/mlbpowerpros2008/`: `hello.nro`, los `.nro` de prueba de `nxvk-switch-smoke-nros-26.2.2.tar.xz` (el de swapchain con `VK_NN_vi_surface`) y, cuando la Tarea 2 esté hecha, `dawn-nx-demo.nro`. Escribe `LEEME.txt` con: qué copiar a la SD, cómo abrir cada uno desde el menú homebrew **dos veces** (desde el álbum y «sobre un juego» manteniendo R) y qué archivos de `logs/` devolver.
- [ ] **Paso 6: commit** `Switch: devkitPro Docker image and a first test NRO`.

### Tarea 2: Dawn para Horizon sobre nuestra versión de Dawn

**Files:**
- Create: `tools/switch/dawn/make-dawn-horizon-patch.sh` (saca el diff de `dawn-nx` respecto a su base upstream), `tools/switch/dawn/dawn-horizon.patch`, `tools/switch/dawn/build-dawn.sh`
- Modify: `config/tools.lock.json` (commit de `dawn-nx`)

**Interfaces:**
- Consume: imagen de la Tarea 1.
- Produce: `local/switch/dawn/install/` con `libwebgpu_dawn.a` (monolítico, estático) y cabeceras de Dawn `v20260603.191052`; la función CMake `dawn_switch_link_nvk(<target>)` (de `switch_smoke_test/NvkLink.cmake`); `local/switch/dawn/dawn-nx-demo.nro`.

- [ ] **Paso 1:** encontrar la base upstream de `dawn-nx` (`switch` en `173c6191`): el merge-base con `google/dawn`. Guardar en el script el commit base y el de `dawn-nx`.
- [ ] **Paso 2:** `make-dawn-horizon-patch.sh` genera `dawn-horizon.patch` (los ~25 archivos del port: superficie `VK_NN_vi_surface`, Vulkan estático, huecos de plataforma, toolchain) y lo aplica sobre el tag `v20260603.191052`. Los conflictos se resuelven a mano siguiendo el mismo patrón; el parche resultante es el que se guarda.
- [ ] **Paso 3: la prueba que falla:** `build-dawn.sh` compila la demo `switch_smoke_test` de `dawn-nx` contra nuestro Dawn parcheado; falla hasta que el parche aplica y compila.
- [ ] **Paso 4:** compilar Dawn solo con Vulkan (`DAWN_ENABLE_VULKAN=ON`, resto de backends y ventanas en OFF, `DAWN_BUILD_MONOLITHIC_LIBRARY=STATIC`, `DAWN_BUILD_PROTOBUF=OFF`, `TINT_BUILD_IR_BINARY=OFF`, `_GNU_SOURCE`/`_DEFAULT_SOURCE` tras `project()`), más la demo como `dawn-nx-demo.nro` con icono y NACP.
- [ ] **Paso 5:** el `.nro` existe; `package-trip.sh trip1` lo incluye.
- [ ] **Paso 6: commit** `Switch: Dawn v20260603 with dawn-nx's Horizon port`.

### Viaje a la consola A (usuario)

Con `local/switch/trip1/`: el usuario copia la carpeta a la SD y abre `hello.nro`, el smoke de nxvk y `dawn-nx-demo.nro`, desde el álbum y «sobre un juego». Devuelve `logs/*.log` y lo que ve. **Preguntas que responde:** ¿arranca un `.nro` nuestro en FW 22.5.0? ¿Funciona Vulkan (NVK)? ¿Y Dawn (10 de 10 en la demo)? ¿Cuánta memoria hay en cada modo? Lo que salga queda en el ledger y decide si la Tarea 4 exige abrirlo «sobre un juego».

### Tarea 3: el runtime y el código traducido compilan y enlazan para Horizon (sin gráficos)

**Files:**
- Create: `tools/patches/wiicompiled-switch.patch` (generado), `local/tmp/switch_edit.py` (fuente del parche, como `aarch64_edit.py`), `tools/switch/build-nro.sh`
- Modify: `tools/wiicompiled-translate.ps1` (lista de parches: `'wiicompiled-switch'` tras `'wiicompiled-aarch64'`)

**Interfaces:**
- Consume: imagen (T1).
- Produce: `tools/switch/build-nro.sh [--graphics null|vulkan]` → `local/switch/build/MLBPowerPros2008.nro`; la macro `MKW_PLATFORM_SWITCH` en el CMake del runtime; las funciones `mkw::switch_layout::Root()` (devuelve `"sdmc:/switch/mlbpowerpros2008"`) y `mkw::switch_layout::Path(std::string_view rel)`.

- [ ] **Paso 1: la prueba que falla:** `build-nro.sh --graphics null` (traducción con el traductor de .NET sobre `local/disc/DATA`, como `build.sh`, y CMake con `Switch.cmake`) falla en la configuración: el runtime no conoce la plataforma.
- [ ] **Paso 2:** llevar de `wiicompiled-nx` (rama `switch`, commit fijado) a `switch_edit.py`, solo lo de Horizon: `runtime/platform_switch/{switch_layout,switch_thread_stack,switch_sdl_shim}.cpp`, `runtime/src/platform/switch/co_switch.S`, `runtime/include/{mkw_thread_local,switch_layout}.h`, las ramas Switch de `guest_flat_memory.{h,cpp}` (camino con comprobaciones, sin alias), `fiber_manager.cpp`, `isa/ppc_isa_config.h` (`MkwStateFreeResult2` sin `ext_vector_type`) y el bloque Switch de `runtime/CMakeLists.txt` y `PublicProducts.cmake` (flags `-D__SWITCH__ -fPIE -ftls-model=local-exec -march=armv8-a+crc+crypto -ffp-contract=off`, `-fsigned-char`). Todo protegido con `__SWITCH__` / `MKW_PLATFORM_SWITCH`. Lo que no sea de Horizon (red, NAND, mods, etc. de su fork) no entra.
- [ ] **Paso 3:** compilar con GCC: arreglar lo que falle de nuestros parches de MLB por extensiones de clang, siempre con un cambio que también compile en clang. Enlazado final con `-Wl,-z,notext` solo en el `.nro` (problema conocido de `libstdc++.a` de devkitA64).
- [ ] **Paso 4:** `build-nro.sh --graphics null` produce el `.nro` (con icono y NACP; nombre «MLB Power Pros 2008», autor «HopeAero»; icono de diseño propio sin arte del juego).
- [ ] **Paso 5: regresión en PC:** `mkaarch64.sh`-equivalente regenera la cadena de parches y aplica limpio; Windows: `tools/wiicompiled-build.ps1` + `tools/benchmark-match.ps1 -Seconds 30` ≥59,8 FPS; Linux x86 y teléfono (`tools/linux/aarch64/adb-match.sh`): primer lanzamiento.
- [ ] **Paso 6: commit** `Switch: the runtime and translated code build for Horizon`.

### Tarea 4: carpeta de la SD, registros que sobreviven, reporte de crash y partido automático por archivo

**Files:**
- Modify: `local/tmp/switch_edit.py` → `tools/patches/wiicompiled-switch.patch` (runtime: `runtime_config`, registro, manejador de crash, `match_benchmark`)
- Create: `tools/switch/make-sd.sh` (prepara `local/switch/sd/switch/mlbpowerpros2008/` con el `.nro`, `Data/` copiado de `local/disc/DATA`, `UserData/Config.toml` y `LEEME.txt`), `tools/switch/test-sd-layout.sh`

**Interfaces:**
- Consume: `mkw::switch_layout::Root()/Path()` (T3).
- Produce: con `MKW_PLATFORM_SWITCH` el directorio de datos es `Root()`, el de disco `Path("Data")`, el de usuario `Path("UserData")`; `logs/mlb.log` recibe todo `RT_LOG` con `fflush` por línea; el manejador de crash escribe `logs/crash-<AAAAMMDD-HHMMSS>.txt` con el mismo contenido que el volcado de PC (registro reciente, PC/LR del invitado, backchain); si existe `autobench.txt`, el runtime fija `WIICOMPILED_MATCH_BENCH=1` y `WIICOMPILED_MATCH_SECONDS=<contenido>`.

Para probar fuera de la consola, el mismo código de rutas acepta `MKW_SWITCH_ROOT_OVERRIDE=<dir>` en la compilación de Linux de prueba (solo con `MKW_SWITCH_LAYOUT_TEST`), de modo que `test-sd-layout.sh` lo ejecuta en Linux x86.

- [ ] **Paso 1: las pruebas que fallan** (`test-sd-layout.sh`, Linux x86 con el layout de prueba): (a) con `autobench.txt` = `30`, el log contiene `[match-bench]` y aparece un informe en `UserData/Benchmarks/`; (b) matando el proceso con `SIGKILL` a mitad del partido, `logs/mlb.log` termina en una línea completa y el informe parcial existe; (c) sin `Data/sys/main.dol`, el proceso sale con un mensaje que nombra `Data/sys/main.dol` y no crashea; (d) un crash provocado (`MKW_TEST_CRASH=1`) deja `logs/crash-*.txt` con `PC=` y `Stack backchain`.
- [ ] **Paso 2:** implementar lo de **Produce**. En la Switch, la comprobación de memoria al arrancar: si `svcGetInfo` da menos de lo que pidió el viaje A (umbral anotado en el ledger), mensaje en pantalla y en el log de que hay que abrirlo «sobre un juego» manteniendo R.
- [ ] **Paso 3:** las cuatro pruebas pasan; el `.nro` se recompila.
- [ ] **Paso 4:** `make-sd.sh` deja la carpeta lista con `LEEME.txt` (pasos del viaje B).
- [ ] **Paso 5: commit** `Switch: SD folder, crash-safe logs, crash report and autobench file`.

### Viaje a la consola B (usuario): el juego sin gráficos

`MLBPowerPros2008.nro` compilado con `--graphics null` y `autobench.txt` = `30`. El usuario lo copia con `Data/` y lo abre (como diga el viaje A). Devuelve `logs/` y `UserData/Benchmarks/`. **Pregunta que responde:** ¿el runtime corre en Horizon hasta el primer lanzamiento (`gameplay phase=4`) y con qué velocidad? Cada crash trae su reporte; se arregla con prueba y se repite el viaje.

### Tarea 5: gráficos con Dawn + Vulkan (NVK)

**Files:**
- Modify: `local/tmp/switch_edit.py` → parche (aurora: `BackendBinding.cpp` con la superficie `SurfaceSourceViNN`/`nwindowGetDefault()` del parche de Dawn, `AuroraDawnProvider.cmake` con el Dawn de la Tarea 2, ventana, mandos y audio sobre SDL3 de Switch, o `window_switch.cpp`/`input_switch.cpp` y la rama audren de `audio_backend.cpp` de `wiicompiled-nx` donde SDL3 no llegue, `imgui_switch.cpp`); `tools/switch/build-nro.sh` (`--graphics vulkan`, enlaza con `dawn_switch_link_nvk`)

**Interfaces:**
- Consume: Dawn de la Tarea 2; layout de la Tarea 4.
- Produce: `build-nro.sh --graphics vulkan` → `.nro` con `graphics_api` `auto` = Vulkan; caché de shaders de Dawn en `UserData/Cache`.

- [ ] **Paso 1: la prueba que falla:** `build-nro.sh --graphics vulkan` no enlaza (sin superficie ni Dawn de Switch).
- [ ] **Paso 2:** implementar lo de **Files**. Durante la precompilación de pipelines, la pantalla de carga de aurora (o un texto) muestra el progreso, y el log registra `Pipeline prewarm` al empezar y al acabar.
- [ ] **Paso 3:** enlaza. Fuera de la consola no se puede ejecutar (es Horizon), así que la verificación local es que enlaza, que el `.nro` trae icono y NACP, y la regresión en PC del Paso 5 de la Tarea 3. Los mandos y el audio se comprueban en el viaje C (el log registra el mando detectado y que el audio abre su dispositivo).
- [ ] **Paso 4: commit** `Switch: graphics through Dawn and NVK`.

### Tarea 6: capturas en los mismos momentos, en la Switch y en el PC

**Files:**
- Modify: el parche `wiicompiled-match-benchmark.patch` (o `switch_edit.py` si queda solo para Switch, pero debe funcionar también en PC): capturas por evento del partido automático
- Create: `tools/switch/compare-captures.py` (página HTML local, lado a lado, en `local/switch/compare/`; no se publica)

**Interfaces:**
- Produce: con `WIICOMPILED_MATCH_CAPTURES=1` (o `captures` en `autobench.txt`), el partido automático guarda PNG del cuadro presentado en tres momentos: `scene=419` + 60 cuadros, `scene=409` + 120 cuadros y `gameplay phase=4` + 30 cuadros, como `UserData/Benchmarks/capture-<momento>.png` (usa la ruta de volcado de presentación de aurora, `AURORA_DUMP_PRESENT`).

- [ ] **Paso 1: la prueba que falla:** en Windows (D3D12, x1, sin mejoras) con capturas activadas, no aparecen los tres PNG.
- [ ] **Paso 2:** implementar; en PC aparecen los tres PNG, con la imagen correcta (se mira cada uno).
- [ ] **Paso 3:** `compare-captures.py <dir PC> <dir Switch>` genera la página lado a lado.
- [ ] **Paso 4: commit** `Match benchmark: captures at fixed moments, for the Switch fidelity check`.

### Viaje a la consola C (usuario): el juego completo

`.nro` con `--graphics vulkan`, `autobench.txt` con `30 captures`. El usuario lo abre, mira si llega al menú y al partido, y devuelve `logs/`, `UserData/Benchmarks/` (informe y capturas). Se genera la comparación con las capturas del PC y **el usuario decide a ojo** si se ve fiel. Si algo se ve roto, cada fallo gráfico se investiga con su prueba y se repite el viaje.

### Tarea 7: documentación y cierre

**Files:**
- Modify: `docs/roadmap/switch.md`, `docs/en/roadmap/switch.md`, `ROADMAP.md`, `ROADMAP.es.md`, `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md`
- Create: `docs/switch.md`, `docs/en/switch.md` (cómo compilar el `.nro` desde tu copia y cómo copiarlo a la SD, con la carpeta de arriba)

- [ ] **Paso 1:** escribir lo de **Files** con lo medido en los viajes (velocidad, memoria, modo de arranque) y lo que falta (deko3d, launcher, Setup). Regenerar los PDF de ingeniería inversa.
- [ ] **Paso 2: commit y push** `Docs: Switch phase 1` → `git push origin switch-arm64`.
