# Packs de estadio desde cero, fase 1 (el escritor) — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** escribir `stadium_<código>.pack` y `stadium_<código>tbl.pack` desde una descripción del estadio, sin partir de ningún pack, y probarlo reconstruyendo los estadios del juego y con un estadio mínimo inventado.

**Architecture:** tres módulos de Python puro en el paquete del complemento: la descripción (`stadium_model.py`, dataclasses), el lector (`stadium_read.py`, packs del juego → descripción, con un mapa de tramos de bytes para las pruebas) y el escritor (`stadium_write.py`, descripción → los dos packs desde búferes vacíos). Dos herramientas de línea de comandos en `tools/` (reconstruir un estadio del juego como mod; generar el estadio mínimo) y un script de pruebas.

**Tech Stack:** Python 3.12 (`C:\Users\david\.pyenv\pyenv-win\versions\3.12.0\python.exe`), solo biblioteca estándar en los tres módulos (para que funcionen dentro de Blender); `numpy` solo en `tools/stadium_minimal.py` (codificación de texturas con `tools/packwii.py`).

**Spec:** `docs/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md` (inglés: `docs/en/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md`).

## Global Constraints

- Nada de datos del juego en el repo (`tools/check_no_game_data.py` en pre-commit). Packs reconstruidos, descripciones volcadas y capturas van a `local/tmp/stadium-writer/` o a una carpeta de mods local; nunca se suben.
- Las pruebas con datos del juego leen el `project.bin` del usuario: `local/disc/DATA/files/project.bin`.
- `stadium_model.py`, `stadium_read.py` y `stadium_write.py` no importan `bpy` ni `numpy`. Se importan fuera de Blender con `sys.path.insert(0, "tools/blender"); from powerpros_stadium import ...` (como `tools/stadium_re.py`).
- El escritor solo recibe un `Stadium`: nunca bytes de un pack existente.
- No se cambia el runtime ni el exportador actual del complemento.
- Código y comentarios en inglés, como el resto del paquete; documentación en `docs/` y su espejo `docs/en/`. Cada hecho nuevo del formato va a `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md`, y se regeneran los PDF con `tools/build_re_pdf.py`.
- Pruebas que deben seguir pasando: `tools/blender/test_import.py`, `test_export.py`, `test_convert.py`, `test_preview.py` y `powerpros_stadium/selftest.py`.
- Pruebas en el juego: avisar antes de abrir ventanas; el usuario no toca mando ni teclado. Visor: `WIICOMPILED_STADIUM_VIEWER=<carpeta del mod>`, `WIICOMPILED_STADIUM_VIEWER_CODE=<código>`, `WIICOMPILED_STADIUM_VIEWER_CAMERA=x,y,z,yaw,pitch,fov`. Benchmark: `WIICOMPILED_MATCH_BENCH=1`, `WIICOMPILED_MATCH_BENCH_STADIUM=<código>` con el mod activado. Capturas y registro: el `tlog.py` del agente con `TLOG_SHOTS`.

## Formato de referencia (lo que el escritor reproduce)

Punteros `0x40000000 | posición`. Cadenas terminadas en cero. Display lists, píxeles de textura y colores de paleta alineados a 32 bytes; el resto a 4.

