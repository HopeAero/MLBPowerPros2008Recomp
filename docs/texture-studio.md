# Power Pros Wii 2008 Texture Studio

*English version: [en/texture-studio.md](en/texture-studio.md)*

Programa para crear **mods de texturas** de MLB Power Pros 2008. Con él puedes:

- ver todas las texturas del juego, ordenadas por lo que son;
- exportarlas a PNG para editarlas en otro programa (Photoshop, GIMP, Krita) o escalarlas con IA desde el propio Studio;
- importar packs de texturas hechos para Dolphin;
- volver a meterlas, del mismo tamaño o más grandes (HD).

El juego nunca se modifica: todo va a un mod que activas o desactivas en el launcher.

## Abrirlo

- Desde el juego: launcher → AJUSTES → **HERRAMIENTAS** → **EDITOR DE TEXTURAS** → Abrir.
- O con el acceso directo **Power Pros Wii 2008 Texture Studio** del escritorio, si lo creaste al instalar.

La primera vez tarda un segundo en leer las texturas de tu copia del juego (unas 9.700 distintas).

## La ventana

- **Arriba:** el mod en el que trabajas, si está activado en el juego (botón **Activar**) y **+ Nuevo mod**.
- **Izquierda:** categorías.
  - Menús, Estadios (por equipo, más cielos y estadios originales), Uniformes, Cartas de jugador, Logos, Caras, MLB Life, Temporada, Success…
  - Al final, las **capturas** que hayas hecho.
- **Centro:**
  - un buscador por nombre, pack o hash;
  - los filtros **Solo reemplazadas** y **Vistas en captura**;
  - las miniaturas, con un ✓ verde en las que ya reemplazaste y un ojo naranja en las que viste al capturar. Doble clic exporta una textura.
- **Ficha:** la textura original junto a tu reemplazo.
  - Haz clic en cualquiera de las dos para verla grande.
  - Debajo verás el pack de donde sale, su formato, el nombre del archivo y los botones **Exportar PNG**, **Importar PNG...**, **Quitar reemplazo** y **Abrir carpeta**.
- **Abajo:** **Exportar esta vista**, **Importar carpeta...**, **Capturar en juego** y **Empaquetar .zip**.

**Idioma:** la píldora **ES | EN** de la banda superior cambia el idioma al momento y lo guarda en `Config.toml` (`[ui] language`), el mismo ajuste que el launcher. Sin elegir, usa el idioma de Windows.

## Crear un pack HD paso a paso

1. **+ Nuevo mod**: ponle un nombre (será la carpeta `mods\<nombre>`).
2. Elige una categoría (por ejemplo, Menús › Menú principal) y pulsa **Exportar esta vista**. Elige una carpeta: se crean subcarpetas con los PNG originales.
3. Edita esos PNG o pásalos por chaiNNer o Upscayl. **No cambies los nombres de archivo**: el nombre `tex1_…` es lo que el juego busca.
4. **Importar carpeta...** y elige la carpeta con tus PNG. Busca en todas las subcarpetas y te enseña un informe: qué se importó y qué no, y por qué.
5. **Activar** (arriba) y abre el juego.

Para una sola textura: selecciónala, **Exportar PNG**, edítala e **Importar PNG...**.

## Tamaños

- Puedes usar la textura más grande, pero con la **misma proporción** (una de 256×128 puede ser 512×256, 1024×512…).
- Lo ideal es multiplicar por 2, 4 u 8. Otro tamaño con la misma proporción se acepta con un aviso.
- Máximo: 4096 píxeles de lado.
- Se aceptan PNG de cualquier tipo (con paleta, en gris, de 16 bits) y los DDS que vuelca el juego. Desde Photoshop, guarda como PNG.

## Paletas

Muchas texturas del juego usan paleta (16 o 256 colores). No tienes que preocuparte: el reemplazo es una imagen a todo color.

- Unas 300 texturas tienen la paleta en otro pack. El Studio las muestra **en gris**, pero se pueden exportar y reemplazar igual.
- Si el juego pinta una misma textura con varias paletas, cada variante es un reemplazo distinto. **Aplicar a todas las paletas** (en la ficha) hace que tu imagen sirva para todas.

## Escalar con IA

**Escalar con IA** (abajo) exporta las texturas, las escala en tu tarjeta gráfica e importa el resultado en el mod activo, todo de una vez.

1. Elige una categoría (o selecciona una textura) y pulsa **Escalar con IA**.
2. Elige el modelo, el tamaño (**x2** o **x4**) y si quieres **esta vista** o **solo la seleccionada**.
3. Pulsa **Escalar**. Al terminar sale el informe de importación; activa el mod y míralo en el juego.

