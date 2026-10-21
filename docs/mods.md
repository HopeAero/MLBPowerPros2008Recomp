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
  - El calendario real de un año se descarga desde el launcher (MODS > TEMPORADA, de 2002 a 2026) o con `python tools/season_schedule.py mlb 2026 mimod/schedule/2026.csv`. Si no cabe en la temporada del juego (19/3 a 1/10, 16 partidos por día), el juego lo coloca unos días antes o después sin perder partidos. Para jugar el calendario de 2026 en el primer año, guárdalo como `schedule/2008.csv`, o mejor haz un paquete de temporada (abajo).
  - El calendario no cambia las divisiones: para que Houston cuente en la AL West hace falta `[divisions]` en un paquete de temporada (abajo). El DH en las dos ligas es aparte (`dh = "both"` o la opción del launcher). Detalles en `docs/ingenieria-inversa.md`, «Calendario de partidos».
- **`riivolution/`**: XML de Riivolution (`<wiidisc>`) válidos para `RL8E54`. La carpeta del mod hace de raíz de la SD.
  - Los parches de archivos funcionan.
  - Los parches de **memoria** solo sirven para datos: el código del juego ya está traducido a nativo, así que cambiar instrucciones PowerPC en memoria no tiene efecto. Lo mismo pasa con los códigos Gecko/Action Replay que modifican código.

Si dos mods activos reemplazan la misma textura, gana la de `UserData\texture_replacements` y luego el primer mod en orden alfabético.

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

Un mod que trae un roster y los calendarios de unos años puede ser un **paquete de temporada**: con `start_year` en su `mod.toml`, una temporada nueva empieza en ese año. Estas claves (`start_year`, `roster_year`, `dh`, `[divisions]` 15/15) también se cambian desde el launcher, en MODS > TEMPORADA, que además muestra qué usará la próxima partida nueva y avisa si dos mods chocan.

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
