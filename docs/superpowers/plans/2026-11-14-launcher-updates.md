# Actualizaciones desde el launcher: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Objetivo:** el launcher enseña su versión, avisa de la última («Nueva versión vX.Y.Z», enlace a la
Discussion, Sí/No) y, con «Sí», baja solo las piezas que cambiaron, recompila lo afectado, cambia la
versión al final y ofrece «Volver a vX.Y.Z» con recuperación de partidas.

**Arquitectura:** cada release publica `update-manifest.txt` (texto plano, una línea por dato) y el
payload partido en piezas zip. El launcher lee el manifiesto en
`https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/latest/download/update-manifest.txt`
(GitHub redirige al último release no borrador ni prerelease; sin API ni límite de consultas), baja con
`curl` las piezas cuya huella difiere de la instalada y llama a un script (`Update.ps1` en Windows,
`tools/linux/update.sh` en Linux/Deck). El script sincroniza por contenido (no toca archivos iguales,
así Ninja no recompila de más), compila en `Staging`, copia las partidas y cambia carpetas por
renombrado. Windows permite renombrar el `.exe` en uso, así que no hace falta un proceso auxiliar.

**Tecnología:** C++20 del runtime (parche nuevo sobre Wiicompiled), PowerShell 5.1, bash, Python 3
(solo en la PC que publica), `curl` del sistema.

**Spec:** `docs/superpowers/specs/2026-11-11-launcher-updates-design.md`

## Restricciones globales

- Nada del juego se publica ni se descarga: las piezas salen del payload del Setup y los kits, que ya
  pasan `tools/check_no_game_data.py`.
- Solo cuenta el último release; no hay cadenas de paquetes.
- La versión instalada (`<Install>\Game`) no se toca hasta que la nueva compiló entera.
- `UserData` y `mods` nunca se mueven ni se sobrescriben; la copia de partidas cubre `UserData\NAND`,
  `UserData\ModSaves`, `UserData\arrange` y `UserData\Config.toml`. El caché no.
- Ningún proceso se crea con `CREATE_SUSPENDED` + `ResumeThread` (Defender); se usa
  `PROC_THREAD_ATTRIBUTE_JOB_LIST` como en `launcher.cpp` (PowerUp+) y `tools/setup/setup.cpp`.
- Textos ES/EN con `Tr(es, en)` según `[ui] language`. Docs en `docs/` y `docs/en/` a la vez.
- Ventana Sí/No una vez por versión; tras «No», marca fija junto a la versión. Clave
  `[update] notified = "X.Y.Z"` en `Config.toml`.
- Sin Discussion en el manifiesto se enlaza `https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/vX.Y.Z`
  (el respaldo «primer enlace de las notas» del spec no hace falta: `Make-Release.ps1` exige la URL).
- Switch fuera de alcance.

## Revisión: lo que más puede fallar

1. **Sin internet, proxy o GitHub caído:** la comprobación no avisa, no cuelga el launcher (hilo aparte,
   `curl --max-time 10`) y JUGAR funciona. Prueba en Tarea 7 con una URL `file:///` inexistente.
2. **Ruta de instalación con espacios y «ñ»** (`C:\Juegos\Pelota Ñ`): comillas en `CreateProcessW`, en
   los argumentos de PowerShell y en bash. Prueba en Tarea 8 (fin a fin) instalando ahí.
3. **El jugador cierra el launcher a mitad:** el job object mata el script; `Game` intacto; el siguiente
   intento vuelve a verificar lo descargado (SHA-256) y recompila. Prueba en Tarea 8.
4. **Fallo de compilación:** `Game` intacto, mensaje con la línea `@@ERROR`, botón para reintentar.
   Prueba en Tarea 5 (script) y Tarea 8 (error de compilación provocado).
5. **Instalación sin `Build`** (v0.1.7, copias de desarrollo, `Build` borrado a mano): solo aviso +
   enlace, sin botón de actualizar, con el texto «reinstala con el Setup una vez». Prueba en Tarea 3.

## Mapa de archivos

