# Packs de estadio desde cero — diseño (fase 1: el escritor)

*English version: [../../en/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md](../../en/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md)*

Fecha: 18 de octubre de 2026. Estado: aprobado por el usuario el 18 de octubre de 2026.

## Por qué

Hoy un estadio nuevo entra en el juego **dentro de un estadio existente** (AT&T Park, Coors Field…): el complemento de Blender vacía sus piezas, mete la geometría del modelo y copia del original todo lo que no entiende (materiales, texmaps, cabeceras, la tabla del estadio). Lo heredado a ciegas rompe cosas sin avisar: el 18 de octubre las repeticiones de textura copiadas del primer texmap dejaron las gradas del Polo Grounds grises sobre AT&T Park y bien sobre Coors Field, por suerte.

El usuario eligió como camino a largo plazo **generar los dos packs del estadio enteros desde cero, sin plantilla** (y aparcó la otra vía, un renderizador propio para `.glb`). El inventario de campos del 18 de octubre (`docs/ingenieria-inversa.md`, «Inventario de campos para escribir packs desde cero») muestra que casi todo el formato está descifrado: lo que varía entre estadios son sobre todo punteros, contadores y tamaños. Faltan los flags de colocación, tres bloques de la tabla y unos pocos campos de modelo.

El proyecto completo tiene cuatro fases, cada una con su diseño:
1. **El escritor (este diseño):** escribir `stadium_<código>.pack` y `stadium_<código>tbl.pack` desde una descripción del estadio, sin partir de ningún pack existente, y probarlo reconstruyendo los estadios del juego.
2. Descifrar con el visor lo que falta (flags, bloques de la tabla, campos sueltos) y documentarlo.
3. Modo «desde cero» en el complemento de Blender: los objetos llevan su papel y el complemento escribe los packs con el escritor de la fase 1.
4. El Polo Grounds hecho desde cero, en el visor y en un partido.

El orden lo propuso el agente y el usuario lo aceptó («adelante»): primero un escritor probado, para que lo que se descifre después se pruebe sobre algo fiable.

## Objetivos de la fase 1

1. **Una descripción del estadio** en Python con todo lo que hace falta para escribir los dos packs: modelos (mallas, materiales, texmaps, texturas y paletas compartidas), colocaciones, colisión y los bloques de la tabla. Los campos aún sin descifrar van en ella **como valores con nombre** (por ejemplo `material.unk_32`), no como trozos de bytes del original.
2. **Un lector** que convierte un estadio del juego (sus dos packs) en esa descripción.
3. **Un escritor** que convierte una descripción en los dos packs, empezando por un búfer vacío. No recibe ni lee ningún pack existente.
4. **Pruebas** de que el escritor es correcto (ver «Cómo se comprueba»):
   - ida y vuelta de los 40 estadios con tabla;
   - un estadio mínimo inventado, sin leer nada del juego, que el juego carga;
   - estadios del juego reconstruidos que el juego dibuja igual que los originales.

## No objetivos

- Descifrar los campos desconocidos: es la fase 2. En la fase 1 se leen, se guardan y se vuelven a escribir.
- Cambiar el complemento de Blender o su exportación actual: es la fase 3. El exportador actual sigue como está.
- Cambiar el runtime. No hace falta ningún parche nuevo: los estadios reconstruidos entran como estadios nuevos de un mod (`[[stadium]]`), por la vía que ya existe.
- Igualdad byte a byte con los packs del juego. Lo que importa es que el juego los cargue y los dibuje igual.

## Anti-objetivos

- **Un escritor que copie a escondidas** bloques del pack original. Si lo hace, la fase 1 no prueba nada. El escritor solo recibe la descripción, y las pruebas lo vigilan.
- **Contenido del juego en el repositorio:** los packs reconstruidos, las descripciones leídas de los packs del juego y las capturas son contenido del juego. Se generan en `local/` y nunca se suben. Las pruebas leen el `project.bin` del usuario.
- Que el escritor solo funcione con lo que hay en los packs del juego (por ejemplo, que dependa del orden de los objetos en el archivo). Tiene que servir para estadios que nunca existieron.

## Lo que ya existe

