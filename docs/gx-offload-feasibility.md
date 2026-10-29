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

## Sonda stock completada y criterios de avance

Después del «listo» del usuario se ejecutó la NRO P18b actual, sin cambios de runtime,
con `WIICOMPILED_MATCH_BENCH=1`, `WIICOMPILED_MATCH_SECONDS=90`, perfil principal y
GX activados, picos a 20 ms/máximo 256 y `WIICOMPILED_SWITCH_THREADS=5`.
Log: `nxlink-MLBPowerPros2008-20261028-170606.log`; símbolos `elf-syms-P18b.txt`.
Informe reproducible privado: `local/tmp/cpu/GX-F1-console-report.md`.

- Portátil, deko3d, CPU 1020 / GPU 307 / EMC 1331 MHz registrados al inicio y final.
- 90,0128 s, 5.347 frames, 59,4027 fps. Host p50 16,6676 / p95 18,4216 /
  p99 21,7355 ms. Diagnóstico instrumentado, no benchmark de rendimiento sin sampler.
- 124 picos >20 ms, uno >30 ms (33,5 ms), 2.670 muestras P y 2.669 pares W.
  Sin agotar límite ni perder capturas por cola llena. `wait_processed` aparece
  en 247 muestras de 90 picos; los PCs del worker cercanos muestran mayormente
  trabajo de decoder/estado/memoria, no una condvar inactiva en ese subconjunto.
- Espera media del productor 0,274 ms/frame; decode transcurrido 11,125 ms/frame.
  No convertir ese último número en uso de CPU de core 1. Cero errores GX registrados.
- La última ventana deko3d, 1.752 presents, tiene GPU p99 20 ms y 73 frames con GPU
  >16 ms. Es una ventana del proceso, con numeración distinta de los frames host;
  no atribuye por sí sola un pico host concreto a GPU. `gpu_samples=0` del resumen
  Aurora no elimina esta telemetría de timestamps propia de deko3d.

Esto respalda liberar el principal y estudiar solapamiento, pero no demuestra que
solo trasladar HLE alcance 60 fps estables en todas las escenas. Hay trabajo y esperas
en CPU, y una ventana GPU por encima del presupuesto. Los pares P/W son secuenciales,
no simultáneos; el muestreo extra perturba tiempos. El arranque sembrado tampoco es
un mismo estado guardado con controles idénticos ni una cobertura exhaustiva de fildeo.
Nxlink y la NRO terminaron; no quedan monitores lanzados.

Los contadores de hilo aportan ahora un criterio de afinidad: 18 dumps durante la
grabación; descartado el primero, que cruza el inicio, quedan 17 deltas de períodos
nominales de 5 s. CPU media por delta: principal 4,272 s, worker GX fijado a core 1
2,587 s, suma de slots registrados fijados a core 2 0,967 s. Equivalen aproximadamente
a 85% / 52% / 19% de un núcleo con ese denominador nominal. `interval_ms` es tiempo
CPU, no duración wall medida. El principal permite afinidad 0x7, por lo que no se
asigna su consumo a un core específico; la suma core 2 excluye sampler/dumper,
un slot con afinidad compartida y carga del sistema. No demuestra 81% libre.
Favorece **core 2 como primer candidato de prototipo**, conservando la necesidad de
medir contrapresión y entrega. Método y límites: `local/tmp/cpu/GX-F1-threads.md`;
el líder reprodujo de forma independiente los 17 recuentos y medias desde el log.

Para activar el prototipo todavía debe inventariarse y encaminarse cada productor
Aurora y cada entrada FIFO directa; el único dueño descrito arriba es un requisito,
no una propiedad ya implementada. Las cachés y objetos host conservados en G7 no se
prestan al worker: este solo consume registros propios que no muten esos mapas.
La época de observación de texels y TLUT debe conservarse incluso si el guest modifica
el origen entre Load y Draw. No hay equivalencia de snapshots anticipados demostrada.

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

## Fase 66: candidata optativa compilada

La primera implementación está en `tools/patches/wiicompiled-gx-offload-source.patch`
y en los dos parches Switch generados. `WIICOMPILED_GX_HLE_WORKER=1` crea un worker
libnx en core 2 con 16.384 registros propios de hasta 128 bytes; sin la variable,
las llamadas siguen el camino original. El enlace intercepta 143 entradas GX/Aurora.
En esta fase, 91 emisores escalares y tres cargas de matrices/luz con copia exacta
pueden ejecutarse en el worker. Las llamadas con resultados, punteros prestados,
objetos de textura/TLUT, callbacks y control se cercan y ejecutan en el principal.
La sombra VCD permite las consultas de atributos sin esperar cada setter. Las
escrituras guest de HLE, el escaneo de listas y la construcción G7 permanecen en el
principal. **Por tanto, esta fase aún no saca el GX entero del hilo del juego.**

