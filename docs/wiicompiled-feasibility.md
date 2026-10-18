# Prueba de viabilidad: Wiicompiled con MLB Power Pros 2008

*English version: [en/wiicompiled-feasibility.md](en/wiicompiled-feasibility.md)*

El objetivo es saber si el traductor y el runtime nativo de Wiicompiled (recompilación de Mario Kart Wii, GPL-3.0, `6fa24737`) pueden servir de base para este juego. La alternativa es reimplementar lo mismo sobre DolRecomp y ModernGekko.

El camino actual sigue intacto: es la referencia y aporta el verificador lockstep.

## Por qué encaja

- Wiicompiled admite un DOL más un REL estático en dirección fija (`inputs.rel.load_address`). MLB tiene ese mismo formato: `ovl_main.rel`, con la cabecera en `0x80575580`, como ya fijaba `local/generated/dolrecomp/rel-linked/layout.json`.
- Su runtime cubre el SDK RVL de forma general:
  - GX sobre aurora (Dawn/WebGPU);
  - OSThread como fibras;
  - mezclador AX nativo;
  - DVD y NAND;
  - PAD, WPAD y KPAD;
  - VI.
- Sus ~377 ganchos nativos están atados a direcciones de MKW. Para MLB hay que reasignarlos.

## Fase 1: traducción (10 de octubre de 2026)

`tools/wiicompiled-translate.ps1`:
- crea la copia de trabajo `local/wiicompiled-mlb/src`;
- aplica `tools/patches/wiicompiled-rel-bss-address.patch`;
- compila el traductor;
- convierte el inventario dtk en `MAP.txt` (`tools/dtk_to_wiicompiled_map.py`);
- traduce con `config/wiicompiled/rl8e54.yml`.

Las cachés de .NET y NuGet, los temporales y las salidas quedan en `local/`. No se escribió nada en C:.

| Medida | Valor |
|---|---|
| Tests del traductor | 642 de 642 |
| Funciones traducidas (DOL + REL) | 48.219 |
| Alcanzadas por el grafo de llamadas desde `__start` | 3.017; el resto se siembra desde el mapa dtk |
| Instrucciones no soportadas | 0 |
| Funciones con SSA inválido | 0 |
| Tiempo de traducción completa | 57 s en frío, 37 s sin cambios |
| C++ generado | 394 MB, empaquetado en 72 shards con contenido equilibrado (1,04×) |
| Datos incrustados | 13,05 MB (DOL 2,30 MB y REL 11,38 MB) |

Para comparar: nuestro generador tarda unos 50 min en una generación completa y unos 22 min con la caché por IR.

Ajustes que hizo falta hacer:
- **BSS del REL.** El juego reserva la BSS en `0x8102B0C0`, `0x40` más allá de donde la pone Wiicompiled por defecto (justo tras el archivo). Las relocaciones `lis`/`addi` hacia la BSS quedan fijadas en el código traducido, así que el parche añade `inputs.rel.bss_address`, con validación de solapamiento y alineación.
- **Entradas `gap_*` de dtk.** No son código; por ejemplo, la cadena `MetroTRK` en `.init`, en `0x80004338`. Generaban la única instrucción "no soportada" (`0x4D657472` = "Metr") y el conversor ahora las descarta.
- **L2CR.** `mfspr 1017` se traduce como `PPC_ReadSpr(1017)`, que su runtime ya conserva como registro sombra.

Alcance: esto es código traducido, no comportamiento validado. El runtime todavía no compila para MLB. *(Resuelto: compila y arranca en la fase 2, abajo.)*

## Cobertura de nombres para reasignar ganchos

El runtime tiene 582 registros nativos:
- 205 son `gx_stub_*`, que abortan a propósito;
- 377 son implementaciones reales.

Cruzando sus nombres normalizados (`GX__SetVtxDesc_8016d3a4` → `GXSetVtxDesc`) con los 361 nombres que dtk asignó en MLB, **solo 61 tienen ya dirección conocida en MLB**:
- 11 de 13 son helpers `PPCM*`;
- `__init_hardware`;
- unas pocas funciones de NAND, IOS, DVD y GX.

No tienen dirección conocida en MLB, por ejemplo:
- casi todo GX: 2 de 71 `GXSet*`, 0 de 9 `GXGet*`, 0 de 9 `GXLoad*`;
- WPAD: 0 de 14;
- SC: 0 de 6;
- las funciones de hilos y alarmas de OS.

Conclusión: sin identificar las funciones del SDK en MLB no se puede aprovechar el runtime de Wiicompiled, ni tampoco hacer reemplazos nativos en el camino actual. Esa identificación es el siguiente trabajo común a ambos caminos.

Hay dos formas viables de hacerla sin MKW:
- **Firmas de bytes con las relocaciones enmascaradas**, generadas a partir de las decompilaciones públicas del SDK RVL que compilan igual que el original. Las versiones del SDK de MLB, por las cadenas del disco, son: OS de enero de 2008, GX y NAND de diciembre de 2007, y el resto de agosto a octubre de 2007.
- **Propagación por el grafo de llamadas** desde funciones ya identificadas, por ejemplo `GXInit` (0x800C25CC) llama al resto de inicializadores GX en un orden conocido.

## Identificación del SDK en MLB (10 de octubre de 2026)

Dos herramientas, ninguna de las cuales necesita un binario de MKW:

1. **`tools/identify_sdk_functions.py`** aplica la base de firmas de Dolphin (`Data/Sys/totaldb.dsy`, 10.549 entradas, con el SDK RVL incluido). Reimplementa `HashSignatureDB::ComputeCodeChecksum`, que solo usa los bits de opcode, sobre el DOL y el REL reubicado.
   - Una coincidencia se acepta solo si el hash es único en el juego y en la base.
   - Se descartan los nombres C++ sin biblioteca, porque son clases de otros juegos que colisionan en funciones minúsculas.
   - Si dtk ya tenía un nombre real, gana el de dtk.
   - Resultado: **1.375 funciones**, con las bibliotecas `gx.a` (108), `os.a` (105), `dvd.a` (63), `vi.a` (26), `wpad.a` (20), `sc.a` (20), `nand.a` (20) y otras.
   - Donde dtk ya tenía nombre, la firma coincide en 173 de 175 casos. Los 2 desacuerdos llevaron a añadir los filtros anteriores.
   - Salida: `local/analysis/sdk-symbols.csv`.
2. **`tools/align_sdk_by_anchors.py`** alinea el orden de enlace con `projects/mkwii/MAP.txt` de Wiicompiled, que solo contiene direcciones y nombres.
   - Entre dos anclas comunes en orden creciente, nombra las funciones intermedias cuando los tamaños coinciden.
   - Si los huecos tienen distinto número de funciones, empareja solo desde cada ancla hacia dentro.
   - Itera usando los nombres nuevos como anclas y descarta los nombres asignados dos veces.
   - Resultado: **118 funciones más**.
   - Precisión en una prueba de ocultación (30 % de los nombres conocidos ocultos, 5 semillas): **97 %** (234 de 241), con un 36 % de recuperación.
   - Salida: `local/analysis/sdk-symbols-aligned.csv`.

`tools/dtk_to_wiicompiled_map.py` fusiona ambas salidas en `MAP.txt` y deja 1.681 funciones con nombre (dtk solo tenía 361). La cobertura de ganchos la mide `tools/wiicompiled_native_coverage.py`:

| Grupo | Antes (dtk) | Ahora |
|---|---:|---:|
| GX | 2 de 71 `GXSet*` | 106 de 136 |
| OS | — | 73 de 99 |
| Almacenamiento | — | 25 de 53 |
| Entrada | 0 de 14 (WPAD) | 12 de 22 |
| Audio | — | 7 de 24 |
| **Total** | **61 de 377** | **241 de 377 (64 %)** |

Sigue sin dirección una parte importante del SDK:
- VI: `VIInit`, `VIConfigure`, `VIGetRetraceCount` y los callbacks de retrace;
- mandos: `PADInit`/`PADRead`, `KPADRead`, `WPADInit`;
- audio: AI/DSP y `OSInitAudioSystem`;
- NAND: abrir, cerrar y leer;
- OS: `__OSInterruptInit`, `__OSPSInit` y `OSGetCurrentThread`.

Algunas pueden no existir en MLB porque el enlazador las eliminó (por ejemplo `GXDrawSphere`). Para el resto quedan dos vías:
- referencias a registros MMIO (VI `0xCC002000`, AI `0xCD006C00`, DSP `0xCC005000`) y a cadenas de texto;
- firmas propias compiladas desde las decompilaciones públicas del SDK.

Precisión: los nombres alineados son candidatos al 97 %. Cada gancho que se reasigne debe validarse con comportamiento (lockstep o prueba dirigida) antes de darlo por bueno.

## Fase 2: runtime de Wiicompiled con MLB (10 de octubre de 2026)

**Toolchain**, todo en `local/`:
- LLVM-MinGW `20260616` (sha256 verificado) en `local/tools/llvm-mingw-20260616-ucrt-x86_64`;
- Dawn y SDL3 descargados por CMake al configurar;
- modo portable (`portable.txt`), con `UserData` dentro del directorio de build.

**Ciclo de trabajo:**
1. `tools/wiicompiled-translate.ps1 -Fresh -Groups os,audio,storage,vi,gx,input` hace, en ~1 min:
   - clona la copia de trabajo y aplica los tres parches;
   - regenera los nombres del SDK;
   - reasigna los ganchos;
   - traduce y emite los shards.
2. `tools/wiicompiled-build.ps1 -RunSeconds N` compila (~2–4 min) y ejecuta.

La vuelta completa ronda los 5 min. Un cambio solo en el runtime compila en segundos.

**Parches** (contra `6fa24737`):
- `wiicompiled-rel-bss-address.patch` (traductor): BSS del REL en su dirección real.
- `wiicompiled-masked-jump-tables.patch` (traductor): tablas de salto cuyo índice está acotado por la máscara de un `rlwinm` y no por un `cmplwi`.
  - MLB las usa, por ejemplo en `0x80031218`.
  - Los 642 tests siguen pasando.
- `wiicompiled-mlb-bringup.patch` (runtime):
  - `game_profile.h` con las constantes de cada juego (entrada, constructores, ID del disco, ArenaLo, `OSLoadContext`);
  - las formas `*_UNBOUND` de los ganchos;
  - `music_attenuation` vacío fuera de MKW;
  - `game_aliases_rl8e54.cpp`, que reenvía las llamadas directas del HLE a funciones de MKW;
  - un volcado de hilos invitados con `WIICOMPILED_DEBUG_THREADS=<s>`.

**Reasignación** (`tools/wiicompiled_rebind.py` + `config/wiicompiled/rl8e54-data.csv`):
- Cada gancho de un grupo activado recibe la dirección de MLB de la misma función, emparejada por nombre entre el mapa de MKW y el de MLB.
- Los nombres de MKW repetidos en varias direcciones no se enlazan.
- Las direcciones de datos se traducen por **bloques verificados**: en cada objeto del SDK, varias anclas leídas en el código de MLB confirman un mismo desplazamiento. La evidencia de cada fila está en el CSV.
- Lo que no se pudo verificar se descarta explícitamente. Por ejemplo, cuatro escrituras de estado del HLE de DVD caerían en variables distintas de MLB y se sustituyen por `(void)0`.
- `tools/function_data_refs.py` y `tools/disasm_guest.py` (desensamblado sacado de la salida C de DolRecomp; retirado, en el tag `moderngekko-era`) son las herramientas de esta decompilación dirigida.

