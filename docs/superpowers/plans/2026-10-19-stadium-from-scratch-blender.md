# Estadios desde cero en Blender (fase 3) — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** modelar en Blender un estadio sin estadio base, con un papel por objeto, y llevarlo al juego (exportar y «Ver en el juego») con el escritor desde cero.

**Architecture:** un módulo nuevo del complemento, `scratch.py`, construye un `stadium_model.Stadium` desde una colección «desde cero». Agrupa los objetos por papel, los convierte con las reglas de `convert.py`, codifica las texturas como `exporter.py`, añade las piezas `BCL` del estadio de datos y la colisión. Lo escribe con `stadium_write.write_stadium`. La interfaz (`blender_ui.py`) gana una colección nueva, el papel por objeto y la exportación desde cero; `preview.py` la usa también.

**Tech Stack:** Python de Blender 4.2 (complemento `powerpros_stadium`), sin dependencias nuevas (numpy ya está en Blender).

**Spec:** `docs/superpowers/specs/2026-10-19-stadium-from-scratch-blender-design.md` (inglés: `docs/en/superpowers/specs/2026-10-19-stadium-from-scratch-blender-design.md`).

## Global Constraints

- Nada de datos del juego en el repo (`tools/check_no_game_data.py`). Las pruebas leen el `project.bin` del usuario (`local/disc/DATA/files/project.bin`); sus salidas van a carpetas temporales o a `local/`.
- Interfaz en español e inglés con `T(es, en)`; documentación en `docs/` y su espejo `docs/en/`.
- El flujo actual (estadios importados del juego, «Convertir objetos en piezas», exportar basado en un estadio) no cambia de comportamiento: `test_import.py`, `test_export.py`, `test_convert.py`, `test_preview.py`, `selftest.py` y `tools/test_stadium_writer.py` siguen pasando.
- Versión del complemento: 0.8.0.
- Pruebas sin interfaz: `local/tools/blender-4.2.23-windows-x64/blender.exe --background --factory-startup --python tools/blender/<test>.py -- <zip> <project.bin> <carpeta temporal>`, con `BLENDER_USER_RESOURCES` temporal y rutas `cygpath -aw`; el zip sale de `python tools/blender/build_addon.py <carpeta>`.
- Pruebas en el juego: avisar antes; el usuario no toca teclado ni mando.

## Papeles (de la spec)

| id | Flags | es / en |
|---|---|---|
| `ground` | `0x04810000` | Suelo / Ground |
| `stand` | `0x00000000` | Grada / Stand |
| `fence` | `0x00001100` | Valla / Fence |
| `transparent` | `0x00060000` | Transparente / Transparent |
| `additive` | `0x000A0800` | Brillo aditivo / Additive glow |
| `crowd_lying` | `0x00001600` | Público tumbado / Lying crowd |
| `crowd_upright` | `0x00001700` | Público vertical / Upright crowd |
| `flags` | `0x00000500` | Banderas / Flags |

«Ocultar desde arriba» / «Hide from above»: bit `0x80000000`. Orden de dibujo de las piezas: `ground`, `fence`, `stand`, `crowd_lying`, `crowd_upright`, `flags`, `transparent`, `additive`.

## Review Focus

1. **Objetos que no son mallas o con modificadores** (curvas, textos, vacíos, instancias de colección): se usa la malla evaluada de los que la tengan y se ignoran los demás, sin error. Task 2 (`test_scratch.py`, un texto y un vacío en la colección).
2. **Objetos sin material, sin UV o con nombre no ASCII** (`Grada_Peñarol`): se exportan; las piezas del pack reciben nombres ASCII generados (`DP_<papel>_<n>`). Task 2.
3. **Modelos enormes:** miles de objetos del mismo papel quedan en pocas piezas; si las piezas pasan de 127, aviso claro (no error). Task 2 (300 cubos de grada → menos de 127 piezas).
4. **Colección desde cero sin carpeta del juego o sin `project.bin`**: mensaje claro al exportar o con «Ver en el juego», sin excepción sin capturar. Task 3.
5. **Lo que el escritor rechace** (`StadiumWriteError`): sale en Blender como mensaje que nombra la pieza. Task 3 (`build_stadium` sustituido en la prueba por uno que devuelve un `Stadium` inválido).

---

### Task 1: Piezas reutilizables (sin cambiar el comportamiento)

**Files:**
- Modify: `tools/blender/powerpros_stadium/convert.py`, `exporter.py`, `collision_gen.py`
- Test: `tools/blender/test_convert.py`, `test_export.py`

