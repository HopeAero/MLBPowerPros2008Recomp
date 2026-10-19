# Estadios en Blender

*English version: [en/stadiums.md](en/stadiums.md)*

El complemento **«Estadios de MLB Power Pros 2008»** abre en Blender cualquier estadio de tu juego: cada pieza en su sitio y con sus texturas, y la malla de colisión del campo.

También **exporta**: lo que cambies en las piezas y en la colisión vuelve al juego como un mod. Y puede meter en el juego **un estadio tuyo** hecho en SketchUp, Blender u otro programa, escrito entero desde tu modelo (ver «Tu propio modelo»). El formato está descrito en `docs/ingenieria-inversa.md`, sección 5, «Estadios».

## Instalar

1. Necesitas **Blender 4.2 o más nuevo** ([blender.org](https://www.blender.org/download/)).
2. Consigue el complemento, un zip llamado `powerpros_stadium-<versión>.zip`: el último, `powerpros_stadium-0.8.2.zip`, está en [Releases](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases) (v0.1.2). Para crearlo desde el repositorio: `python tools/blender/build_addon.py` (queda en `local/build/blender/`).
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
  - va en **un objeto por tipo** («Colisión · Vallas», «Colisión · Gradas», «Colisión · Postes de foul»…), con su material «Superficie N · nombre» del mismo color que en «Ver en el juego» (tabla de tipos en `docs/ingenieria-inversa.md`);
  - en el panel Power Pros, la caja **Colisión** muestra los caras de cada tipo, con un ojo para verlo u ocultarlo y los botones **Todo** y **Nada** (complemento 0.8.2).
- **Panel Power Pros (N):** la pieza seleccionada, su modelo original, el nodo de Maya y los flags de colocación. En la colisión, el tipo de la cara activa.

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
- **Colisión:** si cambiaste la colisión (mover un objeto, editar sus caras, cambiar su tipo, borrar un objeto o añadir uno), junta todos los objetos de colisión en la del juego, la escribe entera y rehace su árbol. Los puntos que comparten se unen.
  - El **tipo** de cada cara sale del número del nombre de su material: «Superficie 9 · Vallas» (o «Superficie 9», en archivos anteriores) es valla. Para cambiar una cara de tipo, asígnale otro material «Superficie N».
  - **Tu propia colisión:** selecciona cualquier malla y pulsa **Convertir en colisión** (caja Colisión del panel). Eliges el tipo (valla, gradas, pared invisible de home run…) y la malla pasa a la colección de colisión: deja de dibujarse y la pelota y los jugadores chocan con ella. Sirve para cerrar huecos de la colisión generada o para hacerla a mano. Comprueba el resultado con **C** en «Ver en el juego».
  - La normal de cada cara apunta al lado que golpea la pelota: arriba en el suelo y hacia el campo en las vallas.
- **Límite de tamaño:** un pack de gráficos de 80 MB carga en un partido y uno de 96 MB no. El original pesa 1,1 MB. Si pasas de 80 MB, el exportador avisa. (En el Wii el tope era de unos 21 MB: el juego reserva un montón de memoria fijo; el recomp lo amplía.) Las texturas HD no cuentan: van aparte, fuera del pack.
- **Banderas:** si borras las banderas (`DP_FLG_*`) del estadio base, el exportador encoge su colocación hasta hacerlas invisibles. El juego las dibuja a partir de la colocación de la tabla, no de su malla, y sin prueba de profundidad: borradas o enterradas seguían viéndose.
- **Menú de elección de estadio** (estadio nuevo): con **Foto del menú** (activado por defecto) el exportador renderiza tu estadio desde detrás del plato, con una vista cenital difuminada a la izquierda como las fotos del juego, y escribe `project\env_stadium_menu_<código>.pack`. **Línea bajo la foto** es el texto verde de debajo, por ejemplo «Home of the Miami Marlins»; vacía, no sale ninguna. Antes se veían la foto y la línea del estadio base.

En un estadio importado, los objetos sueltos que no vinieron del juego se ignoran: pásalos antes por **Convertir objetos en piezas** («Otra vía», abajo), o haz el estadio desde cero («Tu propio modelo»).

Un material nuevo con una imagen sí se exporta: se crea en la pieza con esa imagen como textura nueva.

## Ver en el juego

Lo que se ve bien en Blender puede verse distinto en el juego. **Ver en el juego** (panel **Power Pros** de la vista 3D, bajo «Exportar mod de estadio») abre el juego de verdad directamente en el estadio que estás haciendo, sin empezar un partido:

- Exporta el estadio a una carpeta temporal como un mod aparte («Vista previa», estadio `pp_preview`). **No toca tu carpeta mods** ni tus ajustes.
- El juego abre sin launcher y, unos 8 segundos después, muestra el estadio con la cámara **donde está tu vista de Blender**. Sin jugadores ni marcador: el partido queda quieto, pero el público y las banderas del estadio siguen moviéndose.
- **Teclas:** W/A/S/D para moverte, Q/E para bajar y subir, Shift para ir rápido, botón derecho del ratón + ratón para mirar, rueda para la velocidad. **1**, **2** y **3** ponen las cámaras reales del partido (bateador, jugada, lejana; en ellas se ven los jugadores quietos); **F** vuelve a la cámara libre, **C** muestra la colisión y **H** oculta la ayuda.
- **Colisión (C):** dibuja encima la colisión del estadio, la que usan la pelota y los jugadores, como líneas que se ven a través de todo, con un color por tipo de superficie: paredes invisibles de foul y de home run, límite exterior, césped, tierra, montículo, vallas, gradas, techos, postes de foul y backstop. Abajo a la izquierda, una leyenda con los triángulos de cada tipo y una casilla para ver u ocultar cada uno (clic izquierdo), con «Todo» y «Nada». Las paredes invisibles (foul, home run y límite exterior) empiezan ocultas, porque miden 70 m y lo tapan todo. Sirve con la cámara libre y con las del partido. Así se ve si las vallas y paredes invisibles coinciden con tu modelo.
- Pulsar el botón otra vez cierra el juego que abrió antes y lo abre con tus cambios.
- Usa su propia caché (unos 400 MB en `UserData/Cache/preview`), así que no rehace la del juego normal. Solo carga el estadio que estás haciendo: tus otros mods no se activan.
- Hace falta haber importado el estadio, o creado el estadio desde cero, eligiendo la carpeta del juego instalado (la que tiene `WiiCompiled.exe`); si no, el botón lo dice.
- Mientras la ventana del juego está activa, recibe tus teclas: si escribes en otro sitio sin cambiar de ventana, la cámara se mueve.
- **Para qué sirve:** ver lo que el juego hace con tu modelo sin jugar un partido. Con el Polo Grounds (18 de octubre de 2026) enseñó tres fallos que en Blender no se veían: texturas perdidas al convertir, gradas grises y torres a rayas (ver «Límites de piezas y texturas»). Los tres están corregidos en el complemento 0.7.3.

## Tu propio modelo (SketchUp, glTF, OBJ…)

Puedes llevar al juego un estadio que no existe en él. El complemento escribe el estadio **entero desde tu modelo** (desde la versión 0.8.0): no hace falta vaciar ni rellenar un estadio del juego. Del juego solo se toman unas pocas cosas que tu modelo no puede aportar (ver «Qué se toma del juego»).

**Traer el modelo a Blender:**
- **Impórtalo** en la misma escena: Archivo > Importar > glTF, OBJ, FBX…
   - **SketchUp** (`.skp`): conviértelo antes a `.glb`. Por ejemplo, con [OpenSKP](https://github.com/iamahsanmehmood/openskp) (código abierto, MIT): `pip install openskp pillow` y luego `python -c "from openskp import SkpFile; from openskp.export import glb; s = SkpFile.open('estadio.skp'); s.parse(); glb.export(s, 'estadio.glb', textures=True)"`. Sale en milímetros: escálalo a 0,001.
     - OpenSKP da el color de la capa (`Layer0`, rojo) a las caras con el material por defecto de SketchUp cuando no resuelve el material de su grupo. Si ves caras rojas que en SketchUp no lo son, dales en Blender su color de SketchUp antes de convertir. En el Polo Grounds eran el verde `[Color_G19]` de los techos.
     - OpenSKP también escribe los colores sRGB como si fueran lineales: en Blender se ven más claros que en SketchUp.
   - Borra lo que no sea el estadio, como fotos aéreas, terreno lejano o personas.
- **Colócalo como el estadio del juego:**
   - en metros;
   - con el plato en el origen, el suelo a la altura 0 y la segunda base hacia +Y.
   - Compruébalo con las bases de tu modelo: del plato a primera y a tercera hay 27,43 m (90 pies), y a la goma 18,44 m. Muchos modelos no están a escala: si tus bases están más lejos, escala todo el modelo hasta que cuadren.
   - Es importante porque el juego no lee del estadio dónde están las bases ni la caja de bateo: pone a los jugadores siempre en el mismo sitio, y tu modelo tiene que coincidir con él.

**Crear el estadio:**
1. Panel **Power Pros** → **Nuevo estadio desde cero**. Elige la carpeta del juego (o `files/project.bin`) y un **estadio de datos**: de él salen las partidas, el menú, el cielo y los sonidos del estadio nuevo, pero ninguna de sus piezas.
2. Se crea una colección, «Estadio desde cero», que ya trae **las bases, el plato y el montículo del juego** en su sitio: ajusta tu modelo a ellos.
3. Mete tus objetos (mallas, curvas, textos…) en esa colección, ya colocados.
4. Con un objeto seleccionado, el panel muestra su **papel** y la casilla **Ocultar en bateo y desde arriba**.
5. Pulsa **Generar colisión** y revísala (ver «Colisión del estadio»).
6. **Exportar mod de estadio** con la colección seleccionada: siempre como estadio nuevo, con su código y nombre. Pruébalo antes con **Ver en el juego**.

**Papeles**
| Papel | Qué hace en el juego |
|---|---|
| Automático | Suelo si el objeto es plano a la altura 0; grada si no. |
| Suelo | Se dibuja antes que todo lo demás (capa de fondo). |
| Grada | Pieza normal. |
| Valla | Pieza normal; la valla de verdad para la pelota es la colisión. |
| Público tumbado / Público vertical | El juego dibuja encima **su propio público animado** (las personas de las gradas del estadio de datos). La textura del objeto no se usa: las UV deciden cuánta gente cabe; cada repetición son 8 personas (4 × 2). Tumbado es para gradas inclinadas, vertical para carteles de pie. |
| Banderas | Cada objeto es una bandera que el juego mueve con el viento. Solo cuenta su tamaño (lo más ancho y lo alto) y su primera textura; el mástil va en el centro del objeto. |
| Transparente | Mezcla con el alfa de la textura: cristales, sombras, bordes suaves. |
| Brillo aditivo | Suma su color a lo que hay detrás: luces, reflejos. |

- **Ocultar en bateo y desde arriba:** la pieza no sale en la vista del bateador ni en las cámaras altas, como los edificios de fuera en los estadios del juego. Sirve para lo que está lejos y estorbaría.
- Los objetos con el mismo papel (y la misma casilla) se juntan en una pieza; con más de 30 texturas se parte en varias. Cada bandera va sola.
- **Bases, plato y montículo del juego:** «Nuevo estadio desde cero» los pone en la colección (piezas `BCL` del estadio de datos, bloqueadas), para que ajustes tu modelo a ellos: los jugadores se colocan siempre ahí. Se exportan tal cual y en su sitio. Si tu modelo trae los suyos, borra los de tu modelo (se pisarían con los del juego) o borra la pieza del juego que no quieras: solo se exportan las que sigan en la colección (complemento 0.8.1).
- **Suelo por capas:** el juego dibuja el suelo sin prueba de profundidad, en orden. El exportador pone las capas de abajo arriba (por la altura media de cada material), así la tierra del cuadro, encima del césped, se ve. Antes del 0.8.1 la tierra del Polo Grounds quedaba tapada por un plano de césped 4 cm más abajo.
- **Colisión:** pulsa **Generar colisión** (abajo, «Colisión del estadio»), o la genera el exportador si no la hay.

**Comprobado en el juego** (19 de octubre de 2026, colección de prueba con todos los papeles): se ve en el visor desde la vista del bateador, desde arriba y desde las gradas, y se juega un partido de 2 minutos a 60 FPS sin errores. El público se anima, las banderas giran con el viento, lo transparente se ve a través y el brillo suma luz.

**El Polo Grounds desde cero** (19 de octubre de 2026): el modelo de SketchUp, tal cual (456 objetos, 45.000 triángulos), colocado con sus propias bases y con AT&T Park como estadio de datos, queda en 5 piezas y 6.953 triángulos de colisión. Se ve en el visor con sus texturas desde todas las cámaras y se juega un partido de 2 minutos a 60 FPS sin errores. Al modelo solo se le hizo pasar las caras planas del campo a un objeto aparte y quitar su montículo y sus bases, que se pisaban con los del juego.
- **Consejo:** si el campo está unido a las gradas en un mismo objeto, sepáralo (modo edición, seleccionar el césped, P > Selección). Así «Automático» lo hace suelo y se dibuja como capa de fondo.
- Las banderas del modelo salen como geometría quieta: para que el viento las mueva, dales el papel Banderas. Las gradas salen vacías: el papel Público pone la gente animada del juego.

**Límites:**
- El público no puede llevar imagen propia: el juego anima siempre sus personas.
- Una bandera es siempre un rectángulo: su forma en Blender no cuenta.
- Hasta 127 piezas: con más, el orden de dibujo no está asegurado (el exportador avisa).
- Lo que el juego hace de noche (focos, luces de las gradas) no se ha probado.

**Límites de piezas y texturas** (también en «Otra vía»):
- La colisión puede tener hasta 65.535 triángulos.
- Cada pieza admite hasta 30 texturas, contando las suyas originales; es lo máximo que tiene una pieza en los estadios del juego. El exportador parte las piezas para no pasar de ahí y se niega a escribir una pieza con más. Con 82 en una pieza el juego leía fuera de una tabla, el estadio salía sin vallas y el partido acababa cerrándose.
- En el pack, las texturas pasan a RGB5A3 y potencias de 2 de hasta 256 píxeles. Con **Texturas HD**, el juego usa la imagen a su tamaño (hasta 2048 píxeles).
- Las texturas se pueden repetir todo lo que haga falta. Cada textura del pack dice cuántas veces se repite en horizontal y en vertical. Con 1, el juego no la repite: estira el borde. El exportador calcula esos números con las UV de tus caras y antes quita a cada cara las repeticiones enteras que sobran; el dibujo no cambia. Hasta la versión 0.7.2 los copiaba de la primera textura de la pieza: el Polo Grounds sobre AT&T Park salió con las gradas grises y las torres a rayas, y sobre Coors Field bien, por suerte.
- El generador de colisión supone un campo de béisbol normal: suelo a la altura 0, el centro del campo unos 45 m por delante del plato y paredes alrededor.

## Colisión del estadio

**Generar colisión** (panel Power Pros) crea la colisión de tu estadio, un objeto por tipo. Sustituye la que hubiera, también la hecha con «Convertir en colisión».
- el suelo (césped si es verde, tierra si no) y el montículo;
- las vallas, siguiendo la pared más cercana al campo en todas direcciones;
- las paredes invisibles sobre las vallas, como las del juego: en territorio fair (1, el home run) y en foul (0);
- los postes de foul donde las líneas cortan las vallas;
- las gradas, simplificadas a unos 5.000 triángulos, y un límite exterior detrás del outfield.

En un estadio desde cero no cuentan las banderas, lo transparente ni el brillo aditivo; sí las curvas y los modificadores. Revísala: está oculta, actívala con el ojo o con **Todo** en la caja Colisión. Si algo no cuadra, edítala a mano o añade piezas con **Convertir en colisión**, y compruébala con **C** en «Ver en el juego».

## Otra vía: dentro de un estadio del juego

Antes de la versión 0.8.0 era la única forma: el modelo entraba **dentro de un estadio del juego** (AT&T Park, Coors Field…), cuyas piezas se vaciaban y se rellenaban con tu geometría. Sigue disponible, pero «Tu propio modelo» da mejores resultados: con esta vía el modelo hereda del estadio base sus piezas de público y banderas, el orden de dibujo y algunos ajustes que no se ven.

1. **Importa un estadio del juego** como base (aporta sus datos de juego: medidas, menú, partidas).
2. Importa y coloca tu modelo como arriba.
3. Selecciona los objetos de tu modelo y pulsa **Convertir objetos en piezas** (panel Power Pros, tecla N):
   - El suelo del campo va a la pieza de suelo del estadio y todo lo demás a una pieza de gradas, porque el juego solo dibuja las piezas que ya tiene el estadio.
   - El color de cada material y un sol suave se guardan en el color de los vértices, porque el juego dibuja los estadios sin luz.
   - Cada cara queda una sola vez y mirando al campo. De dos caras en el mismo sitio (las dos caras de una pared, o una textura pintada sobre una cara sin material, como las exporta SketchUp) queda la que tiene textura y, si ninguna o las dos la tienen, la que mira al campo. La que queda se gira hacia el campo.
   - Las superficies pegadas a menos de 25 cm de otra paralela, como ventanas o letreros, se separan hasta 25 cm, para que no parpadeen.
   - Con **Quitar el estadio original** (activado por defecto), las piezas del estadio base desaparecen y solo queda tu modelo. Las piezas con la propiedad `pp_keep` se conservan, por ejemplo una pieza de público en la que hayas puesto tu propio público.
   - Las bases, el plato y el montículo del juego (piezas `BCL`) se conservan siempre: los jugadores se colocan sobre ellos, y los de un modelo rara vez coinciden (el plato del Mario Stadium medía 1,2 m).
   - Cada pieza recibe como mucho 30 texturas: las del modelo se reparten entre las gradas del estadio base.
   - Si el modelo trae una textura pintada sobre una cara sin material, en las mismas esquinas (SketchUp lo hace), se queda la cara con textura.
   - Las piezas convertidas se ven en Blender como en el juego: textura × color de vértice, sin luz. Usa la vista **Material** (Z > Vista previa de material).
4. Pulsa **Generar colisión** (ver «Colisión del estadio») y revísala.
5. **Exporta** como estadio nuevo.

## Qué se toma del juego

Un estadio desde cero se escribe entero desde tu modelo: piezas, texturas, materiales, colisión y la tabla del estadio. Del **estadio de datos** que eliges al crearlo solo se toma lo que el juego necesita y tu modelo no puede aportar:
- **las bases, el plato y el montículo** (piezas `BCL`), porque el juego pone a los jugadores siempre en el mismo sitio;
- **las texturas precargadas** del marcador, la pantalla y el público: el juego anima a su público solo a través de ellas;
- **la foto del menú, las partidas, el cielo y los sonidos** del estadio.

El juego tiene una lista fija de 40 estadios: un estadio nuevo usa el número de su estadio de datos para las partidas, el menú y los datos de juego, así que desactivar el mod nunca rompe una partida (ese partido vuelve al estadio de datos). Cómo se descifró cada campo de los packs: `docs/ingenieria-inversa.md`, «Escribir packs desde cero» y «Qué hacen los campos».

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

**Recomendación para modelarlo en Blender:**
1. Crea el estadio con **Nuevo estadio desde cero**: las bases, el plato y el montículo del juego te dan la escala. Si quieres comparar con un estadio real del juego, impórtalo en otra escena.
2. Modela las gradas como rampas lisas (unos 18°, como las del juego) y aplícales una textura de asientos pequeña que se repita.
3. Para el público, pon caras sobre las rampas con el papel **Público tumbado** (gradas) o **Público vertical** (carteles de pie): el juego dibuja ahí su gente animada. Las del juego repiten la textura cada 6 m de ancho y cada 3–4 m de alto.
4. Paredes, techos y fachadas: cajas sencillas con colores lisos o texturas pequeñas; detalles grandes, nada de piezas de centímetros. Lo lejano, con **Ocultar en bateo y desde arriba**.
5. Ajusta las vallas a las medidas que quieras (líneas, jardines intermedios, centro) y pon los letreros de distancia a 25 cm de la pared.
6. Genera la colisión, revisa las vallas con **C** en «Ver en el juego» y exporta.

**Resuelto desde entonces** (fases 2 y 3, 18-19 de octubre de 2026; detalles en `docs/ingenieria-inversa.md`):
- El público puesto en las piezas de público de AT&T no se veía porque el juego anima su público solo a través de las texturas precargadas del estadio: el papel **Público** lo usa.
- Los flags de colocación: `0x1600`/`0x1700` son el público animado, `0x80000000` (`NOUPCAM`) oculta la pieza en la vista del bateador y en las cámaras altas, `0x500` son las banderas.

**Sin resolver todavía:**
- Lo que hacen `0x40000000` (gradas altas detrás del plato) y `0x20000000` (`EVC`): quitarlos no cambió nada desde las vistas probadas.
- Si la pelota y el home run usan la colisión nueva o datos del estadio base: un home run al centro del Polo Grounds cayó a 400 pies, que es lo que medían allí sus vallas.
- Lo que el juego hace de noche (focos y luces de las gradas).

## Reglas

Los `.blend` con estadios importados contienen modelos y texturas del juego. Úsalos para tu mod, pero **no los compartas**. Un mod de estadio repartirá solo sus `.pack` modificados, igual que los uniformes (`docs/mods.md`).

Un estadio desde cero también lleva algo del juego: las bases, el plato y el montículo y las texturas precargadas (marcador, pantalla y público) del estadio de datos van dentro de su pack. Es poco, pero es contenido del juego: tenlo en cuenta antes de compartir el mod.

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
- **Estadio desde cero** (`tools/blender/test_scratch.py <zip> <project.bin> <carpeta temporal>`): papeles, agrupación, orden de dibujo, piezas `BCL` del juego (visibles y bloqueadas en la colección; solo se exportan las que quedan), capas del suelo de abajo arriba, banderas, público con la precarga, colisión con curvas, foto del menú, operadores y «Ver en el juego» sin abrir el juego.
- **Colisión por tipo** (`tools/blender/test_collision.py <zip> <project.bin> <carpeta temporal>`): un objeto por superficie con nombre y color, ver u ocultar por tipo, «Convertir en colisión», exportar juntando los objetos (y no reescribir si no cambió nada).
- **Ver en el juego** (`tools/blender/test_preview.py`): la exportación de vista previa y el arranque del juego, sin abrir ventanas.
- **Visor del runtime:** `WIICOMPILED_STADIUM_VIEWER=<carpeta del mod>`, `WIICOMPILED_STADIUM_VIEWER_CODE=<código>`, `WIICOMPILED_STADIUM_VIEWER_CAMERA=x,y,z,yaw,pitch,fov`, `WIICOMPILED_STADIUM_VIEWER_COLLISION=1` (empieza con la colisión visible) y `WIICOMPILED_STADIUM_VIEWER_SPIN=<rad/s>` (la cámara libre gira sola, para comprobar que las líneas de colisión no se deslizan).
- **Escritor desde cero** (`python tools/test_stadium_writer.py [project.bin]`): sin el `project.bin`, las pruebas sintéticas; con él, además, las de los 40 estadios del juego (unos 45 s). Herramientas:
  - `python tools/stadium_rebuild.py <project.bin> <código> <carpeta de mods> [--original]`: reconstruye un estadio del juego con el escritor como estadio nuevo (`rb_<código>`), o lo copia sin tocar (`og_<código>`) para compararlo por la misma vía;
  - `python tools/stadium_minimal.py <carpeta de mods>`: el estadio mínimo hecho desde cero (`minimo`).
  Los packs que generan son contenido del juego (salvo el mínimo): déjalos en `local/`.