Con `os,audio,storage,vi,gx,input` activos: **316 ganchos enlazados, 302 sin enlazar, 94 literales traducidos**, ninguno sin resolver.

**Progreso del arranque**, cada grupo activado hizo avanzar el arranque hasta el siguiente punto:

| Grupos activos | Dónde para |
|---|---|
| ninguno | `__start` → escritura MMIO `0xCC003004` (PI) |
| os | `__OSInitAudioSystem` (MMIO DSP) |
| + audio | DVD a bajo nivel desde `OSInit` |
| + storage | `VIInit` (MMIO `0xCC002002`) |
| + vi | `GXInit` (MMIO `0xCC000002`) |
| + gx | `main` → `WPADInit` (GPIO de Hollywood) |
| + input | salto indirecto a una tabla no reconocida, corregido con el parche del traductor |
| todo lo anterior | **corre 60 s sin fallar** |

En esa última ejecución:
- `OSInit`, VI, GX, AI y el hilo mezclador AX están activos.
- Hay 5 hilos invitados como fibras.
- El juego imprime sus `OSReport` (SDK OS, SC, KPAD, AX).
- **No dibuja todavía.** La ventana muestra el marcador de WiiCompiled a ~50 FPS. *(Resuelto en «Render», 11 de octubre.)*
- Los hilos activos son del DOL; el REL `ovl_main.rel` aún no se ha cargado ni enlazado.
- El hilo principal está en el bucle de frame del cargador (`0x800472B4`): sincronización por token de GX y `VIWaitForRetrace`.

**Siguiente paso (resuelto en «Carga de `ovl_main.rel`»):** averiguar en qué espera la carga del REL. Seguramente sea la ruta DVD o NAND del cargador del juego (hilo `0x80242888`, entrada `0x80027A54`). El volcado de fibras solo guarda el contexto inicial, así que hace falta trazar las llamadas DVD y NAND.

Alcance: es comportamiento de arranque observado, no validado contra Dolphin. Cada gancho reasignado sigue siendo candidato hasta que se compare con la referencia (lockstep o pruebas dirigidas).

## Render (11 de octubre de 2026)

**Correcciones** (todas en `wiicompiled-mlb-bringup.patch` y `tools/wiicompiled_rebind.py`):
- **Pantalla de arranque del runtime.** Tapaba la imagen hasta que el gancho de la correa de MKW (`StrapScene`) aceptaba la entrada. Fuera de MKW se da por aceptada.
- **`dynamic_aspect.cpp`.** Escribía registros de pantalla EGG en direcciones de MKW, que en MLB son datos del juego, e invocaba funciones de MKW por dirección. Ahora se anula fuera de MKW.
- **Nombres `__GX::X` del mapa de MKW.** Ya se normalizan a `__GXX`, con lo que se enlazan 7 funciones internas de GX: estado sucio, texturas, TMEM, PE e inicialización de revisión.
- **Stubs fatales de GX (`gx_stub_*`).** No se enlazan nunca. En MKW abortan porque ese juego no los alcanza; en MLB es mejor que corra el original traducido, cuyas escrituras de registros a la FIFO aplica el parser de Wiicompiled.
- Grupos `ios`, `esp`, `c_stdio` y `trk` activados. `sc` sigue sin activar.

**Diagnósticos opcionales:**
- `WIICOMPILED_DEBUG_THREADS=<s>`: hilos invitados, pila nativa del hilo principal con nombres de funciones del juego, contadores `GXCopyDisp`/`GXBegin` y estado de presentación de VI.
- `WIICOMPILED_TRACE_NATIVES=<frame>`: nativos invocados en ese frame. Solo cubre la ruta tipada.
- `WIICOMPILED_DEBUG_DVD=1`: cada lectura de archivo.

**Estado:**
- El juego corre su bucle de frames: ~50–60 `GXCopyDisp`/s y ~70 primitivas por frame.
- Lee del disco los bancos de sonido y `project.bin`.
- Espera en la pantalla de la correa, que requiere pulsar A; en la referencia esa pantalla aparece a ~15 s.
- **La salida es negra.**

**Descartado** con instrumentación temporal:
- Texturas: llegan a aurora y su contenido es el de la correa (volcado DDS).
- Colores: material `ffffffff` y registro TEV C0 `ffffffff`.
- Proyección: ortográfica 640×480 correcta.
- Viewport y scissor: valores válidos.
- `CULL_NONE`.
- Array de posiciones indexado: en 0x8054E1C0, idéntico byte a byte al de la referencia de Dolphin.
- `PSMTXConcat`: un nativo C++ da los mismos resultados. Las matrices de entrada son nulas en los primeros frames y luego identidad.
- Test de profundidad: desactivarlo no cambia nada.
- Rango de profundidad: comprimirlo no cambia nada.

**Resuelto (11 de octubre).** La causa era una copia EFB→textura parcial que aurora no reconocía.

Cómo se aisló:
- Con un color de borrado rojo y sin los dibujos del juego, la pantalla sale roja, así que la presentación funciona.
- Se dibujó un solo draw cada vez. El último de los 8 del frame es un quad a pantalla completa que pinta negro opaco (blend `NONE`).
- Ese quad muestrea un `GXTexObj` RGBA8 de 512×512 en `0x90000840`. Allí la RAM está a cero, también en el volcado MEM2 de Dolphin del segundo 16.
- El juego compone la pantalla y, en cada frame, la copia con `GXCopyTex(0x90000840)` en **512×480**. Después la dibuja como textura de 512×512. Dolphin guarda esa copia solo en GPU, por eso su RAM también está a cero.
- `find_copy_texture_for_texobj` de aurora exigía alto exacto. Al no coincidir, leía la RAM (ceros) y salía negro.

Arreglo, en `tools/patches/wiicompiled-aurora-partial-efb-copy.patch` (4.º parche, aplicado por `wiicompiled-translate.ps1`):
- Se acepta la copia cuando ancho y formato coinciden y la copia es más baja que el `texObj`.
- Se muestrea con el alto real de la copia, de modo que la fila *n* de la textura es la fila *n* de la copia.

**Verificación:**
- Ciclo limpio con `-Fresh`: 4 parches, traducción en 44 s y compilación.
- A los 16 s se ve la pantalla de la correa con el contenido de la referencia de Dolphin.
- A los 20 s ya pasó al segundo panel. Funciona a 60 FPS.
- Es comportamiento observado. No hay comparación píxel a píxel; la escala horizontal de la copia de 512 px a la ventana no está validada.

**Siguiente paso (resuelto abajo):** pasar la pantalla de la correa (pulsación de A automatizada) para que siga la carga de `ovl_main.rel`.

## Carga de `ovl_main.rel` (11 de octubre de 2026)

**Entrada automatizada.** `tools/patches/wiicompiled-scripted-kpad.patch` añade un mando Wii virtual en el canal 0. Con `WIICOMPILED_KPAD_SCRIPT="inicioMs-finMs:botonesHex,..."` mantiene los bits `WPAD_BUTTON_*` durante cada ventana de `SDL_GetTicks`. Por ejemplo, `8000-8150:800` pulsa A. Sin la variable el runtime se comporta como antes. Con A, el juego pasa la pantalla de la correa y muestra "NOW LOADING".

Para llegar a la carga del REL hubo que corregir cuatro fallos, en este orden:

1. **`DVDLowInquiry`.** El HLE de Wiicompiled trataba el primer argumento como bloque de comando y nunca llamaba al callback. `DVDInquiryAsync` quedaba como comando en ejecución del SDK y `DVDGetDriveStatus` devolvía "ocupado" para siempre. El gestor de recursos del juego (`fn_80042C28`) solo procesa su cola cuando la unidad está libre, así que no leía nada. Ahora el HLE rellena `DVDDriveInfo` con los valores de Dolphin y señala fin de transferencia.
2. **`DVDLowSeek`.** No tenía HLE. El SDK real tocaba registros DI y esperaba una interrupción que nunca llega. Ahora el HLE completa el comando al instante. Los puntos 1 y 2 van en `tools/patches/wiicompiled-dvd-low-completion.patch`.
3. **Arena alta de MEM1.** El runtime usaba la de MKW, `0x817F0520`. Dolphin coloca el FST de MLB, de 0x2C0 bytes, en lo alto de MEM1 y deja la arena alta en `0x817FFD40`. Ahora es `GameProfile::kMem1ArenaHi`. Con el valor de MKW el heap principal del juego tenía 62 KB menos que en Dolphin. El heap era idéntico al de Dolphin en la pantalla de la correa, comprobado con volcados de MEM1.
4. **Colocación del REL y de su BSS.** Este fue el fallo que congelaba la ventana:
   - El código traducido lleva fijas las direcciones del módulo (`0x80575580`) y de la BSS (`0x8102B0C0`), pero el juego reserva ambos en su heap.
   - En Dolphin, el bloque del REL ocupa entero un hueco libre de `0xAB5B20` bytes. Comprobado con un sondeo de Dolphin con A automatizada y volcado de memoria (`boot-probe-20261011-183222-065`).
   - En el runtime, el reparto de tiempo entre hilos dejaba ese hueco dividido de otra forma, y la BSS caía en `0x8102B0A0`. Las últimas variables de la BSS pisaban el encabezado del bloque siguiente del heap.
   - Con la lista libre rota, una reserva de 278 KB devolvía NULL y el juego copiaba un paquete `PACKWII` sobre `0x80000000`. Eso machacaba los globales del OS (`__OSCurrentThread` = `0xDC`): `OSSleepThread` no podía aparcar el hilo, `VIWaitForRetrace` giraba sin fin y la ventana dejaba de responder.
   - El arreglo es una descompilación nativa del cargador de overlays del juego, `fn_80051474`, en `runtime/src/hle/mlb/rel_loader.cpp`. Antes de reservar, divide el bloque libre para que el propio asignador del juego deje el módulo y la BSS en las direcciones traducidas.
   - Va en `tools/patches/wiicompiled-mlb-overlay-loader.patch`. `tools/wiicompiled_rebind.py` respeta los ganchos `MLB_*`, que ya llevan direcciones de MLB.

**Resultado observado.** Tras pulsar A, el juego:
- lee los recursos;
- carga y enlaza `ovl_main.rel`;
- ejecuta su `_prolog`;
- lee muchos más recursos de `project.bin`;
- muestra "The Wii system memory is being checked." a 60 FPS.

Es comportamiento observado, no validado píxel a píxel ni contra Dolphin más allá de la disposición del heap.

**Métodos que funcionaron.**
- Volcado de MEM1 desde el hilo de diagnóstico, comparado con el de Dolphin.
- Recorrido de la lista de bloques del heap del juego. La cabecera ocupa 0x20 bytes: `+0` flags, `+8` tamaño, `+0xC/+0x10` enlaces de la lista libre, `+0x14/+0x18` bloques vecinos.
- Watchpoint hardware de Windows: DR0–DR3 en todos los hilos, sobre la vista host y sobre la vista guest `0x100000000000 + dirección`. El código traducido escribe por la vista guest. El watchpoint imprime la pila host simbolizada con `llvm-symbolizer`.

Esta instrumentación fue temporal y no está en ningún parche.

**Siguiente paso (resuelto abajo):** comprobar hasta dónde avanza tras la comprobación de la memoria del sistema (NAND y partida guardada) y medir el arranque hasta el menú.

## Comprobación de memoria, NAND y menú principal (11 de octubre de 2026)

Un sondeo largo de Dolphin con A automatizada (`boot-probe-20261011-191321-242`, gráficos Vulkan y una captura cada 30 s) dio la referencia. La secuencia es "NOW LOADING", el logo de 2K Sports, el vídeo de introducción, el menú principal y "Game Setup". La comprobación de memoria pasa enseguida.

