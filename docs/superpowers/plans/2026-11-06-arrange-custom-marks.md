# Marcas y logotipos propios en Arrange: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Objetivo:** desde el creador de Arrange, cada equipo puede elegir una marca y un logotipo PNG
de carpetas del usuario. Se ven en todo el juego, sin la inicial, en PC y en Switch.

**Arquitectura:** un módulo nativo `arrange_marks` del runtime, entregado como el parche
`tools/patches/wiicompiled-arrange-marks.patch`. Tiene:
- un catálogo de PNG;
- un ajuste de imagen;
- una extensión de guardado por hueco;
- ganchos en el código del juego, localizados en la tarea 1, que añaden las entradas a las
  listas del juego y sustituyen marca, logotipo y bandera al dibujar el equipo.

**Tecnología:** C++20 del runtime de wiicompiled, Aurora (`aurora::gfx::png::load_png_file`,
dibujo), parches contra el árbol fijado, sondas con `WIICOMPILED_PAD_LIVE` y NAND aislada.

**Spec:** `docs/superpowers/specs/2026-11-06-arrange-custom-marks-design.md`

## Restricciones globales

- **Rama:** `arrange-custom-marks`, creada desde `switch-arm64`.
- **Commits:** solo cuando el usuario lo pida. Este plan marca dónde irían.
- **Carpetas:** `UserData/arrange/marks/` y `UserData/arrange/logos/`, bajo
  `RuntimeConfigFile::ApplicationDataDirectory()`. En Switch:
  `sdmc:/switch/mlbpowerpros2008/UserData/arrange/…`.
- **Activación:**
  - `[arrange] custom_marks = true|false` en `Config.toml`, falso por defecto;
  - opción en MEJORAS con `experimental = true` y textos ES/EN.
  - Apagada: el juego es idéntico al original y no se lee ni escribe la extensión.
- **Interfaz dentro del juego:** solo texturas, fuente, botones y sonidos del juego. Nada de
  ventanas ImGui con aspecto propio.
- **No cambian:** los equipos MLB ni los equipos de Arrange sin elección propia (marca del juego
  ⇒ inicial como siempre).
- **Uniforme:** no se toca. Ni `PWM_arrange_uniform` ni `0x806CC2EC`.
- **PNG ausente, renombrado o inválido:** el equipo vuelve a la marca o logotipo del juego que
  tenía.
- **Repositorio:** sin datos del juego ni del usuario (`tools/check_no_game_data.py`). Las
  pruebas usan PNG generados en el test.
- **Runtime:** solo por parches (`tools/patches/`). El código generado se conserva.
- **Builds:** uno pesado cada vez, `MLB_SWITCH_JOBS=2..3` en Switch. Compilar con subagentes
  en marcha deja al PC sin memoria.
- **Switch:** un build y una sola prueba final en la consola del usuario.

## Puntos a vigilar

1. **PNG enorme** (8000×8000, varios MB) o carpeta con cientos de archivos: escanear y cargar no
   debe congelar el menú ni agotar la memoria (sobre todo en Switch). Límite de tamaño y de
   entradas, y carga perezosa solo de lo elegido.
2. **Nombre de archivo con acentos, ñ, espacios o muy largo:** el juego lo muestra solo con
   caracteres de su fuente, recortado. Dos archivos cuyo nombre visible coincide se distinguen
   (sufijo).
3. **Archivo no PNG o PNG corrupto** en la carpeta: se ignora, sin error en pantalla.
4. **Local y visitante con marca propia**, el mismo PNG o dos distintos, en el mismo partido:
   cada uno muestra la suya.
5. **Opción apagada con partidas que tienen elecciones:** el juego se ve original y, al guardar
   con la opción apagada, no se pierde la extensión.

---

### Tarea 1: Localizar los puntos del juego (investigación con pruebas en el juego)

**Archivos:**
- Crear `local/analysis/arrange/hooks.md` (notas y evidencias; local).
- Modificar `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md`, en la subsección
  «Habilidades por ID y modo Arrange», y regenerar el PDF con `python tools/build_re_pdf.py`.

**Interfaces:**
- Consume `local/analysis/arrange/report.md`, sus scripts y capturas, la NAND aislada
  `local/analysis/arrange/nand` y el mod de prueba `ZZLeonesTest`.
