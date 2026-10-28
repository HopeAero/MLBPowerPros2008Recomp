# GX fuera del hilo del juego: factibilidad

Fecha: 2026-10-28. Rama: `switch-arm64`. Base inspeccionada: `314dc01`, fases 1–65.
Ámbito: fase 1 del brief `local/tmp/codex-gx-brief.md`; análisis del runtime y Aurora,
sin implementar todavía el traslado. Tres subagentes `gpt-6-luna`, razonamiento `max`,
revisaron estado, memoria y orden/sincronización. El grafo MCP no estaba disponible;
se usaron búsquedas y lectura dirigida del árbol de build.

## Dictamen

Es factible separar el trabajo pesado de la HLE GX, con condiciones de corrección
que el prototipo debe demostrar. El traslado literal de todas las llamadas a un
worker que reciba direcciones guest y las lea después no conserva el comportamiento
actual. Parte de la interfaz GX escribe RAM guest, devuelve valores o actualiza
estado que la siguiente instrucción del juego puede leer.

La propuesta es conservar una fachada síncrona pequeña y mover construcción de
estado host, interpretación y publicación al worker mediante registros ordenados
con datos propios. Los casos que aún no tengan un contrato de memoria demostrado
impiden activar la migración de esa familia; una operación que deba bloquear tramita
su parte host por el mismo dueño y espera el resultado. Eso permite comenzar con una
parte completa y reversible, sin prometer que el coste total de GX desaparecerá del
hilo principal. El audio queda fuera de los cambios.

## Evidencia de rendimiento y sus límites

El traspaso P15–P19 atribuye aproximadamente 4,2–4,7 ms/frame a GX en el principal.
Es el presupuesto bruto de referencia, no una medición del ahorro de una cola nueva;
las estimaciones por familia no deben sumarse como contadores independientes.

Se volvió a analizar `nxlink-MLBPowerPros2008-20261028-163615.log` con
`elf-syms-P18b.txt` y los analizadores existentes:

- 20 ventanas terminadas entre 70 y 260 s, 11.440 frames host: ocupación aproximada
  del principal de 10,78–15,74 ms/frame; media ponderada 13,46 ms/frame.
- Desde t=60 s hay tres picos capturados mayores de 30 ms: frames 6823, 9759 y 9965,
  de 32,6 / 30,2 / 36,6 ms, con 98 muestras en total. Ninguna de esas cadenas
  muestreadas contiene `wait_processed`. Esto no demuestra ausencia de esperas.
- Ese log no contiene muestras emparejadas W del worker ni registros `cpu_mhz`.
  No demuestra una causa concreta de los bajones de fildeo ni verifica reloj stock.
- El agregado de todos los picos mezcla intro, menús, E/S y juego; no es una
  comparación de fildeo. El corte temporal tampoco identifica jugadores en cámara.

Reproducción: `python local/tmp/cpu/GX-offload-profile.py`. Reutiliza los parsers
P15/P11, ejecuta el self-check de emparejamiento y comprueba ancla única, ausencia de
frames duplicados y coherencia de recuentos. Resultados locales:
`GX-offload-profile.md` y `GX-offload-windows.txt`.

El tiempo `gx_fifo_decode_ms` incluye esperas y desplanificación; los ~11 ms/frame
históricos no son utilización de CPU de core 1. Los datos antiguos P11/P12 describen
esperas y generación de mipmaps en otros builds; no se trasladan como causa probada
al build P19. El overclock que no elimina los bajones justifica medir sincronización,
pero no demuestra por sí solo un cuello de botella del worker o del GPU.

## Inventario de estado y efectos observables

Las rutas siguientes son relativas a `local/switch/work/mlb/src`. Se contrastó la
implementación con los overrides registrados y referencias generadas, sin modificar
código del juego. Hay tres estados distintos: RAM del SDK guest (`__GXData`),
`g_hleGxState` del runtime y `__gx` privado de Aurora. No son intercambiables.

