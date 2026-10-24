# Corrección de fallos

*English: [../en/roadmap/bugs.md](../en/roadmap/bugs.md)*

**Estado:** continuo · [Roadmap](../../ROADMAP.es.md)

## Por qué
Cada versión se prueba sobre todo en una PC. Los jugadores encuentran lo que esa PC no puede: otras gráficas, otros mandos, otras versiones de Windows, partidas largas. Sus reportes son lo más valioso que tiene el proyecto.

## Cómo funciona
- **Reportar:** [abre un issue](https://github.com/HopeAero/MLBPowerPros2008Recomp/issues/new/choose) con el formulario de fallos: versión, CPU, GPU, mando, qué pasó y `UserData\Logs\<último>\console.log`. Para Temporada o MLB Life, la partida ayuda mucho.
- **Ordenar:** cada reporte recibe una etiqueta (`bug`, `crash`, `controller`, `graphics`, `audio`, `season`, `mlb-life`, `modding`) y un arreglo o una pregunta de vuelta.
- **Reproducir primero:** el arreglo empieza con una prueba corta desde una partida guardada que muestra el fallo (tools/probe), y la misma prueba demuestra el arreglo.
- **Publicar:** lo urgente sale en versiones de corrección (como la v0.1.4.1); lo demás, en la siguiente versión. Cada versión se publica tras una instalación limpia desde el disco y una prueba de mando.

## Abierto conocido
- Cuelgue raro al arrancar (cerrar y volver a abrir). Sin explicar; visto en unos 2 de 9 arranques automáticos una vez, nunca desde un arreglo de octubre.
- Copiar / pegar / importar / exportar contraseñas aún sin Wii Remote.
- Los estadios nuevos solo salen en Exhibición.

## Casillas
- [x] Formulario de fallos y etiquetas
- [ ] Responder cada reporte nuevo en pocos días
- [ ] Cada arreglo tiene una prueba que lo reproduce