- Produce, en `hooks.md`, dirección y evidencia de cada punto:
  - **H1 Guardado:** archivo de NAND donde se guardan los 32 equipos, desplazamiento y tamaño
    de cada registro, y cómo se distingue de otras partidas (ruta o tamaño).
  - **H2 Lista Team Mark:** función que la construye y la dibuja, número de entradas (38),
    columnas y desplazamiento, cómo pinta los nombres (fuente y tabla de texto) y qué escribe
    en `+0x17`/`+0x1B` al elegir.
  - **H3 Logo Text:** función de la pantalla y dónde se confirma el texto (`+0x30`…).
  - **H4 Marca:** dónde se carga y enlaza la textura de `env_arrange_logoNN` (y su icono de
    64×64) para un equipo, y qué puntero o índice del equipo está disponible ahí.
  - **H5 Logotipo:** dónde se compone o dibuja el logotipo de letras de un equipo Arrange en
    cada uno de los siete sitios del spec.
  - **H6 Bandera:** dónde se dibuja la bandera con la inicial (atlas `tex1_256x64_bae093b1…` y
    `tex1_256x128_36658f00…`), con el equipo disponible.
  - **H7 COPY y DELETE:** funciones de copiar y borrar un hueco.
  - **Decisión:** ampliar las listas del juego (preferible) o dibujar las entradas nuevas encima
    con atlas y fuente del juego, con el porqué.

- [ ] **Paso 1:** crear la rama: `git switch -c arrange-custom-marks switch-arm64`.
- [ ] **Paso 2:** H1 con volcados MEM1 y diferencias de archivos de NAND al guardar un equipo
  («Save to Main Data? Yes»). Comprobado si al cambiar un byte del registro en el archivo, el
  juego lo muestra tras cargar.
- [ ] **Paso 3:** H2–H7, partiendo de los cargadores de packs y de los lectores de
  `+0x17`/`+0x1B`/`+0x30`. Cada punto se comprueba con un cambio en caliente (por ejemplo,
  `WIICOMPILED_DEBUG_WATCH` o un `REGISTER_NATIVE_FUNCTION_AS` de prueba local que
  registre las llamadas) y una captura.
- [ ] **Paso 4:** escribir `hooks.md` y la sección de la doc; regenerar el PDF.
- [ ] **Paso 5:** si algún H no se encuentra, parar y avisar al usuario antes de seguir. Cambia
  qué partes se pueden hacer nativas.

### Tarea 2: Catálogo de PNG

**Archivos:**
- Crear `runtime/include/arrange_marks.h` (cabecera pura, sin dependencias del juego, como
  `modern_postseason.h`).
- Test: `runtime/tests/arrange_marks_test.cpp`.
- Modificar `runtime/CMakeLists.txt`: añadir `arrange_marks` al `foreach(case …)` de los
  tests `-UNDEBUG`.

**Interfaces:**
- Produce, en `namespace arrange_marks`:
  - `constexpr size_t kMaxEntries = 64; constexpr size_t kMaxFileBytes = 8u << 20; constexpr size_t kNameChars = 12;`
  - `struct Entry { std::string file; std::string label; };`, donde `file` es el nombre real y
    `label` el texto que pinta el juego.
  - `std::vector<Entry> scan(const std::filesystem::path& folder);`
  - `std::string label(std::string_view stem);`

- [ ] **Paso 1: tests que fallan.**

```cpp
assert(label("Leones") == "LEONES");
assert(label("Águila Ñ") == "AGUILA N");        // sin acentos, mayúsculas
assert(label("los leones del caracas") == "LOS LEONES D"); // 12 caracteres
// scan sobre un directorio temporal con: b.png, A.PNG, x.txt, big.png (> kMaxFileBytes),
// sub/ (carpeta), dup.png y DUP!.png
auto e = scan(tmp);
assert(e.size() == 4);                           // A, B, DUP, DUP 2: sin .txt, big, carpetas
assert(e[0].label == "A" && e[1].label == "B");
assert(e[2].label == "DUP" && e[3].label == "DUP 2");
assert(scan(tmp / "no-existe").empty());
```