| Ruta o familia | Evidencia y dependencia | Tratamiento propuesto |
|---|---|---|
| Setters, VCD/VAT y arrays | `runtime/src/hle/gx/gx_vertex.cpp:46` actualiza sombra HLE y llama a Aurora. Los paquetes CP en FIFO/listas también pueden cambiar el layout. | Sombra guest inmediata; aplicación host ordenada. No actualizar la sombra solo desde setters de alto nivel. |
| `GXGetViewportv` | Override activo en `gx_transform.cpp:53`; dos llamadas directas en `generated/functions/func_8008FF54.cpp:95` y `func_80090AF8.cpp:585`. Escribe seis floats guest. | Respuesta síncrona desde sombra del productor. |
| `GXGetScissor` traducido | El mapa identifica `0x800C8F28`; está inline en `func_80090AF8.cpp:487` y `func_8008E2F4.cpp:4085`. Lee el puntero guest y los campos `+0x148/+0x14C`. | Conservar los stores guest de scissor antes de retornar del setter; una sombra host aislada no basta. |
| Otros `GXGet*` | Wrappers de VtxDesc/VtxAttrFmt, TexObj/LOD, proyección y CPU FIFO están `PPC_NATIVE_UNBOUND`. Aurora sí implementa getters internos. | No asumir que el guest no los usa. Conservar entradas traducidas y efectos guest; resolver targets MLB y llamadas indirectas antes de migrar una familia que los afecte. |
| Consultas internas de VCD | `gx_dl.cpp:1503` y `:1543` consulta el estado real de Aurora; `GXGet.cpp:20` lee `__gx`. | Reemplazar por sombra que modele también CP de las listas, o drenar antes de consultar. No leer `__gx` concurrentemente. |
| `__GXData` guest | Puntero en `gx_internal.h:57`; inicialización en `gx_init.cpp:25`. Transform, textura y stubs escriben dirty/status, scissor y campos de copia. | Esos stores siguen síncronos. La búsqueda textual de globals no excluye accesos calculados o indirectos. |
| `GXTexObj` | `gx_texture.cpp:8` y helpers LOD materializan/modifican los 32 bytes guest. P19 mantiene esos efectos inmediatos aunque pospone objetos host. | Guest y extracción de descriptor pertenecen inicialmente al productor. Enviar descriptor por valor al diferir trabajo host. |
| `GXTlutObj` | `gx_texture.cpp:824` guarda metadata por dirección y construye un objeto host; ese wrapper no materializa una estructura TLUT en RAM guest. Load valida datos y actualiza dirty guest. | Preservar este comportamiento real, sin inventar stores nuevos. Capturar descriptor y establecer visibilidad/vida de la paleta. |
| Grabación de listas | `gx_frame.cpp:6` mantiene cursor/count/wrap y escribe el buffer guest; `GXEndDisplayList` devuelve longitud. | Mantener escrituras y retorno síncronos. No enviar al worker el destino guest para llenarlo tarde. |
| Interpretación y scan de listas | `gx_dl.cpp:1800` y siguientes usa el layout vigente, detecta índices y aplica CP; `gx_fifo.cpp:358` interpreta también FIFO directo. | Primer prototipo con scan/CP síncrono. Moverlo después exige snapshot o propiedad exclusiva de bytes/layout/arrays, no solo encolar dirección+tamaño. |
| Tokens y finalización | `gx_stubs.cpp:19` ignora actualmente el token SetDrawSync; `:38` drena en DrawDone y actualiza flag guest. `GXManage.cpp:264` entrega callback tras drain. | Preservar orden y contexto del port actual; no confundir acuse de cola con finalización GPU. |
| Copia y presentación | `gx_copy.cpp:120` coordina CopyDisp/VI/frame worker; `:147` encola CopyTex y recuerda destino. | CopyDisp y transiciones de frame siguen como barreras de ambas etapas; CopyTex necesita orden con draws y política de readback. |

`GXSetDrawSync` tiene seis referencias estáticas directas al override MLB; no se
demostró una llamada directa a DrawDone en el barrido del subagente. No se encontró
una implementación host `GXReadDrawSync`; el mapa sí identifica
`GXSetDrawSyncCallback` en el juego. Ninguno de estos negativos demuestra ausencia
de callbacks, de polling traducido o de accesos MMIO. Los nombres/direcciones MKW
conservados en wrappers `UNBOUND` tampoco identifican por sí solos targets MLB.

La separación de objetos tiene otro requisito previo: `GxObjMutex` en
`gx_internal.h:540` asume un único hilo host cuando G7 está activo; el segundo hilo
aborta en la ruta de comprobación de dueño. Desactivar esa comprobación no resuelve
por sí solo las carreras en RAM guest ni en las cachés. El primer prototipo conserva
metadata y validación de objetos en el productor, sin duplicar mapas; solo difiere
trabajo host con descriptores capturados. Una migración posterior tendrá que asignar
propiedad explícita a cada caché antes de repartir estas funciones entre hilos.

