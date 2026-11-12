# Roadmap

*English: [ROADMAP.md](ROADMAP.md)*

**Este archivo es la fuente de verdad** de qué está hecho, en curso y planificado. Cada línea de trabajo tiene un plan en [`docs/roadmap/`](docs/roadmap/) con sus fases, cómo se verifica y sus casillas. Los fallos van en [Issues](https://github.com/HopeAero/MLBPowerPros2008Recomp/issues); las ideas y preguntas, en [Discussions](https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions). Cuando algo termina, pasa a **Hecho** con la versión que lo trajo.

Para comentarlo: [discusión del roadmap](https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions/7).

Actualizado: 11 de noviembre de 2026 · última versión [v0.1.7](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.7) (PC) y [Switch v0.1.1](https://github.com/HopeAero/MLBPR2008-NX/releases/tag/v0.1.1).

Dos reglas valen para todo: el juego sigue **fiel al original de Wii por defecto** (lo que cambie el juego se activa en el launcher), y **nunca se distribuyen archivos del juego**: en PC cada jugador lo compila desde su propio disco; en Switch se publica el NRO y el jugador pone su propio disco (ver [decisiones](docs/decisiones.md)).

## Ahora

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [Corrección de fallos](docs/roadmap/fallos.md) | Lo que reportan los jugadores, ordenado y arreglado en versiones de corrección | Continuo |
| [Rendimiento en PCs modestas](docs/roadmap/rendimiento.md) | Medir y bajar el coste de CPU y GPU para mantener 60 FPS en gráficas integradas y CPUs antiguas | En curso: la v0.1.6 trajo a PC las optimizaciones de Switch (13 % menos CPU en el hilo del juego) y opciones gráficas más ligeras |
| [OpenGL](docs/roadmap/opengl.md) | Un backend gráfico OpenGL / OpenGL ES para gráficas sin Vulkan ni Direct3D 12 | Hecho: OpenGL ES en Linux y Direct3D 11 en Windows juegan un partido y se eligen en el launcher; falta comprobarlo en Linux nativo y en una gráfica solo Direct3D 11 |

## Siguiente

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [ABS: desafíos de bola y strike](docs/roadmap/abs.md) | El sistema de desafíos de MLB 2026 (2 por equipo, umpire automático), opcional en todos los modos | Planificado: investigación hecha |
| [Herramientas de modding](docs/roadmap/herramientas-modding.md) | Mejoras al Texture Studio, al Editor de temporada, a los rosters y estadios; estadios propios en Temporada y MLB Life | Continuo |

## Investigación

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [Mods de código](docs/roadmap/mods-de-codigo.md) | Mods que cambian la lógica del juego (torneos, reglas, pantallas nuevas) con una API de mods documentada, no solo datos | Investigación |

## Hecho

| Versión | Lo principal |
|---|---|
| [Switch v0.1.1](https://github.com/HopeAero/MLBPR2008-NX/releases/tag/v0.1.1) | Mods de callnames en la Switch; arreglo de la creación de dos vías en MLB Life |
| [Switch v0.1.0](https://github.com/HopeAero/MLBPR2008-NX/releases/tag/v0.1.0) | [Nintendo Switch](docs/roadmap/switch.md): homebrew nativo a 60 fps en portátil sin overclock, launcher con perfiles gráficos, mods, ISO directa ([repo de releases](https://github.com/HopeAero/MLBPR2008-NX)) |
| [v0.1.7](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.7) | [Postemporada de 12 equipos](docs/roadmap/postemporada.md) (ronda de Wild Card) en Temporada y MLB Life; marcas y logotipos PNG propios para los equipos de Arrange; uniformes importados para los equipos de Arrange con el nuevo Uniform Studio |
| [v0.1.6](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.6) | Nombres de jugador nuevos (callnames) para los comentaristas con mods y el Editor de callnames; optimizaciones de Switch portadas a PC (13 % menos CPU en el hilo principal); opciones gráficas más ligeras en VIDEO; el Setup ya no lo marcan las heurísticas de Defender |
| [v0.1.5](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.5) | Jugadores de dos vías (regla Ohtani) en todos los modos; charla de rol con el mánager en MLB Life; escalado con IA y packs de Dolphin en el Texture Studio |
| [v0.1.4.1](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4.1) | Corrección de los controles |
| [v0.1.4](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4) | Calendarios reales de MLB (2002–2026), año de inicio, DH en ambas ligas, ligas 15/15, Editor de temporada, contraseñas de jugador como texto |
| [v0.1.3](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.3) | Temporada de más de 10 años; kits de Linux y Steam Deck |
| [v0.1.2](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.2) | Arreglo de la narración, partidas en uso, estadios desde cero en Blender |
| v0.1.0–v0.1.1 | Primeras versiones públicas: port nativo a PC, launcher, mods, rosters 2026, pantalla panorámica, Texture Studio |

Las decisiones detrás de todo esto, con su motivo y cuándo revisarlas: [docs/decisiones.md](docs/decisiones.md).