- [ ] **Paso 2:** compilar y ejecutar `ctest -R arrange_marks` en `local/wiicompiled-mlb/build`;
  debe fallar.
- [ ] **Paso 3:** implementar `label` y `scan`.
  - `label` quita diacríticos de UTF-8 latino, pasa a mayúsculas y conserva `A–Z 0–9`, espacio
    y `-`. Si la tarea 1 muestra que la fuente tiene otros caracteres, ajustar aquí.
  - `scan` ordena sin distinguir mayúsculas, marca duplicados con ` 2`, ` 3`… y corta en
    `kMaxEntries`.
- [ ] **Paso 4:** `ctest -R arrange_marks` pasa. Control negativo: invertir un assert y ver que
  falla.

### Tarea 3: Ajuste de imagen

**Archivos:** `runtime/include/arrange_marks.h` y el mismo test.

**Interfaces:**
- Produce:
  - `struct Rgba { uint32_t width = 0, height = 0; std::vector<uint8_t> pixels; };`
  - `Rgba fit(const Rgba& source, uint32_t width, uint32_t height);`

- [ ] **Paso 1: tests que fallan.**

```cpp
Rgba wide{4, 2, std::vector<uint8_t>(4 * 2 * 4, 255)};    // blanco opaco
auto f = fit(wide, 8, 8);
assert(f.width == 8 && f.height == 8);
assert(alpha(f, 0, 0) == 0 && alpha(f, 4, 4) == 255);      // relleno arriba, imagen centrada
assert(alpha(f, 4, 1) == 0 && alpha(f, 4, 6) == 0);        // 8x4 centrado: filas 2-5
assert(fit(Rgba{}, 8, 8).pixels == std::vector<uint8_t>(8 * 8 * 4, 0)); // vacía => transparente
```

- [ ] **Paso 2:** `ctest -R arrange_marks` falla.
- [ ] **Paso 3:** implementar `fit`: escala uniforme con filtro de caja o bilineal, centrado y
  relleno con alfa 0. Sin dependencias nuevas.
- [ ] **Paso 4:** pasa.

### Tarea 4: Extensión de guardado por hueco

**Archivos:**
- Crear `runtime/include/arrange_marks_save.h`.
- Modificar `runtime/src/hle/storage/nand_isfs.cpp`, en el archivo y desplazamiento de H1,
  siguiendo el patrón de cola de `season_cap` y `modern_postseason_save.h`.
- Test: `runtime/tests/arrange_marks_save_test.cpp`, añadido al mismo `foreach`.

**Interfaces:**
- Consume H1 de la tarea 1.
- Produce:
  - `struct Choice { std::string mark, logo; };` (nombres de archivo; vacío significa que no
    hay elección);
  - `using Choices = std::array<Choice, 32>;`
  - `constexpr size_t kSaveBytes = 2064;`. Formato:
    - magic `"MLBARRMK"`, 8 bytes;
    - versión `1`, u32 big-endian;
    - suma FNV-1a de 32 bits;
    - 32 × (marca de 32 bytes + logotipo de 32 bytes, en UTF-8 rellenado con ceros).
  - `std::array<uint8_t, kSaveBytes> writeChoices(const Choices&);`
  - `enum class ReadStatus { Missing, Valid, Invalid };`
  - `ReadStatus readChoices(const uint8_t* data, size_t size, Choices& out);`
  - `void copySlot(Choices&, int from, int to); void clearSlot(Choices&, int slot);`

- [ ] **Paso 1: tests que fallan.**
  - Ida y vuelta con los Leones en el hueco 2.
  - Todo ceros ⇒ `Missing`.
  - Magic correcto con la suma alterada ⇒ `Invalid`.
  - Nombre de 31 bytes conservado.
  - Nombre de 40 bytes rechazado al escribir (lanza `std::invalid_argument`).
  - `copySlot(c, 2, 5)` copia y `clearSlot(c, 2)` vacía.
- [ ] **Paso 2:** falla.
- [ ] **Paso 3:** implementar la cabecera.
  - En `nand_isfs.cpp`, al leer la partida de H1 con la opción encendida se carga la cola; si es
    `Invalid`, se registra y se ignora, sin romper la carga. Al escribir se añade la cola.
  - Con la opción apagada no se lee nada, y al escribir se conserva la cola que hubiera
    (punto a vigilar 5).