- **Pack gráfico:** cabecera `PACKWII` de `0x20` bytes (`"PACKWII "`, 0, 0, 0, u32 `0x20`, ceros), luego texturas, paletas y modelos. Sin raíz propia.
- **Cabecera `PJ7M` (`0x28`):** `"PJ7M"`, `0x28`, → nombre (justo antes), → raíz, 0, 0, u32 texturas, u32 posiciones (suma de los arrays de posiciones de las mallas), u32 triángulos (`pj7m.raw_triangle_count`), 0.
- **Raíz (`0x44`):** 9, → escena, → tabla de texturas (0 sin texturas), → tabla de paletas (0 sin texturas), ceros, `+0x40` → `"_Wmodel_<nombre>"`.
- **Tablas de texturas y paletas:** entradas {→, 0} en paralelo, terminadas por una entrada de 8 bytes a cero. Sin paleta, el puntero es 0.
- **Escena (`0x24`):** 1, → nodo, 1, → puntero a la palabra `0x01000000`, n texmaps, → texmaps, n materiales, → materiales, `0x37`.
- **Nodo (`0x0C`):** u16 mallas, u16 0, → lista de mallas, → nombre del nodo de Maya. **Lista:** {u16 material, u16 1, → malla}. **Malla (`0x1C`):** 0, tamaño de la display list, → DL, → POS, → NRM, → CLR0, → TEX0.
- **Display list:** por dibujo, opcode (`0x98` strip, `0x90` triángulos), u16 n, y n × {u8 matriz, u16 pos, nrm, clr, tex}; rellena con 0 hasta múltiplo de 32.
- **Material (`0x34`):** u16 tipo (3; 4 en 6 materiales de Oakland), u16 primer texmap (`0xFFFF` sin textura), tres RGBA8, `+0x1C` 8 × u16 ranuras (`0xFFFF` libres), u16 `+0x32` (`unk_32`, 0 o 1); lo demás a cero.
- **Texmap (`0x4C`):** u16 índice en la tabla de texturas del modelo, u16 `+0x02` = 4, `+0x0C` u16 ancho − 1 y alto − 1, `+0x14` u16 1, `+0x16` u16 1, `+0x1C`/`+0x1E` u16 repeticiones U y V, `+0x20` f32 0,75, `+0x24` f32 1,0, `+0x38` f32 1,0, `+0x3C` f32 1,0, `+0x40` u16 1; lo demás a cero. El inventario es a pasos de 2 bytes: la prueba de bytes de la Task 2 confirma cada constante.
- **Cabecera de textura (`0x20`):** u16 alto, u16 ancho, u8 tipo (`0x80` RGBA8, `0x81` RGB5A3, `0x82` C8, `0x84` C4), u8 0, u16 (`0x2000` C4, `0x6000` RGB5A3, 0 en C8 y RGBA8), → píxeles, 4 × u32 `0x82000000`, 8 ceros. **Cabecera de paleta (`0x0C`):** u16 entradas, u16 0, u8 `0x81`, u8 0, u16 `0x6000`, → colores RGB5A3. En el juego, la de una textura C4/C8 va en `+0x24` de su cabecera.
- **Tabla (`tbl`):** cabecera `PACKWII`, **colocaciones desde `0x20`** (`0x54` bytes: → modelo en el pack gráfico, 19 f32 = traslación, rotación, escala, caja mín/máx, esfera; u32 flags), y al final la raíz {u32 número de colocaciones, 11 punteros en el orden de `stadium_tbl.ROOT_FIELDS`}. El runtime encuentra la raíz de un estadio nuevo buscando desde el final un contador menor que `0x1000` seguido de `0x40000020`.
- **Colisión:** {u16 triángulos, u16 vértices, u32 0} + triángulos {4 × u16}; vértices f32 xyz por separado.
- **Árboles:** A con una sola hoja en el orden de las colocaciones si hay ≤ 127 (como `TblWriter.set_model_tree`, el orden de dibujo depende de él), si no `tbl_write.build_tree`; B con `tbl_write.build_tree(triangle_boxes, leaf=12)`; `tbl_write.pack_tree` para los bytes; la lista justo detrás de los nodos, alineada a 4.
- **`unk_20`:** grupos de `0x20` bytes {→ lista de puntos, u32 n, f32 caja mín xyz, máx xyz}, terminados por un grupo a ceros; cada punto, `0x18` bytes {f32 posición xyz, rotación xyz}.
- **`unk_24`:** pares {→ lista, u32 n} de registros de `0x1C` bytes (7 f32); normalmente 2 pares; `bullpen`, `bullpen2` y `season` tienen un solo par {0, 0} y la raíz justo detrás.
- **Texturas precargadas:** `unk_2c` apunta a {u32 n, n × u32 orden}, y justo detrás (puntero `textures`) n entradas {→ cabecera de textura del pack gráfico, → cabecera de paleta o 0, u32 TMEM, u32 TMEM}. En el juego, n = 8 siempre.
- **Orden en las tablas del juego:** …, vértices de colisión, `unk_20`, `unk_2c`, texturas precargadas, `unk_24`, raíz.

## Review Focus

