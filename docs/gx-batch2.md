# GX: preparación de listas cacheadas y escritura FIFO

Implementación del 29 de octubre de 2026, fases 74–76 de
`local/tmp/switch_edit.py` (fold del contador opcional en 74). Se dirige a los
costes que siguen presentes en el perfil de `HLE1-round2`: preparación de listas,
arrays/VCD y `HleFifoWrite`. No cambia audio ni código traducido.

## Cambios

- **F32 directo:** posiciones, normales y texcoords DIRECT/F32 de un `GXBegin`
  ya iniciado evitan serializar y releer cada componente por el parser general.
  Las ráfagas procesan componentes de un atributo antes de llamar al mismo
  `SubmitAttribute`. Se mantienen los buffers de componentes, los bytes residuales
  del ring, los estados visibles durante cada llamada, el contador de vértices
  y el orden de `GXEnd`. El primer componente que inicia Aurora usa el camino
  anterior. Formatos distintos, colores, matrices indexadas, bytes parciales,
  grabación de listas y fuentes que solapan el estado usan los caminos generales.
- **Matrices XF:** mientras falta payload para completar un paquete XF, una
  escritura escalar de cuatro bytes solo lo añade al ring. La última palabra
  sigue pasando por el parser anterior: despacho, callbacks, marca de trabajo y
  consumo ocurren en el mismo orden. No se acumula un paquete separado ni se
  modifica la grabación guest de listas.
- **Listas simples cacheadas:** después de validar el registro de scan, se usan
  sus tablas de índices en lugar de inicializar y copiar cinco arrays por llamada.
  Se conserva la preparación VCD/VAT, arrays y XF, las identidades y comprobaciones
  G6, el envío por referencia y el fallback. Si falla un array se prepara XF de
  todos modos y se toma el parser sin repetir publicaciones. Listas con CP,
  anidamiento, escrituras de arrays o flatten, y el worker HLE activo, conservan
  la ruta anterior. La preparación síncrona no introduce nuevos puntos de espera.

## Interruptores

| Variable | Efecto |
|---|---|
| `WIICOMPILED_GX_BATCH_FIFO=0` | Desactiva los atajos F32 y XF |
| `WIICOMPILED_GX_BATCH_DL=0` | Desactiva el uso directo de tablas del scan |
| `WIICOMPILED_GX_BATCH_PROFILE=1` | Contadores opcionales `[gx-batch]` de palabras escalares, ráfagas, XF y listas/fallback |

Los atajos están activos por defecto; los contadores están apagados. El worker
HLE sigue apagado por defecto.

## Validación y límites

Harness privados bajo `local/tmp/cpu/GX-batch2-test/`, con los cuerpos originales
y modificados del parser y de `GX__CallDisplayList`. El entorno guest y las
entradas Aurora son simulados; no es una comparación de imagen GPU.

Pasaron 985.000 escrituras/ráfagas, 1.000 fuentes que solapan el estado del parser
y 100.000 secuencias de llamadas de listas, con los interruptores activos y
apagados. Se comparan todo el estado HLE del parser, los buffers residuales,
argumentos y orden de llamadas, bytes de paquetes reenviados y estados en los
puntos de llamada. Incluye divisiones de ráfagas, colores e índices mezclados,
NBT/NBT3, valores F32 por bits aleatorios, paquetes XF de 9/12 words, listas inválidas,
P17/G6 on/off, fallos de arrays/XF y callbacks durante el modo worker.

Siete controles negativos detectan omitir scratch o componentes raw, adelantar
`GXEnd`, consumir por el atajo la palabra XF final, omitir XF al fallar arrays,
ignorar P17 apagado y retirar la guarda del worker. Las comprobaciones de uso de
los atajos fallaban antes de implementar cada cambio.

`mkswitch.sh` y `P11-verify.sh` pasaron: árboles runtime/Aurora idénticos al parche.
Compilación Horizon con cuatro jobs: cero funciones traducidas actualizadas,
48.223 sin cambios. NRO privada `local/switch/build/GX-batch2.nro`, SHA-256
`4f5031436d6331bc671a4764a78a08b300b9d0d903b58051402b86fe0926fe7c`;
símbolos `local/switch/logs/elf-syms-GX-batch2.txt`.

**Todavía no está medida en Switch.** No se atribuye a esta build el ahorro de
la tanda anterior ni se afirma que sostenga 60 fps. La referencia más próxima es
`nxlink-HLE1-round2-20261029-182140.log` con `elf-syms-HLE1-round2.txt`.
Para la siguiente sonda basta un tramo corto comparable; confirmar netloader
antes de enviar y mantener los mismos relojes. Usar una lista corta de argumentos
con profile_dl, spikes=30, spikes_max=256 y batch_profile=1.

Actualización posterior: [GX batch3](gx-preparation.md) incluye estos cambios
y optimiza arrays/VAT. El usuario pidió posponer las pruebas de consola;
continuar con esa candidata cuando retome las sondas.