Fallos corregidos, en orden:

1. **Capa NAND mezclada.**
   - En Wiicompiled había HLE para `NANDInit`, `NANDRead`/`Write`/`Seek`, `NANDCreate`/`Delete`/`CreateDir`, sus versiones asíncronas, `NANDSafeCloseAsync` e `ISFS_OpenLib`. Por otro lado, `NANDOpen`/`SafeOpen` de MLB se ejecutaban traducidos.
   - Además, `ISFS_OpenLib_Initialize` escribe `__fsFd` y el heap IPC con desplazamientos `r13` de MKW. En MLB `__fsFd` está en `0x8031A730`, así que la escritura caía en otra variable del juego.
   - Arreglo: un nuevo tipo de fila `unbind` en `config/wiicompiled/rl8e54-data.csv`, que el rebind respeta. Esos 12 ganchos quedan sin enlazar y MLB ejecuta las bibliotecas NAND e ISFS reales del SDK sobre la capa IOS HLE, que sí es común.
2. **Llamadas IOS asíncronas.**
   - `IOS_OpenAsync`, `IoctlAsync` y el resto están en `hle/ios.cpp`, el grupo `ios`, que estaba deshabilitado.
   - Arreglo: los grupos por defecto de `tools/wiicompiled-translate.ps1` son ahora `os audio storage vi gx input ios`.
3. **ES** (`tools/patches/wiicompiled-mlb-nand-es.patch`):
   - `ES_DIGetTicketView` (27), con la vista del ticket del título en el formato TicketView de Dolphin;
   - `ES_GetConsumption` (22), con 0 entradas, como Dolphin;
   - `ES_GetTitleDir` (29), que necesita el `NANDInit` real;
   - ID de título `00010000/524c3845`, como en Dolphin; el runtime usaba el `00010004` de MKW (`GameProfile::kTitleIdHi`);
   - traza IOS activable con `WIICOMPILED_DEBUG_NAND`.
4. **`SCCheckStatus`** (`tools/patches/wiicompiled-mlb-sc-status.patch`).
   - `OSInit` espera en un bucle activo la lectura asíncrona de SYSCONF.
   - En la consola, la interrupción IPC entrega los callbacks durante ese bucle. El runtime los encola y solo los entrega desde el planificador.
   - Arreglo: una descompilación nativa de `SCCheckStatus` (`0x800D1AB0`) que primero entrega los callbacks pendientes.
   - Sin SYSCONF en la NAND gestionada, el juego usa valores por defecto y arranca igual (comprobado).
5. **`RFLiIsWorking`.** `RFLWaitAsync` (`fn_800876A0`) gira sobre `fn_80087B94`, idéntica a `RFLiIsWorking` de MKW. Wiicompiled ya tenía un HLE que bombea alarmas y callbacks IOS. Se enlaza con una fila `code` y una `range` para el puntero del gestor (`0x8031B168`).
6. **Rendimiento del menú: de 2 a 60 FPS** (`tools/patches/wiicompiled-tlut-reinit-cache.patch`).
   - El muestreo de pila apuntaba a la conversión de texturas con paleta y a la compilación de pipelines (`amdxc64.dll`) en cada frame.
   - Contando en `resolve_static_palette_texture` se vio que cada uso de paleta iba acompañado de un `evict_tlut_object`. MLB vuelve a inicializar su objeto TLUT en cada dibujo, y `CreateHostTlutObj` destruía el anterior. Esa destrucción emitía `DESTROY_TLUT` con un ID que en aurora es de contenido y es compartido, así que tiraba las conversiones que la nueva inicialización iba a reutilizar.
   - Arreglo: al reinicializar, el objeto se sustituye sin destruirlo. Si el contenido cambia, `resolve_tlut_identity` ya sube la versión.

**Otros cambios.**
- `tools/patches/wiicompiled-debug-tools.patch` añade `runtime/src/debug_tools.cpp`, que solo se activa por variable de entorno:
  - `WIICOMPILED_DEBUG_MEM1=<prefijo>[,s]` vuelca MEM1;
  - `WIICOMPILED_DEBUG_WATCH=<addr>` y `WIICOMPILED_DEBUG_WATCH_VALUE` ponen un watchpoint hardware en todos los hilos y en las vistas host y guest, e imprimen la pila con offsets para `llvm-symbolizer`.
- `rea` no admite PowerPC: rechaza el DOL y el ELF que genera `tools/dol_to_elf.py`.

**Resultado observado tras un `-Fresh` con 11 parches** (312 ganchos enlazados, 309 sin enlazar):
- el arranque pasa por la correa, la carga del REL, la comprobación de memoria, el menú principal y el submenú "Power Pros via Wii Remote" con las pulsaciones automáticas;
- los menús van a 60 FPS.

Es comportamiento observado, no validado contra Dolphin píxel a píxel. El audio y el juego en sí siguen sin comprobar.

**Siguiente paso (resuelto en «Dentro del juego» y las secciones siguientes):** guiar las pulsaciones hasta Exhibition y un partido, y comprobar render y entrada en el juego.

## Dentro del juego: modo Home Run (11 de octubre de 2026)

- **Nunchuk virtual.** `WIICOMPILED_KPAD_EXT=nunchuk` le conecta un Nunchuk al mando del script; C es `0x4000` y Z es `0x2000`. Exhibition, Home Run y el resto de modos lo exigen ("This mode requires the Nunchuk or the Classic Controller"). Sin Nunchuk, el menú propone el modo "Power Pros via Wii Remote".
- **Fallo del traductor en tablas de salto.**
  - Síntoma: el runtime se detuvo con "Missing indirect jump target 0x8057BFAC".
  - Causa: en `func_8057BEEC`, r0 vale primero `r4 - 13` (comparado con 2) y después `slwi r0, r3, 2` indexa una tabla de 10 entradas, acotada por `cmplwi r3, 9`. `JumpTableDetector.TryFindUpperBound` buscaba hacia atrás una comparación sobre el registro índice sin parar al llegar a su redefinición, así que tomó el `cmplwi r0, 2` antiguo y generó solo 3 casos.
  - Arreglo: al encontrar el desplazamiento que forma el índice, la búsqueda sigue al registro de origen, y cualquier otra escritura la corta.
  - Cambió la traducción de 67 funciones. Hay una prueba de regresión, `IgnoresCompareOnAnOlderValueOfTheIndexRegister`, y pasan las 643 pruebas del traductor. Va dentro de `tools/patches/wiicompiled-masked-jump-tables.patch`.
- **Resultado observado** con A cada 6 s y el Nunchuk conectado:
  - pasa por el título y el menú y entra en Practice/Home Run;
  - selección de equipo y de estadio;
  - "NOW LOADING" y el modo Home Run en juego: lanzamientos, "STRIKE 70 mph", contador de lanzamientos, récords y bateador (Lugo);
  - todo a 60 FPS.
- **Problema abierto: la geometría 3D del estadio y los jugadores sale rota** (polígonos en astillas y picos). El HUD 2D y los menús se ven bien. Resuelto en la sección siguiente.

## Geometría 3D corregida (11 de octubre de 2026)

**Diagnóstico.** Bisección con interruptores temporales en `GX__CallDisplayList`, ya retirados:
1. Saltar todas las display lists deja solo el HUD. Todo el 3D va por display lists; el HUD va por escrituras directas al FIFO.
2. Forzar el parser HLE (`ParseDisplayList`) para todas las listas dibuja el 3D bien. El fallo está en la ruta directa, en la que aurora decodifica la lista.
3. La caché de escaneo de listas no influye.
4. Forzar el parser solo para un formato de vértice aisló el culpable: `PNMTXIDX` directo con POS, NRM, CLR0 y TEX0 en `GX_INDEX16`. Es el formato de los modelos con huesos.
5. Solo las ejecuciones rotas registran unos 1,9 millones de avisos de aurora `XF: PosMtx sub-copy unsupported`, con longitudes como 1, 17 o 23. Aurora se desincronizaba al recorrer la lista y leía bytes de vértice como cargas XF.

**Causa.** `ApplyAuroraVtxDesc` (`gx_dl.cpp`) guarda un espejo del descriptor de vértices ya publicado en aurora. Solo comprobaba contra el estado real de aurora (`GXGetVtxDesc`) los atributos indexados y se saltaba los índices de matriz. Otras rutas cambian el descriptor de aurora sin pasar por el espejo. Con `PNMTXIDX` desactivado en aurora pero marcado como publicado, aurora calculaba vértices de 8 bytes en lugar de 9.

**Arreglo.** `tools/patches/wiicompiled-dl-vtxdesc-live-check.patch`, el parche número 12: compara todos los atributos hasta `GX_VA_TEX7` con el estado real. Es una lectura de registro sombra por atributo.

**Resultado observado** tras recompilar limpio:
- en el modo Home Run se ven bien el estadio (gradas, marcador, césped, tierra), el bateador, el lanzador y la pelota;
- 60 FPS;
- 0 avisos `sub-copy`.

No está validado píxel a píxel contra Dolphin.

**Técnica reutilizable.** Saltar o desviar por ruta y por formato es una bisección barata cuando el HLE tiene dos caminos para lo mismo. El log de avisos de aurora delató la desincronización.

## Mando de GameCube, mando de PS5 y teclado (11 de octubre de 2026)

**El juego admite mando de GameCube.**
- El código del juego llama a `PADInit` y `PADRead` desde 0x80040B14/0x80040BC8.
- La pantalla inicial dice "If you prefer using the Nintendo GameCube Controller, please connect it while this screen is displayed".

**Mando de PS5.** El HLE `PAD__Read_HLE` usa el `PADRead` de aurora, que lee cualquier mando SDL. Un PS5 por DS4Windows llega como Xbox 360 o DualSense y se mapea a GameCube:

| PS5 | GameCube |
|---|---|
| ✕ | A |
| ○ | B |
| □ | X |
| △ | Y |
| Options | Start |
| R1 | Z |
| L2/R2 | L/R analógicos |
| Sticks | Stick principal y C-stick |
| Cruceta | Cruceta |

**Teclado.** Parche número 13, `tools/patches/wiicompiled-mlb-keyboard-gcpad.patch`:
- **Activación:** aurora traía el teclado sin teclas asignadas y, si se activaba, anulaba el mando en ese puerto. Ahora el teclado viene activado en el puerto 1 y se suma al mando: los botones se combinan con OR y los ejes se suman.
- **Teclas por defecto:**

| Tecla | GameCube |
|---|---|
| J | A |
| K | B |
| L | X |
| I | Y |
| Enter | Start |
| U | Z |
| Q | L |
| E | R |
| WASD | Stick principal |
| TFGH | C-stick |
| Flechas | Cruceta |

- Se reasignan en el menú F10. Si existe `keyboard_bindings.dat`, se usa en lugar de los valores por defecto.
- **Guion para pruebas automáticas:** `WIICOMPILED_PAD_SCRIPT="ms-ms:hexbotones,..."` simula un mando de GameCube en el puerto 0.

**Verificado:**
- solo con pulsaciones A del guion PAD, sin Wiimote, se llega al menú principal con el icono de GameCube activo;
- igual solo con la tecla J, enviada a la ventana del juego;
- no se ha probado con un PS5 físico.

## Entrada fantasma: constante int→float pisada por el HLE de STM (11 de octubre de 2026)

**Síntoma.** El cursor del menú se movía solo y las pantallas avanzaban sin pulsar nada, con o sin mando.

