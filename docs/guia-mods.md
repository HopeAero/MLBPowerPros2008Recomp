# Guía para crear mods de MLB Power Pros 2008

*English version: [en/modding-guide.md](en/modding-guide.md)*

Esta guía explica, paso a paso, cómo crear cada tipo de mod para el recomp de MLB Power Pros 2008 para PC: texturas, uniformes y menús, estadios y rosters. Los detalles técnicos están en los documentos de referencia enlazados al final.

## Qué es un mod

- Un mod es **una carpeta** dentro de `mods`, junto al juego instalado (`<instalación>\Game\mods`).
- **El juego nunca se modifica:** al arrancar lee los mods activos y usa sus archivos en lugar de los originales.
- Se activan y desactivan en el launcher: **MODS** en el menú principal, o la pestaña **MODS** de Ajustes. El cambio se aplica al pulsar **JUGAR**.
- Desactivar un mod nunca rompe nada: el juego vuelve a usar lo original.

## Antes de empezar

1. Ten el juego instalado con el Setup a partir de tu propia copia del disco.
2. En el launcher, **MODS** → **ABRIR CARPETA DE MODS** abre la carpeta `mods` y deja un `LEEME.txt`.
3. Las herramientas están en el launcher, **AJUSTES → HERRAMIENTAS** (editor de texturas) y **MODS → HERRAMIENTAS** (editores de roster).

## La carpeta de un mod

```
mods\
  MiMod\
    mod.toml       nombre, autor y versión (opcional)
    textures\      texturas de reemplazo (PNG o DDS)
    project\       paquetes de project.bin modificados (.pack)
    roster\        un roster de PowerUp / PowerUp+ (.dat)
    disc\          archivos que reemplazan los del disco
    riivolution\   parches Riivolution (.xml)
```

Solo hace falta la subcarpeta del tipo de mod que haces. `mod.toml` es opcional; sin él, el mod se llama como su carpeta:

```toml
name = "Mi mod"
author = "Tu nombre"
version = "1.0"
description = "Qué cambia."
```

## 1. Texturas: Texture Studio

Para cambiar o mejorar cualquier imagen del juego: menús, uniformes, logos, caras, carteles de los estadios, cielos.

1. Abre **Power Pros Wii 2008 Texture Studio**: launcher → AJUSTES → HERRAMIENTAS → **EDITOR DE TEXTURAS**, o su acceso directo.
2. **+ Nuevo mod** y ponle nombre.
3. Busca lo que quieras cambiar por categoría o con el buscador. Para encontrar las texturas de una pantalla concreta, usa **Capturar en juego**: marca con un ojo las que se ven en esa pantalla.
4. **Exportar PNG** (una textura) o **Exportar esta vista** (todas las de la categoría).
5. Edita los PNG en Photoshop, GIMP o Krita, o escálalos con IA (chaiNNer, Upscayl). **No cambies los nombres de archivo.**
   - Puedes hacerlos más grandes (HD), con la misma proporción: ×2, ×4 u ×8 es lo ideal, hasta 4096 píxeles.
6. **Importar PNG...** o **Importar carpeta...**. El informe dice qué entró y qué no, y por qué.
7. **Activar** y abre el juego.
8. Para compartirlo: **Empaquetar .zip**.

Más en [texture-studio.md](texture-studio.md).

## 2. Uniformes, menús y otros paquetes

Casi todo el contenido del juego está en `project.bin`, en paquetes `.pack`. Un mod puede traer paquetes modificados en `project/`, con su nombre exacto (por ejemplo `project/PWM_mdl_uniform_MLB_BOS_00.pack`), y el juego los usa al arrancar.

- **Cambiar las texturas de un paquete** (sin Texture Studio):

  ```
  python tools/projectbin.py extract <project.bin> pack env_mainmenu
  python tools/packwii.py export pack/env_mainmenu.pack png
  #   edita los PNG sin cambiar su tamaño
  python tools/packwii.py import pack/env_mainmenu.pack png MiMod/project/env_mainmenu.pack
  ```

- **Cambiar un paquete por otro**: copiar el uniforme de un equipo con el nombre del de otro hace que el segundo salga con el primero.
- Para saber qué paquete usa cada pantalla, arranca el juego con `WIICOMPILED_DEBUG_DVD=1`: el registro muestra cada lectura y `projectbin.py list` la traduce a nombre.
- El mod reparte **solo** los `.pack` que cambió, nunca el `project.bin` entero.

