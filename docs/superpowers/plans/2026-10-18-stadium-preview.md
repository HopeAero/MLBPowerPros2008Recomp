# Vista previa de estadios en el juego — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** un botón «Ver en el juego» en el complemento de Blender que exporta el estadio a un mod temporal y abre el recomp en un modo visor: directo al estadio (~8 s), sin jugadores ni HUD, con cámara libre y las cámaras reales del partido.

**Architecture:** el runtime gana un modo visor (`stadium_viewer`) activado por variables de entorno que se apoya en el camino del benchmark de partido (`match_benchmark`) para cargar el estadio igual que un partido, carga solo el mod de vista previa con su propia caché, y controla la cámara del juego (localizada por ingeniería inversa) con una cámara libre. El complemento exporta con `exporter.export_stadium` a una carpeta temporal y lanza `WiiCompiled.exe` con esas variables.

**Tech Stack:** C++20 del runtime Wiicompiled (parches en `tools/patches/`), SDL3 (teclado/ratón), Python de Blender 4.2 (complemento `powerpros_stadium`).

**Spec:** `docs/superpowers/specs/2026-10-18-stadium-preview-design.md` (inglés: `docs/en/superpowers/specs/2026-10-18-stadium-preview-design.md`).

## Global Constraints

- Nada de datos del juego en el repo; `tools/check_no_game_data.py` corre como hook de pre-commit.
- El runtime se edita en `local/wiicompiled-mlb/src/runtime` y se guarda como el parche nuevo `tools/patches/wiicompiled-stadium-viewer.patch`, añadido al final de la lista de `tools/wiicompiled-translate.ps1`. El parche se regenera con `git -C local/wiicompiled-mlb/src diff > tools/patches/wiicompiled-stadium-viewer.patch` (el índice de ese repo ya contiene la pila anterior; los archivos nuevos se registran antes con `git add -N`). Tras cada regeneración, la pila completa debe aplicar sobre un clon limpio del Wiicompiled fijado (`config/tools.lock.json`).
- Un `PPC_NATIVE_OVERRIDE` nuevo exige volver a ejecutar `tools/wiicompiled-translate.ps1` (sin `-Fresh`) antes de `tools/wiicompiled-build.ps1`.
- Sin el modo visor activo, nada cambia: partido, benchmark (`WIICOMPILED_MATCH_BENCH=1`) y launcher se comportan como antes.
- La vista previa no escribe `UserData/Config.toml`, no cambia `[mods] enabled` ni la copia normal de `project.bin` (`<cachePath>/mods/disc`).
- Interfaz del complemento en español e inglés con `T(es, en)`; documentación en `docs/` y su espejo `docs/en/`.
- El complemento sigue funcionando sin el recomp: si `pp_game_folder` no tiene `WiiCompiled.exe`, lo dice.
- Python del complemento: sin dependencias nuevas (Blender no trae Pillow).
- Pruebas que deben seguir pasando: `tools/blender/test_import.py`, `test_export.py`, `test_convert.py`, `powerpros_stadium/selftest.py`, las de `runtime/tests` y `tools/test-launcher.ps1`.
- Prueba en el juego: avisar antes de abrir ventanas visibles; el usuario no debe tocar mando ni teclado. Usar el partido de prueba (`WIICOMPILED_MATCH_BENCH=1`) y capturas con `PrintWindow`.

## Review Focus

1. **Rutas con espacios, paréntesis y acentos** (`Mi Estadio (local)`, carpetas de usuario como `C:\Users\José`): el runtime debe leer las variables de entorno como UTF-16 (`_wgetenv`) y convertirlas a `std::filesystem::path`. Lo prueba la Task 1 (`ParseConfig` con una ruta no ASCII) y la Task 7 (vista previa real desde una carpeta con espacios y paréntesis).
2. **La exportación falla** (por ejemplo, más de 30 texturas en una pieza): el error sale en Blender y el juego no se abre. Lo prueba la Task 6 (`test_preview.py`, caso `ExportFailure`).
3. **Falta `WiiCompiled.exe` en `pp_game_folder`** o la colección no tiene `pp_game_folder`: mensaje claro, sin excepción sin capturar. Task 6.
4. **Después de una vista previa, el juego normal sigue igual:** `Config.toml` byte a byte, el sello `<cachePath>/mods/disc/project.bin.sources` sin cambios. Task 2 (comprobación en el juego) y Task 7.
5. **La cámara libre en posiciones extremas** (bajo el suelo, a 2 km, mirando recto arriba o abajo): ni cuelgue ni NaN. `pitch` limitado a ±89°, velocidad limitada, sin división por cero al normalizar. Lo prueba la Task 4 (`free_camera_tests.cpp`).