**Diagnóstico:**
1. El registro de `PADRead` mostraba el mando neutro.
2. Los objetos de entrada del juego son clases con vtable: GC pad en `base+1104`, KPAD en `base+560` y combinado en `base+16`, donde `base` = buffer de `PADRead` − 1648. El objeto GC marcaba arriba, derecha y L/R pulsados de forma continua.
3. Su anillo de muestras tenía todos los ejes a 1.0.
4. `func_80040268` convierte s8/u8 a float con el truco de CodeWarrior: escribe `0x43300000` y el entero en la pila, los lee con `lfd` y resta una constante mágica de `.sdata2`. Una sonda temporal en el shard generado mostró la pila correcta, pero la constante en `r2-31664` = `0x8031BD90` valía `0x4330000000000001` en vez de `0x4330000080000000`.
5. Un volcado de MEM1 comparado con el DOL dio solo 3 palabras distintas en `.sdata2`, `0x8031BD94..9C`, con los valores `1`, `0x00535401` y `0x00535402`.

**Causa.** El HLE `__OSInitSTM` (`hle/os/os_init.cpp`) escribía las variables STM en offsets de r13 de MKW (`r13-0x62cc/-0x62c8/-0x62c4`). En MLB esas direcciones caen sobre esa constante. En MLB, `__OSInitSTM` usa `r13-0x6DDC/-0x6DD8/-0x6DD4`.

**Arreglo.** Parche número 14, `tools/patches/wiicompiled-mlb-stm-sda.patch`, que añade `GameProfile::kStmSdaOffset`.

**Alcance.** Toda conversión int con signo → float que usara esa constante devolvía basura. Probablemente explica también la navegación "errática" vista antes con el Wiimote virtual.

**Verificado:**
- las constantes quedan intactas;
- el anillo del mando queda a 0 en reposo;
- el menú no se mueve;
- el título espera "Press +" y, sin pulsarlo, arranca la demo, igual que el original.

## Cierre al fildear, respuesta del stick y FPS (11 de octubre de 2026)

**Cierre al fildear.**
- Error: `missing_jump_target 0x8092F488`.
- Causa: `func_8092F450` hace un `switch` sobre un `lbz` sin `cmplwi`. `JumpTableDetector` tomó como límite un `cmpwi r0, 0` cercano que comprueba otra cosa, y generó 1 caso de 9.
- Arreglo: parche número 15, `wiicompiled-byte-index-jump-tables`. Si el índice sale de una carga de byte, lee la tabla hasta la primera entrada que no es destino.
- Pasan 644 pruebas, incluida `ReadsByteIndexedTableUntilFirstNonTarget`. La retraducción cambió 38 archivos.
- Auditoría: de los 538 `switch` generados, los únicos de un caso tienen de verdad una entrada.

**Stick.**
- Parche número 16, `wiicompiled-mlb-pad-stick-range`: escala la deflexión completa a la compuerta real de GameCube, 87 en el stick principal y 74 en el C-stick (valores de Dolphin). MLB divide entre 88 y entre 64, así que con ±127 saturaba al 69 % del recorrido.
- Parche número 17, `wiicompiled-pad-radial-deadzone`: la zona muerta de aurora era por eje, de 8000, y saltaba de 0 a ~24 %. Ahora es radial, reescalada y de 3000 por defecto.

**FPS al fildear.**
- Medida: la demo automática del título (sin pulsar Start) reproduce la escena. Un registro temporal de `aurora_get_present_timing` y `aurora_get_stats` dio:
  - con D3D12, 60 FPS a 200-450 draws y 17-30 FPS a 660-990 draws (vista de fildeo), sin compilar pipelines;
  - el hilo del juego dormía en `VI_HLE_WaitForNextRetracePoll`, el de render estaba bloqueado en el driver (`amdxc64`) y la GPU estaba al 20-35 %.
- Con Vulkan, la misma escena de 985 draws va a 60 FPS fijos.
- Arreglo: parche número 18, `wiicompiled-prefer-vulkan`. El modo `auto` prefiere Vulkan en Windows y cae a D3D12 si no hay Vulkan.
- Herramientas:
  - `tools/sample_host_thread.cpp` acepta un argumento `RANK` (el N-ésimo hilo más ocupado) y hay que compilarlo con `-static`;
  - los scripts de histograma y cadenas de llamada están en el scratchpad.

## D3D12 a 60 FPS y cuelgue en "NOW LOADING" (11 de octubre de 2026)

**Método de medida.**
- `tools/wiicompiled-bench.ps1` lanza el juego con una A guionada del mando de GameCube y deja correr la demo del título.
- El runtime, con `WIICOMPILED_BENCH=minDraws,segundos,esperaMax`, espera a un fotograma con al menos `minDraws` draws y mide solo esos fotogramas: p50, p95, p99, máximo y % de fotogramas lentos.
- También imprime contadores de aurora por fotograma (`aurora_get_frame_waits`): espera de staging, *splits*, codificación, adquisición, `Submit`, espera de ritmo, `Present`, lecturas EFB síncronas y texturas creadas. Escribe `bench_result.txt` y sale.
- Reintenta si la escena no aparece y solo captura la ventana de los intentos fallidos.
- La medida cronometrada desde fuera y con capturas no servía: `PrintWindow` provocaba tirones y la escena tardaba un tiempo variable en llegar.

**Cuelgue en "NOW LOADING"** (se reproducía en 4 de 4 intentos).
- **Hilos:** el hilo cargador (`0x80027A54`) duerme en una cola que despierta un hilo reloj (`0x80027BF8`, `OSSleepTicks` de 5 ms y luego `OSWakeupThread`). El temporizador del reloj estaba vencido hacía 67 s y sin procesar: `ProcessSleepTimers` solo corre en `SelectThread`.
- **Por qué no se procesaba:** durante la carga, el hilo principal marca el ritmo dentro de `GXCopyDisp` (`VI_HLE_PresentFrame`, un `sleep` del host) y nunca entra en el planificador. Las rutas diferidas de VI, alarmas y audio dejan hilos READY pero nunca cambian de hilo.
- **Arreglo:** parche `wiicompiled-preemption-point`. Al final de `GXCopyDisp`, `OS_HLE_PreemptionPoint()` llama a `SelectThread(0)` si las interrupciones del juego están activas, el planificador habilitado y no se está avanzando un retrazado. Es la vuelta de la interrupción VI del hardware. Resultado: 0 cuelgues en las ejecuciones posteriores.

**D3D12 al fildear: de 27 FPS a 59,9.**
- **Diagnóstico:**
  - con D3D12 el hilo del juego pasaba el 26 % del tiempo en `NtGdiDdDDIWaitForSynchronizationObjectFromCpu`, más `CreateAllocation`;
  - el hilo de render pasaba el 18 % en `DestroyAllocation2` (exportaciones de `win32u` mapeadas con `dllexports.py` en el scratchpad);
  - el contador dio 32 texturas creadas por fotograma, 188×128 "Copy Conv" y "Resolved".
- **Causa:** `GXCopyTex` necesita una textura nueva por copia cuando el destino ya se muestreó en ese fotograma, y MLB hace unas 17 por fotograma. El pool solo guardaba 3 por destino, así que el resto se creaba y destruía en cada fotograma. En D3D12 cada una baja al kernel; en Vulkan es barato.
- **Arreglo:** parche `wiicompiled-copy-texture-pool`, que sube `kCopyTexturePoolPerKey` a 64.
- **Medido:**

| Backend | FPS | p95 | Texturas creadas por fotograma | Codificación |
|---|---|---|---|---|
| D3D12 (`auto`) | 58,6-59,9 | 17,1 ms | 0,4 | 1,7 ms (antes 8,8) |
| Vulkan | 60 | 17,1 ms | 0,2 | 1,4 ms |

- Se retiró el parche que daba prioridad a Vulkan: `auto` vuelve a D3D12 y Vulkan sigue seleccionable en `Config.toml`.
- **Otros:**
  - parche `wiicompiled-perf-instrumentation`: los contadores, el modo benchmark, `AURORA_DAWN_ENABLE_TOGGLES` / `AURORA_DAWN_DISABLE_TOGGLES` y un volcado de hilos que incluye estado, suspensiones, cola y temporizador de sueño de cada `OSThread`;
  - `tools/sample_host_thread.cpp` acepta `RANK`;
  - `tools/wiicompiled-build.ps1` ya no sobrescribe `Config.toml` en cada compilación, solo actualiza `dvd_root`.

**Pendiente:** el `IPCCltInit` HLE usa `r13-26908`, que en MLB coincide con `IPCInit`. Quedan por auditar los demás HLE que usen offsets de r13 o r2 de MKW. *(Resuelto el 14 de octubre: ver «Restos de Mario Kart Wii en el runtime».)*

## Launcher al arrancar (11 de octubre de 2026)

- Parche 21, `wiicompiled-launcher`: pantalla previa al juego con el estilo de los menús de Power Pros. Fondo azul con rayos, píldoras brillantes (JUGAR / AJUSTES / SALIR), título con contorno, caja de información y ayudas de botones.
- **Sin assets del juego:** todo se dibuja con primitivas de ImGui. Las fuentes son del sistema (Bahnschrift, Segoe UI Bold y Arial Black en Windows; DejaVu en Linux). Si no hay ninguna, se usa la fuente de ImGui. La fuente por defecto del overlay F10 no cambia.
- **Ajustes,** en cuatro pestañas:
  - VIDEO: API gráfica (solo las que esta compilación puede crear), pantalla, resolución interna, forzar 16:9 y mostrar FPS;
  - AUDIO: volumen y silenciar;
  - CONTROLES: Wii Remote y el mapa del teclado;
  - INTERFAZ: mostrar al iniciar.
- **Guardado y reinicio:** cada cambio se guarda en `Config.toml` al momento. Al pulsar JUGAR, si algo cambió, el ejecutable se relanza con `WIICOMPILED_SKIP_LAUNCHER=1`. El padre cierra aurora y la transcripción del proceso antes de relanzar.
- **Controles del launcher:**
  - Teclado: flechas o WASD, Enter/Espacio/J para aceptar, Esc/K para volver, Q/E para cambiar de pestaña.
  - Mando: cruceta o stick, A, B, L1 y R1.
  - Ratón: clic izquierdo y derecho.
- **Entrada solo con la ventana enfocada:** SDL entrega los mandos aunque la ventana no tenga foco, y en una prueba eso cambió ajustes. `WIICOMPILED_LAUNCHER_UNFOCUSED_INPUT=1` acepta el teclado sin foco, solo para scripts de prueba que envían teclas con PostMessage.
- **No aparece** con `WIICOMPILED_SKIP_LAUNCHER`, `WIICOMPILED_BENCH`, scripts de mando o `[launcher] show_on_startup = false`. Se puede volver a activar en F10 con "Show launcher at startup".
- **Variables de prueba:** `WIICOMPILED_LAUNCHER_SELECT=<api>` elige la API y pulsa JUGAR; `WIICOMPILED_LAUNCHER_PAGE=settings` abre los ajustes.
- **Verificado con teclas enviadas solo a la ventana:**
  - ruta AJUSTES → MOSTRAR FPS = No → Esc → JUGAR: guarda, relanza y el hijo arranca el juego sin FPS;
  - pestañas con E;
  - selección de SALIR;
  - `LAUNCHER_SELECT=vulkan`: relanza y el juego corre a 59 FPS.
- **Switch (homebrew):** Dawn no tiene backend para Horizon. Opciones: el backend GLES de Dawn sobre la mesa de devkitPro (switch-mesa/EGL), o un backend de aurora propio en deko3d. OpenGL ES aún no se ofrece porque el paquete de Dawn usado viene sin GL; hace falta compilar Dawn desde fuente. En Switch habrá que cargar la fuente del sistema (`plGetSharedFont`).

