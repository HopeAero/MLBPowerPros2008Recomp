# Editor de temporada (Season Studio)

**Power Pros Wii 2008 Season Studio** prepara las temporadas del modo Temporada y de MLB Life: qué año empiezan, con qué calendario, DH y ligas. Se abre desde el launcher: **AJUSTES → HERRAMIENTAS → EDITOR DE TEMPORADA**. Es `SeasonStudio.exe`, junto al juego.

No lee ni cambia archivos del juego: solo escribe en la carpeta de un mod (`mod.toml` y `schedule/<año>.csv`) y activa o desactiva mods en `Config.toml`, como la pantalla MODS del launcher.

## La ventana

- **Izquierda, PRÓXIMA PARTIDA NUEVA**: cómo empezará una partida nueva con los mods activos y las opciones de MEJORAS:
  - el año,
  - de qué año es el roster (las edades),
  - qué años tienen calendario real y cuáles uno generado por el juego,
  - el DH y las ligas.

  Cada cosa dice de dónde sale: un mod o MEJORAS. Si dos mods activos piden cosas distintas, aparece un aviso con el que se usa (el primero por nombre, como hace el juego).
- **Arriba, Mod**: el mod que se ajusta, si está activado en el juego (con **Activar** / **Desactivar**) y **+ Nuevo mod** para crear uno nuevo, ya activado.
- **AJUSTES**: los ajustes del mod, cada uno con su explicación debajo.
  - **Año de inicio** (`start_year`): 2008 es el del juego.
  - **Año del roster (edades)** (`roster_year`): de qué año es el roster, para que las edades salgan bien. Lo normal es «Mismo que el inicio».
  - **DH**: «Como en MEJORAS», «En ambas ligas» (`dh = "both"`) u «Original (Liga Americana)» (`dh = "original"`).
  - **Ligas**: «Como en MEJORAS», «15 y 15» (`[divisions]`, Houston en la AL West) u «Original (14 y 16)» (`leagues = "original"`). Un mod con otras divisiones en su `mod.toml` sale como «Propias».
  - **Calendarios reales**: elige **Desde** y **Hasta** (2002 a 2026; 2020 no está porque tuvo 60 partidos) y pulsa **Descargar**. Baja los calendarios de las Grandes Ligas (statsapi.mlb.com) y los guarda en el mod. Si el mod no tenía año de inicio, se le pone el primero. Debajo salen los años que tiene el mod: pulsa uno para verlo.
- **CALENDARIO**: el calendario de un año del mod, mes a mes.
  - Con un equipo elegido, cada día dice su rival («vs NYY» en casa, «@ SF» fuera).
  - Con «Todos», cuántos partidos hay ese día.
  - Pulsa un día para ver sus partidos con la hora (del Este de EE. UU.).

  Las fechas son las que juega el juego: si una temporada empezó antes del 19 de marzo o acabó después del 1 de octubre, la corre unos días, y los partidos de un día con más de 16 pasan a un día cercano. La nota de arriba lo dice.

Los cambios valen para la próxima partida nueva. Las partidas empezadas siguen como estaban. Si el juego está abierto, ciérralo y ábrelo.

## Para probar sin ventana

- `SeasonStudio.exe --selftest`: autoprueba.
- `SeasonStudio.exe --download 2025 2026 --mod <carpeta>`: la descarga sin ventana.
- Al abrir: `--mod <carpeta> --tab calendar --year 2025 --team NYY --month 4 --day 5 --lang es|en`.

El código está en `tools/season-studio`. Comparte el aspecto y la lectura de `Config.toml` con el Editor de texturas, y las reglas de temporada con el runtime (`season_pack.h`, `season_text.h`, `season_schedule.h`, `season_schedule_mlb.h`).
