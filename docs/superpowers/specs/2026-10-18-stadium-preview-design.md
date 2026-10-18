# Vista previa de estadios en el juego — diseño

*English version: [../../en/superpowers/specs/2026-10-18-stadium-preview-design.md](../../en/superpowers/specs/2026-10-18-stadium-preview-design.md)*

Fecha: 18 de octubre de 2026. Estado: aprobado por el usuario el 18 de octubre de 2026.

## Por qué

Quien hace un estadio con el complemento de Blender (`tools/blender/powerpros_stadium`) no puede fiarse de lo que ve en Blender: el modelo se ve bien ahí y al llevarlo al juego aparecen fallos que Blender no muestra (superficies que parpadean, piezas que no se dibujan, texturas de más, banderas o público del estadio base, plato o colores raros). Hoy, para verlos, hay que exportar, activar el mod en el launcher, abrir el juego y empezar un partido. Es una depuración lenta que el usuario no debería tener que hacer.

Lo que pidió el usuario: un botón, mientras modela y antes de exportar de verdad, que le muestre el estadio **tal como lo pinta el juego real**, sin empezar un partido, y moverse por él libremente. Prefirió el juego real a una aproximación dentro de Blender porque es lo fiel.

## Objetivos

**Fase 1 (este diseño):**
1. Botón **«Ver en el juego»** en el panel Power Pros de Blender.
2. El botón exporta el estadio de la escena a un mod temporal de vista previa y abre el juego instalado en un **modo visor**: sin launcher, sin menús, sin la pantalla de aviso de la correa, directamente en ese estadio.
3. En el visor, una **cámara libre** controlada con teclado y ratón, y teclas para saltar a las vistas de un partido (detrás del bateador, desde el pitcher, aérea). Esas vistas son **las cámaras reales del juego** (decisión del usuario): misma posición, encuadre y campo de visión que en un partido, y con ellas el juego muestra u oculta las piezas que dependen de la cámara (flags de colocación `0x80000000`, `0x40000000`, `0x20000000`: gradas `NOUPCAM`, `EVC`, `2F`), que es justo un fallo que la vista previa debe dejar ver.
4. La cámara arranca en la misma posición y orientación que la vista 3D activa de Blender.
5. Solo el estadio: sin jugadores, sin marcador ni HUD, con la lógica del partido quieta (jugadores y pelota). El estadio sigue animado: público, banderas y marcadores animados se mueven como en un partido, porque sus fallos también deben verse.
6. Si ya hay una ventana del visor abierta, el botón la cierra y abre una nueva con el estadio actualizado. Objetivo de tiempo: el estadio visible unos 8 segundos después de pulsar.

**Fase 2 (después, fuera de este plan salvo lo que la prepare):** el visor se queda abierto y el estadio se actualiza solo cuando cambia algo en Blender, en unos segundos y sin reiniciar el juego.

## No objetivos

- No se edita nada dentro del juego: se modela en Blender y el juego solo muestra.
- No es un editor nuevo ni sustituye a Blender. Un editor propio o editar dentro del juego quedan como idea muy lejana, solo si Blender no resuelve el problema del todo.
- No es una vista aproximada dentro de Blender (se descartó a favor del juego real).
- No cambia el juego normal: con el visor apagado, el partido, el benchmark y el launcher se comportan como antes.

## Anti-objetivos

- Que la vista previa toque los mods o la configuración del usuario (`UserData/Config.toml`, la lista `[mods] enabled`, `build/mods`) de forma que se note fuera de la vista previa.
- Que lo que muestra el visor no sea lo que se vería en un partido real con ese mod (otra ruta de carga, otro dibujado). El visor debe cargar el estadio por la misma vía que un partido.
- Que la cámara libre deje al juego en un estado que cuelgue o corrompa memoria (la lección del límite de 30 texturas: un fallo silencioso que solo se ve como "cosas raras").

## Lo que ya existe y está comprobado

