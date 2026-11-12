# Mods

*English version: [en/mods.md](en/mods.md)*

**¿Primera vez?** La [guía para crear mods](guia-mods.md) lo explica paso a paso; esto es la referencia.

Los mods se activan en el launcher: **MODS** en el menú principal (con el arte MoonInning) o la pestaña **MODS** de Ajustes (con el estilo dibujado). Cada cambio se guarda en `Config.toml` (`[mods] enabled = [...]`) y se aplica al pulsar JUGAR, porque el juego relee los mods al arrancar.

## Dónde van

En la carpeta `mods` junto al ejecutable (`<instalación>\Game\mods`). La fila **ABRIR CARPETA DE MODS** la abre y deja un `LEEME.txt`. Cada mod es una carpeta:

```
mods\
  UniformesHD\
    mod.toml
    textures\      texturas de reemplazo
    disc\          archivos que reemplazan los del disco
    riivolution\   parches Riivolution (.xml)
    project\       paquetes de project.bin (.pack/.dat/.arc) con su nombre
    roster\        un roster de PowerUp / PowerUp+ (.dat)
    schedule\      calendarios del modo Temporada (<año>.csv)
    callnames\     nombres nuevos para los comentaristas (callnames.toml + WAV o MP3)
```

`mod.toml` es opcional; sin él se usa el nombre de la carpeta:

```toml
name = "Uniformes HD"
author = "Tu nombre"
version = "1.0"
description = "Uniformes redibujados a 4x."
```

## Tipos de contenido

- **`textures/`**: reemplazo de texturas con los nombres de Dolphin, `tex1_<ancho>x<alto>_<hash>[_<hash de paleta>]_<formato>.png` o `.dds`. Pueden tener más resolución que la original.
  - **La forma recomendada es Power Pros Wii 2008 Texture Studio** (`docs/texture-studio.md`): enseña todas las texturas, las exporta a PNG y guarda tus versiones editadas o en HD con el nombre correcto en el `textures/` de tu mod.
  - A mano: para obtener los nombres, pon en `UserData\Config.toml`, bajo `[video]`, `texture_replacements = true` y `texture_dumps = true`. Juega hasta la pantalla que quieras cambiar: las texturas del juego se exportan a `UserData\Cache\texture_dumps`.
  - Edita la que quieras y guárdala en el `textures/` de tu mod con el mismo nombre.
  - Muchas texturas son atlas, es decir, una imagen con varias piezas; solo se ven las zonas que el juego usa.
- **`disc/`**: archivos con la misma ruta que dentro de la carpeta `files` del disco. Por ejemplo, `disc/sound/SeData.bin` reemplaza `files/sound/SeData.bin` y `disc/project.bin` reemplaza el archivo de contenido (ver abajo).
- **`project/`**: entradas de `project.bin` con su nombre exacto, por ejemplo `project/PWM_mdl_uniform_MLB_BOS_00.pack`.
  - Al arrancar, el juego genera una copia de `project.bin` con esos archivos en `UserData\Cache\mods\disc\project.bin` y la usa en lugar de la del disco.
  - La copia se reutiliza mientras no cambien los mods ni el disco. Generarla tarda menos de un segundo en un SSD y ocupa 406 MB.
  - Si dos mods traen el mismo archivo, gana el primero en orden alfabético.
  - Es la forma correcta de repartir cambios de `project.bin`: el mod solo lleva sus `.pack`, nunca el archivo completo.
