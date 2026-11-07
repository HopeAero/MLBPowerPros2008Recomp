# Investigación de la postemporada

*English: [en/postseason-research.md](en/postseason-research.md)*

Fecha: 4 de noviembre de 2026. Juego: Wii USA, RL8E54. Investigación estática, después
implementada y comprobada en Temporada (ver al final).

## Reglas que debe reproducir

Clasifican **12 equipos: 6 por liga**, tres campeones de división y tres comodines.
Los campeones ocupan los puestos 1–3; los comodines, 4–6, ordenados por porcentaje
de victorias dentro de cada grupo. Los puestos 1 y 2 descansan la primera ronda.

| Ronda | Cruces por liga | Victorias necesarias | Localía |
| --- | --- | --- | --- |
| Wild Card | 3 contra 6; 4 contra 5 | 2 | Todos en casa del mejor sembrado |
| Divisional | 1 contra ganador 4/5; 2 contra ganador 3/6 | 3 | 2–2–1 |
| Campeonato | Ganadores divisionales | 4 | 2–3–2 |
| Serie Mundial | Campeones de ambas ligas | 4 | 2–3–2 |

El cuadro es fijo. En las rondas de liga la ventaja corresponde al mejor sembrado;
en la Mundial, al mejor porcentaje de temporada regular. Los empates se resuelven
antes de sembrar: primero los divisionales y después los comodines.
Fuente: reglas 34, 35, 38 y 39 del [reglamento de MLB](https://phuturephillies.com/wp-content/uploads/2025/09/2024-major-league-rules.pdf),
pp. impresas 159–167; documento de MLB alojado por un tercero.

La [explicación vigente de MLB](https://www.mlb.com/news/mlb-playoff-format-faq)
confirma el formato y que en postemporada no se usa corredor automático.
No hay partido 163: para dos equipos se comparan enfrentamientos directos,
división, liga y resultados finales de liga; los empates múltiples necesitan su
procedimiento específico, no un simple comparador por parejas.
Fuente: [desempates de MLB](https://www.mlb.com/news/mlb-playoff-tiebreaker-rules).

## Lo encontrado en el juego

La documentación anterior ya identificaba las tablas de Temporada y el parche
`tools/patches/wiicompiled-season-cap.patch` las conserva al desplazar el historial.
Esta revisión localizó además sus accesores y consumidores en el código traducido.

Sea `P = [0x8104B5AC]` y `y` el índice interno del año, sin el desfase del año mostrado:

| Datos | Dirección | Límite original |
| --- | --- | --- |
| Clasificados | `P + y × 32 + puesto × 4` | 8 identificadores u32 por año, 20 años; 44 = vacío |
| Series | `P + 648 + y × 160 + serie × 20` | 8 registros por año, 20 años |

Ocho registros reservados no significan ocho eliminatorias: la progresión observada
usa índices 0–3 para divisionales, 4–5 para campeonatos y 6 para la Mundial.
El índice 7 recibe tratamiento especial en `0x80B8F0E8`; su finalidad queda pendiente.

| Función / dato | Evidencia estática |
| --- | --- |
| `0x80B377FC`, `0x80B88788` | Getter de P y salto al getter |
| `0x80B88264(y, serie)` | Dirección del registro de 20 bytes para un año explícito |
| `0x80B882B0(serie)` | Igual para el año actual, obtenido con `0x80B4A7DC` |
| `0x80B88304`, `0x80B8836C` | Escritura de un registro; copia cinco palabras |
| `0x80B888A4(y, puesto)`, `0x80B884D4` | Lectura por año y escritura del clasificado del año actual |
| `0x80B88458` | Busca un equipo entre exactamente ocho clasificados |
| `0x80B88658` | Recorre ocho puestos y devuelve una máscara limitada a 8 bits |
| `0x80B88928`, `0x80B888EC` | Lee / incrementa el byte de victorias de cada lado en registro +16 / +17 |
| `0x80B88BA4`, `0x80B88BDC` | Leen identificadores de los lados en registro +8 / +12 |
| `0x80B8895C`, `0x80B88994` | Escriben / leen el estado empaquetado; los consumidores usan 0/1 como lado ganador, 2 pendiente |
| `0x80B8ED34` | Usa series 0/1 o 2/3 para preparar la siguiente ronda, 4 o 5 |
| `0x80B8EF78` | Usa las series 4 y 5 y escribe la 6 |
| `0x80B8F0E8`, tabla `0x80D29E28` | Comprueba las victorias necesarias: 3, 3, 3, 3, 4, 4, 4 para índices 0–6 |

Llamadas directas que deben revisarse: `0x807DF90C`, `0x807DF928`,
`0x809E782C`, `0x809EB3D4`, `0x80B496A8`, `0x80B8CDA8`,
`0x80B94EA0`, `0x80B94F94` y `0x80BA8410`–`0x80BA86C8`.
Las dos llamadas de `0x809E…`, dentro del área previamente estudiada de MLB Life,
piden expresamente la serie 6. Esto es evidencia de dependencia del índice original,
no una validación completa de MLB Life ni un inventario de llamadas indirectas.

Fuentes privadas consultadas, sin modificarlas ni publicar su contenido:
`local/generated/dolrecomp/rel-linked/runtime_rel_chunks/` (especialmente chunks
0368, 0388 y 0390), `linked-analysis.dol`, y el runtime local `hle/mlb/season_cap.cpp`.
La tabla de umbrales se leyó mediante las secciones del DOL y coincidió con los
accesos de la rutina. Son hallazgos de análisis, no funciones reconstruidas con match.

## Consecuencias para la adaptación

El formato moderno necesita **11 eliminatorias reales**: 4 Wild Card, 4 divisionales,
2 campeonatos y 1 Mundial. No cabe en las ocho ranuras originales. Tampoco caben
12 clasificados en la fila de 32 bytes: ampliar solo los bucles invadiría el año siguiente.
Además, la máscara de ocho bits perdería los puestos adicionales.

El alcance mínimo incluye selección y desempates, nueva ronda y progresión,
fechas y descansos, localía, cuadro visible, simulación, guardado/carga, archivo de
temporadas y consumidores de MLB Life. El desplazamiento del historial actualmente
archiva filas de 32 y 160 bytes: también debe conservar el estado moderno.

Conviene mantener las estructuras originales y añadir estado moderno versionado,
siguiendo los mecanismos de persistencia existentes. Es una propuesta pendiente de
comprobar capacidad y guardado en ambos modos; la cola libre del archivo de Temporada
no demuestra espacio disponible en MLB Life. El formato debe fijarse por partida,
evitando reinterpretar un cuadro iniciado al cambiar una opción global.
La opción original de ocho equipos debe seguir disponible, como establece el roadmap.

## Implementación (parche `wiicompiled-modern-postseason`)

Opción **POSTEMPORADA DE 12 EQUIPOS** en MEJORAS (`[season] modern_postseason`) o
`[season] postseason = "modern"` / `"original"` en el `mod.toml` de un pack, que manda
sobre la opción. Se fija al crear la partida y viaja en ella; por defecto, original.

- **Estado:** las siete series originales siguen en sus índices 0–6. Las cuatro Wild Card
  son los índices 8–11, en la cima de MEM2 reservada a IOS (`0x97FFC000`, 80 bytes por año),
  y `0x80B88264`/`0x80B882B0` las resuelven ahí. Los cuatro primeros sembrados de cada liga
  ocupan la fila original de ocho clasificados; los seis por liga van en el estado moderno.
- **Clasificación:** en el día 274/275 (`0x80B4BB28`, `0x80B8E8B4`, `0x80B8F97C`) se lee la
  temporada regular del calendario, se aplican campeones y comodines con los desempates de
  la regla 34 y se programan los Wild Card los días 275–277 (`0x80B4BB28` devuelve la fase 2).
- **Partidos:** el calendario se escribe con los mismos campos que `0x80B72928`, el
  constructor original de partidos de postemporada (tipo 1 en los bits 17–19 de las banderas).
  Con el tipo de temporada regular, la CPU no simulaba los demás cruces del día.
- **Resultados y avance:** `0x80B739AC` redirige los resultados de esos días a las series
  8–11; al cerrarse una, se arma la Divisional fija (1 contra 4/5, 2 contra 3/6) y se
  programa. Campeonatos y Mundial usan los índices originales; la Mundial da la localía al
  mejor récord, con desempate.
- **Pantalla del cuadro:** tras cada partido el juego anima los cruces del día como serie
  «primera de la ronda + hueco», y la ronda guardada (`[P + 640]`) es 0; en días Wild Card
  trataba esos partidos como Divisionales cuyos equipos no están en su cuadro de ocho:
  `0x80B898FC` devuelve −1 y `0x80B89A84` llama a través de un objeto basura (salto a
  `0x148`). `0x80B8AC38` y `0x80B8AD28` se saltan en esos días.
- **Cuadro de 12 equipos** (MLB DATA › Postseason y pantalla entre partidos, en pantalla ancha):
  el cuadro original de ocho casillas sigue igual y cada par Wild Card se dibuja en el margen,
  junto a la casilla que alimenta, con las imágenes del propio juego leídas de MEM2 en
  ejecución (casilla de equipo 64×256 C8, logos pequeños 512×128 C8 en celdas de 36×42 por
  código de equipo, dígitos 512×32 C8 con paleta azul y roja contigua, líneas 512×256 C8 con
  el gris que el juego tiñe: amarillo si el equipo sigue, oliva si quedó fuera). Se reconocen
  por el XXH64 de sus texels; nada sale del juego. `0x80B899B8` (marcadores) y `0x80B897F0`
  (casillas) colocan el lado 1 a la izquierda: en las Divisionales contra el sembrado 2 se
  invierten para que ambos ganadores Wild Card queden en las casillas exteriores, y mientras
  esas series están vacías muestran a los sembrados 1 y 2 (descanso). El título de MLB DATA
  (`0x80B52240`, por la ronda guardada) dice POSTSEASON durante la ronda Wild Card. En 4:3 el
  cuadro del juego se encoge al 75 % en torno a su centro (nodos de layout: traslación en `+64`,
  escala en `+96`; el cuadro es `[pantalla + 0x7B8]` y los ocho caminos `[pantalla + 0x38 + 4i]`)
  y los pares Wild Card van a sus lados. La ventana de ImGui anterior se retiró.
- **Calendario:** los días 275–277 (2–4 de octubre) muestran WILD CARD como los demás días de
  postemporada. `0x80B9C4C0(pantalla, mes)` arma el mes y `0x80B9B5F8(casilla, etiqueta, rival,
  amarillo)` muestra la etiqueta `[casilla + 0xF4 + 4·etiqueta]` (3 = DIVISION); cada nodo de
  etiqueta dibuja la SURF de `[nodo + 0xDC]`. Se usa el nodo DIVISION de esas casillas con una copia
  de la SURF de DIVISION cuyo rectángulo apunta a una zona libre del mismo atlas (256×256 C8,
  x 123–255, y 164–193), donde «WILD CARD» se compone con las letras del atlas (W, R, L de WORLD;
  I, D y una O abierta en C de DIVISION; A de ALL-STAR) en los colores de DIVISION. Antes de cada
  mes se devuelve la imagen original.
- **Clasificación:** `0x80B7B794` rellena cada fila; DIVISION WINNER si `0x80B8D69C(liga,
  división, equipo)` lo encuentra en su hueco de la fila de clasificados, WILD CARD WINNER si
  `0x80B8D6F4(liga, equipo)` lo encuentra en el único hueco de comodín (reglas de 2008). Con el
  formato moderno responden por los sembrados: 1–3 campeones por su división, 4–6 comodines. Antes
  de terminar la temporada regular no se marca comodín (la lógica original lo haría con las reglas
  de 2008). La fila de clasificados se escribe en el orden del juego: campeones de Este, Central y
  Oeste y el primer comodín.
- **Guardado:** Temporada usa la cola `PPSEASON` versión 4 con un bloque `MLBPOSTS` v1
  (6.144 bytes, suma FNV-1a, validado al leer) al final; MLB Life, el mismo bloque en
  `0xF6000` del archivo. Las partidas sin bloque cargan con el formato original. El historial
  desplazado de «Sin límite» también archiva el estado moderno.

## Comprobado

- **Pruebas sintéticas** (18 de ctest): siembra, desempates múltiples, cuadro fijo, localía
  2–2–1 / 2–3–2, fin de series, bloque guardado (corrupto, truncado, versiones) y cola v4.
  No demuestran por sí solas que el juego complete la postemporada.
- **En el juego, Temporada** (4 de noviembre de 2026, copia aislada de NAND en `local/`,
  temporada regular completada en RAM con resultados sintéticos, mod de prueba
  `postseason = "modern"`): `[postseason] year 0: 12 qualifiers`; Yankees sexto sembrado en
  Oakland. Los dos partidos Wild Card se jugaron con Fast Game y los otros tres cruces
  los simuló la CPU: las cuatro series cerraron 2–0 el día 276. Divisionales completas
  (tres a 3–2, una 3–0), campeonatos 4–3 y 4–3 y Serie Mundial 4–3, terminada el 31/10.
  Premios y fuera de temporada continuaron. Guardado el 25/12, juego cerrado y cargado:
  las cuatro series Wild Card se restauran idénticas; el paso a 1/1/2009 deja la fila nueva
  vacía. Replay: los 83 parches aplican sobre el árbol fijado y el código coincide con el probado.

- **En el juego, MLB Life** (5 de noviembre de 2026, partida de S. Ohtani con los Dodgers en la
  copia aislada de NAND; resultados en RAM hasta el 24/9 y el juego simula del 25/9 al 30/9 con
  «saltar» del calendario): clasificación al pasar al día 275, las cuatro series Wild Card
  simuladas (dos terminaron 2–1 en el tercer partido), Divisionales, Campeonatos y Serie Mundial
  hasta el 31/10; calendario con WILD CARD, clasificación con 3 DIVISION WINNER y 3 WILD CARD
  WINNER por liga y el cuadro de 12 equipos. Guardado en la ranura 2 (bloque `MLBPOSTS` en
  `0xF6000`), juego cerrado y cargado: las cuatro series se restauran idénticas.
- **Dos arreglos encontrados con Life** (afectan también a Temporada):
  - **Partidos pendientes, como en MLB:** la postemporada nunca se mueve (en MLB la temporada
    termina un domingo, el lunes queda de reserva y el Wild Card empieza el martes). Cuando ya no
    queda ningún partido programado, cada partido no jugado se repone en un día libre antes del
    día 275 (en 2008 quedan libres el 29/9, el 30/9 y el 1/10) si su resultado puede cambiar los
    clasificados o su siembra (que decide descansos y localía); si no puede, se cancela y esos
    equipos terminan con menos de 162 partidos, como los Nationals en 2008. Sin día libre, también
    se cancela y la clasificación cuenta solo los jugados. Comprobado en Temporada: 16 partidos
    pendientes del 24/9 (con dos dobles carteleras), 3 repuestos (dos Orioles–Yankees y un
    Astros–Athletics) y jugados el 30/9 y el 1/10, 13 cancelados; la clasificación del 2/10 los
    incluye.
  - **Días libres por calendario** (5 de noviembre de 2026): el oficial de 2008 termina el 27/9
    (libres 28/9–1/10); los de los mods de 2025 y 2026, el 28/9 y el 27/9 (3–4 días libres). Los
    que genera el juego llenan casi todo septiembre: 2009 termina el 30/9 (solo queda el 1/10) y
    2010 el 1/10 (ninguno). En esos años un partido pendiente decisivo se cancela por falta de día.
  - Los huecos vaciados del calendario usan los mismos campos que el juego (`0x80BA0DAC`).
- **Nota para pruebas:** el día 277 es el evento 55 y `0x80B8CC80` devuelve las plantillas a la
  foto del 31/8 (marca de septiembre, bit `0x02` de `+0x16`, que pone `0x80B8CBD8` la noche del
  1/9). Una partida de prueba que salta esa noche no tiene marcas y todos los equipos quedan sin
  jugadores de MLB, así que el tercer partido Wild Card falla. Es un efecto de la prueba, no del
  parche: una temporada jugada pasa por el 1/9. `local/analysis/postseason/mark_sept.py` pone las
  marcas en las pruebas.

## Pendiente

- **MLB Life:** sin probar el paso a 2009 dentro de Life (en Temporada sí).
- **Vista de simulación:** la animación entre partidos sigue sin la ronda Wild Card.
- **Clasificación durante la temporada:** solo marca a los clasificados al terminarla; no hay una
  vista de «carrera por el comodín» mientras se juega.
- **Temporada real:** la sonda partió de resultados inyectados en RAM, no de una temporada
  simulada por el juego. Falta la integración completa del roadmap.
- **Empates de cinco o más equipos:** la regla 34(c) de MLB solo detalla empates de dos, tres
  y cuatro equipos y deja el resto al Comisionado. El modelo aplica los pasos de cuatro equipos
  a cualquier número (decisión del usuario, 6/11/2026). Si tras comparar todos los partidos de
  liga siguen empatados, sortea uno con una semilla sacada de los resultados de la temporada:
  al recargar la partida sale el mismo equipo.