**Runtime** (`local/wiicompiled-mlb/src/runtime`, cambios como parches en `tools/patches/`, registrados en `tools/wiicompiled-translate.ps1`):
- `match_benchmark.cpp` (activado con `WIICOMPILED_MATCH_BENCH=1`) ya lleva el juego directo a un partido Yankees–Rockies: salta escenas, prepara la configuración (`0x8104E1D8`) y entra en la escena del partido (409), y salta la presentación del partido.
- `WIICOMPILED_MATCH_BENCH_STADIUM=<código>` elige un estadio nuevo de un mod activado (`new_stadiums::ForceForMatch`).
- Las variables de entorno de pruebas ya hacen que el launcher no se muestre (`launcher.cpp`, `AutomatedRun`).
- Los mods activados salen de `Config.toml` `[mods] enabled`; un mod de estadio declara `[[stadium]]` en su `mod.toml` y trae `project/stadium_<código>.pack`, `stadium_<código>tbl.pack` y, opcional, `env_stadium_menu_<código>.pack` (`mods.cpp`, `hle/mlb/new_stadiums.cpp`).

**Medido el 18 de octubre de 2026** (partido de prueba, RX 5500):
- De abrir el juego a la primera escena pasan 23 s; 21 de ellos son la pantalla de aviso de la correa esperando A. Pulsando A automáticamente (`WIICOMPILED_PAD_SCRIPT=2500-2700:100,3500-3700:100`), el estadio está en pantalla a los ~8 s. La carga del partido en sí son ~3 s.
- Escribir en el gestor de escenas (`0x81033268`: `+1028` siguiente escena, `+1040` y `+1043` a 1) la escena 3 desde el partido lo saca y el benchmark lo vuelve a meter en la 409 en ~0,1 s; el partido sigue estable a 60 FPS. Pero **el juego no vuelve a leer los archivos del estadio**: se queda con el ya cargado (no sale el mensaje `[stadiums] loading`). Pedir la 409 estando en la 409 no hace nada.
- La cámara del partido **no** se construye con `C_MTXLookAt` del SDK (`0x800A8DC4`): solo la llaman dos funciones del DOL (`0x800A8D6C`, `0x80090AF8`). Una sonda que envolvía `C_MTXLookAt` en el shard generado colgó el arranque justo después de iniciar GX (causa sin investigar). El juego construye su cámara con código propio, sin localizar.

**Complemento de Blender** (versión 0.6.0):
- `exporter.export_stadium(coll, mods_folder, mod_name, target_code, new_name, hd, menu, home_line)` escribe un mod completo (packs, texturas HD, pack de menú, `mod.toml`) y lanza `ExportFailure` con mensajes claros (por ejemplo, más de 30 texturas en una pieza).
- La colección del estadio importado guarda `pp_game_folder` (la carpeta del juego instalado) y `pp_project_bin`.
- Marco del juego en Blender: metros, Z arriba, plato en el origen, segunda base hacia +Y (`importer.TO_BLENDER` / `FROM_BLENDER` pasan a decímetros con Y arriba).

## Cómo funciona (decidido)

**Blender:**
- Botón «Ver en el juego» (operador nuevo en `blender_ui.py`), visible cuando la escena tiene un estadio importado.
- Exporta como **estadio nuevo** a una carpeta de vista previa propia, con un código fijo reservado para la vista previa, y con los mismos avisos que la exportación normal (si falla, muestra el error y no abre el juego).
- Abre `WiiCompiled.exe` de `pp_game_folder` en modo visor, pasándole por entorno el estadio a mostrar, la carpeta del mod de vista previa y la cámara inicial (posición y orientación de la vista 3D activa, convertidas al marco del juego).
- Si hay un visor abierto lanzado por Blender, lo cierra antes de abrir el nuevo.

