# Creación de jugadores de dos vías en My Life

El parche `tools/patches/wiicompiled-two-way-player.patch` conserva la posición
principal de pitcher durante la inicialización de una carrera nueva. El rol del
día deja de sustituirla en las llamadas del finalizador `0x80A20FB8`.
La prueba en juego mostró que `[0x81048D84]` puede estar a cero también durante
la creación: esa bandera por sí sola no basta para proteger la inicialización.

## Selección independiente

Con la regla de dos vías activa, al escoger Pitcher y una segunda posición:

1. El panel original pide dos ventajas de pitcheo.
2. Repite el mismo panel con dos ventajas de bateo.
3. El panel de desventajas pide una de pitcheo y después una de bateo.

B desde la primera elección de la segunda tanda vuelve a la primera tanda.
Se conservan por separado los tres identificadores de cada faceta. La cabecera
original sigue guardando el conjunto de pitcheo; las elecciones adicionales
solo necesitan vivir en memoria hasta aplicar los efectos al registro final.
Las habilidades y estadísticas resultantes usan el guardado normal del juego.
El resumen original de creación conserva sus tres filas de pitcheo.
Las listas de la segunda tanda son de bateo; el resumen original no muestra
simultáneamente las seis elecciones y puede conservar etiquetas de la última
tanda mientras se navega. Los identificadores de cada conjunto se mantienen
separados para aplicar sus efectos.

Los wrappers de entrada/actualización de ventajas (`0x80977244`, `0x809772CC`)
y desventajas (`0x80A53244`, `0x80A532A8`) reutilizan las funciones traducidas,
sus listas, navegación, validación de combinaciones y sonidos. Se registran
con `REGISTER_NATIVE_FUNCTION_AS` para conservar los cuerpos traducidos que
llaman. Al confirmar la primera tanda se reinicia el panel para la segunda;
el padre recibe la confirmación cuando acaba la segunda tanda.

## Aplicación de los efectos

`0x80A21160`, al final de la inicialización, aplica también las elecciones de
bateo mediante las rutinas originales `0x80A219F4` (límites de crecimiento) y
`0x80A233E8` (estadísticas y habilidades). Se conservan la palabra de estadísticas
de pitcheo en registro +0x78, las aptitudes de posición en +0x5C..+0x67 y los
límites de pitcheo (índices 7..9 de las dos tablas y la asignación en índice 20).
Después se calcula el crecimiento inicial con las dos facetas ya presentes.

No se cambia código generado a mano, el disco ni las partidas del usuario.

## Verificación

- `python tools/test_two_way_creation.py`: posición real durante la creación,
  incluso con la bandera a cero, y roles de carrera/entrenamiento conservados.
- `python tools/test_two_way_perks.py`: dos tandas, B, conjuntos separados,
  conservación de estadísticas/aptitudes/límites de pitcheo y regla desactivada.
  Compila los hooks del parche con memoria sintética, sin contenido del juego.
- Compilación local de `WiiCompiled` completada.
- Comprobado en juego con una carrera nueva de instituto en los Red Sox,
  posición P y secundaria RF: dos listas de ventajas y dos de desventajas;
  B desde la tanda de bateo restaura la selección de pitcheo. El registro final
  conserva 148 km/h, control 105 y stamina 55, además de poder 115 y contacto 6.
  Se usó una NAND nueva bajo `local/tmp/two-way-creation/`; capturas y trazas
  permanecen bajo `local/`. No se jugó un partido ni se probó la recarga.
- Después de esa prueba se limpió la selección deshabilitada al cambiar de
  tanda y se pasó el conjunto de ventajas correcto a la validación original de
  desventajas. Ambos casos están cubiertos por la prueba sintética.
