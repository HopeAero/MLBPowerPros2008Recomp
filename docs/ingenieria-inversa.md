# MLB Power Pros 2008 (Wii) — Referencia de ingeniería inversa

*English version: [en/reverse-engineering.md](en/reverse-engineering.md)*

Lo que se sabe del juego por dentro, ordenado por sistema: direcciones, estructuras, formatos de archivo, hilos y el flujo de escenas. Sale del trabajo del proyecto de recompilación nativa y está pensado para la comunidad de mods y para futuros desarrolladores.

- **Versión:** `RL8E54` (EE. UU., NTSC-U). Todas las direcciones son de esta versión.
  - `main.dol` SHA-256 `789c208f1611c4b150d272eb32cbd461d24aebbc5d7c11a420102d868ff1c5f7`
  - `ovl_main.rel` SHA-256 `dcc37a1a4824702feff5bcaa6aae7cefdedfc3b5d832fbf3de92b982a59b4edc`
- **Qué no es:** no es una decompilación. El juego está recompilado entero (traducido de PowerPC a C++), pero casi todo su código sigue sin nombre (`func_8003A248`…). Lo de aquí se descubrió al arreglar fallos concretos o al construir herramientas (mods, roster, benchmark).
- **Fiabilidad:** cada dato indica de dónde sale. "Desensamblado" y "ejecución" están comprobados en el binario o en el juego corriendo. "Observado" es comportamiento visto sin leer el código que lo produce.
- **Sin contenido del juego:** este documento no incluye bytes, recursos ni código del juego, solo descripciones.
- **Cómo generar el PDF:** `python tools/build_re_pdf.py` (lo escribe en `local/docs/`).

## Acceso rápido

Qué buscas y dónde está:

