# Launcher y controles

*English version: [en/launcher-controls.md](en/launcher-controls.md)*

La build local conserva el fondo, los colores y los botones brillantes del launcher original. Vídeo, Audio, Controles e Interfaz se abren mediante sus pestañas; Mods y Herramientas conservan su entrada desde el menú principal.

## Diseño adaptable

El panel de ajustes usa el marco dibujado del fondo solo cuando la ventana tiene una proporción cercana a 16:9. Con otras proporciones (ventanas estrechas, altas o 4:3) el panel ocupa la ventana sobre el fondo desenfocado, en lugar de quedar encajonado. Las listas largas se desplazan y la paginación se recalcula al cambiar el tamaño. Las ventanas emergentes se centran al abrirse.

## Uso con mando

- **Menú principal:** cruceta o stick para elegir, Cruz/A para entrar.
- **Listas de ajustes (Vídeo, Audio, Interfaz, Mods):** arriba/abajo eligen la fila (resaltada en naranja); izquierda/derecha o Cruz cambian su valor; L1/R1 cambian de pestaña; L2/R2 pasan de página; Círculo/B vuelve. En ventanas estrechas, Triángulo/Y abre la explicación del ajuste. La barra inferior muestra estas teclas.
- **Controles:** la cruceta mueve el foco por la cuadrícula; Cruz asigna; L1/R1 y Círculo igual que arriba.
- Aceptar y Volver no se repiten al mantenerlos, así que entrar en una pantalla con Cruz no cambia también el primer ajuste.
- El launcher escucha todos los mandos conectados, no solo el primero que lista SDL (DS4Windows expone el mando físico y su gemelo Xbox virtual).
- Con teclado: flechas o WASD, Enter para cambiar, Q/E para pestañas, Esc para volver.

## Controles (estilo Dolphin)

`Ajustes > Controles` es una sola pantalla con grupos, como el diálogo de mando de GameCube de Dolphin:

- **Cabecera:** Jugador (1–4), Dispositivo (mandos conectados o Teclado y ratón), Nombres de botones (automático, PlayStation o Xbox) y Predeterminado. Se reparte en 4, 2 o 1 columnas según el ancho.
- **Grupos:** Botones (A, B, X, Y, Z, START), Cruceta, Stick principal, Stick C, Gatillos (L, R, L analógico, R analógico) y Opciones. Se reparten en 4, 3, 2 o 1 columnas según el ancho; si no caben, la zona se desplaza.
- **Asignar:** clic (o Cruz/A/Enter) en una asignación. El botón se vuelve naranja y espera a que se suelte la entrada anterior; después acepta la nueva. Esc, otro clic en el mismo botón o 8 segundos sin entrada cancelan sin cambiar nada. Clic derecho quita la asignación de un botón o tecla.
- Los botones del juego aceptan botones, gatillos o direcciones de stick del mando. Las direcciones de los sticks y los gatillos analógicos aceptan un eje o un botón. Con Teclado y ratón, también teclas y botones del ratón. Los duplicados entre botones principales se intercambian.
- **Sticks:** cada grupo muestra una puerta octogonal con la zona muerta y la señal que recibe MLB, usando `PADRead` y la misma conversión a los rangos de GameCube del port (87/74, normalizados por MLB entre 88/64). Debajo, zona muerta y sensibilidad. Una sensibilidad menor de 1 suaviza la respuesta cerca del centro; una mayor de 1 la acelera. Ambas conservan el recorrido completo.
- **Probar sticks** (en Opciones) evita que los sticks muevan el menú mientras se prueban; Esc u OPTIONS termina.

### Diagonales al fildear

Un stick moderno es redondo: a fondo en diagonal da unos `(60, 61)` en la escala de GameCube, y con eso el fildeador camina. El teclado (por ejemplo W + D) da `(87, 87)` y corre. Mientras controlas a un fildeador, el runtime lee el stick principal de ese mando como un cuadrado: misma dirección, y en cada eje el recorrido total del stick. Una diagonal a fondo llega así igual que con el teclado, y el fildeador corre. Las direcciones rectas y la inclinación parcial no cambian, así que se puede seguir caminando.

Solo se aplica mientras el juego usa el control de fildeo de ese mando. Al batear, al pitchear y en los menús el stick sigue igual, y **Probar sticks** muestra la señal normal. Parche `tools/patches/wiicompiled-mlb-fielding-diagonals.patch`; prueba `mlb_fielding_stick_tests`. Detalles en `docs/ingenieria-inversa.md` (sección 9, «Mandos»).

## Volver y cierres