## Setup para jugadores: compila en la PC del jugador (12 de octubre de 2026)

**Decisión del usuario:** se distribuye como Wiicompiled, no como skate3recomp. La release trae el Setup y las herramientas; el código del juego se traduce y compila en la PC del jugador a partir de su propia ISO. Ni el repositorio ni las releases contienen nada del juego.

**Piezas (`tools/setup/`):**
- `setup.cpp` + `CMakeLists.txt`: ventana del Setup, con SDL3 y ImGui estáticos (exe de 6 MB sin DLLs).
  - Usa el mismo estilo que el launcher: `power_pros_ui.h`, que ahora forma parte del parche 21 y comparten ambos.
  - Pantallas: elegir disco, carpeta de instalación, acceso directo, progreso con barra y log, "¡Listo!" con JUGAR, y error con REINTENTAR y VER REGISTRO.
  - Arrastrar la ISO sobre el exe la preselecciona.
  - `MLBPP_SETUP_AUTOINSTALL=1` instala sin interacción (para pruebas).
- `LocalBuild.ps1`: lo que ejecuta el Setup, en este orden:
  1. `nodtool info`: comprueba que el disco sea RL8E54.
  2. `nodtool extract`.
  3. Compara los SHA256 de `main.dol` y `ovl_main.rel` con los fijados.
  4. Traduce con `Translator.Cli` autocontenido.
  5. Configura y compila con las funciones de `NativeBuildFlags.ps1` de Wiicompiled y el precompilado de aurora y dependencias.
  6. Instala en `<carpeta>\Game` (`MLBPowerPros2008.exe` en modo portable, con `dvd_root` apuntando a `<carpeta>\Data`).
  7. Borra los temporales.
  - Comunica el progreso con líneas `@@ <porcentaje> <mensaje>` y los errores con `@@ERROR <mensaje>`.
  - El archivo va en UTF-8 con BOM, porque PowerShell 5.1 lee como ANSI los scripts sin BOM.
- `Make-Payload.ps1`: arma el paquete desde un árbol parcheado y reasignado, y rechaza archivos del juego. Para una release se usa `Make-Release.ps1`, que prepara ese árbol desde un clon limpio (ver abajo).
  - Sus `Launcher\artifacts` salen de los scripts de Wiicompiled, reutilizados sin cambios: `Prepare-PortableTools.ps1`, `Prepare-Dependencies.ps1` y `Prepare-NativePrebuilt.ps1` (el último compila aurora y las dependencias con nuestros parches).
  - Recorta de llvm-mingw los sysroots que no son x86_64, el Python incluido y el runtime de Linux: el paquete pasa de 1.150 MB a 839 MB.
- `config/wiicompiled/rl8e54-functions.map` (direcciones y nombres) se sube al repo. Así el jugador no necesita dtk, DolRecomp ni la base de firmas de Dolphin. `tools/wiicompiled-translate.ps1 -RegenerateMap` lo vuelve a generar.
- `check_no_game_data.py` acepta este mapa solo si cada línea es "dirección nombre" y si un nombre en hexadecimal coincide con su propia dirección.

**Medido (12 hilos, ISO NKit del usuario):**

| Paso | Tiempo |
|---|---|
| Extracción | 2 s |
| Traducción | 88 s |
| Configuración con el precompilado | unos 20 s (sin él: 367 s, por los chequeos de SDL) |
| Compilación | 117 pasos en lugar de 814 |
| **Instalación completa** | **285 s** |

- La compilación limpia sin precompilado tardaba 274 s; la mitad de la CPU se iba en dependencias.
- **Verificado:**
  - instalación sin ventana y con la ventana del Setup (progreso, pantalla final y temporales borrados);
  - el juego instalado abre el launcher y llega a la pantalla de la correa a 60 FPS.
- **Ventana:** el título del juego pasa a "MLB Power Pros 2008", en `base_product.cpp` dentro del parche 21.

**Pendiente:**
- Probar en otra PC real (hecho en esta, en limpio: ver abajo).
- Publicar la release (`v0.1.0`, beta, solo Windows). El repositorio está en GitHub (privado) desde el 13 de octubre.
- Linux y Switch.

**Paquete desde un clon limpio (15 de octubre):** `tools/setup/Make-Release.ps1 [-Ref <commit>] [-Zip]` lo hace en un paso y solo con lo que está en git:
1. clona el repositorio en `local\rc` (los cambios sin commit no entran);
2. vuelve a crear el árbol de Wiicompiled desde el commit fijado con todos los parches y la reasignación (`wiicompiled-translate.ps1 -Fresh -PrepareOnly`, sin archivos del juego);
3. ejecuta en él `Prepare-PortableTools`, `Prepare-Dependencies` y `Prepare-NativePrebuilt`, así el precompilado de aurora nunca queda viejo;
4. ejecuta `Make-Payload.ps1` del clon.
- Lo que no está en git (clones de Wiicompiled y PowerUp+, SDK de .NET 10, nodtool, cachés) se toma de `local\vendor`, `local\tools` y `local\cache` por uniones de directorio; cada cosa se fija por commit o hash. `powerupplus.ps1` ahora hace checkout del commit `eb1a2c2`.
- **Medido:** unos 20 minutos; paquete de 902 MB, zip de 298 MB.
- **Instalación limpia comprobada** con ese paquete, copiado a una carpeta nueva y con el Setup ejecutado con un PATH mínimo (sin dotnet, Python, git ni las herramientas del proyecto) desde la ISO del usuario:
  - en español y en inglés: 4,5 y 4,3 minutos de principio a fin, sin errores; la ventana del Setup y los mensajes de progreso salen en el idioma elegido;
  - el juego instalado (1,5 GB) abre el launcher y, sin él, llega al logo de KONAMI a 60 FPS con D3D12.
- Queda sin probar otra PC (otra GPU, sin las herramientas instaladas en el sistema) y elegir el idioma con la píldora del Setup, que es lo único que lo escribe en `Config.toml`.

## Launcher con el arte de MoonInning (12 de octubre de 2026)

- **Origen del arte:** el UI pack de MoonInning, aportado por el usuario. Es arte original, no contenido del juego. Trae dos mockups planos de 1672x941 con textos y estados de ejemplo ya dibujados.
- **Preparación:** `tools/launcher/prepare_mooninning_art.py` borra esas partes y genera `assets/launcher/main.png` y `settings.png`:
  - en el menú, las chispas de "seleccionado" junto a JUGAR, con una difusión limitada a sus píxeles amarillos, y el texto de la caja de info;
  - en ajustes, las pestañas, las filas y la ayuda, interpolando desde los bordes suavizados.
- **Lo que dibuja `launcher.cpp` encima:** si encuentra `launcher/*.png` junto al exe, ajusta el arte a la ventana y rellena las barras con el mismo arte ampliado y oscurecido.
  - **Botón elegido:** se redibuja ampliado y con brillo que pulsa, con las chispas a su lado.
  - **Resto de botones:** atenuados. MODS queda deshabilitado y su línea dice "Próximamente". *(Ya no: MODS funciona desde «Mods», 12 de octubre.)*
  - **Caja de info:** muestra el estado real (disco, ID, API en uso, aviso de reinicio).
  - **Ajustes:** pestañas, filas, valores y ayuda se dibujan con los colores del mockup; el botón de cerrar es el del arte.
  - **Sin las PNG:** se mantiene el estilo procedural de antes (por ejemplo en Switch).
- **Instalación de las PNG:**
  - `wiicompiled-build.ps1` las copia a `build/launcher`;
  - `Make-Payload.ps1` las mete en el paquete y `LocalBuild.ps1` las instala en `Game/launcher`.
  - `check_no_game_data.py` solo admite binarios en `assets/launcher/*.png`, de hasta 4 MiB.
- **Verificado con teclas enviadas solo a la ventana:**
  - las chispas siguen a la selección y MODS muestra "Próximamente" (en aquel momento);
  - la entrada a ajustes y el cambio de pestaña funcionan;
  - las barras quedan rellenas en una ventana de 1920x1009.
- **El Setup usa el mismo arte:** `prepare_mooninning_art.py` también genera `setup.png`, que es el fondo del menú con los cuatro botones en blanco.
  - Se borran icono y texto con una mezcla por filas y se conserva la flecha. Los restos del contorno de la pelota se quitan con difusión.
  - El Setup coloca sus acciones por color: naranja para la acción principal (ELEGIR DISCO, INSTALAR, JUGAR o REINTENTAR), azul para la secundaria (CAMBIAR DISCO, REINSTALAR o VER REGISTRO), verde para CARPETA y morado para SALIR o CANCELAR. Los botones sin uso quedan atenuados.
  - En la caja de info van el disco, la carpeta (recortada por la izquierda si es larga), la casilla del acceso directo, el progreso y el resultado. El registro de compilación aparece en un panel a la derecha.
  - Sin `workspace\launcher\setup.png`, el Setup mantiene el estilo procedural.
  - **Verificado:** instalación completa con la ventana del Setup (inicio, progreso y "¡Listo!").

## Mods (12 de octubre de 2026)

- **Parche 22, `wiicompiled-mods`:**
  - `runtime/src/mods.cpp` busca `<exe>/mods/<carpeta>` (con `mod.toml` opcional) y aplica los mods activos de `[mods] enabled` antes de iniciar aurora:
    - `disc/` y `riivolution/` se registran con `RecompMod::RegisterDvdOverlayRoot`, el mecanismo de Wiicompiled que ya usaba Retro Rewind;
    - `textures/` pasa a aurora como carpetas extra de reemplazo.
  - aurora: `AuroraConfig.textureReplacementRoots` y `textureReplacementRootCount`. `build_index` indexa la carpeta del usuario y luego la de cada mod. Esto cambia la ABI de `AuroraConfig`, así que **hay que regenerar `native_prebuilt`** (`Prepare-NativePrebuilt.ps1`) antes de `Make-Payload.ps1`.
  - Launcher: el botón MODS (arte) o la pestaña MODS (estilo dibujado) muestran un interruptor por mod y la fila ABRIR CARPETA DE MODS (crea `LEEME.txt`). Cada cambio pide relanzar el juego.
- **Verificado:**
  - el volcado de texturas exporta 32 texturas en el arranque;
  - un mod local de prueba con la textura de la correa invertida se ve en el juego con el mod activo;
  - la pantalla MODS activa el mod y lo guarda en `Config.toml`.
- Guía para quien hace mods: `docs/mods.md`.

## Identidad de Discord y mensajes (12 de octubre de 2026)

- **Parche 23, `wiicompiled-mlb-identity`:** Discord, la cabecera de la configuración nueva y los errores de disco, panic, halt y exit usan `RuntimeProduct::Active().displayName`, cuyo proveedor MLB devuelve `MLB Power Pros 2008`.
- El perfil `RL8E54` no reutiliza el ID de aplicación de Discord de WiiCompiled. Sin un ID propio, la presencia básica no conecta; el ID heredado también se ignora si una configuración antigua lo guardó explícitamente. Los IDs personalizados y `enabled = false` se conservan.
- El ejemplo de ruta de disco usa `./DATA`. Las referencias técnicas a las direcciones y funciones originales de Mario Kart, así como las licencias y créditos de WiiCompiled, se mantienen.
- Comprobaciones: los 23 parches se aplican en orden sobre la revisión fijada; una prueba aislada del runtime verifica la configuración nueva, los IDs por defecto/heredado/personalizado, la desactivación y el contenido JSON de la actividad de Discord. No requiere conectarse a la cuenta del usuario ni jugar una partida.
- Compilación Release completada. El ejecutable ya no contiene los antiguos mensajes visibles de Mario Kart. Actualizados los seis archivos del runtime en el payload local del Setup y en su ZIP; comprobados su contenido, ausencia de entradas duplicadas e integridad CRC. No se publicó una release ni se probó una conexión real a Discord con un ID propio.

