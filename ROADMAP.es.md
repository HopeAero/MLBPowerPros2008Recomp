# Roadmap

*English: [ROADMAP.md](ROADMAP.md)*

**Este archivo es la fuente de verdad** de qué está hecho, en curso y planificado. Cada línea de trabajo tiene un plan en [`docs/roadmap/`](docs/roadmap/) con sus fases, cómo se verifica y sus casillas. Los fallos van en [Issues](https://github.com/HopeAero/MLBPowerPros2008Recomp/issues); las ideas y preguntas, en [Discussions](https://github.com/HopeAero/MLBPowerPros2008Recomp/discussions). Cuando algo termina, pasa a **Hecho** con la versión que lo trajo.

Actualizado: 24 de octubre de 2026 · última versión [v0.1.5](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.5).

Dos reglas valen para todo: el juego sigue **fiel al original de Wii por defecto** (lo que cambie el juego se activa en el launcher), y **nunca se distribuyen archivos del juego**: cada jugador lo compila desde su propio disco.

## Ahora

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [Corrección de fallos](docs/roadmap/fallos.md) | Lo que reportan los jugadores, ordenado y arreglado en versiones de corrección | Continuo |
| [Rendimiento en PCs modestas](docs/roadmap/rendimiento.md) | Medir y bajar el coste de CPU y GPU para mantener 60 FPS en gráficas integradas y CPUs antiguas | Lo siguiente |

## Siguiente

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [Postemporada moderna](docs/roadmap/postemporada.md) | La postemporada de 12 equipos (Wild Card Series) en Temporada y MLB Life, opcional | Investigación |
| [OpenGL](docs/roadmap/opengl.md) | Un backend gráfico OpenGL / OpenGL ES para gráficas sin Vulkan ni Direct3D 12, y camino hacia Switch | Investigación |
| [Herramientas de modding](docs/roadmap/herramientas-modding.md) | Mejoras al Texture Studio, al Editor de temporada, a los rosters y estadios; estadios propios en Temporada y MLB Life | Continuo |

## Investigación

| Línea de trabajo | Qué | Estado |
|---|---|---|
| [Mods de código](docs/roadmap/mods-de-codigo.md) | Mods que cambian la lógica del juego (torneos, reglas, pantallas nuevas) con una API de mods documentada, no solo datos | Investigación |
| [Nintendo Switch](docs/roadmap/switch.md) | Una versión homebrew para Switch, desde la copia del propio jugador, como la de PC | Investigación |

## Hecho

| Versión | Lo principal |
|---|---|
| [v0.1.5](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.5) | Jugadores de dos vías (regla Ohtani) en todos los modos; charla de rol con el mánager en MLB Life; escalado con IA y packs de Dolphin en el Texture Studio |
| [v0.1.4.1](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4.1) | Corrección de los controles |
| [v0.1.4](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.4) | Calendarios reales de MLB (2002–2026), año de inicio, DH en ambas ligas, ligas 15/15, Editor de temporada, contraseñas de jugador como texto |
| [v0.1.3](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.3) | Temporada de más de 10 años; kits de Linux y Steam Deck |
| [v0.1.2](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases/tag/v0.1.2) | Arreglo de la narración, partidas en uso, estadios desde cero en Blender |
| v0.1.0–v0.1.1 | Primeras versiones públicas: port nativo a PC, launcher, mods, rosters 2026, pantalla panorámica, Texture Studio |

Las decisiones detrás de todo esto, con su motivo y cuándo revisarlas: [docs/decisiones.md](docs/decisiones.md).
