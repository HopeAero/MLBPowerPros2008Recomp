# Rosters de MLB Power Pros 2008 y PowerUp / PowerUp+

*English version: [en/roster-2008.md](en/roster-2008.md)*

## Qué es PowerUp+

[PowerUp+](https://github.com/ogura-connection/PowerUpPlus) es un fork, con licencia GPLv3, de [PowerUp](https://github.com/CSho27/PowerUp), el editor de partidas guardadas de **MLB Power Pros 2007** para Wii.
- Lee y escribe `pm2maus.dat`, la partida de 2007, y genera rosters reales a partir de la MLB Stats API.
- Trae dos rosters ya generados: 2025/2026 y "All-Time Franchise".
- Todo el formato del guardado de 2007 lo descifró Christopher Shorter (PowerUp).

## Dónde vive el roster en 2008

En 2008 el roster base **no** está en la partida guardada, sino en `project.bin` → `/game_data.pack` (278.176 bytes).
- Ese paquete es `PACKWII` + 0x20 bytes de cabecera + el **mismo bloque de roster** que el guardado de 2007 tiene en 0x68C74. Son 0x43E80 bytes:
  - 1.500 jugadores de 0xB0 bytes (1.007 ocupados en 2008);
  - alineaciones, agentes libres y 32 equipos de 0x140 bytes (30 franquicias + All-Stars AL y NL).
- Los nombres usan la misma tabla de caracteres de 16 bits que 2007 (`Character_Library.csv` de PowerUp).
- `game_data.pack` lo leen todos los modos al arrancar. Por eso un roster cambiado ahí aparece en Exhibition, en la demo y en **MLB Life** (verificado: MLB Life → From Beginning → MLB Career Mode → la selección de jugador muestra los Red Sox 2025 con Crochet, ERA 2.59, 98 mph, abridor zurdo).
- Lo que el modo Edit guarda en la partida no llega a MLB Life porque MLB Life parte de `game_data.pack`. Cambiar el paquete resuelve ese problema.

## Diferencias del registro de jugador 2007 → 2008

Se obtuvieron comparando los 729 jugadores que están en el roster base de ambos juegos (`tools/roster/align_2007_2008.py`): para cada campo de PowerUp se busca el desplazamiento que más coincide, contando solo los jugadores cuyo valor no es el más común.

| Zona (bits de 2007) | En 2008 |
|---|---|
| 0x00–0x5B (id, nombres, número, equipo, apariencia) | igual, salvo la cara (ver abajo) |
| forma de bateo y de lanzamiento | pasan de 8 a 9 bits (0x5C.4 y 0x5D.5) |
| posición principal y capacidades de P y C | +2 bits |
| 0x60–0x6E (capacidades de defensa, zonas calientes, lado de bateo, ratings) | igual |
| segundo "contra zurdos" (0x6F.5) | ya no existe |
| 0x70.0–0x77.6 (habilidades de bateo y defensa) | −3 bits |
| 0x77.7–0x81 (pivot man, pull hitter, bloque de pitcher) | −2 bits; 2008 agrega un bit nuevo en 0x77.4 |
| fecha de nacimiento | −12 bits; el año se suma +1 porque PowerUp calcula desde el día inaugural de 2007 |
| años en las mayores y estadísticas | años en 0x85.2, AVG en 0x85.7, RBI en 0x88.0, HR en 0x89.2; ERA sigue en 0x8C.0 |
| 0x8C–0x93 | igual; la voz (0x92.2, 14 bits) sigue en su sitio pero con otra numeración |
| repertorio (0x94–) | igual |

Los bits que PowerUp no nombra viajan con su zona; el conversor los copia como campos de un bit. Por ejemplo, 0x76.0 y 0x76.1, 0x77.7, 0x80.7, 0x8D.6–0x8E.3 y 0x92.0: 2008 los conserva en la posición esperada, con φ ≈ 1.

**Cómo se confirmó:** `align_2007_2008.py --bits` calcula, para cada bit de 2008, la correlación φ con los bits de 2007 desplazados de 0 a 4 posiciones. Así salen claras las banderas raras que el voto por coincidencias no distingue:
- abridor, relevista y cerrador son campos de 3 bits (0x62.5, 0x63.0 y 0x63.3) y siguen en su sitio (φ 0,82, 0,83 y 0,70);
- el límite entre −3 y −2 está en 0x77.7. Con un límite en 0x78.2, "pull hitter" caía en 0x77.6, donde 2008 tiene un solo jugador marcado; en 0x77.7 hay 42.
- las habilidades se comprueban con los jugadores que las tienen en 2008:
  - "push hitter" (0x71.0): Ichiro, Joe Mauer, Todd Helton, Michael Young, Mark Loretta;
  - "free swinger" (0x71.7): Alfonso Soriano, José Guillén, Iván Rodríguez, Torii Hunter, Garret Anderson;
  - "pull hitter" (0x77.7): Jason Giambi, Alex Rodríguez, Gary Sheffield, Andruw Jones, Carlos Peña.
- "table setter" (0x70.0) no lo tiene ningún jugador de 2008, pero el juego lo sigue usando: tiene su función de lectura, su función de escritura en el modo Edit y dos funciones de juego que lo consultan junto a "rally hitter" (detalle en `docs/ingenieria-inversa.md`, sección 6). El conversor lo escribe en su sitio y funciona si se le da a un jugador.

**Comprobado en el código del juego.** El juego lee cada campo del registro con una función pequeña, todas seguidas y en el orden del registro. Sus posiciones y anchos coinciden con esta tabla en todos los campos movidos. Algunos que PowerUp modela como un solo bit son de 2 bits para el juego. El otro bit viaja como bit sin nombre, así que el valor completo se conserva:
- 0x75.4 (2 bits con signo, "cannon arm" para PowerUp): +1 en 96 jugadores y −1 en 11. No es el brazo cañón; ver la tabla siguiente.
- 0x78.4 (2 bits con signo): una habilidad nueva de 2008 que PowerUp no conoce: partido nocturno, bueno o malo.

**Comprobado en pantalla** (Arrange → un equipo → Fielder Select → stick C, "Player ABL"; capturas de Cora, Varitek, Casey, J.D. Drew, Eckstein y Rios cruzadas con sus bits):

| Bits de 2008 | Lo que muestra el juego | En PowerUp |
|---|---|---|
| `0x73.5`/2 | HD 1st Slide (valor 2) | WillSlideHeadFirst, bien |
| `0x73.7` | Tough Runner | IsToughRunner, bien |
| `0x74.4` | **Cannon Arm** (en los mensajes de MLB Life, "Laser Beam") | lo llama CanBarehandCatch (corregido para 2008) |
| `0x75.2` | Gd Block | IsGoodBlocker, bien |
| `0x75.4`/2 con signo | **nada en ninguna pestaña**: rasgo oculto. +1 en pitchers potentes y bateadores corpulentos, −1 en infielders pequeños; podría ser la complexión (sin confirmar) | **mal:** lo llama HasCannonArm |
| `0x76.2`/2 | Slugger (+1) / Slap Hitter (−1) | SlugOrSlap, bien |
| `0x76.4`/2 | Aggressive (+1) / Patient Hitter (−1) | bien |
| `0x76.6`/2 | Aggressive (+1) / Cautious Stealer (−1) | bien |
| `0x77.0`/2 | Aggressive Runner (+1) | IsAggressiveBaserunner, bien |
| `0x77.2`/2 | Aggressive Fielder (+1) | IsAggressiveFielder, bien |
| `0x77.4` | **Gd Eye**, bit nuevo de 2008 | no existe |
| `0x77.5` | **Barehand Catch** | sin nombre (Bit_77_7); en 2008 lo recibe CanBarehandCatch |
| `0x78.0`/2, `0x78.2`/2 | Gd/Pr Day Game, Gd/Pr Rain | bien |
| `0x78.4`/2 | **Gd (+1) / Pr (−1) Night Game**, nueva de 2008 | no existe |

- Los mismos jugadores tienen estos bits en 2007, así que en 2007 PowerUp también se equivoca de nombre.
- **Para 2008 se corrige por significado, y 2007 se deja como está.** Los rosters que genera PowerUp ya usan las casillas por su nombre (su roster 2025 marca "Cannon arm" a Tatis, Arenado, Salvador Pérez y Betts). Por eso la tabla de 2008 (`MOVED` en `powerup_to_2008.py`) lleva "Barehand catch" a `0x77.5` y "Cannon arm" a `0x74.4`. El rasgo oculto se conserva del registro de 2008, o vale 0 en un jugador nuevo, y los restos de la partida base de 2007 ya no se copian.
- Resultado con el roster 2025: antes, 0 brazos cañón, 117 barehand al azar y 100 rasgos ocultos basura; ahora, los 4 brazos cañón reales y nada más.
- En PowerUp+, un roster de 2008 importado de nuevo muestra cada casilla con su habilidad real (Rios: Cannon Arm; Eckstein: Barehand). La ida y vuelta sigue idéntica byte a byte. Los rosters de 2008 importados antes de este cambio hay que importarlos de nuevo.
- **Gd Eye y Night Game se editan en PowerUp+** (sección General, marcadas "(2008)"). Una importación de 2008 los lee del registro original.
  - La exportación de 2008 los escribe en dos huecos del formato de 2007 que la conversión ya no usa (`MOD_2008`: `0x77.7` → `0x77.4` y `0x78.6`/2 → `0x78.4`/2).
  - El conversor del juego solo los toma de un mod con `roster_ids = "2008"`. Un `.dat` genérico de 2007 se convierte igual que antes.
  - La exportación de 2007 los deja vacíos, así que una partida de 2007 se escribe bit a bit como antes.
- Comprobado: Payton (Pr Night Game) cambiado en el editor a Gd Night Game y Gd Eye, guardado, exportado y convertido. En el roster del juego cambian exactamente esos bits; el runtime da lo mismo que Python.
- Al copiar un jugador (necesario para editar uno importado), PowerUp no copiaba Day Game, Rain, la fecha de nacimiento, la edad, los años en las mayores ni las estadísticas. Sin fecha, la exportación fallaba. Ahora se copian.

**Caras:** 2008 insertó caras nuevas, así que los números de 2007 se corren por tramos:

| Cara 2007 | 0–27 | 28–105 | 106–160 | 161–176 | 177 en adelante (caras genéricas) |
|---|---|---|---|---|---|
| Cara 2008 | igual | +1 | +2 | +7 | +12 |

Con esta tabla coinciden 707 de los 729 jugadores comunes; sin ella, solo 26. Los demás cambiaron de cara entre juegos.

**Piel y ojos** (0x55.4, 4 bits): 2007 tiene 5 tonos de piel y suma 5 si los ojos son marrones. 2008 agregó un tono en la posición 2 y suma 6. Por eso 0–1 quedan igual, 2–6 suben 1 y 7–9 suben 2. Coinciden 695 de 729 (272 sin la tabla).

**El tono de piel de 2008 en PowerUp+** (20 de octubre de 2026, a petición de un jugador en Reddit): el editor lo ofrece como **2.5 (2008)**, entre el 2 y el 3. Dentro de PowerUp+ se guarda como 10 (ojos azules) u 11 (marrones), valores que el formato de 2007 tiene libres; al exportar a 2008 son 2 y 8. Una exportación para el 2007, que no tiene ese tono, pone el 2. El roster de 2008 trae 27 jugadores con él (1 con ojos azules y 26 con marrones): antes se importaban con el tono 2 y ahora conservan el suyo. Comprobado importando el `project.bin`, exportando el mod (mismos valores que el juego) y cambiando dos jugadores al tono nuevo (salen 2 y 8).

**Revisión del resto de campos:** se compararon todos los campos de los 729 jugadores, ya convertidos, con el roster real de 2008. Solo la cara y la piel/ojos tenían números de catálogo cambiados. Lo demás que difiere son datos de la nueva temporada:
- ratings, estadísticas, peinados;
- 2008 marca a la mayoría de los pitchers como robadores "cautelosos".

El año de nacimiento de PowerUp está calculado desde la edad (Crochet figura como nacido en 1981, es decir, 26 años en el calendario de 2007), y por eso se le suma 1 para 2008.

**Voces:** el campo está en el mismo lugar, pero 2008 renumeró parte de las voces: Erstad sigue con la 2051 y Accardo pasó de la 2056 a la 2057.
- 2008 conserva el orden de 2007, pero inserta y quita voces.
- `align_2007_2008.py --voices <Voice_Library.csv>` reconstruye la correspondencia: 810 de las 968 voces de PowerUp, en 206 tramos (`VOICE_RUNS`).
- Método: entre dos jugadores presentes en ambos juegos cuyos huecos miden lo mismo, las voces van una a una, salvo que un nombre de 2008 lo contradiga.
- Ocultando el 10 % de esos jugadores, los 739 casos caen en su voz real.
- Las otras 158 voces son de jugadores que 2008 quitó o que no se pueden ubicar con seguridad: esos jugadores quedan sin voz (0).

## Usarlo

**Como mod (lo normal):** una carpeta `mods\<nombre>\roster\` con el `.dat` de PowerUp / PowerUp+ (por ejemplo `2025_MLB_Rosters.dat`) y un `mod.toml` opcional.
- Al activarlo en MODS y pulsar JUGAR, el juego extrae el `game_data.pack` de **tu** disco, lo convierte con el roster del `.dat` y lo sirve dentro de un `project.bin` modificado.
- La conversión se guarda en `UserData\Cache\mods\roster\<mod>\`.
  - Se rehace si cambia el `.dat`, el conversor o `roster_ids`.
  - Las conversiones viejas se borran.
- El mod nunca lleva archivos del juego.
- Un jugador del `.dat` que también está en el roster original de 2008 parte de su registro de 2008.
  - Se reconoce por nombre, apellido y cumpleaños. Si eso se repite, decide el número de jugador: 2008 tiene dos "Chris Young", dos "Tony Peña" y dos "Great Harris".
  - Así conserva lo que solo existe en 2008 (su voz, los bits nuevos) y sus valores de 2008 en los bits que PowerUp no nombra. PowerUp no puede editar esos bits y los rellena con lo que tuviera ese hueco en su partida base.
  - Los demás jugadores parten de ceros.
- Los huecos sin jugador reciben el registro vacío de 2008, que no es todo ceros.
- Alineaciones, agentes libres y equipos salen del `.dat`, con estos ajustes:
  - las entradas vacías llevan el patrón vacío de 2008;
  - las entradas de un mismo jugador conservan los bits que PowerUp no modela.
- Un `.dat` de 2007 (como `2025_MLB_Rosters.dat`) trae caras y voces con números de 2007.
  - Las caras y la piel/ojos se pasan a 2008 con las tablas de arriba.
  - Las voces de 2007 se pasan a 2008 con `VOICE_RUNS`; los jugadores que también están en 2008 mantienen la suya. Con el roster 2025 de PowerUp, los jugadores sin voz bajan de 1.280 de 1.281 a 199.
- Si el `mod.toml` dice `roster_ids = "2008"` (lo escribe la exportación de PowerUp+), voz, cara y piel/ojos ya vienen en números de 2008 y se copian tal cual.
  - Las exportaciones anteriores a la corrección de PowerUp+ (ver abajo) no traen los bits que PowerUp no nombra. Para que sigan funcionando, sus jugadores nuevos reciben el valor que tiene la mayoría de los jugadores de 2008. Sin ellos (0x4E.1, 0x4E.2, 0x4F.0 y 0x92.0, que todo jugador de 2008 tiene en 1), la pantalla de alineación de Exhibition mostraba barras vacías y todos como "P". Con una exportación nueva el resultado es el mismo con o sin este relleno.
- **Jugadores nativos o modificados:** launcher → MODS → HERRAMIENTAS → **JUGADORES DEL ROSTER** (`[mods] native_roster` en `Config.toml`).
  - **Nativos** (por defecto): todos entran sin editar (`Unedited` 0x50.3 en 1, `IsEdited` 0x50.4 en 0), como en el roster base de 2008.
  - **Como en el editor:** cada jugador conserva el interruptor *Custom Player* de PowerUp+.
  - Según lo observado, el juego deja fuera de MLB Life a los jugadores editados.
  - `powerup_to_2008.py --editor-flags` equivale a "Como en el editor".
  - El parche de PowerUp+ corrige su importación: la condición estaba invertida y marcaba como *Custom Player* a todo jugador importado. Los rosters importados antes de esta corrección siguen con todos marcados; hay que volver a importarlos para que "Como en el editor" los distinga.

**Con PowerUp+ (editar el roster de 2008):** el parche `tools/patches/powerupplus-2008.patch` le agrega a PowerUp+ soporte para 2008.
- **Abrir:** en *Import Roster → Game Save* se puede elegir el `project.bin` del juego (`<instalación>\Data\files\project.bin`) o un `game_data.pack`.
  - PowerUp+ lo convierte al formato de 2007 y lo abre como un roster más.
  - La cara y la piel/ojos pasan a la numeración de 2007, la que muestra su editor.
  - Para los nombres largos que añadió 2008 (Saltalamacchia, Chamberlain…), el parche agrega sus códigos a `SpecialSavedName_Library.csv`.
  - La lista de agentes libres tiene sitio para 200 jugadores; antes PowerUp leía solo 15, y 2008 usa 25.
  - El `game_data.pack` queda guardado en su carpeta de datos, junto a la partida base. Solo vive en la PC del jugador.
- **Exportar:** *Export Roster → MLB Power Pros 2008 Mod* descarga un zip con `<nombre>\mod.toml` y `<nombre>\roster\<nombre>.dat`. Se descomprime en `mods\` y se activa en MODS. A diferencia de la exportación de 2007:
  - **Un registro por persona.** Los equipos All-Star apuntan al mismo jugador, como en el juego, en vez de crear copias. Las copias que trae un roster importado de una exportación de 2007 se unen al jugador de su equipo regular.
  - **Números de jugador originales.** Los jugadores que vienen del roster de 2008 conservan su número de jugador y, con él, su hueco y su salario. Los nuevos toman el número que elige PowerUp por salario o el primero libre.
  - **Huecos vacíos y orden.** Los huecos sobrantes quedan vacíos, en vez de llevar jugadores de la partida base de 2007. Los equipos mantienen su orden, con los de grandes ligas primero; la exportación de 2007 los reordenaba con un `OrderBy` encadenado.
  - **Valores sin tocar.** El modelo de PowerUp no conserva todos los valores exactos: piel y ojos según la cara, color de un pelo que no existe, movimiento de la recta, banderas de "editado". En cada campo que vale lo mismo que daría un viaje de ida y vuelta sin tocar el jugador original, se escribe el valor original de 2008.
  - **Bits sin nombre.** La exportación escribe los bits que PowerUp no modela (`PowerPros2008Fields.Unmodeled`, generado con `--emit-cs`): un jugador que viene de 2008 lleva los suyos y uno nuevo el valor que tiene la mayoría de los jugadores de 2008. Así el `.dat` funciona por sí solo, sin depender de que el juego lo complete.
  - **Caras, piel/ojos y voces.** Se escriben en números de 2008 (`roster_ids = "2008"`). Una cara o piel sin tocar vuelve a su valor exacto de 2008, y una elegida en el editor se convierte con la tabla. La voz elegida en el editor se convierte a 2008 con `VOICE_RUNS`; una sin tocar vuelve a su número exacto de 2008, aunque sea 0 (sin voz). Si la voz elegida es de 2007 y 2008 la quitó, o si un jugador nuevo no tiene voz, recibe la voz de 2008 cuyo nombre suena más parecido, con el mismo método que PowerUp+ usa para 2007.
  - **Voces en el editor.** Al importar, la voz pasa a la numeración de 2007, la que entiende el editor, igual que las caras. Una voz que solo existe en 2008 recibe el número 10000 + su número de 2008 y el nombre del jugador de 2008 que la usa. En modo 2008 la lista solo ofrece voces que existen en el juego: 1.087. Los 1.003 jugadores de 2008 con voz ven el nombre de su propia voz. Un jugador sin voz muestra "Sin voz".
  - Los rosters importados antes de este cambio guardan la voz con el número de 2008: se exportan bien sin tocarlos, pero el editor les muestra otro nombre. Hay que volver a importarlos.
- **Desde el juego (lo normal):** launcher → MODS → pestaña HERRAMIENTAS → **EDITAR ROSTER (PowerUp+)**.
  - Abre PowerUp+ en el navegador (`http://127.0.0.1:5123`) y se cierra junto con el juego.
  - El Setup lo instala ya compilado en `Game\powerupplus\` (autocontenido, no hace falta instalar .NET). Sus datos (base de datos, librerías) quedan en `UserData\PowerUpPlus\`.
  - Corre en "modo 2008":
    - el roster base es el de tu `project.bin`;
    - no usa ni distribuye la partida de 2007 (`BASE.pm2maus.dat`): crea un contenedor vacío;
    - *Export → MLB Power Pros 2008 Mod* guarda el mod directamente en la carpeta `mods\`, listo para activar en MODS.
  - El `.dat` del mod lleva solo el bloque del roster; el resto de la partida va vacío, así que no arrastra nada de una partida de 2007.
  - Un roster importado (también el base) es de solo lectura. Para editarlo se hace *Copy Existing Roster* y se trabaja sobre la copia.
- **Para desarrollo:**
  - `tools/powerupplus.ps1 -Ui` compila y abre la misma interfaz con todo dentro de `local\`. Requiere el SDK de .NET 10 en `local\tools\dotnet10` y Node.
  - `-Publish <carpeta>` hace la versión que va en el Setup.
- **Línea de comandos:** el mismo script sin `-Ui` ejecuta los comandos de PowerUp+. Ejemplos:
  - `read-game-save --in-file <project.bin> --name "MLB 2008"`
  - `write-game-save --roster-id N --mlb2008-mod --out-dir <carpeta mods>`
- **Verificado:**
  - **Ida y vuelta exacta.** Importar el roster de 2008 y exportarlo sin cambios da, tras la conversión del juego, un `game_data.pack` **idéntico byte a byte** al original. Se comprobó por la línea de comandos, con la API de la interfaz levantada desde un clon limpio con el parche (`-Ui`) y en el runtime.
  - **En el juego.** MLB Life muestra a los Red Sox de 2008 (Beckett, ERA 3.27, 97 mph).
  - **Las ediciones llegan.** Un cambio hecho en PowerUp+ (el ERA de Beckett, de 3.27 a 1.23) llega al juego.
  - **Roster 2025.** El de PowerUp+ exportado como mod de 2008 da las mismas caras que el `.dat` directo (1.082 de 1.082), sin voces vacías y sin copias All-Star. Pasa de 1.281 a 1.209 registros.
    - La piel/ojos coincide en 792 jugadores.
    - Los otros 288 tienen caras genéricas "sin color de ojos": el modelo de PowerUp descarta ese bit porque esas caras no lo muestran.
  - **Runtime == Python.** La conversión del runtime es igual byte a byte a la de `powerup_to_2008.py` en los tres casos: roster de 2008 ida y vuelta, 2025 exportado y 2025 en `.dat` directo.
- **Límites:**
  - El roster 2025 de PowerUp+ pone a algunos jugadores traspasados en dos equipos regulares (Ramón Laureano en BAL y SD). Esos quedan como dos registros, igual que en sus datos.
  - La elección de voz por parecido de nombre es aproximada (Aaron Judge → la de Derek Jeter). Es el mismo criterio que usa PowerUp+ en 2007.
  - El editor de PowerUp+ muestra la lista de caras de 2007. Las caras que solo existen en 2008 se ven como la de 2007 anterior, pero se exportan con su número exacto si no se tocan.

**A mano:** `python tools/roster/powerup_to_2008.py <roster.dat> <game_data.pack original> <carpeta del mod>` genera el mod con `project/game_data.pack`. El resultado es byte a byte igual al del runtime y lee `roster_ids` del `mod.toml` que esté junto a la carpeta `roster`. `tools/projectbin.py extract` saca el `game_data.pack` original.

La tabla de campos del runtime (`runtime/src/roster_fields.inc`, parche `wiicompiled-mods`) sale de `powerup_to_2008.py --emit-cpp`, y la de PowerUp+ (`PowerPros2008Fields.cs`) de `--emit-cs`. Las dos se generan desde `GSPlayer.cs` de PowerUp.

## Pendiente

- Averiguar qué controla el rasgo oculto `0x75.4`.
- Una lista de caras de 2008 para el editor de PowerUp+.
