# MLB Power Pros 2008 Recomp

*English version: [README.md](README.md)*

Una **recompilación estática** de *MLB Power Pros 2008* (Wii, USA, `RL8E54`) a un programa nativo de Windows x64 y Linux x86_64, sobre el traductor y el runtime de [Wiicompiled](https://github.com/patchzyy/Wiicompiled), con launcher propio, mods y herramientas para editar texturas, rosters y estadios.

**No es un emulador:** no hay intérprete de CPU ni JIT. El código PowerPC del juego (`main.dol` y `ovl_main.rel`) se traduce a C++ antes de jugar y se compila para la PC; lo que daba la consola (sistema operativo, gráficos, audio, disco, memoria NAND, mandos) lo reimplementa el runtime.

> [!IMPORTANT]
> **Este repositorio no contiene nada del juego:** ni la imagen del disco, ni el ejecutable, ni assets (texturas, modelos, audio, video, fuentes), ni código traducido, ni partidas. Hace falta tu propia copia legal del juego. El Setup traduce y compila el juego **en tu PC** a partir de tu copia, y nada de eso sale de ella. No pidas ni compartas archivos del juego.

> [!NOTE]
> **Hecho casi por completo con IA.** Casi todo el código, las herramientas, la ingeniería inversa y la documentación los escribió Claude (Anthropic) mediante Claude Code, dirigido, probado y revisado por una persona. Ver [Cómo se hizo](#cómo-se-hizo).

![El launcher: jugar, ajustes, mods y salir](docs/media/launcher-es.jpg)

## Estado (octubre de 2026)

**Beta, Windows; Linux y Steam Deck experimentales.** Lo que dice esta tabla está medido en la PC de desarrollo; lo que no se comprobó, lo dice.

| Área | Estado |
|---|---|
| Arranque → correa → menús | funciona, a 60 FPS |
| Partidos (Exhibición), Home Run, alineaciones | funcionan; se juegan partidos completos. Partido de prueba automático: 60 FPS con Direct3D 12 y con Vulkan |
| Temporada, Liga, MLB Life | se alcanzan (MLB Life también con rosters de mods); sin pruebas sistemáticas de cada modo. El modo Temporada puede pasar de sus 10 temporadas (**AJUSTES > MEJORAS > TEMPORADAS**: 20 o sin límite, experimental): una temporada entera simulada en 2029, con la Serie Mundial, hasta 2030. Calendarios reales de MLB 2002-2026, DH en ambas ligas y ligas de 15 y 15 (experimental), también en MLB Life |
| Gráficos | Direct3D 12 o Vulkan (elección automática), resolución interna de 1x a 4x, panorámico 16:9 real, llenar pantalla sin franjas negras, AMD FSR 1, filtro anisotrópico hasta 16x, interpolación a 120 o 180 FPS (experimental), ajustes de calidad |
| Audio | completo, comentarista incluido |
| Mandos | cualquier mando por SDL3, o teclado y ratón, como mando de GameCube; hasta 4 jugadores; reasignación al estilo de Dolphin; nombres de botones de PlayStation o Xbox |
| Mods | texturas (también en HD), archivos de `project.bin` (por ejemplo uniformes), rosters de 2008, estadios editados en Blender y **estadios nuevos** en Exhibición. Sin probar: `riivolution/` |
| Instalación | el Setup verifica tu copia, la extrae, la traduce y la compila: unos **4,5 minutos** (12 hilos). Verificada en limpio, en español y en inglés, en la PC de desarrollo; **falta probarla en otra PC** |
| Código traducido | 48.219 funciones de `main.dol` y `ovl_main.rel`, ninguna sin soporte; 314 funciones del sistema enlazadas a código nativo |
| Idiomas | launcher, Setup, Texture Studio y complemento de Blender en español e inglés (según el sistema, o a elegir); documentación en los dos idiomas |
| Discord Rich Presence | inactivo hasta tener un ID de aplicación propio (ver [Configuración](#configuración)) |
| Linux | compila y se juega en un PC con Fedora (Vulkan, 60 FPS en el partido de prueba) con `tools/linux/build.sh`; Steam Deck con una compilación portable: 60 FPS a 2x en el partido de prueba en una Deck, y jugado en el modo juego con sus controles. El editor de rosters también compila. Ver [docs/linux.md](docs/linux.md) |
| Switch (homebrew) | planeado, sin empezar |

### Problemas conocidos

- Estadios nuevos: la línea «Home of the…» es la del estadio base, solo aparecen en Exhibición (no en Temporada, Liga ni MLB Life) y el público hecho a mano no se dibuja.
- La resolución interna 4x pinta casi a 4K: en GPUs modestas (por ejemplo una RX 5500) lleva la GPU al 80 % o más. «Según la ventana» es lo recomendado.
- Cambiar la API gráfica o los mods con el juego abierto pide reiniciarlo (el launcher lo hace solo).
- En pruebas automáticas se vieron 2 cierres sin error al arrancar (de unos 9, el 13 de octubre); después de un arreglo no se repitieron, pero la causa no está demostrada.

## Instalar y jugar

Requisitos: Windows 10/11 de 64 bits, una GPU con Direct3D 12 o Vulkan, tu copia de *MLB Power Pros 2008* para Wii (USA, `RL8E54`) y unos 2,5 GB libres (el Setup ocupa 900 MB y el juego instalado 1,5 GB).

1. Descarga `MLB-Power-Pros-2008-Setup-v0.1.5.zip` de [Releases](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases) (la última: `v0.1.5` beta) y descomprímelo en una carpeta tuya (no en Archivos de programa).
2. Abre `MLBPowerPros2008-Setup.exe`, pulsa **ELEGIR DISCO** y elige tu copia (ISO, NKit, RVZ, WBFS, WIA, GCZ o CISO).
3. Pulsa **INSTALAR**. El Setup comprueba que sea un volcado limpio de `RL8E54`, lo extrae, traduce el código y lo compila en tu PC.
4. Pulsa **JUGAR** o usa el acceso directo del escritorio. Antes de cada partida se abre el launcher: API gráfica, pantalla, resolución, audio, controles, mods y herramientas.

![El Setup: elige tu disco; no incluye nada del juego](docs/media/setup-es.jpg)

**Linux y Steam Deck** (experimental): todavía sin ventana de Setup; un script hace lo mismo desde un clon de este repositorio, y luego se añade el juego a Steam como juego que no es de Steam:

```sh
tools/linux/build.sh --disc '<tu copia>.rvz'               # este PC
tools/linux/build.sh --container --disc '<tu copia>.rvz'   # compilación portable para una Steam Deck / SteamOS
```

Requisitos, los pasos para la Steam Deck y lo que se probó: [docs/linux.md](docs/linux.md).

## Cómo funciona

```
tu copia del juego ──► nodtool ──► main.dol + ovl_main.rel (+ los archivos del disco)
                                        │
                                        ▼
               Translator.Cli (Wiicompiled)      PowerPC ─► C++, una función de C++ por función del juego
                                        │
                                        ▼
               clang (llvm-mingw) ──► MLBPowerPros2008.exe
                                        │
               runtime (Wiicompiled + parches de este proyecto):
                 HLE del SDK de Wii        OS, hilos, alarmas, DVD, NAND, IOS, audio AX/DSP, VI, mandos
                 aurora                    GX ─► WebGPU (Dawn) ─► Direct3D 12 / Vulkan
                 nativos de MLB            cargador del REL, launcher, mods, roster, estadios nuevos
```

Tres capas de distinta naturaleza:

- **El código del juego** se recompila, no se emula. Es una traducción mecánica: completa y fiel, pero no es código fuente legible.
- **Las llamadas al sistema** de la Wii se reimplementan en Windows. Wiicompiled se hizo para Mario Kart Wii; aquí se reasignan sus 314 funciones nativas a las direcciones de MLB con un mapa de funciones propio (`config/wiicompiled/rl8e54-functions.map`), y los fallos propios de MLB se corrigen con parches.
- **Los gráficos** se traducen a nivel de API (GX), no de hardware: aurora dibuja con WebGPU sobre Direct3D 12 o Vulkan.

Todos los cambios al runtime son **parches reproducibles** sobre el commit fijado de Wiicompiled (`tools/patches/wiicompiled-*.patch`, ver `config/tools.lock.json`).

## Funciones

**Launcher propio** (antes de cada partida, también con mando)
- Vídeo: API gráfica, ventana o pantalla completa, resolución interna, AMD FSR 1, filtro anisotrópico, forzar 16:9, llenar pantalla, interpolación, ajustes de calidad.
- Audio, controles al estilo de Dolphin (puertas de los sticks, zona muerta, prueba de sticks) y opciones de interfaz.
- Mods: se activan o desactivan con un interruptor por carpeta.
- Herramientas: abre el editor de rosters y el Texture Studio.
- Arte original del pack de interfaz MoonInning (no es contenido del juego).

**Herramientas incluidas**
- **Editor de rosters** (PowerUp+ adaptado a 2008): importa y exporta rosters de 2008. Ver [docs/roster-2008.md](docs/roster-2008.md).
- **Power Pros Wii 2008 Texture Studio**: hace mods de texturas, también en HD, y las captura desde el juego en marcha. Ver [docs/texture-studio.md](docs/texture-studio.md).
- **Complemento de Blender** (4.2 LTS o posterior): importa y exporta estadios, con su colisión, y crea estadios nuevos. Ver [docs/stadiums.md](docs/stadiums.md).

**Mods**: texturas, archivos de `project.bin`, rosters y estadios en `mods/<carpeta>`; las partidas con roster modificado van aparte. Ver [docs/mods.md](docs/mods.md).

## Configuración

Los ajustes están en `UserData/Config.toml`, junto al juego. El launcher cambia casi todos; lo que no tiene opción:

```toml
[discord]
enabled = true
client_id = "" # ID propio de la aplicación de Discord. Sin él, Rich Presence queda inactivo.

[ui]
language = "auto" # "auto" (idioma de Windows), "es" o "en"
```

No se incluye un ID de Discord, y se ignora el heredado de Wiicompiled para no anunciar otra aplicación.

## Compilar desde el código (Windows)

Para quien desarrolla. Requisitos: PowerShell 5.1, Git, .NET 8 SDK y Python 3.12. El resto (llvm-mingw, CMake, Ninja, dependencias de aurora) lo descargan y verifican los scripts dentro de `local/`.

```powershell
.\tools\bootstrap.ps1                                   # dtk y NKit, verificados por SHA-256
.\tools\phase0.ps1 -Image '<tu copia>.iso' -DolphinTool '<Dolphin>\DolphinTool.exe'   # extrae a local/disc
.\tools\wiicompiled-translate.ps1 -Build                # clona Wiicompiled en el commit fijado, aplica los parches y traduce
.\tools\wiicompiled-build.ps1 -RunSeconds 30            # compila y prueba
.\tools\wiicompiled-bench.ps1 -Api vulkan               # tiempo de fotograma en la demo del título
```

- **Release:** `tools/setup/Make-Release.ps1 -Zip` arma el paquete del Setup desde un clon limpio, solo con lo que está en git (unos 20 minutos). `tools/setup/LocalBuild.ps1` es lo que ejecuta el Setup en la PC del jugador.
- **Guardia de contenido:** `python tools/check_no_game_data.py --install-hook` instala el hook de pre-commit que rechaza archivos del juego, contenido binario, archivos grandes y volcados de datos.

## Documentación

| Documento | Contenido |
|---|---|
| [docs/wiicompiled-feasibility.md](docs/wiicompiled-feasibility.md) | De la primera traducción al juego jugable: cada parche, cada fallo y cada medición |
| [docs/ingenieria-inversa.md](docs/ingenieria-inversa.md) | Referencia del juego por sistema: memoria, hilos, `project.bin`, roster, texturas, estadios (PDF con `tools/build_re_pdf.py`) |
| [docs/launcher-controles.md](docs/launcher-controles.md) | Launcher, controles e idioma |
| [docs/mods.md](docs/mods.md) | Tipos de mods y cómo instalarlos |
| [docs/roster-2008.md](docs/roster-2008.md) | Rosters de 2008 y el editor PowerUp+ |
| [docs/texture-studio.md](docs/texture-studio.md) | Texture Studio |
| [docs/stadiums.md](docs/stadiums.md) | Estadios con Blender y cómo hacer uno desde cero |
| [docs/linux.md](docs/linux.md) | Linux y Steam Deck: compilar, instalar, lo que se probó |
| [docs/reverse-engineering-tools.md](docs/reverse-engineering-tools.md) | Ghidra y dtk |

Todos tienen versión en inglés en [docs/en/](docs/en/).

## Estructura del repositorio

| Carpeta | Contenido |
|---|---|
| `tools/` | traducción, compilación, Setup, launcher, mods y análisis |
| `tools/patches/` | parches reproducibles sobre Wiicompiled y PowerUp+ |
| `tools/setup/` | el Setup (`setup.cpp`), lo que corre en la PC del jugador (`LocalBuild.ps1`) y el empaquetado |
| `tools/texture-studio/`, `tools/blender/`, `tools/roster/` | Texture Studio, complemento de Blender y conversión de rosters |
| `config/` | identidad del juego, mapa de funciones y versiones fijadas |
| `assets/launcher/` | arte propio del launcher (no es contenido del juego) |
| `docs/` | notas técnicas y resultados comprobables; `docs/en/` en inglés |
| `local/` | disco, herramientas descargadas, código generado, builds y partidas; excluido de git |

El runtime anterior (DolRecomp y ModernGekko, sus parches e informes) está en el tag de git `moderngekko-era`.

## Créditos

- **[Wiicompiled](https://github.com/patchzyy/Wiicompiled)** (patchzyy): el traductor PowerPC → C++, el runtime y sus scripts de empaquetado, sobre los que se construye todo este proyecto.
- **[aurora](https://github.com/encounter/aurora)** (encounter), incluido en Wiicompiled: GX sobre WebGPU. Usa [Dawn](https://dawn.googlesource.com/dawn), [SDL3](https://libsdl.org) y [Dear ImGui](https://github.com/ocornut/imgui).
- **[nod](https://github.com/encounter/nod)** y **[decomp-toolkit](https://github.com/encounter/decomp-toolkit)** (encounter): lectura del disco y análisis del DOL y el REL.
- **[PowerUp](https://github.com/CSho27/PowerUp)** (CSho27) y **[PowerUp+](https://github.com/ogura-connection/PowerUpPlus)** (ogura-connection): el formato del roster de 2007 y el editor que aquí se adapta a 2008.
- **[Dolphin](https://github.com/dolphin-emu/dolphin)**: comparación de comportamiento y su base de firmas del SDK, usada para nombrar funciones. **[DolRecomp](https://github.com/AH64-dll/DolRecomp)** y **[ModernGekko](https://github.com/ExpansionPak/ModernGekko)**: la primera etapa del proyecto.
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)**, **[CMake](https://cmake.org)** y **[Ninja](https://ninja-build.org)**: compilan el juego en la PC del jugador.
- **[AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR)** (MIT): la opción de escalado.
- **MoonInning UI pack**: el arte del launcher y del Setup, aportado por el autor del proyecto.
- **[Blender](https://www.blender.org)** y **[OpenSKP](https://github.com/iamahsanmehmood/openskp)**: edición e importación de estadios.

Versiones, licencias y qué se redistribuye: [THIRD_PARTY.md](THIRD_PARTY.md).

## Cómo se hizo

Este proyecto se hizo con IA. Casi todo el código lo escribieron agentes de Claude (Anthropic) mediante Claude Code: la persona decide qué intentar, juega, prueba y revisa; la IA implementa, mide y documenta.

Dos reglas lo mantienen honesto:
- toda afirmación sale de una prueba o una medición, y este README dice qué se midió y qué no;
- nada derivado del juego entra en el repositorio, y un hook de pre-commit lo vigila.

Es un proyecto de estudio: espera asperezas, y errores que un especialista no habría cometido.

## Cómo colaborar

Toda ayuda es bienvenida: reportes de errores, pruebas con otras GPUs y mandos, código, ingeniería inversa, documentación y traducciones. Lee primero [CONTRIBUTING.es.md](CONTRIBUTING.es.md): nada del juego entra en el repositorio, los cambios al runtime son parches y cada afirmación dice cómo se comprobó.

## Aviso legal

Proyecto de fans, sin relación con Konami, 2K, Major League Baseball, la MLBPA ni Nintendo, ni respaldado o patrocinado por ellos. *MLB Power Pros*, *Wii* y los nombres de equipos y jugadores son marcas de sus dueños y se nombran solo para decir con qué funciona este software. El repositorio y sus releases no contienen datos del juego con derechos de autor, ni claves, ni firmware, y no ayudan a conseguirlos: necesitas tu propia copia del juego.

## Licencia

GNU General Public License v3 (ver [LICENSE](LICENSE)). Los componentes de terceros conservan sus licencias ([THIRD_PARTY.md](THIRD_PARTY.md)). La licencia cubre el código de este proyecto; no concede nada sobre el juego.
