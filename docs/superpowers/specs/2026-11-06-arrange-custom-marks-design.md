# Marcas y logotipos propios en Arrange: diseño

Pedido del usuario el 6 de noviembre de 2026. Alcance: el creador de equipos de Arrange en PC
y Switch.

## Intención

El usuario quiere crear equipos de Arrange con escudo y logotipo propios, por ejemplo los
Leones del Caracas. Los PNG los pone él en una carpeta. Lo quiere **nativo**: que se elija
desde las pantallas del propio creador de Arrange, con los botones, letras, marcos y sonidos
del juego, como si la opción siempre hubiera estado ahí. No quiere un mod de texturas: la
prueba con un pack de texturas (`local/analysis/arrange/`) funcionó, pero cambia también un
equipo de la MLB y comparte la marca entre equipos, y el usuario la descartó.

## Qué hace

1. **Marcas propias**
   - Carpeta `UserData/arrange/marks/` con PNG del usuario.
   - En «Team Mark» (pantalla «Please set the team's mark», lista de 38 marcas en dos
     columnas: DOG, CAT… GRIM REAPER) aparecen al final las marcas de la carpeta, nombradas
     por el archivo en mayúsculas.
   - Se eligen como una marca del juego, con su vista previa en el recuadro TEAM MARK.
   - El selector de color A–D no se aplica a una marca propia: se oculta o no tiene efecto,
     a elección del implementador.
2. **Logotipos propios**
   - Carpeta `UserData/arrange/logos/`.
   - En la pantalla «Logo Text» («Please set your logo»), además de escribir letras con el
     teclado del juego, se puede elegir un logotipo de la carpeta.
   - Se muestra tal cual en el recuadro TEAM LOGO y en todos los sitios donde el juego
     muestra el logotipo del equipo.
3. **Sin inicial**
   - Un equipo con marca propia no muestra la bandera con la inicial (el icono rojo con la
     letra). En su lugar sale su marca.
   - Aplica en todos los sitios donde hoy sale esa bandera.
   - «Marca propia» significa aquí una marca **PNG importada**, no la bandera `0x02` del
     juego, que también se activa al elegir cualquiera de las 38 marcas del juego. Los equipos
     con una marca del juego siguen mostrando su inicial como siempre. Por eso, el gancho de
     la bandera consulta la elección guardada en la extensión.

Cada uno de los 32 equipos de Arrange tiene su propia elección. Varios equipos pueden usar
el mismo PNG.

**Sitios que deben mostrar marca, logotipo y bandera**, todos vistos con la prueba de texturas
(capturas en `local/analysis/arrange/shots_leones/`):
- lista de huecos de Arrange;
- pantalla del equipo en Arrange (TEAM FLAG, TEAM LOGO, MARK);
- elección de equipo de Exhibición (cuadrícula y «VS»);
- local/visitante;
- presentación del partido («X AT Y»);
- alineación titular;
- marcador del partido.

**Otros modos.** Estos siete sitios son el criterio de aceptación. Otros modos que admitan
equipos de Arrange (Liga, Temporada, Home Run…) se prueban si se puede. Lo que no funcione en
ellos se documenta como pendiente y no bloquea la entrega. Decisión mía.

## Objetivos

- Criterio de éxito:
  - el usuario crea a los Leones desde las pantallas del juego eligiendo su escudo y sus
    letras;
  - los ve en todos los sitios listados, sin la inicial;
  - los Nationals, los demás equipos MLB y los demás equipos de Arrange no cambian.
- Funciona igual en PC y en Switch (`sdmc:/switch/mlbpowerpros2008/UserData/arrange/…`).
- La elección se guarda con cada equipo, en la partida, y sobrevive a guardar y cargar.
  Copiar un equipo con COPY copia la elección; borrarlo con DELETE la borra.

## Fuera de alcance

- **Uniformes propios:** otro proyecto, después. El uniforme no cambia en este proyecto:
  - no se tocan las letras del pecho ni la inicial de la gorra del uniforme genérico de
    Arrange (`PWM_arrange_uniform`);
  - no se engancha `0x806CC2EC`.
- **Editor de habilidades, incluidas las secretas:** otro proyecto. Arrange no tiene editor
  de habilidades.
- Ventana de archivos de Windows y explorador de carpetas dentro del juego: solo la carpeta
  fija.

## No debe pasar (aunque técnicamente funcione)

- Que se note añadido. No vale una ventana ImGui con aspecto propio dentro de las pantallas
  del juego: todo con texturas, fuente y botones del juego. Regla del proyecto, memoria
  «Game textures for in-game UI».
