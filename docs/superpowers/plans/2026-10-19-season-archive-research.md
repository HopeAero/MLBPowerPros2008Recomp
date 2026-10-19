# Modo Temporada sin tope, con «Archive» (fase 1: investigación) — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** averiguar, con herramientas y pruebas en el juego, lo que la spec pide saber antes de construir: todos los datos por año de la partida, el estado final de la temporada 10, el menú del modo Temporada y dónde se escribe el año. Sale de aquí el plan de la fase 2 (construir).

**Architecture:** una sonda del runtime (`hle/mlb/season_probe.cpp`, por variables de entorno) que registra el índice del año y deja forzarlo; una herramienta Python (`tools/season_save.py`) que compara partidas `pm3usma.dat` y lista qué cambia; recorridos del juego con guion de mando. Los hallazgos van a `docs/ingenieria-inversa.md` y a una nota de hallazgos que decide la fase 2.

**Tech Stack:** C++20 del runtime (parche Wiicompiled), Python 3.12 (`C:/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe`), sin dependencias nuevas.

**Spec:** `docs/superpowers/specs/2026-10-19-season-archive-design.md` (inglés: `docs/en/superpowers/specs/2026-10-19-season-archive-design.md`).

**Por qué solo investigación:** la spec dice que los hallazgos pueden cambiar el alcance (secciones de Archive que no se puedan rellenar, bytes libres para el año real). Planear ahora el código de la ventana deslizante y de la pantalla sería adivinar. Este plan termina con una nota de hallazgos y el plan de la fase 2 escrito desde ella.

## Global Constraints

- Nada de datos del juego en el repo (`tools/check_no_game_data.py`): ni partidas, ni volcados, ni capturas. Todo eso va a `local/tmp/season/`.
- Los cambios al runtime van como parche nuevo `tools/patches/wiicompiled-season-probe.patch`, registrado en `tools/wiicompiled-translate.ps1` antes de `wiicompiled-stadium-viewer`. La sonda no hace nada si sus variables de entorno no están puestas.
- Pruebas en el juego: avisar antes; el usuario no toca teclado ni mando. Partidas de prueba en un mod propio (`local/wiicompiled-mlb/build/mods/SeasonProbe`, sin activar fuera de las pruebas) para no tocar las del usuario.
- Cada dato del juego que se averigüe se documenta en `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md` («Año de la temporada»), y se regeneran los PDF (`tools/build_re_pdf.py <md> <pdf>`, rutas de `local/docs/`).
- Lo ya sabido (spec): año = 2008 + byte con signo en `[[0x8104B5A8] + 8]`; tope en `0x80BB4A40`, `0x80BF24B4`, `0x80BF4BD4` (dos), `0x80C0CB54`; tabla de 428 bytes por año en `[0x8104B5C4]`; todo en un bloque de `0x80A556E0` (estado en +`0x47DEC`, tabla en +`0xEEBAC`); `pm3usma.dat` mide `0xD4000` bytes.

## Review Focus

1. **Partidas de mods distintos:** cada mod guarda en `UserData/ModSaves/<mod>/`; la sonda y las herramientas deben usar la carpeta del mod activo, no una fija. Task 1 (la ruta sale del log del runtime) y Task 3.
2. **Partida recién empezada frente a partida avanzada:** una diferencia entre dos partidas mezcla datos por año con datos que cambian cada día (fecha, resultados). Task 3 compara pares que solo difieren en una cosa (un día; un cambio de temporada) para separarlos.
3. **Índice forzado fuera de rango** (`WIICOMPILED_SEASON_INDEX=12`): la sonda no lo aplica más allá de 9; en la investigación no se escribe nunca fuera de la tabla. Task 1, prueba `ParseSeasonIndex`.
4. **Partida terminada (después de la temporada 10):** su estado final puede vivir en otro byte, no en el índice. Task 4 la produce y la compara con una de la temporada 10 sin terminar.
5. **Años mostrados por texto y por imagen:** el año puede salir como texto (`0x80B4A888`) o como dígitos dibujados. Task 5 lista los dos.

---

### Task 1: Sonda del año de temporada en el runtime