- [ ] **Paso 4:** pasan los tests y siguen pasando `mkw_modern_postseason_save_test` y el
  resto de `ctest`.

### Tarea 5: Opción en Config y en el launcher

**Archivos:**
- Modificar `runtime/include/runtime_config.h`:
  - campo `std::optional<bool> arrangeCustomMarks;  // [arrange] custom_marks`;
  - `ArrangeCustomMarks(bool fallback = false)` y `SetArrangeCustomMarks(bool)`.
- Modificar `runtime/src/launcher.cpp`: opción en la pestaña MEJORAS.
- Test: `runtime/tests/runtime_config_tests.cpp`.

**Interfaces:**
- Produce `RuntimeConfigFile::ArrangeCustomMarks(false)`, que consultan las tareas 4, 6, 7 y 8.

- [ ] **Paso 1: test que falla.** `[arrange]\ncustom_marks = true|false|"x"|1`. Solo `true` y
  `false` producen valor, y el documento vacío da `false`, como el test de
  `modern_postseason`.
- [ ] **Paso 2:** falla.
- [ ] **Paso 3:** implementar el campo, el getter y el setter. Opción del launcher, con
  `experimental = true`:
  - título `Tr("MARCAS Y LOGOTIPOS PROPIOS (ARRANGE)", "CUSTOM MARKS AND LOGOS (ARRANGE)")`;
  - texto ES: «Añade a las listas de Team Mark y Logo Text de Arrange las imágenes PNG de
    UserData/arrange/marks y UserData/arrange/logos. Un equipo con marca propia la muestra en
    lugar de su inicial.»;
  - texto EN: «Adds the PNG images in UserData/arrange/marks and UserData/arrange/logos to
    Arrange's Team Mark and Logo Text lists. A team with its own mark shows it instead of its
    initial.»;
  - añadirla a `{Tr("MEJORAS", "ENHANCEMENTS"), {…}}` y al manejador de cambios.
  - Crear las dos carpetas al activar la opción.
- [ ] **Paso 4:** pasa. Captura del launcher con la opción en ES y EN, siguiendo la memoria
  «Launcher visual testing» (PrintWindow).

### Tarea 6: Marcas propias en la lista Team Mark

**Archivos:**
- Crear `runtime/src/hle/mlb/arrange_marks.cpp`, con las sustituciones registradas con
  `REGISTER_NATIVE_FUNCTION_AS` como en `modern_postseason.cpp`.
- Modificar `runtime/CMakeLists.txt` para compilarlo con el runtime.

**Interfaces:**
- Consume H2 y H4, `scan`, `fit`, `Choices` y `ArrangeCustomMarks`.
- Produce, en `arrange_marks.cpp`:
  - `const Choice& choiceFor(uint32_t teamRecordAddress);` (hueco a partir del registro, según
    H1);
  - `bool markTexture(const Choice&, TextureRef& out);` (textura de la marca, ya ajustada a
    256×256 y a 64×64; `TextureRef` es el tipo de textura de Aurora o el atlas que se use para
    dibujar);
  - `void onSlotCopied(int from, int to); void onSlotDeleted(int slot);`, enganchadas en H7.
  - Las tareas 7 y 8 las usan.

- [ ] **Paso 1:** lista: las entradas de `scan(marks)` aparecen tras GRIM REAPER con la
  etiqueta `label` y la fuente del juego, con el mecanismo decidido en la tarea 1.
  - Al elegir una se guarda `Choice.mark` en el hueco y se deja el registro del juego con una
    marca válida del juego (la que tenía), para que el juego original siga leyéndolo.
  - Con la marca propia elegida, el selector A–D se oculta o queda sin efecto.
- [ ] **Paso 2:** vista previa: el recuadro TEAM MARK muestra el PNG ajustado. La textura se
  carga solo al elegir o al dibujar ese equipo, nunca la carpeta entera (punto a vigilar 1).
- [ ] **Paso 3:** sonda con NAND aislada y PNG generados (escudo de prueba, no los del
  usuario). Capturas de la lista, de la selección y del cancelar.
  - Con la opción apagada, la lista es la original de 38 (captura comparada con `arr6`).
