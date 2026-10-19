# Modo Temporada de 20 temporadas (fase 2) — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** opción del launcher «TEMPORADAS: 10 / 20». Con 20, el modo Temporada sigue de 2017 a 2027 con los datos de cada año en su sitio; con 10 (por defecto) el juego se comporta como el original.

**Architecture:** el tope sale de una función del juego (`0x80C06534`, devuelve 10) y de unas comparaciones fijas con 9 o 10 metidas en código traducido. Lo primero se reemplaza con una función nativa. Para lo segundo se le enseña al traductor de Wiicompiled a sustituir el inmediato de una comparación concreta por una llamada al runtime (`season_cap::LastIndex()`), así no hay que reescribir a mano funciones de miles de líneas y la opción se decide al arrancar. Las finanzas, la única tabla con sitio para 10 años, guardan los años 11 a 20 en la zona final de la partida que siempre está a cero.

**Tech Stack:** C# del traductor (parche a Wiicompiled), C++20 del runtime, Python 3.12 (`C:/Users/david/.pyenv/pyenv-win/versions/3.12.0/python.exe`) para la sonda y las herramientas.

**Spec:** `docs/superpowers/specs/2026-10-19-season-archive-design.md` (sección «Cambio de alcance tras la investigación») y `docs/superpowers/specs/2026-10-19-season-archive-findings.md`. Detalle técnico: `docs/ingenieria-inversa.md`, «Año de la temporada».

## Global Constraints