Más en [mods.md](mods.md).

## 3. Estadios: Blender

Con el complemento de Blender puedes editar los estadios del juego o meter uno tuyo como estadio nuevo, que aparece como una entrada más en la elección de estadio de Exhibición.

**Preparación**
1. Blender 4.2 o más nuevo.
2. Instala el complemento: **Editar > Preferencias > Obtener extensiones** → menú ⌄ → **Instalar desde disco…** → `powerpros_stadium-<versión>.zip`.
3. **Importa un estadio** desde la carpeta del juego instalado (la que tiene `UserData`): Archivo > Importar > Estadio de Power Pros 2008, o la barra lateral (tecla N), pestaña **Power Pros**.

**Editar un estadio del juego**
- Mueve, gira o escala piezas, edita sus mallas o sus imágenes. Las propiedades `pp_*` guardan lo que necesita la exportación: no las borres.

**Meter un modelo tuyo** (SketchUp, glTF, OBJ…), desde cero
1. Panel **Power Pros** → **Nuevo estadio desde cero**. Eliges un **estadio de datos**, que solo aporta partidas, menú, cielo y sonidos; la colección nueva ya trae las bases, el plato y el montículo del juego.
2. Importa tu modelo (un `.skp` se pasa antes a `.glb` con OpenSKP) y mételo en esa colección, en metros, con el plato en el origen, el suelo a la altura 0, la segunda base hacia +Y y 90 pies entre bases, ajustado a las bases del juego.
3. Da a cada objeto su **papel**: suelo, grada, valla, público (el juego pone su gente animada), banderas (se mueven con el viento), transparente o brillo aditivo.
4. **Generar colisión**: sale un objeto por tipo (vallas, gradas, postes…), que ves u ocultas en la caja Colisión del panel; con **Convertir en colisión** haces colisión de cualquier malla.
5. El estadio se escribe entero desde tu modelo. Qué se toma del juego y la otra vía, meter el modelo dentro de un estadio del juego con «Convertir objetos en piezas»: [stadiums.md](stadiums.md), «Tu propio modelo».

**Ver cómo queda en el juego, sin jugar un partido**
1. Pon la vista 3D donde quieras empezar.
2. Panel **Power Pros** → **Ver en el juego**. En unos 8 segundos se abre el juego en tu estadio, con la cámara en tu vista, sin jugadores ni marcador.
3. Muévete con W/A/S/D, Q/E (bajar y subir), Shift (rápido), botón derecho del ratón + ratón (mirar) y la rueda (velocidad). 1, 2 y 3 ponen las cámaras del partido (bateador, jugada, lejana), F vuelve a la cámara libre, C muestra la colisión (por colores, a través de todo) y H oculta la ayuda.
4. Cambia algo en Blender y vuelve a pulsar: el juego anterior se cierra y se abre con el cambio. No toca tu carpeta `mods` ni tus ajustes.

**Exportar**
1. **Exportar mod de estadio**, con un código corto (por ejemplo `polo`) y un nombre. Un estadio desde cero sale siempre como estadio nuevo; uno importado del juego, como **Estadio nuevo** o **Reemplazar un estadio**.
2. Elige la carpeta `mods` del juego. Opcional: **Foto del menú** y la línea bajo la foto («Home of …»).
3. Actívalo en MODS y elígelo en Exhibición.

Consejos para que se vea bien en el juego, límites y problemas conocidos: [stadiums.md](stadiums.md).

## 4. Rosters: PowerUp+

1. Launcher → MODS → HERRAMIENTAS → **EDITAR ROSTER (PowerUp+)**.
2. Edita jugadores y equipos, o genera un roster real desde la MLB Stats API.
3. **Export Roster → MLB Power Pros 2008 Mod** descarga un zip con el mod hecho (`mod.toml` y `roster\<nombre>.dat`).
4. Descomprímelo en `mods` y actívalo en MODS.

- El roster vale para todos los modos, **MLB Life incluido**.
- Un mod de roster tiene **partidas propias**: una carrera o una temporada empezada con él solo tiene sentido con ese roster, así que se guarda aparte. Las partidas normales no se tocan.

Más en [roster-2008.md](roster-2008.md).

## Jugadores de Success: copiar, pegar, exportar e importar contraseñas

