# Trampas del entorno

*English: [en/pitfalls.md](en/pitfalls.md)*

Fallos silenciosos o engañosos del entorno de trabajo, una línea con fecha por trampa. El traspaso de cada tanda está en [traspaso.md](traspaso.md).

- **2026-11-07 · `pipeline_cache.db` envenenado:** un arranque que falla (p. ej. un benchmark de partido con equipos de Arrange sin datos cargados) puede dejar filas en `local/wiicompiled-mlb/build/UserData/Cache/pipeline_cache.db` que abortan **todos** los arranques siguientes en el hilo de pipelines (`aurora::gx::build_shader` → `Module::fatal`, visto con lldb), aunque el código esté bien. Arreglo: borrar las filas nuevas (`DELETE FROM pipeline_cache WHERE rowid > <última buena>`) o restaurar una copia. Antes de un arranque arriesgado, copia el archivo.