1. **Piezas sin texturas** (97 en los estadios del juego): tablas de texturas y paletas con puntero 0 y 0 texturas en la cabecera. Prueba en la Task 2 (`test_graphics_synthetic`, un modelo sin texturas).
2. **Materiales sin textura** (226): primer texmap `0xFFFF` y ninguna ranura usada. Task 2, mismo test.
3. **Más de 127 colocaciones:** el árbol A deja de ser una hoja y usa `build_tree`. Ningún estadio del juego pasa de 100. Task 3 (`test_table_many_placements`, 140 colocaciones).
4. **Bloques vacíos:** `unk_20` sin grupos, `unk_24` con pares vacíos o con un solo par, lista de precargadas vacía. Task 3 (`test_table_empty_blocks`) y la ida y vuelta de `bullpen`/`season` en la Task 4.
5. **Límites superados** (31 texmaps en un modelo, 65.536 triángulos de colisión, 4.096 colocaciones): un `StadiumWriteError` claro que nombra el modelo o el límite, sin escribir un pack a medias. Task 3 (`test_write_limits`).

---

### Task 1: La descripción y el lector

**Files:**
- Create: `tools/blender/powerpros_stadium/stadium_model.py`
- Create: `tools/blender/powerpros_stadium/stadium_read.py`
- Create: `tools/test_stadium_writer.py`
- Modify: `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md` («Inventario de campos…»: `unk_20` es una lista de grupos de puntos con caja, no puntos de luces; `unk_24` puede tener un solo par; el bloque de la escena es un puntero a la palabra `0x01000000`)

**Interfaces:**
- Produces (`stadium_model.py`):
  - `Palette(entries: int, colors: bytes)`, `Texture(fmt: int, width: int, height: int, pixels: bytes)` (`fmt` GX: 5 RGB5A3, 6 RGBA8, 8 C4, 9 C8)
  - `TexMap(slot: int, repeat_u: int, repeat_v: int)` (`slot`: índice en `Model.textures`)
  - `Material(kind: int, colors: tuple, texmaps: list[int], unk_32: int)` (`colors`: tres tuplas RGBA)
  - `Mesh(material: int, positions: list, normals: list, colors: list, uvs: list, draws: list)` (`draws`: `[(opcode, [(mtx, pos, nrm, clr, tex), …])]`)
  - `Model(name: str, wname: str, node_name: str, textures: list[tuple[int, int | None]], texmaps: list[TexMap], materials: list[Material], meshes: list[Mesh], scene_word: int = 0x01000000)` (`textures`: (índice en `Stadium.textures`, índice en `Stadium.palettes` o `None`))
  - `Placement(model: int, translation: tuple, rotation: tuple, scale: tuple, bbox: tuple, sphere: tuple, flags: int)`
  - `PointGroup(points: list[tuple], box: tuple)` (puntos de 6 f32, caja de 6 f32)
  - `PreloadEntry(texture: int, palette: int | None, tmem_a: int, tmem_b: int)`
  - `Stadium(textures, palettes, models, placements, collision_vertices, collision_triangles, unk_20: list[PointGroup], unk_24: list[list[tuple]], preload_order: list[int], preload: list[PreloadEntry])`
  - `Span(kind: str, key: tuple, start: int, end: int, pointers: tuple[int, ...])` (`key`: índices dentro de la descripción, como `(modelo, malla)`, nunca posiciones en el archivo, para emparejar el original con lo reescrito; `pointers`: posiciones de campos puntero relativas a `start`)
- Produces (`stadium_read.py`): `read_stadium(gfx: bytes, tbl: bytes, spans: dict[str, list[Span]] | None = None) -> Stadium`. Con `spans`, añade a `spans["gfx"]` y `spans["tbl"]` un tramo por estructura leída (cabeceras, raíz, escena, palabra de escena, nodo, lista de mallas, malla, DL, cada array, material, texmap, tablas de texturas/paletas, cabeceras y datos de textura y paleta, cadenas, colocación, árboles A/B con sus listas, colisión, vértices, `unk_20` y sus listas, `unk_24` y sus listas, `unk_2c`, precargadas, raíz). Lanza `stadium_read.ReadError` ante algo que no sabe representar (por ejemplo los campos extra de raíz de `stadium_common`).

- [ ] **Step 1: Pruebas que fallan** en `tools/test_stadium_writer.py` (estilo `check(ok, label)` de `tools/blender/test_export.py`; `python tools/test_stadium_writer.py [project.bin]` ejecuta las sintéticas y, con el `project.bin`, las del juego; acaba en `RESULT: PASS|FAIL`):
  - `test_reader_known_values(archive)`: Coors (`col`) tiene 82 colocaciones, 4 grupos `unk_20` de 32, 50, 50 y 25 puntos, 2 listas `unk_24` de 7, 8 precargadas con orden 0–7; Yankee (`nyy`) 2 grupos de 150 y 144; `bullpen` tiene 0 grupos y una sola lista `unk_24` vacía; ningún `scene_word` distinto de `0x01000000`.
  - `test_reader_covers_every_byte(archive)`: en los 40 estadios con tabla, todo byte distinto de cero de los dos packs cae dentro de algún tramo, y ningún tramo se sale de su archivo.