## `project.bin` descifrado (12 de octubre de 2026)

- **Formato:** cabecera (versión 2, sector 0x800, 0x1211 ranuras de hash); registros de 0x1C en 0x6000 (desplazamiento del nombre, sectores, alineación 0x800, bytes); tabla de orden en 0x26000 (registro, sector relativo a 0x3D800, ordenada); nombres en 0x2B000; datos en 0x3D800.
  - Verificado en las 2.424 entradas: tamaños consecutivos, fin exacto en 0x18390000, 2.356 `PACKWII` y 2 `PACKPS2`.
  - rea no abre el archivo (formato no soportado). El análisis se hizo con Python y con el código traducido: `func_800431E8` monta `/PROJECT.BIN` (0x80155DD0) llamando a `func_80059E7C`.
- **`tools/projectbin.py`:** list, extract y build.
  - `build` sin reemplazos reproduce el archivo byte a byte (mismo SHA-256).
  - Con `PWM_mdl_uniform_MLB_CHC_00.pack` reemplazado por el de NYY y servido como `mods/<x>/disc/project.bin`, el juego lee el archivo del mod (`WIICOMPILED_DEBUG_DVD`) y los Cubs salen con uniforme de los Yankees en la selección de equipo.
- **`WIICOMPILED_DEBUG_DVD=1` más `projectbin.py list`:** dicen qué pack carga cada pantalla. Por ejemplo `env_title_logo`, `env_mainmenu`, `PWM_mdl_uniform_MLB_<equipo>_<n>` y `env_teamnameNN`.
- **Pack de texturas de Dolphin del usuario** (logos y uniformes 2023, 206 PNG): funciona como `textures/` de un mod sin cambios; 32 de sus texturas se usan en la demo.
- **Paquetes de `project.bin` en mods (parche 22):** la carpeta `project/` de cada mod activo trae `.pack` por nombre.
  - `mods::Apply` genera `UserData/Cache/mods/disc/project.bin` con un port en C++ de `projectbin.py build` y lo registra como overlay del disco.
  - Se reconstruye solo si cambian el original o algún pack (tamaño y fecha en `project.bin.sources`).
  - **Medido:** 656 ms la primera vez y reutilizado después. El SHA-256 es idéntico al de la herramienta de Python, y los Cubs aparecen con el uniforme de los Yankees.

## Partidas por roster (12 de octubre de 2026)

- **Parche 24, `wiicompiled-mod-saves`:** los mods que cambian el roster (`roster/` o `project/game_data.pack`) llevan la carpeta de partidas del juego a `ModSaves\<mods>`. Así una carrera de MLB Life empezada con un roster no se mezcla con las partidas normales. Detalles en `docs/mods.md`.
- Se encontró que `CurrentNandDataDir()` deja de valer una vez que el juego corre: lee el código de juego en 0x80000000, que el juego sobrescribe, y cae a `RMCP`. El redireccionador de mods toma la carpeta del título desde la ruta. El de Riivolution conserva su comportamiento.

## Editor de rosters en el launcher (12 de octubre de 2026)

