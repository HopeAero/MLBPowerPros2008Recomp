# Editor de uniformes libre en Arrange: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Objetivo:** cada equipo de Arrange puede llevar un uniforme propio —logos y texto colocados libres en camiseta (frente/espalda), mangas, pantalón y gorra/casco, sin la inicial impuesta—, hecho en una herramienta de PC sencilla y elegido en el propio creador de Arrange, en PC y Switch.

**Arquitectura:** tres piezas contra un mismo contrato de archivo (`.ppu`, atlas horneado C8 + paleta RGB5A3 de 256):
- **Horneador + conversor** en Python (`tools/uniform-studio/bake.py`, reusa `tools/packwii.py`): PNG/`.iff`/`.fsh` + capas → `UserData/arrange/uniforms/<nombre>/uniform.ppu`.
- **Editor de PC** (`tools/uniform-studio/`, carcasa C++/SDL3/ImGui de Texture Studio): lienzo de capas, texto, vista previa, guardar.
- **Runtime nativo** (parche `wiicompiled-arrange-uniforms.patch`): carga el `.ppu` a MEM2, lo elige en LINE TYPE, engancha `0x806BDC3C` para sustituir las texturas del uniforme, quita la inicial, guarda la elección en la extensión `MLBARRMK`.

**Tecnología:** C++20 del runtime de wiicompiled; Python 3.12 (numpy+Pillow) para el horneado; C++20 + SDL3 + Dear ImGui para el editor; Aurora para dibujo; parches contra el árbol fijado; sondas con NAND aislada.

**Spec:** `docs/superpowers/specs/2026-11-07-arrange-imported-uniforms-design.md`

## Restricciones globales

- **Rama:** `arrange-imported-uniforms`, creada desde `arrange-custom-marks` (el uniforme extiende el guardado `MLBARRMK` a v3 y reusa `choiceFor` de las marcas; esa rama ya tiene esos commits, no pusheada).
- **Commits:** solo cuando el usuario lo pida. El plan marca dónde irían.
- **Carpeta:** `UserData/arrange/uniforms/<nombre>/`, bajo `RuntimeConfigFile::ApplicationDataDirectory()`. En Switch, `sdmc:/switch/mlbpowerpros2008/UserData/arrange/uniforms/…`.
- **Activación:** `[arrange] custom_uniforms = true|false` en `Config.toml`, falso por defecto; opción en MEJORAS con `experimental = true` y textos ES/EN. Apagada: el juego es idéntico al original y no se lee ni escribe la extensión.
- **Herramienta sencilla, como el juego original** (decisión del usuario): molde claro, capas que se arrastran, pocos botones; reusa los widgets y el estilo de los Studios.
- **Interfaz dentro del juego:** solo texturas, fuente, botones y sonidos del juego. Nada de ventanas propias.
- **No cambian:** los equipos MLB ni los equipos de Arrange sin uniforme propio.
- **Gancho:** `0x806BDC3C` (llamada real ×15, nunca en línea). Nunca enganchar `0x806F0670` ni los setters de pieza (en línea).
- **Resolución:** topada a 128×128 por el modelo; no se amplía la malla en esta versión.
- **Número del dorsal:** lo sigue dibujando el juego en su sitio; no se toca.
- **Repositorio:** sin datos del juego ni del usuario (`tools/check_no_game_data.py`). Los tests usan PNG/atlas generados en el test.
- **Runtime:** solo por parches (`tools/patches/`). El código generado se conserva.
- **Builds:** uno pesado cada vez; `MLB_SWITCH_JOBS=2..3` en Switch. No compilar con subagentes en marcha.
- **Switch:** un build y una sola prueba final en la consola del usuario.

## Puntos a vigilar

1. **Nombre de carpeta con acentos/ñ/espacios o muy largo:** el juego lo muestra solo con su fuente, recortado; dos cuyo nombre visible coincide se distinguen (sufijo).
2. **`.ppu` ausente, renombrado o inválido** (versión desconocida, tamaño mal, magic mal): el equipo vuelve a su uniforme de Arrange de respaldo, sin error en pantalla.
3. **Casa y visitante con uniforme propio** (el mismo `.ppu` o dos distintos) en el mismo partido: cada uno muestra el suyo; búferes propios por lado (en el spike compartían el atlas global).
4. **Opción apagada con partidas que tienen elecciones:** el juego se ve con el respaldo y, al guardar apagada, no se pierde la extensión.
5. **Varias capas con muchos colores:** la cuantización a 256 no debe reventar ni dejar basura; aviso en el editor si se pasa.

