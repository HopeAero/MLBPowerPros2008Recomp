# GX: resolución de arrays y formatos de vértices

Revisión del 29 de octubre de 2026, fases 77–78 de
`local/tmp/switch_edit.py`, sobre [GX batch2](gx-batch2.md). Continúa la
preparación HLE de geometría señalada por el perfil HLE1-round2. Por decisión
del usuario, la prueba y medición en consola quedan para después.

## Cambios

- Los arrays de vértices y matrices XF que caben completamente en una página
  RAM con bias writable válido resuelven su puntero con una sola consulta.
  Evitan `Memory::Contains` seguido de `GuestToHostPtr`. El bias se consulta en
  cada llamada: no se conserva el puntero entre llamadas. Cruces de página,
  rangos vacíos, desbordamientos y páginas sin ese bias mantienen ambas
  operaciones anteriores, incluido su manejo de fallos. No cambian los spans,
  strides, repacking ni la vida de los buffers para FIFO asíncrono.
- La publicación VAT reutiliza la igualdad de filas que ya mantiene G6/A3.
  Si el mirror es válido, su bit de fila está válido, el hash HLE está limpio
  y coincide, evita recorrer los atributos de esa fila. Los cambios solo en
  VCD pueden subir la generación global sin invalidar esta igualdad. No
  calcula un hash adicional ni evita setters que la comparación anterior
  hubiera emitido.

Ambos cambios se desactivan con `WIICOMPILED_GX_BATCH_PREP=0`. Están activos
por defecto, independientemente de los interruptores FIFO y DL de batch2.
`WIICOMPILED_GX_BATCH_PROFILE=1` añade `array_direct`, `array_fallback` y
`vat_rows` al registro opcional `[gx-batch]`; los contadores siguen apagados
por defecto. El worker HLE conserva su configuración anterior.

## Evidencia y límites

Los diferenciales privados de `local/tmp/cpu/GX-prep-test/run.py` extraen las
funciones anteriores y actuales. Se ejecutan con el cambio activo y apagado:

- Arrays: 40.000 consultas de rango y 80.000 preparaciones de vértices/XF,
  comparando resultado, orden y argumentos de publicaciones, identidad de
  punteros RAM, mirror y bytes repacked. Incluyen aliases, remapeo, bias ausente,
  páginas parciales, cruces, rango vacío, wrap, formatos NBT3, stride mayor que
  255, máscaras de atributos, llamadas repetidas y FIFO asíncrono.
- VAT: 180.000 pasos aleatorios y dos secuencias dirigidas. Se comparan todas
  las llamadas y argumentos de setters y una firma por paso de los mirrors,
  flags, hashes y contadores G6. Incluyen cambios VCD sin cambio VAT, filas
  sucias, invalidación, publicación parcial y G6/A3 activos y apagados.

El entorno de memoria y las entradas Aurora son simulados; los helpers de
tamaño y repacking proceden del código real. El digest del harness VAT usa
FNV como sustituto de XXH3, igual que el harness G6 existente. Estas pruebas
no validan la imagen GPU ni miden el rendimiento de Switch.

Pasaron ambas configuraciones y cinco controles negativos: cruce a región
inválida, bias ausente, fila sucia, bit de mirror inválido y hash diferente.
En la campaña sintética hubo 146.769 resoluciones directas y 36.872 filas VAT
omitidas; son cuentas de trabajo evitado en el harness, no ahorros de tiempo
ni tasas de acierto del juego.

La regeneración del parche y P11 pasaron: árboles runtime/Aurora idénticos.
Build Horizon con cuatro jobs completada; cero funciones traducidas
actualizadas y 48.223 sin cambios. Candidata privada:
`local/switch/build/GX-batch3.nro`, SHA-256
`1a0c2df102e0a3c715623937e997e8153e98234946e52502b5febdcf99433789`.
Símbolos: `local/switch/logs/elf-syms-GX-batch3.txt`.

La candidata incluye batch2 y esta revisión. Se envió a consola posteriormente,
cuando el usuario confirmó que estaba en netloader. La primera medición se
describe a continuación; no se afirma que alcance 60 fps estables en fildeo.

## Primera medición en consola

Log privado `nxlink-GX-batch3-20261029-191939.log`, con los símbolos y SHA-256
de esta candidata. Se habilitaron profile_dl, spikes=30, spikes_max=256 y
batch_profile; los contadores confirman que se ejecutaron los atajos. Se mantuvo
la instrucción del usuario de probar sin overclock, sin telemetría de relojes
en este modo. No se añadió otra partida para aumentar la muestra.

El informe usa una instantánea privada tomada a las 19:24:44, mientras la
captura seguía abierta: `local/tmp/cpu/GX-prep-test/console-snapshot.log`.
Contiene ocho inicios de volcado; se usan los siete cuya impresión terminó
antes del siguiente inicio. Se reparó un anchor dividido por la intercalación
del reporte DK. En el volcado elegido se reconocen sus 9.697 registros PC;
la referencia archivada HLE1-round2 tiene seis volcados completos.

Comparación de CPU ocupada estimada por muestras PC, en cubetas compartidas
240–479 listas/frame con al menos 200 frames por captura. Pesos: mínimo de
frames por cubeta, 1654/964/808/401/257. Son escenas distintas, sin replay del
mismo estado; estas cifras no son tiempos completos de frame ni FPS.

| Grupo | HLE1-round2, ms | GX-batch3, ms |
|---|---:|---:|
| CPU ocupada muestreada | 8,764 | 8,104 |
| FIFO y envío de atributos | 0,350 | 0,163 |
| Preparación/envío de listas | 0,215 | 0,194 |
| Preparación VCD | 0,176 | 0,140 |
| Preparación VAT | 0,041 | 0,015 |
| Preparación de arrays | 0,107 | 0,140 |
| GuestToHostPtr | 0,076 | 0,069 |

La diferencia global observada es −0,660 ms de CPU ocupada, con resultados
heterogéneos: 240–279 empeora 0,230 ms, 280–319 mejora 3,494 ms y 360–399
empeora 2,590 ms. No demuestra un ahorro causal de ese tamaño. Arrays no
presenta reducción medida en esta comparación, aunque se confirma el uso de
su resolución directa. No se aíslan por separado las contribuciones batch2/3.

Los contadores opcionales cuestan aproximadamente 0,194 ms en esas cubetas;
están activos solo en la candidata durante esta comparación. No se deben
restar para inventar un resultado sin diagnóstico: esa configuración no se
midió. En la cubeta 600–639 hay 298 frames nuevos frente a solo 49 de la
referencia, insuficientes para comparar la toma más pesada. Allí siguen
apareciendo costes en llamadas indirectas, listas, memoria/asignaciones y GX.
El audio se mantiene fuera del alcance de los cambios.

En las ventanas DK de esta instantánea no hubo frames con GPU mayor que 16 ms.
Las ventanas con partido muestran p95 de GPU entre 6,50 y 9,25 ms, mientras
persisten picos de intervalo de unos 30–40 ms o más, atribuidos a CPU por DK.
No se registran errores GX en la instantánea. La confirmación visual del
usuario sigue pendiente; esto no es validación pixel a pixel ni evidencia de
60 fps estables.

Análisis reproducible privado: `console_report.py console-snapshot.log`,
`console-report.txt/json`, `last-complete.log` y `hotspots.txt`, bajo
`local/tmp/cpu/GX-prep-test/`. La captura en vivo puede contener datos
posteriores que no forman parte de esta instantánea.