- **Lectores** (Python puro, sin Blender), en `tools/blender/powerpros_stadium/`:
  - `pj7m.py` lee los modelos: cabecera, raíz, escena, nodo, mallas, materiales, texmaps, texturas y display lists;
  - `stadium_tbl.py` lee la tabla: raíz de 11 punteros, colocaciones, colisión y árboles.
- **Escritores parciales**, que hoy modifican un pack existente:
  - `pj7m_write.py` (`PackWriter`): añade texturas RGB5A3 y materiales, reemplaza mallas, trocea mallas y ordena triángulos;
  - `tbl_write.py`: construye y escribe los árboles A y B y la colisión.
- **Herramientas:** `tools/stadium_re.py` (`check` comprueba las invariantes en los 47 packs), `tools/projectbin.py`, `tools/packwii.py` (codificación de texturas).
- **Formatos:** `docs/ingenieria-inversa.md`, sección 5 («Formato `PACKWII`», «Estadios: modelos `PJ7M` y tabla `tbl`», «Inventario de campos…»).
- **Runtime:**
  - estadios nuevos por mod (`[[stadium]]` en `mod.toml`, parche `wiicompiled-new-stadiums`): el runtime da números libres a sus archivos y encuentra la raíz de su tabla buscando desde el final;
  - el visor de estadios (`WIICOMPILED_STADIUM_VIEWER`, cámara inicial por `WIICOMPILED_STADIUM_VIEWER_CAMERA`) y el benchmark de partido (`WIICOMPILED_MATCH_BENCH_STADIUM`);
  - las herramientas de captura del agente: `tlog.py` con `TLOG_SHOTS`.

## Datos del formato que fijan el diseño

- **Los dos packs van juntos.** El pack gráfico es una cabecera `PACKWII` (sin tabla de entradas) seguida de texturas, paletas y modelos, sin raíz propia. El juego llega a todo desde la tabla: cada colocación apunta a la cabecera `PJ7M` de su modelo en el pack gráfico, y la lista de texturas de la tabla apunta a cabeceras de textura del pack gráfico. El escritor tiene que escribir los dos a la vez y conocer las posiciones del pack gráfico al escribir la tabla.
- **Punteros:** `0x40000000 | posición` dentro de su archivo. Display lists y píxeles de textura alineados a 32 bytes.
- **Raíz de la tabla:** va al final del archivo, y las colocaciones empiezan en `0x20`: el runtime encuentra la raíz de un estadio nuevo buscando desde el final un contador seguido de un puntero a `0x20`. Para un estadio del juego, la posición de la raíz está apuntada desde la tabla de estadios del REL (`0x80E9F9D0`); para un estadio nuevo, el runtime la busca desde el final. Por eso las pruebas en el juego usan estadios nuevos.
- **Texturas compartidas:** varias piezas del pack usan la misma textura, y la tabla de texturas y la de paletas de cada modelo acaban con una entrada de 8 bytes a cero.
- **Límites conocidos:** como mucho 30 texmaps por pieza, hojas de árbol de como mucho 127 entradas y 65.535 triángulos de colisión.

## Diseño

**Descripción del estadio** (decisión del agente: dataclasses de Python, sin dependencias de Blender):
- `Stadium`: lista de texturas, lista de modelos, colocaciones, colisión y bloques de la tabla.
- `Texture`: formato GX, ancho, alto, píxeles ya codificados y paleta (o ninguna). Los modelos la referencian por índice, así que las compartidas se escriben una vez.
- `Model`: nombre, nombre del nodo de Maya, mallas y materiales, más los campos de la escena y la raíz que aún no se entienden (por ejemplo el bloque de `{1, → ?}`).
- `Mesh`: material y primitivas tal como están, cada una con su tipo (strip `0x98` o triángulos `0x90`) y sus vértices como índices a los arrays de posiciones, normales, colores y UV de la malla. Así un estadio reconstruido dibuja exactamente los mismos triángulos, y la fase 3 puede dar solo triángulos.
- `Material` y `TexMap`: los campos conocidos con nombre, y los desconocidos también con nombre provisional (`unk_32`, colores sin significado comprobado…).
- `Placement`: modelo por índice, traslación, rotación, escala, caja, esfera y flags.
- `Collision`: vértices y triángulos con superficie.
- Bloques de la tabla: `unk_20`, `unk_24` y la lista de texturas precargadas (con sus valores TMEM) como registros con campos, no como bytes. La lista de texturas precargadas referencia texturas de la descripción por índice.

