# Mods de código

*English: [../en/roadmap/code-mods.md](../en/roadmap/code-mods.md)*

**Estado:** investigación · [Roadmap](../../ROADMAP.es.md)

## Por qué
Hoy un mod son datos: texturas, rosters, calendarios, estadios. Muchas ideas necesitan lógica: un torneo, una regla nueva, una pantalla nueva. Cada mejora de juego hasta ahora (la regla Ohtani, la charla de rol con el mánager, el generador de calendarios 15/15) se escribió como código nativo dentro del runtime. La meta es que los modders hagan lo mismo sin recompilar el runtime.

## Lo que ya sabemos
- Cada función del juego tiene una dirección, y el runtime puede reemplazar cualquiera con código nativo (así se hizo cada mejora).
- Las conversaciones de MLB Life son guiones que ejecuta un intérprete, y un mod puede aportar su propio guion y su texto.
- Las alineaciones, el calendario, la temporada y la partida están documentados en las notas de ingeniería inversa.

## Plan
1. **Diseñar una API de mods:** puntos de enganche documentados (alineaciones, calendario, reglas, eventos, diálogos, menús), qué puede leer y cambiar un mod, y cómo se cargan los mods y siguen funcionando entre versiones.
2. **Elegir el lenguaje:** uno de scripts (por ejemplo Lua) por seguridad y facilidad para compartir, plugins nativos para lo pesado, o ambos.
3. **Demostrarlo con un mod real:** por ejemplo, un modo torneo hecho entero como mod.

## Cómo se verifica
El mod de prueba funciona en una instalación limpia con solo ese mod añadido, y la API queda documentada para otros.

## Casillas
- [ ] Puntos de enganche y propuesta de API escritos y abiertos a comentarios
- [ ] Lenguaje elegido
- [ ] Un torneo (o similar) hecho como mod