La cola conserva orden FIFO y copia el payload antes de publicar; cada publicación
sincroniza el aviso con el mutex del worker, lo que cierra las dos carreras de wake
detectadas durante revisión. Los límites de frame, drain/quiesce, raw draw y locks G7
ceden la propiedad al productor solo tras vaciarla. Las esperas atienden VI, alarmas
y la consulta de audio desde el principal; el worker no ejecuta callbacks guest.
`GXEnd` comparte el orden encolado de `GXBegin` y los vértices. Se hace join antes
de cerrar Aurora y antes de destruir las fibras guest. Una excepción del worker en
un hook `noexcept` se registra y termina el proceso, pues la cola y el estado Aurora
ya no admiten continuación segura.

Verificación local: test de cola con wake forzado y variante negativa de copia;
diferenciales con cuerpos reales HLE/Aurora para RAM guest, VCD, FIFO, scissor,
matrices/luz, reentrada y seis wrappers GNU `--wrap`. Los mutantes de fence, sombra,
orden de callback y payload fueron detectados. La cadena de 74 parches más Linux y
Switch aplica con rebind; `treecheck` coincide exactamente en runtime y Aurora. La
traducción dejó 48.223 archivos iguales; el build Switch de cuatro trabajos enlazó
el NRO Vulkan de 119.314.629 bytes, SHA-256
`93bda05ab39c64953a09a8858a2a555c8a17c919a3266a50e578efc69f8e6a0a`.
El NRO previo está en `local/switch/build/GX-Q66-baseline.nro` con SHA-256
`6d0b1ab24d2082cba079fe13b1577b52fa449a8c03ef9ee1d707f79008e597f2`.

## A/B en Switch: fases 66, 70 y 71

Pruebas de 90 s en modo portátil con deko3d y relojes 1020/307/1331 MHz,
`WIICOMPILED_SWITCH_PROFILE=1`, perfil GX, picos de 20 ms y cinco hilos. El
autoplay arranca igual, pero no reproduce controles bit a bit; estas cifras son
comparables para orientar el trabajo, no una garantía de rendimiento por escena.

| Candidata | Worker | Frames | FPS | Host p50 / p95 / p99 ms | Muestras `Queue::Flush` |
|---|---:|---:|---:|---:|---:|
| Q66 | 0 | 5298 | 58,8581 | 16,6708 / 19,4966 / 22,9446 | — |
| Q66 | 1 | 4045 | 44,9406 | 22,2776 / 26,4727 / 29,2799 | 30674 |
| Q70 | 1 | 3797 | 42,1858 | 23,9092 / 27,4993 / 30,2607 | 22995 |
| Q71 | 1 | 3861 | 42,8976 | 23,5293 / 27,2125 / 29,9159 | 17012 |
| Q72 | 1 | 3895 | 43,2696 | 23,2970 / 27,1137 / 29,7971 | 12559 |
| Q73 | 1 | 4097 | 45,5171 | 21,9824 / 26,2879 / 29,4295 | 14592 |

Logs privados: `nxlink-MLBPowerPros2008-20261028-224749.log`,
`-225120.log`, `nxlink-MLBPowerPros2008-20261029-001201.log` y
`nxlink-GX-Q71-display-ref-20261029-003259.log` y
`nxlink-GX-Q72-small-list-20261029-004832.log` y
`nxlink-GX-Q73-signal-20261029-005743.log` bajo `local/switch/logs/`.
Ninguna ejecución registró errores de decode GX. Q70 amplió capturas exactas
de matrices normales, runs y `GXSetArray`; Q71 encoló listas G6 por referencia.
Las muestras de espera bajaron, pero el rendimiento no se recuperó. El perfil de
Q71 mantiene esperas en listas no referenciadas y locks de objetos. Q72 hizo
copias exactas de listas pequeñas y quitó el fence del lock de mapas G7, que
solo usa el productor. Así redujo `Queue::Flush` a 12559 muestras, pero agregó
coste de entrega por comando. El worker
HLE consumió aproximadamente 5 % de un núcleo en el último intervalo de 5 s,
frente a 88 % del principal: mover llamadas Aurora pequeñas tras cada barrera
añade más coordinación que trabajo útil desplazado.