**Files:**
- Create: `local/wiicompiled-mlb/src/runtime/src/hle/mlb/season_probe.cpp`, `local/wiicompiled-mlb/src/runtime/include/season_probe.h`, `local/wiicompiled-mlb/src/runtime/tests/season_probe_tests.cpp`
- Modify: `local/wiicompiled-mlb/src/runtime/src/settings_overlay.cpp` (llamar a `season_probe::OnHostFrame()` junto a `stadium_viewer::OnHostFrame()`), `local/wiicompiled-mlb/src/runtime/CMakeLists.txt` (test), `tools/wiicompiled-translate.ps1`
- Create: `tools/patches/wiicompiled-season-probe.patch`

**Interfaces:**
- Produces:
  - `std::optional<int> season_probe::ParseSeasonIndex(const char* text)`: entero 0..9, o vacío (nulo, vacío, no numérico, fuera de rango).
  - Variables: `WIICOMPILED_SEASON_LOG=1` registra `[season] index=N year=Y state=0xADDR` cada vez que cambia el índice; `WIICOMPILED_SEASON_INDEX=N` escribe N en el byte del índice una sola vez, la primera vez que el puntero `[0x8104B5A8]` es válido y apunta dentro de MEM1/MEM2, y lo registra.

- [ ] **Step 1: Test** `tests/season_probe_tests.cpp`: `ParseSeasonIndex("0") == 0`, `("9") == 9`, `("10")`, `("-1")`, `("x")`, `("")` y `nullptr` vacíos.
- [ ] **Step 2:** compilarlo con el clang de `local/tools/llvm-mingw-20260616-ucrt-x86_64` (como `stadium_collision_tests`). Esperado: no compila (falta `season_probe.h`).
- [ ] **Step 3:** escribir `season_probe.h/.cpp`; `OnHostFrame()` lee `[0x8104B5A8]` con `Memory::Read32` y el byte `+8` con `Memory::Read8` solo si el puntero es válido.
- [ ] **Step 4:** test pasa. Compilar el runtime (`tools/wiicompiled-build.ps1`) y arrancar el juego sin variables (el log no tiene líneas `[season]`) y con `WIICOMPILED_SEASON_LOG=1` entrando en Season → From Beginning → Quick Start (guion de mando de `scratchpad/season_path.txt`). Esperado: `[season] index=0 year=2008`.
- [ ] **Step 5:** regenerar el parche (diff del árbol de Wiicompiled frente al índice, como `wiicompiled-stadium-viewer`), registrarlo, commit (`Runtime: season probe (log and force the season index)`).

### Task 2: Comparador de partidas

**Files:**
- Create: `tools/season_save.py`, `tools/test_season_save.py`

**Interfaces:**
- Produces:
  - `diff(a: bytes, b: bytes, gap: int = 16) -> list[tuple[int, int]]`: rangos `[inicio, fin)` donde difieren, juntando los separados por menos de `gap` bytes.
  - `strided(ranges, stride: int) -> list[tuple[int, int, int]]`: grupos de rangos que se repiten cada `stride` bytes (`(inicio, fin, repeticiones)`), para ver tablas por año o por equipo.
  - CLI: `python tools/season_save.py diff A.dat B.dat` imprime los rangos con su tamaño; `python tools/season_save.py byte A.dat OFFSET` imprime el byte.

- [ ] **Step 1: Tests** con bytes sintéticos (sin datos del juego): dos búferes que difieren en `[10,12)` y `[20,21)` dan `[(10,12),(20,21)]` con `gap=4` y `[(10,21)]` con `gap=16`; tres cambios cada 428 bytes salen como un grupo de 3 repeticiones con `stride=428`.
- [ ] **Step 2:** ejecutar `python tools/test_season_save.py`. Esperado: falla al importar.
- [ ] **Step 3:** implementar.
- [ ] **Step 4:** pasa. Commit (`Tools: season save diff`).

### Task 3: Qué datos guarda la partida por año

**Interfaces:**
- Consumes: Task 1 (`WIICOMPILED_SEASON_LOG`, `WIICOMPILED_SEASON_INDEX`), Task 2 (`diff`, `strided`).

