# Postemporada moderna: diseño

Autorizado por el usuario el 4 de noviembre de 2026 tras la investigación enlazada
en el roadmap. Alcance: Temporada y MLB Life; 12 equipos, rondas y localías MLB
actuales, persistencia por partida y opción original conservada.

El motor nativo seleccionará seis equipos por liga y controlará la ronda Wild Card.
Se reutilizarán los partidos y la simulación del juego. Los siete índices originales
de divisionales, campeonatos y Mundial se conservarán para sus consumidores; las
cuatro series adicionales vivirán fuera de las tablas originales. El estado moderno
se guardará con versión y validación, sin reinterpretar partidas antiguas por una
opción global. El cuadro mostrará también clasificados y resultados Wild Card.

La mejora se elegirá al crear partida desde MEJORAS o `[season] postseason` del
paquete (`"modern"` / `"original"`). Por defecto, original. No se convertirán cuadros
en curso ni se alterarán archivos originales o partidas del usuario para probarla.

Se identificarán primero los puntos de clasificación, agenda, resultados y carga.
Las pruebas usarán datos sintéticos y copias de partidas en `local/`; deben cubrir
desempates múltiples, byes, cruces fijos, victorias, localía, guardado/carga y avance
de año. Un test del modelo no demuestra que el juego complete la postemporada.
Todos los cambios del runtime se entregarán como parches contra la dependencia
fijada; el código generado se conserva.