| Archivo | Qué hace |
|---|---|
| `VERSION` (nuevo, raíz) | `0.1.8`; única fuente de la versión |
| `tools/update_manifest.py` (nuevo) | huellas de piezas y manifiesto (PC que publica) |
| `tools/test_update_manifest.py` (nuevo) | pruebas del anterior |
| `tools/setup/Make-Payload.ps1`, `Make-Release.ps1`, `CMakeLists.txt`, `setup.cpp` | piezas, manifiesto, versión, instalación en `<Install>\Build` |
| `tools/setup/LocalBuild.ps1` | `-Stage`, `-Update`, versión a CMake |
| `tools/setup/Update.ps1` (nuevo) | aplicar piezas, compilar, copiar partidas, cambiar, volver atrás |
| `tools/test_update_ps1.py` (nuevo) | pruebas de `Update.ps1` con carpetas falsas |
| `tools/linux/build.sh`, `make-kits.sh`, `update.sh` (nuevo) | lo mismo en Linux/Deck |
| `tools/patches/wiicompiled-launcher-updates.patch` (nuevo, al final de la lista de `tools/wiicompiled-translate.ps1`) | runtime: `mlbpp_version.cpp.in`, `include/launcher_update.h`, `src/launcher_update.cpp`, cambios en `src/launcher.cpp`, prueba `tests/launcher_update_tests.cpp` |
| `docs/instalar*.md` / `docs/en/*`, `docs/traspaso.md`, `AGENTS.md` (tabla de comprobaciones) | uso y proceso de publicación |

## Formato del manifiesto (lo usan todas las tareas)

`update-manifest.txt`, UTF-8, LF, una entrada por línea, campos separados por un espacio, `#` comenta:

```
version 0.1.8
discussion_es https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions/11
discussion_en https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions/11
piece <nombre> <archivo.zip> <sha256 zip> <bytes zip> <huella contenido> <raíz1>;<raíz2>;...
kit <linux|steamdeck> <archivo.tar.gz> <sha256> <bytes> [<sha256 de tools/linux/Containerfile>]
```

- Huella de contenido = SHA-256 del texto `"<ruta relativa con />\t<sha256 del archivo>\n"` de todos
  los archivos de la pieza, ordenado por ruta. No depende de fechas ni del zip.
- Raíces relativas a la carpeta del payload (`toolkit/...`, `workspace/...`).
- Piezas fijas (nombre: raíces):
  - `core`: `workspace/LocalBuild.ps1;workspace/Update.ps1;workspace/NativeBuildFlags.ps1;workspace/config;workspace/launcher;workspace/src/runtime;workspace/src/aurora-main;workspace/TextureStudio.exe;workspace/SeasonStudio.exe;workspace/CallnameStudio.exe;workspace/UniformStudio.exe;toolkit/nodtool.exe;MLBPowerPros2008-Setup.exe`
  - `translator`: `toolkit/Translator`
  - `deps`: `workspace/Dependencies`
  - `buildtools`: `toolkit/CMake;toolkit/Ninja;toolkit/README-LICENSES.txt`
  - `llvm`: `toolkit/llvm-mingw`
  - `powerupplus`: `workspace/powerupplus`
  - `uniform`: `workspace/uniform-studio`
  - `licenses`: `licenses`
- Nombre del zip: `MLBPP-<versión>-<pieza>.zip`. En la instalación, el manifiesto instalado es
  `<Install>\Build\update-manifest.txt` (Windows) y `<kit>/local/update-manifest.txt` (Linux).

---

### Task 1: Versión única y visible

**Archivos:**
- Crear: `VERSION` (`0.1.8` + salto de línea)
- Modificar: `tools/setup/CMakeLists.txt:6` (leer `VERSION` con `file(STRINGS ...)` en vez de `0.1.7` a mano)
- Modificar: `tools/setup/LocalBuild.ps1` (pasar `-DMLBPP_VERSION=<VERSION del payload>` en `-AdditionalArguments`)
- Modificar: `tools/setup/Make-Payload.ps1` (copiar `VERSION` a `workspace\VERSION`)
- Modificar: `tools/linux/build.sh` (pasar `-DMLBPP_VERSION=$(cat "$root/VERSION")`)
- Parche runtime: `runtime/CMakeLists.txt` (`configure_file(src/mlbpp_version.cpp.in ${CMAKE_BINARY_DIR}/mlbpp_version.cpp)` añadido a las fuentes de `WiiCompiled`; por defecto `dev`), `runtime/include/launcher_update.h` (`const char* MlbppVersion();`), `src/launcher.cpp` (texto «v0.1.8» en la esquina del launcher)

