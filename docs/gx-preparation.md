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

La candidata incluye batch2 y esta revisión. Ambas siguen pendientes de una
sonda corta comparable, a los mismos relojes sin overclock. La referencia de
consola continúa siendo `nxlink-HLE1-round2-20261029-182140.log`; no se atribuyen
a esta build sus resultados ni se afirma que alcance 60 fps estables en fildeo.
