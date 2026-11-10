# Editor de uniformes libre en Arrange: diseño

Pedido del usuario el 7 de noviembre de 2026. Alcance: el creador de equipos de Arrange en PC
y Switch. Continúa el trabajo de [marcas y logotipos propios](2026-11-06-arrange-custom-marks-design.md),
ya terminado, y reutiliza su mecánica (extensión del guardado, índices virtuales en una lista del
juego, huella por hueco, opción EXPERIMENTAL en MEJORAS).

## Intención

El editor de uniformes del juego es muy limitado: compone con 10 piezas y una paleta de 32 colores, y
**impone** la inicial del equipo (una sola letra, p. ej. «L») mal puesta en gorra, pecho y número. El
usuario quiere **libertad total**: tratar el uniforme como un lienzo propio y colocar en él lo que
quiera, donde quiera.

En concreto, pidió:

- **Quitar la letra/número impuestos** que el editor pega sin control.
- **Colocar logos donde se le antoje** en la camiseta (frente y espalda), la gorra y el casco, y poner
  **varios** logos, no uno.
- **Colocar texto** —letras, frases, un título en la espalda, el nombre— en frente y espalda, con la
  **posición y el tamaño** que elija, incluida la zona del número.
- **El número del dorsal se queda donde está** (esa posición le gusta); el juego lo sigue dibujando.
- **Importar** uniformes de otros juegos (MLB 2K `.iff`, MVP Baseball 2005 `.fsh`) como una fuente más de
  arte para soltar en el lienzo, no como único modo.

Lo quiere **nativo** (elegido en el propio creador de Arrange, guardado con la partida), no un mod de
texturas (el mod cambia equipos de la MLB y se comparte entre equipos).

**La herramienta de PC tiene que ser sencilla de usar, como el juego original** (decisión del usuario,
7/11/2026): nada de un editor profesional complicado; soltar logos/texto, ver la vista previa y guardar.
Molde claro del uniforme, capas que se arrastran, pocos botones.

Validado con pruebas en el juego (`local/analysis/arrange/uniform/spike/`): se reemplazó la **textura
entera del cuerpo** de un equipo de Arrange (negro + «Aguilas» dorado + ribetes + gorra con logo), a
color y a 60 fps, solo por `poke`. Confirma que el mecanismo es viable.

## Cómo funciona (de la ingeniería inversa)

Detalle en `local/analysis/arrange/uniform/re.md` y `spike/spike.md`. Lo esencial:

- El uniforme de un personaje es **un atlas de 128×128** (C8 + paleta de 256) que el modelo desenvuelve
  sobre todo el muñeco: **frente y espalda de la camiseta, mangas, pantalón** y, en su atlas de gorra,
  el **frente de la gorra/casco**. No es una textura plana por prenda.
- El compositor `0x806CC2EC` **copia esa textura base cada cuadro** y encima dibuja el **número** en su
  posición fija. Por eso: si pintamos el atlas a gusto, el número sigue saliendo donde está, y lo que
  pintemos aparece alrededor.
- Para un equipo de Arrange con bandera `0x04`, el descriptor del uniforme (`0x806BDC3C`, llamada real
  ×15, nunca en línea, cubre los 6 contextos) tiene `+0x00 = 0`, así que sus campos son **punteros
  directos** a texturas y paletas: `+0x04`/`+0x08` cuerpo, `+0x0C..0x20` gorra/casco/pecho, `+0x24..0x34`
  números. Enganchando `0x806BDC3C` tras el original se sustituyen esos punteros por los de nuestros
  búferes en MEM2.
- La **inicial impuesta** son las letras de `+0x0C`/`+0x1C` (gorra/pecho); apuntándolas a una textura
  **transparente** desaparecen. Lo demás lo aporta nuestro atlas.

## Qué puede hacer, de cara al usuario