Los flush/invalidate/DMA notifican cambios y mantienen versiones; no equivalen a
capturar datos ni a impedir su sobreescritura. Para matrices basta una copia de
tamaño conocido. Arrays indexados y listas anidadas requieren resolver dependencias
o un fallback síncrono. Una barrera situada después de que el guest sobrescriba los
bytes no puede recuperar su contenido anterior.

Capturar antes tampoco garantiza equivalencia: hoy LoadTexObj serializa el descriptor,
pero los texels y la paleta normal se leen después al resolver el draw. Si el juego
cambia el origen entre Load y Draw, copiarlo indiscriminadamente en Init/Load puede
cambiar la imagen. Cada familia debe definir su instante de observación y probar
mutaciones entre llamadas; sin demostrarlo no se activa su migración.

CopyTex ya tiene un límite de readback documentado en el código: algunas copias EFB
se mantienen solo en GPU, las sondas pequeñas se descargan diferidas y una lectura
CPU que necesite RAM requiere `aurora_flush_efb_copies_to_ram`. El traslado no debe
añadir una demora más ni usar el flag DrawDone como prueba de que esa RAM está lista.

## Arquitectura propuesta y alternativas

1. **Worker HLE separado, inicialmente candidato en core 2.** El hilo del juego
   conserva efectos guest y una sombra de lo que necesita consultar; encola datos
   tipados en bloques. El worker HLE es el único productor de la cola Aurora existente,
   y core 1 sigue decodificándola. Las rutas síncronas también tramitan su parte Aurora
   por ese dueño y esperan su acuse; no alternan hilos productores. Core 2 también ejecuta frame
   worker, presenter y mezcla: su margen debe medirse, sin cambiar el audio.
2. **Fusionar HLE y decodificador en core 1.** Evita otra etapa de planificación,
   pero concentra ambos costes en el mismo núcleo y exige ejecutar las nuevas
   operaciones sin volver a encolarlas recursivamente en el FIFO que consume ese
   worker. Evaluarla después de medir CPU real y esperas; no asumir que 11+4 ms
   describe su presupuesto de CPU.
3. **Más atajos en el productor actual.** Menor riesgo de concurrencia, pero no
   satisface por sí solo el traslado grande solicitado; P17/P19 ya ahorraron solo
   ~0,2–0,3 ms/frame medidos en conjunto según el traspaso.

La primera opción ofrece la separación más clara para un prototipo diferencial.
La afinidad definitiva depende del perfil de los núcleos y del camino crítico.
No se añadirá un mutex global compartido por el juego y el worker para cada setter.

En el prototipo conservador, el productor sigue escaneando listas y validando objetos
textura/TLUT. Eso retiene aproximadamente 2 ms de los buckets históricos: el presupuesto
de setters, descriptores, stream y publicación ronda 2,6 ms/frame brutos, con solapamiento
y redondeo. El ahorro inicial sería menor, después de conservar sombra, capturar y
encolar. Llegar al objetivo de 3–4 ms exige migrar también parte de scan/objetos, con
sus contratos de memoria resueltos; no es el resultado esperado de ese primer paso.

Contrato mínimo de la cola:

- Un orden total para llamadas GX, escrituras FIFO escalares/burst, listas,
  invalidaciones, copias y límites de frame. No hay colas independientes que permitan
  adelantar setters a un draw o una copia.
- Capturar argumentos por valor, incluidos los bits de floats y colores. Copiar
  matrices y otros argumentos de vida corta antes de retornar; no transferir el
  `CpuContext` vivo ni ejecutar fibras guest en el worker.
- Payload propio o referencia cuya vida y visibilidad estén demostradas hasta el
  acuse correspondiente. Publicación release/acquire y acuses por secuencia; sin
  reutilizar buffers antes de su consumo. Una generación de dirty no conserva bytes.
- Colas llenas aplican contrapresión, sin descartar ni reordenar trabajo. El hilo
  principal conserva el servicio temporal que ya ejecuta durante sus esperas; los
  callbacks guest nunca se ejecutan desde el worker. La reentrada añade trabajo al
  mismo orden y obliga a volver a comprobar el objetivo del drenaje.
- `fifo_async.inc:670` llama `sWaitService` desde el hilo que espera. Al convertir
  al worker HLE en productor esa llamada ya no estaría en el host thread guest y el
  scheduler puede rechazarla. El puente debe hacer que el principal siga atendiendo
  sus temporizadores mientras espera una respuesta/espacio, y que una espera del
  worker solicite ese servicio al principal, sin ejecutarlo él. No bloquear el
  principal detrás de un worker que a su vez espera un callback del principal.
