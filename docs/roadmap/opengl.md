# OpenGL

*English: [../en/roadmap/opengl.md](../en/roadmap/opengl.md)*

**Estado:** investigación · [Roadmap](../../ROADMAP.es.md)

## Por qué
El juego dibuja con Dawn (WebGPU), que hoy usa Direct3D 12 o Vulkan. Gráficas antiguas y algunos controladores no tienen ninguno de los dos funcionando bien. OpenGL / OpenGL ES es además el camino más directo a Switch, donde Dawn no tiene backend propio.

## Dónde estamos
- El paquete precompilado de Dawn que usa el proyecto viene sin sus backends de OpenGL y OpenGL ES.
- Dawn tiene un backend OpenGL ES en su código fuente; activarlo implica compilar Dawn desde fuente.

## Plan
1. Compilar Dawn desde fuente con los backends OpenGL / OpenGL ES y correr el juego sobre ellos en Windows y Linux.
2. Encontrar lo que el dibujo del juego necesita y esos backends no tienen, y resolverlo.
3. Ofrecer OpenGL en el launcher, junto a Vulkan y Direct3D 12, con la prueba del plan de rendimiento.

## Cómo se verifica
La misma prueba de rendimiento y las mismas comprobaciones visuales (menús, un partido, cada estadio) en OpenGL que en Vulkan.

## Casillas
- [ ] Dawn compilado con OpenGL / OpenGL ES
- [ ] El juego arranca y juega un partido en OpenGL
- [ ] OpenGL elegible en el launcher