---

### Tarea 1: Diseño de ganchos y verificación en el juego

**Archivos:**
- Modificar `local/analysis/arrange/uniform/re.md` y `spike/spike.md` (notas; local).
- Modificar `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md` (subsección Arrange, uniforme) y regenerar el PDF con `python tools/build_re_pdf.py`.

**Interfaces:**
- Consume `re.md`, `spike.md` y la NAND aislada `local/analysis/arrange/uniform/nand`.
- Produce, en `re.md`, confirmado con prueba en el juego:
  - **G1 Sustitución del cuerpo:** cómo dar a un personaje su propia textura+paleta de cuerpo sin afectar a otros equipos (descriptor `+0x04`/`+0x08` con puntero propio, o su objeto de textura propio), evitando el atlas global que compartieron casa/visitante en el spike.
  - **G2 Gorra/casco/pecho:** horneado del logo **dentro del atlas** de la gorra (sin el recuadro oscuro del spike) y supresión de la inicial (`desc+0x0C`/`+0x1C` a transparente).
  - **G3 Mapa (gestor, idx) → (hueco, lado):** puntos `0x805A83F0`, `0x806C10FC` (o sus 4 llamadores), `0x80BC1558`; limpieza en `0x806BD974`/`0x806BD8D0`.
  - **G4 Número:** confirmar que el número del dorsal se mantiene al sustituir el cuerpo.

- [ ] **Paso 1:** crear la rama `git switch -c arrange-imported-uniforms arrange-custom-marks`.
- [ ] **Paso 2:** verificar G1 y G3 con `poke` en la NAND aislada: dos equipos de Arrange en un partido, cada uno con un atlas de cuerpo distinto, sin que uno pise al otro. Captura.
- [ ] **Paso 3:** verificar G2 (escudo horneado en el atlas de la gorra, sin recuadro) y G4 (número intacto). Capturas.
- [ ] **Paso 4:** escribir G1–G4 en `re.md` y la doc; regenerar el PDF.
- [ ] **Paso 5:** si G1 o G3 no salen limpios, parar y avisar antes de seguir.

### Tarea 2: Contrato `.ppu` y horneador (Python)

**Archivos:**
- Crear `tools/uniform-studio/bake.py` (usa `tools/packwii.py`: `new_palette`, `encode`, `tile`, `to_rgb5a3`).
- Test: `tools/uniform-studio/test_bake.py`.

**Interfaces:**
- Produce:
  - `KIND = ("body", "cap", "helmet", "number")`; tamaños `{"body":(128,128), "cap":(64,32), "helmet":(64,32), "number":(64,32)}`.
  - `def bake_atlas(rgba: "PIL.Image", kind: str) -> tuple[bytes, bytes]` → `(palette512, tiled_c8)`: cuantiza a ≤256 colores RGB5A3 (entrada 0 = transparente, `0x0000`), tesela C8 con `packwii.tile`.
  - `def write_ppu(path: str, atlases: dict[str, "PIL.Image"], backup_colors: list[int]) -> None`: escribe el contenedor.
  - `def read_ppu(path: str) -> dict`: `{"version":1, "atlases":{kind:(w,h,palette512,tiled_c8)}, "backup":[10 ints 0..31]}`.
  - Formato `.ppu`: magic `b"PPUNIF\0\0"`; versión `1` u32 BE; FNV-1a u32 del resto; `backup` 10 bytes (índices 0..31 de la paleta de 32); por cada kind presente: 1 byte kind-id, u16 w, u16 h, 512 bytes de paleta, `w*h` bytes C8 teselados.

- [ ] **Paso 1: tests que fallan.**

```python
img = checker_rgba(128, 128)              # ayudante del test
pal, c8 = bake_atlas(img, "body")
assert len(pal) == 512 and len(c8) == 128*128
assert pal[0:2] == b"\x00\x00"            # entrada 0 transparente
write_ppu(tmp/"u.ppu", {"body": img, "cap": checker_rgba(64,32)}, [1]*10)
r = read_ppu(tmp/"u.ppu")
assert r["version"] == 1 and set(r["atlases"]) == {"body","cap"}
assert r["backup"] == [1]*10 and r["atlases"]["body"][0:2] == (128,128)
# magic/checksum/tamaño malos => ValueError
for bad in corrupt_variants(tmp/"u.ppu"): 
    try: read_ppu(bad); assert False
    except ValueError: pass
```