| Busco… | Ir a |
|---|---|
| Modo Temporada: fecha, año, temporadas, tablas por año | [Modo Temporada](#9-modo-temporada) |
| Calendario de partidos (tabla, números de equipo, generador) | [Calendario de partidos](#calendario-de-partidos) |
| Partida de Temporada (`.dat`) | [Partida guardada de Temporada](#partida-guardada-de-temporada) |
| Jugadores y roster (`game_data.pack`) | [Roster: `game_data.pack` y formato de jugador](#7-roster-game_datapack-y-formato-de-jugador) |
| Estadios (modelos, colisión, añadir estadios) | [Estadios: modelos `PJ7M` y tabla `tbl`](#6-estadios-modelos-pj7m-y-tabla-tbl) |
| Texturas y packs (`PACKWII`) | [Formato `PACKWII`](#formato-packwii) |
| Archivos del disco (`project.bin`) | [Disco y `project.bin`](#5-disco-y-projectbin) |
| Menús y escenas | [Escenas, menús y preparación de un partido](#8-escenas-menús-y-preparación-de-un-partido) |
| Comentarista y sonido | [Motor de sonido y comentarista](#4-motor-de-sonido-y-comentarista) |
| Cámara del partido | [Cámara del partido](#cámara-del-partido) |
| Mandos | [Mandos](#11-mandos) |
| Partidas guardadas, NAND | [Partidas guardadas, NAND, SYSCONF y ES](#12-partidas-guardadas-nand-sysconf-y-es) |
| Portar el juego: lo que hay que respetar | [Peculiaridades del juego que importan al portarlo](#14-peculiaridades-del-juego-que-importan-al-portarlo) |
| Herramientas y cómo seguir investigando | [Cómo seguir investigando](#15-cómo-seguir-investigando) |

## Índice

1. [Ejecutable y mapa de memoria](#1-ejecutable-y-mapa-de-memoria)
2. [Arranque y sistema operativo](#2-arranque-y-sistema-operativo)
3. [Hilos y ritmo](#3-hilos-y-ritmo)
4. [Motor de sonido y comentarista](#4-motor-de-sonido-y-comentarista)
    [Hilos](#hilos) · [Canales y voces AX](#canales-y-voces-ax) · [Streams del comentarista](#streams-del-comentarista)
5. [Disco y `project.bin`](#5-disco-y-projectbin)
    [Formato `PACKWII`](#formato-packwii)
6. [Estadios: modelos `PJ7M` y tabla `tbl`](#6-estadios-modelos-pj7m-y-tabla-tbl)
    [Modelo `PJ7M`](#modelo-pj7m) · [Tabla `tbl`](#tabla-tbl) · [Árboles A y B](#árboles-a-y-b) · [Escribir la colisión](#escribir-la-colisión) · [Escribir estadios](#escribir-estadios) · [Meter un modelo externo](#meter-un-modelo-externo) · [Inventario de campos para escribir packs desde cero](#inventario-de-campos-para-escribir-packs-desde-cero) · [Escribir packs desde cero](#escribir-packs-desde-cero) · [Qué hacen los campos, probado en el visor](#qué-hacen-los-campos-probado-en-el-visor) · [Estadio desde cero en el juego](#estadio-desde-cero-en-el-juego) · [Lista de estadios del juego](#lista-de-estadios-del-juego) · [Pendiente](#pendiente) · [Herramientas](#herramientas)
7. [Roster: `game_data.pack` y formato de jugador](#7-roster-game_datapack-y-formato-de-jugador)
8. [Escenas, menús y preparación de un partido](#8-escenas-menús-y-preparación-de-un-partido)
9. [Modo Temporada](#9-modo-temporada)
    [Pantallas y menús](#pantallas-y-menús) · [Estado, fecha y año](#estado-fecha-y-año) · [Calendario de partidos](#calendario-de-partidos) · [Paso de año y número de temporadas](#paso-de-año-y-número-de-temporadas) · [Datos por año](#datos-por-año) · [Partida guardada de Temporada](#partida-guardada-de-temporada)
10. [Gráficos (GX y VI)](#10-gráficos-gx-y-vi)
    [Cámara del partido](#cámara-del-partido)
11. [Mandos](#11-mandos)
12. [Partidas guardadas, NAND, SYSCONF y ES](#12-partidas-guardadas-nand-sysconf-y-es)
13. [Bibliotecas y funciones identificadas](#13-bibliotecas-y-funciones-identificadas)
14. [Peculiaridades del juego que importan al portarlo](#14-peculiaridades-del-juego-que-importan-al-portarlo)
15. [Cómo seguir investigando](#15-cómo-seguir-investigando)

---

## 1. Ejecutable y mapa de memoria

El juego es un DOL pequeño y un módulo REL grande con casi todo el juego.

| | Archivo | Texto | Datos | BSS |
|---|---:|---:|---:|---:|
| `sys/main.dol` | 2.299.840 | 1.200.448 | 1.099.136 | 954.200 |
| `files/ovl_main.rel` | 11.229.928 | 7.820.936 | 1.806.784 | 154.196 |

- **Entrada:** `__start` en `0x8000403C`; `main` en `0x8005E4C4`.
- **Funciones:** el REL tiene el 87 % del código. Hay 48.177 funciones en el mapa: 5.065 en el DOL y 43.112 en el REL. Solo 1.794 tienen nombre (casi todas del SDK, en el DOL).
- **Áreas de datos pequeños:** `r13` (`_SDA_BASE_`) = `0x80322060` y `r2` (`_SDA2_BASE_`) = `0x80323940`. Las fija `__init_registers` (`0x800041A8`).
- **REL `ovl_main.rel`:**
  - Módulo 1, formato de REL versión 3.
  - Cargado en `0x80575580`, con su BSS en `0x8102B0C0`.
  - `_prolog` `0x80AA0718`, `_epilog` `0x80AA0794`, `_unresolved` `0x80AA07DC`.
- **Detalle del BSS del REL** (ejecución):
  - El REL ocupa un hueco libre entero de `0xAB5B20` bytes en el montón principal y termina en `0x8102B0A0`.
  - El BSS va justo después, tras una cabecera de bloque de `0x20` bytes: en `0x8102B0C0`.
  - Si el BSS queda `0x20` más abajo, sus últimas variables pisan la cabecera del bloque siguiente y rompen la lista libre del montón.
- **Tope de la arena:** `0x817FFD40`. Dolphin coloca el FST (`0x2C0` bytes) arriba del todo de MEM1.
  - El juego dimensiona su montón principal con toda la arena y lo llena casi al byte.
  - Con una arena más baja, una carga de 278 KB devuelve NULL. Entonces el juego copia un paquete `PACKWII` a `0x80000000` y destruye las variables globales del sistema.
- **Montón principal** (desensamblado de `0x80051474`):
  - Objeto en `0x802B57A8`; búsqueda first-fit por la lista libre (`+0x10`).
  - Cabecera de bloque de `0x20` bytes antes de los datos:

| Offset | Campo |
|---|---|
| `+0x00` | banderas (`0x80000000` libre, `0x40000000` usado) |
| `+0x08` | tamaño de datos |
| `+0x0C` / `+0x10` | enlaces de la lista libre |
| `+0x14` / `+0x18` | bloque anterior / siguiente en memoria |

- **Montones del juego** (los crea `0x8005AF78` al arrancar; asignan con `0x8005A804` desde la cabeza, `0x8005A9DC` desde la cola y liberan con `0x8005ABAC`, el handle en `r3`):
  - `0x802B57A8`, MEM1: toda la arena de MEM1 (a partir de `0x8032EAE0`, que el juego fija) hasta `0x81800000`.
  - `0x802B57C8`, MEM2: un tamaño **fijo** de `0x033E0000 - 0x800` bytes desde el inicio de la arena de MEM2, es decir `0x90000800`–`0x933E0000`, la arena retail entera. No mira el tope de la arena.
  - En la cola de MEM2 van, entre otros, búferes de 1 y 2 MB (`0x800329A4`) y el pack de gráficos del estadio (`0x800434B8`). Si una reserva falla, `0x800434B8` la repite sin fin: es el cuelgue en «Preparando partido» con un pack de 32 MB (comprobado con trazas el 18 de octubre de 2026).
  - El recomp sustituye `0x8005AF78` por una versión nativa (`hle/mlb/game_heaps.cpp`, parche `wiicompiled-mlb-mem2-heap`) que da al montón de MEM2 toda la arena: 125,7 MB con los 128 MB de MEM2 del runtime.
- **Objetos únicos del juego**, punteros dentro del BSS del REL:
  - `0x81033268`: gestor de escenas ([sección 8](#8-escenas-menús-y-preparación-de-un-partido))
  - `0x8104E1D8`: configuración del partido
  - `0x8103D118`: datos comunes del partido

Fuentes: `docs/phase0-report.md`, `config/wiicompiled/rl8e54.yml`, `tools/patches/wiicompiled-mlb-overlay-loader.patch`, `wiicompiled-rel-bss-address.patch`, `wiicompiled-match-benchmark.patch`.

## 2. Arranque y sistema operativo

**Secuencia:**
1. `__start` llama a `__init_registers` y a `__init_cpp` (`0x800A8730`), y luego a `main`.
2. `main` carga `ovl_main.rel` del DVD, lo enlaza (`OSLink`) y llama a su `_prolog`.
3. Pantallas: aviso de la correa del Wii Remote (pide A), "NOW LOADING" (mientras carga el REL y lee más de `project.bin`), comprobación de memoria ("The Wii system memory is being checked."), logo de 2K Sports, vídeo de introducción, menú principal.

**Cargador del REL, `0x80051474`** (reconstruido en `hle/mlb/rel_loader.cpp`):
- Recibe el índice de módulo en `r4` y devuelve 1 si lo carga bien.
- Tabla de módulos en `0x8020B920`, entradas de `0x28` bytes:

| Offset | Campo |
|---|---|
| `+0x0C` | puntero al nombre de archivo |
| `+0x10` | contador de usos |
| `+0x14` | dirección del módulo; si no es 0, ya está cargado y solo suma un uso |
| `+0x18` | tamaño |
| `+0x1C` | dirección del BSS |
| `+0x20` | tamaño del BSS |

- Pasos de la carga:
  1. `DVDOpen` `0x800A9764`
  2. reserva con `HeapAlloc` `0x8005A804` (alineado a `0x20`)
  3. `memset` + `DCFlushRange` `0x8009C488`
  4. `DVDReadPrio` `0x800A9CDC` con prioridad 2
  5. `DVDClose` `0x800A9A10`
  6. reserva el BSS
  7. `OSLink` `0x800A1678`
  8. llama a `_prolog` (cabecera del módulo `+0x34`)
- Si algo falla libera con `HeapFree` `0x8005A76C`.

**Memoria baja:** la cabecera de disco en `0x80000000` lleva `RL8E` + `54`. El juego la sobrescribe una vez en marcha, así que el código de juego no se puede leer de ahí después del arranque.

**SYSCONF:**
- `OSInit` (`0x8009AA90`) espera mientras `SCCheckStatus` (`0x800D1AB0`) devuelve "ocupado"; la lectura la lanza `SCInit` (`0x800D1A10`).
- Estado del trabajo en `0x8031B788` (1 ocupado, 3 lectura terminada, 0 listo).
- Bloque de control en `0x802D3E40` y búfer de SYSCONF en `0x802D3FE0` (`0x4000` bytes).
- Si el SYSCONF no es válido (lo comprueba `0x800D2064`), el juego crea uno vacío con las marcas `SCv0` y `SCed` y arranca con valores por defecto.

**STM:** `__OSInitSTM` (`0x800A5B6C`) guarda su bandera de inicio en `r13-0x6DDC` (`0x8031B284`). En `+4` y `+8` van los descriptores de `/dev/stm/immediate` y `/dev/stm/eventhook`.

**Variables globales del SDK localizadas:**

| Dirección | Qué es |
|---|---|
| `0x802B6CD0` | `__OSErrorTable` (en `+0x40`, la de coma flotante) |
| `0x802B7078` | `DefaultThread` |
| `0x802B7390` | `RunQueue` |
| `0x802B7490` | `IdleThread` |
| `0x8031B1F0` | cola de alarmas (`r13-0x6E70`) |
| `0x8031B248` | puntero a la tabla de interrupciones |
| `0x8031B268` | contador de `OSDisableScheduler` |
| `0x8031A5D8` | `SwitchThreadCallback` |
| `0x8031B290` / `0x8031B298` | aviso y callback del botón de apagado |
| `0x8031B740`, `0x8031B744` | IPC: inicializado, búfer bajo/alto |
| `0x80308898` | `btm_cb` (Bluetooth) |

**Objeto de hilo (`OSThread`):** `+0x2C8` estado (u16), `+0x2CC` suspensiones, `+0x2D0` prioridad, `+0x2DC` cola de espera.

Fuentes: `docs/wiicompiled-feasibility.md`, `config/wiicompiled/rl8e54-data.csv`, `hle/mlb/sc_check_status.cpp`, `wiicompiled-mlb-stm-sda.patch`, `wiicompiled-mlb-bringup.patch`.

## 3. Hilos y ritmo

- **El juego va a 60 fotogramas por segundo** y espera al retrazo vertical (`VIWaitForRetrace`, `0x800B327C`). No dibuja fotogramas de más.
- **Hilos de sonido:** el juego tiene un hilo temporizador que despierta al hilo de sonido cada 5 ms (sección 4). Mide el tiempo en vueltas: cada vuelta vale 5 ms.
- **El hilo principal casi nunca se bloquea:**
  - Durante "NOW LOADING" gira en un bucle que no se bloquea. En consola, los otros hilos avanzan porque la interrupción de VI da paso al hilo de más prioridad listo.
  - Al empezar un partido, mientras los jugadores van a sus posiciones, calcula 17-20 ms seguidos (un fotograma entero) sin ceder.
- **Interrupciones:** el código del juego llama a menudo a `OSDisableInterrupts` (`0x800A0944`), `OSEnableInterrupts` (`0x800A0958`) y `OSRestoreInterrupts` (`0x800A096C`). En consola, una interrupción pendiente (por ejemplo, un temporizador vencido) se atiende justo al reactivarlas.
- **Funciones de hilos del SDK:**

| Función | Dirección |
|---|---|
| `OSCreateThread` | `0x800A4448` |
| `OSResumeThread` | `0x800A4B50` |
| `OSInitThreadQueue` | `0x800A3E90` |
| `SelectThread` | `0x800A41CC` |
| `OSSleepThread` | `0x800A4F7C` |
| `OSWakeupThread` | `0x800A5068` |
| `OSSleepTicks` | `0x800A526C` |

- **Espera de la biblioteca de Miis (RFL):** `RFLWaitAsync` (`0x800876A0`) gira sobre `RFLiIsWorking` (`0x80087B94`), que lee el indicador `+0x1B34` del gestor (puntero en `0x8031B168`). Hasta el 14 de octubre de 2026 el HLE de `RFLiIsWorking` leía el puntero en la dirección de Mario Kart Wii (`0x80386298`, memoria libre en MLB); ahora usa la de MLB (parche `wiicompiled-mlb-disc-identity`).
- **IPC:** `IPCCltInit` (`0x800CC534`) solo inicializa una vez: comprueba y pone a 1 la bandera `r13-26876` (`0x8031B764`), llama a `IPCInit` (`0x800CC038`) y reserva `0x1000` bytes desde el búfer bajo (`r13-26908`; el alto está en `r13-26904`). Desensamblado, `RL8E54`.
- **Para portarlo:** el juego da por hecho que los temporizadores llegan a tiempo y que las interrupciones cortan cualquier cálculo. Con hilos cooperativos hay que ofrecer puntos de cambio (tras `GXCopyDisp` y al reactivar interrupciones) y recuperar los periodos perdidos.

Fuentes: desensamblado de `0x80027A54`/`0x80027BF8`; `wiicompiled-preemption-point.patch`, `wiicompiled-periodic-sleep.patch`, `docs/wiicompiled-feasibility.md`.

## 4. Motor de sonido y comentarista

Investigado en octubre de 2026 para arreglar el petardeo del comentarista (desensamblado, puntos de vigilancia de memoria y ejecución). Las funciones AX sin nombre se identificaron por su comportamiento, no por firma.

**Archivos de sonido en el disco:**

| Archivo | Contenido |
|---|---|
| `sound/SeData.bin` | "KSS SE Link Data" |
| `sound/TrSeData.bin` | "KSS BGM Link Data" |
| `sound/WaveData.bin` | "KSS Wave Link Data" |
| `sound/strbgm.bin` | música en stream (321.687.552 bytes) |
| `sound/voice0.bin` … `voice17.bin` | 18 bancos de voz del comentarista |

- "KSS" es la etiqueta que aparece en los archivos; no está confirmado de qué middleware viene.
- En el REL también hay JAudio2/JASystem (`JAS*`, `JAU*`).
- La salida del juego va en bloques de 3 ms a 32 kHz.

### Hilos

| Dirección | Qué es | Prioridad |
|---|---|---|
| `0x80027A54` | **Hilo de sonido.** Bucle: trabajo de sonido y `OSSleepThread(r13-0x7378)` hasta el siguiente aviso. Objeto de hilo en `0x80242888` | 4 |
| `0x80027BF8` | **Temporizador de sonido.** Bucle: `OSSleepTicks(5 ms)` y `OSWakeupThread(r13-0x7378)` | 3 |

- Se crean juntos con `OSCreateThread`, pilas de `0x1000` bytes, y se arrancan con `OSResumeThread`.
- El periodo sale del reloj del bus: `[0x800000F8] / 4 / 1000 × 5` ticks = 5 ms (303.750 ticks a 243 MHz).
- **El juego cuenta el tiempo en vueltas, no con un reloj:** en consola son 200 vueltas por segundo, y si llegan tarde el audio que depende de ellas se alarga.
- Cada vuelta, en orden:
  1. `0x800329BC`, `0x80027CD4`, `0x80028290`, `0x80027F60`, `0x8002809C`, `0x80027EAC`
  2. `0x8002F660`
  3. el gestor de streams `0x80038E50`
  4. `0x80035A3C`
  5. el proceso de canales `0x8002C604`
- Cada 4 vueltas llama además a `0x80007360` y `0x800073B0`. El contador de vueltas está en `r13-0x73C0`.

### Canales y voces AX

- **55 canales** en `0x80236930`, de 596 (`0x254`) bytes cada uno. El puntero a la voz AX (`AXVPB`) está en `+584`.
- El comentarista usa el **canal 19**.
- **Cola de órdenes de sonido** en `0x80297C20`: entradas de 28 bytes (tipo, canal, valor).
  - El tipo 8 cambia el ratio de remuestreo; ratio 0 = pausa.
  - Despachador: `0x8002D038`.
- `0x8002C480(canal, modo)`: 0 consulta, 1 reserva, 2 libera. La parada sigue la cadena `0x8002C480` → `0x8002C310` → `AXSetVoiceState`.
- **Funciones AX localizadas:**

| Función | Dirección |
|---|---|
| `AXSetVoiceSrc` | `0x800BBD38` |
| `AXSetVoiceSrcRatio` | `0x800BBDBC` |
| `AXSetVoiceState` | `0x800BB99C` |
| `AXSetVoiceAddr` | `0x800BBB08` |
| `AXAcquireVoice` | `0x800B8CC0` |
| `AXFreeVoice` | `0x800B8C44` |
| `__AXSetupDsp` | `0x800BA458` |

- Tarea DSP de AX en `0x802C12A0`; imagen del microcódigo en `0x80225E00`.
- Callbacks de la tarea: init `0x800BA3E4`, resume `0x800BA3F0`, done `0x800BA444`, request `0x800BA454`.

### Streams del comentarista

- **Tabla de 4 objetos de stream** en `0x802A1A78`. `0x80038E50` recorre los 4 y llama al gestor `0x80038F08` para cada uno válido.
- Un objeto es válido si tiene `0x5A5A5A5A` en `+0x824` y `0x8A8A8A8A` en `+0x1960`. `+0x820` es su índice.
- **Estado en `+0x828`.** El gestor salta por la tabla `0x801559E0` y guarda lo que devuelve cada caso como estado siguiente:

| Estado | Destino | Qué hace |
|---|---|---|
| 0 | `0x80038F4C` | limpia el stream (libera lo apuntado por `+0x898`, `+0x8A0`, `+0x89C`, `+0x894`) |
| 1 | `0x800398BC` | carga; depende de `+0x848` (petición de DVD) mediante `0x8003B398` |
| 2 | `0x80039B94` | lee la cabecera de la frase y prepara su reloj; devuelve 3 |
| 3 | `0x8003A048` | arranca la reproducción |
| 4 | `0x8003A248` | reproduce: avanza el reloj; devuelve 4 o 5 |
| 5 | `0x800392E8` | fin de frase |
| 6 | `0x800392E0` | salida sin cambios |
| 7 | `0x800391C0` | `+0x8C0 = 5000`, pasa a 8 |
| 8 | `0x800391D4` | `+0x8C0 = 5000`, pasa a 3 |

- **Voces:** DSP-ADPCM mono a 22.050 Hz. Se remuestrean a 32 kHz con interpolación lineal; el ratio AX es la frecuencia × 2,048 (`0xB066` para 22.050 Hz).
- Hay **dos voces de stream** que se alternan, cada una con su búfer circular de 64 KB, y se reproduce una voz por frase o nombre.
- **Cabecera de frase** (bloque de `0x800` bytes antes de los datos):

| Offset | Contenido |
|---|---|
| `+0x00` | bytes de datos de la frase |
| `+0x04` | `(0xB4 o 0x78) << 16 \| 22050`: banderas y frecuencia |
| `+0x10` | **duración en milisegundos** (p. ej. `0x0A0F` = 2.575 ms) |
| `+0x88` | longitud en nibbles ADPCM |
| `+0xB0` | escala de predicción inicial |

- **Reloj de cada frase:**
  - `0x80039B94` (estado 2) copia la duración a `+0x858`; si es 0, la calcula con `0x80038430`. La ajusta por `+0x8F8`/`+0x8FA` (`(x + 0x100) >> 8`) y la guarda en `+0x874` (duración total) y `+0x85C`. Después pone `+0x858 = 0` y `+0x8C0 = 5000`.
  - `0x8003A248` (estado 4) **suma 5 a `+0x858` en cada vuelta**. Al llegar a `+0x874 − 10`, para la voz con `0x8002C7CC(+0x834)` y pasa al estado 5.
  - `0x8003A248` también avanza las posiciones del búfer (`+0x888`, `+0x8AC`, `+0x8B8`) según `+0x904` y `+0x8FC`.
  - `0x80038358(índice)` devuelve los ms que le quedan a la frase (`max(0, +0x874 − +0x858)`) en estado 4, `0xFFFF` en otros estados y 0 si el stream no existe.
  - Tras cada vuelta del estado 4, el gestor llama a `0x8002670C` con `+0x854`. Es un stub vacío (`blr`).
- **Cómo para el juego una frase:** pausa (ratio 0) y después libera y reasigna la voz. Lo hace 2-4 bloques (6-12 ms) después de que se acaben los datos, mientras el decodificador ADPCM mantiene su último nivel. Por eso el DSP original aplica una rampa a cero (*depop*).
- **Saturación:** las frases del comentarista llegan a ±32767 en los propios datos del disco: 11 de 12 frases decodificadas por separado saturan. No es un fallo de la recompilación.

**Fallo encontrado y su causa (para portadores):** si el temporizador de 5 ms no despierta 200 veces por segundo, el reloj de las frases va lento. Entonces la frase se para entre 1,2 y 1,7 veces tarde: suena el relleno de ceros y el principio de la frase siguiente, y luego se corta. Se oye como chasquidos y palabras cortadas.

**Lo mismo con la ventana oculta en Windows 11** (19 de octubre de 2026): Windows 11 deja de respetar `timeBeginPeriod(1)` mientras la ventana del programa está minimizada o tapada del todo, y los temporizadores vuelven a 15,6 ms. Medido con un programa de prueba de `Sleep(5)`: 135 despertares/s con la ventana visible y 64/s minimizada. A ese ritmo el temporizador de 5 ms va a un tercio y el comentario se rompe mientras dure (un jugador: unos 15 segundos en la séptima entrada). El runtime pide la exención con `SetProcessInformation(ProcessPowerThrottling)` (`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION` y `EXECUTION_SPEED`): 171-177/s también minimizada. Parche `wiicompiled-timers-when-hidden`.

Fuentes: desensamblado (`0x80027A54`, `0x80027BF8`, `0x80038E50`-`0x8003A41C`); `wiicompiled-periodic-sleep.patch`, `wiicompiled-audio-depop.patch`, `config/wiicompiled/rl8e54-data.csv`, `docs/wiicompiled-feasibility.md`.

## 5. Disco y `project.bin`

- **Disco:** `RL8E54`, título interno `MLB Power Pros`. El FST tiene 28 archivos, sin `.map`, `.sym` ni `.elf`. Los principales:
  - `sys/main.dol`, `files/ovl_main.rel`
  - `files/project.bin` (406.388.736 bytes)
  - `files/sound/*`
  - `files/movie/*.thp` (3 vídeos)
  - `opening.bnr`

**`project.bin`** (todo big-endian; verificado sobre sus 2.424 entradas):

| Offset | Contenido |
|---|---|
| `0x00` | versión = 2 |
| `0x04` | tamaño de sector = `0x800` |
| `0x0C` | huecos de la tabla hash = `0x1211` |
| `0x10` | tabla hash, en `0x40` |
| `0x14` | tabla de registros, en `0x6000` |
| `0x28` | tabla de orden, en `0x26000` |
| `0x2C` | tabla de nombres, en `0x2B000` |
| `0x34` | inicio de los datos, en `0x3D800` (el archivo acaba justo en `0x18390000`) |

- **Registros** de `0x1C` bytes: 0, offset del nombre, tamaño en sectores, alineación (`0x800`), 0, 0, tamaño en bytes.
- **Tabla de orden:** está dividida en **cubos**. Lo confirma el código del juego: montaje `0x80059E7C`, índice `0x8005A334`, búsqueda `0x800599E4`.
  - Las posiciones de los cubos dentro de la tabla de orden están en la tabla de `[0x18]` (`0x5800`); su número, en `[0x20]` (2).
  - Cada cubo es: u32 número de entradas, u32 0 y los pares (número de archivo, sector de inicio relativo a `0x3D800`), ordenados por sector.
  - El disco tiene un cubo vacío en `+0` y otro en `+8` con los 2.423 archivos.
  - `tools/projectbin.py` lee la cabecera de ese cubo, (2423, 0), como un primer par sin nombre. Es inofensivo para listar, extraer y reconstruir.
  - **Añadir archivos exige subir el contador del último cubo.** Sin eso, el juego no encuentra el archivo nuevo y lee desde el sector 0 (comprobado).
- **Tabla de números de archivo** (`0x800`, [`0x0C`] = 4.625 huecos):
  - La casilla i guarda la posición del registro i − 1 en la tabla de registros.
  - El juego la indexa con número + 1, así que el archivo número n es el registro n.
  - El juego pide los archivos por número, nunca por nombre: no hay nombres de archivo en el ejecutable.
- **Nombres:** rutas terminadas en NUL que empiezan por `/`.
- **Tipos:**
  - `.pack`: 2.356 empiezan por `PACKWII ` y 2 por `PACKPS2` (`team_logo.pack`, little-endian). Formato abajo.
  - `.dat`: 60 `PCD`, 2 `TCD` y 1 `SID`, little-endian: comentarios de las cartas de jugador y de equipo, y de la tienda. Empiezan por la etiqueta y un NUL, un `float` 1.0, el número de textos y sus posiciones. El texto va en u16 con una tabla de caracteres propia, aún sin decodificar.
  - `.arc`: 2 archivos U8 de Nintendo (`RFLRes01`, `SpeakerSe`).
- **Montaje:** `0x800431E8` monta `/PROJECT.BIN` (cadena en `0x80155DD0`) llamando a `0x80059E7C`.
- **Gestor de recursos de DVD:** `0x80042C28` solo procesa su cola cuando `DVDGetDriveStatus` dice que la unidad está libre. Al arrancar, el juego lanza `DVDInquiryAsync` y luego consulta el estado.
- **Entradas conocidas:**
  - `/game_data.pack` (roster, [sección 7](#7-roster-game_datapack-y-formato-de-jugador))
  - `/ClaimTheTitle.pack`, `team_logo.pack`
  - `env_title_logo`, `env_mainmenu`, `env_teamnameNN`
  - `PWM_mdl_uniform_MLB_<equipo>_<n>.pack`: uniformes. Cambiar el de CHC por el de NYY pone la gorra "NY" a los Cubs.
- **Para mods:** un `project.bin` reconstruido solo cambia los tamaños de los registros (`+0x08` sectores, `+0x18` bytes) y los sectores de inicio. La cabecera, la tabla hash y los nombres no cambian (`tools/projectbin.py`).

### Formato `PACKWII`

Big-endian, un grafo de objetos con punteros. Verificado sobre las 2.356 entradas.

- **Cabecera:** `"PACKWII "`, u32 0, u32 número de entradas (`+0x0C`), puntero a la tabla (`+0x10`), u32 `0x20` (tamaño de la cabecera).
- **Puntero:** `0x40000000 | posición en el archivo` (base 0).
- **Tabla:** entradas de 12 bytes {u32 0, u32 id, puntero}. Solo 19 packs la tienen; el resto llega a sus objetos desde una estructura raíz en `+0x20`.
- **Etiquetas en `+0x20`:**
  - `1MPP`: movimientos de lanzador (263 packs).
  - `1MBP`: movimientos de bateador (249).
  - `PJ7M`: modelos (8).
  - Otras: `1HMP`, `1UAP`, `1LMP`, `pM_U`.
  - 1.822 no tienen etiqueta: interfaces `env_*` (`pM2`), modelos, estadios, temporadas.
- **`SURF`:** capa 2D de las interfaces. Tamaño `0x2C`, nombre, escala 100/100 y punteros.

**Texturas** (comprobadas decodificándolas; `tools/packwii.py`):

| Tipo | Cabecera |
|---|---|
| Modelo | u16 **alto**, u16 **ancho**, u8 tipo (`0x80` RGBA8, `0x81` RGB5A3, `0x82` C8, `0x84` C4), `+0x08` puntero a los píxeles, cuatro palabras `0x82000000`. En C4/C8 la cabecera de paleta va en `+0x24`. |
| Interfaz | u16 **alto**, u16 **ancho**, u32 formato GX, `+0x08` píxeles, `+0x14` 1, `+0x18` 1, `+0x24` u16 entradas de paleta, `+0x28` u32 formato de paleta (2 = RGB5A3), `+0x2C` puntero a la paleta (0 si está en otro sitio). |
| Paleta | u16 entradas (16 o 256), u16 0, u8 `0x81`, u8 0, u16 `0x6000`, `+0x08` puntero a los colores RGB5A3. |

- **Conjuntos de texturas:** {u32 n, → n texturas, u32 m, → m paletas}. Hay una paleta para todas o una por textura.
- **Píxeles:** en el orden por bloques de GX (C4 8×8, C8 8×4, RGB5A3 y RGBA8 4×4; RGBA8 guarda AR y GB en dos mitades de 32 bytes por bloque).
- **Cobertura:** 11.292 texturas en 1.637 packs; 10.769 de las 11.200 con paleta quedan emparejadas.
  - Por puntero: cabecera de interfaz, conjuntos de texturas, modelo con paleta en `+0x24`.
  - Por posición: los colores de la paleta van justo después de los píxeles. La cabecera de paleta que hay justo antes de una textura es la de la textura anterior, no la suya.
  - Las demás reciben la paleta de otro pack.
- **Comprobado contra el juego** (14 de octubre de 2026, volcado de texturas del runtime hasta el menú principal):
  - El hash XXH64 de los píxeles calculado desde el pack coincide con el del juego: el juego usa los bytes del pack tal cual. Con la orientación corregida (abajo), Texture Studio calcula el nombre exacto de 37 de los 39 volcados que vienen de packs.
  - De 38 texturas del pack vistas con paleta, 36 tienen la misma paleta que la emparejada aquí. Las otras dos: una paleta suelta de `env_common.pack` sin puntero conocido, y `env_mainmenu.pack` `0x163A0`, que el juego dibuja con una paleta distinta de la que apunta su cabecera (cambio de paleta en ejecución).
  - El resto del volcado no viene de packs: fotogramas de vídeo THP (I8 de 512×448 y 256×224) y unas pocas texturas de tamaño no potencia de dos (texto generado en ejecución, por confirmar).
- **Atlas de etiquetas del menú principal:** `env_mainmenu.pack` `0x2A920` (512×512, C8) tiene las 12 etiquetas en sus colores; la mitad superior es la normal y la inferior la seleccionada. Un recuadro pintado sobre «Exhibition» se ve en el juego.
- **Al importar** `packwii.py` deja fija la paleta de las texturas emparejadas por posición y conserva el índice exacto de los píxeles no editados, porque el juego puede dibujar una textura con otra paleta.
- **Alto antes que ancho** (14 de octubre de 2026): las cabeceras de textura guardan primero el alto y luego el ancho.
  - Los 19 volcados no cuadrados del menú tienen las dimensiones al revés de lo que se leía (por ejemplo `env_mainmenu.pack` `0x12140`: el juego la usa como 256×64).
  - Medido además por suavidad de la imagen en 1.600 texturas no cuadradas de packs: 1.585 se ven bien leyendo alto y ancho, también las de modelo.
  - Era la causa de las texturas «rayadas» de `env_mainmenu.pack` (`0x12140`, `0x9B6E0`, `0xBB940`): ahora se decodifican bien (`0x9B6E0` es el arte del menú del Wii Remote).

**Funciones DVD:**

| Función | Dirección |
|---|---|
| `DVDInit` | `0x800A9F7C` |
| `DVDOpen` | `0x800A9764` |
| `DVDClose` | `0x800A9A10` |
| `DVDReadPrio` | `0x800A9CDC` |
| `DVDLowInquiry` | `0x800B0F40` |
| `DVDLowRead` | `0x800B1840` |
| `DVDLowSeek` | `0x800B19F0` |

- Respuesta de unidad comercial que espera el juego: `+0` = `0x00000002`, `+4` = `0x20060526`, `+8` = `0x41000000`.

Fuentes: `docs/phase0-report.md`, `docs/mods.md`, `tools/projectbin.py`, `tools/packwii.py`, `wiicompiled-dvd-low-completion.patch`.

## 6. Estadios: modelos `PJ7M` y tabla `tbl`

Cada estadio son dos entradas de `project.bin`:
- `stadium_<código>.pack`: los gráficos. Entre 50 y 100 modelos `PJ7M` y sus texturas, unos 1,1 MB.
- `stadium_<código>tbl.pack`: la colocación de cada modelo y la malla de colisión.

Los códigos son los de los equipos (`col` = Coors Field, `nyy` = Yankee Stadium…), más `org01`–`org06`, `ml01`, `bullpen`, `bullpen2` y `season`. Hay además `stadium_sky_*` (cielos, sin tabla), `stadium_common`, `stadium_hanabi` y `stadium_debug`.

Verificado (14 de octubre de 2026):
- `tools/stadium_re.py check` recorre los 47 packs de estadio y cumplen las invariantes de abajo.
- Cuatro cambios en una copia de Coors Field, cargada como mod en un partido, se ven en el juego (ver «Comprobado en el juego»).

### Modelo `PJ7M`

Big-endian, punteros `0x40000000 | posición`.

| Estructura | Contenido |
|---|---|
| Cabecera (`0x28`) | `'PJ7M'`, u32 `0x28`, → nombre (va justo antes), → raíz, 0, 0, u32 texturas, u32 posiciones (suma de todas las mallas), u32 triángulos, 0 |
| Raíz | u32 9, → escena, → tabla de texturas, → tabla de paletas, ceros, `+0x40` → `"_Wmodel_<nombre>"` |
| Escena | {1, → nodo}, {1, → ?}, {n, → texmaps}, {n, → materiales}, u32 `0x37` |
| Nodo (`0x0C`) | u16 número de mallas, u16 0, → lista de mallas, → nombre del nodo de Maya (`pPlane650`…) |
| Lista de mallas | entradas de 8 bytes: u16 material, u16 1, → malla |
| Malla (`0x1C`) | u32 0, u32 tamaño de la display list, → display list, → POS, → NRM, → CLR0, → TEX0 |
| Material (`0x34`) | u16 3, u16 primer texmap (`0xFFFF` sin textura), 3 colores RGBA8, `+0x1C` 8 ranuras u16 de texmap (`0xFFFF` = libre; siempre se usa una) |
| Texmap (`0x4C`) | u16 índice de textura, `+0x0C` u16 ancho − 1 y alto − 1, `+0x1C` u16 y `+0x1E` u16 cuántas veces se repite la textura en U y en V; los bytes `+2..+0x0C` son iguales en las 11.281 de todos los estadios |
| Tablas de texturas y paletas | entradas de 8 bytes {→ cabecera, 0}, en paralelo. Las cabeceras son las de textura de modelo de la tabla de texturas de arriba. Sin paleta, el puntero es 0. |

- **Vértices:** sin cuantizar.
  - POS y NRM en f32 xyz, CLR0 en RGBA8, TEX0 en f32 uv.
  - Las UV llegan hasta 32: las texturas se repiten.
  - **Repeticiones del texmap** (`+0x1C` U, `+0x1E` V): en las mallas de los 43 packs de estadio, siempre `≥` el mayor `|u|` y `|v|` de las mallas que lo usan (normalmente su redondeo hacia arriba; hasta 280). Con 1 el juego no repite la textura en esa dirección y estira el borde: color plano con 1/1, rayas con 1 en una sola dirección. Visto el 18 de octubre de 2026 en el visor con el Polo Grounds sobre AT&T Park (UV de SketchUp de hasta ±1.600, plantillas 1/1 y 1/2): gradas grises y torres a rayas; con los campos bien puestos, igual que en Blender.
  - Cada array mide lo que pide el índice más alto de su display list; no hay campo de tamaño.
  - Unas pocas normales valen (0, 0, 0).
- **Display lists:**
  - Alineadas a 32 bytes y rellenas con NOP.
  - Solo usan `0x98` (strip, 182.126) y `0x90` (triángulos, 3.647), siempre con VAT 0.
  - Cada vértice ocupa 9 bytes: u8 índice de matriz (`PNMTXIDX`: 0, o 3/6/9/12 solo en cielos y `common`) y u16 de POS, NRM, CLR0 y TEX0.
- **Triángulos de la cabecera:** cuentan n − 2 por strip, degenerados incluidos (450 de 450 modelos comprobados).
- **Sentido de los triángulos:** horario visto de frente (como GX). La normal geométrica es opuesta a la guardada en 11.093 de los 11.109 triángulos de Coors Field.
- **Unidades:** decímetros. Y hacia arriba, el plato en el origen, −Z hacia el center field y +X hacia el right field.
  - La goma del lanzador mide 6 (61 cm) y está a z ≈ −190.
- **El campo es fijo; el estadio se adapta a él** (14 de octubre de 2026):
  - en la colisión de todos los estadios salvo Toronto, el montículo (superficie 8) tiene el centro a 18,51 m del plato y 5,64 m de diámetro;
  - el estadio no tiene ningún dato con la posición de las bases, la caja de bateo o la defensa: sus gráficos solo las dibujan;
  - por eso un modelo propio tiene que ponerse en ese marco (plato en el origen, bases a 90 pies), para que lo dibujado coincida con donde el juego pone a los jugadores;
  - sin comprobar: de dónde saca la defensa la distancia a las vallas, si de la colisión o de datos del estadio base.
  - La valla del left field está a unos 1.085 (108 m).
- **Texturas:** C4, C8, RGB5A3 y RGBA8.
  - Se comparten entre modelos del mismo pack; Coors Field tiene 115.
  - Las piezas `*_ALP_*` llevan texturas con alfa: manchas sobre el césped, tierra y luces.
- **Sin transformación en el modelo:** casi todo está en coordenadas del mundo. Las piezas repetidas (banderas, fuentes) están en coordenadas locales y se colocan con la tabla.
- **Nombres de pieza:** `DP_ground`, `DP_MIP_turf`, `DP_stand_L1`…`R3`, `DP_fence_L/R`, `DP_score`, `DP_auroravision`, `DP_Audience*`, `DP_FLG_flagNN`, `DP_*_lumine_*`, `DP_EVC_NOUPCAM_outside_*`…

### Tabla `tbl`

- **Raíz:** es el último objeto del archivo: u32 número de modelos y 11 punteros. Se encuentra buscando desde el final el número seguido del puntero a `0x20`.
- **Punteros de la raíz, por orden:**
  1. colocaciones;
  2. árbol A: nodos de plano que reparten las colocaciones (ver «Árboles A y B»);
  3. listas u16 del árbol A (índices de colocación);
  4. árbol B: nodos de plano que reparten los triángulos de colisión;
  5. listas u16 del árbol B (índices de triángulo);
  6. vértices de colisión;
  7. cabecera de colisión;
  8. registros de posición más dirección (`0x18` bytes, sin identificar; van justo detrás de los vértices de colisión);
  9. dos pares {→, 7} sin identificar;
  10. lista de texturas del pack de gráficos;
  11. sin identificar.
- **Colocación** (`0x54` bytes, una por modelo): → cabecera `PJ7M` en el pack de gráficos, traslación xyz, rotación xyz en radianes, escala xyz, caja (mín. y máx.), esfera (centro y radio), u32 flags.
  - Solo 12 de las 2.925 colocaciones de todos los estadios giran en X o Z. El orden de los ejes no está comprobado; el importador usa XYZ.
- **Colisión:** u16 número de triángulos, u16 número de vértices, u32 0; luego triángulos de cuatro u16 (a, b, c, superficie) y vértices f32 xyz.
  - Coors Field tiene 2.922 triángulos y 2.133 vértices.
  - **Sentido:** la normal (b − a) × (c − a) apunta al lado que golpea la pelota. Todos los suelos (5, 6, 8) miran hacia arriba y casi todas las paredes (0, 1, 2, 9, 15) hacia el plato. En las gradas (11) hay de los dos sentidos.

### Árboles A y B

14 de octubre de 2026; `tools/stadium_re.py check` los comprueba en los 47 packs.
- **Nodo de `0x1C` bytes**, en preorden y con el nodo 0 como raíz:

| Offset | Contenido |
|---|---|
| `+0x00` | plano: nx, ny, nz, d (f32) |
| `+0x10` / `+0x12` | u16 hijo del lado H / del lado L (0 = el lado es una hoja) |
| `+0x14` / `+0x16` | u16 inicio de la lista del lado H / L |
| `+0x18` / `+0x19` | u8 número de entradas de la lista del lado H / L |
| `+0x1A` | u16 0 |

- **Lado H:** lo que llega a n·p + d < 0. **Lado L:** lo que llega a n·p + d > 0. Lo que cruza el plano está en los dos.
  - En los 47 packs, cada triángulo (por sus vértices) y cada colocación (por su caja y su esfera) está en el lado correcto de todos los planos por encima.
  - Desde la raíz se llega a todos los triángulos y a todas las colocaciones.
- **Árbol A:** reparte las colocaciones por su caja. En AT&T Park, Coors Field y Yankee Stadium usa planos diagonales; en todos los packs tiene como mucho 32 por hoja. El juego lo usa para **recortar por cámara**: ver «Escribir packs desde cero».
- **Árbol B:** reparte los triángulos de colisión. Usa planos alineados con los ejes, profundidad ≤ 13, hojas de 11 a 15 triángulos de media y como mucho 111.
- **Recorrido:** `0x80C860E4` y `0x80C862EC` son recursivas (índice × 28). Llaman a `0x80CCB438` con el plano para decidir qué lados visitan. En cada hoja ponen a 1 `banderas[índice]` en un array de un byte por elemento: no hay un búfer de tamaño fijo.
  - **Leen el número de la hoja con signo** (`lbz` + `extsb`): una hoja con más de 127 entradas no se recorre.

### Escribir la colisión

14 de octubre de 2026, `tools/blender/powerpros_stadium/tbl_write.py`.
- **Sitio de los datos:** la colisión, los vértices y los dos árboles nuevos van detrás de la raíz, y se repuntan sus campos. La raíz no se mueve: la tabla de estadios del juego apunta a ella, y el runtime la sigue encontrando al buscar desde el final.
- **Construcción de los árboles:** planos alineados con los ejes y, en cada nodo, el corte que mejor reparte sin duplicar mucho; hojas de como mucho 127.
  - Con los triángulos de Coors Field sale un árbol de 240 nodos y 6.450 entradas; el del juego tiene 1.086 nodos y 13.047 entradas.
- El exportador de Blender rehace el árbol A cuando una pieza cambia de caja, y escribe la colisión cuando se edita su malla.
- **Comprobado fuera del juego:** ida y vuelta en Blender (`tools/blender/test_export.py`) y las mismas comprobaciones que los packs del juego.
- **Comprobado en el juego** (14 de octubre de 2026): estadio nuevo «Polo Grounds», hecho con un modelo de SketchUp convertido con el complemento, con base AT&T Park, 7,8 MB de gráficos y colisión generada de 7.116 triángulos.
  - El partido del benchmark carga y se juega a 60 FPS.
  - El plato, la caja de bateo, las bases y el montículo del modelo coinciden con los jugadores.
  - Un rodado recorre el campo nuevo con normalidad, y no faltan caras ni se ven las traseras.
  - Comprobado por el usuario (15 de octubre): los batazos chocan con las vallas y los home runs cuentan. Pero la colisión generada no seguía al modelo visible en todas partes: en algunos sitios la bola debía caer en las gradas que se ven y en cambio rebotaba contra una pared que no se ve. Lo que ve el jugador no siempre es con lo que choca el juego; el generador de colisión necesita trabajo.
  - Hay 3 triángulos degenerados o repetidos.
- **IDs de superficie** (por geometría en Coors Field y Yankee Stadium; su efecto en el juego está sin comprobar):

| ID | Geometría | Probable |
|---|---|---|
| 0, 1, 2 | paredes verticales de 70 m, alrededor del campo | límites invisibles |
| 5 | suelo a altura 0, del plato al fondo | césped |
| 6, 7 | suelo a altura 0 | tierra y pista de aviso |
| 8 | 25 m² a 16–21 m del plato, apenas elevado | montículo |
| 9 | paredes bajas (hasta 4 m) | vallas y muros del campo |
| 11 | lo más numeroso, de 40 a 170 m del plato, hasta 34 m de alto | gradas |
| 13 | superficies horizontales elevadas | gradas y techos |
| 14 | verticales a unos 108 m, hasta 25 m de alto | postes de foul |
| 15 | pared a 15–16 m (Coors) o 25 m (Yankee) del plato | backstop |
| 17, 18 | pocas caras | sin identificar (18 en Coors: plano a 138 m, ¿la fuente?) |

**Comprobado en el juego** (14 de octubre de 2026): una copia modificada de `stadium_col.pack` y `stadium_coltbl.pack` en `project/` de un mod, en un partido del benchmark en Coors Field.
- Subir 15 m las posiciones de `DP_stand_C` sube las gradas centrales.
- Poner azul el color de vértice de `DP_ground` vuelve azul la tierra del infield.
- Pasar a rojo el segundo color del material de `DP_MIP_turf` no cambia nada: el estadio se dibuja sin la luz de los materiales.
- Cambiar la traslación de las colocaciones de `DP_FLG_*` pone las banderas en fila sobre el césped del center field. El juego usa la tabla, aunque la caja y la esfera de la colocación sigan en el sitio viejo.
- Las banderas (`0x500`) son tela que el juego anima desde la colocación, no desde la malla de la pieza: con la pieza vacía siguen ondeando en su sitio. Se dibujan sin prueba de profundidad: bajadas 100 m bajo el campo se veían a través del suelo. Con la escala de la colocación a `1e-4` desaparecen (18 de octubre de 2026).
- **Pack de menú por estadio:** `env_stadium_menuNN.pack`, con NN el número del estadio (orden de `kStadiumCodes`, Coors Field = 27). Dos texturas C8 con paleta propia de 256 colores: la foto (512 × 256, píxeles en `0x60`) y «Home of…» (512 × 64). Un pack con el mismo tamaño y las dos texturas recodificadas funciona como menú de un estadio nuevo.

### Escribir estadios

Comprobado en partidos de Coors Field, 14 de octubre de 2026.
- **Datos nuevos al final del pack:** los acepta, con punteros nuevos (`0x40000000 | posición`). Una malla repuntada a una display list nueva de triángulos sueltos (`0x90`) en vez de strips, y a arrays nuevos, se dibuja bien. No hay tabla de reubicación que actualizar.
- **Tamaño:** un `stadium_col.pack` de 9,5 MB y uno de 21 MB cargan y el partido se juega. Uno de 31,5 MB o de 43 MB se queda en «Preparando partido»: no cabe en el montón de MEM2 (sección 1). Con el montón ampliado (parche `wiicompiled-mlb-mem2-heap`), uno de 32 MB y uno de 80 MB cargan y el partido se juega; con 96 MB el pack se lee, pero el partido no empieza.
- **Tablas de texturas y paletas:** acaban con una entrada de 8 bytes a cero, en los 82 modelos de Coors Field. El juego las recorre hasta esa entrada: al copiarlas a otro sitio sin ella, las texturas con paleta del modelo salen blancas. Ampliar el array de texmaps o el de materiales (que usan su contador de la escena) funciona. **Límite de texmaps por pieza:** ninguna pieza de los 47 packs pasa de 30. Cada material lleva en `+0` (s16) un índice que `0x800613BC` pasa a `0x800537B4`, que lo usa en una tabla de ranuras de `0x78` bytes; con 82 texmaps en una pieza (un modelo descargado convertido, 18 de octubre de 2026) los índices se salían: `GXLoadTexObj: invalid tex map id`, objetos TLUT corruptos, vallas sin dibujar y lecturas fuera de memoria desde `0x8069CF38` hasta cerrar el juego. Repartidas en piezas de 30 como mucho, el partido va bien. La capacidad exacta de la tabla está sin medir.
- **Lista de texturas del `tbl`:** entradas {→ textura, → paleta, u32, u32}. Los dos u32 (`0xAC800`, `0xB2540`…) parecen direcciones de TMEM: texturas precargadas. Sin comprobar.

### Meter un modelo externo

Polo Grounds de SketchUp, 14 y 15 de octubre de 2026; partidos del benchmark.
- **Tamaños de los estadios del juego**, en los 47 packs:
  - como mucho 24.768 bytes de display list, 573 vértices por llamada de dibujo, 1.303 posiciones por malla y 29 mallas por modelo;
  - texturas de 32 a 256 píxeles, casi todas de un solo nivel (las piezas `MIP`, como el césped, usan también niveles más pequeños: ver «Inventario de campos…»): 8.135 CI4, 3.130 CI8, 77 RGBA8 y 33 RGB5A3 (`tools/stadium_re.py stats`). El recomp les genera mipmaps al cargarlas (parche `wiicompiled-auto-mipmaps`).
  El exportador trocea las mallas a 600 triángulos, 1.300 posiciones y 570 vértices por llamada, y reduce las texturas nuevas a 256 píxeles. Sin trocear, lo que faltaba en pantalla se explicó luego por las superficies pegadas, no por el tamaño: el troceado es por prudencia.
- **Profundidad:** superficies paralelas a 0–2 cm (ventanas sobre una pared, una textura de asientos sobre la grada, las dos caras de una pared) parpadean o se tapan a 100–150 m; en Blender se ven bien. Separarlas o dejar una sola cara lo arregla. El recomp dibuja con profundidad `Depth32Float` y Z invertida (aurora, `UseReversedZ`), mucho más precisa que la de 24 bits del Wii: el parpadeo de las caras a 0 cm es inevitable, pero a 2 cm no debería darse. Sin comprobar con una prueba aislada.
- **Caras traseras:** el modelo, con las dos caras de cada pared en el mismo sitio, mostraba la de atrás tapando la buena. Con una sola cara mirando al campo se ve bien. No está comprobado si el juego descarta caras traseras en los estadios.
- **Texturas grandes** (512 px o más) a 100 m se ven ruidosas: casi ninguna textura de estadio trae mipmaps (solo las de las piezas `MIP`).
- **`0x80047874`** no dibuja modelos. Pone `GXSetZMode(1, GX_ALWAYS, …)`, cambia la escritura de color y alfa y dibuja un cuadrado: es un borrado de pantalla.
- **Público del estadio:**
  - `DP_AudienceF_*` son caras casi horizontales sobre las gradas (normal z ≈ 0,95), con texturas de 128 × 64 que se repiten cada 6 m de ancho y 4 m de alto;
  - `DP_AudienceS_*` son carteles verticales que se repiten cada 6 × 3 m;
  - las gradas usan una textura de asientos de 128 × 128 cada 25 m.
  - Caras nuevas en esas piezas, con sus materiales, se ven en Blender, pero en el partido de prueba no aparecieron. Sin resolver.
- **Una textura con paleta de otra pieza:** reutilizarla en una pieza distinta (tablas de textura y paleta ampliadas) la dibujó negra. Convertida a RGB5A3 se ve.
- **Orden de las piezas:** reconstruir el árbol A como una sola hoja en el orden de la tabla no cambió nada visible.

### Inventario de campos para escribir packs desde cero

18 de octubre de 2026; recorrido de los 43 packs gráficos y 40 tablas por campo de 2 o 4 bytes.
- **Modelo `PJ7M`:** todo lo que varía es puntero, contador o tamaño, que se calcula al escribir, salvo:
  - la segunda lista de la escena `{1, → ?}`: apunta a un puntero a la palabra `0x01000000`, igual en los 2.925 modelos de estadio con tabla (lo que se leía detrás eran ceros de relleno y el comienzo de una display list). Solo un modelo de `stadium_common` lleva otra palabra y campos en `+0x10..+0x3C` de la raíz;
  - el material: `+0x00` vale 3 (11.239) o 4 (6); los tres colores RGBA8 de `+0x04` casi siempre son `000000FF`, `CCCCCCFF`/`FFFFFFFF` y `000000FF` (sin efecto visible comprobado); `+0x32` vale 1 (7.567) o 0 (3.678), sin descifrar;
  - el texmap: además del índice, el tamaño y las repeticiones, todo constante;
  - la cabecera de textura: `+0x06` vale `0x2000` en C4, `0x0000` en C8/RGBA8 y `0x6000` en RGB5A3;
  - la pantalla grande (`DP_auroravision`) usa una textura RGB5A3 de 8 × 8 con una cabecera de paleta «hueco»: 0xFFFF entradas, formato `0x4000` y sin colores (puntero 0); parece el sitio donde el juego pone la imagen de la pantalla;
  - texturas sin usar: varios packs traen cabeceras de textura (con su paleta en `+0x24`) a las que no apunta nada, ni los modelos ni la tabla;
  - detrás de los píxeles de algunas texturas van sus **mipmaps**: en la textura C4 de 32 × 64 de Arizona, los niveles de 16 × 32 y 8 × 16. La tabla de texturas del modelo dice qué nivel usa cada entrada: el segundo u32 de cada entrada {→ cabecera, nivel} vale 0, 1 o 2 (176 entradas no son 0). Las piezas `MIP` repiten la misma textura con los niveles 2, 1 y 0 (`DP_ALP_MIP_line` en Arizona);
  - el texmap tiene su propio tamaño (`+0x0C`): en 129 texmaps no es el de su textura. En las piezas de público, una textura de 8 × 8 va declarada de 128 × 64: parece que el juego la cambia en marcha por los fotogramas del público.
- **El código del juego no busca piezas por nombre:** ni `main.dol` ni `ovl_main.rel` contienen `DP_`, `Audience`, `FLG` o `auroravision`. El papel de cada pieza lo dan los **flags de colocación**.
- **Flags de colocación según la pieza** (por los nombres de los modelos que los llevan; hipótesis por comprobar en el juego):
  - **byte `0x0000FF00`, tipo de pieza:** `0x01` marcador, `0x02` pantalla (`auroravision`), `0x04` montículo y líneas (`BCL`), `0x05` banderas (sin prueba de profundidad), `0x06` sombras, `0x08` luces (`lumine`), `0x09` focos encendidos, `0x0A` parasoles de focos, `0x11` vallas, `0x12` red de detrás del plato, `0x15` público C, `0x16` público F (tumbado), `0x17` público S (vertical), `0x18` fuentes, `0x19` césped `MIP`, `0x1A` césped con alfa; `0x00` gradas y resto;
  - **byte `0x00FF0000`, cómo se dibuja:** `0x06` con alfa (`ALP`), `0x0A`/`0x0F` aditivo (`ADD`), `0x81`/`0x87`/`0x90`/`0x93` suelo, `0x20` `LOF`;
  - **bits altos:** `0x40000000` gradas altas detrás del plato (`2F`), `0x20000000` `EVC`, `0x80000000` `NOUPCAM`; `0x04000000`/`0x08000000` acompañan al suelo y al césped.
- **Bloques de la tabla sin descifrar** (Coors Field):
  - `unk_20`: grupos de `0x20` bytes {→ lista de puntos, u32 n, caja mín. xyz, máx. xyz} terminados por un grupo a ceros; cada punto son `0x18` bytes {posición xyz, rotación xyz}. Coors Field tiene 4 grupos (32, 50, 50 y 25 puntos) a unos 50 m de altura y Yankee Stadium 2 (150 y 144); Arizona, `bullpen`, `bullpen2` y `season`, ninguno. Por la cantidad y la rotación, parece público colocado en 3D;
  - `unk_24`: dos pares {→ lista, u32 n} de registros de `0x1C` bytes {posición xyz, escala xyz = 1, ángulo}; en Coors, 7 por lista, a 24 m a cada lado del plato y 0,64 m bajo el suelo: parecen los asientos de los banquillos. `bullpen`, `bullpen2` y `season` tienen un solo par {0, 0} y la raíz justo detrás;
  - `textures` (puntero 10): entradas de `0x10` bytes {→ cabecera de textura, → paleta, u32, u32}, con los dos u32 como direcciones de TMEM (texturas precargadas);
  - `unk_2c`: {u32 n, n × u32 orden} (siempre 8 y 0 a 7) seguido de la lista de texturas, que acaba con una entrada {0, 0, u32, 0}; ese u32 va de `0x692800` a `0x703280` según el estadio (sin descifrar). En `bullpen`, algunas entradas no apuntan a ninguna textura.

### Escribir packs desde cero

18 de octubre de 2026; `tools/blender/powerpros_stadium/stadium_model.py`, `stadium_read.py` y `stadium_write.py`.
- **Descripción:** un estadio como datos (texturas, paletas, modelos, colocaciones, colisión, árbol A y los bloques de la tabla), con índices en lugar de posiciones. El lector la saca de los dos packs del juego; el escritor escribe los dos packs desde búferes vacíos, sin leer ningún pack.
- **Comprobado fuera del juego** (`tools/test_stadium_writer.py`, 45 s):
  - el lector cubre todos los bytes distintos de cero de los 40 estadios con tabla;
  - cada estructura escrita es igual byte a byte a la del juego, puestos a cero sus punteros (salvo el árbol B, que se reconstruye);
  - leer, escribir y volver a leer da lo mismo en los 40, y lo escrito pasa `stadium_re.py check`;
  - una descripción pasada a listas, números y texto hexadecimal escribe los mismos bytes: el escritor no usa nada más.
- **Comprobado en el juego:**
  - un **estadio mínimo inventado** (`tools/stadium_minimal.py`: suelo a cuadros, valla, grada y colisión, sin leer nada del juego, con los bloques de la tabla vacíos) se ve en el visor a los 7,6 s y se juega un partido de 2 minutos en él, a 60 FPS y sin accesos fuera de memoria;
  - **Coors Field, AT&T Park y Yankee Stadium reconstruidos** (`tools/stadium_rebuild.py`, como estadios nuevos) se ven en el visor igual que sus originales, cargados por la misma vía, desde la vista del bateador y dos cámaras libres: diferencia media por píxel de 0 a 0,003 sobre 255. Un partido de 2 minutos con cada uno, sin accesos fuera de memoria.
- **El árbol A recorta por cámara:** reconstruido como una sola hoja (como hace el exportador del complemento), Coors Field visto desde fuera dibujaba las gradas detrás del plato y otras piezas que el original no dibuja desde ahí; desde la vista del bateador no cambiaba nada. El juego recorre ese árbol con la posición de la cámara y descarta las colocaciones de los lados que no visita (`0x80CCB438` decide los lados). Por eso la descripción conserva el árbol A del juego.
- **Pendiente** (fase 2): qué hace cada flag de colocación, los bloques `unk_20` y `unk_24`, las texturas precargadas, los campos del material y cómo construir un árbol A propio que recorte bien.

### Qué hacen los campos, probado en el visor

Fase 2, 18 de octubre de 2026. Método: Coors Field reconstruido con el escritor, con un solo dato cambiado, comparado en el visor con el reconstruido sin cambios desde las mismas cámaras (`exp.py` del agente, capturas a los 17 y 19 s para ver lo que se anima).
- **Flags de colocación, byte `+0x52` (tipo de pieza).** El código lo lee en `0x80C600C8`, que trata aparte el 17 (vallas) y el 18 (red de detrás del plato). Quitando el tipo (a 0):
  - `0x16` y `0x17` (público tumbado y vertical): el público **deja de animarse** (la diferencia entre dos capturas baja de 1,44 a 0,25 en una escala de 0 a 255) y sigue dibujado;
  - `0x01` (marcador): desaparece lo que el juego dibuja encima en vivo (las filas de resultados salen como bloques negros);
  - `0x02` (pantalla grande): la pantalla sale negra: el juego pinta su imagen en la textura «hueco»;
  - `0x05` (banderas): las banderas pasan a dibujarse como piezas normales y quietas. Con su tipo no las dibuja el estadio sino un sistema de banderas propio que cuelga del árbol de estados del partido: **en el visor, con el partido quieto, no salen** (las de un estadio escrito desde cero sí salen en el visor: ver «Estadio desde cero en el juego»);
  - `0x11` (vallas): ningún cambio visible: su efecto es de juego, no de dibujo.
- **Flags de colocación, byte `+0x51` (grupo de dibujo).** `0x80C600C8` lo busca en una lista de grupos del objeto escena (`+1528`, entradas con su número en `+4`). Cambiándolo:
  - `0x06` (piezas `ALP`) a 0: las manchas y líneas del campo pasan de transparencia suave a recorte con bordes duros: `0x06` es el grupo con mezcla alfa;
  - el `0x81` del suelo a 0: el césped atraviesa la tierra del cuadro: `0x81` es la capa de fondo, que se dibuja antes que el resto. Dentro de ella no cuenta la profundidad, sino el orden: en el Polo Grounds desde cero, la tierra del cuadro a la altura 0 no se veía porque el plano de césped de 4 cm más abajo se dibujaba después; dibujando las capas de abajo arriba se ve (19 de octubre de 2026);
  - `0x06` puesto en una grada: los anuncios de su valla se vuelven semitransparentes: el grupo afecta a toda la pieza.
- **Bits altos:** sin `0x80000000` (`NOUPCAM`), desde una cámara alta fuera del estadio aparecen los edificios y árboles de alrededor que el original esconde: esconde la pieza cuando la cámara está arriba. También la esconde en la vista del bateador (comprobado en la fase 3, en el visor y en un partido). Quitar `0x40000000` o `0x20000000` no cambió nada desde las vistas probadas (bateador, aérea, gradas); dependen de otras cámaras del partido.
- **`unk_20`:** vaciarlo no cambia nada de día. Sus puntos están dentro de las cajas de las piezas `lumine`, a 46-57 m de altura y girados hacia el campo, un grupo por torre (Coors: 32, 50, 50 y 25): casi seguro son los focos, uno a uno, para la noche. Sin comprobar en un partido de noche.

### Estadio desde cero en el juego

Fase 3, 19 de octubre de 2026; `tools/blender/powerpros_stadium/scratch.py`. Una colección de prueba con un objeto por papel, escrita sin estadio base y abierta en el visor y en un partido:
- **Banderas (tipo `0x05`):** todas las banderas del juego son el mismo plano de 10 vértices (5 columnas × 2 filas, mástil en x = 0, tela hacia +x, 32,64 × 23,78 unidades antes de escalar), una pieza por bandera, con la rotación a 0: el sistema de banderas la gira con el viento. Las cuatro banderas de prueba juntas en una pieza de 16 vértices hacían que el sistema escribiera fuera de memoria (6 accesos por pieza; `lr` `0x8005D5A0`, `0x8005BA60`, `0x8005D364`) y el estadio no llegaba a estar listo. Una pieza por bandera carga bien, con el plano del juego o con un rectángulo de 4 vértices; con el plano, la tela se pliega.
- **Público (tipos `0x16`/`0x17`):** el juego lo anima a través de las **texturas precargadas**. La lista de precarga (`textures` y `unk_2c` de la tabla) tiene siempre 8 entradas con papel fijo, en los 37 estadios con precarga: 0 a 2 marcador (C4), 3 pantalla (el «hueco» RGB5A3 de 8 × 8), 4 y 5 público vertical, 6 y 7 público tumbado (C8 de 128 × 64, o de 8 × 8 en dos estadios, siempre declarado 128 × 64 en el texmap). Las piezas de público que usan esas texturas muestran a las personas del juego y cambian de postura cada pocos segundos, sea cual sea la imagen de la textura (con texturas propias en esas entradas se ven las mismas personas). Sin precarga, Coors Field deja de animar su público; una pieza de público con una textura no precargada se dibuja quieta con su imagen.
- **`0x80C5DA4C`:** recorre las colocaciones y, según el tipo (`0x13`/`0x17`, `0x14`/`0x16`, `0x15`), escribe en una tabla de visibilidad por pieza (escena `+1720`) tres valores distintos; la pieza con `0x40000000` usa otro (escena `+1973`). Parece la cantidad de público según la asistencia.
- **Luces (tipo `0x08`, `lumine`):** con `0x000A0800` (como las piezas `ADD_lumine` del juego) un brillo de prueba no se dibuja de día; con `0x000A0000` se dibuja sumando luz (247 sobre un cielo de 98). El tipo `0x08` es lo que se enciende de noche; sin comprobar de noche.
- **`NOUPCAM`:** un edificio de prueba con `0x80000000` no sale ni desde la vista del bateador, en el visor y en el partido; sin el bit, sí.
- **Sin explicar:** con una cámara libre del visor justo delante del edificio de prueba, a 150 m del plato, no se dibuja ni sin `NOUPCAM`; movido 150 m hacia el campo, sí.

### Lista de estadios del juego

14 de octubre de 2026, por los datos del REL; para añadir estadios.
- **Números de archivo:** el juego no guarda nombres de archivo. Pide las entradas de `project.bin` por número de registro: Coors Field = 75, su tabla = 76. Hay 4.681 registros, de los que 2.424 tienen nombre: quedan 2.256 libres para archivos nuevos.
- **Tabla de estadios:** `ovl_main.rel` `+0x92A450` (en memoria `0x80E9F9D0`, sección 5). Son 40 entradas de 12 bytes: {u32 número del pack gráfico, u32 número del `tbl`, puntero a la raíz del `tbl` (`0x40000000 | posición`)}.
  - El juego no busca la raíz del `tbl`: la lee de aquí. Un `tbl` reconstruido debe dejarla en el mismo sitio o actualizar esta tabla.
  - **Orden:** el de los equipos de MLB por división: BAL, BOS, NYY, TB, TOR, CWS, CLE, DET, KC, MIN, LAA, OAK, SEA, TEX, ATL, FLA, NYM, PHI, WSH, CHC, CIN, HOU, MIL, PIT, STL, ARI, COL, LAD, SD, SF. Siguen `org01`–`org06`, `ml01`, `bullpen`, `bullpen2` y `season`.
  - Justo después hay una vtable: la tabla no se puede alargar en su sitio.
- **`0x80C7AF48`** (`GetStadiumFiles`, nombre propio): copia `tabla[(número − 1) × 12]` a su primer argumento. El número de estadio va de 1 a 40 y no se comprueba. Es la única referencia a la tabla en el código.
- **Las pantallas de elección de estadio son dos:**
  - **Exhibición:** constructor en `0x80CD29xx`, sin el widget de lista genérico.
    - Lista de números en `+0x296C`, número de entradas en `+0x2968` y cursor en `+0x2938`, con sitio para 38 entradas. Detrás vienen campos en uso.
    - La lista se construye con los 30 de MLB (vía `0x807A8BE8(equipo)`) más `org01`–`org06` y `ml01` si están desbloqueados (`0x805C719C(0x2E..0x34)`) o son el estadio de casa.
    - El cursor se mueve dentro de la función de actualización: +1 o −1, dando la vuelta con el número de entradas.
    - `0x80CD3F6C` (`ShowStadium`) muestra la entrada tras cada movimiento:
      - pide una vez el pack de menú del estadio (`0x80EA23D8[número]`, marca en `+0x2941 + número`);
      - cuando está cargado lo dibuja con `0x807A8EBC` en los dos objetos de vista de `+0x2A18` (modo 1) y `+0x2A1C` (modo 2);
      - guarda el estadio mostrado en `+0x293C`.
  - **La otra pantalla** (vtable `0x80D3DE58`, constructor `0x805C6DC4`, lista en `+0x5250` y widget de lista en `+0x5538`) no es la de Exhibición: no se usa al elegir estadio en Exhibición (comprobado con un registro). Probablemente es la de Arrange o Temporada.
- **`0x807A8EBC(vista, modo, estadio)`:** dibuja las texturas del pack de menú del estadio.
  - Usa la tabla `0x80D99E38` (28 bytes por estadio: pack de menú y punteros a sus texturas).
  - Guarda en caché el último (modo, estadio) en `+0xDC`/`+0xE0`.
- **Imágenes del menú de estadios:** `ovl_main.rel` `+0x7C8814` (`0x80D3DD94`) guarda 37 números de archivo `env_stadium_menu01`…`36` y `100`. Lo usan funciones de `0x805C6DDC` a `0x805C8660` (pantalla de elección de estadio), con la base en `0x80D3DD90`.
- **`env_order_stadiumNN`:** `ovl_main.rel` `+0x7DCD0C` (`0x80D5228C`), entradas de 20 bytes {número de archivo, cuatro punteros dentro del pack}. Lo usan `0x80672F20` a `0x8067312C`.
- **Estadios nuevos** (hecho el 14 de octubre de 2026; parche `wiicompiled-new-stadiums`, `runtime/src/hle/mlb/new_stadiums.cpp`):
  - Un mod declara `[[stadium]]` en `mod.toml` y trae `project/stadium_<código>.pack` y `stadium_<código>tbl.pack`, y opcionalmente `env_stadium_menu_<código>.pack`.
  - El runtime da a esos archivos números libres desde 4388 (el mayor del juego es 4387) y los añade al último cubo del `project.bin` con mods.
  - **`ShowStadium` sustituida:**
    - pone los estadios nuevos en los huecos libres de la lista, como una ventana que se desplaza (pasar el último hueco muestra el siguiente), así que no hay límite;
    - el hueco lleva el número del estadio base, así que medidas, partidas y el resto del juego ven un estadio que conocen;
    - muestra el pack de menú del estadio nuevo cambiando por un momento la entrada de `0x80D99E38`.
  - **`GetStadiumFiles` sustituida:** devuelve los archivos del estadio nuevo cuando era el elegido y el partido es de Exhibición.
  - **Comprobado:**
    - en el carrusel de Exhibición, izquierda desde Fenway muestra la entrada nueva con su imagen;
    - al confirmarla, el partido carga «Coors Field 2» desde sus archivos 4389/4390;
    - con dos mods de estadio a la vez, el benchmark carga cualquiera de los dos (`WIICOMPILED_MATCH_BENCH_STADIUM=<código>`).
  - **Rótulo con el nombre** (hecho el 14 de octubre de 2026, `runtime/src/stadium_banner.cpp`):
    - el rótulo no sale del pack de menú del estadio, sino del pack común `env_stadium_menu.pack` (archivo `0x8E2`), a través de las tablas `0x80EA2490` (textura) y `0x80EA2528` (paleta);
    - la pantalla lo guarda en `+0x2A24` (textura) y `+0x2A28` (paleta) en el fotograma del movimiento, y el sprite lee esas casillas;
    - la tabla de dibujo `0x80D99E38` tiene, por estadio: el pack de menú, el rótulo (común `+4`, 512 × 64) con su paleta (común `+8`), la foto (512 × 256) con su paleta y «Home of…» (512 × 64) con su paleta. Estos dos últimos están en el pack del estadio;
    - resolutores: `0x8000C330(id, puntero)` da la textura y `0x8000C3E0` la paleta;
    - formato del rótulo del juego:
      - cabecera {u16 alto 64, u16 ancho 512, u32 formato 9 (C8), → píxeles, 0, 0, 1, 1}, con los píxeles en `+0x40` y los colores de la paleta detrás;
      - cabecera de la paleta: {256, 0, `0x81`, 0, `0x6000`, →};
      - el texto ocupa las filas 4 a 37;
    - un estadio nuevo usa `project/env_stadium_name_<código>.pack` si el mod lo trae. Si no, el runtime dibuja uno con una fuente del sistema (nunca con datos del juego) y lo guarda en la caché, `mods/banners/`;
    - en cada fotograma, la pantalla escribe ese rótulo en `+0x2A24`/`+0x2A28` mientras el estadio nuevo está a la vista;
    - **comprobado:** «Coors Field 2» y «Coors Field 3» salen con su rótulo propio (primera versión del dibujo). La versión actual, más parecida al estilo del juego, solo está vista decodificada.
  - **Pendiente:**
    - la línea «Home of the…» sigue siendo la del estadio base;
    - la otra pantalla de elección de estadio;
    - Temporada, Liga y MLB Life eligen estadio por equipo.
- **Lo que hacía falta (cumplido arriba salvo lo pendiente):**
  - poder añadir archivos nuevos a `project.bin`;
  - sustituir `GetStadiumFiles` y las funciones que leen las otras dos tablas por versiones nativas con entradas extra;
  - encontrar en la pantalla de elección el número de estadios (36 más «aleatorio»);
  - revisar si hay más tablas por estadio y si las partidas guardan el número de estadio.

### Pendiente

- los flags de la colocación (`0x500`, `0x1700`, `0x40000000`…);
- qué elige la mezcla aditiva de las piezas `*_ADD_*`;
- los bytes del texmap y del material que no varían;
- para qué usa el juego el árbol A (¿qué piezas se dibujan?) y qué decide `0x80CCB438`;
- el efecto en el juego de cada ID de superficie. Hipótesis: la 1 marca el home run, la 0 el foul a las gradas y la 2 el límite exterior. Se comprobará con el Polo Grounds.

### Herramientas

- `tools/stadium_re.py` (`info`, `check`, `stats`);
- el complemento de Blender `tools/blender/powerpros_stadium` (`docs/stadiums.md`).

## 7. Roster: `game_data.pack` y formato de jugador

- **`/game_data.pack`** (dentro de `project.bin`, 278.176 bytes): `PACKWII ` + cabecera de `0x20` + **bloque de roster de `0x43E80` bytes**.
  - Es el mismo bloque que guarda MLB Power Pros 2007 en su partida (`pm2maus.dat`, offset `0x68C74`).
  - **Todos los modos lo leen al arrancar**, MLB Life incluido. En 2008 el roster base no está en la partida guardada.
  - Lo cambiado en el modo Edit no llega a MLB Life. Observado: el juego deja fuera de MLB Life a los jugadores marcados como editados.

**Bloque de roster:**

| Offset | Contenido |
|---|---|
| `0x00000` | 1.500 jugadores × `0xB0` (2008 usa 1.007) |
| `0x40740` | 32 alineaciones × `0x48`: 18 entradas de 4 bytes (u16 jugador; nibble alto del byte 2 = posición) |
| `0x41040` | 200 agentes libres × 8 bytes (2008 usa 25) |
| `0x41680` | 32 equipos × `0x140` (30 franquicias + All-Star AL y NL): 40 entradas × 8 bytes |

- Entradas vacías y huecos sin jugador: llevan un registro "vacío" fijo, no ceros.
- Agentes libres: vacío = `00000000000f1400`; usado = id + `000000001400`.
- Los All-Star apuntan al mismo registro que el equipo normal.
- El número de jugador decide su hueco y su salario.

**Registro de jugador (`0xB0` bytes).** Los bits se cuentan desde el más significativo del inicio. Hay dos notaciones: `0x50.3` es byte `0x50`, bit 3; `0x283` sin punto es el número de bit (el mismo sitio: `0x283` = `0x50 × 8 + 3`). "`/ 9`" indica el ancho en bits.

| Campo | Posición |
|---|---|
| ID de jugador | u16 en `0x00` (0 = hueco vacío) |
| ID de nombre especial | u16 en `0x02`; `≥ 0xF00` = nombre largo, y el juego no lee `0x04`-`0x15` |
| Apellido / nombre | `0x16` y `0x32`, 14 caracteres u16 cada uno (tabla de caracteres de 2007) |
| Necesarios para la alineación | bits `0x4E.1`, `0x4E.2`, `0x4F.0`, `0x92.0` = 1 en todo jugador de 2008. Sin ellos, la pantalla de alineación sale vacía y todos como "P" |
| No editado / editado | bits `0x50.3` = 1 y `0x50.4` = 0 en los jugadores originales |
| Cara | bit `0x2A4`, 8 bits |
| Piel y ojos | bit `0x2AC`, 4 bits |
| Forma de bateo / lanzamiento | `0x2E4` / 9 y `0x2ED` / 9 (eran 8 en 2007) |
| Posición principal, capacidad P/C | `0x2F6` / 4, `0x2FA` / 3, `0x2FD` / 3 |
| Roles de lanzador (abridor, relevo, cerrador) | `0x62.5`, `0x63.0`, `0x63.3`, 3 bits cada uno |
| Fecha de nacimiento | año `0x414` / 11 (2008 = año de 2007 + 1), mes `0x420` / 4, día `0x424` / 5 |
| Años en las mayores, AVG, RBI, HR | `0x42A` / 5, `0x42F` / 10, `0x440` / 10, `0x44A` / 10 |
| ERA | `0x460` / 14 |
| Voz | bit `0x492`, 14 bits (misma posición que 2007, numeración nueva; ver "Voces renumeradas") |
| Repertorio | `0x4A0`-`0x4FF`: 12 lanzamientos de 5 bits de tipo + 3 de movimiento |

**Cambios respecto a 2007:**
- Los bytes `0x00`-`0x5B` y `0x60`-`0x6E` son iguales, salvo la cara.
- El segundo campo "contra zurdos" (`0x6F.5`) desaparece: los bits de 2007 de `0x380` a `0x3BE` se mueven 3 posiciones atrás.
- 2008 añade un bit en `0x77.4`: los bits de `0x3BF` a `0x41A` se mueven 2 atrás.
- La frontera −3/−2 está en `0x77.7` (bateador de tirón).
- Habilidades confirmadas con jugadores reales:
  - "push hitter" `0x71.0` (Ichiro, Mauer, Helton…)
  - "free swinger" `0x71.7` (Soriano, Guillén…)
  - "pull hitter" `0x77.7` (42 jugadores: Giambi, A-Rod…)
- **Caras renumeradas:** +0 / +1 / +2 / +7 / +12 para las caras de 2007 por debajo de 28 / 106 / 161 / 177 / resto. Coinciden 707 de 729 jugadores compartidos.
- **Piel y ojos:** 2008 inserta un tono en la posición 2, y los ojos marrones suman 6 (en 2007, 5).
- **Nombres largos nuevos** (IDs especiales): Van Benschoten 3867, Pignatiello 3893, E. Patterson 3895, Saltalamacchia 3896, Sonnanstine 3897, Chamberlain 3899, S. Rodriguez 3900, Lillibridge 3901.
- La tabla completa de unos 140 campos está en `kRosterFields` (`wiicompiled-mods.patch`) y en `PowerPros2008Fields.cs` (PowerUp+ parcheado). Los 127 bits que PowerUp no nombra (incluido el rasgo oculto) están en `kRosterUnnamedRuns`, en tramos.

**Getters del registro en el código (desensamblado).** El juego lee cada campo con una función pequeña: `lwz rX, D(r3)` + extracción del campo + `blr`, una tras otra desde `0x8082B000` (`ovl_main.rel` + `0x2B5A80`) en el orden del registro. Es la lista de campos tal como la ve el juego, con posición, ancho y signo:
- Coinciden con la tabla de arriba la forma de bateo y la de lanzamiento (9 bits), clutch `0x6F.5`/3, ánimo `0x70.1`/2, todas las habilidades de bateo de `0x70.3` a `0x73.0` (push hitter `0x71.0`, free swinger `0x71.7`…), las de defensa y las de lanzador hasta `0x80.7`, y el nacimiento.
- **"Table setter" `0x70.0` sigue vivo en 2008:** getter `0x8082B068` (`srwi 31`), setter del modo Edit en `0x8085EF74` (limita a 0–1) y dos funciones de juego que lo leen junto a "rally hitter": `0x80985E4C` y `0x80A30C54`. Ningún jugador de 2008 lo tiene, pero funciona si se le da a uno.
- Algunos campos que PowerUp trata como un solo bit son de **2 bits** para el juego. El otro bit viaja como "bit sin nombre", así que el conversor conserva el valor completo:
  - `0x73.5`/2 (deslizarse de cabeza): solo aparecen 0 y 2; el bit que modela PowerUp es el correcto.
  - `0x75.4`/2 con signo ("cannon arm" para PowerUp): +1 en 96 jugadores (Carpenter, Randy Johnson, Dye…) y −1 en 11 (Furcal, Rollins, Eckstein…). No aparece en la ficha del jugador: es un rasgo oculto.
  - `0x77.0`/2 y `0x77.2`/2 con signo (corredor y defensor agresivos): solo 0 y +1.
  - `0x78.4`/2 con signo: **habilidad nueva de 2008, partido nocturno** (Gd/Pr Night Game). +1 en 11 (Figgins, Cora, Easley…), −1 en 16 (Giambi, Kendall, Dellucci…).

**Comprobado en pantalla** (Arrange → un equipo → Fielder Select → stick C, "Player ABL"; capturas de Cora, Varitek, Casey, J.D. Drew, Eckstein y Rios cruzadas con sus bits):

| Bits de 2008 | Lo que muestra el juego | En PowerUp |
|---|---|---|
| `0x73.5`/2 | HD 1st Slide (valor 2) | WillSlideHeadFirst, bien |
| `0x73.7` | Tough Runner | IsToughRunner, bien |
| `0x74.4` | **Cannon Arm** (en los mensajes de MLB Life, "Laser Beam") | lo llama CanBarehandCatch (corregido para 2008) |
| `0x75.2` | Gd Block | IsGoodBlocker, bien |
| `0x75.4`/2 con signo | **nada en ninguna pestaña**: rasgo oculto. +1 en pitchers potentes y bateadores corpulentos, −1 en infielders pequeños; podría ser la complexión (sin confirmar) | **mal:** lo llama HasCannonArm |
| `0x76.2`/2 | Slugger (+1) / Slap Hitter (−1) | SlugOrSlap, bien |
| `0x76.4`/2 | Aggressive (+1) / Patient Hitter (−1) | bien |
| `0x76.6`/2 | Aggressive (+1) / Cautious Stealer (−1) | bien |
| `0x77.0`/2 | Aggressive Runner (+1) | IsAggressiveBaserunner, bien |
| `0x77.2`/2 | Aggressive Fielder (+1) | IsAggressiveFielder, bien |
| `0x77.4` | **Gd Eye**, bit nuevo de 2008 | no existe |
| `0x77.5` | **Barehand Catch** | sin nombre (Bit_77_7); en 2008 lo recibe CanBarehandCatch |
| `0x78.0`/2, `0x78.2`/2 | Gd/Pr Day Game, Gd/Pr Rain | bien |
| `0x78.4`/2 | **Gd (+1) / Pr (−1) Night Game**, nueva de 2008 | no existe |

- Los mismos jugadores tienen estos bits en 2007, así que en 2007 PowerUp también se equivoca de nombre.
- **Para 2008 se corrige por significado, y 2007 se deja como está.** Los rosters que genera PowerUp ya usan las casillas por su nombre (su roster 2025 marca "Cannon arm" a Tatis, Arenado, Salvador Pérez y Betts). Por eso la tabla de 2008 (`MOVED` en `powerup_to_2008.py`) lleva "Barehand catch" a `0x77.5` y "Cannon arm" a `0x74.4`. El rasgo oculto se conserva del registro de 2008, o vale 0 en un jugador nuevo, y los restos de la partida base de 2007 ya no se copian.
- Resultado con el roster 2025: antes, 0 brazos cañón, 117 barehand al azar y 100 rasgos ocultos basura; ahora, los 4 brazos cañón reales y nada más.
- En PowerUp+, un roster de 2008 importado de nuevo muestra cada casilla con su habilidad real (Rios: Cannon Arm; Eckstein: Barehand). La ida y vuelta sigue idéntica byte a byte. Los rosters de 2008 importados antes de este cambio hay que importarlos de nuevo.
- **Gd Eye y Night Game se editan en PowerUp+** (sección General, marcadas "(2008)"). Una importación de 2008 los lee del registro original.
  - La exportación de 2008 los escribe en dos huecos del formato de 2007 que la conversión ya no usa (`MOD_2008`: `0x77.7` → `0x77.4` y `0x78.6`/2 → `0x78.4`/2).
  - El conversor del juego solo los toma de un mod con `roster_ids = "2008"`. Un `.dat` genérico de 2007 se convierte igual que antes.
  - La exportación de 2007 los deja vacíos, así que una partida de 2007 se escribe bit a bit como antes.
- Comprobado: Payton (Pr Night Game) cambiado en el editor a Gd Night Game y Gd Eye, guardado, exportado y convertido. En el roster del juego cambian exactamente esos bits; el runtime da lo mismo que Python.
- Al copiar un jugador (necesario para editar uno importado), PowerUp no copiaba Day Game, Rain, la fecha de nacimiento, la edad, los años en las mayores ni las estadísticas. Sin fecha, la exportación fallaba. Ahora se copian.
- La ficha se abre en el juego desde Arrange → crear equipo → Fielder Select (o Pitcher Select) → stick C. Para automatizarlo, `WIICOMPILED_PAD_SCRIPT` acepta los bits `0x10000`–`0x80000` como stick C (parche `wiicompiled-pad-script-cstick`).

**Voces renumeradas.** 2008 conserva el orden de las voces de 2007, pero inserta y quita algunas. Entre dos jugadores presentes en ambos juegos cuyos huecos miden lo mismo, las voces se corresponden una a una, salvo que un nombre conocido de 2008 lo contradiga. Así se ubican 810 de las 968 voces de PowerUp, en 206 tramos (`VOICE_RUNS`). Ocultando el 10 % de los jugadores de referencia, los 739 casos caen en su voz real. Ryan Braun y dos "Great" tienen su voz regrabada en otro sitio.

Fuentes: `docs/roster-2008.md`, `tools/roster/powerup_to_2008.py`, `tools/roster/align_2007_2008.py --voices`, `wiicompiled-mods.patch`, `wiicompiled-roster-flags.patch`, `wiicompiled-roster-voices.patch`, desensamblado de `ovl_main.rel`.

## 8. Escenas, menús y preparación de un partido

**Gestor de escenas** (puntero en `0x81033268`; objeto con vtable `0x80D4EB88`):

| Offset | Campo |
|---|---|
| `+0x400` | objeto de la escena actual (su fase en `+0x38`, un campo de control en `+0x48`) |
| `+0x404` | ID de la escena siguiente |
| `+0x408` | ID de la escena actual |
| `+0x410` | u8: transición en curso |
| `+0x413` | u8: confirmar el cambio |

**IDs de escena conocidos:**

| ID | Escena |
|---|---|
| 1, 2 | pantallas de arranque y correa (fases 5 y 6 esperan una pulsación) |
| 3 | vídeo de apertura (`+0x48 = 3` lo cierra limpiamente) |
| 4 | pantalla de título ("Press +"; sin pulsar nada arranca la demo CPU contra CPU) |
| 409 | el campo: partido en juego |
| 419 | escena previa al arranque (aparece en los registros antes de la 1) |

**Preparar un partido desde código** (como hace el benchmark: Yankees contra Colorado en Coors Field, CPU contra CPU):
1. Escribir `0x4D4C4208` ("MLB\x08") en `r13-32744`.
2. Llamar a `0x80C8BAF0`, el inicializador de partidos del juego.
3. `config = *(0x8104E1D8)`, `common = *(0x8103D118)`.
4. Equipos: `config+19 = 3` (Yankees), `config+20 = 27` (Rockies).
5. Para cada lado (0, 1): escribir el ID del equipo en `common+1752+lado·2204` y `common+1753+lado·2204`, y llamar a `0x80973828(lado, equipo)`. Cada lado ocupa 2.204 (`0x89C`) bytes.
6. `0x807A8BE8(27)` devuelve el estadio del equipo (Coors Field); se escribe en `config+31` y `common+4`.
7. `config+18 = 0`, `config+13 = 0` (Exhibition), `config+14 = 0` (ningún mando humano), `config+26 = 3`, `config+27 = 3`, `common+976 = 0` (u16), `common+978 = 0` (u8).
8. Siguiente escena = 409 y `+0x413 = 1`.

**Puntos de código ligados a menús:**
- **Home Run (Practice):** `0x8057BEEC`, tabla de saltos de 10 entradas acotada con `cmplwi r3, 9`.
- **Vista de fildeo:** `0x8092F450`, tabla de 9 entradas indexada por un byte, sin comparación previa.
- **Exhibition → LINEUP:** `0x80663264` llama por `bctrl` a `0x80664C30`.
  - En `0x80664C18` hay cinco funciones seguidas que dtk tomó por una sola. `C20`, `C28` y `C30` son getters de dos instrucciones llamados solo desde la vtable `0x80D5183C`.
  - Quedan unos 450 candidatos parecidos sin revisar.
- **Mandos por modo:** Exhibition, Home Run y otros modos piden Nunchuk o mando Clásico. Sin Nunchuk se ofrece "Power Pros via Wii Remote".

**Modo Temporada, recorrido con el mando** (19 de octubre de 2026; mando de GameCube por guion, `WIICOMPILED_PAD_SCRIPT`):
- **Arranque hasta el menú principal:** aviso de la correa (dos A) → «Loading data» → logos de Konami, 2K Sports, licencias y Power Pros Production (se saltan con Start) → título hacia los 32 s → pantalla de mandos («You can use the following Controllers»), que espera una pulsación → menú principal.
- **Menú principal:** 12 opciones en 3 filas: Exhibition, Success, Home Run, Practice / MLB Life, Season, League, Arrange / Wii Remote, Shop, Baseball Cards, My Data. El cursor empieza en Exhibition. Al cambiar de mando sale «1P Nintendo GameCube Controller detected».
- **Pantallas del modo Temporada** (entrar, temporada nueva, menú, MLB Data): en [Modo Temporada › Pantallas y menús](#pantallas-y-menús).
- **Para los guiones:** los tiempos varían de un arranque a otro (la charla de presentación empezó entre los 84 y los 160 s), así que un guion de tiempos fijos se descuadra; mejor pulsar L repetidamente hasta pasar la charla.

Fuentes: `wiicompiled-match-benchmark.patch` (`match_benchmark.cpp`), `docs/wiicompiled-feasibility.md`.

## 9. Modo Temporada

Todo lo encontrado sobre el modo Temporada (Season): fecha y año, calendario de partidos, paso de año y número de temporadas, tablas por año y la partida guardada. Investigado del 19 al 20 de octubre de 2026 con el código traducido, la sonda del runtime `wiicompiled-season-probe` (fija la fecha y el año, pulsa botones y vuelca la memoria) y partidas de prueba en un mod aparte.

- **Notación:** P = `[0x8104B5AC]`, X = `[0x8104B5A0]`, Q = `[0x8104B59C]` + 1344. Son punteros del BSS del REL al bloque de datos del modo; X + n es una posición dentro de ese bloque.
- **Herramientas:** `tools/season_save.py` (compara partidas y volcados), `tools/season_cap_sites.py` (busca en el código las comparaciones y sumas con el año).

### Pantallas y menús

- **Entrar en Season:** pantalla negra unos segundos y una ventana de ayuda («Starting Play», se cierra con B). Al entrar, el juego intenta abrir `pm3usse0.dat`, `pm3usse1.dat` y `pm3usse2.dat` en la carpeta de partidas: en una partida nueva no existen. Parecen tres huecos de partida del modo Temporada, aparte de `pm3usma.dat`.
- **Pantallas de una temporada nueva:** From Beginning / Continue → Quick Start / Start → elegir equipo (30 equipos, con estadísticas de 2007 y salario) → opciones (Game Setup, Controller Setup, Season Settings) → aviso de que mejoras, agentes libres, draft y traspasos solo se cambian en ciertos momentos de la pretemporada → charla de presentación con la secretaria (A avanza, L salta, R historial de la charla). Empieza el lunes 24 de marzo de 2008.
- **Menú del modo Temporada:** a la izquierda, siete pastillas: Daily Progress, Practice, Management, Commands, MLB Data, System, Exit; a la derecha, la clasificación de la división. Arriba, el equipo, la fecha, jugadores en MLB y AAA, puntos del dueño y salario total. Abajo, la barra de ayudas: Start (ayuda), Y (calendario), R (historial), Z (cambiar información).
- **MLB Data:** pestañas «◄ 2008 ►» (el año, que se cambia con L/R), All-Time/Career y Honors. Dentro de un año: Player Stats (Season Stats, Player Records), Team Stats (Team Stats, Team Records), Schedule (Standings, Matchup Chart, Postseason) y Roster (Player List, Transactions).

### Estado, fecha y año

- **Estado:** `[0x8104B5A8]` apunta al estado de la temporada. En +4 está el día de hoy (u16, día del año desde el 1 de enero: 0x54 = 25/3), en +6 el día anterior y en +8 el **índice del año** (byte con signo; año = 2008 + índice). `0x80B74BCC` lee el índice, `0x80B4A888` devuelve el año, `0x80B74BA4` lo fija y `0x80B74BB4` le suma 1.
- **Modo Temporada o MLB Life:** los dos ponen `[0x8104B5A8]` y `[0x8104B59C]` en su propio bloque. Los distingue el primer número de jugador, el u16 de `[0x8104B608]`: 7001 en Temporada (`0x80C48344`) y 9001 en MLB Life (`0x80A556E0`). El número de temporadas lo lee además `0x80C0C50C` (con `0x80BA0878`, hacia `[0x8104BA2C]`) al entrar en el modo, antes de que se escriba el 7001.
- **Tramos del año:** tramos de días en `0x80D2A700` (8 bytes por tramo: primer y último día del año). Los tramos 17 a 28 son los meses; `0x80B4AA38` devuelve el mes de hoy. Tramos de la temporada: regular 19/3–1/10, postemporada 2/10–31/10, pretemporada 1/11–14/2. Días de eventos en `0x80D2A7F0` (u32 por evento; el 57 es el 1/11).
- **Día de la semana:** `0x80B4DADC` lo calcula con una fórmula a partir del año, el mes y el día, así que también vale para años después de 2017.
- **El año en pantalla y el calendario:** `0x80B4A888` (2008 + índice) se llama siempre (53 sitios). Suman 2008 o 2007 al índice sin llamarla `0x80B4A730` (año de una fecha, como mínimo 2008), `0x8087B668`, `0x8087E224` y `0x80ADC620` (2008 + el año elegido en un selector). El calendario usa el año de dos cifras, índice + 8 (`0x80B74C00`), que el traductor copiaba dentro de 32 funciones; en diez de ellas (`0x80B4AEE4` a `0x80B4CB84`) se limita a 8..28 antes de la fórmula del día de la semana (`li 28` y `cmpwi 28`), y ese límite solo protege la fórmula. Una función reemplazada en el runtime deja de copiarse dentro de otras (el traductor no copia lo que tiene sustituto nativo).
- **Fechas de eventos y año del calendario** (20 de octubre de 2026): las fechas de los eventos de la temporada (`0x80BAFB8C`, `0x80BAFCC4`, `0x80BAFE40`, `0x80BAFFE8`, y `0x80A38494`) usan la temporada de una fecha (`0x80B4A7DC`) + 8, sin pasar por `0x80B74C00`: 13 sitios más del año (`tools/season_cap_sites.py --years` los busca: `addi r3, 8/2007/2008` justo después de una llamada que da el índice). El título del calendario (Y) es una imagen: `0x80B9D450` (pantalla, índice, mes) pone la del año de una tabla de 20 (`0x80E8EB40`, 2008–2027) y la del mes (`0x80E8EB90`) con `0x8001DFB8` (panel, 1696 = `season_schedule.pack`, imagen). Cada imagen es un `SURF` de `0x48` bytes en el pack, de 2007 a 2028 seguidas, con su rectángulo de 86 × 37 en un atlas C8 de 512 × 512. El pack se carga en MEM2 al abrir el calendario. Con años archivados, un reemplazo nativo pone la imagen del año real hasta 2028 y, pasado 2028, repinta la de 2007 (que el modo Temporada no enseña) con el año compuesto de las imágenes del juego por celdas: «20» de cualquier año, la cifra de las unidades de 2010 + d y la misma cifra una celda (19 columnas) a la izquierda para las decenas. Probado: abril y mayo de 2029, marzo de 2030, y marzo de 2008 con 10 temporadas sin cambios. Se encontró con un watchpoint de lectura sobre el índice al abrir el calendario (única lectura nueva: `0x80B9C034`). Las tablas de años de `0x80D29D18` (1998–2028) y `0x80E65DC4` no son el título.

### Calendario de partidos

- **Calendario de partidos** (20 de octubre de 2026; código traducido y volcados de memoria y partidas): tabla de 245 días × 16 huecos de 16 bytes en X + 10192 (62.720 bytes), que va dentro de la partida (archivo 0x2B8C8). El día 0 es el 1 de marzo (día del año 60). La casilla la da `0x80BA0444` (base, día del año, hueco): base + 10192 + (día − 60) × 256 + hueco × 16, o 0 fuera de los 245 días. Cada partido: u32 local, u32 visitante, u32 hora de inicio en hora del Este (horas × 400 + minutos × 4: 13:05 = 0x1464; al jugarse guarda otros datos), u32 banderas (`0x8000` = nocturno: lo llevan todos los que empiezan a partir de las 18:00 y ninguno antes; en 2008 el resto vale `0x00A06700`). Hueco vacío: equipos 44 y 44. El All-Star de 2008 (15/7, día 136) enfrenta a los equipos 33 y 37.
  - **Números de equipo** (por división, en el orden de la clasificación final de 2007; sale al comparar los días de cada equipo con el calendario real de 2008, coincidencia de 0,98 a 1,0 en los 30): AL Este 0 BOS, 1 NYY, 2 TOR, 3 BAL, 4 TB; AL Central 5 CLE, 6 DET, 7 MIN, 8 CWS, 9 KC; AL Oeste 10 LAA, 11 SEA, 12 OAK, 13 TEX; NL Este 14 PHI, 15 NYM, 16 ATL, 17 WSH, 18 FLA; NL Central 19 CHC, 20 MIL, 21 STL, 22 HOU, 23 CIN, 24 PIT; NL Oeste 25 ARI, 26 COL, 27 SD, 28 LAD, 29 SF.
  - **2008 es el calendario oficial:** 2.431 partidos (162 por equipo y el All-Star): el calendario publicado antes de la temporada, sin los cambios por aplazamientos (212 de los 245 días tienen el mismo número de partidos que el calendario jugado; los demás difieren por partidos aplazados). Empieza con Athletics–Red Sox en Tokio el 25 y el 26/3 (12 y 0; Oakland era el local designado) y Nationals–Braves el 30/3 (17 y 16).
  - **Los años siguientes los genera el juego:** al pasar al 1/1, `0x80B4E564` llama a `0x80B97D68`, que vacía las 245 × 16 casillas (`0x80BA0DAC`). `0x80B97DC4` monta el calendario: vacía, consulta `0x80B97E08` y, si da 1, llama a `0x80B97EA4` (carga el calendario oficial desde datos del REL en `0x80E8E8B8`, que no están en bruto) y si da 0 a `0x80B98320` (el generador, que usa los días de la semana); luego llama a `0x80B98294`, `0x80B73E2C` y `0x80B9890C`. `0x80B97E08` solo da 1 con el índice del año en 0 y las seis divisiones iguales a la tabla de `0x80D29060`: el calendario oficial solo vale para 2008 con las divisiones originales (en una partida nueva de 2008 se carga el oficial, comprobado con el registro del parche de abajo). Los calendarios generados tienen 2.491 partidos (166 por equipo; unos 60 llevan otras banderas, `0x00A40000` y parecidas) y cambian cada año (2009 y 2027 solo comparten 7 emparejamientos). Si se salta con la fecha el día en que se genera, el año se queda sin calendario (partidas de prueba de 2017 a 2020 con 0 partidos).
- **Partidos de exhibición de marzo:** no van en esta tabla. El calendario (Y) los marca como «EXHIBITION» los días 1, 4, 13 y 18 de marzo de 2008 aunque la tabla esté vacía esos días; salen de los eventos de la temporada.
- **Calendario desde un mod** (20 de octubre de 2026; parche `wiicompiled-season-schedule`, `runtime/src/hle/mlb/season_schedule.cpp`, formato en `docs/mods.md`): una sustitución nativa de `0x80B97DC4` hace las mismas llamadas y, entre el relleno y `0x80B98294`, cambia los partidos por los de `schedule/<año>.csv` del primer mod activo que lo tenga. Conserva el All-Star del juego en su día y las casillas vacías tal como las dejó el juego. Los partidos nuevos llevan las banderas de 2008 (`0x00A06700`, más `0x8000` desde las 18:00).
  - **Comprobado en el juego:** temporada nueva de 2008 con el calendario de 2026 como `2008.csv` (`tools/season_schedule.py`). El 25/3 se juega Giants–Yankees en AT&T Park (el partido inaugural de 2026), «Game Results» de ese día solo trae ese partido, y en el calendario de marzo los Yankees juegan el 27 y 28 en San Francisco y el 30 y 31 en Seattle, como en 2026. Registro: `[season] schedule 2008: 2429 games from mod "…" replace the official one (1 dropped: more than 16 on a day)`; el que no cabe es del 25/9/2026, con 17 partidos.
  - **La herramienta reproduce el calendario del juego:** con la respuesta de la API de MLB para 2008, `tools/season_schedule.py` da 2.429 de los 2.430 partidos de la tabla oficial en el mismo día (uno se movió un día en la versión publicada; el CSV trae además el desempate White Sox–Twins) y la misma hora en 2.249.

### Paso de año y número de temporadas

- **Paso de año:** `0x80B4E564` se llama al avanzar de día; si el día nuevo cae en enero, suma 1 al índice y prepara la temporada nueva (`0x80B962E8`, `0x80B71B1C`, `0x80B97D68`). Se puede provocar sin jugar: desde el menú de pretemporada, día = 25/12 y «Daily Progress» lleva al 1/1 del año siguiente («These are last year's financials»). Comprobado de 2008 a 2009 y de 2009 a 2010.
- **El tope de 10 temporadas es una variable:** `[0x8104BA2C]` = 10. La leen `0x80B40580`, `0x80B4065C` (temporadas que quedan, en pantalla), `0x80B4DB78`, `0x80B50948` y `0x80B5C4D4`, que comparan el índice con 10 − 1. Además hay comparaciones fijas con 9 o 10: `0x80BB4A40` (en 9, mensaje 141), `0x80BF24B4`, dos en `0x80BF4BD4` (mensaje 140), `0x80C0CB54`, `0x80BECF40` (lista de años) y `0x80C0D4F8` (bucle de 10 años de las finanzas).
- **Las comparaciones fijas, instrucción por instrucción** (`tools/season_cap_sites.py`, que busca `bl 0x80B74BCC` seguido de `cmpwi` con 9 o 10 en el código cargado): `0x80BB4A6C` (índice == 9, mensaje 141), `0x80BF2530` (índice < 9, llama a `0x80BF619C`), `0x80BF51BC` y `0x80BF52D0` (eventos de pretemporada si índice < 9), `0x80C0CC0C` (índice < 10). Además hay dos bucles de 10 años con un contador: `0x80BECF8C` recorre una lista de interfaz con 10 huecos (`obj + 11184`, 4 bytes cada uno; no se puede ampliar sin más) y `0x80C0D584` inicializa los 10 años de finanzas.
- **Final de la temporada 10** (probado con el índice puesto en 9 y la fecha en 25/12): al pasar al 1/1 el índice llega a 10 (en pantalla 2018), sale la despedida («Take care, now»), el resumen de clasificaciones, premios y postemporada (vacío, porque lee el hueco del año 10), los créditos, «Save as Arrange Team» y se guarda. Al cargar esa partida se repite el final. Ni con 10 ni con 20 llega antes: el 1/11 de 2017 (índice 9) solo trae los premios y las retiradas, y el final sale con el paso al 1/1 (probado).
- **Partida terminada:** el bit 0x20 de `[estado + 0x104]` marca que el modo acabó; una partida con él repite el final al cargarla (comprobado poniéndolo en una partida de 2019). `0x80C0D438` lo lee, pero la comprobación al cargar va por otro camino. La partida lleva suma de control: cambiar ese byte en el archivo la deja «corrupted».
- **Hasta 20 temporadas** (parche `wiicompiled-season-cap`, opción del launcher): `0x80C06534` (devuelve 10) da el número de temporadas, del que sale `[0x8104BA2C]`; las cinco comparaciones fijas pasan por el runtime (`translation.immediate_overrides` del traductor); las funciones de finanzas por año (`0x80C0D614`, `0x80C0D658`, `0x80C0D6D0`, `0x80C0D758`, `0x80C0D818`, `0x80C0D85C`) se reemplazan y guardan los años 11 a 20 aparte; la marca de terminado de un final anterior se borra en memoria una sola vez, justo después de cargar una partida de Temporada. El número de temporadas se fija al empezar o cargar (opción, o la partida ya jugada con 20: lleva la cola propia o pasa del índice 10), nunca desde el índice en vivo, que pasa por 10 camino del final original. Con 10, las finanzas van siempre a las direcciones del original. Probado: 2016 → 2017 → 2018 sin final, menú y días de la semana de 2018 y 2019, final tras la temporada 20, partida terminada del original seguida en 2018, y con 10 todo igual que antes.
- **Sin límite de temporadas** (opción «Sin límite», `[season] max_seasons = 0`): el juego sigue con sus 20 años de tablas. Al pasar al 1/1 después de la temporada 20, `0x80B74BB4` (índice + 1, llamado por `0x80B4E564`) no suma: todas las tablas por año bajan un año, el año más antiguo se guarda en la cola de la partida (caben unos 34; luego se descarta el más viejo) y un desfase de años, también en la cola, hace que `0x80B4A888`, `0x80B74C00` y los sitios de año den el año real. Los días de la semana salen del año real (probado: sábado 1/1/2028, lunes 1/1/2029, sábado 8/1/2028). El hueco nuevo de cada tabla queda como un año sin jugar: clasificados a 44, series sin equipos ni victorias, la fila 20 de las clasificaciones (que nunca se juega), el equipo del año que sale, y finanzas y registros a cero. «Annual Results» muestra los 10 últimos años; las imágenes de título solo existen para 2008–2017 y los demás años salen sin título.
- **Una temporada simulada con «Sin límite»** (2029, desde la partida de 2027 + 2): exhibiciones con Fast Game (pausa > FAST GAME > Z, unos 12 s por partido), temporada regular con el calendario (Y, elegir el último día del mes, «Skip to the end of …?»: un mes en unos 20 s), postemporada y Serie Mundial con Fast Game. Bien: días de la semana, MVP semanales, votaciones del All-Star, draft, clasificación final (162 partidos), cuadro de postemporada, informe del 1/1/2030 (ingresos 114.461 P, saldo +37.281 P, impuesto 0), «Annual Results» de 2029 después del desplazamiento (2010 archivado), y guardar y cargar en 2030 (desfase 3, 3 años archivados).

- **Año inicial de un paquete de temporada** (20 de octubre de 2026; parche `wiicompiled-season-start-year`, formato en `docs/mods.md`, «Paquetes de temporada»): `start_year` en el `mod.toml` de un mod activo. Al montar el calendario con el índice 0 (partida nueva), la sustitución de `0x80B97DC4` fija un desfase de años `start_year` − 2008 que se suma al de «Sin límite» en todo lo que muestra el año (`0x80B4A888`, `0x80B74C00`, los sitios del año y las imágenes del calendario); el umbral del impuesto de lujo sigue el índice. Va en la cola de la partida, versión 3 («PPSEASON», u32 3, desfase de «Sin límite», desfase inicial, u32 banderas con 1 = pasada la temporada 10, y lo de la versión 2); la versión 2 se sigue leyendo.
  - **Jugadores de la partida:** el modo Temporada tiene su propia copia de los jugadores, 1.500 registros de `0xB0` en `[0x8104B59C]` + `0x3564` (números 7001 a 8500; en la partida, 1.209), en el mismo sitio en todos los volcados de 2008 a 2028. La edad sale del año que se ve y la fecha de nacimiento (`0x80B861B0`: `0x80B4A888`, y el año de nacimiento, 11 bits desde el bit 1 del word `+0x80`, que lee `0x8082B544`; luego `0x80B05E2C` resta y ajusta por mes y día). Por eso, al empezar en otro año, el año de nacimiento de esos jugadores se adelanta lo mismo.
  - **Comprobado en el juego:** con `start_year = 2026` y el calendario de 2026, la temporada nueva empieza el «TUESDAY 3/24/2026», el menú del 25/3 dice «WEDNESDAY» y el rival de hoy, Giants a las 8:05 PM. Max Fried (31 años en el roster de prueba, hecho para 2025) sale nacido el 18/1/1995. Guardada, cerrado el juego y vuelta a cargar, sigue en 2026 (`[season] season 1 loaded; our tail, year offset 0, start shift 18`), la lista de partidas también muestra 2026, y una partida antigua de 2018 sin cola carga en 2018 sin heredar el desfase.

### Datos por año

- **Datos por año del modo Temporada** (P = `[0x8104B5AC]`, X = `[0x8104B5A0]`, Q = `[0x8104B59C]` + 1344; confirmados en volcados de memoria):

  | Datos | Dónde | Por año | Sitio |
  |---|---|---|---|
  | Clasificados de postemporada (8 equipos; 44 = vacío) | P + año × 32 | 32 bytes | 20 años |
  | Series de postemporada (8 series de 20 bytes) | P + 648 + año × 160 | 160 bytes | 20 años |
  | Clasificación de las ligas | X + 0x11CD0 + año × 336 | 336 bytes | 21 años |
  | Un equipo por año, sorteado al crear la partida | X + 0x159A0 + año × 2 | 2 bytes | 20 años |
  | Valor anual de finanzas | Q + 4 + año × 4 | 4 bytes | **10 años** |
  | Resumen anual de finanzas (10 conceptos y saldo) | Q + 44 + año × 44 | 44 bytes | **10 años** (detrás va la tabla mensual) |
  | Registros de los equipos (8 × 16 bytes) | `[0x8104B59C]` + 60 + año × 128 | 128 bytes | **10 años** (detrás van las finanzas) |

  Casi todo tiene sitio para 20 años; solo las finanzas y los registros de los equipos tienen 10. Con el índice en 10, `0x80C0D658` escribe en el saldo del primer mes. El significado exacto de algunos campos es una suposición: las partidas probadas no tienen resultados jugados.
- **Registros de los equipos por año, otra tabla de 10 años** (20 de octubre de 2026): `[0x8104B59C]` + 60 + año × 128 + k × 16 (8 registros de 16 bytes con campos de bits por año), justo antes de las finanzas (`+ 1344`). La dirección la da `0x80C41BA8` (año, k); escribe `0x80C417A8` (lo llama `0x80C342B4`, desde `0x80BB4A40`, con el índice) y leen doce funciones (`0x80C41800` a `0x80C41B5C`, enteros o multiplicados por el `float` de `0x80D2F7D0`), que usa la pantalla con selector de año `0x80ADC620`. Al crear la partida los 10 años vienen rellenos. Con 20 temporadas, desde la 11 se escribía encima de las finanzas: ahora los años 11 a 20 van aparte, como las finanzas. `0x80BB4A40` no se disparó con los saltos de fecha probados (31/10, 25/12, febrero–marzo).
- **Impuesto de lujo, una tabla fija de 10 años** (20 de octubre de 2026): `0x80C4764C` calcula el impuesto del mes: (nómina, `0x80C475A4`) − (umbral del año), por una tasa de `0x80D2F9E0` (0,225, 0,30 o 0,40 según `0x80C41CFC`, de 0 a 2; parece el número de años seguidos por encima); `0x80C47720` lo apunta como gasto 9 («Luxury Tax»). El umbral lo da la hoja `0x80C4762C` con la temporada de hoy (`0x80B4A7DC`) de una tabla de 10 en el ejecutable, `0x80D2F9B8`: 155.000, 162.000, 170.000 y luego +8.000 por año hasta 226.000 (2017). Pasado 2017 el juego lee lo que sigue a la tabla (las tasas como enteros, luego otros datos): sin impuesto en 2018–2020 y, desde 2021, un umbral de 0 o negativo. Probado: el informe del 1/2/2029 restaba 480.304.640 P de impuesto. Con más de 10 temporadas el umbral sigue con el último paso de la tabla (+8.000 por año); con 10, igual que el original. También la leen `0x80BC3C3C` y `0x80C4113C` (dos veces). Un barrido de las lecturas de tablas fijas con el índice del año (`0x80B74BCC`, `0x80B4A7DC`, `0x80B4A888`, `0x80B74C00`) no encontró otra.
- **«Annual Results» (Management):** la monta `0x80BECE70`, con 10 páginas (`[pantalla + 11184]`, una por año) y el título de cada año como imagen (tabla de 10 en `0x80E98850`, de 2008 a 2017, que pone `0x80C10424`). `0x80BECF40` rellena los años pasados y pone el cursor en la página del índice, que pasada la temporada 10 cae fuera de las 10 (el juego se colgaba). `0x80BECFD0` cambia de página. «Monthly Results» (`0x80BCEF7C`) también pone la imagen del año del índice.
- **No son del modo Temporada:** la tabla de 428 bytes por año de `[0x8104B5C4]` y la de 176 bytes son de MLB Life (sus punteros valen 0 en Temporada). (año + 1) × 10 + 390 en `0x80B3C99C` y (año + 1) × 3 en `0x80BF54BC` son cálculos, no tablas.
- **Sin encontrar todavía:** un historial de premios o de estadísticas de jugadores por año.

### Partida guardada de Temporada

- **La partida de Temporada** (`pm3usse<hueco>.dat`, 1.179.648 bytes) es una copia de un bloque de memoria; un desplazamiento del archivo es la posición dentro del bloque + 0x8C. El índice del año está en el archivo en 0x47E8C. Cambiar el día en memoria y guardar mueve la fecha de la partida.
- **Sitio libre en el archivo, no en memoria:** los datos de la partida ocupan 0x119910 bytes; el archivo se rellena con ceros hasta 0x120000 (26.352 bytes libres). Al cargar, el juego lee el archivo a un búfer aparte y copia solo esos datos, así que lo que haya en la cola del archivo no llega a memoria ni impide cargar (probado escribiendo una marca en 0x11F000). La memoria que sigue a los datos la usa otra cosa.
- **Cargar y guardar una partida de Temporada:** la carga lee el archivo en trozos de 16 KB hasta el trozo que pasa del final de los datos (0x11C000), sin llegar al final del archivo. Al guardar, el juego lee entero el archivo viejo de la ranura, escribe el nuevo y lo vuelve a leer entero para comprobarlo. Por eso el runtime lee la cola por su cuenta cuando una lectura llega al final de los datos, y la descarta si las lecturas siguen más allá.

Fuentes: `wiicompiled-season-probe.patch`, `tools/season_save.py`, volcados en `local/tmp/season/`, `docs/superpowers/specs/2026-10-19-season-archive-findings.md`.

## 10. Gráficos (GX y VI)

- **Panorámico nativo:** el juego consulta `SCGetAspectRatio` (`0x800D3374`) desde `0x8004D994`, `0x80055928`, `0x80062D24` y, en el REL, `0x807AB5D4` y `0x80CCAE3C`, y adapta la imagen a 16:9 (los objetos redondos siguen redondos). Código traducido y observación, `RL8E54`. **Hasta el 17 de octubre de 2026 no se activaba en el recomp:** la respuesta propia de Wiicompiled a `SCGetAspectRatio` lleva la dirección de Mario Kart Wii y no se enganchaba, y la función original lee `IPL.AR` de un SYSCONF que la instalación no tiene, así que devolvía 4:3 y «Forzar 16:9» solo estiraba una imagen 4:3 (lo notó un jugador). Arreglo: nativo `hle/mlb/sc_aspect_ratio.cpp` en `0x800D3374` (parche `wiicompiled-mlb-widescreen`), que contesta según los ajustes de vídeo. Comprobado en el partido de prueba: el juego registra `SCGetAspectRatio -> 16:9`, la bola sale redonda y el texto del HUD con sus proporciones.
- **Borde negro de la imagen:** el juego configura el VI con 640x480 (`VIConfigure` `0x800B37FC`, `viXOrigin` 40, `viYOrigin` 0) y su copia de pantalla toma las 480 líneas, pero dibuja dentro de un borde negro de 16 líneas arriba y abajo (448 líneas útiles, también en los menús); la línea contigua sale medio oscura. Medido en capturas del partido de prueba y del menú de estadios, `RL8E54`. En una tele el overscan lo escondía; en un monitor eran dos franjas negras (lo pidió un jugador). Opción «Llenar pantalla» (`[video] crop_black_borders`, activada por defecto): la copia de pantalla deja fuera 17 líneas arriba y abajo (`aurora_set_display_crop_lines`, parche `wiicompiled-mlb-fill-screen`) y la imagen ocupa la ventana; en 16:9 queda un 8 % más alta (480/446). Comprobado: sin franjas ni filo oscuro y con el HUD completo.
- `GXInit` `0x800C25CC`, `GXCopyDisp` `0x800C61F8`.
- Variables globales de GX:
  - `FifoObj` `0x802D2AC0`
  - `gxData` `0x802D2B40` (el puntero `__GXData` está en `0x8031C398`)
  - FIFO de la GPU `0x802D3140`, de la CPU `0x802D3164`, de listas de pantalla `0x802D3188`
  - cola de `GXDrawDone` `0x8031B6F8`
- Variables globales de VI:
  - inicializado `0x8031B400`
  - formato de TV `0x8031B470`
  - callbacks post/pre retrazo `0x8031B47C` / `0x8031B480`
  - cola de `VIWaitForRetrace` `0x8031B488`
  - contador de retrazos `0x8031B4AC`
  - `HorVer` `0x802BD070` (`+0x30` = siguiente framebuffer)
- **Cómo dibuja:**
  - Todo el 3D va por listas de pantalla (display lists); la interfaz escribe directo en el FIFO.
  - Los modelos con esqueleto usan `GX_VA_PNMTXIDX` directo, con POS/NRM/CLR0/TEX0 en `GX_INDEX16` (vértices de 9 bytes).
- **Carga por fotograma:**
  - Menús: 200-450 dibujos.
  - Partido: de 208 a 1.012.
  - Vista de fildeo: 650-990.
  - Unas 35 copias EFB por fotograma en partido; unas 17 van a texturas ya leídas en el mismo fotograma.
- **Pantalla de la correa:** compone la imagen, la copia con `GXCopyTex` a `0x90000840` en 512×480 y la dibuja como textura de 512×512 sobre un quad de pantalla completa. El hardware no comprueba el tamaño, así que la copia rellena las filas de arriba.
- **TLUT:** el juego vuelve a inicializar el mismo objeto TLUT en cada dibujo, sin destruirlo.
- **Saltos con máscara:** el juego usa tablas de saltos acotadas con `rlwinm` en vez de `cmplwi` (p. ej. `0x80031218`).

Fuentes: `config/wiicompiled/rl8e54-data.csv`, `docs/wiicompiled-feasibility.md`, `wiicompiled-aurora-partial-efb-copy.patch`, `wiicompiled-tlut-reinit-cache.patch`.

### Cámara del partido

Encontrada el 18 de octubre de 2026 para la vista previa de estadios (`runtime/src/hle/mlb/game_camera.cpp`, parche `wiicompiled-stadium-viewer`). Métodos: volcados de MEM1 cada 3 s (`WIICOMPILED_DEBUG_MEM1`) buscando pares ojo/objetivo plausibles; watchpoint de escritura sobre el ojo (`WIICOMPILED_DEBUG_WATCH`) resuelto con `llvm-nm`; envoltorios temporales en los shards.

- **Objeto cámara** (clase del DOL, vtable `0x801568AC`, bloques de `0x2C0` bytes en el montón de MEM1; su dirección cambia entre ejecuciones):

| Offset | Campo |
|---|---|
| `+0x40` | ojo (x, y, z, 1), en decímetros, Y arriba, segunda base hacia −Z |
| `+0x60` | objetivo (x, y, z, 1) |
| `+0x70` | vector arriba (0, 1, 0, 1) |
| `+0x8C` | puntero a su función de vista (`0x80055D24`) |
| `+0x90` | puntero a su función de proyección (`0x80054B1C`) |
| `+0x94` | cot de la mitad del ángulo **horizontal** de la proyección: 2,206 en la vista del bateador (48,8° de ancho; 28,6° de alto en 16:9). Comprobado al dibujar la colisión en el visor: leído como ángulo vertical, todo salía 16/9 veces más cerca del centro de la imagen |
| `+0x154` | matriz de vista 3×4 |
| `+0x184` | dirección de la vista |

- **`0x80055D24`** reconstruye la vista en cada fotograma: `LookAt` propio del juego (`0x80055620`, firma como `C_MTXLookAt`: matriz, ojo, arriba, objetivo) en `+0x154`; luego `objetivo − ojo` (`0x8006327C`) guardado con `0x80060588` en `+0x184`. El partido **no** usa `C_MTXLookAt` del SDK (`0x800A8DC4`); sí lo usa el render de Miis (`0x80090AF8`).
- **Director de cámaras** del partido: objeto apuntado desde `0x81050810` (BSS). `0x80C779D4(director, fuente)` copia una cámara fuente en la principal con `0x80055300` (copia de cámara, 633 bytes desde `+0x40`); la principal la da `0x80061CF8([[director+60]+732], 0)`; después marca el bit 1 del byte `+128`, llama al virtual `+24` de la cámara, a `0x80C6217C([director+60]+64)` y al virtual `+116` de `[director+60]`. Solo actúa con `[director+64] == 5`.
- **Tomas** (cámaras fuente, una por toma, cada una con su propio controlador; vistas en 2 minutos de partido automático):

| Quién llama al director | Toma | Ojo → objetivo de muestra |
|---|---|---|
| `0x80CC5BE8` | detrás del bateador (cambia de lado con la mano del bateador) | (±2,1, 15,5, 59,6) → (∓9, −1,7, −229) |
| `0x808F1CD8` | jugada, alta y lateral tras el batazo (cot 3,51 ≈ 31,8°) | (±328,6, 184,8, 102,7) → (0, 0, −120,1) |
| `0x80AA0BA4` | lejana; varía: (−570,5, 200, −560,5) → (−4,8, 5, −189,1) o (0, 51,3, 154) | — |
| `0x808962AC` | presentación (al empezar) | (0, 50, 0) → (0, 699, −1200) |

  No apareció una cámara aparte para lanzar: con la IA, el lanzamiento se ve desde la del bateador.
  Al empezar, un vuelo de presentación mueve la cámara principal por el estadio sin pasar por el director (escribir su ojo y objetivo antes del `LookAt` no lo frena). Aplicar una toma con `0x80C779D4` lo termina.
- **Lo que no sigue a esta cámara:** los jugadores, la pelota y el cursor de la zona de strike se dibujan con la matriz de vista (`+0x154`) de las cámaras fuente, no con la de la principal; el HUD es ortográfico. El estadio, el público y las banderas sí siguen a la principal.
- **Actualización del partido por fotograma:** `0x808816C8(partido)` llama a `0x8088175C(partido)` (máquina de estados propia vía `0x80882E84`) y después a `0x808661BC(partido)`, que salta a `0x80866EA4`: el árbol de estados del partido. Es una máquina de estados jerárquica: `0x8086C980(nodo, ptmf, evento)` apila el estado (`[nodo+16]` es la profundidad; `+4`, `+20`, `+32` y `+44` por nivel) y llama al manejador con `0x80129758` (llamada a puntero a miembro). Del árbol cuelgan el lanzamiento, el bateo, la defensa y el director de cámaras: la pila de una toma de bateador es `0x80CC5BE8` ← `0x80CAD398` ← `0x80867A74` ← `0x8086C980` ← … ← `0x80866EA4` ← `0x808816C8` ← `0x80022438`. El público animado y las banderas del estadio se actualizan fuera de ese árbol: sin él siguen moviéndose (comprobado en el estadio original).
  El slot `+0x18` (`0x808816FC`) del vtable `0x80E5E350` no se llamó ni en 60 s de partido normal: no es esta actualización.
- **El recomp** sustituye `0x80055D24` y `0x80C779D4` por versiones nativas idénticas. Con la vista previa activa, la primera escribe el ojo y el objetivo de la cámara libre en la cámara principal antes del `LookAt`, y la segunda aprende cada cámara fuente por quién la aplica, para mostrar esas tomas. Partido normal comprobado sin cambios: mismas tomas en el mismo orden.
  Con la cámara libre, las cámaras fuente miran a otro lado (desde 10 km bajo el campo) para sacar de pantalla jugadores, pelota y cursor; su ojo, objetivo y vector arriba se guardan y se reponen al elegir una toma. `0x808816C8` también es nativa: en la vista previa, con el estadio ya en pantalla, se salta `0x808661BC` y el partido queda quieto (2 minutos sin accesos fuera de memoria; el partido normal sigue jugando).

## 11. Mandos

- **Mando de GameCube:**
  - `PADRead` `0x800B76BC`; las llamadas del juego a `PADInit`/`PADRead` están en `0x80040B14` / `0x80040BC8`.
  - El juego divide el stick principal entre **88** y el C-stick entre **64**. Las guías reales llegan a ~87 y ~74, así que un ±127 completo se satura al ~69 % del recorrido.
- **Movimiento del fildeador (comprobado en el juego, 15 de octubre):**
  - El paquete de entrada de cada puerto está en `common+992+puerto·368` (`common = *(0x8103D118)`); `+12`/`+14` son los ejes del stick en s16.
  - El decodificador de fildeo `0x8088540C` (llamado desde `0x80884FE4`) solo acepta la entrada mientras un humano mueve a un fildeador. Copia esos ejes a `field+656`/`+658` (`field = *(0x8103E708)`) y devuelve `field`; las salidas tempranas devuelven el paquete.
  - `0x8096A690` lee ese vector: guarda el ángulo en `field+676` y pone `field+672 = 1` si algún eje pasa de 8 (stick inclinado).
  - Una diagonal a fondo de un stick redondo, `(60, 61)` tras la puerta de GameCube, hace caminar; el teclado, `(87, 87)`, hace correr. Ampliar solo `+12`/`+14` durante el decodificador, y restaurarlos después, no bastó: el fildeador siguió caminando.
  - **Solución:** el runtime marca el puerto cuando el decodificador acepta la entrada, y en la siguiente lectura de `PADRead` convierte el stick principal de ese puerto en cuadrado: misma dirección y el radio en cada eje. Así `(90, 90)` sin escalar pasa a `(127, 127)`, como el teclado. Con capturas cada 1,5 s en un partido de prueba con fildeo manual, la marca solo se enciende en la vista de fildeo; al pitchear y al batear queda apagada.

- **Objetos de entrada:** son clases C++ con vtable. Con `base` = búfer de `PADRead` − 1648: mando GC en `base+1104`, KPAD en `base+560`, entrada combinada en `base+16`. Cada uno guarda un anillo de muestras.
- **Entero a coma flotante:** `0x80040268` convierte con el truco de CodeWarrior. Usa la constante `0x4330000080000000` de `r2-31664` (`0x8031BD90`); si esa constante se corrompe, todas las conversiones con signo fallan.
- **KPAD/WPAD:** enlazados igual que en Mario Kart Wii.
  - `KPADRead` `0x80104C4C`, `KPADInitEx` `0x80105378`
  - callback de muestreo `0x801056A4`
  - `WPADiInitSub` `0x80108ABC`, `WPADInit` `0x80108CCC`
- **Bits:** Wii Remote A = `0x800`; Nunchuk C = `0x4000`, Z = `0x2000`; GC A = `0x100`, A+Start = `0x1100`.

Fuentes: `wiicompiled-mlb-pad-stick-range.patch`, `config/wiicompiled/rl8e54-data.csv`, `docs/wiicompiled-feasibility.md`.

## 12. Partidas guardadas, NAND, SYSCONF y ES

- **Título:** `00010000/524c3845` (título de disco). La partida es `title/00010000/524c3845/data/pm3usma.dat`.
- **IOS:** el TMD del disco pide IOS33 (`tmd.bin` `+0x184` = `0x0000000100000021`). El juego imprime la versión en su `OSReport` de arranque («Firmware: 33.14.24»).
- **Cabecera del disco en memoria baja:** `0x80000000` = `RL8E`, `0x80000004` = `0x35340001` (editor «54», disco 0, versión 1). El runtime escribía el editor «01» y el disco 1 de Mario Kart Wii, y usaba «RMCP» como código de respaldo; desde el parche `wiicompiled-mlb-disc-identity` usa los de MLB.
- MLB Life, Season y League escriben partida.
- **NAND:** el juego usa las bibliotecas NAND/ISFS del SDK sobre IOS.
  - `NANDInit` `0x800D0640`, `libState` `0x8031B778`.
  - El directorio propio (`0x802D3AE0`) lo pide a `ESP_GetDataDir` (`0x80064044`).
- **Peticiones a ES (`/dev/es`):**
  - ioctlv 22 `ES_GetConsumption`: un título de disco no tiene entradas.
  - ioctlv 29 `ES_GetTitleDir`: `"/title/%08x/%08x/data"`.
  - ioctlv 27 `ES_DIGetTicketView`: `0xD8` bytes. `+0x0C` ID de consola, `+0x10`/`+0x14` ID de título, `+0x18` máscara de acceso `0xFFFF`.
- **SYSCONF:** sección 2.
- **Partida del modo Temporada** (`pm3usse<hueco>.dat`): formato, sitio libre, carga y guardado en [Modo Temporada › Partida guardada de Temporada](#partida-guardada-de-temporada).
Fuentes de la sección: `wiicompiled-mlb-nand-es.patch`, `hle/mlb/sc_check_status.cpp`, `docs/mods.md`.

## 13. Bibliotecas y funciones identificadas

**Fechas de compilación de los módulos del SDK** (etiquetas en el binario):

| Módulos | Fecha |
|---|---|
| THP, EXI, SI, DVD, VI, AI, AX, DSP, SC, ENC, KPAD | 8 ago 2007 |
| RFL | 5 sep 2007 |
| PAD | 3 oct 2007 |
| GX, NAND | 11 dic 2007 |
| OS | 30 ene 2008 |
| HBM (menú HOME) | 7 feb 2008 |

- **Compilador:** Metrowerks CodeWarrior (hay identificadores de su runtime y de MetroTRK); la versión exacta no está identificada.
- **Bibliotecas presentes:**
  - JSystem de Nintendo: J3D, J2D, JAudio2/JAS, JStudio, JMessage, JGadget
  - menú HOME (`homebutton::*` desde `0x80064248`, `nw4hbm`)
  - pila Bluetooth de Broadcom
  - MetroTRK
  - SDK RVL
- **Ausentes:** no hay NHTTP, DWC/GameSpy ni sockets (no se encontraron sus cadenas), ni reverb AXFX.
- **Cómo se nombraron las 1.794 funciones** (`config/wiicompiled/rl8e54-functions.map`, formato `<dirección> <nombre>`):
  1. Firmas de la base de Dolphin (`totaldb.dsy`), aceptando solo coincidencias únicas: 1.375 funciones, entre ellas `gx.a` 108, `os.a` 105 y `dvd.a` 63.
  2. Alineación del orden de enlace con el mapa de Mario Kart Wii entre anclas comunes: 118 más (≈97 % de precisión en pruebas de ocultación).
  3. Nombres de dtk.
- **Equivalencias con Mario Kart Wii** (útiles para portar código o ganchos de MKW): `InsertAlarm` `0x8009B448`, `OSSaveContext` `0x8009CCD8`, `__OSThreadInit` `0x800A3C0C`, `__OSTimeToSystemTime` `0x800A53A4`, `OSLoadContext` `0x8009CD58`. `config/wiicompiled/rl8e54-data.csv` traduce 65 direcciones de datos de MKW a MLB, cada una con la instrucción que lo demuestra.
- **Sin dirección todavía:** `VIInit`/`VIConfigure`, `PADInit`, `OSInitAudioSystem`, `__OSInterruptInit`, `OSGetCurrentThread` y algunas más. Algunas pueden no existir porque el enlazador las eliminó.

## 14. Peculiaridades del juego que importan al portarlo

| Peculiaridad | Consecuencia |
|---|---|
| El sonido mide el tiempo en vueltas de 5 ms | Si el temporizador se retrasa, el comentarista se corta y suenan chasquidos (sección 4) |
| El hilo principal no cede durante cálculos largos ni en "NOW LOADING" | Con hilos cooperativos, los demás se quedan sin turno; hacen falta puntos de cambio |
| El montón ocupa toda la arena casi al byte | El tope de la arena tiene que ser `0x817FFD40`, y el BSS del REL tiene que quedar en `0x8102B0C0` |
| El juego borra el código de juego de la memoria baja | No sirve para saber qué juego corre después del arranque |
| Copia 512×480 sobre una textura de 512×512 | Una copia parcial es válida; el emulador no debe rechazarla |
| Tablas de saltos sin comparación previa | Un traductor estático debe reconocer el índice acotado con `rlwinm` |
| Getters dentro de otras funciones, llamados por vtable (`0x80664C20`-`0x80664C38`) | Hay que añadirlos al mapa de funciones o el salto indirecto falla |
| Stick de GC dividido entre 88/64 | Un mando moderno necesita escalar el recorrido |
| `OSInit` espera al SYSCONF y `RFLWaitAsync` gira en bucle | Los callbacks de IOS deben llegar mientras el juego espera |

## 15. Cómo seguir investigando

- **Mapa de funciones:** `config/wiicompiled/rl8e54-functions.map`. Añadir un nombre al mapa lo lleva al código traducido la próxima vez que se traduce.
- **Datos del SDK de MKW a MLB:** `config/wiicompiled/rl8e54-data.csv`, con la prueba de cada fila.
- **Herramientas** (`docs/reverse-engineering-tools.md`):
  - `tools/identify_sdk_functions.py`
  - `tools/align_sdk_by_anchors.py`
  - `tools/function_data_refs.py`
  - `tools/projectbin.py` (lee y reconstruye `project.bin`)
  - `tools/packwii.py` (exporta e importa las texturas de un `.pack`)
  - `tools/stadium_re.py` (comprueba los estadios: modelos `PJ7M` y tabla `tbl`) y el complemento de Blender `tools/blender/powerpros_stadium`
  - `tools/roster/` (convierte rosters)
- **Puntos de vigilancia en el juego recompilado:** `WIICOMPILED_DEBUG_WATCH=<dirección>` informa de cada escritura en esa dirección, con la pila de llamadas. Así se encontró el reloj de las frases.
- **Diagnóstico de audio:** `WIICOMPILED_AUDIO_DUMP=<archivo>` guarda la mezcla final para medir chasquidos.
- **Pendiente de investigar:**
  - objetos de los `.pack` aparte de las texturas y los estadios: modelos de jugadores, capas `SURF`/`SURG` y movimientos
  - lo pendiente de los estadios (flags de colocación, árboles de planos, efecto de cada superficie)
  - texto de los `.dat` `PCD`/`TCD`/`SID`
  - estructura de los equipos y las alineaciones más allá de lo mapeado
  - lógica de MLB Life
  - los ~450 candidatos de getters en mitad de funciones
  - nombres de las funciones del REL (43.088 sin nombre)

Al añadir algo a este documento, indica de dónde sale (desensamblado, ejecución u observación) y la versión `RL8E54`.