**Interfaces:**
- Produces:
  - `convert.prepare(sources: list, bake_light: bool = True) -> dict` — `{imagen o None: bmesh}`: las caras de los objetos en el marco del juego de Blender (metros), con el color horneado, una sola copia por cara vuelta hacia el campo, sin gemelas (`drop_back_twins` con la textura primero) y con las capas separadas (`separate_layers`). Es exactamente lo que `convert.convert` hace hoy por pieza, sacado a una función; `convert.convert` pasa a usarla.
  - `exporter.image_texture(img, hd: bool) -> tuple` — `(stadium_model.Texture RGB5A3, nombre del reemplazo HD o None, imagen HD (h, w, 4) o None)`, la codificación de `Exporter.texture_for` sacada a una función; `Exporter.texture_for` pasa a usarla.
  - `collision_gen.generate(coll)` acepta también una colección desde cero (`coll.get("pp_scratch")`): sus piezas son todas las mallas que no son la colisión.
- [ ] **Step 1:** Añadir a `test_convert.py` una comprobación de `prepare` sobre dos cubos: devuelve un bmesh por material, con color en `Color` y sin caras duplicadas. Verla fallar (`AttributeError`).
- [ ] **Step 2:** Extraer `prepare` e `image_texture` y adaptar `generate`.
- [ ] **Step 3:** `test_convert.py`, `test_export.py`, `test_import.py`, `test_preview.py` y `selftest.py` → `RESULT: PASS` / `selftest OK`.
- [ ] **Step 4:** Commit (`Stadium add-on: reusable conversion, texture and collision helpers`).

### Task 2: La descripción del estadio desde la escena

**Files:**
- Create: `tools/blender/powerpros_stadium/scratch.py`
- Create: `tools/blender/test_scratch.py`

**Interfaces:**
- Consumes: Task 1; `stadium_model`, `stadium_read.read_stadium`, `stadium_write.validate`, `projectbin.Archive`, `exporter.collision_data`, `collision_gen.build`/`make_object`.
- Produces (`scratch.py`):
  - `ROLES: dict[str, tuple[int, str, str]]` (id → flags, texto es, texto en), `DRAW_ORDER: list[str]`, `HIDE_ABOVE = 0x80000000`.
  - Propiedades registradas en `bpy.types.Object`: `powerpros_role` (`EnumProperty`: `"auto"` + los ids de `ROLES`) y `powerpros_hide_above` (`BoolProperty`).
  - `role_of(obj) -> str`: el papel elegido, o con `"auto"`, `"ground"` si todas sus caras están a menos de 0,2 m de la altura 0, si no `"stand"`.
  - `class ScratchFailure(Exception)`.
  - `build_stadium(coll, game_bases: bool = True, bake_light: bool = True) -> tuple[Stadium, dict]`: el estadio y las imágenes HD (nombre de archivo → imagen). La colección lleva `pp_scratch = 1`, `pp_stadium` (código del estadio de datos), `pp_project_bin` y `pp_game_folder`.
    - Agrupa las mallas evaluadas por (papel, ocultar desde arriba).
    - Convierte cada grupo con `convert.prepare`.
    - Reparte las texturas de cada grupo en piezas de 30 como mucho, y cada pieza en mallas con `pj7m_write.split_mesh`.
    - Nombra las piezas `DP_<papel>_<n>` (con `_up` si lleva el bit de arriba).
    - Coloca cada pieza con traslación 0, escala 1, y caja y esfera de sus vértices, todo en f32.
    - Pone las piezas en el orden de `DRAW_ORDER`.
    - Con `game_bases`, añade las piezas `BCL` del estadio de datos (leídas con `read_stadium` del `project.bin`, con sus texturas y paletas reindexadas).
    - Colisión: la del objeto colisión de la colección, o generada con `collision_gen` (y dejada en la colección como hace «Generar colisión»).
    - Campos sin descifrar como en `tools/stadium_minimal.py`.
    - Más de 127 piezas: lo anota en `Stadium` para que la exportación avise (devolver un aviso en el diccionario de estadísticas de la Task 3).
- [ ] **Step 1: Pruebas que fallan** en `test_scratch.py` (estilo `check(ok, label)`):
  - una colección desde cero (creada en la prueba con las propiedades de arriba) con:
    - un plano de 120 m (auto → suelo);
    - un anillo de valla (`fence`);
    - dos cubos de grada (`stand`);
    - un plano de público vertical;
    - un plano transparente con una imagen con alfa;
    - un plano aditivo;
    - una bandera;
    - un edificio con «ocultar desde arriba»;
    - un objeto llamado `Grada_Peñarol`;
    - un texto y un vacío.
  - `build_stadium` da:
    - una pieza por grupo, con los flags de su papel;
    - los dos cubos de grada en la misma pieza;
    - el edificio con el bit `0x80000000`;
    - las piezas en el orden de dibujo;
    - las piezas `BCL` presentes (y ausentes con `game_bases=False`);
    - colisión con triángulos;
    - todos los nombres ASCII;
  - `write_stadium` lo escribe, `read_stadium` lo vuelve a leer igual y `stadium_re.load` no da problemas;
  - 300 cubos de grada quedan en menos de 127 piezas.