1. **Editor de uniformes de PC** (herramienta tipo Texture Studio, no dentro del juego). Muestra el
   **molde (UV)** del cuerpo (frente, espalda, mangas, pantalón) y de la gorra/casco. El usuario:
   - suelta **logos** (escudo, rótulo, los que quiera) en cualquier posición, con el tamaño que elija;
   - escribe **texto y frases** (fuente propia o la del juego) en frente, espalda y la zona del número;
   - pinta colores, rayas y patrones de fondo;
   - ve la **vista previa** en el muñeco;
   - al guardar, se **hornea** la textura nativa (atlas de cuerpo + de gorra, C8 128×128 + paleta de 256)
     en `UserData/arrange/uniforms/<nombre>/`.
   - Un `.iff` (2K) o `.fsh` (MVP) se **importa** como punto de partida: el conversor extrae rótulo,
     escudo, números y colores y los coloca como capas que el usuario reubica.
2. **Elección en el juego.** En la pantalla de uniformes del creador de Arrange, la ventana **LINE TYPE**
   gana entradas «IMPORTED: NOMBRE» (índices virtuales ≥3, como las marcas añaden ≥38 a Team Mark). Elegir
   una pone ese uniforme en el lado (casa/visitante) del equipo.
3. **Sin la letra/número impuestos.** Con un uniforme propio elegido, la inicial pegada por el editor se
   apaga. El **número del dorsal se mantiene** donde el juego lo dibuja.
4. **Por lado y por hueco.** Cada uno de los 32 equipos elige su uniforme de casa y el de visitante;
   varios equipos pueden usar la misma carpeta.
5. **Sin el runtime** (juego original, o Switch con la opción apagada): el equipo se ve con el uniforme de
   Arrange normal y colores de respaldo. No se corrompe nada.
6. **Opción EXPERIMENTAL** en MEJORAS/ENHANCEMENTS, apagada por defecto, PC y Switch. Reutiliza
   `SaveHooksEnabled()` de las marcas.

## Partes

### A. Editor/conversor de PC (`tools/`)

- **Conversor de entrada:** lee `.iff` (MLB 2K), `.fsh` (MVP, SHPI/RefPack) y PNG sueltos; extrae
  rótulo, escudo, números y colores. Prototipo en `local/analysis/uniform2k/` (`fsh.py`, `qfs.py`,
  decodificadores `.iff`).
- **Editor de lienzo:** sobre el molde UV del cuerpo y la gorra (mapa de regiones en
  `local/analysis/arrange/uniform/body_regions.png`), coloca capas (logos, texto, patrones) con
  posición/tamaño libres y vista previa en el muñeco.
- **Horneado:** compone las capas en el atlas 128×128, cuantiza a C8 + paleta RGB5A3 de 256 en el formato
  del descriptor del juego, y escribe `UserData/arrange/uniforms/<nombre>/` (cuerpo, gorra, casco y, si
  se quiere, paletas de número). No distribuye bytes del juego ni de otros juegos.
- Entra en el launcher como las demás herramientas (decisión del implementador: subcomando y/o botón).

**Base técnica recomendada** (`local/analysis/uniform2k/tool-base.md`): app nueva `tools/uniform-studio/`
clonando la carcasa de Texture Studio (C++20 + SDL3 + Dear ImGui, con los widgets y el estilo del juego y
el conmutador ES/EN ya hechos), con el horneado delegado a `tools/packwii.py` (ya cuantiza PNG → C8 +
paleta de 256 RGB5A3 y está auto-testeado). Lo único nuevo: el lienzo de capas (arrastrar logos/texto), la
vista previa en el muñeco y el escritor de `UserData/arrange/uniforms/<nombre>/`. El launcher añade la
herramienta con un parche `wiicompiled-uniform-studio.patch` (constante `kOpenUniformStudio`,
`OpenStudio(L"UniformStudio.exe")`, par ES/EN con espejo `docs/en/`). Riesgo a resolver en el plan: el
puente C++↔Python del horneado (invocar un exe, portar el cuantizador a C++, o app toda en Python
perdiendo el look).

### B. Carga en el runtime

Al activar la opción, el runtime lee `UserData/arrange/uniforms/`, carga los atlas horneados (o convierte
los PNG a C8 + paleta) y arma, por uniforme, los descriptores de textura/paleta en MEM2 (como las marcas
reservan bloques). Búferes distintos por uniforme; nunca se reescribe en sitio uno en uso.

### C. Gancho de dibujo

