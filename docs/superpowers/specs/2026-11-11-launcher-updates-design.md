# Actualizaciones desde el launcher: diseño

Pedido del usuario el 11 de noviembre de 2026, para la próxima versión (después de v0.1.7). Alcance:
PC con Windows y los kits de Linux y Steam Deck. La Switch queda fuera: allí actualizar ya es cambiar
el `.nro`.

## Intención

Hoy, para pasar a una versión nueva, el jugador tiene que enterarse por su cuenta (GitHub, Discord),
bajar el Setup entero (~330 MB), volver a elegir el disco, esperar la compilación completa (~4–5 min)
y copiar a mano `UserData\NAND`, `UserData\ModSaves`, `UserData\arrange` y `mods` de la instalación
vieja a la nueva. El usuario quiere que el **launcher se actualice solo**: que diga qué versión es, que
avise cuando hay una nueva y que la instale sin reinstalar nada ni tocar los datos del jugador.

## Lo que decidió el usuario

- **Versión visible:** el launcher enseña su versión (por ejemplo «v0.1.7») en un sitio fijo.
- **Aviso:** el launcher comprueba si hay una versión nueva y, si la hay, lo indica con su número.
  Además hay una opción para **buscar actualizaciones** a mano.
- **Contenido del aviso (14 nov):** «Nueva versión vX.Y.Z», un **enlace a la Discussion** de esa
  versión (vídeo, imágenes y detalles técnicos, como ya enlazan las notas de cada release) y la pregunta
  de si quiere actualizar, **Sí / No**. «Sí» empieza la actualización; «No» cierra el aviso y deja jugar.
- **Actualizar es preferible a solo avisar:** un botón que actualiza desde el propio launcher, sin
  Setup ni reinstalación, conservando partidas, mods, marcas, logotipos y uniformes.
- **Progreso:** una barra que muestra cuántos MB pesa la actualización y cuánto lleva descargado, y
  después cómo va la compilación.
- **Rápida:** el usuario aceptó «bajar decenas de MB y esperar 1–2 minutos» en lugar de volver a
  compilar desde cero. Compilar es inevitable (ver «Restricciones»); lo que se pide es que sea solo
  lo que cambió.
- **Segura:** la versión instalada sigue funcionando intacta hasta que la nueva esté lista del todo.
  Si la actualización falla a medias (sin internet, PC apagada, error de compilación), el jugador
  sigue jugando con la que tenía y puede reintentar.
- **Plataformas:** Windows, Linux y Steam Deck.
- **Volver atrás:** si la versión nueva compila bien pero sale con un fallo (no arranca, un bug), el
  launcher conserva la versión anterior (~1,5 GB más en disco) y ofrece «Volver a vX.Y.Z». Solo se
  guarda una versión anterior. Las partidas y los mods son los mismos para las dos.
- **Copia de partidas antes de actualizar (14 nov):** como una versión nueva puede guardar en un
  formato que la anterior no lee (v0.1.7 cambió las partidas de temporada y MLB Life), antes del
  primer arranque de la versión nueva el launcher copia `UserData\NAND`, `UserData\ModSaves`,
  `UserData\arrange` y `Config.toml` (~35 MB en la PC de pruebas; el caché no). Al «Volver a vX.Y.Z»
  pregunta si recuperar las partidas de antes de actualizar, avisando de que lo jugado después en la
  versión nueva se pierde. Solo se guarda una copia, la de la última actualización. `mods` no se copia
  (no lo escribe el juego).

## Enfoques considerados (14 nov)

Se compararon tres formas; el usuario eligió la **B**.

- **A. Volver a pasar el Setup entero sin preguntas.** El launcher baja el Setup (346 MB: llvm-mingw
  105, dependencias 63, PowerUp+ 50, CMake 34, traductor 29, Uniform Studio 24; fuentes ~3 MB) y lo
  corre con la ISO y la carpeta conocidas. Simple, pero cada versión baja 346 MB y compila todo.
- **B. Paquete pequeño + compilación incremental (elegida).** Se baja solo lo que cambió, se conserva
  lo compilado en la instalación y se recompila lo afectado. Es lo que más se parece a cómo actualizan
  otros ports (Skate 3 Recomp, Unleashed Recompiled, Dusklight: el trabajo pesado —extraer la ISO— se
  hace una vez y la actualización solo cambia lo nuevo), con la diferencia de que ellos publican el
  `.exe` ya compilado y aquí no se puede (ver «Restricciones»).
- **C. Solo avisar y enlazar la descarga.** Casi gratis, pero no evita bajar y compilar desde cero.

## Objetivos

1. Ver la versión instalada en el launcher.
2. Saber, al abrir el launcher, si hay una versión nueva (y poder comprobarlo a mano).
3. Actualizar con un botón: descarga incremental + compilación incremental, con progreso en MB y en
   pasos de compilación.
