# Pipeline HLE de modelos: primera medición y revisión

29 de octubre de 2026. Implementación inicial: `76b9ba5`, fases 69–71.
Ambas capturas usaron la misma NRO y el worker HLE apagado.

| Captura privada | Configuración efectiva |
|---|---|
| `nxlink-HLE1-model-20261029-170347.log` | TEV, texturas y TLUT activos |
| `nxlink-HLE1-model-20261029-171145.log` | TEV y texturas apagados; TLUT siguió activo |

El argumento TLUT `=0` del segundo envío no tuvo el efecto esperado: los
contadores muestran TEV=0/0 y tex=0/0, pero aciertos/fallos TLUT. No es un
control de todos los cambios ni permite aislar TLUT. La causa de la transmisión
o resolución de ese argumento quedó sin confirmar; futuros envíos deben ser
más cortos y verificar los modos efectivos.

Análisis con `P20-buckets.py`, símbolos `elf-syms-HLE1.txt`; ventana activa de
diez volcados completos, control de nueve. Los resultados son CPU ocupada
estimada mediante muestras de PC, no tiempos completos de frame ni fps.

| Cubeta, listas/frame | Frames control/activa | CPU control/activa, ms |
|---|---:|---:|
| 240–279 | 3300 / 5738 | 9,185 / 11,278 |
| 280–319 | 2124 / 1483 | 9,197 / 8,352 |
| 320–359 | 1523 / 974 | 9,635 / 7,744 |
| 600–639 | 298 / 299 | 13,242 / 11,421 |

Estandarizando las cubetas compartidas 240–479 con el mínimo de frames por
cubeta, control/activa dan **9,723 / 9,866 ms**: no se demuestra una mejora global
de fildeo. En 600–639, TEV/canales dan 0,540 / 0,438 ms y texturas
0,893 / 0,766 ms; TLUT queda en 0,487 / 0,498 ms. La reducción total de esa
cubeta no se puede atribuir íntegramente a los cambios HLE. Frente al P22
histórico, la cubeta predominante 240–279 dio 11,386 / 11,278 ms.

No fue un replay determinista del mismo estado. Los relojes no quedaron
registrados en este modo de perfil y no se confirmó su valor ni la equivalencia
visual. No hubo errores GX registrados en las capturas examinadas; no se
emitieron resúmenes numéricos de decode/check. El diagnóstico de aciertos estaba
activo. No se afirma una mejora stock ni 60 fps estables.

Los contadores activos dieron 91,0% de aciertos TEV, 28,0% de texturas y 13,4%
de TLUT. Estos datos motivaron una revisión de código, sin otra partida:

- Mezclar la dirección antes del desplazamiento final del hash de descriptores;
  la primera fórmula descartaba esos bits al elegir cubeta.
- Capturar los 32 bytes de sombra directamente en RAM plana, conservando sus
  cuatro valores big-endian y la ruta escalar para los demás mapeos.
- Retener 128 identidades TLUT por valor para soportar paletas alternantes,
  invalidando el conjunto ante vaciados y llamadas legacy. En los fallos se
  reutiliza una sola muestra de generación.

Revisión: fases 72–73 con folds en 37/69/71. Pasaron los diferenciales existentes
y la compilación Horizon; las dos nuevas comprobaciones de lecturas de sombra
y alternancia de ocho paletas fallaban antes del cambio. NRO privada
`HLE1-round2.nro`, SHA-256
`e81cd3c0622ac1822c1124d6b316bac4163794c6692cf7e14047c210973920fa`.
Esta revisión aún no está medida en Switch. Informes, comandos, comparaciones
y pruebas permanecen bajo `local/tmp/cpu/HLE1-test/`; ningún log ni contenido
del juego se incluye en el repositorio.