- Que cambie un equipo de la MLB o la marca de otros equipos de Arrange que usan la misma
  marca del juego.
- Que un PNG borrado, renombrado o inválido deje un equipo roto, un cuelgue o una pantalla
  rara: el equipo vuelve a la marca o logotipo del juego que tenía.
- Que estropee partidas guardadas existentes o las haga ilegibles para el juego original.
- Que baje el rendimiento del partido en Switch, que está justo en 60 fps (ver
  `docs/traspaso.md`).

## Restricciones

- **Activación:** opción en la pestaña MEJORAS del launcher, apagada por defecto, con la
  etiqueta EXPERIMENTAL, y textos en español e inglés (memorias «Enhancements tab» y «UI
  language»). Con la opción apagada, el juego es idéntico al original.
- **Cambios del runtime:** como parches reproducibles contra la dependencia fijada
  (`tools/patches/`, en la cadena de `tools/wiicompiled-translate.ps1` y en la de Switch).
  El código generado del juego se conserva.
- **Contenido en el repositorio:** ninguno del juego ni del usuario. Ni texturas, ni volcados,
  ni los PNG de los Leones; `tools/check_no_game_data.py` lo vigila.
- **Imágenes:**
  - PNG con transparencia de cualquier tamaño;
  - se encajan sin deformar, con relleno transparente, como en la prueba;
  - el decodificador PNG que ya usa el reemplazo de texturas sirve.
- **Rama:** trabajo nuevo en una rama propia (`arrange-custom-marks`), creada desde
  `switch-arm64`, que tiene el árbol de PC y de Switch. Decisión mía.

## Lo que ya se sabe del juego

Fuente: `local/analysis/arrange/report.md` y `docs/ingenieria-inversa.md`, sección 7,
«Habilidades por ID y modo Arrange».

**Registro del equipo Arrange** (copia de trabajo en `0x8106D84E`):

| Offset | Contenido |
|---|---|
| `+0x17` | marca |
| `+0x18` | color de la marca |
| `+0x19` | color del logotipo |
| `+0x1B` | banderas: `0x02` marca propia, `0x10` logotipo propio |
| `+0x30`… | letras del logotipo |

**Recursos**
- Marcas: `env_arrange_logo01`–`38.pack`, C8 de 256×256 con 4 paletas.
- Bandera de inicial: atlas de letras `tex1_256x64_bae093b1…` (bandera y marcador) y
  `tex1_256x128_36658f00…` (campo inicial e iconos).
- Icono pequeño de la marca: 64×64 en `env_arrange_teamflag`.
- Letras del logotipo: `env_arrange_team_logo`.

**Ya hecho en el runtime y reutilizable**
- `modern_postseason.cpp` dibuja en pantallas del juego con atlas del juego encontrados en
  MEM2 por XXH64.
- El mismo módulo guarda una extensión versionada dentro de las partidas
  (`modern_postseason_save.h`, `nand_isfs.cpp`).

**Sin localizar**
- Dónde se guardan los equipos Arrange en la partida.
- El código que carga `env_arrange_logoNN` y el que compone el logotipo de letras.
- El código que dibuja la bandera con la inicial.
- La lista de marcas de «Team Mark»: tamaño fijo, desplazamiento y fuente.

## Lo que decide el implementador

- **Mecanismo:**
  - ampliar las listas del propio juego (preferible si es viable) o dibujar las entradas
    nuevas encima con atlas y fuente del juego;
  - cómo identificar qué equipo se está dibujando para sustituir su marca o logotipo.
  - El primer paso es localizar el código anterior.
- **Formato de la extensión de guardado:** nombre de archivo y huella por hueco, con versión.
- **Nombres en las listas:** recorte a lo que quepa, solo con caracteres que tenga la fuente
  del juego; orden alfabético.
- **Límite de marcas y logotipos** que se ofrecen.
- **Resolución interna** a la que se suben las imágenes.
- **Pruebas:**
  - tests de runtime (`ctest`) para la extensión de guardado y la carga de imágenes;
  - sondas con NAND aislada y capturas para las pantallas listadas;
  - en Switch, el build y una única prueba final en consola: el usuario no quiere pruebas de
    consola repetidas (memoria «Switch / ARM64»).
- **Documentación:**
  - `docs/mods.md` y su versión en inglés, con las carpetas y el uso;
  - `docs/ingenieria-inversa.md` (y EN) con lo que se descubra, y el PDF regenerado.
