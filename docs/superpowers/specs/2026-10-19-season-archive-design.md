# Modo Temporada sin tope de 10 temporadas, con «Archive» — diseño

*English version: [../../en/superpowers/specs/2026-10-19-season-archive-design.md](../../en/superpowers/specs/2026-10-19-season-archive-design.md)*

Fecha: 19 de octubre de 2026. Estado: aprobado por el usuario el 19 de octubre de 2026 («adelante»).

## Por qué

Jugadores de la comunidad piden que el modo Temporada dure más de 10 temporadas. Hoy acaba en la décima (2017). El usuario quiere que siga sin límite, sin perder los años antiguos, y que se puedan consultar dentro del juego con una pantalla que se parezca a las del propio modo Temporada.

Lo que sabemos (`docs/ingenieria-inversa.md`, «Año de la temporada»; leído en el código traducido, sin probar en el juego):
- el año es 2008 + un byte con signo en `[[0x8104B5A8] + 8]`; `0x80B4E564` le suma 1 al pasar de temporada;
- el tope son comparaciones de ese índice con 9 o 10 (`0x80BB4A40`, `0x80BF24B4`, dos en `0x80BF4BD4`, `0x80C0CB54`);
- hay al menos una tabla por año, en `[0x8104B5C4]`, de 428 bytes por año y con sitio para 10. Quitar las comparaciones sin más haría que la temporada 11 escribiera fuera de esa tabla y estropeara la partida.

## Lo que decidió el usuario

1. **Ventana de 10 años más archivo propio** (de tres opciones: ventana sola, ampliar la partida, o ventana con archivo). El juego sigue trabajando con sus 10 años; al pasar de la temporada 10 a la 11, el año más viejo se guarda en un archivo nuestro antes de descartarlo. No se pierde nada.
2. **Una pantalla propia para los años viejos**, dentro del juego, en el modo Temporada, **muy parecida a las del juego**: la misma información que MLB Data ofrece de una temporada y las mismas acciones.
3. **Se abre con una opción más del menú del modo Temporada** (Daily Progress, Practice, Management, Commands, MLB Data, System, Exit), con la misma estética. Se descartaron un botón en una esquina (no hay sitio: ahí están la fecha y los puntos del dueño) y la barra de ayudas.
4. **La opción se llama «Archive»**, en inglés. «History» no, porque el juego ya lo usa en la barra de abajo (R).
5. **Tiene que funcionar en Switch y en otros ports de consola**: todo con el mando, nada que dependa de teclado o ratón.
6. **Carreras y premios:** si All-Time/Career y Honors solo suman los 10 años de la ventana, se acepta que muestren esos 10; los totales de toda la carrera se ven en Archive. No se tocan las pantallas originales.
7. **Partidas ya terminadas:** una partida que ya llegó al final de la temporada 10 tiene que poder seguir a 2018 y sumar años, igual que una que aún no ha llegado.
8. **Calendario a partir de 2018:** se reutiliza el calendario de partidos del juego, pero con los días de la semana del año real.
9. **Sin logos de MLB ni de los equipos**: son marcas registradas aunque una imagen esté con licencia CC. Los equipos se reconocen por nombre, siglas y colores. Se pueden usar fotos CC0 o CC BY (citando al autor cuando lo pida la licencia).

## Objetivos

1. **El modo Temporada pasa de la temporada 10.** Al terminar 2017 se ofrece 2018, y así sin fin. El año en pantalla es el real (2018, 2019…), en todas las pantallas que lo muestran, con las fechas y días de la semana de ese año. Las partidas que ya acabaron la temporada 10 salen de ese estado final y siguen.
2. **Archivo de temporadas.** Antes de descartar el año más viejo, el runtime guarda todo lo que el juego conservaba de ese año en un archivo propio, junto a la partida (en la carpeta de partidas del mod activo, como `pm3usma.dat`). En Switch y otros ports va con las partidas de esa plataforma.
3. **Opción «Archive»** en el menú del modo Temporada, entre MLB Data y System, con una pastilla dibujada por nosotros imitando el estilo de las del juego (sin copiar sus imágenes).
4. **Pantalla Archive:** imita MLB Data:
   - incluye los totales de carrera de toda la historia (años archivados más los de la ventana), ya que las pestañas del juego solo cubrirán 10 años;
   - selector de año con L/R, solo para los años del archivo;
   - las secciones de MLB Data de un año: Player Stats (Season Stats, Player Records), Team Stats (Team Stats, Team Records), Schedule (Standings, Matchup Chart, Postseason) y Roster (Player List, Transactions), en la medida en que el juego guarde esos datos por año;
   - se maneja con el mando: cruceta para mover, A para entrar, B para volver, L/R para el año.
   - Si el archivo está vacío (aún no se ha pasado de 10 temporadas), la pantalla lo dice.