- [ ] **Step 2: Ejecutar y ver que fallan** — `python tools/test_stadium_writer.py local/disc/DATA/files/project.bin` → `ModuleNotFoundError: stadium_model` y `RESULT: FAIL`.
- [ ] **Step 3: Implementar** `stadium_model.py` y `stadium_read.py` reutilizando `pj7m.parse_model`, `pj7m.walk_display_list`, `pj7m.array` y `stadium_tbl.parse_table`. Texturas y paletas se deduplican por posición en el archivo (la misma cabecera = el mismo índice). Los bytes de píxeles miden ancho × alto × bits del formato / 8; los de la paleta, entradas × 2.
- [ ] **Step 4: Ejecutar** → las dos pruebas `PASS`. Si la cobertura encuentra bytes sueltos, se identifican, se añaden a la descripción con nombre y se documentan (es la regla del diseño: no perder ningún campo).
- [ ] **Step 5: Documentar** los tres hechos de arriba en las dos versiones del documento de ingeniería inversa y regenerar los PDF.
- [ ] **Step 6: Commit** (`Stadium writer 1/6: stadium description and reader`).

### Task 2: El escritor del pack gráfico

**Files:**
- Create: `tools/blender/powerpros_stadium/stadium_write.py`
- Test: `tools/test_stadium_writer.py`

**Interfaces:**
- Consumes: Task 1.
- Produces: `GraphicsLayout(models: list[int], textures: list[int], palettes: list[int])` (posición en el pack gráfico de cada cabecera de modelo, de textura y de paleta); `write_graphics(st: Stadium) -> tuple[bytes, GraphicsLayout]`; `class StadiumWriteError(ValueError)`.

- [ ] **Step 1: Pruebas que fallan:**
  - `test_graphics_synthetic()` (sin datos del juego): un `Stadium` hecho a mano con una textura RGB5A3 de 8×8, una C4 de 8×8 con paleta de 16, un modelo con dos mallas (una con strip y otra con triángulos), un material sin textura y un modelo sin texturas. `pj7m.find_models` encuentra 2 modelos; `pj7m.parse_model` devuelve los mismos nombres, mallas (`draws` iguales), arrays, materiales y texturas; `stadium_re.check_model` no da problemas; las DL, los píxeles y los colores empiezan en múltiplos de 32.
  - `test_graphics_bytes_match_game(archive)`: para los 40 estadios, con los tramos del lector sobre el original y sobre lo escrito, cada estructura del pack gráfico con la misma `(kind, key)` tiene los mismos bytes una vez puestos a cero sus campos puntero.
- [ ] **Step 2: Ejecutar** → FAIL por `stadium_write` inexistente.
- [ ] **Step 3: Implementar `write_graphics`** desde un `bytearray` vacío, con el formato de referencia de arriba. Texturas y paletas una sola vez cada una (con la cabecera de paleta de una textura C4/C8 en `+0x24` de su cabecera, como el juego); el primer texmap del material es `texmaps[0]` o `0xFFFF`; el tamaño del texmap sale de la textura de su ranura.
- [ ] **Step 4: Ejecutar** → PASS. Una diferencia de bytes en un campo constante corrige la constante del formato de referencia en este plan y en el documento de ingeniería inversa.
- [ ] **Step 5: Commit** (`Stadium writer 2/6: graphics pack from scratch`).

### Task 3: El escritor de la tabla

**Files:**
- Modify: `tools/blender/powerpros_stadium/stadium_write.py`
- Test: `tools/test_stadium_writer.py`

**Interfaces:**
- Consumes: Task 2 (`write_graphics`, `GraphicsLayout`), `tbl_write.build_tree`, `tbl_write.pack_tree`, `tbl_write.triangle_boxes`, `tbl_write.MAX_LEAF`.
- Produces: `write_table(st: Stadium, layout: GraphicsLayout) -> bytes`; `write_stadium(st: Stadium) -> tuple[bytes, bytes]` (gráfico, tabla). `write_stadium` comprueba los límites antes de escribir nada.

