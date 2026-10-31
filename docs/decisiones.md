# Decisiones

*English: [en/decisions.md](en/decisions.md)*

Las decisiones que dan forma al proyecto, con su motivo y cuándo revisarlas. Una decisión nueva se añade aquí; una que cambia se tacha y se apunta la nueva.

| Fecha | Decisión | Por qué | Revisar cuando |
|---|---|---|---|
| 2026-10-08 | Recompilación estática (Wiicompiled), no un emulador ni un decomp | Rendimiento nativo y un camino a otras plataformas sin esperar años de decompilación | Si aparece un decomp de 2008 |
| 2026-10-08 | Nunca se distribuyen archivos del juego (texturas, modelos, audio, disco): en PC cada jugador lo compila desde su propio disco (Setup) | Legalidad y respeto a los dueños del juego | — |
| 2026-10-09 | Fiel al original por defecto; lo que cambie el juego va en MEJORAS y es opcional | Los jugadores eligen; el original sigue intacto | — |
| 2026-10-11 | Vulkan antes que Direct3D 12 en AMD | Direct3D 12 caía en las vistas pesadas de fildeo; Vulkan mantiene 60 FPS | Tras el plan de rendimiento |
| 2026-10-13 | Los cambios al runtime son parches reproducibles sobre un Wiicompiled fijado | Cualquiera puede rehacer la versión desde el código | — |
| 2026-10-14 | Mods de texturas con los nombres de Dolphin | Compatibilidad con los packs de la comunidad (comprobada con Dolphin 2609) | — |
| 2026-10-21 | Todo lo que funcione en Temporada debe funcionar también en MLB Life | MLB Life es el modo más jugado | — |
| 2026-10-22 | La interfaz dentro del juego usa los dibujos y la fuente del propio juego, no imitaciones | Que las mejoras parezcan parte del juego | — |
| 2026-10-24 | El escalado con IA descarga el motor y el modelo de sus autores; no van con el juego | Licencias (AnimeSharp es CC BY-NC-SA) | Si aparece un modelo libre igual de bueno |
| 2026-10-24 | El roadmap vive en `ROADMAP.md`; los fallos en Issues; las ideas en Discussions | Una sola fuente de verdad pública | — |
| 2026-10-30 | Switch: el front end de gráficos (Aurora) va en un hilo del núcleo 2 y el decodificador FIFO en el núcleo 1, por defecto | Bajó los frames tarde por CPU de 6,5 % a 3,6 % sin cambiar nada visible; verificado byte a byte | Si el núcleo 1 o el 2 se saturan |
| 2026-10-30 | Las opciones que bajan gráficos para ir más rápido (sombras, público, reflejos, cámara) van en VIDEO, no en MEJORAS | MEJORAS es para lo que el juego de Wii no tenía | — |
| 2026-10-31 | Switch: se publica el NRO en un repositorio aparte ([MLBPR2008-NX](https://github.com/HopeAero/MLBPR2008-NX)); el jugador pone su propio disco (ISO o carpeta) | Compilar para Switch pide demasiadas dependencias; es lo que hacen Dusklight-NX, Ship of Harkinian y Unleashed Recompiled; el repo aparte protege el principal ante un DMCA | Si llega una reclamación, o si el Setup puede compilar para Switch sin esfuerzo |
| 2026-10-31 | Switch: perfiles gráficos Bajo (relojes de fábrica), Medio (original) y Alto (1,5×); Medio y Alto recomiendan overclock | El jugador elige entre fidelidad y fluidez con un solo ajuste | Tras los reportes de los jugadores |