**Decisión:** `WIICOMPILED_GX_HLE_WORKER` sigue desactivado por defecto. Estas
fases todavía no sacan el GX HLE entero del hilo principal ni aseguran 60 fps.
Q73 evita tomar el mutex del worker cuando está despierto. Pasó el test de
despertar al borde de la condición y el test de cola; en consola subió a 45,52
fps y mantuvo cero errores de decode GX. `Queue::Flush` todavía ocupó 14592 de
87573 muestras y `Queue::Submit` 12554. La suma de pequeños trabajos, copias y
fronteras de observación sigue perdiendo frente al camino anterior.

**Conclusión de esta línea de trabajo:** no activar el worker. La evidencia
rechaza una cola por llamada GX como vía a 60 fps estables. La unidad siguiente
debe ser un bloque de comandos HLE ya analizado con sus bytes y objetos propios,
publicado una vez y consumido en orden; antes hay que identificar las escrituras
guest, consultas, callbacks y copias que obligan a devolver resultados al
principal. El decoder FIFO existente sigue en otro hilo, así que este rediseño
debe evitar duplicar su trabajo o crear otro punto de espera. Aun liberando CPU,
la ventana GPU p99 de la sonda stock supera 16,67 ms; habrá que medirla aparte.

## Q75: bloque BP retirado, sin ejecución real en Switch

La candidata agrupaba ráfagas FIFO de 2–79 paquetes BP completos (`0x61`) en un
payload propio de hasta 320 bytes y una publicación HLE. Solo se admitía con el
worker activo, fuera de Begin, grabación y FIFO parcial, y con todos los trabajos
HLE ya terminados. Con trabajo pendiente conservaba el parser anterior: su
primera llamada BP puede atender callbacks antes de leer el paquete siguiente;
copiar la ráfaga antes de esa espera cambiaría los bytes observados.

Sus variables `WIICOMPILED_GX_BP_BURST` y `WIICOMPILED_GX_BP_BURST_PROFILE`
solo aplican a las NRO experimentales conservadas. Se retiraron helper,
diagnóstico e interceptación después de observar cero candidatos y cero envíos
durante los 17 intervalos completos de juego de Q76.

Los cinco sitios BP puros del inventario estático están en cuerpos generados de
`GXInvalidateTexAll` (`0x800C7114`), `__GXSetTmemConfig` (`0x800C745C`, tres
sitios) y `GXSetTevColor` (`0x800C7DC0`). El dispatch Switch selecciona nativos
HLE para esos tres targets antes del cuerpo traducido, tanto por la ruta directa
como por la prioridad del registry indirecto. No se encontró una llamada C++
directa que saltara esos reemplazos. El inventario de 51 sitios no probaba que
los cinco elegibles se ejecutaran. Evidencia: `GX-Q75-dead-sites.md` privado.

La revisión confirmó que CP directo solo cambia la sombra HLE; las siguientes
salidas Aurora usan wrappers síncronos, la misma cola HLE o `HleProducerScope`
en raw draw. El diferencial host extrae los cuerpos reales del helper, las
barreras y los emisores BP de Aurora. Coinciden bytes BP y estado modelado; quitar
la barrera de draw en el control negativo invierte el orden y la prueba lo detecta.
El test de cola cubre copia propia, límite de 79 palabras, filtros y fallback con
callback que modifica el segundo paquete. Pruebas privadas en
`local/tmp/cpu/GX-Q75-bp-test/` y `GX-Q75-diff/`; no sustituyen el decoder completo
ni una comprobación de imagen en Switch. Son pruebas históricas de Q75; su
extractor necesita el árbol anterior a la retirada (commit `56778de`).

La NRO Q75 inicial dio **45,5421 fps**, 4.099 frames en 90,0047 s a
1020/307/1331 MHz, con p50/p95/p99 de 21,9698/26,063/28,9355 ms y cero errores
de decode GX. `Queue::Flush` recibió 14.464 muestras y `Queue::Submit` 12.312 de
87.595. Q73 dio 45,5171 fps: esta captura no muestra una mejora apreciable. Se usó
el mismo arranque sembrado y perfil, pero no un replay desde estado idéntico; Q75
limitó picos a 40 frente a 256 en Q73. No atribuir la diferencia al bloque BP.
El contador inicial solo usaba `atexit` y el benchmark llama `_Exit`, por lo que
no produjo datos de frecuencia; se corrigió a reporte periódico para la siguiente
NRO. Log privado: `nxlink-GX-Q75-bp-20261029-111618.log`.

