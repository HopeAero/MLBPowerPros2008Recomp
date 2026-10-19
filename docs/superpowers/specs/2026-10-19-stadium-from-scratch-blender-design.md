# Estadios desde cero en Blender — diseño (fase 3)

Fecha: 19 de octubre de 2026. Estado: borrador para aprobar.

## Por qué

Hoy un estadio propio entra en el juego **dentro de un estadio existente**: el complemento vacía las piezas de la base y les mete la geometría del modelo. Lo que se hereda de la base sin entenderlo ha roto cosas sin avisar (texturas que el juego estiraba, árbol A, público y banderas de la base). El usuario eligió el camino de **generar los packs enteros desde cero**.

La fase 1 hizo el escritor: escribe los dos packs desde una descripción del estadio, probado con los 40 estadios del juego y con un estadio mínimo inventado que se juega (`docs/superpowers/specs/2026-10-18-stadium-pack-from-scratch-design.md`). La fase 2 averiguó con el visor qué hacen los flags de colocación (`docs/ingenieria-inversa.md`, «Qué hacen los campos, probado en el visor»). Esta fase 3 lleva todo eso a Blender: modelar un estadio sin base y llevarlo al juego. La fase 4 será el Polo Grounds hecho así.

El usuario respondió «continua» a la propuesta de pasar a la fase 3. Las decisiones de este diseño son del agente salvo donde se dice otra cosa, y van marcadas para revisarlas.

## Objetivos

1. **Colección «desde cero».** Una acción del panel Power Pros crea una colección vacía de estadio desde cero. Cada objeto de Blender lleva su papel.
   - **Agrupación:** el exportador junta en una misma pieza del pack los objetos con el mismo papel y la misma casilla «Ocultar desde arriba», y parte las piezas a las 30 texturas y por tamaño (los mismos límites de malla del exportador actual).
   - Así un modelo con cientos o miles de objetos (el Polo Grounds de la fase 4) queda en pocas piezas.
   - Las piezas se escriben en orden de dibujo (suelo, opacas, transparentes, aditivas) y se quedan por debajo de 127, para usar el árbol A de una hoja, que respeta ese orden. Si aun así pasan de 127, el exportador avisa.
   - Decisión del agente.
2. **Papel de cada pieza.** En el panel, cada objeto tiene un **papel** que el complemento traduce a los flags del juego:

   | Papel | Flags | Qué hace en el juego (fase 2) |
   |---|---|---|
   | Suelo | `0x04810000` | capa de fondo, se dibuja antes que el resto |
   | Grada (opaco) | `0x00000000` | pieza normal |
   | Valla | `0x00001100` | tipo valla (efecto de juego) |
   | Transparente | `0x00060000` | mezcla alfa suave (cristales, mallas, carteles recortados) |
   | Brillo aditivo | `0x000A0800` | luces que suman (focos) |
   | Público tumbado | `0x00001600` | público animado por el juego |
   | Público vertical | `0x00001700` | público animado por el juego |
   | Banderas | `0x00000500` | las dibuja el sistema de banderas del juego |

   Además, una casilla **«Ocultar desde arriba»** añade el bit `0x80000000` (`NOUPCAM`) para lo que no debe verse con cámaras altas (edificios de fuera).
   Papel por defecto (decisión del agente): «Suelo» para objetos planos a la altura 0, «Grada» para el resto.
3. **Exportar desde cero:** el complemento construye la descripción del estadio (`stadium_model.Stadium`) desde la escena y la escribe con `stadium_write.write_stadium`. No lee ni copia ninguna pieza de un estadio del juego, con una excepción: las bases, el plato, el montículo y sus líneas.
   - **Bases del juego:** una casilla «Bases del juego», activada por defecto, copia del `project.bin` del jugador las piezas `BCL` del estadio de datos elegido (decisión del agente). Los jugadores se colocan siempre en el mismo sitio, y esas piezas encajan con él.
   - **Estadio de datos:** el estadio nuevo sigue necesitando un número de estadio del juego para partidas, menús, cielo y sonidos (`base` en `mod.toml`). Se elige en la exportación, como hoy.
   - **Solo «Estadio nuevo»:** un estadio desde cero no reemplaza a uno del juego. La raíz de la tabla de los estadios del juego está fijada en el REL, y el runtime solo la busca para los estadios añadidos. Reemplazar exigiría tocar el runtime. Decisión del agente.
