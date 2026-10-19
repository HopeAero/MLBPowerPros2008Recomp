# Estadios en Blender

*English version: [en/stadiums.md](en/stadiums.md)*

El complemento **«Estadios de MLB Power Pros 2008»** abre en Blender cualquier estadio de tu juego: cada pieza en su sitio y con sus texturas, y la malla de colisión del campo.

También **exporta**: lo que cambies en las piezas y en la colisión vuelve al juego como un mod. Y puede meter en el juego **un estadio tuyo** hecho en SketchUp, Blender u otro programa (ver «Tu propio modelo»). El formato está descrito en `docs/ingenieria-inversa.md`, sección 5, «Estadios».

## Instalar

1. Necesitas **Blender 4.2 o más nuevo** ([blender.org](https://www.blender.org/download/)).
2. Consigue el complemento, un zip llamado `powerpros_stadium-<versión>.zip`. Para crearlo desde el repositorio: `python tools/blender/build_addon.py` (queda en `local/build/blender/`).
3. En Blender: **Editar > Preferencias > Obtener extensiones**. En el menú ⌄ de arriba a la derecha elige **Instalar desde disco…** y selecciona el zip.

**Idioma:** en las preferencias del complemento (**Editar > Preferencias > Complementos**), **Idioma / Language**: Automático (el idioma de la interfaz de Blender), Español o English. Se aplica sin reiniciar Blender.

## Importar un estadio

1. **Archivo > Importar > Estadio de Power Pros 2008**. También está en la barra lateral de la vista 3D (tecla N), pestaña **Power Pros**.
2. Elige la **carpeta del juego** instalado con el Setup (la que tiene `UserData`) o directamente el archivo `files/project.bin`.
3. En el panel de la derecha elige el **estadio**, deja marcada **Incluir colisión** si la quieres y pulsa **Importar estadio**.

Tarda unos segundos. No se extrae nada al disco: todo se lee del `project.bin` de tu juego.

## Qué obtienes

- **Una colección por estadio** (por ejemplo «Colorado Rockies — Coors Field») con **un objeto por pieza**, con el nombre que les dio el juego:
  - `DP_ground`: tierra;
  - `DP_MIP_turf`: césped;
  - `DP_stand_*`: gradas;
  - `DP_fence_*`: vallas;
  - `DP_score`: marcador;
  - `DP_auroravision`: pantalla;
  - `DP_Audience*`: público;
  - `DP_FLG_*`: banderas.
- **Escala real:** 1 unidad de Blender = 1 metro. Z hacia arriba, el plato en el origen y el center field hacia +Y.
- **Materiales como en el juego:** textura × color de vértice, sin iluminación. Las piezas con transparencia (`*_ALP_*`) se mezclan y las `*_ADD_*` suman luz. Para verlas así usa la vista **Material** (Z > Vista previa de material).
- **Texturas** empaquetadas dentro del `.blend`.
- **Colisión**, en la subcolección «… · Colisión», oculta al importar (actívala con el ojo):
  - es la malla con la que el juego decide por dónde rebota la pelota;
  - cada cara tiene un `surface_id` y un color por tipo (césped, tierra, montículo, vallas, gradas, postes de foul…; ver la tabla en `docs/ingenieria-inversa.md`).
- **Panel Power Pros (N):** la pieza seleccionada, su modelo original, el nodo de Maya y los flags de colocación. En la colisión, la superficie de la cara activa.

Las propiedades personalizadas `pp_*` de objetos y materiales guardan los datos originales que necesitará la exportación. No las borres si quieres volver a meter el estadio en el juego.

## Exportar al juego

1. Edita lo que quieras de las piezas importadas:
   - mover, girar o escalar piezas;
   - editar su malla (también añadir o quitar caras dentro de una pieza);
   - pintar o cambiar sus imágenes;
   - asignarles otros materiales del mismo estadio;
   - borrar piezas.
2. **Archivo > Exportar > Mod de estadio de Power Pros 2008**, o el botón **Exportar mod de estadio** del panel Power Pros.
3. Rellena el diálogo:
   - **Nombre del mod**;
   - **Carpeta de mods**: si importaste desde la carpeta del juego, ya viene puesta (`<juego>\mods`);
   - **Tipo**:
     - **Estadio nuevo** (predeterminado): se **añade** a la elección de estadio de Exhibición y no quita ninguno. Pide un **código** corto (letras minúsculas, números o _, por ejemplo `truist`) y un **nombre**. Usa los datos de juego del estadio importado: medidas, menú y partidas;
     - **Reemplazar un estadio**: sustituye al mismo estadio o a otro. Por ejemplo, puedes poner tu Coors Field editado en el hueco de Turner Field.
4. Activa el mod en **MODS** del launcher. Un estadio nuevo aparece en Exhibición al elegir estadio, después de los del juego (con la izquierda desde el primero llegas antes).

Qué hace al exportar:
- Escribe `mods\<nombre>\project\stadium_<código>.pack` y `stadium_<código>tbl.pack`, más un `mod.toml`.
- **Solo reescribe las piezas que cambiaste**; el resto queda idéntico al original. Lo nuevo se añade al final del pack.
- Una pieza borrada se queda sin geometría.
- Una imagen editada o nueva se guarda en el pack como textura RGB5A3. Si sus lados no son potencias de 2, se reescala a la más cercana, de 4 a 256 píxeles, como las del juego.
- **Texturas HD** (activado por defecto en el diálogo): además, la imagen se guarda a su tamaño, hasta 2048 píxeles y con 8 bits por color, en `mods\<nombre>\textures\stadium_<código>\`. El juego la usa en lugar de la del pack y le crea mipmaps, así que una imagen grande se ve nítida de cerca y limpia de lejos. La de 256 del pack queda para cuando no hay texturas de reemplazo.
- Si mueves una pieza, actualiza su caja y su esfera en la tabla, para que el juego no la oculte cuando entra en cámara, y rehace el árbol con el que el juego reparte las piezas.
- **Colisión:** si cambiaste la malla «Colisión» (moverla, editar sus caras o cambiar su superficie), la escribe entera y rehace el árbol de colisión del juego.
  - La **superficie** de cada cara sale del número final del nombre de su material: «Superficie 9» es valla. Para cambiar una cara de tipo, asígnale otro material «Superficie N».
  - La normal de cada cara apunta al lado que golpea la pelota: arriba en el suelo y hacia el campo en las vallas.
- **Límite de tamaño:** un pack de gráficos de 80 MB carga en un partido y uno de 96 MB no. El original pesa 1,1 MB. Si pasas de 80 MB, el exportador avisa. (En el Wii el tope era de unos 21 MB: el juego reserva un montón de memoria fijo; el recomp lo amplía.) Las texturas HD no cuentan: van aparte, fuera del pack.
- **Banderas:** si borras las banderas (`DP_FLG_*`) del estadio base, el exportador encoge su colocación hasta hacerlas invisibles. El juego las dibuja a partir de la colocación de la tabla, no de su malla, y sin prueba de profundidad: borradas o enterradas seguían viéndose.
- **Menú de elección de estadio** (estadio nuevo): con **Foto del menú** (activado por defecto) el exportador renderiza tu estadio desde detrás del plato, con una vista cenital difuminada a la izquierda como las fotos del juego, y escribe `project\env_stadium_menu_<código>.pack`. **Línea bajo la foto** es el texto verde de debajo, por ejemplo «Home of the Miami Marlins»; vacía, no sale ninguna. Antes se veían la foto y la línea del estadio base.

Aún no: objetos sueltos que no vinieron del juego. El exportador los ignora: pásalos antes por **Convertir objetos en piezas** (abajo).

Un material nuevo con una imagen sí se exporta: se crea en la pieza con esa imagen como textura nueva.

## Ver en el juego

Lo que se ve bien en Blender puede verse distinto en el juego. **Ver en el juego** (panel **Power Pros** de la vista 3D, bajo «Exportar mod de estadio») abre el juego de verdad directamente en el estadio que estás haciendo, sin empezar un partido:

- Exporta el estadio a una carpeta temporal como un mod aparte («Vista previa», estadio `pp_preview`). **No toca tu carpeta mods** ni tus ajustes.
- El juego abre sin launcher y, unos 8 segundos después, muestra el estadio con la cámara **donde está tu vista de Blender**. Sin jugadores ni marcador: el partido queda quieto, pero el público y las banderas del estadio siguen moviéndose.
- **Teclas:** W/A/S/D para moverte, Q/E para bajar y subir, Shift para ir rápido, botón derecho del ratón + ratón para mirar, rueda para la velocidad. **1**, **2** y **3** ponen las cámaras reales del partido (bateador, jugada, lejana; en ellas se ven los jugadores quietos); **F** vuelve a la cámara libre y **H** oculta la ayuda.
- Pulsar el botón otra vez cierra el juego que abrió antes y lo abre con tus cambios.
- Usa su propia caché (unos 400 MB en `UserData/Cache/preview`), así que no rehace la del juego normal. Solo carga el estadio que estás haciendo: tus otros mods no se activan.
- Hace falta haber importado el estadio desde la carpeta del juego instalado (la que tiene `WiiCompiled.exe`); si no, el botón lo dice.

## Tu propio modelo (SketchUp, glTF, OBJ…)

Puedes llevar al juego un estadio que no existe en él.

1. **Importa un estadio del juego** como base. Pon el que quieras que aporte sus datos de juego (medidas, menú, partidas); por ejemplo, AT&T Park para un estadio de los Giants.
2. **Importa tu modelo** en la misma escena: Archivo > Importar > glTF, OBJ, FBX…
   - **SketchUp** (`.skp`): conviértelo antes a `.glb`. Por ejemplo, con [OpenSKP](https://github.com/iamahsanmehmood/openskp) (código abierto, MIT): `pip install openskp pillow` y luego `python -c "from openskp import SkpFile; from openskp.export import glb; s = SkpFile.open('estadio.skp'); s.parse(); glb.export(s, 'estadio.glb', textures=True)"`. Sale en milímetros: escálalo a 0,001.
   - Borra lo que no sea el estadio, como fotos aéreas, terreno lejano o personas.
3. **Colócalo como el estadio del juego:**
   - en metros;
   - con el plato en el origen, el suelo a la altura 0 y la segunda base hacia +Y.
   - Compruébalo con las bases de tu modelo: del plato a primera y a tercera hay 27,43 m (90 pies), y a la goma 18,44 m. Muchos modelos no están a escala: si tus bases están más lejos, escala todo el modelo hasta que cuadren.
   - Es importante porque el juego no lee del estadio dónde están las bases ni la caja de bateo: pone a los jugadores siempre en el mismo sitio, y tu modelo tiene que coincidir con él.
4. Selecciona los objetos de tu modelo y pulsa **Convertir objetos en piezas** (panel Power Pros, tecla N):
   - El suelo del campo va a la pieza de suelo del estadio y todo lo demás a una pieza de gradas, porque el juego solo dibuja las piezas que ya tiene el estadio.
   - El color de cada material y un sol suave se guardan en el color de los vértices, porque el juego dibuja los estadios sin luz.
   - Cada cara queda una sola vez y mirando al campo. De dos caras en el mismo sitio (las dos caras de una pared, o una textura pintada sobre una cara sin material, como las exporta SketchUp) queda la que tiene textura y, si ninguna o las dos la tienen, la que mira al campo. La que queda se gira hacia el campo.
   - Las superficies pegadas a menos de 25 cm de otra paralela, como ventanas o letreros, se separan hasta 25 cm, para que no parpadeen.
   - Con **Quitar el estadio original** (activado por defecto), las piezas del estadio base desaparecen y solo queda tu modelo. Las piezas con la propiedad `pp_keep` se conservan, por ejemplo una pieza de público en la que hayas puesto tu propio público.
   - Las bases, el plato y el montículo del juego (piezas `BCL`) se conservan siempre: los jugadores se colocan sobre ellos, y los de un modelo rara vez coinciden (el plato del Mario Stadium medía 1,2 m).
   - Cada pieza recibe como mucho 30 texturas: las del modelo se reparten entre las gradas del estadio base.
5. Pulsa **Generar colisión**. Crea la malla «Colisión» de tu estadio:
   - el suelo (césped si es verde, tierra si no) y el montículo;
   - las vallas, siguiendo la pared más cercana al campo en todas direcciones;
   - las paredes invisibles sobre las vallas, como las del juego: en territorio fair (1, el home run) y en foul (0);
   - los postes de foul donde las líneas cortan las vallas;
   - las gradas, simplificadas a unos 5.000 triángulos, y un límite exterior detrás del outfield.

   Revísala: está oculta, actívala con el ojo. Si algo no cuadra, puedes editarla a mano.
6. **Exporta** como estadio nuevo.

Límites:
- La colisión puede tener hasta 65.535 triángulos.
- Cada pieza admite hasta 30 texturas, contando las suyas originales; es lo máximo que tiene una pieza en los estadios del juego. «Convertir objetos en piezas» reparte las texturas del modelo entre las gradas del estadio base para no pasar de ahí, y el exportador se niega a escribir una pieza con más. Con 82 en una pieza el juego leía fuera de una tabla, el estadio salía sin vallas y el partido acababa cerrándose.
- En el pack, las texturas pasan a RGB5A3 y potencias de 2 de hasta 256 píxeles. Con **Texturas HD**, el juego usa la imagen a su tamaño (hasta 2048 píxeles).
- El generador de colisión supone un campo de béisbol normal: suelo a la altura 0, el centro del campo unos 45 m por delante del plato y paredes alrededor.

## Cómo hacer un modelo que funcione bien en el juego

Lo aprendido al meter el Polo Grounds, un modelo de SketchUp descargado (14 y 15 de octubre de 2026). Ese modelo, hecho para verse en SketchUp, dio muchos problemas en el juego; uno pensado para el juego desde el principio se ahorra casi todos.

**Lo que el juego necesita (comprobado):**
- **El campo en su sitio:** plato en el origen, bases a 90 pies, goma a 60 pies 6 pulgadas y montículo con el centro a 18,51 m. El juego pone a los jugadores siempre ahí; tu modelo no lo cambia.
- **Escala real en metros.** Las vallas quedan donde estén en tu modelo: si el modelo está a otra escala, las distancias cambian.
- **Colores en el color de los vértices o en texturas:** el juego no ilumina los estadios. El color de un material sin textura solo llega si el convertidor lo guarda en los vértices.
- **Nada de superficies pegadas:** dos caras paralelas a menos de unos centímetros (una ventana sobre una pared, un letrero, una textura de asientos sobre la grada) parpadean a 100–150 m. Dos caras en el mismo plano parpadean siempre; las separadas unos centímetros no deberían en el recomp, que usa profundidad de 32 bits, pero se vio con el Polo Grounds y no está aislado. Deja al menos 25 cm o haz el detalle con textura.
- **Una cara por superficie:** sin caras dobles en el mismo sitio. En SketchUp, pinta solo la cara delantera.
- **Texturas:** con **Texturas HD** puedes usar imágenes de hasta 2048 píxeles: el juego les crea mipmaps y no se ven ruidosas a distancia. Sin texturas HD, quédate en 32 a 256 píxeles, como las del juego.
- **Sin geometría fina:** filas de sillas, barandillas o mástiles de pocos cm miden menos de un píxel a distancia y salen rotos. Los asientos son una rampa con textura de asientos, y los mástiles deben tener al menos unos 30 cm.
- **Pocos triángulos:** un estadio del juego tiene unos 10.000. Uno de 40.000 funciona en PC, pero cuesta más dibujarlo y que se vea limpio.

**Recomendación para hacerlo desde cero (en Blender):**
1. Importa el estadio base del juego. Su campo, sus medidas y sus piezas son tu plantilla de escala y de estilo.
2. Modela las gradas como rampas lisas (unos 18°, como las del juego) y aplícales una textura de asientos pequeña que se repita.
3. Para el público, coloca caras sobre las rampas y dentro de las piezas de público del estadio base (`DP_AudienceF_*`, tumbadas sobre la grada, y `DP_AudienceS_*`, carteles verticales), con sus mismos materiales. Las del juego repiten la textura cada 6 m de ancho y cada 3–4 m de alto.
4. Paredes, techos y fachadas: cajas sencillas con colores lisos o texturas pequeñas; detalles grandes, nada de piezas de centímetros.
5. Ajusta las vallas a las medidas que quieras (líneas, jardines intermedios, centro) y pon los letreros de distancia a 25 cm de la pared.
6. Convierte, genera la colisión, revisa las vallas y exporta como estadio nuevo.

**Sin resolver todavía:**
- El público puesto en las piezas de público de AT&T no se llegó a ver en un partido (en Blender sí). Falta saber cuándo dibuja el juego esas piezas.
- El efecto de los flags de colocación (`0x40000000` en las gradas altas detrás del plato, `0x80000000`/`0x20000000` en las piezas `NOUPCAM`/`EVC`, `0x1600`/`0x1700` en el público). El convertidor no pone nada en piezas con los tres primeros.
- Si la pelota y el home run usan la colisión nueva o datos del estadio base: un home run al centro del Polo Grounds cayó a 400 pies, que es lo que medían allí sus vallas.

## Reglas

Los `.blend` con estadios importados contienen modelos y texturas del juego. Úsalos para tu mod, pero **no los compartas**. Un mod de estadio repartirá solo sus `.pack` modificados, igual que los uniformes (`docs/mods.md`).

## Para desarrolladores

- `python tools/blender/powerpros_stadium/selftest.py`: autotest de los lectores, sin datos del juego.
- `python tools/stadium_re.py check <project.bin>`: comprueba los 47 packs de estadio. `info` y `stats` dan resúmenes e histogramas de lo que falta por descifrar.
- **Prueba completa dentro de Blender** (Blender 4.2 LTS portátil en `local/tools/`):

  ```
  set BLENDER_USER_RESOURCES=<carpeta temporal>
  blender --background --factory-startup --python tools/blender/test_import.py -- <zip> <project.bin> col <carpeta de salida>
  ```

  Instala el zip, importa Coors Field, compara piezas, caras, texturas y colisión con lo que dicen los packs, y renderiza una vista cenital y otra desde el plato.
- **Exportación de ida y vuelta** (`tools/blender/test_export.py`, mismos argumentos con la carpeta de mods al final):
  1. importa Coors Field y le hace cuatro cambios: sube `DP_stand_C` 15 m, sube la geometría de `DP_score` 10 m, borra una bandera y tiñe la imagen de `DP_ground`;
  2. exporta el mod `BlenderTest`;
  3. relee los packs y comprueba cada cambio, también que la imagen teñida sale como textura HD con el nombre que busca el juego.

  El mod resultante, cargado en un partido, muestra los cuatro cambios.
  4. vuelve a importar Coors Field con colisión, sube sus vallas (superficie 9) 3 m, cambia una cara a poste de foul y comprueba la tabla escrita: superficies, sentido de las caras y los dos árboles.
- **Modelo propio** (`tools/blender/test_convert.py`, mismos argumentos que la exportación): construye un estadio de prueba con formas simples (césped, tierra, una valla circular y gradas), lo convierte en piezas de Coors Field, genera su colisión, lo exporta como estadio nuevo y comprueba lo escrito.