**Runtime, modo visor:**
- Se apoya en el camino del benchmark (escenas, preparación del partido, estadio nuevo forzado) para que el estadio se cargue exactamente como en un partido.
- Activa el mod de vista previa **solo en esa ejecución**, sin escribir `Config.toml` (los demás mods del usuario no se cargan en la vista previa).
- Acepta solo la pantalla de la correa y salta lo demás, para llegar en ~8 s.
- Cámara libre con controles al estilo de Blender/Unreal: clic derecho + ratón para mirar, WASD para moverse, Q/E para bajar y subir, rueda para la velocidad, Shift para ir más rápido. Teclas para las vistas del partido.
- Oculta jugadores, pelota, marcador y HUD, y deja el juego quieto. Si ocultar a los jugadores resulta difícil, la primera versión puede mostrarlos; el resto sigue.
- Sin el modo visor activo, nada cambia.

**Fase 2, preparar sin hacer:** dejar la forma de pasar el estadio al visor (carpeta y código de vista previa, proceso lanzado desde Blender) de manera que más adelante se pueda sustituir "cerrar y reabrir" por "avisar al visor abierto y recargar". Las pistas para la recarga: alternar entre dos copias del estadio para que el juego crea que es otro, o liberar el estadio cargado antes de volver a la escena 409; y que la capa de DVD sirva los archivos nuevos (hoy `mods` compone `project.bin` al arrancar).

## Decisiones tomadas en la revisión (del asistente, no del usuario)

- **Estadios de reemplazo:** la vista previa siempre carga el estadio como estadio nuevo basado en el importado, aunque el usuario piense exportarlo como reemplazo de otro. La geometría y las texturas que se ven son las mismas; solo cambiarían datos de juego del estadio reemplazado (medidas, partidas), que no son visuales. Recordar en la colección las opciones de exportación y montar un segundo camino de "reemplazo" queda fuera.
- **Copia de `project.bin`:** `mods::Apply` compone un `project.bin` completo (~406 MB) en `ApplicationDataDirectory/Cache/mods/disc/` y lo rehace cuando cambian los packs (medido: ~0,8 s, cabe en los ~8 s). La vista previa usa **su propia carpeta de caché**, para no invalidar la copia de las partidas normales ni chocar con un juego normal abierto a la vez; cuesta ~400 MB más de disco. Servir los packs sin copia es trabajo de la fase 2.
- **Qué se para:** solo la lógica del partido (jugadores y pelota). El juego invitado sigue corriendo para que el estadio siga animado.

## Restricciones

- Nada de datos del juego en el repositorio (`tools/check_no_game_data.py`). La vista previa escribe en carpetas locales del usuario, nunca en el repo.
- Cambios del runtime como parches reproducibles sobre el Wiicompiled fijado; cada parche aplica limpio sobre la pila completa.
- Interfaz en español e inglés (`T(...)` en el complemento; el runtime ya tiene `Tr(...)` en el launcher); documentación en `docs/` y su espejo `docs/en/`.
- El complemento sigue funcionando sin el recomp (la vista previa solo necesita `pp_game_folder` con un `WiiCompiled.exe`); si no lo encuentra, lo dice.
- Pruebas: las de Blender sin interfaz (`tools/blender/test_*.py`) y las del runtime (`runtime/tests`) siguen pasando; la lógica nueva no trivial deja su prueba.

## Lo que queda para quien lo construya

- Localizar la cámara del juego (estructura y momento en que se escribe), la forma de controlarla y cómo pedirle sus vistas reales de partido; también cómo ocultar jugadores, pelota y HUD.
- Nombres de variables de entorno, código reservado y carpeta de la vista previa, y el formato con que se pasa la cámara inicial.
- Cómo activa el runtime un mod extra solo para una ejecución.
- Las teclas exactas de las vistas del partido y la velocidad por defecto de la cámara.
- Cómo encuentra Blender el visor abierto para cerrarlo.
- Si conviene un parche nuevo para el visor o extender `match_benchmark`.
