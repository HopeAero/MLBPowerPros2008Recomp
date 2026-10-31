# ABS: desafíos de bola y strike

*English: [../en/roadmap/abs.md](../en/roadmap/abs.md)*

**Estado:** planificado, sin ejecutar (investigación de la fase 1 hecha el 31 de octubre de 2026) · [Roadmap](../../ROADMAP.es.md)

Pedido del dueño del proyecto (2026-10-31). El sistema de desafíos de MLB (Automated Ball-Strike, reglas de 2026) como opción de **MEJORAS**, apagada por defecto y marcada EXPERIMENTAL, en Exhibición, Temporada y MLB Life.

## Por qué
Es la regla nueva de las Grandes Ligas: el umpire humano canta y el bateador, el pitcher o el catcher puede pedir revisión justo después del lanzamiento. El sistema automático confirma o corrige. Cada equipo tiene 2 desafíos y conserva el que acierta. Le da al partido una decisión más y acerca el juego a la MLB actual, igual que la regla Ohtani o la DH en ambas ligas.

## Qué sabemos (ingeniería inversa, fase 1)
Detalle completo, con direcciones: [ingeniería inversa, sección 21](../ingenieria-inversa.md#21-bola-o-strike-cuenta-y-ventana-tras-el-canto-abs-fase-1-2026-10-31).

- **El umpire del juego no se equivoca al azar.** `0x80CA9A14` compara el punto donde la pelota cruza la zona con un rectángulo fijo: dentro es strike; fuera y sin swing, bola. Sin aleatoriedad, dificultad ni ajustes.
- **Pero sus bordes no son parejos:** el de afuera tiene un margen de ~1,03 (probablemente el radio de la pelota); el de dentro, el de arriba y el de abajo, ninguno. El juego marca además los cantos ajustados (`0x37714`) y los comentarios usan una zona simétrica sin margen. De ahí la impresión de que «el umpire falla en las líneas».
- **Hay una ventana natural para el desafío:** tras el canto, el estado 0x11 del turno (`0x80CAFAA4`) espera ~1,07 s (fotograma 0x40) antes de aplicar el ponche o la base por bolas. Antes de eso solo han cambiado los bytes de la cuenta.
- **Los partidos simulados no tienen ubicación** (`0x80C95980` decide con azar): ahí no aplica.
- Con la opción apagada no se toca el generador aleatorio global del juego (`0x8031A078`).

## Diseño
- **Opciones (MEJORAS):** ABS sí/no; 2 desafíos por equipo, se conserva si acierta; zona ABS «reglamento» (la pelota toca la zona por cualquier lado, valores que el juego ya tiene en `0x80D32F9C`) o «del juego»; error del umpire apagado / bajo / realista.
- **Sin error del umpire**, los desafíos que ganan son casi siempre del pitcher (bolas por poco dentro, arriba o abajo que el reglamento daría strike). **Con error**, el umpire falla de vez en cuando en los bordes y el bateador también puede ganar. El error usa un generador propio, nunca el del juego.
- **Desafiar:** en la ventana tras el canto, un botón dedicado (leído en la capa de mandos del runtime). La CPU decide por la distancia al borde ABS, la cuenta, los outs, la entrada y los desafíos que le quedan, con algo de ruido.
- **Revelar:** la repetición usa lo que el juego ya dibuja: el marcador de la pelota (`0x8060AADC`) y la zona, con textos en atlas del juego repintados (nunca imitaciones con ImGui).
- **Revocar:** antes del fotograma 0x41 se intercambian strike y bola, se corrigen el contador de bolas del pitcher, el historial, el registro de lanzamientos y la clase de ubicación, y se repiten el gesto y la voz del umpire. El propio juego aplica después el ponche o la base por bolas.

## Fases
- **ABS-a · Sonda (0,5 días):** desde una partida guardada, registrar el punto 2D, la pelota 3D en el plato, el lado del bateador y el canto, para confirmar la regla de los bordes y que el marcador coincide.
- **ABS-b · Enganches (2–3 días):** canto, ventana y revocación con la cuenta, el ponche y la base por bolas correctos. Pruebas automáticas desde partidas guardadas.
- **ABS-c · Gestos y voces (1 día):** repetir el gesto y la voz correctos tras revocar.
- **ABS-d · Interfaz del desafío (2–4 días):** aviso y contador de desafíos con gráficos del juego.
- **ABS-e · Repetición (2–3 días):** la zona y el punto de la pelota.
- **ABS-f · CPU (1 día):** cuándo desafía.
- **ABS-g · Error del umpire (1 día):** opcional, con generador propio.
- **ABS-h · Launcher y modos (1,5–2,5 días):** opciones ES/EN en MEJORAS; Exhibición, Temporada y MLB Life.

Total estimado: **10–15 días de desarrollo**.

## Riesgos
- Wild pitch, passed ball, tercer strike no atrapado y pelotazo no pasan por la ventana: no se podrán desafiar.
- Hay efectos que ya ocurrieron al cantar y hay que deshacer o repetir: voces de cuarta bola o tercer strike, contadores, reacciones, la comprobación de bases llenas y el historial.
- El marcador de la cuenta (`0x8063ABCC`) no se refresca si bola y strike se intercambian (la suma no cambia).
- Los desafíos que queden en una partida guardada a mitad de juego (Temporada, MLB Life) aún no están estudiados.

## Decisiones pendientes (del dueño)
- ¿Con **error del umpire** para que el bateador también gane desafíos, o solo zona de reglamento?
- ¿Qué botón para desafiar en mando, Wii Remote y teclado?

## Casillas
- [x] Fase 1: dónde se canta, cuenta, ventana y gráficos reutilizables (2026-10-31)
- [ ] ABS-a sonda
- [ ] ABS-b enganches con pruebas
- [ ] ABS-c gestos y voces
- [ ] ABS-d interfaz
- [ ] ABS-e repetición
- [ ] ABS-f CPU
- [ ] ABS-g error del umpire
- [ ] ABS-h launcher y modos