Volver (Esc, Retroceso, K o Círculo/B) solo cuenta en el fotograma en que se pulsa y nunca se repite. Así, la pulsación que cierra un desplegable o cancela una captura no cierra también la página de ajustes. Antes, mantenerla un instante provocaba esos cierres en cadena.

## Idioma

El launcher está en español o en inglés. Por defecto usa el idioma de Windows: español si Windows está en español e inglés en cualquier otro caso. Para cambiarlo: **AJUSTES > INTERFAZ > IDIOMA / LANGUAGE** (Automático, Español o English). El cambio se ve al momento y se guarda en `Config.toml` (`[ui] language = "auto" | "es" | "en"`); el Setup y el Texture Studio usan el mismo ajuste.

## Mejoras

**AJUSTES > MEJORAS** reúne lo que el juego de la Wii no tiene: cambios en cómo se juega o en lo que ofrece. Lo gráfico (FSR, filtrado, MSAA) se queda en VIDEO porque no cambia el juego, solo cómo se ve.

- Cada mejora empieza como en la Wii: apagada, o en su valor original, hasta que la cambies.
- Las que todavía están probando los jugadores llevan la etiqueta **EXPERIMENTAL**, y su ayuda pide avisar en GitHub Discussions o en un issue si algo falla. Cuando se hayan probado bastante, pierden la etiqueta y se quedan aquí.

## Temporadas (modo Temporada)

**AJUSTES > MEJORAS > TEMPORADAS** (experimental) decide cuánto dura el modo Temporada: **10 (original)**, que acaba tras la temporada de 2017 como en la Wii; **20**, que sigue hasta 2027; o **Sin límite**, que no acaba nunca. Se guarda en `Config.toml` (`[season] max_seasons = 10 | 20 | 0`; 0 es sin límite).

- Con 20 o sin límite, una partida que el juego original ya dio por terminada (tras la temporada 10, que repite el final al cargarla) sigue en 2018.
- Una partida que ya pasó del tope elegido sigue igual, para no estropearla: una de más de 10 temporadas sigue con 20, y una de después de 2027 sigue sin límite.
- El juego guarda 10 años de finanzas y de registros de los equipos; los años 11 a 20 van en la parte libre del final del archivo de la partida (`pm3usse<hueco>.dat`). Con 10, la partida queda igual byte a byte que la del original.
- **Sin límite:** las tablas del juego guardan 20 años. Al acabar cada temporada a partir de 2027, el año más antiguo sale de ellas y se guarda en un archivo de temporadas viejas dentro de la misma partida (caben unas 34; después se descarta la más antigua). Las pantallas del juego muestran los 20 años más recientes, con el año real y los días de la semana de ese año.
- El texto de la despedida del final dice «10 years» aunque hayan pasado 20.

Detalles en `docs/ingenieria-inversa.md` («Año de la temporada») y `docs/superpowers/plans/2026-10-19-season-20-build.md`.

## DH en ambas ligas (Temporada y MLB Life)

**AJUSTES > MEJORAS > DH EN AMBAS LIGAS** (experimental): el bateador designado en todos los partidos del modo Temporada y de MLB Life, como en las Grandes Ligas desde 2022. El juego original solo lo usa en los estadios de la Liga Americana; en los de la Nacional batea el pitcher. Se guarda en `Config.toml` (`[season] universal_dh = true`).

- Solo cambia el modo Temporada y MLB Life. Exhibición tiene su propio ajuste de DH y lo respeta.
- Un paquete de temporada con `dh = "both"` en su `mod.toml` lo activa aunque aquí esté en No (`docs/mods.md`).
- Vale para la partida que se esté jugando: no se guarda en ella.

Detalles en `docs/ingenieria-inversa.md` («DH en ambas ligas»).

## Ligas 15 y 15 (Temporada y MLB Life)

**AJUSTES > MEJORAS > LIGAS 15 Y 15** (experimental): Houston pasa a la División Oeste de la Liga Americana, como desde 2013, y las dos ligas quedan con 15 equipos. Se guarda en `Config.toml` (`[season] modern_leagues = true`).