- [ ] **Paso 2:** `python tools/uniform-studio/test_bake.py` falla.
- [ ] **Paso 3:** implementar `bake_atlas`, `write_ppu`, `read_ppu`.
- [ ] **Paso 4:** pasa. Control negativo: invertir un assert y ver que falla.

### Tarea 3: Conversor de entrada (`.iff`, `.fsh`, PNG → capas)

**Archivos:**
- Crear `tools/uniform-studio/import_sources.py` (reusa los prototipos de `local/analysis/uniform2k/`: `fsh.py`, `qfs.py`, decodificador `.iff`).
- Test: `tools/uniform-studio/test_import.py` (con `.fsh`/`.iff` mínimos generados en el test, no ficheros del usuario).

**Interfaces:**
- Produce:
  - `def layers_from_fsh(path) -> list[Layer]` y `def layers_from_iff(path) -> list[Layer]`, donde `Layer = {"name":str, "rgba":PIL.Image, "suggest":str}` (`suggest` ∈ KIND, p. ej. `jerf`→`body/chest`, `hat`→`cap`, `zig0..9`→`number`).
  - `def nearest_palette_colors(rgba) -> list[int]`: colores dominantes → índices 0..31 de la paleta de 32 del juego (tabla en `local/analysis/arrange/uniform/packs/PWM_arrange_color.pack`, copiada al test como datos).

- [ ] **Paso 1: tests que fallan.** `.fsh` SHPI mínimo comprimido QFS con una entrada `chst` → `layers_from_fsh` devuelve 1 capa con `suggest=="body/chest"`; `nearest_palette_colors` de un RGBA negro+dorado → los 2 índices esperados.
- [ ] **Paso 2:** falla.
- [ ] **Paso 3:** implementar, moviendo lo estable de `local/analysis/uniform2k/`.
- [ ] **Paso 4:** pasa.

### Tarea 4: Editor de PC (uniform-studio)

**Archivos:**
- Crear `tools/uniform-studio/{main.cpp, canvas.cpp/.h, layers.cpp/.h, preview.cpp/.h, CMakeLists.txt, build.ps1}`, clonando la carcasa de `tools/texture-studio/` (`ui.h`, `power_pros_ui.h`, `images.cpp`, `config.cpp`, `gx.*` para decodificar la vista previa).
- El horneado se delega a `bake.py` (tarea 2): invocar el intérprete de Python al guardar, o portar `bake_atlas` a C++ — decisión del implementador (riesgo del spec), documentada.

**Interfaces:**
- Consume `bake.write_ppu`/`read_ppu` (tarea 2), `import_sources` (tarea 3), `gx.decode` (vista previa).
- Produce `UniformStudio.exe`: carga PNG/`.iff`/`.fsh` como capas, arrastra/escala/posiciona capas y texto sobre el molde UV (`local/analysis/arrange/uniform/body_regions.png`, incluido como dato del editor), vista previa en el muñeco, guarda `UserData/arrange/uniforms/<nombre>/uniform.ppu`. Conmutador ES/EN como los demás Studios.

- [ ] **Paso 1:** maqueta de la interfaz: paneles del molde (cuerpo/gorra), lista de capas, botones Importar/Guardar; look de los Studios. Captura (PrintWindow, memoria «Launcher visual testing»).
- [ ] **Paso 2:** colocar una capa (logo) y texto sobre el molde con posición/tamaño; vista previa que decodifica el atlas horneado y lo muestra. Captura.
- [ ] **Paso 3:** Importar un `.fsh`/`.iff` de prueba → capas colocables. Guardar `.ppu` y releer con `read_ppu` para comprobar ida y vuelta (test de integración `tools/uniform-studio/test_roundtrip.py`).
- [ ] **Paso 4:** aviso si una capa supera los 256 colores al cuantizar (punto a vigilar 5). Captura.

### Tarea 5: Carga del `.ppu` en el runtime

**Archivos:**
- Crear `runtime/include/arrange_uniforms.h` (cabecera pura: catálogo + lector del `.ppu`, sin dependencias del juego).
- Test: `runtime/tests/arrange_uniforms_test.cpp`, añadido al `foreach(case …)` de `runtime/CMakeLists.txt` (`-UNDEBUG`).