---

### Task 1: Configuración del modo visor, mod de una sola ejecución y caché propia

**Files:**
- Create: `local/wiicompiled-mlb/src/runtime/include/stadium_viewer.h`
- Create: `local/wiicompiled-mlb/src/runtime/src/stadium_viewer.cpp`
- Modify: `local/wiicompiled-mlb/src/runtime/src/mods.cpp` (`Discover()` y el directorio de `ApplyProjectPacks`)
- Test: `local/wiicompiled-mlb/src/runtime/tests/stadium_viewer_tests.cpp` (registrar en `runtime/CMakeLists.txt` como `mkw_stadium_viewer_tests`, igual que `mkw_auto_mips_tests`)

**Interfaces:**
- Produces (en `stadium_viewer.h`, namespace `stadium_viewer`):
  - `struct CameraPose { float eye[3]; float yaw; float pitch; float fovY; };` en el marco del juego: decímetros, Y arriba; `yaw` en radianes, 0 = mirando hacia segunda base, positivo hacia primera; `pitch` en radianes, positivo hacia arriba; `fovY` en grados.
  - `struct Config { std::filesystem::path modDir; std::string code; std::optional<CameraPose> camera; };`
  - `std::optional<Config> ParseConfig(const std::wstring& modDir, const std::wstring& code, const std::wstring& camera);`: vacío si `modDir` está vacío; `code` vacío significa `"pp_preview"`; `camera` = `"x,y,z,yaw,pitch,fov"` (punto decimal, sin espacios); cámara mal formada → `camera` vacío (el resto vale).
  - `const std::optional<Config>& Active();`: lee una vez `WIICOMPILED_STADIUM_VIEWER`, `WIICOMPILED_STADIUM_VIEWER_CODE` y `WIICOMPILED_STADIUM_VIEWER_CAMERA` con `_wgetenv` (en no Windows, `getenv` + UTF-8).
  - `bool Enabled();` = `Active().has_value()`.

- [ ] **Step 1: Escribir las pruebas que fallan** en `stadium_viewer_tests.cpp` (estilo `match_benchmark_tests.cpp`, `require` que aborta):
  - `ParseConfig(L"", L"", L"")` → vacío.
  - `ParseConfig(L"C:\\Temp\\Vista previa (José)", L"", L"")` → `modDir == std::filesystem::path(L"C:\\Temp\\Vista previa (José)")`, `code == "pp_preview"`, sin cámara.
  - `ParseConfig(L"D:\\m", L"miestadio", L"12.5,30,-400,0.5,-0.25,45")` → `code == "miestadio"`, cámara `{12.5, 30, -400}`, `yaw 0.5`, `pitch -0.25`, `fovY 45`.
  - `ParseConfig(L"D:\\m", L"", L"1,2,3")` → cámara vacía; `ParseConfig(L"D:\\m", L"", L"1,2,3,x,0,45")` → cámara vacía.
- [ ] **Step 2: Compilar y ver que fallan** — `local/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe -std=c++20 -static -I local/wiicompiled-mlb/src/runtime/include local/wiicompiled-mlb/src/runtime/tests/stadium_viewer_tests.cpp local/wiicompiled-mlb/src/runtime/src/stadium_viewer.cpp -o <scratchpad>/stadium_viewer_tests.exe` → error de compilación (no existe `stadium_viewer.h`).
- [ ] **Step 3: Implementar `stadium_viewer.h/.cpp`.** `ParseConfig` no depende de nada del runtime (para que la prueba compile sola); la lectura del entorno vive en `Active()`.
- [ ] **Step 4: Ejecutar las pruebas** — mismo comando y ejecutar el `.exe` → código de salida 0.
- [ ] **Step 5: En `mods.cpp`:** si `stadium_viewer::Enabled()`, `Discover()` devuelve solo el mod de `Active()->modDir` con `enabled = true` (los de `Directory()` se ignoran y `Config.toml` no se lee ni se escribe para ellos); `ApplyProjectPacks` usa `<cachePath>/preview/mods/disc` en vez de `<cachePath>/mods/disc`, y la caché de pancartas (`mods/banners`) también va bajo `preview/`. Log: `[viewer] mod "<modDir>" (code <code>)`.
- [ ] **Step 6: Comprobar en el juego que el juego normal no cambia** — copiar `build/UserData/Config.toml` y `build/UserData/Cache/mods/disc/project.bin.sources`, compilar (`tools/wiicompiled-build.ps1`) y lanzar el benchmark normal 40 s (`tlog.py`-style o `tools/benchmark-match.ps1 -Seconds 20`). Después: ambos archivos byte a byte iguales; el log trae `gameplay phase=4`.
- [ ] **Step 7: Regenerar el parche y commit**

