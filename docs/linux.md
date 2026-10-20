# Linux y Steam Deck

*English version: [en/linux.md](en/linux.md)*

El juego se compila y funciona de forma nativa en Linux x86_64: el mismo código traducido y el mismo runtime que en Windows, con Vulkan a través de Dawn, SDL3 para la ventana, los mandos y el audio (PipeWire, PulseAudio o ALSA), y el mismo launcher, mods, Texture Studio y editor de rosters. Todavía no hay ventana de Setup para Linux: un script hace lo mismo que el Setup.

Igual que en Windows, **no se distribuye nada del juego**: el script extrae, traduce y compila tu propia copia en tu equipo. Por eso tampoco hay una descarga lista para Linux: el programa que produce contiene el código traducido del juego.

## Estado (octubre de 2026)

Lo que se midió en un PC (Fedora 44, Ryzen 7 8840HS, Radeon 780M con RADV/Mesa 26.2, Wayland/KDE), el 18 de octubre:

| Área | Estado |
|---|---|
| Compilación | `tools/linux/build.sh` desde un clon limpio hasta el juego instalado. Los parches de Windows se aplican sin cambios; un parche más, `wiicompiled-linux`, añade lo propio de Linux |
| Arranque → pantallas iniciales | funciona, 60 FPS |
| Launcher | funciona: fuentes encontradas con fontconfig, idioma según `LANG`, disco detectado. Diseño comprobado a 854x480 y 1280x800 |
| Partido | el partido de prueba automático Yankees–Colorado (`WIICOMPILED_MATCH_BENCH=1`): 30 s a 2x, 16:9, Vulkan: **59,9 FPS**, tiempo de fotograma p99 17,0 ms, GPU 5,1 ms por fotograma |
| Audio | un flujo de SDL suena por PipeWire (visto con `pactl list sink-inputs`); no se comprobó de oído |
| Texture Studio | compila y pasa su autoprueba (`TextureStudio --selftest`: 204 comprobaciones); la ventana y la captura desde el juego no se probaron a mano |
| Compilación portable (`--container`) | funciona en este PC (59,9 FPS en el mismo partido) y en una Steam Deck (ver abajo); necesita glibc 2.35 y no depende de libstdc++ |
| Steam Deck (LCD, SteamOS 3.8.28, glibc 2.41) | la compilación portable, copiada desde el PC: se resuelven todas las bibliotecas; el partido de prueba en modo escritorio a 2x, 16:9, Vulkan (RADV VANGOGH): **59,9 FPS**, tiempo de fotograma p50 16,7 ms, p99 24,4 ms, GPU 6,6 ms por fotograma |
| Steam Deck, a mano | modo juego: pantalla completa, los controles integrados y el sonido funcionan (según quien lo probó, 18 de octubre) |
| Partidas | funcionan tras el arreglo de `wiicompiled-mlb-save-dir` (abajo): guardar en el juego escribe `pm3usma.dat` (868 KB). Antes, toda partida decía "Save failed" |
| Editor de rosters (PowerUp+) | compila para Linux (`linux-x64`, autónomo). Con su API, en este PC: el roster base se carga del `project.bin` del jugador, un roster se exporta como mod de 2008 a `mods/`, y el juego lo activa, lo convierte y reconstruye `PROJECT.BIN` (visto en su registro). A mano: EDITAR ROSTER del launcher lo abre en el navegador y la edición funciona (según quien lo probó, 18 de octubre) |
| **Sin probar** | compilar en la Deck; el editor de rosters en la Deck (se abre en un navegador: usa el modo escritorio); sesiones X11; GPU NVIDIA e Intel; los modos temporada, liga y MLB Life |
| Sin portar | los contadores de CPU/GPU/RAM de la prueba dicen "n/d" (vienen de los contadores de rendimiento de Windows) |

## Compilar en un PC con Linux

Requisitos: git, Python 3, el SDK de .NET 8 o más nuevo (el de .NET 10 para compilar también el editor de rosters), CMake 3.24+, Ninja, Clang, pkg-config y los archivos de desarrollo que SDL necesita para el sonido y las ventanas. Unos 3 GB en `local/` (4,5 GB si hay que expandir un GCZ) y 1,5 GB para el juego instalado; en el mismo sistema de archivos que `local/`, los archivos del disco del juego instalado son enlaces duros y no ocupan más.

