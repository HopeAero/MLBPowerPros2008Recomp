# Editor de callnames (Callname Studio)

**Power Pros Wii 2008 Callname Studio** crea nombres de comentarista sin escribir TOML. Se abre desde **AJUSTES → HERRAMIENTAS → EDITOR DE CALLNAMES**. `CallnameStudio.exe` se instala junto al juego; usa el mismo aspecto e idioma que el Editor de temporada.

1. Elige un mod o pulsa **+ Nuevo mod**. Un mod nuevo se activa automáticamente; **Activar / Desactivar** cambia su estado en `Config.toml`.
2. Pulsa **Añadir**. El número de voz se asigna entre 7223 y 10239 usando el primer hueco libre de todos los mods, incluso los desactivados. Puedes cambiarlo; el editor avisa si ya se usa. Si dos mods activos chocan, el juego usa el primero por nombre.
3. Escribe **Texto de la lista**. Debajo ves el resultado real del juego: sin tildes, sin caracteres incompatibles y su forma para **Search Type**. Debe tener entre 1 y 15 caracteres visibles para guardar.
4. Elige **Normal** y, si quieres, **Emocionado / Completo**. Sin estas variantes se usa Normal. Acepta WAV PCM de 16 bits o MP3, mono/estéreo, 8–192 kHz, máximo 5 segundos. Cada archivo elegido se valida y se copia a `callnames/` con un nombre seguro; **Escuchar** lo reproduce. Usa audio propio o autorizado.
5. Pulsa **Guardar**. En **PowerUp+ (MODS › EDITAR ROSTER)** aparecen como **Callname <texto>** en el selector de voz. Asigna cada voz, exporta el roster y activa tanto el roster como el mod de callnames. Reinicia el juego si estaba abierto.

Selecciona una fila para **Editar** o **Borrar**. Borrar pide confirmación y conserva el audio, por si lo usa otra entrada. Cancelar una edición también puede dejar los archivos que ya importaste; no se eliminan automáticamente.

Solo escribe en el mod elegido y en `Config.toml`; no modifica archivos del juego. Lee manifests escritos a mano y conserva sus claves desconocidas y comentarios (el formato y orden pueden cambiar). Un TOML ilegible bloquea la edición/asignación automática con un mensaje, para evitar perder datos o asignar una voz ocupada. Los audios deben estar dentro de `callnames/`.

## Compilar y comprobar

```powershell
& tools/callname-studio/build.ps1
& local/build/callname-studio/CallnameStudio.exe --selftest
python tools/callname-studio/test_cli.py local/build/callname-studio/CallnameStudio.exe
```

Comparte `ui.h`, `config.cpp`, SDL3 y Dear ImGui con los otros studios. Enlaza `collect.cpp` y `audio.cpp` del runtime: la conversión de texto y la validación de audio son las mismas.

Opciones de pruebas (índices desde cero; rutas UTF-8):

```text
--mods <carpeta> --config <Config.toml> --mod <id>
--create-mod
--add --list <texto> --normal <archivo> [--excited <archivo>] [--full <archivo>] [--voice <id>]
--update <índice> --list <texto> [--normal <archivo>] [--voice <id>]
--delete <índice>
--validate
--lang es|en --edit <índice>
```

`--create-mod` crea y activa; puede combinarse con `--add`. `--add`, `--update`, `--delete` y `--validate` terminan sin ventana. `--edit` abre el diálogo para capturas. La CLI usa la misma importación, validación y escritura que la ventana.