4. No perder nunca nada del jugador ni dejarlo sin juego.
5. Que una actualización normal (solo cambia nuestro runtime) baje decenas de MB y tarde 1–2 min de
   compilación en una PC como la de pruebas (Ryzen 5 5500, 12 hilos).

## No objetivos

- Actualizar la Switch.
- Distribuir el juego ya compilado (ver «Restricciones»).
- Separar el runtime en una DLL para no compilar nada: es un cambio de arquitectura grande y muchos
  parches tocan también el código traducido. Se descartó en la conversación.
- Actualizar el traductor, la cadena de herramientas o las dependencias por otra vía que no sea
  volver a descargarlas: cuando cambien, la actualización simplemente pesa más y compila más.

## Lo que sería un fracaso aunque funcione

- Que un jugador se quede sin poder jugar por una actualización a medias.
- Que se pierda o se sobrescriba cualquier dato del jugador (`UserData`, `mods`, la ISO elegida).
- Que la actualización publique o descargue algo con contenido del juego.
- Que el antivirus (Defender) la marque: ya pasó con el Setup v0.1.5, que Defender puso en cuarentena
  a mitad de una reinstalación; en v0.1.6 el Setup y el launcher dejaron de arrancar procesos con
  `CREATE_SUSPENDED` + `ResumeThread` (`tools/setup/setup.cpp`, `PROC_THREAD_ATTRIBUTE_JOB_LIST`).
- Que avise de versiones que no lo son (borradores, la Switch, un prerelease) o que moleste en cada
  arranque si el jugador no quiere actualizar todavía.

## Restricciones

- **Nada del juego se distribuye** (`AGENTS.md`, `tools/check_no_game_data.py`). El `.exe` del juego
  lleva el código traducido del `main.dol` del jugador, así que no se puede publicar compilado: cada
  PC compila el suyo. Las actualizaciones solo pueden traer lo que ya trae el payload del Setup
  (fuentes del runtime parcheadas, dependencias, herramientas, launcher, estudios).
- **Cómo instala hoy el Setup (Windows):** `tools/setup/LocalBuild.ps1` corre dentro de la carpeta del
  payload descomprimido (`<payload>\toolkit`, `<payload>\workspace`), extrae la ISO en
  `<instalación>\Data`, traduce a `workspace\generated`, compila en `workspace\build` e instala en
  `<instalación>\Game`; al final **borra `build` y `generated`** salvo con `-KeepWorkspace`. El payload
  está donde el jugador lo descomprimió y puede borrarlo después. Hoy, por tanto, una instalación no
  conserva nada con lo que recompilar.
- **Linux/Steam Deck:** el kit (`tools/linux/make-kits.sh`, `tools/linux/build.sh`) es un `git archive`
  del commit con un instalador; deja su árbol `local/` dentro de la carpeta del kit y ya recompila un
  cambio del runtime en menos de un minuto (`docs/en/linux.md`). La Steam Deck compila dentro de un
  contenedor podman.
- **Publicación:** los releases salen en GitHub
  (`HopeAero/MLBPowerPros2008Recomp`, título «vX.Y.Z beta — …», no prerelease, marcado Latest), con
  `MLB-Power-Pros-2008-Setup-vX.Y.Z.zip`, los dos kits, el complemento de Blender y `SHA256SUMS.txt`.
  El payload se arma con `tools/setup/Make-Release.ps1` desde un clon limpio.
- **El traductor ya no reescribe lo que no cambió:** los builds recientes informan «0 archivos traducidos
  actualizados, 48.223 sin cambios» (`docs/traspaso.md`), así que Ninja no recompila el código traducido
  si el traductor y el `main.dol` no cambian. El plan lo comprueba en el camino de Windows
  (`LocalBuild.ps1`).