- [ ] **Paso 4:** un archivo no PNG y un PNG corrupto en la carpeta no aparecen y no hacen
  fallar nada (punto a vigilar 3). Captura.

### Tarea 7: Logotipos propios en Logo Text

**Archivos:** `runtime/src/hle/mlb/arrange_marks.cpp`.

**Interfaces:**
- Consume H3 y H5, `scan(logos)`, `fit` y `choiceFor`.
- Produce `bool logoTexture(const Choice&, TextureRef& out);`, ajustada a las proporciones del
  recuadro TEAM LOGO según H5.

- [ ] **Paso 1:** en «Please set your logo», junto a las letras, una entrada para elegir de
  `scan(logos)` con controles del juego. Al confirmar se guarda `Choice.logo` y el texto del
  juego queda como estaba.
- [ ] **Paso 2:** el recuadro TEAM LOGO muestra el PNG ajustado.
- [ ] **Paso 3:** sonda con un PNG generado y capturas.

### Tarea 8: Marca, logotipo y bandera en todo el juego, y COPY/DELETE

**Archivos:** `runtime/src/hle/mlb/arrange_marks.cpp`.

**Interfaces:**
- Consume H4, H5, H6 y H7, `markTexture`, `logoTexture` y `choiceFor`.

- [ ] **Paso 1:** en H4, H5 y H6, si el equipo tiene `Choice` no vacía y su PNG existe, se
  dibuja la textura propia.
  - La bandera con inicial se sustituye por la marca propia.
  - Si no, se dibuja lo del juego.
- [ ] **Paso 2:** sonda de aceptación con los siete sitios del spec, capturas antes y después.
  - Partido con local y visitante, cada uno con un PNG propio distinto, y otro con el mismo PNG
    (punto a vigilar 4).
  - Los Nationals y un equipo Arrange con marca LION e inicial L sin cambios.
- [ ] **Paso 3:** borrar el PNG de la carpeta y recargar: el equipo vuelve a su marca e
  inicial del juego, sin errores.
- [ ] **Paso 4:** COPY de un hueco con elección y DELETE de otro, guardar y recargar: se
  conservan como se espera.
- [ ] **Paso 5:** Liga, Temporada y Home Run con un equipo de Arrange, si lo admiten. Lo que no
  funcione se apunta en la doc como pendiente.
- [ ] **Paso 6:** coste. En el benchmark de partido (`WIICOMPILED_MATCH_BENCH=1`) en PC, sin
  regresión medible con la opción encendida y un equipo propio.

### Tarea 9: Parche, Switch y documentación

**Archivos:**
- Crear `tools/patches/wiicompiled-arrange-marks.patch` y añadirlo al final de la lista de
  `tools/wiicompiled-translate.ps1`.
- La cadena de Switch, igual que se aplica `wiicompiled-modern-postseason` (buscar con
  `grep -rn modern-postseason tools/switch local/tmp/mkswitch.sh`).
- Modificar `docs/mods.md` y su versión en inglés (carpetas, nombres y límites),
  `docs/traspaso.md` y `docs/roadmap/herramientas-modding.md`.

- [ ] **Paso 1:** generar el parche desde el árbol y comprobarlo con
  `git apply -R --check` sobre `local/wiicompiled-mlb/src`. Aplicar la cadena en limpio y
  ejecutar `ctest` completo.
- [ ] **Paso 2:** build de Switch (`tools/switch/build-nro.sh`, `MLB_SWITCH_JOBS=2..3`) sin
  errores.
- [ ] **Paso 3:** pedir al usuario **una** prueba en la consola: crear un equipo con marca y
  logotipo propios en `sdmc:/switch/mlbpowerpros2008/UserData/arrange/` y jugar un partido de
  Exhibición. Comprobar en el log de nxlink que no hay errores y que la extensión se guardó.
- [ ] **Paso 4:** `python tools/check_no_game_data.py` limpio. Commit cuando el usuario lo
  pida:

```bash
git add tools/patches/wiicompiled-arrange-marks.patch tools/wiicompiled-translate.ps1 docs/
git commit -m "Arrange: custom PNG marks and logos per team (PC and Switch)"
```