- **Parche 25, `wiicompiled-roster-editor`:**
  - La pantalla MODS tiene dos pestañas: **MODS** (solo los mods, con su interruptor) y **HERRAMIENTAS** (abrir la carpeta de mods y el editor de rosters). Q/E cambian de pestaña.
  - HERRAMIENTAS → **EDITAR ROSTER (PowerUp+)** arranca el PowerUp+ instalado en `powerupplus\` en "modo 2008", con el `project.bin` del jugador y la carpeta `mods\`, dentro de un *job object* para que se cierre con el juego. Abre una página local que espera al servidor y luego lo muestra.
  - Las listas largas del launcher van por páginas de 6 filas, con "Página 1 de 2" y flechas clicables en la cabecera. Re Pág / Av Pág cambian de página, y bajar desde la última fila también. La rueda del mouse mueve la selección. Antes, con muchos mods, las filas se salían del panel.
  - La ayuda larga reduce su letra para no salirse del panel.
  - En ventanas con otra proporción que el arte, los bordes muestran una copia difuminada y oscurecida, con una sombra suave. Antes se veía un trozo repetido del arte. El Setup hace lo mismo, con el difuminado calculado en la CPU.
  - Ganchos para pruebas sin teclado: `WIICOMPILED_LAUNCHER_ROW`, `WIICOMPILED_LAUNCHER_PAGE=tools` y `WIICOMPILED_LAUNCHER_ACTION=roster-editor`. Las teclas simuladas con `WIICOMPILED_LAUNCHER_UNFOCUSED_INPUT` también leían el teclado real.
- **Setup:** `Make-Payload.ps1` incluye PowerUp+ compilado (`tools/powerupplus.ps1 -Publish`, unos 113 MB) con su licencia, el commit y el parche. `LocalBuild.ps1` lo instala en `Game\powerupplus\`. El control de contenido del juego también rechaza `game_data.pack` y los `.dat` de rosters.

## JUGAR sin cambiar de ventana (13 de octubre de 2026)

- **Parche 26, `wiicompiled-launcher-live-settings`:** antes, cambiar cualquier ajuste en el launcher hacía que JUGAR cerrara la ventana y abriera otro proceso, porque el runtime lee la configuración antes de crear la ventana.
  - Pantalla, resolución interna, forzar 16:9, mostrar FPS, volumen y silenciar ya no reinician: `main.cpp` y `settings_overlay::InitializeRuntimeSettings` los vuelven a leer de `Config.toml` después del launcher, igual que el menú F10 los cambia en vivo. El modo de pantalla se aplica al momento, en el propio launcher.
  - Siguen reiniciando la API gráfica (hace falta otro renderer), el Wii Remote (pistas de SDL previas a su inicio) y los mods (aurora indexa las texturas y el disco al iniciar).

## Cierre en Exhibition → LINEUP (13 de octubre de 2026)

- Error: `missing_jump_target 0x80664C30`, desde `func_80663264` (llamada por puntero, `bctrl`).
- Causa: en `0x80664C18` hay cinco funciones seguidas que el mapa de dtk tenía como una sola. `C18` suma 4 y salta a `C38`; `C20`, `C28` y `C30` son *getters* de dos instrucciones (`lwz`/`addi` + `blr`) que solo se llaman desde una *vtable* en `0x80D5183C`. Al no estar en el mapa, no se traducían como funciones.
- Arreglo: `0x80664C20`, `0x80664C28`, `0x80664C30` y `0x80664C38` añadidas a `config/wiicompiled/rl8e54-functions.map`. La retraducción pasa de 48.219 a 48.223 funciones.
- La pantalla se alcanzó con el roster de un mod que ahora muestra nombres (antes la alineación salía vacía).
- Pendiente: puede haber más funciones así en otras *vtables*. Un rastreo de punteros a mitad de función encontró unos 450 candidatos, pero mezclados con tablas de `switch`. Partir funciones en etiquetas de `switch` rompería el código, así que no se añadieron en bloque.

## Carga de GPU a x4 (13 de octubre de 2026)

- **Parche `wiicompiled-native-efb-snapshot`:** con interpolación desactivada, una copia de color que limpiaba el EFB primero copiaba la fuente a una textura intermedia y después hacía la copia solicitada. El encoder ya ordena la copia solicitada antes del siguiente pase que limpia el EFB; esa instantánea adicional es redundante. Se omite en la ruta nativa y se conserva con interpolación activada. No se cambia la resolución, el filtrado, los efectos, los mods ni el ritmo del juego.
- Se añadieron contadores acumulados `efbSnapshotCopies`/`efbSnapshotBytes` a `AuroraFrameWaits`. El test gráfico real exige cero instantáneas adicionales en la ruta nativa y su conservación con interpolación. Primero falló con el código anterior; después pasó la comprobación de píxeles independientes y toda la batería existente: copias con limpieza parcial, lecturas síncronas/asíncronas, offscreen, cortes de staging y worker con interpolación.
- **Medición local, Radeon RX 5500, Direct3D 12, x4 y mismos mods/configuración:** media de las últimas 20 muestras de actividad 3D del proceso, tomadas aproximadamente cada 1,3 s: 75,85 % antes y 69,60 % después. A x1, la referencia fue 34,15 %. Son ventanas de la demo del juego; no son una comparación de una cámara idéntica ni una garantía para todos los partidos. La carga sigue siendo considerable.
- Las pruebas usan un ejecutable y datos de usuario aislados en `local/analysis/gpu-x4`. Registros y CSV quedan allí. El ejecutable actualizado para jugar está en `local/wiicompiled-mlb/build/WiiCompiled.exe`; la configuración del usuario sigue en x4 y API automática.
- No se confirmó la hipótesis de renderizado sin límite: los contadores de presentación indican aproximadamente 60 presentaciones/s. El benchmark que filtra por 600 draws recogió demasiado pocos frames para interpretar sus percentiles/FPS como rendimiento del partido; se conservan sus registros, pero no se usan para esa conclusión.
- El texto del launcher «4x (1440p)» no describe la resolución interna de Aurora: su workspace EFB incluye 640×528 y el modo 16:9 puede expandirlo. No se rebajó x4 para alcanzar 1440p.
- **Distribución:** este cambio modifica Aurora y amplía `AuroraFrameWaits`; antes de crear un nuevo Setup hay que regenerar `native_prebuilt` y el payload. El Setup/ZIP anterior no se actualizó en esta intervención.

## Prueba de partido directo para medir GPU (13 de octubre de 2026)

- **Parche `wiicompiled-match-benchmark`:** con `WIICOMPILED_MATCH_BENCH=1`, el runtime salta menús y videos y arranca un partido CPU contra CPU (Yankees contra Colorado en Coors Field) con las rutinas originales de inicialización del juego, llamadas desde el gancho de `VIWaitForRetrace`. Sin esa variable el juego no cambia.
  - Panel en pantalla: x1 a x4, forzar 16:9 y F10, FPS, tiempo GPU y "Empezar a medir" / "Detener y guardar" (la medición dura lo que el usuario quiera; antes paraba a los 30 s). Debajo, y al inicio del informe `.txt`, va el equipo: CPU y hilos, RAM, GPU y sistema (parche `wiicompiled-benchmark-device-info`). También el uso en vivo, muestreado cada 0,5 s en un hilo aparte: CPU del juego (sobre todos los hilos, como el Administrador de tareas), RAM del juego (conjunto de trabajo), GPU 3D y VRAM (contadores de GPU de Windows, `pdh.dll` cargada al vuelo) y RAM del sistema en uso. El informe guarda media y pico; el CSV, una columna por valor. Comprobado contra el monitor del script: GPU 3D 34,5 % frente a 34,4 % (x1) y 75,6 % frente a 76,1 % (x4). La RAM del juego es el conjunto de trabajo (~0,67 GB); la memoria privada que registra el script (~5,2 GB) incluye memoria reservada para el juego que no está en uso. Los cambios son temporales: en este modo `Config.toml` no se escribe.
  - Informe en `UserData/Benchmarks`: CSV por fotograma (dibujos, tiempo GPU, codificación CPU, copias EFB y sus píxeles, lecturas, presentaciones) y resumen con FPS y percentiles.
  - Tiempo GPU: consultas de *timestamp* de WebGPU alrededor del flujo de render sellado. No incluye el presentador. Si el adaptador no las admite, se informa "no disponible".
- **Uso:** `tools/benchmark-match.ps1 -Scale 1,4 -Force169 on -Seconds 30` mide sin intervención y guarda además la utilización 3D del proceso según Windows. `tools/benchmark-match.cmd` abre el modo con el panel.
- **Medición, Radeon RX 5500, Direct3D 12, 16:9, 30 s en el campo** (carpeta aislada `local/analysis/gpu-x4`):

  | | x1 | x4 |
  |---|---|---|
  | FPS | 59,5 | 60,0 |
  | GPU por fotograma | 3,6 ms | 11,7 ms |
  | Utilización 3D (Windows, últimas 20 muestras) | 31 % | 73 % (máximo 80 %) |
  | Copias EFB por fotograma | 34,6 | 34,6 |
  | Píxeles copiados por fotograma | 1,4 M | 22,9 M |

  - El juego no dibuja fotogramas de más: va a 60 por segundo. A x4 la GPU trabaja el 70 % de cada fotograma. Unas 35 copias del EFB por fotograma escalan con la resolución, 16 veces más píxeles a x4.
  - Las dos sesiones llegaron solas al campo, con los mismos equipos y estadio, y los dibujos variaron entre 208 y 1.012 (el partido se mueve). Es juego automático de la CPU, no una repetición exacta fotograma a fotograma.
- **Pendiente:** saber qué parte de los 11,7 ms son esas copias y cuál el dibujado en sí. *(Resuelto en «Por qué x4 carga tanto la GPU»: casi todo es dibujado.)*

## Por qué x4 carga tanto la GPU (13 de octubre de 2026)

- **Desglose por pase** (`WIICOMPILED_GPU_BREAKDOWN=1`, parte de `wiicompiled-match-benchmark`): marca de tiempo al inicio y al final de cada pase de render. El tiempo dentro del pase es dibujado; el hueco hasta el siguiente es la copia o conversión del EFB que los separa. Cada 300 fotogramas escribe en el registro la media por fotograma y los pases más caros.
- **Resultado en el partido, RX 5500, D3D12, 16:9:**

  | | x1 | Según la ventana (1920×1009) | x4 |
  |---|---|---|---|
  | Resolución interna | 939×528 | 1794×1009 | 3755×2112 |
  | GPU por fotograma | 4,0 ms | 8,8 ms | 12,3 ms |
  | Dibujado | 2,7 ms | 8,0 ms | 11,1 ms |
  | Copias y conversiones EFB (~32) | 0,9 ms | 1,0 ms | 1,1 ms |
  | Utilización 3D (Windows) | 35 % | 60 % | 78 % |

- **Causa:** x4 no es 1440p. El EFB tiene 528 líneas y en 16:9 se ensancha a 939, así que x4 dibuja 3755×2112: 7,9 millones de píxeles, casi 4K y 2,1 veces 1440p. Después se reduce a la ventana. El 90 % del tiempo es dibujado; las copias del EFB pesan poco.
- En las pantallas de carga, tres pases con efectos a pantalla completa (15, 7 y 3 dibujos) cuestan 9-10 ms a x4 y 0,75 ms a x1: 13 veces más para 15 veces más píxeles. Escala con los píxeles.
- Los formatos del EFB son normales (color de 8 bits, profundidad de 32 bits) y MSAA está desactivado.
- El porcentaje de Windows y los milisegundos no son proporcionales al trabajo: la GPU baja reloj cuando le sobra tiempo (por eso "Según la ventana" no baja tanto el porcentaje). La temperatura depende de la potencia, no del porcentaje.
- **Arreglo, parche `wiicompiled-resolution-labels`:** el launcher decía "2x (720p)", "3x (1080p)" y "4x (1440p)". Ahora dice "2x (~1080p)", "3x (~1600p)" y "4x (~4K)", y la ayuda recomienda "Según la ventana", que dibuja a la resolución de la ventana.
- `tools/benchmark-match.ps1 -Scale 0` mide el modo "Según la ventana".

## Audio con chasquidos y cortes (13 de octubre de 2026)

- **Síntomas:** chasquidos, comentaristas que se cortan y audio del menú que a veces arranca tarde.
- **Medición** (`WIICOMPILED_AUDIO_STATS=1`, cada 2 s: bloques producidos, huecos entre ticks, nivel de la cola de SDL, descartes y vacíos), en el partido directo de 60 s:
  - El juego produce exactamente el audio que toca (2 s cada 2 s, bloques de 3 ms a 32 kHz).
  - El hilo del juego se detiene 40-93 ms en cargas y momentos de mucho trabajo. Durante esas pausas la cola se vaciaba casi del todo (28 bytes, 0,2 ms): corte audible.
  - Después de cada pausa el juego se pone al día de golpe y ese retraso se acumulaba (la cola pasó de ~30 a ~105 ms). Al llegar al límite de 120 ms se descartaban bloques: el aviso "output queue full" aparece en 38 sesiones.
  - Al arrancar, la cola empezaba casi vacía.
- **Arreglo, parche `wiicompiled-audio-queue`** (`AudioBackend`):
  - El dispositivo se abre en pausa y empieza a sonar cuando hay 80 ms en cola.
  - Para mantener la cola cerca de 80 ms, la velocidad de reproducción se ajusta como mucho ±2 % con `SDL_SetAudioStreamFrequencyRatio`, sin cortar audio. En la prueba se movió entre 0,998 y 1,002.
  - El límite pasa de 120 a 250 ms: solo frena un crecimiento descontrolado.
  - Si la cola se vacía, se pausa hasta volver a tener 80 ms: un silencio corto en vez de chasquidos.
- **Resultado, misma prueba:** la cola se mantiene en 65-80 ms. En las pausas de 40-50 ms del partido no baja de 16-40 ms. No hay descartes. Solo una pausa de 88 ms al cargar el partido la dejó casi vacía.
- No se comprobó a oído en ese momento. *(Confirmado después: ver el final de «Chasquidos del comentarista».)*

## Chasquidos del comentarista (13 de octubre de 2026)

- Después del arreglo de la cola, el chasquido seguía en la voz del comentarista, sobre todo al leer la alineación y los nombres. La cola ya no se vaciaba: el defecto estaba en la mezcla.
- **Mediciones** (diagnósticos locales: `WIICOMPILED_AUDIO_DUMP=archivo` graba la salida; el volcado por voz y la traza de eventos de voz no se conservan):
  - En 119 s de partido hay unos 27 saltos bruscos en la salida. Sin el hilo de mezcla salen los mismos (28), así que no es una carrera. Con un solo bloque por tick también (29), así que no es la recuperación tras las pausas.
  - El comentarista usa dos voces *stream* (`stream=1`) con dos búferes alternos, uno por frase o nombre. Los datos de cada búfer no cambian mientras suena: la carga del disco está bien.
  - El juego para cada frase con `running=0`, 2-4 fotogramas (6-12 ms) después de que se acaban sus datos. Mientras tanto el decodificador ADPCM lee ceros y se queda en el último nivel (por ejemplo 16614); al parar, cae de golpe a 0. En una de las dos voces pasó 38 veces.
  - Las voces del comentarista llegan a ±32767 (113 y 57 veces); la música no. Puede ser la mezcla original del juego: falta una referencia para saberlo.
- **Parche `wiicompiled-audio-depop`:** el mezclador guardaba el último valor de cada voz por bus (`dpop`) pero no lo usaba, igual que Dolphin. Ahora, cuando el juego para una voz que sonaba, ese valor baja a cero en un fotograma (3 ms) en vez de cortarse. El volcado `WIICOMPILED_AUDIO_DUMP` se incluye para comprobaciones futuras.
- No se pudo demostrar con números que desaparezca lo que se oye: en la salida mezclada, la multitud y la música tapan ese patrón. Falta escucharlo, y comparar con Dolphin la saturación del comentarista.
- **Seguimiento, mismo día:** con el depop, el chasquido se seguía oyendo en la prueba directa. Se descartó más (diagnósticos locales, no conservados):
  - **Salida real:** se capturó lo que llega al dispositivo con `SDL_SetAudioPostmixCallback` (44,1 kHz, 8 canales). Con control de velocidad, 15 saltos en 65 s; sin él, 18: el mismo ritmo que antes de SDL. No hay silencios. SDL y el control de velocidad no añaden nada.
  - **Saltos dentro de la voz:** caen todos en datos contiguos, sin cambios de dirección. La voz está a 22.050 Hz (`ratio 0xb066`) y se convierte a 32 kHz con interpolación lineal (`src_type 1`), como el DSP. Muchos "saltos" son consonantes del habla.
  - **Saturación:** se guardaron 12 clips crudos del comentarista con sus coeficientes y se decodificaron aparte con el algoritmo DSP-ADPCM estándar. 11 se salen de rango por sí solos (10-390 muestras cada uno). Está en los datos del juego, no en nuestro decodificador. La salida final no satura (pico 26.521).
  - Pendiente: comparar a oído con Dolphin o con la consola, para saber si lo que se oye es como suena el original.
- **Causa encontrada (ingeniería inversa), parche `wiicompiled-periodic-sleep`:** en la grabación de Dolphin del usuario hay 3,1 saltos/min; en la nuestra, 13-15. El juego paraba cada frase del comentarista tarde (factor 1,2-1,7), así que la voz tocaba el relleno de ceros y el inicio de la frase siguiente de su búfer, y luego se cortaba.
  - El reloj de cada frase está en el objeto de stream: la cabecera de la frase trae su duración en ms (`+0x10`), `func_80039B94` la copia a `[obj+0x874]`, y `func_8003A248` suma 5 a `[obj+0x858]` en cada vuelta del hilo de sonido; al llegar a duración − 10 la frase se para.
  - El hilo de sonido (`0x80027A54`, prioridad 4) da una vuelta por cada `OSWakeupThread` de un hilo temporizador (`0x80027BF8`, prioridad 3) que hace `OSSleepTicks(5 ms)` en bucle. En consola son 200 vueltas por segundo.
  - Medido en nuestro runtime: 110-170 sueños de 5 ms por segundo. Los hilos del juego son fibras cooperativas y el temporizador solo se atiende cuando algún hilo llega al planificador, con milisegundos de retraso; cada sueño contaba desde "ahora" y ese retraso se perdía.
  - Arreglo: un `OSSleepTicks` repetido con la misma duración se ancla al plazo anterior (máximo 50 ms de recuperación); los demás sueños empiezan desde ahora, como en el SDK. El retraso se recupera mientras los otros hilos esperan. El plazo anclado nunca queda a menos de un cuarto del periodo desde ahora: un plazo ya vencido podría dispararse antes de que el hilo termine de aparcarse (hubo un cuelgue al arrancar en 1 de ~12 pruebas sin ese límite; con él, 9 de 9 bien y 2,5 saltos/min).
  - **Inicio del partido:** el usuario aún oía chasquidos un momento al principio, cuando los jugadores van a sus posiciones. Medido segundo a segundo: el temporizador bajaba a 130-180 despertares/s entre los segundos 34 y 47, porque el hilo principal calcula hasta 17-20 ms seguidos (un fotograma entero) sin ceder el turno. Además, el límite de un cuarto de periodo movía el ancla y tiraba la deuda acumulada.
  - Arreglo: (1) el ancla lógica guarda la deuda completa; solo el temporizador real respeta el cuarto de periodo. (2) Cuando el código del juego reactiva las interrupciones (`OSRestoreInterrupts`/`OSEnableInterrupts`) y hay un temporizador vencido, se cede el turno ahí mismo, como hace la consola al tomar la interrupción pendiente (`OS_HLE_PreemptionPoint`, el mismo mecanismo que ya usa `GXCopyDisp`). Solo en las llamadas desde el juego; el C++ del runtime sigue usando las versiones normales. Un atómico con el plazo más próximo evita bloquear en cada llamada.
  - Resultado: 200-203 despertares/s todo el tiempo (solo se pierden ~15 en el segundo de la carga del campo), 0 chasquidos en 69 s, 1,5/min en 90 s sin ninguno al inicio, 60 FPS en x1 y x4, 8 de 8 arranques bien.
  - Resultado, mismo partido directo: 200-201 sueños/s, **3,0-3,4 saltos/min** (Dolphin 3,1), 59,96 FPS a x1.
  - **Confirmado a oído** por el usuario en juego normal el 13 de octubre de 2026: el chasquido del comentarista ya no se oye. La comparación con Dolphin de la saturación propia de los datos sigue sin hacerse.

## Texture Studio (14 de octubre de 2026)

- **Power Pros Wii 2008 Texture Studio** (`tools/texture-studio`, guía en `docs/texture-studio.md`): app aparte para crear mods de texturas. Lee el `project.bin` del jugador, muestra las 9.657 texturas distintas por categorías, las exporta a PNG e importa las editadas (también en HD) como `tex1_…` en `mods/<mod>/textures/`.
- **Integración:** el launcher la abre desde HERRAMIENTAS → EDITOR DE TEXTURAS (parche `wiicompiled-texture-studio`), el Setup la instala junto al juego y `wiicompiled-build.ps1` la deja junto a `WiiCompiled.exe`.
- **Captura en juego:** `WIICOMPILED_TEXTURE_DUMPS=1` activa el volcado de texturas solo para esa ejecución, sin tocar `Config.toml`.
- **Verificado en el juego:** el atlas de etiquetas del menú principal y dos texturas de Coors Field (vallas y marcador) escaladas ×4 con el Studio se ven en el menú y durante un partido.
- **Ingeniería inversa:** las cabeceras de textura de los `.pack` guardan el alto antes que el ancho (ver `docs/ingenieria-inversa.md`, sección 5).

## Restos de Mario Kart Wii en el runtime (14 de octubre de 2026)

Auditoría de todo el runtime buscando direcciones, desplazamientos de `r13`/`r2` e identidad de Mario Kart Wii que sigan activos en MLB.

- **Arreglado (parche `wiicompiled-mlb-disc-identity` y `config/wiicompiled/rl8e54-data.csv`):**
  - `RFLiIsWorking` leía el puntero del gestor de Miis en `0x80386298` (Mario Kart; memoria libre en MLB). Una fila `ignore` del CSV ganaba a la fila `range` correcta; se quitó y ahora lee `0x8031B168`.
  - El disco se identificaba con el editor «01» y el disco 1 de Mario Kart sobre el `0x35340001` de MLB; y si `0x80000000` no tenía un código válido, la NAND, ES y Riivolution caían a «RMCP». Ahora usan `GameProfile::kDiscGameCode` y `kDiscMakerId`.
  - Las llamadas de red diferidas usaban el hilo por defecto de Mario Kart (`0x80347498`); ahora el de MLB (`0x802B7078`).
  - `IPCCltInit` no marcaba la bandera de "ya inicializado" (`r13-26876`): una segunda llamada habría vuelto a avanzar el búfer. Ahora la comprueba y la marca, como el original.
- **Comprobado:** 8 de 8 arranques sin cierre, `IPCCltInit` una sola vez, NAND solo en `title/00010000/524c3845`, el partido automático llega a jugarse y las pruebas del launcher pasan. Antes se habían visto 2 cierres silenciosos en 9 arranques; si el puntero de Miis era la causa no está demostrado.
- **Sin efecto en MLB** (código no enlazado o desactivado): mezcla de audio, ISFS, EGG/GX, SC, ES, hilos del sistema de Mario Kart, cargador de mods de Retro Rewind.
- **Revisado después, mismo día:**
  - **IOS:** el TMD del disco pide IOS33 (`0x21`); el runtime declaraba IOS36. Ahora `GameProfile::kIosVersion` da `0x00210E18` (valor de Dolphin para IOS33; misma fecha y memoria que IOS36). El juego lo confirma al arrancar: «Firmware: 33.14.24» (antes 36.14.24).
  - **Panorámico:** MLB lo soporta de forma nativa: consulta `SCGetAspectRatio` (`0x800D3374`) en 5 sitios, 2 de su REL, y en 16:9 los objetos redondos siguen redondos. Se mantiene 16:9 por defecto. *(Corrección del 17 de octubre: no se activaba; ver «Panorámico nativo» en `docs/ingenieria-inversa.md`. Ahora sí, con el parche `wiicompiled-mlb-widescreen`.)*
  - **Volúmenes:** música, efectos, interfaz y voces del menú en pantalla dependen del gestor de sonido de Mario Kart y no hacen nada en MLB. Para MLB se ocultan y queda solo el volumen general (el launcher ya solo ofrecía ese).
  - **Fotogramas:** MLB va a 60 FPS de forma nativa: cada fotograma de 16,7 ms trae su dibujado completo (346-357 llamadas, ninguno vacío) y la lógica va a velocidad normal; el runtime no interpola (`frame_interpolation_fps = 0`). *(17 de octubre: ahora es opcional, «Interpolación» en el launcher. Medido en el partido de prueba a 1x en una RX 5500: a 120 presenta 119,6 FPS con el juego a 60 y la GPU pasa de 3,1 a 7,6 ms por fotograma del juego (29 % a 54 %); a 180, 10,8 ms (65 %). `tools/benchmark-match.ps1 -Interpolation 120|180`; a 180 su contador de presentaciones marca 0 aunque la pantalla muestra 180.)*

## Opciones gráficas para equipos modestos (14 de octubre de 2026)

Parche `wiicompiled-graphics-options` (aurora y runtime). Pestaña VIDEO del launcher; también en el menú F10.

- **ESCALADO = AMD FSR 1.** Portado a WGSL desde el algoritmo publicado por AMD (MIT): EASU (12 muestras, dirección del borde, sin halos) y RCAS (nitidez adaptativa, 0,2 stops). Va en la presentación final, en el mismo punto que el escalador MetalFX de Mac, y solo actúa si la imagen del juego es más pequeña que la ventana. Comprobado en el menú a x1 en una ventana de 1920×1009: bordes de las letras claramente más nítidos que el escalado bilineal.
- **FILTRADO DE TEXTURAS** (Original, 2×, 4×, 8×, 16×). Aurora solo aplicaba anisotrópico a las texturas que lo piden en GX (`GX_ANISO_2/4`); «Original» sigue así. Un nivel forzado se aplica a toda textura cuyos filtros lo permitan, como el «forzar anisotrópico» de Dolphin. En partido, la tierra y el césped vistos de lado ganan detalle.
- **CALIDAD:** Baja (1x + FSR 1 + filtrado original), Media (2x + FSR 1 + 4×), Alta (según la ventana + 16×) o Personalizada.
- **Medido en partido** (RX 5500, Direct3D 12, ventana 1920×1009): x1 2,88 ms de GPU (32 %); x1 + FSR 1 + 16× 4,92 ms (40 %); x2 + 16× 7,54 ms (51 %).
- **Descartado: saltar fotogramas.** MLB hace unas 35 copias del EFB por fotograma (dibuja a texturas que usa después); no dibujar un fotograma dejaría esas texturas viejas o vacías. FSR 1 con resolución interna baja da el ahorro sin ese riesgo. Bajar a 30 FPS tampoco vale: la lógica del juego va a 60 Hz e iría a cámara lenta.
- **Menú F10:** «Disable bloom» se ocultó fuera de Mario Kart Wii (apuntaba a código de Mario Kart que no existe en MLB).

## Mipmaps generados y antialiasing (18 de octubre de 2026)

Parche `wiicompiled-auto-mipmaps` (aurora y runtime). Pestaña VIDEO del launcher (página 2): **MIPMAPS** (Sí por defecto) y **ANTIALIASING** (No, MSAA 2x, 4x, 8x). Mipmaps también en F10.

- **Problema:** las texturas de los estadios no traen mipmaps. En los 47 packs hay 8.135 texturas CI4, 3.130 CI8, 77 RGBA8 y 33 RGB5A3, todas de un solo nivel. A distancia cada píxel de pantalla toma un texel suelto: público y carteles ruidosos y con parpadeo al mover la cámara. Además, el anisotrópico forzado no actuaba en ellas: aurora lo desactiva si el muestreo no usa mipmaps.
- **Solución:** al crear una textura estática RGBA sin mipmaps (también las de paleta, que aurora ya convierte a RGBA en la CPU), aurora genera la cadena completa (`lib/gfx/auto_mips.hpp`) y la muestrea con filtrado trilineal si el juego pidió `GX_LINEAR`; `GX_NEAR` sigue sin filtrar.
  - El color se promedia ponderado por alfa, para que los texels transparentes negros no oscurezcan los bordes.
  - En texturas recortables (público, árboles, banderas: casi todo opaco o transparente) se reescala el alfa de cada nivel para mantener la cobertura de la prueba de alfa del nivel 0; sin eso, el público lejano adelgaza y desaparece.
  - Sesgo de LOD de −0,5 para esas texturas: con el promedio simple los carteles quedaban blandos.
- **Comprobado en el partido de prueba** (Coors Field, RX 5500, Direct3D 12, 1,5x, filtrado 16x): el público y las gradas lejanas se ven limpios, los carteles siguen legibles. GPU 4,78 ms con y sin mipmaps; la memoria de cada textura crece un tercio.
- **MSAA:** aurora ya lo soportaba (`AuroraConfig::msaa`); ahora llega desde `[video] msaa`. MSAA 4x se dibuja bien, copias del EFB incluidas, y suaviza barandillas y líneas, pero sube la GPU de 4,78 a 9,11 ms. Por eso viene desactivado. Necesita reiniciar el juego.
- **Prueba:** `runtime/tests/auto_mips_tests.cpp` (tablero de ajedrez a gris, borde rojo sin oscurecer, público disperso que no desaparece).

## Montón de MEM2 ampliado: packs de estadio de hasta 80 MB (18 de octubre de 2026)

Parche `wiicompiled-mlb-mem2-heap` (runtime).

- **Causa del tope de ~21 MB:** `0x8005AF78` crea el montón de MEM2 con un tamaño fijo, la arena retail (51,9 MB), aunque el runtime da 128 MB de MEM2. El pack del estadio se reserva desde su cola (`0x8005A9DC` desde `0x800434B8`); con 32 MB la reserva devuelve NULL y el juego la repite sin fin. Encontrado con un envoltorio temporal de los asignadores en los shards generados y la traza `WIICOMPILED_DEBUG_DVD`: el pack de 20 MB se lee en `0x913FC3E0`; el de 32 MB nunca se lee.
- **Arreglo:** `hle/mlb/game_heaps.cpp` reproduce `0x8005AF78` y da al montón de MEM2 toda la arena, como en el Wii: 125,7 MB.
- **Medido en el partido de prueba** (Coors Field, packs rellenados con ceros): 32 MB y 80 MB cargan y el partido avanza; con 96 MB el pack se lee en `0x911DC3E0`, pero luego falla otra reserva. El partido normal con los mods del usuario sigue igual.
- **Más margen:** ampliar `Memory::kMem2Size` (hoy 128 MB, heredado del modo NDEV de Mario Kart) daría más; no hace falta todavía.
