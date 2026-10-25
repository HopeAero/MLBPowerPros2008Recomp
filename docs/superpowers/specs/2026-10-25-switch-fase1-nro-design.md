# Switch, fase 1: el juego arranca en la consola con gráficos fieles al PC — diseño

*English version: [../../en/superpowers/specs/2026-10-25-switch-fase1-nro-design.md](../../en/superpowers/specs/2026-10-25-switch-fase1-nro-design.md)*

Fecha: 25 de octubre de 2026. Estado: aprobado por el usuario el 25 de octubre de 2026 («continua»). Rama: `switch-arm64` (master no se toca).

## Por qué

El usuario quiere jugar MLB Power Pros 2008 en su Nintendo Switch con homebrew, compilado desde su propia copia como en PC. Ya hizo un port a Switch de otro juego (Need for Madness) y conoce el flujo: probar en la consola, pasar informes al chat, guardar todo en una carpeta de la SD. La fase anterior (rama `switch-arm64`) demostró que el código traducido funciona en ARM64 real: en un teléfono llega al primer lanzamiento con el backend nulo.

## Qué es éxito en esta fase

El usuario abre el `.nro` desde el menú homebrew de su Switch y **el juego arranca, llega al menú y entra a un partido, y se ve igual que en el PC**: sin gráficos rotos, sin texturas o colores mal. Que vaya lento está bien; la velocidad es la fase siguiente. En palabras del usuario: «mientras gráficamente no se rompa y se vea fiel al port de PC, entonces lo que falta es optimizar».

## Objetivos

1. Compilar el juego como `.nro` de Switch en el PC del desarrollador, desde la ISO del usuario, con devkitPro (devkitA64 + libnx) dentro de Docker, igual que el port de Need for Madness.
2. El runtime (Wiicompiled + los parches de MLB) funcionando sobre Horizon: memoria del invitado, hilos y fibras, archivos en la SD, mandos, audio.
3. Gráficos con la misma aurora del PC sobre **Dawn + Vulkan con el driver NVK de Mesa para Switch** (`nxvk`), es decir, el mismo código de dibujo que en PC.
4. Una carpeta propia en la SD con el juego, los datos del disco, las partidas, la configuración, las cachés de shaders, los informes y los registros.
5. Informes para el chat:
   - el informe del partido automático CPU contra CPU que ya existe (`WIICOMPILED_MATCH_BENCH`), escrito en la SD y lanzable sin variables de entorno (por ejemplo, desde una opción o un archivo en la SD);
   - un reporte de crash detallado escrito en la SD cuando el juego falla (registro del runtime y estado del invitado, como el que ya produce en PC).
6. Comprobación de fidelidad: capturas del mismo momento del partido automático en la Switch y en el PC, comparadas.

## Fuera de esta fase (no-objetivos)

- Velocidad: ni 30 ni 60 FPS son requisito. Se mide y se informa, no se optimiza.
- El renderizador deko3d (el de Twilight Princess / SwitchWaker). Es el camino elegido para la fase de optimizar; aquí no se toca.
- El launcher adaptado a la Switch (manejo con mando, sin teclado). En esta fase el juego arranca directo; el launcher llega en una fase posterior.
- El Setup de PC que genere el `.nro` y los datos para la SD automáticamente. En esta fase los genera el desarrollador.
- Red, Discord, mods, Texture Studio, editor de rosters y demás herramientas de PC.
- Modo portátil, dock y resoluciones: lo que dé el sistema por defecto.

## Lo que haría que fracase aunque «funcione»

- Que se vea distinto al PC: colores, texturas, efectos o geometría rotos. La fidelidad gráfica es el criterio principal.
- Publicar o incluir datos del juego: el `.nro` contiene código traducido del juego y nunca se sube a GitHub ni a releases; el repositorio solo lleva scripts y parches (regla del proyecto).
- Romper las compilaciones de Windows y Linux: todo cambio del runtime debe dejar Windows a 60 FPS y Linux igual.

## Restricciones