- [ ] **Step 2:** Ejecutarlas y verlas fallar (`scratch` no existe).
- [ ] **Step 3:** Implementar `scratch.py`.
- [ ] **Step 4:** Ejecutar → `RESULT: PASS`.
- [ ] **Step 5:** Commit (`Stadium add-on: build a stadium description from a from-scratch collection`).

### Task 3: Exportar, interfaz y «Ver en el juego»

**Files:**
- Modify: `tools/blender/powerpros_stadium/scratch.py`, `blender_ui.py`, `preview.py`, `blender_manifest.toml` (0.8.0)
- Test: `tools/blender/test_scratch.py`

**Interfaces:**
- Consumes: Task 2.
- Produces:
  - `scratch.export_scratch(coll, mods_folder, mod_name, new_code, new_name, hd=True, menu=True, home_line="") -> dict`. Escribe `mods/<mod>/project/stadium_<code>.pack` y la tabla, `textures/stadium_<code>/` (HD), `[[stadium]]` en `mod.toml` (`exporter.add_stadium_to_toml`, base = `pp_stadium`) y, con `menu`, el pack de menú como hoy. Devuelve estadísticas: piezas, texturas, triángulos de colisión, tamaño y avisos. Los errores salen como `exporter.ExportFailure` con un texto que nombra la pieza o el objeto.
  - Operador `PP_OT_new_scratch` («Nuevo estadio desde cero» / «New stadium from scratch»): pide la carpeta del juego (o `project.bin`) y el estadio de datos, y crea la colección.
  - Panel: con un objeto de una colección desde cero, muestra su papel y la casilla.
  - El operador de exportar, con una colección desde cero, solo ofrece «Estadio nuevo» y llama a `export_scratch`.
  - «Generar colisión» y «Ver en el juego» funcionan con ella (`preview.launch` llama a `export_scratch` si `coll.get("pp_scratch")`).
- [ ] **Step 1: Pruebas que fallan** en `test_scratch.py`:
  - `bpy.ops.object.powerpros_new_scratch(filepath=<project.bin>, stadium="col")` crea la colección;
  - la exportación con el operador escribe los packs y `mod.toml` declara el código nuevo con `base = "col"`;
  - `preview.launch(coll, pose, dry_run=True)` escribe el mod de vista previa desde cero;
  - una colección sin `pp_project_bin` da `ExportFailure` con mensaje;
  - con `scratch.build_stadium` sustituido en la prueba por uno que devuelve un `Stadium` con una pieza inválida (31 texmaps), la exportación da `ExportFailure` con el nombre de la pieza.
- [ ] **Step 2:** Verlas fallar.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** `test_scratch.py`, `test_convert.py`, `test_export.py`, `test_preview.py`, `test_import.py`, `selftest.py` → PASS.
- [ ] **Step 5:** Commit (`Stadium add-on 0.8.0: stadiums from scratch`).

### Task 4: Los papeles en el juego y la documentación

**Files:**
- Modify: `tools/blender/powerpros_stadium/scratch.py` (solo si un papel no funciona), `docs/stadiums.md`, `docs/en/stadiums.md`, `docs/guia-mods.md`, `docs/en/modding-guide.md`, `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md`

- [ ] **Step 1:** Un script del agente construye en Blender sin interfaz la colección de prueba de la Task 2. Le pone a la grada una textura de asientos y al público una textura de público hecha a mano (sin datos del juego), la exporta como mod (código `scratchtest`, base `col`) y la guarda como `.blend` en `local/tmp/`.
- [ ] **Step 2: Visor** (avisar antes), con capturas a los 17 y 19 s desde la vista del bateador, una cámara alta fuera del estadio y una a la altura de las gradas. Esperado:
  - el suelo se dibuja debajo del resto;
  - lo transparente, con bordes suaves;
  - lo aditivo, sumando luz;
  - el edificio «ocultar desde arriba» no sale en la cámara alta;
  - el público cambia entre las dos capturas (animado) y se ve su textura.
- [ ] **Step 3: Partido** de 2 minutos con el mod en `build/mods`: `gameplay phase=4`, ninguna línea `unmapped`; en una captura del partido se ve la bandera. Quitar el mod al terminar.
- [ ] **Step 4:** Por cada papel que no haga lo esperado: corregir su flag o quitarlo de `ROLES`, con una línea `Ruling:` en el ledger. Volver a probar ese papel.
- [ ] **Step 5: Documentación** (español e inglés):
  - en `stadiums.md`, una sección «Estadio desde cero» (crear la colección, papeles, exportar, ver en el juego, límites);
  - en `guia-mods.md`, una mención en estadios;
  - en ingeniería inversa, lo comprobado de público y banderas.
  - Regenerar los PDF.
- [ ] **Step 6:** Commit (`Stadium add-on: from-scratch roles checked in game, docs`).