```bash
git -C local/wiicompiled-mlb/src add -N runtime/include/stadium_viewer.h runtime/src/stadium_viewer.cpp runtime/tests/stadium_viewer_tests.cpp
git -C local/wiicompiled-mlb/src diff > tools/patches/wiicompiled-stadium-viewer.patch
git add tools/patches/wiicompiled-stadium-viewer.patch tools/wiicompiled-translate.ps1
git commit -m "Stadium viewer: env config, one-run preview mod, own project.bin cache"
```

### Task 2: Arranque directo al estadio

**Files:**
- Modify: `local/wiicompiled-mlb/src/runtime/src/match_benchmark.cpp`
- Modify: `local/wiicompiled-mlb/src/runtime/src/stadium_viewer.cpp`
- Modify: `local/wiicompiled-mlb/src/runtime/src/launcher.cpp` (`AutomatedRun`)

**Interfaces:**
- Consumes: `stadium_viewer::Enabled()`, `stadium_viewer::Active()->code` (Task 1).
- Produces: `bool stadium_viewer::StadiumReady();` true desde que el partido llega a `gameplay phase>=4 status==2` (lo mismo que `match_benchmark::GameplayReady()`); log una vez `[viewer] stadium ready at <s> s`.

- [ ] **Step 1: `match_benchmark::Enabled()`** también es true con `stadium_viewer::Enabled()`; el estadio forzado es `Active()->code` en lugar de `WIICOMPILED_MATCH_BENCH_STADIUM` (si existe esa variable, sigue mandando el código del visor). `WIICOMPILED_MATCH_SECONDS` sin efecto en el visor (no se cierra solo).
- [ ] **Step 2: Pantalla de la correa:** en el visor, si `WIICOMPILED_PAD_SCRIPT` no está definida, se define antes del primer `PADRead` como `2500-2700:100,3500-3700:100` (A dos veces). Medido: estadio en pantalla a ~8 s.
- [ ] **Step 3: `AutomatedRun()`** en `launcher.cpp` cuenta `WIICOMPILED_STADIUM_VIEWER` para no mostrar el launcher.
- [ ] **Step 4: Ocultar el panel del benchmark** («Prueba GPU – …») y el contador de FPS en el visor; el panel de «Preparando partido…» puede quedarse hasta `StadiumReady()`.
- [ ] **Step 5: Compilar y medir** — crear a mano un mod de vista previa copiando un mod de estadio de `build/mods` a `<scratchpad>/Vista previa (José)` con `code = "pp_preview"` en su `mod.toml` y los archivos renombrados a `stadium_pp_preview*.pack` / `env_stadium_menu_pp_preview.pack`; lanzar `WiiCompiled.exe` con `WIICOMPILED_STADIUM_VIEWER=<esa ruta>` y registrar la salida con marcas de tiempo. Esperado: `[viewer] mod ...`, `[stadiums] loading "..."`, `[viewer] stadium ready at` ≤ 12 s, ningún `unmapped guest touch`; captura sin launcher ni panel del benchmark; `Config.toml` y el sello normal de `project.bin` sin cambios.
- [ ] **Step 6: Regenerar el parche y commit** (`Stadium viewer: boot straight into the preview stadium`).

### Task 3: Cámara del juego (ingeniería inversa)