- [ ] **Step 1:** con el mod `SeasonProbe`, partida nueva (Quick Start, Red Sox), guardar (System → Save) y copiar `pm3usma.dat` a `local/tmp/season/day0.dat`; avanzar un día (Daily Progress) y guardar → `day1.dat`.
- [ ] **Step 2:** confirmar si `pm3usma.dat` es el bloque de `0x80A556E0`: buscar en `day0.dat` el byte del índice del año (estado en +`0x47DEC` → byte +8 relativo al estado, o el desplazamiento que corresponda en el archivo).
- [ ] **Step 3:** con `WIICOMPILED_SEASON_INDEX` en 0, 3 y 8, repetir day0/day1: los rangos que se mueven 428 bytes (u otro paso) por cada año son datos por año. Listar cada tabla por año: desplazamiento, paso, número de años y qué parece contener.
- [ ] **Step 4:** buscar cómo llegar al final de una temporada sin jugar 162 partidos (Daily Progress hasta el final, simular hasta una fecha…); hacer un cambio de temporada real desde el índice 3 y comparar antes/después: qué se copia, qué se borra, qué se suma a carreras o premios.
- [ ] **Step 5:** ¿All-Time/Career y Honors acumulan todo o solo los 10 años? Responderlo con el cambio de temporada de Step 4 y las pantallas de MLB Data (capturas en `local/tmp/season/`).
- [ ] **Step 6:** buscar bytes que estén a cero en todas las partidas probadas y no los toque ninguna función conocida, candidatos a guardar el número de años archivados (decisión de la spec). Si no hay, anotarlo.
- [ ] **Step 7:** documentar en `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md`, PDF, commit (`RE docs: per-year save data`).

### Task 4: El final de la temporada 10

- [ ] **Step 1:** con `WIICOMPILED_SEASON_INDEX=9`, llegar al final de esa temporada (como en Task 3 Step 4) y guardar antes y después del mensaje final → `s10_before.dat`, `s10_after.dat`.
- [ ] **Step 2:** comparar: qué marca el estado «terminado» (el índice, otro byte, ambos).
- [ ] **Step 3:** confirmar en el juego qué hace cada comparación conocida (`0x80BB4A40` mensaje 141, `0x80BF24B4`, `0x80BF4BD4` mensaje 140, `0x80C0CB54`), con registro temporal del runtime si hace falta. Anotar cuáles hay que cambiar para seguir.
- [ ] **Step 4:** documentar ES/EN, PDF, commit (`RE docs: end of the tenth season`).

### Task 5: El menú del modo Temporada y el año en pantalla

- [ ] **Step 1:** localizar el código del menú del modo Temporada (7 opciones: Daily Progress … Exit): cuántas opciones, cómo se mueve el cursor, de dónde salen las imágenes de las pastillas (pack y textura) y qué hace cada una al elegirse. Punto de partida: los mensajes de ayuda de cada opción («Advance the schedule.», «View player and team records…») en los datos del juego y quién los usa.
- [ ] **Step 2:** listar los sitios que muestran el año: los 53 llamadores de `0x80B4A888` (año como número) y cualquier otro que lea el índice con `0x80B74BCC` para dibujar dígitos o elegir texturas; marcar los que forman fechas (día de la semana).
- [ ] **Step 3:** de dónde sale el calendario de partidos y los días de la semana (decisión 8 de la spec: mismo calendario, días del año real).
- [ ] **Step 4:** documentar ES/EN, PDF, commit (`RE docs: Season mode menu and year display`).

### Task 6: Nota de hallazgos y plan de la fase 2

**Files:**
- Create: `docs/superpowers/specs/2026-10-19-season-archive-findings.md` (y espejo en `docs/en/superpowers/specs/`)

- [ ] **Step 1:** escribir la nota: tablas por año (Task 3), estado final (Task 4), menú y año en pantalla (Task 5), sitio para el número de años archivados, qué secciones de Archive se pueden rellenar con lo que guarda el juego y cuáles no.
- [ ] **Step 2:** si algo contradice la spec (no hay bytes libres; una sección de Archive no tiene datos), anotarlo como pregunta para el usuario y parar ahí.
- [ ] **Step 3:** si no, escribir `docs/superpowers/plans/<fecha>-season-archive-build.md` (fase 2: ventana deslizante, archivo, salida del estado final, año real, opción Archive, pantalla Archive), commit (`Plan: Season archive, phase 2`).