**AJUSTES > MEJORAS > REGLA OHTANI** (experimental): con DH, un pitcher abridor también puede ser el bateador designado y seguir bateando cuando deja el montículo, como en las Grandes Ligas desde 2022. En la alineación, en **PITCHER**, pon de abridor al jugador que está de DH (por ejemplo Ohtani): queda en los dos puestos y el abridor que tenía el equipo pasa al bullpen. Al cambiar luego de pitcher, el jugador sigue de DH. Sirve en todos los modos con DH. Además, al crear un pitcher en MLB Career, al elegir **Pitcher** el panel pregunta **Select Second Position**: elige una posición del campo (o **None**) y el jugador nace con esa segunda posición (aptitud 5 de 7), como los jugadores de dos vías de Pawapuro. Cuando el juego arma la alineación solo (modo Temporada, MLB Life, los equipos de la CPU), un pitcher de dos vías (aptitud 4 o más en alguna posición del campo, o con bateo de bateador, como el Ohtani de los rosters actualizados) sale de **DH** los días que no abre, y de P y DH a la vez el día que abre. En MLB Life, con tu jugador de dos vías (pitcher con segunda posición), en la lista de entrenamiento **X** cambia entre los ejercicios de pitcher y los de bateador; los días que pitcheas el juego te evalúa y te hace crecer como pitcher, y los días que solo bateas, como bateador. Se guarda en `Config.toml` (`[season] two_way_player = true`).

- Vale para las temporadas nuevas del modo Temporada y las carreras nuevas de MLB Life; las partidas ya empezadas siguen como estaban.
- Con 15 y 15 el generador de calendarios del juego deja calendarios desiguales (de 142 a 162 partidos por equipo), así que el runtime usa uno propio con el formato de las Grandes Ligas desde 2023: 162 partidos por equipo, 13 contra cada rival de división (52), 6 o 7 contra el resto de la liga (64) y 46 de interliga (3 contra cada equipo de la otra liga y 4 contra el rival natural: Yankees–Mets, Cubs–White Sox, Dodgers–Angels…), en series de 2 a 4 partidos, unos 81 en casa, del 25 de marzo a finales de septiembre. Las fechas y los cruces no son los del calendario real de un año: para eso, un paquete de temporada con `schedule/<año>.csv`.
- Un paquete de temporada con `[divisions]` usa sus divisiones aunque aquí esté en No (`docs/mods.md`).
- Con Start (en vez de Quick Start), la pantalla Custom League se abre ya en 15/15 y deja confirmar 15 y 15 (el juego original solo admite ligas pares). Si cambias ahí la distribución y la confirmas, se juega la tuya; la mejora solo cambia las ligas de 2008 tal cual.

## Editor de temporada (HERRAMIENTAS)

**HERRAMIENTAS > EDITOR DE TEMPORADA** abre el Editor de temporada en su propia ventana (`docs/season-studio.md`). Desde ahí se ve qué usará una partida nueva del modo Temporada o de MLB Life, se ajusta cada mod (año de inicio, edades del roster, DH y ligas, con «Original» para dejarlo como el juego), se descargan calendarios reales y se ven mes a mes. El pie de **MEJORAS** resume en una línea con qué empezará la próxima partida nueva, porque lo que trae un mod manda sobre esas opciones.

## Guardado

Se mantienen los archivos de mapeo existentes y la sección `[controller]` de `Config.toml`. Los ajustes adicionales se guardan bajo `UserData` en instalaciones portátiles:

- `stick_response.txt`: sensibilidad de ambos sticks por jugador; archivo versionado, con valores predeterminados de 1 si falta o no es válido.
- `launcher_prompts.txt`: familia de nombres de botones elegida.

No se cambia el formato binario de los mapeos existentes. Una dirección de stick asignada a un botón usa el campo `nativeButton` que ya existía en el mapeo de ejes. El parche reproducible es `tools/patches/wiicompiled-launcher-controls.patch`, aplicado después de los parches existentes por `tools/wiicompiled-translate.ps1`.

## Verificación

`tools/test-launcher.ps1` crea una copia portátil privada bajo `local/analysis/launcher-tests/` y ejecuta una prueba breve con un mando virtual SDL. Comprueba:

- cambios de nombres de botones y rechazo del ruido pequeño del stick;
- sensibilidad aplicada por `PADRead` con el recorrido completo;
- captura en el propio botón e intercambio de duplicados;
- dirección de stick asignada a un botón;
- cancelación con Esc;
- desconexión del mando;
- lectura de los ajustes en un proceso nuevo;
- en una tercera pasada, la lista de ajustes con el mando: una fila por pulsación, cambiar con derecha, Cruz mantenida que cambia una sola vez y R1 para la siguiente pestaña.

No inicia una partida ni modifica los ajustes de la instalación del usuario.

El target `mkw_stick_response_tests` comprueba centro, borde de zona muerta, diagonales, respuesta monótona, sensibilidad y extremos. La interfaz se revisó a 640×480, 800×1061, 1280×720 y 1920×1061. Esto no sustituye una comprobación física con el mando del usuario.