**Files:**
- Create: `local/wiicompiled-mlb/src/runtime/include/mlb_camera.h`
- Create: `local/wiicompiled-mlb/src/runtime/src/hle/mlb/game_camera.cpp` (nombres únicos: el directorio `hle/mlb` se compila como una sola unidad)
- Modify: `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md` (sección nueva «Cámara del partido»), y regenerar los PDF con `tools/build_re_pdf.py` (ambos idiomas)

**Interfaces:**
- Consumes: `stadium_viewer::CameraPose` (Task 1).
- Produces (namespace `game_camera`):
  - `enum class MatchView { Batter, Pitcher, Aerial };`
  - `void SetOverride(const stadium_viewer::CameraPose& pose);`: desde el siguiente fotograma, el juego dibuja con esa cámara (posición, orientación y campo de visión).
  - `void ClearOverride();`: el juego vuelve a su cámara.
  - `bool ShowMatchView(MatchView view);`: el juego usa su propia cámara de esa vista (posición, encuadre, campo de visión y la visibilidad de piezas que dependa de ella); false si aún no se puede (partido no listo).
  - `std::optional<stadium_viewer::CameraPose> CurrentPose();`: la cámara con la que el juego dibujó el último fotograma, en el marco de `CameraPose` (para que la cámara libre arranque desde una vista del partido).

- [ ] **Step 1: Localizar la cámara.** Pistas, en este orden:
  - `C_MTXPerspective` (`0x800A913C`) y `GXSetProjection` (`0x800C8A9C`): buscar quién las llama en `generated/build_shards/base_common/*.cpp` (llamadas `MKW_STATIC_TRANSLATED_CALL(0x800A913Cu, ...)` / `InvokeDirectCpu<0x800A913Cu>`); el campo de visión sale de un objeto cámara del REL.
  - Ese objeto cámara guarda posición y objetivo (o una matriz de vista) que el código propio del juego convierte en la vista. `C_MTXLookAt` (`0x800A8DC4`) **no** es la cámara del partido.
  - Instrumentar con envoltorios temporales en el shard generado (renombrar la función a `*_probe_orig`, añadir un envoltorio con `fprintf(stderr, ...)`), como se hizo con `HeapAlloc`. Ojo: un envoltorio de `C_MTXLookAt` colgó el arranque tras iniciar GX; si pasa, probar otra función o un contador sin `fprintf`. Un `tools/wiicompiled-translate.ps1` normal borra las sondas; no deben entrar en el parche.
  - Las vistas del partido: localizar dónde elige el juego la cámara del bateador, la del pitcher y la aérea (el benchmark ya pasa por las tres sin intervención; `match + 14964` es la fase del partido).
- [ ] **Step 2: Documentar** en la sección nueva de los dos documentos de ingeniería inversa: direcciones, campos y unidades del objeto cámara, quién lo escribe en cada fotograma, cómo se eligen las vistas, y cómo se probó.
- [ ] **Step 3: Implementar `game_camera`** sobre lo encontrado (escribir el objeto cámara tras la actualización del juego y antes del dibujado, o sustituir la función que construye la vista con un `PPC_NATIVE_OVERRIDE` nativo; lo decide quien implementa según lo encontrado). Volver a ejecutar `tools/wiicompiled-translate.ps1` si hay un override nuevo.
- [ ] **Step 4: Verificar en el juego** con una prueba temporal (variable de entorno solo de prueba, no en el parche final): en el partido de prueba, `SetOverride({eye {0, 300, -900}, yaw 0, pitch -0.35, fovY 45})` (30 m de alto, 90 m detrás del plato, mirando al campo). La captura debe mostrar el estadio desde detrás del plato en alto; `ClearOverride()` devuelve la vista normal; `ShowMatchView` con cada valor da la vista correspondiente; 2 minutos sin `unmapped guest touch`.
- [ ] **Step 5: Regenerar el parche y commit** (`Stadium viewer: game camera override and real match views` + docs + PDFs fuera del repo).

### Task 4: Cámara libre

**Files:**
- Create: `local/wiicompiled-mlb/src/runtime/include/free_camera.h` (solo cabecera, sin dependencias del runtime, para la prueba)
- Modify: `local/wiicompiled-mlb/src/runtime/src/stadium_viewer.cpp` (entrada SDL y aplicación cada fotograma)
- Test: `local/wiicompiled-mlb/src/runtime/tests/free_camera_tests.cpp` (registrar `mkw_free_camera_tests`)