**Interfaces:**
- Produce, en `namespace arrange_uniforms`:
  - `constexpr size_t kMaxEntries = 64;`
  - `struct Entry { std::string folder; std::string label; };`
  - `std::vector<Entry> scan(const std::filesystem::path& root);` (como `arrange_marks::scan`, pero por subcarpeta con `uniform.ppu`).
  - `struct Atlas { uint16_t w, h; std::array<uint8_t,512> palette; std::vector<uint8_t> indices; };`
  - `struct Uniform { std::map<std::string,Atlas> atlases; std::array<uint8_t,10> backup; };`
  - `enum class ReadStatus { Missing, Valid, Invalid };`
  - `ReadStatus readPpu(const uint8_t* data, size_t size, Uniform& out);` (mismo formato que `bake.write_ppu`, verifica magic/versión/FNV/tamaño).

- [ ] **Paso 1: tests que fallan.** Un `.ppu` válido (bytes generados en el test, idénticos al formato de la tarea 2) → `Valid`, atlas `body` 128×128 y `backup` correcto; magic/versión/suma/tamaño malos → `Invalid`; vacío → `Missing`. `scan` sobre un tmp con `A/uniform.ppu`, `B/uniform.ppu`, `C/` (sin ppu), `x.txt` → 2 entradas A, B (punto a vigilar 2).
- [ ] **Paso 2:** `ctest -R arrange_uniforms` falla.
- [ ] **Paso 3:** implementar `scan` y `readPpu`.
- [ ] **Paso 4:** pasa; control negativo.

### Tarea 6: Extensión de guardado (uniforme por hueco y lado)

**Archivos:**
- Modificar `runtime/include/arrange_marks_save.h` (ampliar el bloque `MLBARRMK` a versión 3) y `runtime/src/hle/storage/nand_isfs.cpp`.
- Test: `runtime/tests/arrange_marks_save_test.cpp` (ampliar).

**Interfaces:**
- Consume el formato v2 existente de `MLBARRMK`.
- Produce, en v3: por hueco, además de marca y logotipo, `std::string uniformHome, uniformRoad;` (nombre de carpeta; vacío = sin elección). Los lectores de v1/v2 siguen válidos (migración: v2 leído → uniformes vacíos). La huella por hueco no cambia.
  - Ampliar `struct Choice` con `uniformHome`, `uniformRoad`; `writeChoices`/`readChoices` escriben/leen v3; `copySlot`/`clearSlot` los arrastran.

- [ ] **Paso 1: tests que fallan.** Ida y vuelta v3 con uniforme de casa y visitante distintos en el hueco 2; un buffer v2 se lee con uniformes vacíos (migración); suma alterada → `Invalid`; `copySlot`/`clearSlot` arrastran los dos uniformes; nombre de 40 bytes rechazado.
- [ ] **Paso 2:** falla.
- [ ] **Paso 3:** implementar v3. Con la opción apagada no se lee; al guardar apagada se conserva la cola (punto a vigilar 4).
- [ ] **Paso 4:** pasan los tests y el resto de `ctest`.

### Tarea 7: Opción en Config y en el launcher (+ lanzar el editor)

**Archivos:**
- Modificar `runtime/include/runtime_config.h`: `std::optional<bool> arrangeCustomUniforms; // [arrange] custom_uniforms`, con `ArrangeCustomUniforms(bool=false)` y `SetArrangeCustomUniforms(bool)`.
- Modificar `runtime/src/launcher.cpp`: opción en MEJORAS (`experimental = true`) y botón/acción que abre `UniformStudio.exe` (patrón `OpenStudio`).
- Test: `runtime/tests/runtime_config_tests.cpp`.

**Interfaces:**
- Produce `RuntimeConfigFile::ArrangeCustomUniforms(false)`, que consultan las tareas 5, 6, 8 y 9.

- [ ] **Paso 1: test que falla.** `[arrange]\ncustom_uniforms = true|false|"x"|1`: solo `true`/`false` dan valor; documento vacío → `false`.
- [ ] **Paso 2:** falla.
- [ ] **Paso 3:** implementar campo/getter/setter; opción en MEJORAS con textos ES/EN (título `Tr("UNIFORMES PROPIOS (ARRANGE)", "CUSTOM UNIFORMS (ARRANGE)")`, descripción ES/EN análoga a marcas); crear la carpeta `uniforms/` al activar; botón que abre el editor.
- [ ] **Paso 4:** pasa. Captura del launcher en ES y EN (PrintWindow).

### Tarea 8: Carga a MEM2 y gancho de dibujo

**Archivos:**
- Crear `runtime/src/hle/mlb/arrange_uniforms.cpp` (sustituciones con `REGISTER_NATIVE_FUNCTION_AS`).
- Modificar `runtime/CMakeLists.txt` para compilarlo.