## No objetivos

- Ver años viejos en las pantallas originales de MLB Data (se habló de meterlos en las hojas del juego: el usuario lo vio arriesgado).
- Ampliar el formato de la partida del juego.
- Cambiar las clases del draft, la progresión, las ligas de 15 y 15, el calendario o los playoffs (otros pedidos de la comunidad, aparte).
- Cambiar las pestañas All-Time/Career y Honors del juego (decisión 6).

## Lo que haría que esto fracase aunque funcione

- Que una partida se estropee: años cruzados (datos de un año mostrados en otro), estadísticas que se pierden o una partida que no carga.
- Que una partida de 10 temporadas o menos se comporte distinto que hoy.
- Que la pantalla Archive parezca un añadido ajeno al juego.
- Que algo solo funcione con teclado o ratón.

## Restricciones

- Ningún contenido del juego en lo que publiquemos: la pastilla, las letras y los dibujos de la pantalla son nuestros (`tools/check_no_game_data.py`).
- Los cambios al juego van como reemplazos nativos de funciones en el runtime (parches reproducibles sobre Wiicompiled), no editando el código traducido.
- El archivo tiene que sobrevivir a cerrar el juego en cualquier momento, y una partida sin archivo (por ejemplo, copiada de otro PC) sigue funcionando.
- Textos de la pantalla en inglés, como el resto del juego.

## Primer paso (antes de construir)

Averiguar con una partida de varias temporadas, y en el código:
1. **todos** los datos que el juego guarda por año, dónde están y en qué formato (sin esto, la ventana deslizante deja años cruzados);
2. si All-Time/Career y Honors ya acumulan todos los años;
3. cómo está hecho el menú del modo Temporada (número de opciones, cursor, imágenes de las pastillas) y qué cuesta añadir una;
4. todos los sitios que escriben el año en pantalla.

Lo que salga puede cambiar el alcance de la pantalla (secciones que no se puedan rellenar porque el juego no guarda ese dato por año). Si es así, se vuelve a hablar con el usuario.

## Decisiones del agente (revisables)

- **Dónde vive el año real:** el número de años archivados se guarda **dentro de la partida** (en bytes libres de `pm3usma.dat` que encuentre la investigación), no solo en el archivo. Así una partida sin su archivo (copiada de otro PC) sigue mostrando el año correcto; solo la pantalla Archive sale vacía. Si no hay bytes libres seguros, se vuelve a hablar con el usuario.
- **Archivo y partida al día:** el archivo se escribe cuando el juego escribe la partida, no al pasar de temporada. Cada año archivado lleva su año real, así que repetir un año no lo duplica (gana el último escrito), y si el jugador vuelve a una partida anterior, los años del archivo que esa partida aún tiene en su ventana se ignoran (manda la partida).
- **Switch y otros ports:** es una condición del diseño (todo con el mando, rutas de partida a través del runtime), no una entrega de esta fase: hoy no hay versión de Switch que probar.

## Cambio de alcance tras la investigación (19 de octubre de 2026)

La investigación ([hallazgos](2026-10-19-season-archive-findings.md)) mostró que casi todas las tablas por año tienen sitio para 20 años; solo las finanzas tienen 10. El usuario eligió hacerlo en dos pasos:

1. **Fase 2: subir el tope a 20 temporadas** (2008–2027). Sin pantalla Archive: las pantallas del juego muestran los 20 años. Las finanzas de los años 11 a 20 van en la zona del final de la partida que siempre está a cero, si se confirma que nadie la usa.
2. **Más adelante: la ventana con archivo y la pantalla Archive** de este diseño, para pasar de 20.

Lo demás de este diseño (partidas terminadas que siguen, días de la semana del año real, nada de logos, todo con el mando) sigue valiendo para la fase 2.

## Lo que queda al constructor

- Formato del archivo (cómo se guarda cada año), mientras sea robusto y permita añadir años.
- Cómo dibujar la pantalla Archive (con el sistema de dibujo del propio juego o con una capa del runtime), mientras se vea como el juego y funcione en las plataformas objetivo.
- Cómo probar el paso de la temporada 10 a la 11 sin jugar 10 temporadas (por ejemplo, forzando el índice de año en una partida).
