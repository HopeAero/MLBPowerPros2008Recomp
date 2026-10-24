# Rendimiento en PCs modestas

*English: [../en/roadmap/performance.md](../en/roadmap/performance.md)*

**Estado:** lo siguiente · [Roadmap](../../ROADMAP.es.md)

## Por qué
El juego debería ir a 60 FPS estables en las PCs que la gente tiene: portátiles con gráfica integrada, CPUs de 4 núcleos de hace unos años, la Steam Deck. Hoy se ajusta y se mide en un solo escritorio (Ryzen 5 5500 + Radeon RX 5500).

## Dónde estamos
- GPU a resolución nativa (x1) en un partido: unos 2,9 ms por cuadro en la RX 5500; x2 con filtrado 16x unos 7,5 ms. El launcher ya tiene calidades (Baja / Media / Alta), escalado AMD FSR 1, mipmaps y MSAA.
- La lógica del juego está traducida de antemano a código nativo, así que no hay coste de emulación, pero el código traducido y el runtime aún no se han perfilado en CPUs lentas.
- Vulkan mantiene 60 FPS donde Direct3D 12 caía en AMD en las vistas pesadas de fildeo; el launcher elige el backend.

## Plan
1. **Medir en equipos modestos reales:** una prueba repetible (el mismo partido guardado, el mismo recorrido de cámara) que da el tiempo de CPU y GPU por cuadro, en gráficas integradas Intel y AMD y en una CPU de 4 núcleos antigua. Los resultados de la comunidad son bienvenidos.
2. **CPU:** perfilar el código traducido y el runtime (mezcla de audio, decodificación de texturas, envío de dibujos) y arreglar lo más caro.
3. **GPU:** pasadas de efectos a pantalla completa más baratas en resoluciones altas, menos cambios de estado por dibujo, una calidad «Baja» pensada para gráficas integradas.
4. **Steam Deck:** un perfil medido y ajustes recomendados.

## Cómo se verifica
Números de antes y después con la misma prueba, en las mismas máquinas, en las notas de cada versión.

## Casillas
- [ ] Prueba de rendimiento que los jugadores puedan correr y compartir
- [ ] Resultados de al menos tres equipos modestos
- [ ] Los tres costes de CPU mayores arreglados
- [ ] La calidad «Baja» mantiene 60 FPS en gráficas integradas
