# Nintendo Switch

*English: [../en/roadmap/switch.md](../en/roadmap/switch.md)*

**Estado:** investigación · [Roadmap](../../ROADMAP.es.md)

## Por qué
Power Pros es, en el fondo, un juego de consola. Una versión homebrew para Switch permitiría jugarlo en el sofá y fuera de casa, compilado desde la copia de cada uno como en PC.

## Qué hace falta
- **Gráficos:** Dawn no tiene backend para el sistema de Switch. Opciones: el backend OpenGL ES de Dawn sobre la Mesa de Switch (por eso OpenGL va primero) o un renderizador nativo en deko3d.
- **CPU y memoria:** el código traducido tiene que compilarse para ARM64 (la CPU de Switch) y caber en su memoria.
- **Sistema:** mandos, audio, archivos y fuentes con libnx (devkitPro).
- **Distribución:** como en PC, no se distribuye nada del juego; el jugador aporta su propia copia.

## Plan
1. OpenGL / OpenGL ES funcionando primero en PC (ver [OpenGL](opengl.md)).
2. Una versión ARM64 para Linux (por ejemplo en una Raspberry Pi o un TV box Android) como paso entre PC y Switch.
3. Un primer prototipo en Switch que llegue al menú.

## Casillas
- [ ] Backend OpenGL ES funcionando
- [ ] Versión ARM64 funcionando
- [ ] El prototipo de Switch llega al menú principal