```sh
# Fedora
sudo dnf install git python3 dotnet-sdk-10.0 cmake ninja-build clang lld pkgconf-pkg-config \
    pipewire-devel pulseaudio-libs-devel alsa-lib-devel libXScrnSaver-devel libdecor-devel \
    wayland-devel libxkbcommon-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
    libXi-devel libXfixes-devel libXtst-devel mesa-libgbm-devel libdrm-devel systemd-devel dbus-devel \
    libusb1-devel libpng-devel zlib-devel freetype-devel
# Debian / Ubuntu 24.04+
sudo apt install git python3 dotnet-sdk-10.0 cmake ninja-build clang lld pkg-config curl \
    libpipewire-0.3-dev libpulse-dev libasound2-dev libxss-dev libdecor-0-dev libwayland-dev \
    wayland-protocols libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxfixes-dev libxtst-dev libgbm-dev libdrm-dev libudev-dev libdbus-1-dev libusb-1.0-0-dev \
    libpng-dev zlib1g-dev libfreetype-dev
```

Después, desde el repositorio:

```sh
tools/linux/build.sh --disc '/ruta/a/tu copia.rvz' --install ~/Games/MLBPowerPros2008 --desktop-entry
~/Games/MLBPowerPros2008/MLBPowerPros2008
```

El script comprueba que el disco sea un `RL8E54` limpio (ISO, RVZ, WIA, WBFS, CISO, NKit, GCZ), lo extrae, clona Wiicompiled en el commit fijado, aplica los parches en el mismo orden que `tools/wiicompiled-translate.ps1` y después `wiicompiled-linux`, traduce, compila e instala. Medido aquí, con 16 hilos: traducción 40 s, una primera compilación de unos 4 minutos (el runtime, SDL y las dependencias de aurora; Dawn viene precompilado), una recompilación tras un cambio del runtime en menos de un minuto. Registros: `local/analysis/linux-*.log`.

- nodtool rechaza los GCZ antiguos de Dolphin con bloques de 16 KiB; el script los expande antes a un ISO en `local/images/` (`tools/gcz_to_iso.py`, que comprueba la suma de verificación de cada bloque).
- El editor de rosters (PowerUp+) se compila desde su commit fijado con `tools/patches/powerupplus-*.patch`, como hace `tools/powerupplus.ps1 -Publish` en Windows; Node.js 22 para su interfaz web se descarga en `local/tools` y se comprueba con SHA-256. Sin el SDK de .NET 10 el script lo omite con un aviso, y el launcher no ofrece EDITAR ROSTER. `--no-roster-editor` lo omite a propósito.
- El script no compila sin los archivos de desarrollo de PipeWire, PulseAudio o ALSA: si no, SDL haría un juego sin sonido.
- **Sin root:** si no puedes instalar los paquetes `-devel`, extráelos en `local/sysdeps/` y el script los usa (Fedora: `dnf download <paquetes>`, luego `rpm2cpio <rpm> | cpio -idm` dentro de `local/sysdeps`, y apunta las líneas `prefix=`/`libdir=` de `local/sysdeps/usr/lib64/pkgconfig/*.pc` a esa carpeta). Solo hacen falta las cabeceras: SDL carga las bibliotecas de sonido al ejecutarse. O usa `--container`.

La carpeta instalada es autónoma y se puede mover: `Config.toml` apunta a `../Data`. `portable.txt` deja la configuración, las partidas (NAND) y las cachés en `UserData/`, junto al juego.

## Steam Deck

La compilación normal depende de la glibc del equipo que compila (aquí Fedora 44: 2.43), más nueva que la de SteamOS, así que no arranca en una Deck. `--container` compila dentro de `tools/linux/Containerfile` (Ubuntu 22.04: glibc 2.35, clang 20, con libstdc++ enlazada de forma estática, y el SDK de .NET 10). El resultado funciona en SteamOS y en cualquier distribución actual, y el equipo solo necesita podman o docker, sin compiladores ni paquetes `-devel`.

**Opción A: compilar en un PC con Linux y copiar la carpeta.**

```sh
tools/linux/build.sh --container --disc '/ruta/a/tu copia.rvz' --install ~/Games/MLBPowerPros2008
rsync -a ~/Games/MLBPowerPros2008 deck@steamdeck:~/Games/     # o con un USB o una tarjeta SD
```

**Opción B: compilar en la Deck** (modo escritorio, Konsole). SteamOS 3.5 y posteriores traen podman: compruébalo con `podman --version`. La compilación es más lenta con los 4 núcleos de la Deck.