- Nada de datos del juego en el repo (`tools/check_no_game_data.py`). Partidas y volcados en `local/tmp/season/`.
- El código generado no se edita a mano. Los cambios van como parches reproducibles: `tools/patches/wiicompiled-translator-immediate-overrides.patch` (traductor) y `tools/patches/wiicompiled-season-cap.patch` (runtime y launcher), registrados en `tools/wiicompiled-translate.ps1` después de `wiicompiled-season-probe`.
- Lo que cambie el juego va detrás de una opción del launcher (palabra del usuario en la discussion #1). Por defecto 10: sin la opción, el juego queda igual que hoy, byte a byte en la partida.
- Pruebas en el juego desde estados guardados y con la sonda (`live.py`, mod `SeasonProbe`); no se juegan partidos. Avisar al usuario antes de abrir el juego.
- Textos del launcher en español e inglés (`[ui] language`), como el resto.
- Cada dato nuevo del juego va a `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md` («Año de la temporada»), con PDF regenerado.

## Review Focus

1. **Partida de 20 temporadas abierta con la opción en 10:** el índice puede valer 10 a 19 y el juego original escribiría fuera de las finanzas. Se espera que el runtime fuerce el tope a 20 para esa partida (lo decide el índice cargado) y lo diga en el registro. Task 4.
2. **Partida terminada con el juego original (índice 10, repite el final):** con la opción en 20 tiene que poder seguir a 2018 (decisión 7 del diseño). Task 5.
3. **Final de la temporada 20:** índice 19 → 20; el final del modo tiene que salir igual que hoy en la 10, sin escribir fuera de ninguna tabla. Task 5.
4. **Zona final de la partida:** si el juego la usa en algún momento (otra pantalla, otro modo), las finanzas de los años 11 a 20 la pisarían. Task 3 lo comprueba antes de usarla.
5. **Comparación reemplazada que no es del año:** sustituir un inmediato en la instrucción equivocada rompe otra cosa en silencio. El traductor exige que la instrucción en esa dirección sea `cmpwi`/`cmplwi` con el inmediato esperado y falla la traducción si no. Task 1.

---

### Task 1: Inmediatos de comparación reemplazables en el traductor

**Files:**
- Modify: `local/wiicompiled-mlb/src/translator/src/Translator.Core/Lifting/PpcLifter.cs` (rama `cmpwi`/`cmplwi`), `Ir/IrNodes.cs` (valor nuevo), `CodeGen/CxxLinearCodeGenerator*.cs` (emitirlo), `Translator.Cli/TranslationProjectConfig.cs` (leerlo)
- Modify: `config/wiicompiled/rl8e54.yml` (`translation.immediate_overrides`)
- Test: el proyecto de tests del traductor si existe; si no, una prueba de traducción de una función conocida (comparar el `.cpp` generado)
- Create: `tools/patches/wiicompiled-translator-immediate-overrides.patch`

**Interfaces:**
- Produces: en el YAML,
  ```yaml
  translation:
    immediate_overrides:
      - address: 0x80BF51BC   # dirección de la instrucción de comparación
        expect: 9             # inmediato original; si no coincide, error de traducción
        expression: season_cap::LastIndex()
        header: season_cap.h
  ```
  El `.cpp` generado de la función lleva `#include "season_cap.h"` y `SetCR...(..., static_cast<int32_t>(season_cap::LastIndex()))` en lugar del inmediato.

- [ ] **Step 1: Test:** traducir `0x80BB4A40` con un override en su `cmpwi r3, 9` (dirección: el bloque tras la lectura del índice; sacarla del ensamblador del traductor o de `--emit-trace`). Esperado del `.cpp`: la expresión en lugar de `9`; con `expect: 8`, la traducción falla con un mensaje que nombra la dirección.
- [ ] **Step 2:** ejecutarlo. Esperado: falla (la clave del YAML no existe).
- [ ] **Step 3:** implementar: un `IrValue` nuevo que lleva una expresión C++ y se emite tal cual; el lifter lo usa como operando derecho cuando la dirección está en la lista; la lista se pasa del config al lifter.
- [ ] **Step 4:** test pasa; una traducción completa sin overrides da el mismo `generated/` que antes (`git diff --stat` del árbol generado vacío, o comparar los hashes de `translation-metadata.json`).
- [ ] **Step 5:** extraer el parche (índice temporal, como `wiicompiled-season-probe`), registrarlo, commit (`Translator: replaceable compare immediates`).

### Task 2: Mapa de comparaciones del tope

**Files:**
- Create: `tools/season_cap_sites.py` (lista de direcciones a partir del código generado), `tools/test_season_cap_sites.py`
- Modify: `config/wiicompiled/rl8e54.yml` (las entradas de `immediate_overrides`)
- Modify: `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md`

**Interfaces:**
- Consumes: Task 1 (formato del YAML).
- Produces: `sites(functions_dir) -> list[(addr, imm, func)]`: cada comparación de una lectura del índice (`0x80B74BCC`, también en línea) con 9 o 10. La dirección es la del `cmpwi` (bloque `loc_X` + 4 × instrucciones anteriores del bloque).

- [ ] **Step 1: Test** con un `.cpp` sintético con el patrón de `func_80BF4BD4.cpp` (lectura en línea en `loc_80BF51B8`, `SetCRResident(..., 9)` a continuación): `sites` devuelve `0x80BF51BC, 9`.
- [ ] **Step 2:** falla. **Step 3:** implementar. **Step 4:** pasa.
- [ ] **Step 5:** ejecutarlo sobre `local/wiicompiled-mlb/generated/functions`. Esperado: al menos las conocidas (`0x80BB4A40`, `0x80BF24B4`, dos en `0x80BF4BD4`, `0x80C0CB54`, `0x80BECF40`, `0x80C0D4F8`). Para cada una, leer qué controla (mensaje 140/141, eventos de pretemporada…) y decidir: tope (se reemplaza por `LastIndex()` o `Count()`) o tamaño de tabla de finanzas (no se reemplaza: Task 3). Documentarlo en «Año de la temporada» y escribir las entradas del YAML.
- [ ] **Step 6:** commit (`Season cap: compare sites`).

### Task 3: La zona final de la partida para las finanzas

**Interfaces:**
- Consumes: sonda (`poke`, `dump`), `tools/season_save.py`.
- Produces: `season_cap::FinanceYearAddress(int year) -> uint32_t`: para el año 0..9 la dirección del juego (Q + 44 + año × 44), para 10..19 una dirección en la zona final (desplazamiento del archivo ≥ 0x11A000, a 4 KB del final usado), y lo mismo para el valor anual (Q + 4 + año × 4).

- [ ] **Step 1:** con la sonda, escribir una marca en la zona final del bloque (archivo 0x11F000 = bloque + 0x11F000) en una partida, guardar, salir, cargar y pasar dos cambios de año: la marca sigue ahí y la suma de control (cabecera, 0x48) la acepta. Esperado: la partida carga sin aviso de datos dañados.
- [ ] **Step 2:** comprobar que ninguna función escribe en esa zona: volcado antes y después de guardar, cargar, cambiar de año, abrir MLB Data y las finanzas; la zona no cambia salvo la marca.
- [ ] **Step 3:** si algo la toca o la suma de control falla, parar y anotarlo como pregunta para el usuario (alternativa: un archivo propio junto a la partida).
- [ ] **Step 4:** documentar (ES/EN, PDF) y commit (`RE docs: free area at the end of the Season save`).

### Task 4: Opción del launcher y tope en el runtime

**Files:**
- Create: `local/wiicompiled-mlb/src/runtime/include/season_cap.h`, `src/hle/mlb/season_cap.cpp`, `tests/season_cap_tests.cpp`
- Modify: el launcher (`src/launcher.cpp`, fila nueva «TEMPORADAS / SEASONS» en JUEGO o MODOS, valores 10 y 20), `runtime_config` (`[season] max_seasons = 10`)
- Create: `tools/patches/wiicompiled-season-cap.patch`

**Interfaces:**
- Consumes: Task 2 (sitios en el YAML llaman a `season_cap::LastIndex()`/`Count()`), Task 3 (`FinanceYearAddress`).
- Produces:
  - `int season_cap::Count()`: 10 o 20 según la opción; 20 también si la partida cargada tiene índice ≥ 10 (Review Focus 1).
  - `int season_cap::LastIndex()`: `Count() - 1`.
  - Reemplazo nativo de `0x80C06534` que devuelve `Count()`.
  - Reemplazos nativos de las funciones de finanzas por año que indexan la tabla de 10 (`0x80C0D614`, `0x80C0D658`, `0x80C0D6D0`, `0x80C0D758`, y las que Task 2 encuentre en `0x80BB486C`/`0x80BB7518`/`0x80C0D4F8`), usando `FinanceYearAddress`.

- [ ] **Step 1: Tests** (`season_cap_tests.cpp`, sin juego): `Count()` con opción 10 e índice 3 → 10; opción 10 e índice 12 → 20; opción 20 → 20; `FinanceYearAddress(9)` = Q + 44 + 9 × 44; `FinanceYearAddress(10)` en la zona final; `FinanceYearAddress(20)` rechazado.
- [ ] **Step 2:** fallan. **Step 3:** implementar. **Step 4:** pasan.
- [ ] **Step 5:** traducir y compilar; con la opción en 10, la partida de prueba hace el cambio de año 2008 → 2009 igual que antes (mismo diff de memoria que en la investigación, `tools/season_save.py`).
- [ ] **Step 6:** parche, registro, commit (`Runtime: Season mode up to 20 seasons (launcher option)`).

### Task 5: Prueba de punta a punta

- [ ] **Step 1:** opción en 20. Índice 8 → pasar a 9 (2017): no sale el final ni el mensaje de última temporada. Índice 9 → 10 (2018): pretemporada normal (eventos de diciembre y enero, finanzas de 2017 en su sitio), menú del 2018.
- [ ] **Step 2:** finanzas: con el año 2019 en curso, la pantalla de finanzas muestra 2017 y 2018 con sus valores (los de la zona final); la tabla mensual no cambia.
- [ ] **Step 3:** índice 18 → 19 → 20: sale el final del modo igual que en el original; ningún volcado muestra escrituras fuera de las tablas (comparar con la investigación).
- [ ] **Step 4:** partida terminada del original (`local/tmp/season/s_finished.dat`, índice 10): con la opción en 20, al cargarla sigue en 2018 en lugar de repetir el final (si no, implementar la salida del estado final y repetir).
- [ ] **Step 5:** opción en 10 con una partida de 2019 (índice 11): carga y sigue con tope 20 (Review Focus 1), y el registro lo dice.
- [ ] **Step 6:** documentar (ES/EN, PDF, `docs/mods.md` o la guía donde estén las opciones del launcher), commit (`Season mode: 20 seasons, tested`).