**Interfaces:**
- Consumes: `stadium_viewer::CameraPose` (Task 1), `game_camera::SetOverride/ClearOverride/ShowMatchView/CurrentPose` (Task 3), `stadium_viewer::StadiumReady()` (Task 2).
- Produces (en `free_camera.h`, namespace `stadium_viewer`):
  - `struct FreeCameraInput { bool forward, back, left, right, up, down, fast, look; float mouseDx, mouseDy, wheel, dt; };`
  - `class FreeCamera { public: explicit FreeCamera(const CameraPose& start); void Update(const FreeCameraInput& in); const CameraPose& Pose() const; float Speed() const; };`
  - Valores: velocidad inicial 100 dm/s (10 m/s); `fast` la multiplica por 4; cada paso de rueda la multiplica o divide por 1,25, limitada a [10, 2000] dm/s; ratón 0,0035 rad por píxel con `look`; `pitch` limitado a ±89°; `up`/`down` mueven en Y del mundo; adelante/atrás según `yaw` y `pitch`; `dt` limitado a 0,1 s.

- [ ] **Step 1: Escribir las pruebas que fallan** en `free_camera_tests.cpp`:
  - desde `{eye {0,0,0}, yaw 0, pitch 0}`, `forward` 1 s → el ojo avanza 100 dm hacia segunda base y nada en Y;
  - `right` 1 s con `yaw 0` → avanza 100 dm hacia primera base (el mismo sentido en que `yaw` positivo gira);
  - `up` 0,5 s → Y += 50; `fast` + `forward` 1 s → 400 dm;
  - `look` con `mouseDy = -100000` → `pitch == +89°` (en radianes, tolerancia 1e-4); sin `look` el ratón no gira;
  - `wheel = +100` → velocidad 2000; `wheel = -100` → 10;
  - `dt = 5` con `forward` → avanza como con 0,1 s;
  - ojo en `{1e6, -1e6, 1e6}` y `pitch` a ±89° → ningún componente NaN ni infinito tras 100 actualizaciones.
- [ ] **Step 2: Compilar y ver que fallan** (`clang++ -std=c++20 -static`, como en la Task 1).
- [ ] **Step 3: Implementar `FreeCamera`** en `free_camera.h`.
- [ ] **Step 4: Ejecutar las pruebas** → salida 0.
- [ ] **Step 5: Conectar en el visor** (solo cuando `StadiumReady()`, cada fotograma en el hilo que ya llama a `match_benchmark::OnHostFrame`): teclado con `SDL_GetKeyboardState`, ratón con `SDL_GetMouseState` / movimiento relativo mientras se mantiene el botón derecho (capturar el ratón solo mientras tanto), rueda desde el evento de SDL que ya procese el runtime o aurora. Teclas: W/A/S/D, Q baja, E sube, Shift rápido; `1` bateador, `2` pitcher, `3` aérea (`ShowMatchView`), `F` vuelve a la cámara libre partiendo de `CurrentPose()`; mover la cámara libre (WASD o ratón) estando en una vista del partido también pasa a cámara libre desde esa vista. Al arrancar: cámara libre desde `Active()->camera` si vino; si no, vista del bateador. Un rótulo pequeño y discreto en una esquina con las teclas, en el idioma de `[ui] language` (`Tr` del launcher o equivalente); se oculta con `H`.
- [ ] **Step 6: Verificar en el juego** con el mod de la Task 2 y `WIICOMPILED_STADIUM_VIEWER_CAMERA=0,300,-900,0,-0.35,45`: la primera captura es esa vista; con `WIICOMPILED_PAD_SCRIPT` no se puede simular teclado, así que el movimiento se comprueba a mano con el usuario, o con una variable temporal de prueba que inyecte `FreeCameraInput` (no en el parche final). Las teclas 1/2/3 dan las vistas del partido.
- [ ] **Step 7: Regenerar el parche y commit** (`Stadium viewer: free camera`).

### Task 5: Partido quieto, sin jugadores ni HUD

**Files:**
- Modify: `local/wiicompiled-mlb/src/runtime/src/hle/mlb/game_camera.cpp` o un archivo nuevo `src/hle/mlb/viewer_scene.cpp` (nombres únicos)
- Modify: `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md` (lo encontrado), PDFs

