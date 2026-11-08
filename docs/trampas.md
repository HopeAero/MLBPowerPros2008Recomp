# Trampas del entorno

*English: [en/pitfalls.md](en/pitfalls.md)*

Fallos silenciosos o engañosos del entorno de trabajo, una línea con fecha por trampa. El traspaso de cada tanda está en [traspaso.md](traspaso.md).

- **2026-11-07 · `pipeline_cache.db` envenenado:** un arranque que falla (p. ej. un benchmark de partido con equipos de Arrange sin datos cargados) puede dejar filas en `local/wiicompiled-mlb/build/UserData/Cache/pipeline_cache.db` que abortan **todos** los arranques siguientes en el hilo de pipelines (`aurora::gx::build_shader` → `Module::fatal`, visto con lldb), aunque el código esté bien. Arreglo: borrar las filas nuevas (`DELETE FROM pipeline_cache WHERE rowid > <última buena>`) o restaurar una copia. Antes de un arranque arriesgado, copia el archivo.
- **2026-11-07 · Riesgo de downgrade v2/v3 del guardado de Arrange:** con este parche, cualquier guardado con la opción de marcas O de uniformes activa migra el bloque `MLBARRMK` a la versión 3 en `0xD2E00`. Un runtime v2 antiguo que lea ese guardado ve el bloque como Invalid en `0xD3000` y, si guarda con cambios, pisa parte del v3 (se pierden las elecciones de uniforme). Es aceptable mientras v2 no esté ampliamente distribuido; una vez actualizado, no volver a un runtime anterior con esos guardados.