**Interfaces:**
- Produce: `const char* MlbppVersion()` → `"0.1.8"` o `"dev"`.

- [ ] **Paso 1:** comprobar que `VERSION` no puede ir como `target_compile_definitions` sobre `WiiCompiled`:
  cambiaría la línea de compilación de los ~48.000 archivos traducidos y cada versión nueva recompilaría
  todo. Por eso va en un solo `.cpp` generado con `configure_file`.
- [ ] **Paso 2:** implementar lo de «Archivos» y crear el parche
  `tools/patches/wiicompiled-launcher-updates.patch`, añadido al final de la lista de
  `tools/wiicompiled-translate.ps1:57` (`build.sh` la lee de ahí).
- [ ] **Paso 3:** `tools\wiicompiled-build.ps1` de la copia local; cambiar solo `VERSION` y recompilar.
  Esperado: Ninja recompila `mlbpp_version.cpp` y enlaza (≤ 3 objetos), el launcher dice «v0.1.8».
  Captura con PrintWindow (memoria «Launcher visual testing»).
- [ ] **Paso 4:** compilar el Setup; propiedades del `.exe`: versión 0.1.8.
- [ ] **Paso 5:** commit (`feat: single VERSION file shown in the launcher`).

### Task 2: Manifiesto y piezas al publicar

