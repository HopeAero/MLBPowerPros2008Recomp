# Rendimiento en PCs modestas

*English: [../en/roadmap/performance.md](../en/roadmap/performance.md)*

**Estado:** lo siguiente · [Roadmap](../../ROADMAP.es.md)

## Por qué
El juego debería ir a 60 FPS estables en las PCs que la gente tiene: portátiles con gráfica integrada, CPUs de 4 núcleos de hace unos años, la Steam Deck. Hoy se ajusta y se mide en un solo escritorio (Ryzen 5 5500 + Radeon RX 5500).

## Dónde estamos
- GPU a resolución nativa (x1) en un partido: unos 2,9 ms por cuadro en la RX 5500; x2 con filtrado 16x unos 7,5 ms. El launcher ya tiene calidades (Baja / Media / Alta), escalado AMD FSR 1, mipmaps y MSAA.
- La lógica del juego está traducida de antemano a código nativo, así que no hay coste de emulación, pero el código traducido y el runtime aún no se han perfilado en CPUs lentas.
- GPU por cuadro en un partido (RX 5500, `WIICOMPILED_GPU_BREAKDOWN=1`): 3,0 ms a x1 (939x528), 7,5 ms a x2, 11,6 ms a x4. Cada cuadro tiene unas 28 pasadas de render: el juego hace ~27 copias del EFB por cuadro (en la Wii son baratas, en PC cada una cierra la pasada y la siguiente vuelve a cargar color y profundidad). La escena principal son 0,6 ms; las otras 27 pasadas pequeñas suman 1,3 ms más 0,7 ms de copias. El filtrado anisotrópico y los mods de texturas no cambian el coste. En gráficas integradas con poco ancho de banda y en Switch, reducir esas pasadas es lo que más importa.
- Memoria: el juego usa unos 630 MB en un partido (D3D12; unos 100 MB menos con Direct3D 11). El buffer de subida de uniforms bajó de 24 a 8 MB por cuadro (el pico medido es 1 MB, 2,9 MB con interpolación a 180 FPS): 44 MB menos de RAM y 22 MB menos de VRAM, sin cambio de FPS. El resto: la memoria de la Wii emulada (24 + 128 MB), el código traducido (94 MB), el driver de gráficas (~100 MB).
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