`0x806BDC3C`: ejecutar el original y, si el (hueco, lado) tiene elección, sobrescribir `+0x04/+0x08`
(cuerpo), `+0x0C/+0x14/+0x1C` (gorra/casco/pecho, a transparente o a nuestro arte) y sus paletas, y
opcional `+0x24..0x34` (números). El mapa (gestor, idx) → (hueco, lado) se arma en los mismos puntos que
las marcas: `0x805A83F0`, `0x806C10FC` (o sus 4 llamadores) y `0x80BC1558`; se limpia en
`0x806BD974`/`0x806BD8D0`.

### D. Guardado

Ampliar el bloque `MLBARRMK`: por hueco, el uniforme de casa y el de visitante (índice resuelto por
nombre de carpeta al cargar; si el nombre ya no existe, se ignora). Misma huella por hueco. El registro
queda válido para el juego original (bandera `0x04` y colores de respaldo). COPY/DELETE/crear/«Reset to
Base Team»/«Save as Arrange» ya cubiertos por el mecanismo de marcas: ampliarlos para el uniforme.

### E. Interfaz en el juego

Entradas virtuales ≥3 en la ventana **LINE TYPE** (`0x805C8CE8`, `0x805C8F28`, `0x805CACC8`,
`0x805CB0BC`, `0x805CAF4C`, `0x805CB064`, `0x805CC0F8`, `0x805CB1F4`, `0x805C9EF4`), análogas a los
índices virtuales de las marcas. Etiqueta con el widget de texto del juego (`0x805C28D4`). Elegir <3
quita la pendiente y vuelve al uniforme nativo.

## Objetivos

- Tratar el uniforme de un equipo de Arrange como lienzo: colocar logos y texto (frente, espalda, mangas,
  pantalón, gorra/casco) con posición y tamaño libres, varios a la vez.
- Quitar la inicial/letra impuesta; mantener el número del dorsal donde el juego lo dibuja.
- Importar `.iff` (2K) y `.fsh` (MVP) y PNG como fuente de arte.
- Elegido en el juego, por lado y por hueco; guardado en la partida y válido para el juego original.
- PC y Switch; opción EXPERIMENTAL apagada por defecto.

## No objetivos / límites reales

- **Resolución topada a 128×128** (pecho ≈ 60px, texto pequeño pixelado). Ampliar la malla/UV del modelo
  es el objetivo «cuando decompilemos», fuera de esta versión.
- **Solo donde hay UV:** zonas que el modelo no texturiza (p. ej. lateral/espalda de la gorra) no admiten
  arte sin tocar el modelo.
- **El número** lo dibuja el juego con su tamaño y posición; reubicarlo o cambiar su tamaño es un gancho
  del compositor, más adelante.
- No se importa geometría ni sombreado de otros juegos.
- No se descarga nada de webs con control de acceso.
- Temporada/Liga/MLB Life copian el registro a sus tablas; igual que las marcas, se resuelve por huella
  mientras no se cambien nombre/inicial/base después. Verlo en esos modos = seguimiento, no requisito.
- Un solo formato de textura probado (C8 + paleta).

## Anti-objetivos (fracaso aunque «funcione»)

- Que cambie el uniforme de un equipo de la MLB o se comparta sin querer entre equipos de Arrange.
- Que un guardado hecho con la opción activa se corrompa o pierda elecciones al abrirlo sin el runtime.
- Coste por cuadro en el partido (el gancho actúa al montar el uniforme, no por cuadro).

## Riesgos conocidos

- Vida de los punteros: los datos viven toda la sesión en MEM2; el personaje puede reaplicar el
  descriptor.
- `0x806F0670` (ruta MLB) y los setters de pieza están en línea: no se enganchan.
- Casa y visitante deben tener búferes propios (en el spike compartían el atlas global).
- Contextos de Temporada/MLB Life (`0x806C0304`/`0x80BC1558`) sin captura todavía.

## Qué queda al implementador

- Diseño del editor de PC (molde UV, capas, fuentes para el texto, vista previa).
- Formato de la carpeta `UserData/arrange/uniforms/<nombre>/` y cómo se guardan las capas vs. el atlas
  horneado.
- Cuantización PNG/atlas → C8 (biblioteca y parámetros).
- Algoritmo de color más cercano para las sugerencias del importador.
- Texto exacto de las etiquetas en el juego y orden de las entradas virtuales.