**Modelos:**

- **AnimeSharp** (el recomendado): menús, logos, letras y personajes; conserva el sombreado y el estilo de dibujo.
- **Real-ESRGAN anime**: más nítido, con contornos muy marcados (más «dibujo animado»).
- **Real-ESRGAN x4plus**: para fotos, como los estadios y las cartas de jugador.
- **Tus modelos**: cualquier modelo NCNN (`<nombre>.param` + `<nombre>.bin`) que pongas en `UserData\Tools\upscale\models` sale en la lista.

**La primera vez** se descargan, gratis y de sus autores, el motor **Real-ESRGAN ncnn-vulkan** (unos 45 MB, licencia MIT, del GitHub de xinntao) y, si eliges AnimeSharp, **4x-AnimeSharp de Kim2091** (unos 33 MB, licencia CC BY-NC-SA 4.0: uso no comercial, de su página de Hugging Face). Van a `UserData\Tools\upscale` y no vienen con el juego. Funciona en tarjetas AMD, NVIDIA e Intel (Vulkan).

**Consejos:**

- Las texturas en gris (su paleta está en otro pack) se saltan: se verían grises en el juego.
- Con x4, una textura que pasaría de 4096 px se escala a x2.
- Hazlo por categorías: un x4 multiplica por 16 los píxeles, y el juego entero en x4 son varios GB. Como referencia, en una RX 5500 son unos 4 segundos por textura de 512×512 con AnimeSharp.
- Las texturas que se repiten (césped, gradas, muros) pueden mostrar costuras: revísalas en el juego y, si se notan, déjalas en el original o en x2.
- También puedes escalar fuera del Studio (chaiNNer, Upscayl): exporta, escala **sin cambiar los nombres de archivo** y usa **Importar carpeta...**.

## Packs de texturas de Dolphin

Los nombres de las texturas son los mismos que en Dolphin (comprobado con Dolphin 2609: los 695 nombres de las texturas que se vieron en los dos son idénticos), así que **un pack hecho para Dolphin para MLB Power Pros 2008 (RL8E54) funciona tal cual**.

1. Crea o elige un mod arriba.
2. **Pack de Dolphin...** y elige la **carpeta** del pack (la que tiene los `tex1_…`, normalmente `Load\Textures\RL8E54`) o directamente el **.zip**.
3. Los archivos se copian al mod sin cambiar nombres, con sus subcarpetas, mipmaps (`_mip1`…) y DDS (BC1, BC3, BC5, BC7). El informe dice cuáles son texturas del juego; las que no están en el índice se copian igual (pueden ser texturas que se crean al jugar).
4. **Activar** y abre el juego.

Al revés debería servir igual (sin probar): un mod hecho con el Studio, en Dolphin, copiando la carpeta `textures` del mod a `Load\Textures\RL8E54`.

## Capturar en juego

Sirve para encontrar la textura de algo que ves en pantalla.

1. Pulsa **Capturar en juego**: se abre el juego sin el launcher.
2. Ve a la pantalla que te interesa y cierra el juego.
3. El Studio crea **Capturas › Sesión N** con las texturas que aparecieron y las marca con el ojo.

Algunas texturas solo existen mientras se juega (no están en ningún pack). Salen en **Solo vistas en juego** y también se pueden reemplazar. Los vídeos no se pueden reemplazar.

## Compartir tu mod

**Empaquetar .zip** crea un zip con tu `mod.toml` y tus imágenes, nada más. Quien lo reciba lo descomprime en su carpeta `mods` y lo activa en el launcher (MODS).

No compartas texturas originales sin modificar: son contenido del juego. Comparte solo lo que tú hiciste.

## Para usuarios avanzados

`TextureStudio.exe` también funciona desde la línea de comandos:

```
TextureStudio.exe --new-mod <carpeta mods> <nombre>
TextureStudio.exe --export <project.bin> "<grupo>[/<subgrupo>]" <carpeta>
TextureStudio.exe --import-folder <project.bin> <carpeta del mod> <carpeta con PNG>
TextureStudio.exe --import-dolphin <project.bin> <carpeta del mod> <carpeta del pack>
TextureStudio.exe --upscale <carpeta de herramientas> <modelo> <2|4> <carpeta con PNG>   (escribe <carpeta>-up)
TextureStudio.exe --selftest
```

La ventana se puede abrir en una vista concreta con `--view "Estadios/Yankees (NYY)"`, `--search <texto>`, `--select <nombre tex1_>` y `--mod <nombre>`.

Para meter texturas dentro de los `.pack` en lugar de usar un mod de texturas, está `tools/packwii.py` (ver `docs/mods.md`).