**Interfaces:**
- Consumes: `stadium_viewer::Enabled()`, `StadiumReady()`.
- Produces: `void viewer_scene::Apply(CpuContext* cpu);` llamado cada fotograma del invitado en el visor (desde donde se llama `match_benchmark::OnGuestFrame`).

- [ ] **Step 1: Localizar** (ingeniería inversa, mismas técnicas de la Task 3): qué hace avanzar la lógica del partido (lanzamientos, jugadores, pelota) y qué dibuja jugadores, pelota, marcador y HUD 2D. El objetivo es parar solo la lógica del partido: el invitado sigue corriendo y el estadio sigue animado (público, banderas, marcadores).
- [ ] **Step 2: Implementar `viewer_scene::Apply`** para parar la lógica y no dibujar jugadores, pelota ni HUD. Si ocultar a los jugadores resulta inviable en un tiempo razonable, la primera versión los muestra (lo dice el spec) y se deja anotado en el documento de ingeniería inversa como pendiente; la parada de la lógica y el HUD sí son obligatorios.
- [ ] **Step 3: Verificar en el juego:** dos capturas con 10 s de diferencia en el visor: jugadores y pelota quietos (o ausentes) y sin marcador ni HUD; las banderas y el público del estadio cambian entre capturas; 2 minutos sin `unmapped guest touch`; el benchmark normal (sin visor) sigue jugando el partido (`gameplay phase` avanza, marcador visible).
- [ ] **Step 4: Regenerar el parche y commit** (`Stadium viewer: freeze the match, hide players and HUD`).

### Task 6: Botón «Ver en el juego» en Blender

**Files:**
- Create: `tools/blender/powerpros_stadium/preview.py`
- Modify: `tools/blender/powerpros_stadium/blender_ui.py` (operador y botón en el panel), `blender_manifest.toml` (versión 0.7.0)
- Test: `tools/blender/test_preview.py` (sin interfaz, mismos argumentos que `test_export.py`)

**Interfaces:**
- Consumes: `exporter.export_stadium(coll, mods_folder, mod_name, target_code, new_name, hd, menu, home_line)` y `exporter.ExportFailure` (existentes), `importer.FROM_BLENDER` (existente).
- Produces (en `preview.py`):
  - `PREVIEW_CODE = "pp_preview"`; `PREVIEW_MOD = "Vista previa"`.
  - `def preview_folder() -> pathlib.Path`: `<tempfile.gettempdir()>/powerpros_preview` (se borra y se rehace en cada exportación).
  - `def camera_pose(view_matrix: mathutils.Matrix, lens_mm: float, sensor_mm: float = 72.0) -> tuple[float, float, float, float, float, float]`: `(x, y, z, yaw, pitch, fovY)` en el marco del juego (`CameraPose`, Task 1) a partir de la matriz de vista de la región 3D de Blender (metros, Z arriba, segunda base +Y).
  - `def environment(mod_dir: pathlib.Path, pose) -> dict[str, str]`: las tres variables `WIICOMPILED_STADIUM_VIEWER*` con la cámara en el formato de la Task 1.
  - `def launch(coll, pose, *, dry_run: bool = False) -> dict`: exporta (`export_stadium(coll, preview_folder(), PREVIEW_MOD, PREVIEW_CODE, PREVIEW_MOD, hd=True, menu=False, home_line="")`), comprueba `<pp_game_folder>/WiiCompiled.exe`, cierra el visor anterior lanzado desde esta sesión de Blender (proceso guardado en el módulo; si el usuario ya lo cerró, se ignora) y lo lanza con `subprocess.Popen` y `cwd=pp_game_folder`. Con `dry_run`, no lanza nada y devuelve `{"exe", "env", "mod_dir", "stats"}`. Errores como `exporter.ExportFailure` con mensaje `T(...)`.

