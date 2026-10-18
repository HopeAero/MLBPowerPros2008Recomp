# Linux y Steam Deck

*English version: [en/linux.md](en/linux.md)*

El juego se compila y funciona de forma nativa en Linux x86_64: el mismo código traducido y el mismo runtime que en Windows, con Vulkan a través de Dawn, SDL3 para la ventana, los mandos y el audio (PipeWire, PulseAudio o ALSA), y el mismo launcher, mods y Texture Studio. Todavía no hay ventana de Setup para Linux: un script hace lo mismo que el Setup.

Igual que en Windows, **no se distribuye nada del juego**: el script extrae, traduce y compila tu propia copia en tu equipo. Por eso tampoco hay una descarga lista para Linux: el programa que produce contiene el código traducido del juego.

## Estado (octubre de 2026)

Lo que se midió en un PC (Fedora 44, Ryzen 7 8840HS, Radeon 780M con RADV/Mesa 26.2, Wayland/KDE), el 18 de octubre:

| Área | Estado |
|---|---|
| Compilación | `tools/linux/build.sh` desde un clon limpio hasta el juego instalado. Los 46 parches de Windows se aplican sin cambios; un parche más, `wiicompiled-linux`, añade lo propio de Linux |
| Arranque → pantallas iniciales | funciona, 60 FPS |
| Launcher | funciona: fuentes encontradas con fontconfig, idioma según `LANG`, disco detectado. Diseño comprobado a 854x480 y 1280x800 |
| Partido | el partido de prueba automático Yankees–Colorado (`WIICOMPILED_MATCH_BENCH=1`): 30 s a 2x, 16:9, Vulkan: **59,9 FPS**, tiempo de fotograma p99 17,0 ms, GPU 5,1 ms por fotograma |
| Audio | un flujo de SDL suena por PipeWire (visto con `pactl list sink-inputs`); no se comprobó de oído |
| Texture Studio | compila y pasa su autoprueba (`TextureStudio --selftest`: 204 comprobaciones); la ventana y la captura desde el juego no se probaron a mano |
| Compilación portable (`--container`) | ver [Steam Deck](#steam-deck) |
| **Sin probar** | una Steam Deck real; mandos en Linux (SDL3, el mismo código que en Windows); sesiones X11; GPU NVIDIA e Intel; los modos temporada, liga y MLB Life |
| Sin portar | el editor de rosters (PowerUp+, que solo se compila para Windows): el launcher no ofrece EDITAR ROSTER en Linux. Los mods de roster los convierte el propio juego (`mods/<mod>/roster/`), con el mismo código que en Windows, así que los rosters hechos con PowerUp+ en Windows deberían cargar; no se probó. Los contadores de CPU/GPU/RAM de la prueba dicen "n/d" (vienen de los contadores de rendimiento de Windows) |

## Compilar en un PC con Linux

Requisitos: git, Python 3, el SDK de .NET 8 (o más nuevo), CMake 3.24+, Ninja, Clang, pkg-config y los archivos de desarrollo que SDL necesita para el sonido y las ventanas. Unos 3 GB en `local/` (4,5 GB si hay que expandir un GCZ) y 1,5 GB para el juego instalado; en el mismo sistema de archivos que `local/`, los archivos del disco del juego instalado son enlaces duros y no ocupan más.

```sh
# Fedora
sudo dnf install git python3 dotnet-sdk-8.0 cmake ninja-build clang lld pkgconf-pkg-config \
    pipewire-devel pulseaudio-libs-devel alsa-lib-devel libXScrnSaver-devel libdecor-devel \
    wayland-devel libxkbcommon-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
    libXi-devel libXfixes-devel libXtst-devel mesa-libgbm-devel libdrm-devel systemd-devel dbus-devel \
    libusb1-devel libpng-devel zlib-devel freetype-devel
# Debian / Ubuntu 24.04+
sudo apt install git python3 dotnet-sdk-8.0 cmake ninja-build clang lld pkg-config curl \
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
- El script no compila sin los archivos de desarrollo de PipeWire, PulseAudio o ALSA: si no, SDL haría un juego sin sonido.
- **Sin root:** si no puedes instalar los paquetes `-devel`, extráelos en `local/sysdeps/` y el script los usa (Fedora: `dnf download <paquetes>`, luego `rpm2cpio <rpm> | cpio -idm` dentro de `local/sysdeps`, y apunta las líneas `prefix=`/`libdir=` de `local/sysdeps/usr/lib64/pkgconfig/*.pc` a esa carpeta). Solo hacen falta las cabeceras: SDL carga las bibliotecas de sonido al ejecutarse. O usa `--container`.

La carpeta instalada es autónoma y se puede mover: `Config.toml` apunta a `../Data`. `portable.txt` deja la configuración, las partidas (NAND) y las cachés en `UserData/`, junto al juego.

## Steam Deck

La compilación normal depende de la glibc del equipo que compila (aquí Fedora 44: 2.43), más nueva que la de SteamOS, así que no arranca en una Deck. `--container` compila dentro de `tools/linux/Containerfile` (Ubuntu 22.04: glibc 2.35, clang 20, con libstdc++ enlazada de forma estática). El resultado funciona en SteamOS y en cualquier distribución actual, y el equipo solo necesita podman o docker, sin compiladores ni paquetes `-devel`.

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

En la propia Deck **no se probó**: la compilación, los FPS, el consumo de batería, la suspensión y los valores por defecto de Steam Input. La GPU de la Deck usa el mismo driver Vulkan que el PC de prueba (RADV), y su APU tiene las instrucciones x86-64-v3 que exige la compilación. La 780M del PC de prueba es más rápida que la GPU de la Deck: baja la resolución interna (1x o 2x) si caen los FPS.

## Qué cambió para Linux

- `tools/patches/wiicompiled-linux.patch` (se aplica después de los demás):
  - las fuentes del launcher recurren a fontconfig (`fc-match`) cuando no están las rutas de DejaVu (Fedora y SteamOS);
  - MODS → abrir carpeta usa `xdg-open`;
  - HERRAMIENTAS → editor de texturas abre `TextureStudio` junto al juego, y EDITAR ROSTER se oculta;
  - el `LD_PRELOAD` de Steam no pasa a estas herramientas.
- Texture Studio (`tools/texture-studio`): ramas POSIX para leer los argumentos, abrir carpetas y abrir el juego para capturar texturas (`posix_spawn` con `WIICOMPILED_TEXTURE_DUMPS=1`). El código de Windows no cambia.
- `tools/linux/build.sh`, `tools/linux/Containerfile` y `tools/gcz_to_iso.py` son nuevos. Los scripts de Windows no cambiaron. El script de Linux lee la lista de parches de `wiicompiled-translate.ps1`, así que las dos plataformas aplican los mismos parches.

Wiicompiled ya funcionaba en Linux en el commit fijado (fibras libco System V, Vulkan con Dawn, SDL3 enlazado de forma estática); los parches de MLB compilan sobre él sin cambios.