- El worker no llama a wrappers que toquen mapas G7 conservados en el principal.
  Esas rutas deben preparar descriptores/registros inmutables en el productor y
  tramitar su parte Aurora por el único dueño. Un drenaje por sí solo no migra el
  dueño del lock G7 ni hace atómicas las variables del productor FIFO.
- `GX_HLE_MidCommand` alimenta los puntos seguros del scheduler. Debe consultar
  la transacción/sombra del productor o un indicador publicado explícitamente,
  nunca leer sin sincronización el parser del worker.
- Las barreras de fin de frame deben cubrir ambas etapas. Un acuse de HLE no equivale
  a FIFO decodificado, GPU terminado ni RAM de una copia EFB disponible.
- Crear y cerrar hilos con el patrón compatible con libnx; parada, vaciado y join
  explícitos. No usar `std::thread::detach`.

## Presupuesto de ahorro

El ahorro neto en el principal es `G − S − Q − C − ΔW`: coste GX actual menos efectos
síncronos/sombra, encolado, copias de captura y esperas añadidas. La ocupación del
nuevo núcleo y la entrega al GPU pueden limitar el beneficio visible.

Los 3–4 ms/frame del brief son un objetivo plausible de la migración amplia, todavía
sin validar. Como cálculo de sensibilidad, con G=4,5 ms y costes residuales conjuntos
de 1 / 2 / 3 ms, el ahorro sería 3,5 / 2,5 / 1,5 ms; esos costes son supuestos,
no mediciones. No existe evidencia para asignar hoy un intervalo neto más preciso.
El primer prototipo debe medir sus bytes copiados, tiempo de encolado, sombra,
esperas y carga por núcleo antes de ampliar la migración.

## Alcance de la implementación posterior

La separación alcanzará las entradas GX de setters/transform/vertex/texture/copy,
los emisores FIFO escalares y burst, scan/grabación de listas, notificaciones DC/DMA,
los puntos VI/frame, el servicio temporal de espera y la creación/apagado del worker.
También los mapas G7 y sus callers de invalidación cuando se migren objetos. Los
tests/fijaciones diferenciales deben cubrir todos esos productores, con interruptor
de arranque y comparación contra el camino previo. No se requiere cambiar exports
públicos del proyecto ni código generado; cualquier puente interno nuevo y lista de
build afectada quedarán en el parche reproducible. El diseño de registros concreto
se decide en la fase del prototipo, tras medir y cerrar los contratos anteriores.

## Próxima prueba y criterios de avance

Antes de implementar la migración amplia, capturar con la NRO actual y reloj stock:
`WIICOMPILED_SWITCH_PROFILE=1`, `WIICOMPILED_SWITCH_PROFILE_GX=1`,
`WIICOMPILED_SWITCH_SPIKES=20`, `WIICOMPILED_SWITCH_SPIKES_MAX=256` y, para juego
normal, `WIICOMPILED_SWITCH_PROFILE_WINDOW=10`. Opcionalmente el contador existente
`WIICOMPILED_SWITCH_THREADS=5` ayuda a separar CPU de tiempo transcurrido. El muestreo
GX perturba los tiempos: se usa para diagnóstico, no para prometer rendimiento.

La captura libre actual no imprime `SwitchClocks`; la prueba temporizada sí imprime
relojes al inicio y en el resumen. Usar esa vía para una sonda automatizada corta con
relojes registrados, y distinguirla de una captura de fildeo: el benchmark sembrado
no reproduce un estado guardado ni una secuencia idéntica de controles. Una captura
libre sin registro independiente de relojes se etiquetará sin verificación de stock.
Enviar solo después del aviso «listo» del usuario para el netloader.

Para avanzar: pruebas host con estado/FIFO idénticos antes y después, incluyendo
registros mezclados, listas anidadas, punteros reutilizados, consultas tras setters,
callbacks reentrantes, cola llena, cambios de frame y apagado. Las pruebas deben
comparar también RAM guest y retornos; comparar solo bytes FIFO no detecta todos
los errores. Reutilizar el enfoque de `P17-test/` y `P19-test/`, con código real cuando
sea posible y casos negativos que fallen si se omite una captura o una barrera.

La primera fase de runtime será posterior a la 65, con interruptor
`WIICOMPILED_...=0` que restaure el camino anterior, folds donde corresponda y sin
cambiar headers incluidos por el código traducido. Integración con `mkswitch.sh`,
`treecheck.sh` y ambos `diff -rq`; build solo por el líder con cuatro jobs. A/B en
consola separando diagnóstico de benchmark y sin atribuir causalidad a partidas
distintas. Un pico limitado por GPU puede persistir aunque baje el coste del principal.