- **Enlace a la Discussion:** la API de GitHub devuelve `discussion_url: null` en los releases (la
  Discussion se crea aparte, p. ej. #10 para v0.1.7); el enlace solo está en el texto de las notas.
- **Ya existe una descarga en el launcher:** el calendario de temporadas usa el `curl` del sistema
  (Windows 10+ y Linux; `wiicompiled-season-schedule-download.patch`). No hace falta una librería HTTP.
- **Windows PowerShell 5.1** ejecuta `LocalBuild.ps1`; sus trampas conocidas están en `docs/trampas.md`.
- **El launcher es parte del `.exe`** del juego (pre-boot launcher, parches `wiicompiled-launcher*`), así
  que no puede sustituirse a sí mismo mientras corre.

## Llamadas mías (el usuario no las decidió; revisables)

- **Fuente de la versión nueva:** la API de releases de GitHub del repositorio (el último release no
  borrador y no prerelease). No hay servidor propio.
- **De dónde sale el enlace a la Discussion:** lo publica el propio release en su manifiesto de
  actualización (un campo con la URL, escrito por `Make-Release.ps1`), en ES/EN si hay dos. Si un
  release no lo trae, el launcher usa el primer enlace `…/discussions/N` de las notas; si tampoco hay,
  enlaza a la página del release.
- **Aviso en ES/EN:** el texto sigue el idioma del launcher (`[ui] language`).
- **Cuántas veces sale la ventana Sí/No:** una vez por versión nueva. Tras «No», no vuelve a saltar en
  cada arranque; queda una marca fija «Nueva versión vX.Y.Z» junto a la versión instalada, que vuelve a
  abrir el aviso al pulsarla. Si sale otra versión más nueva, la ventana salta otra vez. Se guarda la
  última versión avisada en `Config.toml`.
- **Solo cuenta el último release (decisión del usuario, 14 nov):** da igual cuántas versiones atrás
  vaya el jugador; el launcher solo mira y baja del **último** release, que trae todo. No hay cadenas
  de paquetes ni dependencia de releases viejos.
- **Cómo trae «todo» sin bajar 346 MB:** el último release publica el payload partido en piezas, como
  archivos del release (nada de repos, ramas ni Pages aparte): fuentes + parches + launcher (~3–15 MB),
  traductor, dependencias, CMake, llvm-mingw, PowerUp+, estudios. Un manifiesto lista la huella de cada
  pieza (y el enlace a la Discussion). El launcher compara con las huellas de lo instalado y baja solo
  las piezas distintas. Normalmente cambia solo la de fuentes.
- **Qué se descarga:** solo las piezas que difieren de lo instalado, no el zip entero. El primer salto desde una instalación v0.1.7 o anterior no
  tiene piezas compiladas guardadas, así que esa primera actualización hará la compilación completa
  (~4–5 min); desde ahí, las siguientes son incrementales.
- **Qué se guarda en la instalación:** a partir de esta versión, el Setup conserva junto a la
  instalación lo necesario para recompilar (herramientas, fuentes, `generated`, `build`), en una
  carpeta propia de la instalación, no en la del payload descomprimido. Cuánto ocupa se mide en el
  plan; el usuario lo sabrá antes de aprobar el plan.
- **Instalación nueva al lado, cambio al final:** la versión nueva se compila aparte y solo sustituye
  a la vieja cuando ha terminado bien; si algo falla, la vieja no se ha tocado.
- **Reinicio:** al terminar, el launcher se cierra y se abre ya en la versión nueva.
- **Aviso discreto:** el aviso no bloquea JUGAR; tras «No» solo queda la marca fija (ver «Cuántas veces
  sale la ventana Sí/No»).
- **Verificación:** lo descargado se comprueba contra las sumas SHA-256 publicadas con el release.
- **Instalaciones v0.1.7 y anteriores:** no tienen código de actualización, así que pasan una vez por el
  Setup de la versión que traiga esto («REINSTALAR» en la misma carpeta, que ya conserva `UserData` y
  `mods`). Desde ahí se actualizan con el launcher. No hay un actualizador aparte para ellas.
- **«1–2 minutos» es un objetivo, no una garantía:** vale para las versiones que solo cambian el runtime.
  Si una versión cambia el traductor o cabeceras que incluye todo el código traducido, la compilación
  es casi completa (~4–5 min). El launcher muestra antes de empezar el tamaño de la descarga y si será
  rápida o completa. El plan mide en releases pasados cuántos habrían sido rápidos.
- **Qué instalaciones de Linux se actualizan:** las del kit (`install.sh`) y las de la Steam Deck con su
  contenedor (`install-steam-deck.sh`), que conservan la carpeta del kit con `local/`. Las demás formas
  de `docs/en/linux.md` (un clon de git con `build.sh`, o compilar en una PC y copiar a la Deck) no:
  el launcher solo avisa y remite al kit nuevo. Si un release cambia el `Containerfile`, la
  actualización en la Deck reconstruye la imagen y lo dice antes (es más lenta).
- **Sin jugar mientras tanto:** la actualización corre en primer plano dentro del launcher, con su barra,
  y JUGAR espera a que termine o a que se cancele. Así la compilación, que usa todos los núcleos, no
  compite con el juego.

## Lo que queda al constructor

- Dónde exactamente se ve la versión y el aviso en la interfaz del launcher (siguiendo su estilo).
- El formato de lo que se publica para las actualizaciones incrementales (manifiesto con la huella de
  cada archivo, paquete por versión, etc.) y cómo lo genera `Make-Release.ps1`.
- Cómo se lanza la compilación desde el launcher y cómo se sustituye el `.exe` en uso (proceso
  auxiliar, script, etc.), sin las técnicas que marcan los antivirus.
- Cómo se adapta en Linux y en la Steam Deck (contenedor) con la misma experiencia.
- Cómo se prueba (incluida una instalación v0.1.7 que se actualiza, y un fallo provocado a mitad).