Las auditorías de bloques mayores están en `local/tmp/cpu/GX-Q74-*.md` y
`GX-Q76-scalar-batching.md`. El epoch actual no cubre todos los stores RAM guest,
por lo que no autoriza lecturas concurrentes de listas o arrays. El primer
prototipo Q76 con payload combinado pasó cuatro pruebas, pero se sustituyó por
una publicación agrupada de los slots existentes, descrita abajo.

## Q76: publicación agrupada, candidata optativa

`WIICOMPILED_GX_HLE_BATCH=1` copia los mismos registros a la cola existente y
los publica al worker cada 16 slots o al llegar a una barrera. Permanece apagado
por defecto. El watermark público incluye todos los slots enviados, incluso el
tail aún no publicado; el worker observa solo la frontera publicada. El helper
`Idle()` quedó solo en las pruebas tras retirar Q75.

`Flush(target)` publica hasta target, `FlushAll` vuelve a consultar el watermark
si los callbacks agregaron trabajo, y `Stop` publica el tail incluso cuando una
excepción de servicio interrumpe el drenaje. Con el ring lleno se publica el
prefijo antes de esperar; la llamada exterior relee los índices después de una
posible reentrada. No se suspende el servicio guest ni se agrega otro formato de
payload. Se conserva el protocolo de despertar de Q73 sobre la frontera publicada.
`WIICOMPILED_GX_HLE_BATCH_PROFILE=1` registra publicaciones y slots acumulados
cada cinco segundos para medir el tamaño real de los bloques.

El harness de la cola real pasó con capacidades de 16 y 64 slots: umbral de 16,
tail de 1–15, despertar forzado en el borde de sueño, Flush parcial, ring lleno
con reentrada y sombra VCD, FlushAll con tail añadido por callback, Stop con
callback que lanza, fallo del worker, fallback inline y configuración por entorno.
También pasaron Q66 y el diferencial Q75 en ambos modos, incluyendo rechazo de
staged tail y lectura posterior de BP2 mutado. Tests privados en
`local/tmp/cpu/GX-Q76-publish-test/`. Dos revisiones de código y el líder no
encontraron una inversión de orden en esos contratos. Este cambio agrupa la
entrega de llamadas Aurora ya admitidas por
Q73: el escaneo HLE de listas, las escrituras guest y las texturas/TLUT todavía
permanecen en el principal. No se ha sacado el GX entero ni garantizado 60 fps.

El build Switch Q76 terminó con 48.223 archivos traducidos sin cambios y la
reconstrucción de runtime/Aurora coincidió exactamente. NRO privada
`local/switch/build/GX-Q76-publish.nro`, SHA-256
`444044c9717d02a9f1bd65bf10ec85660dd3fff462d34df6f5aa92e1fa71cf75`.

La captura con batch activado dio **45,9614 fps**, 4.137 frames en 90,0103 s,
stock portátil 1020/307/1331 MHz, p50/p95/p99 21,8487/26,1113/28,4295 ms y cero
errores de decode. En 17 intervalos completos dentro de la grabación (~85 s),
53.769.953 slots / 4.346.444 publicaciones = **12,371 comandos por publicación**;
se excluyeron startup y el delta que cruzaba el inicio de grabación. No equivale
a un ahorro de CPU o FPS del mismo porcentaje. `Flush` recibió 18.792 de 88.483
muestras (21,2%), `Submit` 12.566 (14,2%) y `PublishThrough` 469 (0,53%).
Las muestras de `Flush` aumentaron frente a Q75 aunque bajaron las de mutex.
La comparación con Q75 es pequeña y mixta; falta el control batch-off con la
misma NRO y los mismos diagnósticos. Log privado
`nxlink-GX-Q76-publish-20261029-113957.log`; análisis `GX-Q76-console-on.md`.

Después se retiró Q75 y `Idle()` quedó bajo `GX_OFFLOAD_QUEUE_TEST`. El diff de
`gx_fifo.cpp` contra phase65/Q73 es vacío. Q66, Q76 con capacidades 16/64 y
configuración batch on/off volvieron a pasar; la reconstrucción del parche y el
build Switch también. NRO limpia privada `local/switch/build/GX-Q76-clean.nro`,
SHA-256 `0493c0e5327528016e89c17fb9fb6f0ea1eb89ab74eb194ed5041f2f0b37ae8e`.
Esta limpieza no se ha ejecutado en consola; el control batch-off pendiente usa
la NRO original `GX-Q76-publish.nro` para conservar el mismo binario.