- **`roster/`**: un roster de [PowerUp / PowerUp+](https://github.com/ogura-connection/PowerUpPlus) (el `.dat` de MLB Power Pros 2007). El juego lo convierte al formato de 2008 al arrancar y vale para todos los modos, **MLB Life incluido**. Detalles en `docs/roster-2008.md`.
- **`schedule/`**: calendarios de partidos del modo Temporada, uno por año: `schedule/2026.csv` sustituye los partidos de la temporada que el juego llama 2026 cuando el juego monta el calendario de ese año, es decir, al empezar la partida (el primer año) o al pasar de año. Una temporada ya empezada no cambia.
  - Formato: una línea por partido, `fecha,local,visitante,hora`. La fecha es `AAAA-MM-DD` (o `MM-DD`), los equipos van por código (`BOS`, `NYY`, `LAD`…; valen `MIA`/`FLA`, `ATH`/`OAK`, `TBR`, `CHW`, `WSN`…) y la hora es la de inicio en hora del Este, `HH:MM` (si falta, 19:05). Se ignoran las líneas vacías, las que empiezan por `#` y la cabecera `date,...`.
  - Límites del juego: del 1 de marzo al 31 de octubre y 16 partidos por día; lo que no cabe se descarta y el registro (`console.log`) lo dice. La temporada regular del juego va del 19 de marzo al 1 de octubre. El All-Star del juego se queda en su día.
  - El calendario real de un año se descarga desde el launcher (HERRAMIENTAS > EDITOR DE TEMPORADA, `docs/season-studio.md`, de 2002 a 2026) o con `python tools/season_schedule.py mlb 2026 mimod/schedule/2026.csv`. Si no cabe en la temporada del juego (19/3 a 1/10, 16 partidos por día), el juego lo coloca unos días antes o después sin perder partidos. Para jugar el calendario de 2026 en el primer año, guárdalo como `schedule/2008.csv`, o mejor haz un paquete de temporada (abajo).
  - El calendario no cambia las divisiones: para que Houston cuente en la AL West hace falta `[divisions]` en un paquete de temporada (abajo). El DH en las dos ligas es aparte (`dh = "both"` o la opción del launcher). Detalles en `docs/ingenieria-inversa.md`, «Calendario de partidos».
- **`callnames/`**: nombres de jugador nuevos que dicen los comentaristas y que salen en la lista de voces del editor (Arrange › VOICE SETUP) y en el selector de voz de PowerUp+. Detalles en [Callnames](#callnames).
- **`riivolution/`**: XML de Riivolution (`<wiidisc>`) válidos para `RL8E54`. La carpeta del mod hace de raíz de la SD.
  - Los parches de archivos funcionan.
  - Los parches de **memoria** solo sirven para datos: el código del juego ya está traducido a nativo, así que cambiar instrucciones PowerPC en memoria no tiene efecto. Lo mismo pasa con los códigos Gecko/Action Replay que modifican código.

Si dos mods activos reemplazan la misma textura, gana la de `UserData\texture_replacements` y luego el primer mod en orden alfabético.

## Callnames

Lo más fácil es el [Editor de callnames](callname-studio.md): crea y edita varios nombres con tu audio, sin escribir TOML.

Cada nombre lleva tres audios: el apellido en tono normal y emocionado (comentarista principal) y el nombre completo (comentarista de fondo). El audio lo pones tú: grabado, TTS, etc. Comparte solo audio que tengas derecho a compartir.

```
mods\
  MisCallnames\
    mod.toml
    callnames\
      callnames.toml
      acuna.wav
      acuna_hype.wav
      acuna_full.wav
```

```toml
[[callname]]
voice = 7300                 # número de voz, de 7223 a 10239; único entre tus mods
list = "Acuna Jr."           # texto de la lista del editor, hasta 15 caracteres
normal = "acuna.wav"         # apellido, tono normal (obligatorio)
excited = "acuna_hype.wav"   # apellido emocionado (opcional: si falta, se usa normal)
full = "acuna_full.wav"      # nombre completo (opcional: si falta, se usa normal)
```

- **WAV o MP3:** WAV PCM de 16 bits o MP3, mono o estéreo, de 8.000 a 192.000 Hz y hasta 5 segundos por variante. Se pueden mezclar extensiones en `normal`, `excited` y `full` (por ejemplo, `full = "acuna_full.mp3"`). El juego los pasa a su formato al arrancar, iguala el volumen al de los nombres del juego y lo guarda en `UserData\Cache\mods\callnames`: el primer arranque con un nombre nuevo tarda un poco más.
- **Texto:** el juego no tiene tildes ni Ñ: «Acuña» se ve «Acuna». Lo que no puede mostrar se quita y el registro (`console.log`) lo dice. En la búsqueda por nombre (Search Type) sale en mayúsculas, sin espacios.
- **Asignarlo a un jugador:** en el juego, Arrange › un equipo › VOICE SETUP › el jugador › Select Type › la inicial. En PowerUp+ (MODS › EDITAR ROSTER), en el selector de voz sale como «Callname» y el texto de `list`; al exportar el roster, el jugador lleva el número de voz del callname.
- Las voces se conservan como ids directos de 14 bits (por ejemplo, 7300) al guardar desde PowerUp+. Si un audio falla o no cabe en el banco 14, el nombre no se añade al editor del juego. Un reemplazo completo de un banco por `disc/` o Riivolution desactiva su extensión de callnames.
- **Un roster que use un callname necesita el mod de callnames activo.** Sin él, ese jugador queda sin nombre (silencio), como con una voz vacía.
- Si dos mods activos usan el mismo número de voz, gana el primero en orden alfabético y el registro lo dice.
- Vale en todos los modos que usan comentario, MLB Life incluido.
- **En Switch:** copia la carpeta del mod tal cual a `sdmc:/switch/mlbpowerpros2008/mods/<Mod>/` (junto al `.nro`) y actívalo en el launcher de la Switch (MODS); se aplica en el siguiente arranque. Funciona igual con `Data/` que con la ISO directa (`game.iso`). Los audios convertidos se guardan en `sdmc:/switch/mlbpowerpros2008/UserData/Cache/mods/callnames`. La conversión ocurre antes de la primera imagen: el primer arranque con audio nuevo tarda algo más en salir de la pantalla negra y los siguientes no. El mod y el roster se preparan en el PC (Editor de callnames, PowerUp+) y se copian a la SD.
- Cómo funciona por dentro: `docs/ingenieria-inversa.md`, «Bancos de voz y nombres de jugador».

## `project.bin`

Casi todo el contenido de MLB (modelos, uniformes, menús, estadios, datos) está en `files/project.bin`, un archivo propio de 406 MB con 2.424 entradas (`.pack`, `.dat`, `.arc`). `tools/projectbin.py` lo lee y lo reconstruye:

```
python tools/projectbin.py list    <project.bin>                      # posición, tamaño y ruta de cada entrada
python tools/projectbin.py extract <project.bin> <carpeta> [filtro]   # p. ej. filtro "uniform_MLB_BOS"
python tools/projectbin.py build   <project.bin> <reemplazos> <salida>
```

`build` copia el original y sustituye las entradas cuyo nombre (p. ej. `PWM_mdl_uniform_MLB_CHC_00.pack`) esté en la carpeta de reemplazos, recalculando tamaños y posiciones. Sin reemplazos reproduce el archivo byte a byte. Para jugar no hace falta: basta con poner los `.pack` en `project/` del mod, y el juego hace lo mismo al arrancar (con resultado idéntico al de `build`). `extract` sirve para sacar el original y editarlo.

Para saber qué entrada usa cada pantalla, ejecuta el juego con `WIICOMPILED_DEBUG_DVD=1`: el registro muestra cada lectura de `project.bin` con su posición, que `projectbin.py list` traduce a nombre.

Formato (verificado en las 2.424 entradas): cabecera con versión 2 y sector 0x800; registros de 0x1C bytes en 0x6000 (nombre, sectores, alineación, tamaño); tabla de orden en 0x26000 (registro, sector de inicio); nombres en 0x2B000; datos desde 0x3D800. El formato interno de los `.pack` está en `docs/ingenieria-inversa.md`, sección 5.

Un mod no debe distribuir un `project.bin` completo (es contenido del juego): solo sus `.pack` en `project/`.

### Texturas dentro de un `.pack`

`tools/packwii.py` saca las texturas de un `.pack` a PNG y vuelve a meter las que edites. Necesita `pip install numpy pillow`.

```
python tools/projectbin.py extract <project.bin> pack env_mainmenu
python tools/packwii.py info   pack/env_mainmenu.pack                 # texturas, formato y paleta
python tools/packwii.py export pack/env_mainmenu.pack png             # un PNG por textura + textures.json
#   edita los PNG que quieras, sin cambiar su tamaño
python tools/packwii.py import pack/env_mainmenu.pack png mimod/project/env_mainmenu.pack
```

- Cada PNG se llama `<posición>_<ancho>x<alto>.png`. `import` solo vuelve a codificar los que cambiaron. Si no cambia ninguno, el `.pack` sale idéntico byte a byte.
- **Formatos:** RGBA8, RGB5A3, I4, I8, C4 y C8.
- **Paletas (C4/C8):**
  - Si la paleta es solo de esa textura, se recalcula con los colores nuevos.
  - Si la comparten varias texturas, o la paleta se encontró solo por su posición en el archivo, los colores no cambian: el juego puede dibujar la textura con otra paleta. Los píxeles editados toman el color más parecido y los demás conservan su índice exacto.
- **Texturas que no se pueden importar:**
  - Unas 400 tienen la paleta en otro pack: salen en gris y `import` las rechaza.
- **Comprobado en el juego:** un recuadro pintado sobre «Exhibition» en `env_mainmenu.pack` aparece en el menú principal y el resto de botones no cambia.

No subas ni compartas los PNG exportados ni los `.pack` del juego. Un mod reparte solo los `.pack` que modificó, igual que con los uniformes.

## Estadios nuevos

Un mod puede **añadir** estadios: aparecen como entradas nuevas en la elección de estadio de Exhibición, después de los del juego, y no quitan ninguno.

```toml
[[stadium]]
code = "truist"          # sus archivos: project/stadium_truist.pack y project/stadium_truisttbl.pack
name = "Truist Park"
base = "atl"             # estadio cuyos datos de juego usa (medidas, menú, partidas)
```

- La forma fácil de hacerlos es el complemento de Blender, opción «Estadio nuevo» al exportar (`docs/stadiums.md`).
- `project/env_stadium_menu_<código>.pack` es opcional: una copia del pack de menú del estadio base (`env_stadium_menuNN.pack`, NN = número del estadio) con otra imagen. Sin él se ve la imagen del estadio base.
- El juego guarda en partidas y configuración el número del estadio base, así que desactivar el mod nunca rompe nada: ese partido vuelve al estadio base.
- Por ahora solo en Exhibición.
- El rótulo con el nombre sale de `project/env_stadium_name_<código>.pack` si el mod lo trae (formato en `docs/ingenieria-inversa.md`). Si no, el runtime lo dibuja con el `name` del mod y una fuente del sistema.
- Los archivos nuevos reciben números libres de `project.bin` (desde 4388), que el juego no usa.

## Paquetes de temporada

Un mod que trae un roster y los calendarios de unos años puede ser un **paquete de temporada**: con `start_year` en su `mod.toml`, una temporada nueva empieza en ese año. Estas claves (`start_year`, `roster_year`, `dh`, `[divisions]` 15/15) también se cambian desde el launcher, en HERRAMIENTAS > EDITOR DE TEMPORADA, `docs/season-studio.md`, que además muestra qué usará la próxima partida nueva y avisa si dos mods chocan.

```toml
name = "Temporada 2026"
start_year = 2026
roster_year = 2023 # opcional: el año para el que está hecho el roster (si no, start_year)
dh = "both"      # opcional: DH en las dos ligas, como desde 2022 ("original": solo en la Americana, aunque el launcher diga Sí)

[divisions]      # opcional: solo las divisiones que cambian
al_west = ["HOU", "LAA", "OAK", "SEA", "TEX"]
```

```
mods\
  Temporada2026\
    mod.toml
    roster\      el roster de 2026 (.dat de PowerUp+)
    schedule\    2026.csv, 2027.csv…
```

- Al empezar una temporada nueva con el paquete activo, el primer año es 2026: la fecha, los días de la semana, el calendario (Y) y la lista de partidas lo dicen, y `schedule/2026.csv` es el que se usa. Los años siguientes son 2027, 2028…
- **Edades:** el roster guarda la edad de cada jugador para la temporada en que se hizo (PowerUp calcula el año de nacimiento a partir de la edad). Con `roster_year` se dice qué temporada es: un roster de 2023 usado con `start_year = 2026` lleva `roster_year = 2023`, y en 2026 cada jugador tiene 3 años más que en el roster, su edad real. Sin `roster_year` se toma `start_year` (las edades del roster tal cual). Los jugadores que crea el juego después (draft) ya usan el año que se ve.
- **Familia en MLB Life:** la esposa y los hijos se crean con las fechas del juego original (2007); con el paquete se mueven al año que se ve y conservan la edad con que se crean.
- **DH:** con `dh = "both"` todos los partidos del modo Temporada llevan bateador designado, también en los parques de la Nacional (es la opción DH EN AMBAS LIGAS del launcher, encendida por el paquete). Con `dh = "original"` el DH queda como en el juego, solo en los parques de la Americana, aunque esa opción esté en Sí. Con `leagues = "original"` las ligas quedan las de 2008 aunque LIGAS 15 Y 15 esté en Sí. Si varios mods activos lo dicen, manda el primero por nombre.
- **Divisiones:** `[divisions]` con `al_east`, `al_central`, `al_west`, `nl_east`, `nl_central` y `nl_west` (listas de códigos de equipo). Solo hace falta poner las que cambian: un equipo que aparece en una lista se mueve a esa división y los demás se quedan donde estaban. Con el ejemplo, Houston pasa a la AL West y las ligas quedan 15 y 15, como desde 2013. Se aplica al empezar una temporada nueva: clasificaciones, pantallas y liga de cada equipo (también para el DH) siguen las divisiones nuevas. Cada división admite de 1 a 7 equipos; si algo no cuadra (un código desconocido o repetido), el juego usa las de siempre y el registro lo dice.
  - Con divisiones cambiadas el juego no usa su calendario oficial de 2008 y genera uno. Su generador está pensado para ligas pares; con 15 y 15 el runtime usa uno propio (162 partidos por equipo, ver «Ligas 15 y 15» en `docs/launcher-controles.md`). Para el calendario real, el paquete trae `schedule/<año>.csv`.
  - Sin paquete, la opción **LIGAS 15 Y 15** de MEJORAS hace lo mismo con Houston.
- **Lo que no cambia:** la economía del juego (salarios, impuesto de lujo) sigue la de sus temporadas 1, 2, 3…, como en 2008. La postemporada sigue siendo de 8 equipos.
- El año inicial se fija al crear la partida y va dentro de ella: una partida de 2026 sigue en 2026 aunque luego se desactive el paquete, y las partidas sin paquete no cambian.
- Con varios paquetes activos manda el primero, por nombre, que tenga `start_year`.
- **MLB Life también:** una carrera nueva de MLB Life con el paquete activo empieza en la temporada baja anterior (noviembre de 2025 para `start_year = 2026`), con las edades del roster, las divisiones del paquete y `schedule/2026.csv` para su primera temporada. El año va dentro de su partida (`pm3usmy*.dat`) como en Temporada.
- Texturas, uniformes y estadios mejor en mods aparte: se combinan con el paquete al activarlos a la vez.

## Partidas por roster

Una carrera de MLB Life, una temporada o una liga empezadas con un roster modificado solo tienen sentido con ese roster. Por eso los mods que cambian el roster tienen partidas propias. Cuentan como tales los que traen `roster/` o un `game_data.pack` en `project/`.
- Con uno o más de ellos activos, la carpeta de partidas del juego (`title/00010000/524c3845/data` de la NAND) se lee y escribe en `<datos del usuario>\ModSaves\<mod+mod...>`. Cada combinación tiene la suya.
- Sin ellos, vuelven las partidas normales.
- Los mods de texturas, uniformes (`project/` sin `game_data.pack`), `disc/` o Riivolution usan las partidas normales.
- Si un mod Riivolution define su propio `<savegame>`, ese tiene prioridad.
- Una combinación nueva empieza sin partidas.
- **En el launcher** (MODS), la primera fila, **PARTIDAS EN USO**, dice qué partidas usará el juego («Normales» o los mods de roster activos) y al pulsarla abre su carpeta. Al activar o desactivar un mod de roster, avisa de que ese conjunto tiene sus propias partidas y de que las otras siguen guardadas. Parche `wiicompiled-launcher-saves`. Un jugador creyó que el juego no guardaba porque, al cambiar de mods, sus partidas «desaparecían».
- Las partidas jugadas con un roster antes de este cambio quedaron en las normales.
- Parche `wiicompiled-mod-saves`. Usa el mismo redireccionador que el `<savegame>` de Riivolution, pero toma la carpeta de datos del título de disco que aparece en la ruta: el código de juego en memoria baja no dura una vez que el juego corre, y `CurrentNandDataDir` vuelve a `RMCP`.
- Verificado:
  - con Roster2025 activo, el juego busca `pm3usma.dat` en `ModSaves\Roster2025\`;
  - con solo uniformes y texturas, en la NAND normal.
  - La creación de una partida dentro de esa carpeta no se probó todavía.

## Marcas y logotipos propios (Arrange)

Imágenes PNG propias como marca (escudo) y logotipo de los equipos de Arrange. No es un mod: no va en `mods\`.

- **Activación:** opción **MARCAS Y LOGOTIPOS PROPIOS (ARRANGE)** en la pestaña MEJORAS del launcher. Está apagada por defecto y marcada EXPERIMENTAL. En `Config.toml` es `[arrange] custom_marks = true`.
- **Carpetas** (el launcher las crea al encender la opción):

```
UserData\arrange\
  marks\   marcas: salen al final de la lista Team Mark
  logos\   logotipos: salen en una fila nueva de Logo Text
```

  En Switch son `sdmc:/switch/mlbpowerpros2008/UserData/arrange/marks` y `.../arrange/logos`.

**Cómo se usa**
- Copia los PNG en `marks` o `logos`. Valen con transparencia y de cualquier tamaño dentro de los límites; el juego los encaja sin deformarlos, con relleno transparente.
- **Marca:** Arrange › un equipo › Team Name/Mark › MARK. Los PNG salen después de las 38 marcas del juego, con el nombre del archivo. Elige uno y confirma. Si el PNG ya no se puede usar (se ha cambiado o estropeado desde que se abrió la lista), suena el aviso y la lista sigue abierta.
- **Logotipo:** Arrange › un equipo › Logo Text. La fila nueva está entre LETTERS y DELETE. Izquierda/derecha recorre «- - -» y los logotipos, TEAM LOGO muestra la vista previa y OK lo guarda. La fila solo sale si hay logotipos válidos o si el equipo ya tenía uno.
- La elección se guarda con la partida (`pm3usma.dat`), en una extensión que el juego original no lee. Cada elección va con una huella del equipo: su nombre, su inicial y su equipo base.
- Se ve en la lista de equipos de Arrange, en las pantallas del equipo, en la cuadrícula y el VS de Exhibición, en HOME/ROAD, en «X AT Y», en la alineación titular y en el marcador.

**Comportamiento**
- Una marca PNG oculta la inicial, la letra que el juego pone sobre la marca.
- Un logotipo PNG sustituye a las letras. Las letras del juego se conservan como respaldo. Teclear letras nuevas en Logo Text y confirmarlas quita el logotipo PNG.
- Si el PNG se borra, se renombra o no es válido, el equipo vuelve a la marca o a las letras del juego que tenía.
- COPY copia también la marca y el logotipo propios al hueco de destino. DELETE los borra.
- Cambiar el nombre, la inicial, el equipo base, los colores o el uniforme de un equipo con la opción encendida conserva su marca y su logotipo: al guardar, la huella se actualiza. «Reset to Base Team» y «Save as Arrange» sí quitan la marca y el logotipo propios del hueco que cambian.
- Con la opción apagada, el juego es el original y no lee ni escribe la extensión. Guardar no la borra: al volver a encender la opción, las elecciones siguen ahí.
- Salvo las de los equipos que cambiaron mientras la opción estaba apagada (en otra sesión, en Dolphin o en una Wii): si se borró el equipo, se creó otro en su hueco o se le cambió el nombre, la inicial o el equipo base, la huella ya no coincide y ese equipo pierde su marca y su logotipo propios. Vuelve a lo del juego y se pueden elegir de nuevo. Cambiar con la opción apagada la marca del juego, los colores, las letras o el uniforme no cuenta como otro equipo. Límite: la huella solo mira el nombre, la inicial y el equipo base, así que un equipo nuevo creado con la opción apagada con esos tres datos iguales (por ejemplo, el nombre por defecto y el mismo equipo base) se toma por el mismo y conserva la elección.
- Una extensión que esta versión no entiende (de una versión posterior, o dañada) se ignora y no se borra al guardar, hasta que se elige una marca o un logotipo en esa sesión.

**Límites**
- Se listan los 64 primeros `.png` de cada carpeta por orden de nombre (sin distinguir mayúsculas; los de más de 8 MiB no cuentan). Los que luego se descartan, por nombre largo o por no ser un PNG válido, también ocupan sitio: con más de 64 archivos, alguno válido puede quedarse fuera.
- Los nombres de archivo de más de 31 bytes en UTF-8 no salen en la lista: acórtalos.
- Archivos de 8 MiB y 4096×4096 píxeles como máximo.
- Los archivos truncados, los que no son PNG y los que tienen datos después del final del PNG (el bloque IEND) se ignoran, sin aviso en pantalla.
- El nombre sale en mayúsculas, solo con letras de la fuente del juego y recortado a 10 caracteres visibles. Si dos nombres coinciden, el segundo lleva un número.

**Pendiente**
- Temporada y Liga copian el equipo a su propia tabla: allí se ven la marca y la inicial del juego.
- Home Run no se ha probado.
- La tecla Z (subrayado) de Logo Text no cambia nada visible con un logotipo PNG.
- La primera vez que se ve un PNG grande en la vista previa puede haber una pausa mientras se decodifica, sobre todo en Switch. Las siguientes van de la caché.
- En 16:9, las unidades 2D del juego no son cuadradas. Una marca o un logotipo se ven unas 1,5 veces más anchos que el archivo, igual que las marcas del juego.
- Falta la prueba en una Switch.

Parche `wiicompiled-arrange-marks`. Detalles técnicos en `docs/ingenieria-inversa.md` («Marcas y logotipos propios»).

## Uniformes importados (Arrange)

Uniformes propios para los equipos de Arrange, hechos con el editor Uniform Studio. No es un mod: no va en `mods\`.

- **Siempre activo:** forma parte del juego, no es una mejora opcional (no hay interruptor en MEJORAS; `[arrange] custom_uniforms` ya no se lee). El editor se abre desde el launcher: HERRAMIENTAS > EDITOR DE UNIFORMES.
- **Carpeta** (el launcher la crea al encender la opción): `UserData\arrange\uniforms\<nombre>\uniform.ppu`. En Switch es `sdmc:/switch/mlbpowerpros2008/UserData/arrange/uniforms/<nombre>/uniform.ppu`.
- **Editor:** **Uniform Studio** (`UniformStudio.exe`, herramienta de PC; se abre desde HERRAMIENTAS > EDITOR DE UNIFORMES del launcher). Es sencillo: eliges el uniforme, colocas logos y texto, ajustas los colores y guardas el `.ppu` en la carpeta anterior. El Setup lo instala junto al juego con todo lo que necesita (también su propio Python, en `uniform-studio\`): no hay que instalar nada más.
- **Qué se importa:** logos, texto y colores. De los `.iff` de MLB 2K y los `.fsh` de MVP Baseball se pueden sacar logos y otros elementos para colocarlos en tu uniforme, pero no sirven como uniforme entero: su ropa no encaja tal cual en el molde del juego.

**Cómo se usa**
- Arrange > un equipo > ORIGINAL TEAM > UNIFORM > fila **LINE TYPE**: la ventana del juego con los tres tipos de línea sigue con flechas hacia la derecha en páginas de tres con tus uniformes. Cada uno se ve con su miniatura (o su nombre si el `.ppu` no la trae) y la barra de arriba nombra el que tiene el cursor; los muñecos HOME y ROAD lo enseñan mientras mueves. A lo elige, B deshace y OK de la pantalla lo guarda. Elegir uno de los tres tipos del juego vuelve al uniforme del juego. Se elige por lado (R cambia HOME/ROAD).
- **La ventana del Studio:** arriba, Nuevo, Abrir (tus uniformes guardados, con su miniatura), el nombre, Guardar (con «*» si hay cambios sin guardar), Deshacer y Rehacer. A la izquierda, cuatro pestañas: Colores, Logos y texto (las capas de la zona), Número y Dibujo. En el centro, las zonas como botones, la barra de la capa (Centrar, Ajustar a la zona, Duplicar, Subir, Bajar, Borrar) y el molde. A la derecha, el muñeco y la capa seleccionada. Atajos: Ctrl+Z / Ctrl+Y deshacer y rehacer, Ctrl+S guardar, Ctrl+D duplicar, Supr borrar. Guardar deja además el proyecto editable en `UserData\arrange\uniforms\<nombre>\studio\` (tus capas y elecciones), para volver a abrirlo con Abrir. Los uniformes guardados antes también salen en Abrir (marcados «antiguo»): se abren con cada parte como una sola imagen de base, sobre la que puedes seguir añadiendo logos y texto.
- **Cualquier color:** al pulsar una pieza (GORRA, CAMISETA…), debajo de los 32 colores del juego está «Color libre»: rueda, valores RGB y código hex. La pieza toma ese color con sus tonos claro, medio y oscuro (calculados con la proporción media de los colores del juego). El uniforme lo lleva tal cual porque se guarda como imagen; para el respaldo del registro se guarda el color del juego más cercano (se ve debajo de la rueda). El NÚMERO y su BORDE solo admiten los 32 colores del juego, porque el número lo pinta el juego con su paleta.
- **Dibujo y patrones de líneas:** la pestaña Dibujo convierte el molde en un lienzo. Encima del molde: las herramientas (Pincel B, Línea L, Rectángulo R, Rect. lleno Shift+R, Rellenar F —rellena la mancha del mismo color que ves—, Borrador E y Cuentagotas I), el color en uso, el grosor (también la rueda o [ y ]), «Solo en la zona» (lo pintado no se sale de la zona elegida arriba: pecho, pantalón…) y el zoom (Ctrl + rueda). A la izquierda, el color (rueda, RGB, hex, colores del uniforme y los 32 del juego) y la capa de dibujo. A la derecha, «Patrón de líneas»: rayas verticales, horizontales, diagonales o cuadros, con grosor, separación y desplazamiento, y «Vista previa» para verlo en el molde y en el muñeco antes de aplicarlo. Todo va a una capa «Dibujo» de la parte, que sale en Logos y texto (se puede ocultar o borrar), se deshace con Ctrl+Z y se guarda en el proyecto.
- **Crear desde cero, por piezas:** en Uniform Studio, «Crear desde cero: colores por pieza» pinta el uniforme como el editor de Arrange del juego: GORRA, VISERA, MANGAS / ETC, CAMISETA, PANTALÓN, LÍNEA y TIPO DE LÍNEA, cada pieza con uno de los 32 colores del juego (COLOR NO. 1–32). Encima van tus logos y textos. El número y el color de la marca se siguen eligiendo en el juego (UNIFORM #, # OUTLINE, COLOR OF MARK).
- **Zonas y logos:** «Zona del uniforme» elige dónde trabajas: Pecho (frente), Espalda, Mangas / etc., Pantalón, Gorra, Visera, Todo el uniforme, Letra de la gorra, Casco y Número. Las zonas del cuerpo salen de tu juego (qué pieza pinta cada píxel del atlas y qué lado mira al frente en el muñeco). El molde hace zoom a la zona y oscurece lo de fuera, y lo que importas o escribes entra centrado en ella. Sobre el molde: arrastrar mueve, las asas de las esquinas cambian el tamaño, las de los lados estiran a lo ancho o a lo alto, la rueda cambia el tamaño y las flechas mueven píxel a píxel (Mayús: 5). El círculo sobre la caja gira la imagen (Mayús: de 15 en 15). En «Capa seleccionada» están también el tamaño, el ancho y el alto, el giro (−90 / +90 / 0°), X / Y, «Forma original» y «Ajustar a la zona». Achicar o aplastar una imagen no la estropea: el Studio guarda la original y cada cambio sale de ella, promediando lo que cae en cada píxel. El límite es el del juego: todo el cuerpo es un atlas de 128 × 128 píxeles con 256 colores de 15 bits, así que un logo en el pecho mide unos 20–30 píxeles.
- **Rayas del Studio (en «Colores»):** bajo «Tipo de línea», «Rayas del Studio» pinta rayas Finas, Dobles o Trenzadas sobre la camiseta (y el pantalón, si quieres), con sus propios colores y separación. Se suman al tipo de línea del juego: «Ribete» con el color de LÍNEA más rayas de otro color da un borde dorado con rayas de cadena. Cambian solas al cambiar los colores de las piezas.
- **Rayas de dos colores:** en «Dibujo» → «Patrón de líneas» hay «Dobles» (dos rayas finas juntas, una de cada color) y «Trenzadas» (dos hilos de distinto color que se cruzan, como las rayas de cadena). El primer color es el del dibujo y el otro es «Segundo color». Para un ribete en cuello y mangas más rayas, elige «Ribete» en Colores (con el color de LÍNEA) y aplica el patrón encima, zona por zona.
- **Lo que se repite:** el modelo del juego usa los mismos píxeles del atlas para los dos lados del cuerpo. Lo que pongas en «Mangas / etc.» o «Pantalón» sale en las dos mangas o en las dos piernas, en espejo (un texto se lee al revés en un lado), y lo mismo pasa con los lados de la gorra. Pecho y espalda son únicos. El logo de la gorra y el del casco son una sola imagen de 64 × 32 en el frente. Las muñequeras no son del uniforme: cada jugador lleva las suyas; «Muñequeras» solo las quita o pone en la vista previa. Sin ellas, el antebrazo entero toma el color de un solo píxel del atlas (el de MANGAS / ETC): se le puede dar otro color, pero no rayas ni dibujos.
- **Número:** en las piezas, NÚMERO y BORDE DEL NÚMERO (UNIFORM # y # OUTLINE del juego) y un «Dorsal de prueba» que se ve en el muñeco (espalda y pecho, dibujado con la paleta y las tablas de posición del juego). El dorsal real lo pone el juego. Al elegir el uniforme en la ventana LINE TYPE, el juego toma esos dos colores para ese lado, como si los hubieras elegido en sus filas; B lo deshace y los puedes cambiar después en el juego. **Dónde va:** «Pecho» elige como el juego, sin número o una de las 8 posiciones del juego; «pecho X / Y» lo afina en píxeles; «dorsal X / Y» y «dorsal %» mueven y agrandan el de la espalda. Se guarda con el uniforme y el juego lo aplica a cualquier jugador que lo lleve; «Número como el juego» lo deja como estaba.
- **Molde y muñeco:** botones − / + / Ver todo, Ctrl + rueda (o la rueda sin nada seleccionado) para el zoom del molde y botón derecho para desplazarlo; «Centrar», «Centrar horizontal» y «Vertical» colocan la capa en el centro de la zona. Un clic en el muñeco (o «Ampliar») abre una ventana grande para inspeccionarlo: arrastrar gira, la rueda acerca, el botón derecho mueve, y hay vistas de frente, espalda, lados y desde arriba.
- **El muñeco del juego en la vista previa:** el Studio muestra el personaje de Power Pros con tu uniforme. Lo lee, igual que las piezas, de **tu** juego instalado (`[paths] dvd_root` de `Config.toml`) y lo guarda en `UserData\arrange\uniform\` (`game_base.bin`, `game_doll.bin`). Nada de eso va en el repositorio ni en las descargas; sin el juego, el Studio usa su maniquí propio y no ofrece las piezas.
- **Miniatura:** Uniform Studio la crea al guardar (el muñeco del juego de frente, o el maniquí propio sin juego). Los `.ppu` guardados antes del 9 de noviembre de 2026 no la traen: vuelve a guardarlos para tenerla.
- La elección se guarda con la partida (`pm3usma.dat`), en la misma extensión que las marcas, que pasa a la versión 3.
- Con la opción apagada, el juego es el original.

**Límites**
- **Resolución 128x128:** el uniforme del juego es un atlas de 128x128 píxeles. Los rótulos largos o con letra pequeña pierden nitidez y se ven pixelados.
- Falta la prueba en una Switch.

Parche `wiicompiled-arrange-uniforms`. Diseño en `docs/superpowers/specs/2026-11-07-arrange-imported-uniforms-design.md`.

## Reglas

Un mod solo debe traer sus propios archivos o diferencias, **nunca copias del juego** (ni texturas originales sin modificar, ni `project.bin`, ni el ejecutable).

## Estado

- **Verificado** (12 de octubre de 2026):
  - activar un mod en la pantalla MODS lo guarda y pide reinicio;
  - un mod con `textures/` reemplaza la pantalla de la correa del Wii Remote (`tex1_512x512_fdbf0c48ec7ed2b1_25db17d8ad4d988a_9`);
  - el registro muestra `mods: enabled ...` e `Indexed N texture replacements from 2 folder(s)`.
  - un pack de texturas de Dolphin real (logos y uniformes 2023, 206 texturas) funciona sin cambios como `textures/`;
  - un mod con solo `project/PWM_mdl_uniform_MLB_CHC_00.pack` (el de los Yankees, 6,9 KB) hace que los Cubs salgan con la gorra "NY" en la selección de equipo. El juego genera la copia en 656 ms, la reutiliza en el siguiente arranque y queda idéntica a la de `projectbin.py build` (mismo SHA-256).
  - (13 de octubre de 2026) un `env_mainmenu.pack` editado con `packwii.py` (recuadro sobre «Exhibition») se ve en el menú principal sin alterar los demás botones.
  - (14 de octubre de 2026) un mod hecho con Texture Studio (atlas del menú principal y vallas y marcador de Coors Field, ×4) se ve en el menú y durante un partido.
- **Sin probar todavía:** `riivolution/` en MLB. Usa el mismo código de Wiicompiled que ya funciona con Mario Kart Wii.
- **Hecho:** volcar texturas sin editar `Config.toml`: «Capturar en juego» de Texture Studio (launcher → HERRAMIENTAS → EDITOR DE TEXTURAS).
- **Pendiente:**
  - modelos dentro de `PACKWII` (las texturas ya se editan con Texture Studio o `packwii.py`). Los estadios ya se importan y se exportan con Blender, colisión incluida, y se pueden hacer con modelos propios (`docs/stadiums.md`);
  - mods de código en C++ compilados por el Setup.