**Lector:** reutiliza `pj7m.py` y `stadium_tbl.py`; añade lo que les falta para no perder ningún campo (el bloque de la escena, los bloques de la tabla, los campos del material).

**Escritor** (decisión del agente: un módulo nuevo junto a los actuales, que reutiliza la codificación y la construcción de árboles existentes):
- empieza por un búfer vacío por archivo;
- escribe el pack gráfico: cabecera `PACKWII`, texturas y paletas, y cada modelo con todas sus estructuras, contadores y punteros calculados;
- escribe la tabla: colocaciones con punteros al pack gráfico, árbol A y árbol B **construidos de nuevo** con `tbl_write.build_tree` (no se copian los del juego), colisión, bloques de la tabla y la raíz al final;
- comprueba al escribir los límites conocidos y falla con un mensaje claro si se pasan.

**Herramienta de línea de comandos** (decisión del agente): `tools/stadium_rebuild.py <project.bin> <código del juego> <carpeta de mods> [código nuevo]`. Lee el estadio del juego, lo escribe con el escritor y deja un mod con `[[stadium]]` (por ejemplo `rb_col`, base `col`) listo para el visor y el benchmark.

## Cómo se comprueba

1. **Ida y vuelta de los 40 estadios con tabla:** leer, escribir, volver a leer lo escrito y comparar con la descripción, campo a campo: deben ser iguales. Los packs escritos pasan las comprobaciones de `stadium_re.py check`.
2. **El escritor no copia:** su única entrada es la descripción. Una prueba además escribe la descripción, la pasa por una copia profunda sin ninguna referencia a los bytes leídos y comprueba que el resultado es el mismo.
3. **Estadio mínimo inventado** (sin leer nada del juego): un suelo con textura, una valla, una grada y una colisión sencilla. El visor lo muestra y el benchmark juega un partido de 2 minutos con él sin errores. Es la prueba de «sin plantilla».
   - Los campos sueltos sin descifrar (`material.unk_32`, flags de colocación…) llevan su valor más común en los packs del juego, como constantes escritas a mano en el código (números, no datos copiados).
   - Los bloques de la tabla sin descifrar (`unk_20`, `unk_24`, la lista de texturas precargadas y `unk_2c`) van **ausentes**, con su puntero a 0: así están en varias tablas del juego (`unk_20` falta en 4, `unk_24` en 3 y la lista de texturas en 3). Decisión del agente.
   - Si aun así el juego no lo carga por un campo sin descifrar, la fase 1 descifra **solo ese campo**, lo documenta y sigue. Decisión del agente.
4. **Reconstrucciones en el juego:** Coors Field, AT&T Park y Yankee Stadium como estadios nuevos.
   - Capturas del visor del original y del reconstruido desde las mismas vistas (detrás del bateador y dos cámaras libres): iguales a la vista, con una diferencia de píxeles por debajo de un umbral que fije el constructor.
   - Un partido del benchmark de 2 minutos con cada reconstruido, sin errores de memoria en el registro.
5. **Las pruebas de siempre siguen pasando:** las del complemento (`test_import`, `test_export`, `test_convert`, `test_preview`, `selftest`).

## Lo que se deja al constructor

- Nombres de módulos, clases y funciones, y cómo se reparte el código entre el lector, la descripción y el escritor.
- El orden de los objetos dentro de cada archivo, mientras se respeten la alineación y la raíz al final.
- El umbral de diferencia de píxeles de las capturas.
- Si el lector guarda alguna información extra para poder comparar, mientras el escritor no la use.
- **La descripción vive en memoria** (lector → escritor, y más tarde Blender → escritor). En la fase 1 basta un volcado legible para depurar, en `local/`. Un formato guardado y estable, si hace falta, se decide en la fase 3. Decisión del agente.

## Después de la fase 1

La fase 2 usa el estadio mínimo y las reconstrucciones como banco de pruebas: cambiar un flag o un bloque en la descripción, escribir, mirar en el visor y documentar. Su diseño se escribe cuando acabe la fase 1.