```sh
git clone https://github.com/HopeAero/MLBPowerPros2008Recomp.git && cd MLBPowerPros2008Recomp
tools/linux/build.sh --container --disc '/run/media/<tarjeta SD>/MLB Power Pros 2008.rvz'   # donde esté tu copia
```

**Añadirlo al modo juego:** en el modo escritorio abre Steam → *Juegos* → *Añadir un juego que no es de Steam a mi biblioteca* → *Examinar* → `~/Games/MLBPowerPros2008/MLBPowerPros2008`. En sus *Propiedades*, comprueba que *Iniciar en* sea esa carpeta (`~/Games/MLBPowerPros2008/`). **No** elijas una herramienta de compatibilidad (Proton): es un programa nativo de Linux. De vuelta en el modo juego arranca a pantalla completa bajo gamescope. Los controles de la Deck llegan al juego como un mando estándar a través de Steam Input; el launcher se maneja con mando.

**Medido en una Steam Deck** (modelo LCD, SteamOS 3.8.28, 18 de octubre), con la compilación portable de la opción A: el partido de prueba automático en modo escritorio a 2x, 16:9, Vulkan fue a 59,9 FPS (p50 16,7 ms, p99 24,4 ms; GPU 6,6 ms de los 16,7 ms del fotograma), así que 2x es un valor seguro. El p99 es más alto que en el PC (17,0 ms): algunos fotogramas largos, quizá la compilación de shaders la primera vez (la caché queda en `UserData/Cache`); no se investigó. Jugado a mano en el modo juego: la pantalla completa, los controles integrados y el sonido funcionan. **Sin probar en la Deck:** 3x y 4x, el consumo de batería, la suspensión, compilar en la Deck (opción B) y el editor de rosters, que se abre en un navegador y por eso va en el modo escritorio.

Una NAND creada por una compilación anterior tiene un `setting.txt` europeo (ver abajo); se puede guardar igual, pero apartar `UserData/NAND` antes del primer inicio da una nueva, de EE. UU.

## Qué cambió para Linux

- `tools/patches/wiicompiled-linux.patch` (se aplica después de los demás):
  - las fuentes del launcher recurren a fontconfig (`fc-match`) cuando no están las rutas de DejaVu (Fedora y SteamOS);
  - MODS → abrir carpeta usa `xdg-open`;
  - HERRAMIENTAS → editor de texturas abre `TextureStudio` junto al juego;
  - HERRAMIENTAS → EDITAR ROSTER inicia el servidor de PowerUp+ incluido una vez por sesión (recibe `SIGTERM` cuando el juego se cierra, como el job object en Windows) y lo abre con `xdg-open`; la opción solo aparece si `powerupplus/` está instalado;
  - el `LD_PRELOAD` de Steam no pasa a estas herramientas.
- `tools/patches/wiicompiled-mlb-save-dir.patch` (no es propio de Linux; está en la lista común. Probado también en Windows el 20 de octubre de 2026: con una NAND nueva y sin mods, guardar una partida de Temporada decía «Save failed» y con el parche escribe `pm3usse0.dat` y un `banner.bin` de 32 KB): el juego lista su carpeta de datos del título (`/title/00010000/524c3845/data`) antes de su primera partida y dice "Save failed" si no existe; nunca la crea, la crea el sistema de la consola. Ahora la crea el runtime. Las NAND nuevas reciben además el `setting.txt` de EE. UU. de Dolphin (`AREA=USA`, `VIDEO=NTSC`) en vez del europeo de Mario Kart; un `setting.txt` existente se conserva.
- Texture Studio (`tools/texture-studio`): ramas POSIX para leer los argumentos, abrir carpetas y abrir el juego para capturar texturas (`posix_spawn` con `WIICOMPILED_TEXTURE_DUMPS=1`). El código de Windows no cambia.
- `tools/linux/build.sh`, `tools/linux/Containerfile` y `tools/gcz_to_iso.py` son nuevos. De los scripts de Windows solo cambió la lista de parches de `wiicompiled-translate.ps1` (`wiicompiled-mlb-save-dir`). El script de Linux lee la lista de parches de `wiicompiled-translate.ps1`, así que las dos plataformas aplican los mismos parches.

Wiicompiled ya funcionaba en Linux en el commit fijado (fibras libco System V, Vulkan con Dawn, SDL3 enlazado de forma estática); los parches de MLB compilan sobre él sin cambios.