4. **Conversión de cada objeto**, con las reglas que ya funcionan en «Convertir objetos en piezas»:
   - color del material y luz suave en el color de los vértices;
   - de dos caras en el mismo sitio queda una, con preferencia por la texturizada y vuelta hacia el campo;
   - separación de superficies pegadas;
   - texturas a RGB5A3 de hasta 256 píxeles, más su versión HD;
   - repeticiones del texmap calculadas.

   Un objeto con más de 30 texturas se reparte solo en varias piezas.
5. **Colisión:** la malla «Colisión» de la colección si existe; si no, se genera con `collision_gen` como hoy.
6. **«Ver en el juego»** funciona con una colección desde cero igual que con una importada.
7. **Foto y rótulo del menú** como en la exportación actual.

## No objetivos

- Focos de noche (`unk_20`), banquillos (`unk_24`), texturas precargadas y recorte por cámara con un árbol A propio. Van vacíos o con una sola hoja, como en el estadio mínimo. Más de 127 piezas usan el árbol espacial del escritor.
- Marcador con resultado en vivo y pantalla grande con imagen: hacen falta convenciones de textura sin descifrar. En esta fase un marcador o una pantalla son piezas normales («Grada» o «Transparente»).
- Que las banderas se vean en el visor: su sistema se para con el partido congelado (pendiente aparte).
- Cambiar el flujo actual de estadios basados en uno del juego: sigue igual.

## Anti-objetivos

- Que un papel prometa algo que el juego no hace. Cada papel de la tabla se comprueba en el juego durante la implementación. El que no funcione como se describe se quita o se corrige antes de entregar.
- Datos del juego en el repositorio o en el mod más allá de las piezas `BCL` del propio `project.bin` del jugador, escritas en su carpeta de mods como hoy.
- Un fallo silencioso. Lo que el escritor rechace (`StadiumWriteError`) sale en Blender como mensaje claro que nombra el objeto.

## Riesgos conocidos

- **Público animado:** el juego lo anima con su tipo, pero no se sabe aún qué hace con las texturas de un público propio (los fotogramas pueden venir de las texturas precargadas, que irán vacías). Si en el juego no se ve bien, el papel «Público» se queda con el tipo de grada (quieto) y se documenta.
- **Banderas:** el sistema de banderas puede necesitar la bandera en coordenadas locales y con una forma concreta. Si no funciona con objetos cualesquiera, el papel se quita y se documenta.

## Cómo se comprueba

1. **Prueba sin interfaz en Blender** (`tools/blender/test_scratch.py`): una colección desde cero hecha con formas sencillas, con un objeto por papel.
   - Exporta y vuelve a leer los packs con `stadium_read`.
   - Cada pieza lleva los flags de su papel.
   - Las bases del juego están.
   - La colisión existe.
   - `stadium_re` no da problemas.
   - El mod declara el estadio nuevo.
2. **En el juego:** esa colección en el visor y en un partido de 2 minutos sin errores. Cada papel se mira en el visor: público, banderas, transparencia, aditivo, suelo, valla y «Ocultar desde arriba».
3. **Las pruebas de siempre siguen pasando:** las del complemento y `tools/test_stadium_writer.py`.

## Lo que se deja al constructor

- Cómo se guardan el papel y la casilla en el objeto (propiedades personalizadas) y cómo se ven en el panel.
- Cómo se reparte el código: un módulo nuevo que construye la descripción desde la escena, reutilizando lo que haga falta de `convert.py`, `exporter.py` y `collision_gen.py`.
- Nombres de las piezas en el pack (los del objeto, en ASCII).
- La interfaz en español e inglés con `T(es, en)`, y la documentación en `docs/stadiums.md` y su espejo.