- [ ] **Step 1: Escribir las pruebas que fallan** en `test_preview.py` (estilo `check(ok, label)` de `test_export.py`):
  - importar Coors Field, una vista mirando desde `(0, -90, 30)` m hacia el plato → `camera_pose` da `x≈0`, `y≈300`, `z≈900` dm (el plato en el origen, detrás del plato es −Y en Blender = +Z en el juego), `yaw≈0` (mirando hacia segunda base), `pitch<0`;
  - `launch(coll, pose, dry_run=True)` con `pp_game_folder` apuntando a una carpeta temporal con un `WiiCompiled.exe` vacío → existe `<preview_folder()>/Vista previa/project/stadium_pp_preview.pack`, `mod.toml` declara `code = "pp_preview"`, el `env` trae las tres variables y `WIICOMPILED_STADIUM_VIEWER` es esa carpeta;
  - sin `WiiCompiled.exe` → `ExportFailure` cuyo texto nombra la carpeta;
  - con una pieza de más de 30 texturas → `ExportFailure` (la del exportador) y no se lanza nada;
  - la carpeta de mods del juego (`pp_game_folder/mods`) no cambia.
- [ ] **Step 2: Ejecutar y ver que fallan** — `local/tools/blender-4.2.23-windows-x64/blender.exe --background --factory-startup --python tools/blender/test_preview.py -- <zip> <project.bin> <carpeta temporal>` (con `BLENDER_USER_RESOURCES` temporal y rutas `cygpath -aw`) → `RESULT: FAIL`.
- [ ] **Step 3: Implementar `preview.py`.** Convención de `yaw`: `atan2(dir_juego.x, -dir_juego.z)`. Coherente con la Task 4 (0 = hacia segunda base, positivo hacia primera); `pitch = asin(dir_juego.y)`; `fovY = 2·atan((sensor/2)/lens)` con la relación de aspecto de la región.
- [ ] **Step 4: Operador `PP_OT_preview_in_game`** en `blender_ui.py` («Ver en el juego» / «View in game»), en el panel Power Pros bajo «Exportar mod de estadio»; `poll` = hay estadio importado; toma la vista 3D activa (`context.region_data`) o, sin ella, una vista desde detrás del plato; informa el error de `ExportFailure` con `self.report({"ERROR"}, ...)`.
- [ ] **Step 5: Ejecutar `test_preview.py`, `test_export.py`, `test_convert.py` y `selftest.py`** → `RESULT: PASS` / `selftest OK`.
- [ ] **Step 6: Commit** (`Stadium add-on 0.7.0: View in game button`).

### Task 7: Integración, documentación y comprobación de punta a punta

**Files:**
- Modify: `docs/stadiums.md`, `docs/en/stadiums.md` (sección «Ver en el juego»: qué hace, teclas, tiempos, que usa su propia caché ~400 MB, que solo carga el estadio que estás haciendo)
- Modify: `docs/wiicompiled-feasibility.md`, `docs/en/wiicompiled-feasibility.md` (sección del modo visor con lo medido)
- Modify: `tools/wiicompiled-translate.ps1` (si no se hizo en la Task 1)

- [ ] **Step 1: Pila de parches desde cero** — clon limpio del Wiicompiled fijado, aplicar todos los parches de la lista → todos aplican; `tools/wiicompiled-translate.ps1` + `tools/wiicompiled-build.ps1` compilan; `runtime/tests` pasan.
- [ ] **Step 2: Punta a punta desde Blender sin interfaz** — un script temporal importa un estadio de prueba de `local/analysis/` o Coors Field, llama a `preview.launch(coll, pose)` real con `pp_game_folder = local/wiicompiled-mlb/build`, con la carpeta temporal renombrada a una con espacios y paréntesis. Esperado: el juego abre sin launcher, `[viewer] stadium ready` ≤ 12 s después de lanzar, captura con la vista pedida, sin jugadores ni HUD. Segundo `launch` → el primer proceso se cierra y el nuevo muestra el cambio (por ejemplo, una pieza movida 20 m).
- [ ] **Step 3: El juego normal sigue igual** — tras lo anterior, `Config.toml` y `<cachePath>/mods/disc/project.bin.sources` sin cambios; benchmark normal con los mods del usuario → `gameplay phase=4`, marcador y jugadores visibles.
- [ ] **Step 4: Prueba a mano con el usuario** (avisando antes): pulsa «Ver en el juego» en Blender, vuela con la cámara, prueba 1/2/3/F/H, cierra y repite. Anotar lo que diga.
- [ ] **Step 5: Documentación ES/EN** (archivos de arriba) y commit (`Stadium preview: docs and end-to-end check`).