Los jugadores que creas en Success se comparten con una contraseña de 156 caracteres. En el juego original había que copiarla de una foto y escribirla con el mando; aquí va como texto:

- **Copiar:** abre My Data › View or Delete Custom Players › el jugador › Password Display y pulsa el botón naranja **Copy password** (en la barra de abajo) o **Ctrl+C**. La contraseña queda en el portapapeles como texto, en grupos de 3 y filas de 18 como en la pantalla, para pegarla en un mensaje, un foro o un archivo.
- **Pegar:** abre My Data › Password Input, pulsa el botón **Paste password** (bajo OK / INSERT / EXIT) o **Ctrl+V** y luego OK. El jugador queda guardado como si lo hubieras escrito.
- **Exportar a un archivo:** en Password Display, el botón azul **Export file** abre la ventana de guardar de tu sistema; el jugador queda en un `.txt` donde quieras, para compartirlo o guardarlo.
- **Importar un archivo:** en Password Input, el botón azul **Import file** abre la ventana de abrir; elige el `.txt` y luego OK. Sirve también sin portapapeles.

Al pegar se ignoran los espacios y saltos de línea, y se aceptan equivalentes para los símbolos que no están en el teclado: `-` (−), `/` (÷), `!=` (≠), `@` (◎), `^` (△), `*` (☆), `->` (⇒), `<->` (⇔), `■` (□), `▼` (▽), `◆` (◇), `0` (O). Abajo sale un aviso con el resultado. Un jugador de Success también tiene la edad correcta en MLB Life con un paquete de temporada.

## 5. Otros tipos

- **`disc/`**: archivos con la misma ruta que en la carpeta `files` del disco, por ejemplo sonidos.
- **`riivolution/`**: parches Riivolution para `RL8E54`. Los de archivos funcionan; los de memoria solo sirven para datos, porque el código del juego ya es nativo. Sin probar todavía en MLB.

## Probar tu mod

- Activa solo tu mod y pulsa JUGAR. Si no ves el cambio, comprueba que el mod está activado y que los nombres de archivo son exactos.
- **Conflictos:** si dos mods activos traen el mismo archivo, gana el primero en orden alfabético.
- Para estadios, prueba primero con **Ver en el juego** y al final juega un partido.

## Compartir tu mod

- Comprime la carpeta del mod en un `.zip`. Quien lo use lo descomprime en su carpeta `mods`.
- **Nunca incluyas contenido del juego:** ni `project.bin`, ni el ejecutable, ni texturas originales sin modificar, ni paquetes que no cambiaste. Un mod lleva solo sus propios archivos o diferencias.
- Los `.blend` con estadios importados contienen modelos y texturas del juego: úsalos para hacer tu mod, pero no los compartas.
- Si usas modelos de otras personas (Sketchfab, 3D Warehouse…), respeta su licencia.

## Problemas frecuentes

| Problema | Solución |
|---|---|
| El mod no aparece en MODS | Debe ser una carpeta directamente dentro de `mods`, no una carpeta dentro de otra. |
| Los cambios no se ven | Los mods se aplican al pulsar JUGAR. Comprueba que el mod está activado y los nombres de archivo. |
| La primera partida con un mod tarda más | El juego prepara su copia de `project.bin` (406 MB) la primera vez; luego la reutiliza. |
| Estadio con gradas grises o texturas a rayas | Actualiza el complemento de Blender a la 0.7.3 o posterior y vuelve a exportar. |
| En Blender el estadio convertido se ve gris | Usa la vista **Material** (Z > Vista previa de material). |
| En «Ver en el juego» la cámara se mueve sola | La ventana del juego activa recibe tus teclas: cambia de ventana antes de escribir. |
| Mis partidas desaparecieron al activar un mod | Los mods de roster tienen partidas propias; las otras siguen guardadas. En MODS, **PARTIDAS EN USO** dice cuáles usa el juego y abre su carpeta (v0.1.2). |

## Más información

- [mods.md](mods.md): referencia de mods (carpetas, `project.bin`, partidas por roster).
- [texture-studio.md](texture-studio.md): Texture Studio.
- [season-studio.md](season-studio.md): Editor de temporada (año de inicio, DH, ligas, calendarios reales).
- [stadiums.md](stadiums.md): estadios en Blender.
- [roster-2008.md](roster-2008.md): rosters y PowerUp+.
- [ingenieria-inversa.md](ingenieria-inversa.md): formatos del juego, para quien quiera ir más allá.
