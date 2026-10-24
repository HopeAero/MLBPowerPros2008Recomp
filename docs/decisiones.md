# Decisiones

*English: [en/decisions.md](en/decisions.md)*

Las decisiones que dan forma al proyecto, con su motivo y cuándo revisarlas. Una decisión nueva se añade aquí; una que cambia se tacha y se apunta la nueva.

| Fecha | Decisión | Por qué | Revisar cuando |
|---|---|---|---|
| 2026-10-08 | Recompilación estática (Wiicompiled), no un emulador ni un decomp | Rendimiento nativo y un camino a otras plataformas sin esperar años de decompilación | Si aparece un decomp de 2008 |
| 2026-10-08 | Nunca se distribuyen archivos del juego: cada jugador lo compila desde su propio disco (Setup) | Legalidad y respeto a los dueños del juego | — |
| 2026-10-09 | Fiel al original por defecto; lo que cambie el juego va en MEJORAS y es opcional | Los jugadores eligen; el original sigue intacto | — |
| 2026-10-11 | Vulkan antes que Direct3D 12 en AMD | Direct3D 12 caía en las vistas pesadas de fildeo; Vulkan mantiene 60 FPS | Tras el plan de rendimiento |
| 2026-10-13 | Los cambios al runtime son parches reproducibles sobre un Wiicompiled fijado | Cualquiera puede rehacer la versión desde el código | — |
| 2026-10-14 | Mods de texturas con los nombres de Dolphin | Compatibilidad con los packs de la comunidad (comprobada con Dolphin 2609) | — |
| 2026-10-21 | Todo lo que funcione en Temporada debe funcionar también en MLB Life | MLB Life es el modo más jugado | — |
| 2026-10-22 | La interfaz dentro del juego usa los dibujos y la fuente del propio juego, no imitaciones | Que las mejoras parezcan parte del juego | — |
| 2026-10-24 | El escalado con IA descarga el motor y el modelo de sus autores; no van con el juego | Licencias (AnimeSharp es CC BY-NC-SA) | Si aparece un modelo libre igual de bueno |
| 2026-10-24 | El roadmap vive en `ROADMAP.md`; los fallos en Issues; las ideas en Discussions | Una sola fuente de verdad pública | — |
