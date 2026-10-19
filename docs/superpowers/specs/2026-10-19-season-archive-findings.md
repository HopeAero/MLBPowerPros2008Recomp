# Modo Temporada sin tope: hallazgos de la investigación (fase 1)

*English version: [../../en/superpowers/specs/2026-10-19-season-archive-findings.md](../../en/superpowers/specs/2026-10-19-season-archive-findings.md)*

Fecha: 19 de octubre de 2026. Diseño: [2026-10-19-season-archive-design.md](2026-10-19-season-archive-design.md). Plan: [../plans/2026-10-19-season-archive-research.md](../plans/2026-10-19-season-archive-research.md). Detalle técnico: `docs/ingenieria-inversa.md`, «Año de la temporada».

## Cómo se investigó

- Sonda del runtime (`wiicompiled-season-probe`): registra el índice del año, lo puede forzar y deja manejar el juego en vivo (botones, escribir en memoria, volcar memoria).
- `tools/season_save.py` para comparar partidas y volcados.
- Partidas de prueba en un mod aparte (`SeasonProbe`); las partidas del usuario no se tocaron.
- Sin jugar partidos: la fecha se mueve en memoria y se guarda.

## Lo que se sabe

1. **El cambio de año se puede provocar sin jugar.** Desde el menú de pretemporada, con el día en el 25/12, «Daily Progress» pasa al 1/1 y suma un año. Visto de 2008 a 2009 y de 2009 a 2010.
2. **El tope de 10 es una variable** (`[0x8104BA2C]` = 10) que leen cinco funciones, más unas siete comparaciones fijas con 9 o 10.
3. **Final de la temporada 10.** Con el índice en 9, el paso a enero lo lleva a 10 y sale el final del modo (despedida, resumen, créditos, «Save as Arrange Team»). Una partida terminada repite el final al cargarla. El índice 10 queda guardado en la partida (archivo, 0x47E8C), así que una partida terminada se reconoce por eso.
4. **Datos por año:** casi todas las tablas del modo Temporada tienen sitio para **20 años** (clasificados y series de postemporada, clasificación de las ligas, equipo por año). Solo las **finanzas** tienen sitio para 10, y con el índice en 10 el juego ya escribe donde no debe (en el saldo del primer mes).
5. **Día de la semana:** se calcula por fórmula a partir del año; los años después de 2017 salen bien sin hacer nada.
6. **No encontrado:** un historial de premios o de estadísticas de jugadores por año. Las pestañas de carrera parecen acumular en los propios jugadores, no por año (sin confirmar).

## Lo que esto cambia del diseño

El diseño aprobado supone que el juego solo guarda 10 años y propone una ventana de 10 años con archivo. Los datos dicen otra cosa: el juego ya tiene sitio para 20 años salvo en las finanzas. Esto abre una opción más sencilla que el diseño no contemplaba:

- **A. Subir el tope a 20 temporadas** (2008–2027). Cambiar la variable y las comparaciones fijas, y dar sitio a 10 años más de finanzas (un archivo propio o una zona libre de la partida). La partida y el juego no cambian de formato en lo demás; no hace falta la pantalla Archive porque las pantallas del juego mostrarían los 20 años.
- **B. Lo aprobado: ventana con archivo y pantalla Archive**, sin fin. Mover las seis tablas una posición cada año. Con 20 huecos, la ventana podría ser de 20 años en lugar de 10.
- **C. A primero y B después:** 20 temporadas ya, y más tarde la ventana con archivo para pasar de 20.

Según la regla del diseño («si cambia el alcance, se vuelve a hablar con el usuario»), la decisión es del usuario. La fase 2 se planea cuando la tome.

## Riesgos y huecos

- Las partidas probadas no tienen partidos jugados: el sitio de cada tabla está confirmado, el significado exacto de algunos campos no.
- El final normal de la temporada 10 probablemente llega el 1/11 (por el código), no en enero; no se ha visto en el juego.
- Al saltar del 25/3 al 29/9 sin jugar, la pantalla «Improvement Notifications» se colgó al pulsar A (llamada a un objeto nulo). Seguramente lo causó el salto de fechas; queda por comprobar en una partida normal.
- No se ha probado una temporada 11 entera: más allá de las tablas puede haber algo que dependa del año sin seguir el patrón buscado.