- [ ] **Step 1: Pruebas que fallan:**
  - `test_table_synthetic()`: el estadio de la Task 2 con 3 colocaciones, 4 triángulos de colisión, 1 grupo `unk_20` de 2 puntos, 2 listas `unk_24` de 1 registro y 2 precargadas. `stadium_tbl.find_root` encuentra la raíz y las colocaciones empiezan en `0x20`; `read_stadium(write_stadium(st))` es igual a `st`; `stadium_re.load` no da problemas.
  - `test_table_empty_blocks()`: `unk_20 = []`, `unk_24 = [[], []]`, `preload = []`: se escriben presentes (sin punteros a 0 en la raíz) y vuelven a leerse iguales.
  - `test_table_many_placements()`: 140 colocaciones de un modelo: el árbol A tiene más de un nodo y `stadium_re.check_trees` no da problemas.
  - `test_write_limits()`: 31 texmaps en un modelo, 65.536 triángulos de colisión y 4.096 colocaciones dan cada uno `StadiumWriteError` con el nombre del modelo o el límite en el texto.
  - `test_table_bytes_match_game(archive)`: como en la Task 2, para las estructuras de la tabla salvo los árboles A y B (se reconstruyen), en los 40 estadios.
- [ ] **Step 2: Ejecutar** → FAIL.
- [ ] **Step 3: Implementar** `write_table` y `write_stadium` con el formato de referencia: punteros al pack gráfico desde `layout`; árboles nuevos; los bloques en el orden del juego y la raíz al final; relleno final a 32 bytes y comprobación de que `stadium_tbl.find_root` da la raíz escrita.
- [ ] **Step 4: Ejecutar** → PASS.
- [ ] **Step 5: Commit** (`Stadium writer 3/6: table from scratch`).

### Task 4: Ida y vuelta de los 40 estadios y la herramienta de reconstrucción

**Files:**
- Create: `tools/stadium_rebuild.py`
- Test: `tools/test_stadium_writer.py`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: `python tools/stadium_rebuild.py <project.bin> <código del juego> <carpeta de mods> [--code <código nuevo>] [--original]`. Escribe `<carpeta de mods>/Reconstruido <código>/` con `mod.toml` (`name = "Reconstruido <código>"` y `[[stadium]] code = "rb_<código>"`, `name`, `base = "<código>"`) y `project/stadium_rb_<código>.pack` y `stadium_rb_<código>tbl.pack`. Con `--original`, copia los packs del juego sin pasar por el escritor, como `Original <código>` y código `og_<código>` (para comparar en el juego por la misma vía).

- [ ] **Step 1: Pruebas que fallan:**
  - `test_round_trip_all(archive)`: para los 40 estadios con tabla, `read_stadium(*write_stadium(read_stadium(gfx, tbl))) == read_stadium(gfx, tbl)`, y `stadium_re.load` con `check_model` sobre lo escrito no da problemas.
  - `test_writer_uses_only_the_description(archive)`: para Coors, la descripción se pasa a una estructura de listas, números y cadenas hexadecimales y se reconstruye desde ahí (sin ningún objeto del lector); `write_stadium` de las dos da los mismos bytes.
  - `test_rebuild_tool(archive)`: `stadium_rebuild.py` sobre `col` en `local/tmp/stadium-writer/mods` deja el `mod.toml` y los dos packs esperados, y los packs se leen sin error.
- [ ] **Step 2: Ejecutar** → FAIL.
- [ ] **Step 3: Implementar** `tools/stadium_rebuild.py` (con `projectbin.Archive` y `exporter.valid_code` para el código) y lo que las pruebas pidan del escritor.
- [ ] **Step 4: Ejecutar** → PASS, con el tiempo total de la ida y vuelta de los 40 anotado en el ledger.
- [ ] **Step 5: Commit** (`Stadium writer 4/6: round trip of the game's stadiums, rebuild tool`).

### Task 5: El estadio mínimo inventado

**Files:**
- Create: `tools/stadium_minimal.py`
- Test: `tools/test_stadium_writer.py`

**Interfaces:**
- Consumes: Tasks 1–3, `tools/packwii.py` (`to_rgb5a3`, `tile`).
- Produces: `minimal_stadium() -> Stadium` y `python tools/stadium_minimal.py <carpeta de mods> [--code minimo]`, que escribe `<carpeta de mods>/Estadio mínimo/` con `[[stadium]] code = "minimo"`, `name = "Estadio mínimo"`, `base = "col"`.