- Todo el trabajo va en la rama `switch-arm64`.
- Los cambios del runtime son parches reproducibles en `tools/patches`, listados en `tools/wiicompiled-translate.ps1`, sobre el commit fijado de Wiicompiled (`6fa2473`).
- devkitA64 es solo GCC (sin clang); el código traducido y el runtime deben compilar con GCC.
- Nada de la ISO, del ejecutable ni de las capturas del juego en git.
- Licencias: Wiicompiled y `wiicompiled-nx` son GPL-3.0 (como el proyecto); Dawn BSD-3; Mesa/NVK MIT (enlazado estático, el `.nro` queda bajo GPL); SDL3 zlib.
- Documentación en español e inglés (`docs/` y `docs/en/`), y los hallazgos de ingeniería inversa en `docs/ingenieria-inversa.md`.

## Decisiones tomadas

- **Probar en la Switch real del usuario** (Atmosphère + menú homebrew), no en emulador. El usuario copia el `.nro` y los datos a la SD y devuelve informes y reportes.
- **Gráficos: Dawn + Vulkan (NVK) primero, deko3d en la fase de optimizar.** El puente deko3d de Twilight Princess depende de una estructura interna de aurora (`gfx/frame`, `recording`, `render_worker`, `resource_cache`) que nuestra aurora (la de Wiicompiled) no tiene; adaptarlo son semanas sin ver nada en pantalla. Dawn + Vulkan funciona con nuestra aurora tal como está y es el renderizador predeterminado y de reserva de la aurora de Twilight Princess.
- **Reutilizar el trabajo de `nx-mod`** (GPL-3.0): `wiicompiled-nx` ya portó nuestro mismo runtime a Horizon (Mario Kart Wii jugable en la consola, a ⅕ de velocidad), `dawn-nx` porta Dawn a Horizon con superficie `VK_NN_vi_surface`, y `nxvk` es el driver NVK para Switch. Sus notas (`local/research/wiicompiled-nx-switch-port-notes.md`) documentan los problemas ya resueltos: devkitA64 solo GCC, sin alias de memoria en Horizon (el runtime usa el camino con comprobaciones), `thread_local` roto bajo libnx, el `.nro` necesita icono y NACP, el backend GL de Dawn no arranca.
- **Ventana, audio y mandos con la rama SDL3 de devkitPro** (la que usa Twilight Princess), si cubre lo que aurora necesita; si no, los shims de `wiicompiled-nx`.
- **Compilación en Docker** con la imagen de devkitPro (`devkitpro/devkita64`) más lo que pidan nxvk y Dawn.

## Respuestas a la revisión del diseño