**Archivos:**
- Crear: `tools/update_manifest.py`
- Crear: `tools/test_update_manifest.py`
- Modificar: `tools/setup/Make-Payload.ps1` (escribir `update-manifest.txt` sin líneas `piece` de zip: solo `version` y la huella de contenido, para que el Setup la instale)
- Modificar: `tools/setup/Make-Release.ps1` (parámetros `-Discussion <url>` obligatorio con `-Zip`, `-DiscussionEn <url>` opcional; zips por pieza y manifiesto final en `local\setup-payload\release\`; añade las líneas `kit` si encuentra los `.tar.gz` de `local\release`)

**Interfaces:**
- Produce (CLI): `python -I tools/update_manifest.py hash <payload dir>` → imprime `<pieza> <huella>` por pieza;
  `python -I tools/update_manifest.py release <payload dir> <out dir> <versión> <discussion_es> [<discussion_en>] [--kits <dir>]` → zips + `update-manifest.txt`.
- Produce (Python): `content_hash(root: Path, roots: list[str]) -> str`, `PIECES: dict[str, list[str]]`.

- [ ] **Paso 1: pruebas que fallan** en `tools/test_update_manifest.py` (`unittest`, carpetas temporales):

```python
def test_hash_ignores_mtime_and_order(self):    # tocar la fecha de un archivo no cambia la huella
def test_hash_changes_with_content(self):       # cambiar 1 byte de workspace/src/runtime/a.cpp cambia solo 'core'
def test_every_payload_file_in_one_piece(self): # payload falso con un archivo por raíz + uno suelto -> error que nombra el suelto
def test_release_manifest_lines(self):          # 'version 0.1.8', 'discussion_es ...', 8 líneas 'piece' con 6 campos
```

- [ ] **Paso 2:** `python -I tools/test_update_manifest.py` → FAIL (no existe el módulo).
- [ ] **Paso 3:** implementar. Un archivo del payload fuera de toda raíz es error: así una carpeta nueva
  del payload no se queda sin publicar en silencio.
- [ ] **Paso 4:** pruebas → PASS. `Make-Release.ps1 -Zip -Discussion https://example.invalid/d/0` sobre el
  payload local: la suma de los zips ≈ tamaño del Setup zip; `core` < 20 MB.
- [ ] **Paso 5:** commit (`feat: update manifest and payload pieces for releases`).

### Task 3: Lógica del launcher (pura, con pruebas)

**Archivos:**
- Parche runtime: `runtime/include/launcher_update.h`, `runtime/src/launcher_update.cpp`, `runtime/tests/launcher_update_tests.cpp`, `runtime/CMakeLists.txt` (`add_executable(mkw_launcher_update_tests ...)` + `add_test`, como `mkw_season_schedule_mlb_tests`)

**Interfaces (namespace `launcher_update`):**

```cpp
struct Piece { std::string name, file, sha256; uint64_t bytes = 0; std::string content; };
struct Kit { std::string platform, file, sha256; uint64_t bytes = 0; std::string containerfile; };  // vacío si no viene
struct Manifest { std::string version, discussionEs, discussionEn; std::vector<Piece> pieces; std::vector<Kit> kits; };
std::optional<Manifest> ParseManifest(std::string_view text);           // nullopt si falta 'version'
int CompareVersions(std::string_view a, std::string_view b);            // -1/0/1; acepta "v" delante; "dev" < todo
std::vector<Piece> PiecesToDownload(const Manifest& latest, const Manifest& installed);  // por 'content'
bool ShouldPopup(std::string_view latest, std::string_view installed, std::string_view notified);
std::string DiscussionUrl(const Manifest& m, bool english);             // en -> es -> página del release
enum class Mode { None, Notify, Update };                               // Notify = sin Build/kit: solo enlace
Mode Decide(const Manifest& latest, std::string_view installed, bool canUpdate);
```

- [ ] **Paso 1: pruebas que fallan** en `launcher_update_tests.cpp`:
  - `ParseManifest` del ejemplo del formato: versión `0.1.8`, 8 piezas, `bytes` correcto; línea desconocida ignorada; texto vacío → `nullopt`.
  - `CompareVersions("0.1.10","0.1.9") == 1`, `("v0.1.8","0.1.8") == 0`, `("dev","0.0.1") == -1`.
  - `PiecesToDownload`: solo `core` difiere → 1 pieza; pieza nueva que no está instalada → incluida.
  - `ShouldPopup("0.1.9","0.1.8","")` true; `("0.1.9","0.1.8","0.1.9")` false; `("0.1.10","0.1.8","0.1.9")` true; `("0.1.8","0.1.8","")` false.
  - `DiscussionUrl` sin `discussion_en` en inglés → la de español; sin ninguna → `.../releases/tag/v0.1.8`.
  - `Decide` con `canUpdate=false` y versión mayor → `Notify`; igual o menor → `None`.
- [ ] **Paso 2:** compilar y correr `mkw_launcher_update_tests` → FAIL.
- [ ] **Paso 3:** implementar en `launcher_update.cpp` (sin dependencias nuevas; `std::from_chars`).
- [ ] **Paso 4:** → PASS. Regenerar el parche.
- [ ] **Paso 5:** commit (`feat: launcher update manifest logic`).

### Task 4: El Setup deja la instalación lista para actualizar

**Archivos:**
- Modificar: `tools/setup/setup.cpp` (antes de `LocalBuild.ps1`, copiar `toolkit`, `workspace`, `licenses`, `update-manifest.txt` y el `.exe` del Setup a `<Install>\Build` sin `workspace\build` ni `workspace\src\generated`; correr `<Install>\Build\workspace\LocalBuild.ps1 -KeepWorkspace`)
- Modificar: `tools/setup/LocalBuild.ps1`:
  - `-Stage <dir>`: instala los archivos del programa en `<dir>` en vez de `<Install>\Game` y no toca `UserData`, `mods`, `Config.toml`, `portable.txt` ni accesos directos.
  - `-Update`: no pide `-Iso`; si `Test-Inputs` falla, error «Falta la copia extraída del disco; usa el Setup».
  - Cuenta la lista de archivos del programa (exe, dll, `launcher`, `powerupplus`, estudios, `uniform-studio`, `config`, `wii_bootstrap`) en una función `Get-ProgramItems` que `Update.ps1` reutiliza.

**Interfaces:**
- Produce: `<Install>\Build\{toolkit,workspace,update-manifest.txt}`; `LocalBuild.ps1 -Update -Stage <dir> -Install <Install>`.
- Produce: `Get-ProgramItems` → nombres de primer nivel en `Game` que son del programa.

- [ ] **Paso 1:** Setup en una carpeta nueva (`D:\pruebas\Pelota Ñ`). Esperado: `Build` existe con `build` y
  `generated` llenos, `Game` juega. Anotar en el informe el tamaño de `Build` (estimado ~3,2 GB: toolkit
  548 MB + workspace 431 MB + generated ~590 MB + objetos ~1,6 GB).
- [ ] **Paso 2:** `REINSTALAR` sobre la misma carpeta: conserva `UserData` y `mods` (como hoy) y reutiliza `Build`.
- [ ] **Paso 3:** `LocalBuild.ps1 -Update -Stage <Install>\Staging -Install <Install>` sin cambios: Ninja
  «no work to do» o solo enlace, en < 1 min; `Staging` contiene el programa y nada de `UserData`.
- [ ] **Paso 4:** cambiar un `.cpp` del runtime en `Build\workspace\src\runtime` y repetir: medir el tiempo
  (objetivo del spec: 1–2 min en Ryzen 5 5500). Medir también tocar una cabecera que incluye el código
  traducido; anotar ambos tiempos.
- [ ] **Paso 5:** commit (`feat: setup keeps a rebuildable workspace in the install`).

### Task 5: `Update.ps1` (aplicar, compilar, copiar partidas, cambiar, volver)

**Archivos:**
- Crear: `tools/setup/Update.ps1` (va en la pieza `core` como `workspace\Update.ps1`)
- Crear: `tools/test_update_ps1.py`

**Interfaces:**
- `Update.ps1 -Install <dir> -Phase apply -Downloads <dir>`: verifica el SHA-256 de cada zip contra el
  manifiesto nuevo (`<Downloads>\update-manifest.txt`), extrae a temporal y sincroniza cada raíz de la pieza
  **por contenido** (escribe solo lo que difiere, borra lo que ya no está), y al final copia el manifiesto
  nuevo a `<Install>\Build\update-manifest.txt`. Las piezas no descargadas no se tocan.
- `-Phase build`: `LocalBuild.ps1 -Update -Stage <Install>\Staging` (borra `Staging` antes).
- `-Phase swap`: copia partidas a `<Install>\Backup` (borrándolo antes; escribe `Backup\version.txt` con la
  versión vieja); vacía `<Install>\Previous`; mueve los elementos de `Get-ProgramItems` de `Game` a
  `Previous` y los de `Staging` a `Game`; escribe `Previous\version.txt`.
- `-Phase rollback [-RestoreSaves]`: intercambia `Game` ↔ `Previous` (solo programa); con `-RestoreSaves`,
  vuelve a copiar `Backup` sobre `UserData`.
- Progreso `@@ <porcentaje> <texto>` y `@@ERROR <texto>` como `LocalBuild.ps1`; `-Lang es|en`.

- [ ] **Paso 1: pruebas que fallan** en `tools/test_update_ps1.py` (corre `powershell -NoProfile -ExecutionPolicy Bypass -File`; instalación falsa en temporal; `-Phase build` no se prueba aquí):
  - `test_apply_keeps_mtime_of_unchanged`: pieza `core` con un archivo cambiado y otro igual → solo cambia la fecha del cambiado; un archivo borrado en la pieza desaparece.
  - `test_apply_rejects_bad_sha`: zip alterado → `@@ERROR`, nada escrito en `Build`, manifiesto instalado sin cambios.
  - `test_swap_moves_program_only`: `UserData\NAND\x` y `mods\y` siguen en `Game`; `Previous` tiene el exe viejo; `Backup` tiene `NAND`, `ModSaves`, `arrange`, `Config.toml` y no `Cache`.
  - `test_rollback_restore_saves`: tras swap y cambiar `UserData\NAND\x`, `rollback -RestoreSaves` lo deja como estaba; sin la opción, lo deja cambiado.
  - `test_paths_with_spaces_and_enye`: todo lo anterior bajo `Pelota Ñ`.
- [ ] **Paso 2:** `python -I tools/test_update_ps1.py` → FAIL.
- [ ] **Paso 3:** implementar. Cambio por `Move-Item` dentro del mismo volumen (renombrar; el `.exe` en uso
  se puede renombrar en Windows). Comentario `ponytail:` con el límite: un apagón justo durante el cambio
  deja `Game` a medias y se arregla con `REINSTALAR` del Setup.
- [ ] **Paso 4:** → PASS.
- [ ] **Paso 5:** commit (`feat: Update.ps1 applies pieces, swaps and rolls back`).

### Task 6: Linux y Steam Deck

**Archivos:**
- Modificar: `tools/linux/build.sh` (`--stage DIR`: instala solo el programa, sin `Data`, `UserData`, `Config.toml`, entrada de escritorio; `--from-kit` (solo lo pasan `install.sh` / `install-steam-deck.sh` de `make-kits.sh`): al instalar, escribe `<install>/kit.txt` con `root=<kit>`, `platform=linux|steamdeck` y `options=<opciones que no sean --disc/--install/--stage/--from-kit>`; un clon de git sin `--from-kit` no escribe `kit.txt` y queda en modo solo aviso, como dice el spec; copia `VERSION` al `local/update-manifest.txt` mínimo `version X`)
- Crear: `tools/linux/update.sh <install dir> <kit tarball> <sha256>`: verifica el SHA-256, extrae a temporal, sincroniza por contenido sobre `root` excepto `local/` (`python3 -I`, presente en SteamOS), corre `build.sh $options --stage <install>.staging`, copia partidas a `<install>.backup`, cambia programa a `<install>.previous`; `update.sh --rollback [--restore-saves] <install dir>`. Mismo formato `@@`.
- Modificar: `tools/linux/make-kits.sh` (los dos instaladores pasan `--from-kit`; `VERSION` va en el kit por `git archive`; las líneas `kit` las añade `Make-Release.ps1`)
- En Linux se compara solo la versión: si la última es mayor se baja el kit entero (~8 MB) de su
  plataforma; `PiecesToDownload` es solo de Windows.

- [ ] **Paso 1:** prueba en WSL (memoria «OpenGL backend»: WSL solo para función): kit 0.1.8 instalado,
  kit 0.1.9 con un cambio en el runtime; `update.sh` → compila incremental (< 1 min, como dice
  `docs/en/linux.md`), `UserData` intacto, `--rollback --restore-saves` vuelve.
- [ ] **Paso 2:** fallo provocado (error de sintaxis en el runtime del kit 0.1.9) → `@@ERROR`, la
  instalación sigue con 0.1.8.
- [ ] **Paso 3:** con `--container` (podman en WSL si está; si no, anotarlo como pendiente para la Deck).
  Si el kit nuevo cambia `tools/linux/Containerfile`, `update.sh` lo detecta antes de compilar (comparar el
  archivo viejo con el nuevo) y emite `@@ <n> Reconstruyendo el contenedor (tarda más)...`; el launcher lo
  enseña antes de empezar (manifiesto: el kit dice si cambió → campo opcional `containerfile <sha256>`
  en la línea `kit steamdeck`, comparado con el del kit instalado).
- [ ] **Paso 4:** commit (`feat: Linux and Steam Deck kits update in place`).

### Task 7: Launcher: aviso, descarga, progreso, cambio y volver

**Archivos:**
- Parche runtime: `src/launcher.cpp`, `src/launcher_update.cpp`

**Interfaces:**
- Consume: Tarea 3 (`ParseManifest`, `Decide`, `ShouldPopup`, `PiecesToDownload`, `DiscussionUrl`), Tarea 5/6 (scripts y fases).
- URL base del manifiesto: `https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/latest/download/`;
  la variable de entorno `MLBPP_UPDATE_URL` la sustituye (pruebas con `file:///`). Las piezas se bajan de
  la misma base + nombre de archivo.
- `canUpdate`: Windows → existe `<carpeta del exe>\..\Build\update-manifest.txt`; Linux → existe `kit.txt`
  junto al ejecutable.

- [ ] **Paso 1:** al abrir el launcher, un hilo baja el manifiesto (`curl -sSfL --max-time 10`, proceso
  oculto, sin `CREATE_SUSPENDED`) a `UserData\Cache\update-manifest.txt`. Cualquier fallo → sin aviso y
  sin log molesto (una línea `RT_LOG`). Con `MLBPP_UPDATE_URL=file:///D:/no/existe/` el launcher abre
  normal y JUGAR funciona.
- [ ] **Paso 2:** `Mode::Update`/`Notify` + `ShouldPopup` → ventana con estilo del launcher (no ImGui suelto;
  memoria «Native UI, not tape»): «Nueva versión vX.Y.Z» / «New version vX.Y.Z», enlace «Novedades»
  (`ShellExecuteW` / `xdg-open` a `DiscussionUrl`), tamaño de la descarga (suma de `bytes` de
  `PiecesToDownload`) y si será rápida (`core` sola) o completa; botones «Sí» / «No». En `Notify` el
  botón es «Descargar el Setup» (página del release) y el texto pide reinstalar una vez.
  «No» → escribe `[update] notified` y deja la marca «Nueva versión vX.Y.Z» junto a la versión; pulsarla
  vuelve a abrir la ventana. En HERRAMIENTAS: «BUSCAR ACTUALIZACIONES» repite el paso 1 y abre la ventana
  aunque `notified` sea igual.
- [ ] **Paso 3:** «Sí»: baja cada pieza con `curl -sSfL -o` a `<Install>\Update\downloads` y enseña MB
  bajados / totales leyendo el tamaño del archivo cada frame; luego corre `apply`, `build`, `swap` del
  script en un job object y pinta la barra con las líneas `@@`. JUGAR desactivado mientras dura;
  «Cancelar» cierra el job. Error → texto del `@@ERROR` + «Reintentar».
- [ ] **Paso 4:** tras `swap`, `Relaunch()` sin `WIICOMPILED_SKIP_LAUNCHER` (abre el launcher nuevo) y
  sale. Comprobar que el nuevo dice la versión nueva.
- [ ] **Paso 5:** si existe `Previous\version.txt` (o `<install>.previous`), HERRAMIENTAS muestra
  «VOLVER A vX.Y.Z»: pregunta «¿Recuperar también las partidas de antes de actualizar? Lo jugado en
  vX.Y.Z (nueva) se pierde.» Sí/No → `rollback [-RestoreSaves]` → `Relaunch()`.
- [ ] **Paso 6:** regenerar el parche; commit (`feat: launcher update notice, download and rollback`).

### Task 8: Prueba de punta a punta, docs y publicación

**Archivos:**
- Modificar: `docs/` y `docs/en/` (guía de instalación: actualizar, volver atrás, espacio en disco);
  `docs/traspaso.md`; `AGENTS.md` (tabla de comprobaciones: `test_update_manifest.py`,
  `test_update_ps1.py`, `mkw_launcher_update_tests`); notas de publicación: `Make-Release.ps1 -Zip
  -Discussion <url>` y subir todos los archivos de `local\setup-payload\release\` al release.

- [ ] **Paso 1:** «release» falso local: payload 0.1.8 instalado con el Setup en `D:\pruebas\Pelota Ñ`;
  payload 0.1.9 con un cambio visible en el launcher; `MLBPP_UPDATE_URL=file:///D:/pruebas/release-0.1.9/`.
  Esperado: ventana Sí/No una vez, descarga solo `core`, compilación incremental (anotar tiempo), abre en
  0.1.9, partidas intactas.
- [ ] **Paso 2:** «No» → reabrir el launcher: sin ventana, con marca; «BUSCAR ACTUALIZACIONES» la abre.
- [ ] **Paso 3:** cerrar el launcher durante la compilación → reabrir: sigue 0.1.8 jugable; «Sí» otra vez
  termina bien.
- [ ] **Paso 4:** 0.1.9 con un error de compilación → mensaje de error, 0.1.8 jugable.
- [ ] **Paso 5:** «VOLVER A v0.1.8» con y sin recuperar partidas.
- [ ] **Paso 6:** medir con `git diff --stat` entre `v0.1.5`, `v0.1.6` y `v0.1.7` qué releases habrían sido
  rápidos (sin cambios en `local/vendor` del traductor, `config/wiicompiled` ni cabeceras incluidas por el
  código traducido) y anotarlo en el informe.
- [ ] **Paso 7:** `tools/check_no_game_data.py --all` sobre todo lo que se publica; pasar las pruebas de la
  tabla de `AGENTS.md`.
- [ ] **Paso 8:** commit (`docs: launcher updates`).