El estadio, en decímetros (Y arriba, plato en el origen, −Z hacia el center field), sin ningún dato leído del juego:
- **suelo:** un cuadrado de 2.400 × 2.400 centrado en (0, 0, −600) con una textura RGB5A3 de 64 × 64 a cuadros verdes generada en código, repetida 40 veces; flags `0x04810000`;
- **valla:** un anillo de 32 lados de radio 1.100 alrededor de (0, 0, −450) y 30 de alto, color de vértice gris, material sin textura; flags `0x00001100`;
- **grada:** una rampa de 32 lados detrás de la valla, de 1.150 a 1.600 de radio y de 30 a 250 de alto; flags `0x00000000`;
- **colisión:** el suelo (superficie 5), la valla (9) y una pared invisible sobre la valla hasta 200 de alto (1);
- **campos sin descifrar** en su valor más común en el juego: material tipo 3, colores (`000000FF`, `CCCCCCFF`, `000000FF`), `unk_32 = 1`; `unk_20 = []`, `unk_24 = [[], []]`, precargadas vacías.

- [ ] **Step 1: Prueba que falla:** `test_minimal_stadium()` (sin datos del juego): `write_stadium(minimal_stadium())` se vuelve a leer igual, `stadium_re.load` y `check_model` no dan problemas, y el pack gráfico ocupa menos de 1 MB.
- [ ] **Step 2: Ejecutar** → FAIL.
- [ ] **Step 3: Implementar** `tools/stadium_minimal.py`.
- [ ] **Step 4: Ejecutar** → PASS.
- [ ] **Step 5: En el juego** (avisar antes): escribir el mod en una carpeta temporal y abrir el visor con él (`WIICOMPILED_STADIUM_VIEWER=<mod>`, `…_CODE=minimo`, cámara `0,150,600,0,-0.2,45`). Esperado: `[viewer] stadium ready` en ≤ 12 s y una captura con suelo verde a cuadros, la valla y la grada. Después, el mod en `local/wiicompiled-mlb/build/mods` y el benchmark con `WIICOMPILED_MATCH_BENCH_STADIUM=minimo` 2 minutos: `gameplay phase=4` y ninguna línea `unmapped`. Quitar el mod de `build/mods` al terminar. Si el juego no lo carga por un campo sin descifrar, se descifra solo ese campo, se documenta y se anota en el ledger (regla del diseño).
- [ ] **Step 6: Commit** (`Stadium writer 5/6: minimal stadium from nothing`).

### Task 6: Reconstrucciones en el juego y documentación

**Files:**
- Modify: `docs/stadiums.md`, `docs/en/stadiums.md` («Para desarrolladores»: `test_stadium_writer.py`, `stadium_rebuild.py`, `stadium_minimal.py`)
- Modify: `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md` («Escribir packs desde cero»: qué se escribe, cómo se probó, resultados)

- [ ] **Step 1: Reconstruir** `col`, `sf` y `nyy` con `stadium_rebuild.py`, y también con `--original`, en `local/tmp/stadium-writer/mods`.
- [ ] **Step 2: Capturas en el visor** (avisar antes), a los 10 s de `[viewer] stadium ready`, con tres vistas: la de bateador (sin cámara inicial), `-1100,900,1100,0.574,-0.418,45` y `20,25,60,1.15,0.12,45`. Para cada estadio y vista: original (`og_<código>`) dos veces y reconstruido (`rb_<código>`) una. Esperado: diferencia media por píxel entre original y reconstruido ≤ máx(1,0; 1,5 × la diferencia entre las dos capturas del original), que mide lo que se mueve solo (banderas, público).
- [ ] **Step 3: Benchmark** de 2 minutos con cada reconstruido (mod en `build/mods`, activado con el `MODS` de `tlog.py`): `gameplay phase=4` y ninguna línea `unmapped`. Quitar los mods al terminar.
- [ ] **Step 4: Pruebas de siempre:** `test_import.py`, `test_export.py`, `test_convert.py`, `test_preview.py` y `selftest.py` → `RESULT: PASS` / `selftest OK`; `python tools/test_stadium_writer.py local/disc/DATA/files/project.bin` → `RESULT: PASS`.
- [ ] **Step 5: Documentación** en español e inglés y PDF regenerados.
- [ ] **Step 6: Commit** (`Stadium writer 6/6: rebuilds checked in game, docs`).