**Interfaces:**
- Consume G1–G4 (tarea 1), `scan`/`readPpu`/`Uniform` (tarea 5), `Choice` v3 (tarea 6), `ArrangeCustomUniforms` (tarea 7).
- Produce:
  - carga perezosa: al elegir o al dibujar un equipo con uniforme, construye en MEM2 (como las marcas) los objetos de textura+paleta de cada atlas del `.ppu`; búferes propios por (hueco, lado) (punto a vigilar 3).
  - `const Choice& choiceFor(uint32_t teamRecordAddress);` (reusa el de marcas).
  - mapa `(gestor, idx) → (hueco, lado)` en los puntos de G3.
  - gancho `0x806BDC3C`: tras el original, si hay uniforme, sobrescribe `+0x04/+0x08` (cuerpo), `+0x0C/+0x14/+0x1C` y sus paletas (gorra/casco/pecho), y pone a transparente la inicial; número intacto.

- [ ] **Paso 1:** con un `.ppu` generado (atlas de prueba, no del usuario) en la NAND aislada, un equipo de Arrange con uniforme elegido muestra la textura propia en el editor Uniform y en Exhibición; otro equipo sin elección, intacto. Capturas.
- [ ] **Paso 2:** casa y visitante con `.ppu` distintos en el mismo partido, cada uno el suyo; mismo `.ppu` en los dos, bien (punto a vigilar 3). Captura.
- [ ] **Paso 3:** `.ppu` inválido o carpeta borrada → respaldo (bandera `0x04` + 10 colores de `backup`), sin errores (punto a vigilar 2). Captura.
- [ ] **Paso 4:** coste: en `WIICOMPILED_MATCH_BENCH=1` (PC), sin regresión medible con la opción encendida y un equipo propio (el gancho actúa al montar, no por cuadro).

### Tarea 9: Elección en la pantalla de uniformes (LINE TYPE)

**Archivos:** `runtime/src/hle/mlb/arrange_uniforms.cpp`.

**Interfaces:**
- Consume `scan` (tarea 5), `choiceFor` y el gancho (tarea 8); puntos de UI de `re.md` §5 (`0x805C8CE8`, `0x805C8F28`, `0x805CACC8`, `0x805CB0BC`, `0x805CAF4C`, `0x805CB064`, `0x805CC0F8`, `0x805CB1F4`, `0x805C9EF4`).

- [ ] **Paso 1:** entradas virtuales ≥3 «IMPORTED: NOMBRE» en la ventana LINE TYPE (índices como las marcas ≥38), con la fuente del juego. Elegir una guarda `uniformHome`/`uniformRoad` según el lado y deja el registro con respaldo válido; elegir <3 quita la elección.
- [ ] **Paso 2:** sonda con NAND aislada: elegir un uniforme propio, cancelar (restaura), confirmar (guarda). Capturas. Con la opción apagada, la ventana es la original de 3 entradas.
- [ ] **Paso 3:** COPY/DELETE/«Reset to Base Team» (`0x805BC394`)/«Save as Arrange» arrastran o borran la elección de uniforme (ampliar el mecanismo de marcas). Guardar y recargar: coherente.

### Tarea 10: Parche, Switch, documentación y prueba en consola

**Archivos:**
- Crear `tools/patches/wiicompiled-arrange-uniforms.patch` y añadirlo al final de `tools/wiicompiled-translate.ps1`.
- Cadena de Switch (como `wiicompiled-arrange-marks`: `grep -rn arrange-marks tools/switch local/tmp/mkswitch.sh`).
- Modificar `docs/mods.md` y `docs/en/mods.md` (carpeta, editor, formato, límites), `docs/roadmap/herramientas-modding.md` y su espejo EN, y `docs/traspaso.md`.

- [ ] **Paso 1:** generar el parche desde el árbol, `git apply -R --check` sobre `local/wiicompiled-mlb/src`, aplicar la cadena en limpio y `ctest` completo.
- [ ] **Paso 2:** build de Switch (`tools/switch/build-nro.sh`, `MLB_SWITCH_JOBS=2..3`) sin errores.
- [ ] **Paso 3:** pedir al usuario **una** prueba en consola: crear un uniforme en el editor, ponerlo a un equipo de Arrange en `sdmc:/switch/mlbpowerpros2008/UserData/arrange/uniforms/` y jugar un partido de Exhibición; comprobar en nxlink que no hay errores y que la extensión se guardó.
- [ ] **Paso 4:** `python tools/check_no_game_data.py` limpio. Commit cuando el usuario lo pida.