- **Repositorios de `nx-mod`:** todos públicos en GitHub (25-10-2026): `wiicompiled-nx` (rama `switch`, último push 29-9), `dawn-nx` (`switch`, 22-10; también `switch-deko3d` y `switch-gl-20261007`), `nxvk` (`switch`, 6-10; ramas por versión de Mesa), `sqlite-nx` (`switch`), `aurora-nx` (`switch`, 7-10) y `wii-nx` (`main`, 15-10). No hace falta ninguna credencial. Se fija un commit de cada uno; cuál es el que dibujó en la consola se averigua leyendo sus notas e historial (tarea del constructor).
- **Docker:** la imagen se construye desde cero sobre `devkitpro/devkita64` *(decisión mía)*; no se reutilizan los archivos del port de Need for Madness.
- **Referencia de fidelidad** *(decisión mía)*: el PC con Direct3D 12 a resolución nativa (x1), sin mejoras (FSR, MSAA, anisotrópico, CALIDAD, mipmaps automáticos y packs de texturas apagados), en los mismos momentos del partido automático. Las capturas de la Switch y del PC se ponen lado a lado y **el usuario decide a ojo**: no se exige igualdad de píxeles (la resolución y la precisión de la GPU difieren).
- **Capas de PC (ImGui):** con Dawn + Vulkan el backend WebGPU de ImGui de aurora funciona como en PC, así que no hay que portar nada. Aun así, la fidelidad se juzga sobre la imagen del juego; los paneles de ImGui no cuentan, y el partido automático se activa desde un archivo en la SD *(decisión mía)*.
- **Informe del partido automático** *(decisión mía)*: un tramo acotado (desde el primer lanzamiento, N segundos como en PC con `WIICOMPILED_MATCH_SECONDS`), no un partido entero. El informe se escribe en la SD de forma incremental, porque HOME cierra el proceso sin pasar por `atexit`. El audio debe sonar sin colgarse; que suene perfecto no es requisito de esta fase.
- **Ciclo de prueba en la consola (respuesta del usuario):** la Switch del usuario tiene firmware 22.5.0 y Atmosphère 1.11.2, y no usa `nxlink`; podría conectarse por red, pero le resulta complicado. Cada prueba es una copia manual a la SD. Por eso el plan:
  - escalona las versiones de prueba para que cada viaje a la consola responda una pregunta (por ejemplo: un `.nro` mínimo que arranca y escribe un registro; Dawn presentando un color; el juego sin gráficos; el juego completo);
  - escribe todo registro en la SD a medida que ocurre (con `fflush`), para que sobreviva a un crash o a HOME;
  - prueba en Docker y en el teléfono todo lo que no necesite la consola antes de pedir una prueba;
  - da al usuario pasos cortos y exactos por prueba: qué copiar, dónde, cómo abrirlo y qué archivo devolver.

  No se sabe si el usuario puede abrir el menú homebrew «sobre un juego» (manteniendo R al abrir un juego), que da toda la RAM; el plan lo da como forma recomendada y mide si el modo applet basta.
- **Relación con master:** master está congelado en `a497b6f` (Direct3D 11) hasta que el usuario decida integrar, así que `switch-arm64` no lo sigue; la integración es una tarea aparte, al final.

## Lo que ya tenemos a favor

- El código traducido funciona en ARM64 (teléfono, primer lanzamiento en 28-29 s con el backend nulo).
- Los arreglos de la fase anterior que la Switch necesita: el callback de inicio del DSP (sin él, el camino de memoria con comprobaciones se cuelga al iniciar el audio) y la guarda de hilos de menos prioridad (sin ella, un procesador que no llega a 60 FPS deja sin turno al cargador y la intro no termina).
- El partido automático CPU contra CPU y su informe, y el volcado del estado del invitado al fallar.

## Riesgos conocidos

- **Memoria:** el `.nro` de Mario Kart Wii pesa unos 101 MB; el de MLB será parecido. El modo applet del menú homebrew da poca memoria; puede hacer falta abrirlo «sobre un juego» (title override) para tener la RAM completa. Hay que medirlo.
- **Divergencia de runtimes:** `wiicompiled-nx` parte de un Wiicompiled del 7-9-2026 con 79 commits propios; nuestro commit fijado tiene 41 commits más nuevos y 75 parches de MLB encima. Hay que llevar sus cambios a nuestro árbol, no al revés.
- **Primera carga:** en Mario Kart la precompilación de pipelines tardó casi dos minutos; con caché de shaders persistente (sqlite) la segunda vez es rápida.
- **Firmware más nuevo:** el port de Mario Kart Wii se probó en el firmware 16.0.5; el usuario tiene 22.5.0. libnx y nxvk tienen que soportarlo, y el mapa de memoria puede ser distinto.
- **GCC:** partes del runtime o de los parches de MLB pueden depender de extensiones de clang.

## Lo que se deja a quien lo construya

- Cómo llevar los cambios de `wiicompiled-nx`, `dawn-nx` y `nxvk` a nuestro árbol (cherry-pick, parches nuevos, submódulos o paquetes precompilados en la imagen Docker), con tal de que sea reproducible.
- La estructura exacta de la carpeta en la SD y el nombre de la carpeta.
- Cómo se activa el partido automático en la Switch (opción, archivo en la SD o botón al arrancar).
- El orden interno de las tareas y las pruebas de cada una (las que no necesitan la consola, en Docker o en el teléfono; las de consola, con pasos que el usuario pueda seguir).
