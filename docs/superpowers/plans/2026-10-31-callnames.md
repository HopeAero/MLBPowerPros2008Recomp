# Callnames nuevos: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Objetivo:** que un mod añada nombres nuevos que los dos comentaristas dicen en el partido, que salgan en la lista de nombres del editor del juego y que se puedan elegir en PowerUp+, en PC y en Switch.

**Arquitectura:**
- Un módulo nuevo del runtime (`callnames`) lee `callnames/` de los mods activos y pasa los WAV a DSP-ADPCM.
- Sirve `voice14/15/17.bin` como archivos virtuales: cabecera e índice ampliados, más las frases nuevas añadidas al final del original.
- Regenera `env_voice_data.pack` (lista del editor) por el camino `project/` existente.
- Sustituye por nativos dos funciones de rangos de bancos y las funciones de la lista que llevan los tamaños escritos en el código.
- PowerUp+ añade los nombres a su selector de voz.

**Tecnología:** C++20 del runtime (parche nuevo `tools/patches/wiicompiled-callnames.patch`), toml11 (ya en el runtime), `DvdFstContract::Virtual` (hoy en el parche Switch), C# de PowerUp+ (`tools/patches/powerupplus-2008.patch`), Python 3.12 para herramientas y pruebas, Piper TTS (local) para el audio de prueba.

**Diseño:** `docs/superpowers/specs/2026-10-31-callnames-design.md` (espejo en `docs/en/superpowers/specs/`). Hechos de RE: `docs/ingenieria-inversa.md`, sección "Bancos de voz y nombres de jugador (callnames)".

## Restricciones globales

- **`VoiceId` nuevos:** 7223-10239. Índice local = `VoiceId − 0x800`.
- **Bancos y variantes:**

| Banco | Base | Variante del toml |
|---|---|---|
| 14 | `0x11000` | `normal` |
| 15 | `0x13000` | `excited` (si falta, `normal`) |
| 17 | `0x16000` | `full` (si falta, `normal`) |

- **Formato del mod:** `mods/<mod>/callnames/callnames.toml` con `[[callname]]` `voice`, `list` (≤ 15 caracteres), `normal` (obligatorio), `excited` y `full` (opcionales).
- **WAV admitidos:** PCM de 16 bits, mono o estéreo, cualquier frecuencia, ≤ 5 s por variante. Salida: mono a 22.050 Hz.
- **Texto:** se quitan las tildes ("Acuña" pasa a "Acuna") y se borra cualquier carácter que no esté en la tabla del juego. La conversión sale en el log.
- **Conflictos:** si dos mods declaran el mismo `voice`, gana el primero en orden de carga y se avisa en el log. Un callname inválido se descarta con aviso y el resto sigue cargando.
- **Sectores** de cada banco por debajo de `0x10000`: los nombres que no quepan se descartan con aviso.
- **Sin mods de callnames**, cero cambios de comportamiento: no se registran archivos virtuales y los nativos dan resultados bit a bit iguales a los originales.
- **Nunca** entra en el repo ni en un mod audio del juego, bancos, packs, audio del usuario ni audio Piper. Las pruebas usan datos sintéticos generados por el propio test o archivos en `local/`.
- **Nativos:** registrados con `PPC_NATIVE_OVERRIDE*` y **nunca** bajo `#if` (regla de `docs/traspaso.md`: el traductor lee las registraciones sin preprocesador).
- **Documentación:** en español con espejo en `docs/en/`. La RE nueva va a `docs/ingenieria-inversa.md` y `docs/en/reverse-engineering.md`, con los PDF reconstruidos (`tools/build_re_pdf.py`).
- **Python:** `C:\Users\david\.pyenv\pyenv-win\versions\3.12.0\python.exe`. Antes de cada commit, `tools/check_no_game_data.py` (hook) tiene que pasar.
- **Pruebas en el juego:** cortas y desde estados conocidos (`WIICOMPILED_MATCH_BENCH=1`, `tools/probe/live.py`). Nunca partidos enteros a mano, y cada prueba se para en cuanto se cumple su condición.

## Puntos de revisión

1. **Callnames y MLB Life:** los nombres nuevos tienen que sonar también en MLB Life y en Temporada, no solo en el partido de prueba. La prueba de la tarea 7 incluye una partida de MLB Life (con `live.py`, hasta el primer bateador).
2. **Mod quitado:** si un roster apunta a un `VoiceId` nuevo y el mod de callnames ya no está activo, el juego no puede colgarse. Se espera silencio, igual que con una voz vacía. Prueba en la tarea 7.
3. **WAV raro** (8 bits, 24 bits, float, vacío, de más de 5 s, cabecera rota): se descarta con aviso, sin colgar el arranque. Prueba en la tarea 3.
4. **Cambiar el WAV sin cambiar el nombre del archivo** tiene que regenerar la caché (la clave de caché incluye el contenido y no solo la fecha). Prueba en la tarea 5.
5. **Lista del editor con una letra sin nombres originales** (la X en la lista B) más un nombre nuevo con esa inicial: la tabla de letras tiene que dejar de valer -1 en esa letra. Prueba en la tarea 8.

---

### Tarea 1: RE restante (lista del editor, aceptación del `VoiceId`, montón e índice)

Solo investigación y documentación, sin código del runtime. Lo que se averigua aquí decide los detalles de las tareas 6 y 8.

**Archivos:**
- Modificar: `docs/ingenieria-inversa.md`, `docs/en/reverse-engineering.md` (sección de callnames)
- Scripts de apoyo: solo en el scratchpad o en `local/tmp/callnames/`

**Interfaces:**
- Produce (documentado en la sección de RE):
  - qué lista o listas enseña el editor y con qué funciones;
  - todas las direcciones que usan `0x77B`, `0x3CA` o `0x3D` o las posiciones `0x40000020`, `0x40001E0C`, `0x4000FEB4`, `0x40010ED0`, `0x40010DDC`, `0x4001807C` y `0x4000FE80` del pack 3081;
  - si el editor recorta `VoiceId`;
  - quién rellena `0x8029C1A0` y `0x8029C2D8`, y de qué parte del banco salen los umbrales;
  - qué montón reserva el índice de cada banco, cuánto sitio libre queda y si el aumento máximo (bancos 15/17 de `0xE42` a `0x1437` + N entradas de 4 bytes) cabe.

- [ ] **Paso 1:** con el ayudante de capstone (el script `ppc.py` de la sesión de RE, o uno equivalente que cargue `main.dol` y `local/generated/dolrecomp/rel-linked/linked-analysis.dol`), listar los llamadores de `0x807EE310`, `0x807EE2D0`, `0x807EE288`, `0x807EE240`, `0x807EE200`, `0x807EE280`, `0x807EE0C8` y `0x807EDF4C`, y los `li`/`cmplwi` con `0x77B`, `0x3CA` y `0x3D` en `0x807E0000`-`0x807F0000`. **Esperado:** una lista de funciones del menú del editor. Documentarla.
- [ ] **Paso 2:** prueba corta en el modo Edición con `tools/probe/live.py`: crear un jugador, abrir la elección de nombre de voz, hacer `shot:` de la lista y `peek:` del `VoiceId` del jugador editado tras elegir un nombre de la A y otro de la B. **Esperado:** se sabe qué lista sale, si se navega por letras y que `VoiceId` = clave − `0x8000`. Parar en cuanto se vea.
- [ ] **Paso 3:** con un `game_data.pack` de prueba en un mod local (mismo método que `set-voice` de la tarea 2), dar a Jeter un `VoiceId` de 7300 y ejecutar `WIICOMPILED_MATCH_BENCH=1` con `WIICOMPILED_DEBUG_DVD=1`. **Esperado:** sin cuelgue, y lecturas de `voice14.bin`/`voice15.bin`/`voice17.bin` hacia el índice local `0x1484`, o ninguna lectura con silencio si los rangos originales lo rechazan. Las dos cosas confirman la linealidad y la falta de recorte del lado del partido. Documentar cuál pasa.
- [ ] **Paso 4:** `peek:` de `0x8029C1A0` (18 × 12 bytes) y de `0x8029C2D8` (18 × `0x80`) en el menú principal; desensamblar quién los escribe (buscar `0x8029C1A0`/`0x8029C2D8` como constante o base). **Esperado:** se conoce el origen de los umbrales y el montón que reserva el índice. Documentar si el aumento del índice cabe; si no cabe, anotar que las tareas 6 y 7 usan la variante con nativo de `0x8003ACF0`.
- [ ] **Paso 5:** reconstruir los dos PDF y hacer commit.

```bash
python tools/build_re_pdf.py && python tools/build_re_pdf.py docs/en/reverse-engineering.md
git add docs/ingenieria-inversa.md docs/en/reverse-engineering.md
git commit -m "RE: editor callname list users, VoiceId range acceptance, voice index heap"
```

### Tarea 2: `tools/callnames.py` y audio de prueba con Piper

**Archivos:**
- Crear: `tools/callnames.py`
- Crear: `tools/test_callnames.py`
- Local, sin commit: `local/tools/piper/`, `local/tmp/callnames/mod-acuna/`

**Interfaces:**
- Produce:
  - `decode_phrase(bank_path: str, local: int) -> tuple[list[int], int]`: muestras PCM y frecuencia.
  - `decode_adpcm(data: bytes, coefs: tuple[int, ...], samples: int) -> list[int]`.
  - CLI `callnames.py decode <voiceN.bin> <VoiceId> <out.wav>` (local = `VoiceId − 0x800`).
  - CLI `callnames.py set-voice <game_data.pack> <apellido> <VoiceId> <out.pack>`: pone el `VoiceId` (14 bits en `0x92.2`) de los jugadores con ese apellido. Usa `bits_at` y el bloque de `tools/roster/align_2007_2008.py` y escribe un pack para un mod de prueba local.

- [ ] **Paso 1:** escribir el test `test_decode_adpcm_known_frame`, con un cuadro ADPCM sintético construido en el test, sin datos del juego. Con coefs `(0, 0)` en el índice 0, el byte de cabecera `0x00` (escala 1) y los nibbles `1, 2, …`, las muestras esperadas son `[1, 2, …]`. Y `test_set_voice_bits`: un registro de `0xB0` bytes a cero con `VoiceId` 7300 da `bits_at(rec, 0x92*8+2, 14) == 7300`, y los demás bits siguen a cero.
- [ ] **Paso 2:** `python tools/test_callnames.py`. **Esperado:** FAIL (no existe el módulo).
- [ ] **Paso 3:** implementar el decodificador. Es el de `dspdec.py` de la sesión de RE: escala `1 << (ps & 15)`, coefs `(ps >> 4) * 2`, `(s * escala * 2048 + c1 * h1 + c2 * h2 + 1024) >> 11` y saturación.
- [ ] **Paso 4:** `python tools/test_callnames.py`. **Esperado:** PASS.
- [ ] **Paso 5:** Piper local.
  - Descargar `piper` (versión de Windows) a `local/tools/piper/` y una voz en español cuya `MODEL_CARD` tenga licencia abierta (CC0, CC-BY o MIT; anotar la licencia en `local/tools/piper/LICENSE-voz.txt`).
  - Generar en `local/tmp/callnames/mod-acuna/callnames/`:
    - `acuna.wav`: "Acuña";
    - `acuna_hype.wav`: "¡Acuña!", con `--length_scale 0.85` y el tono subido un 8 % (remuestreo en Python);
    - `acuna_full.wav`: "Ronald Acuña Junior".
  - Escribir `callnames.toml` con `voice = 7300` y `list = "ACUNA"`.
- [ ] **Paso 6:** commit (solo `tools/callnames.py` y `tools/test_callnames.py`).

### Tarea 3: WAV y codificador DSP-ADPCM en el runtime

**Archivos** (en el árbol `local/wiicompiled-mlb/src/runtime`, que acaba en el parche de la tarea 9):
- Crear: `runtime/include/callnames.h`
- Crear: `runtime/src/callnames/audio.cpp`
- Crear: `runtime/tests/callnames_tests.cpp`
- Modificar: `runtime/CMakeLists.txt` (`add_executable(mkw_callnames_tests …)`, como `mkw_mod_saves_tests`)

**Interfaces:**
- Produce, en `namespace callnames`:
  - `bool ReadWav(const std::filesystem::path&, std::vector<int16_t>& mono22050, std::string& error)`: PCM16 mono/estéreo, remuestreo lineal a 22.050 Hz, error si no es PCM16, si está vacío o si dura más de 5 s.
  - `struct Adpcm { std::array<int16_t,16> coefs; std::vector<uint8_t> data; uint32_t samples; }`.
  - `Adpcm Encode(const std::vector<int16_t>&)`: determinista.
  - `std::vector<int16_t> Decode(const Adpcm&)`.
  - `uint32_t AdpcmBytes(uint32_t samples)` = `(samples / 14) * 8 + (samples % 14 ? 1 + (samples % 14 + 1) / 2 : 0)`; el número de nibbles es `2 × AdpcmBytes`.
  - `void Normalize(std::vector<int16_t>&, int16_t peak = 29000)`: el pico de los nombres originales ronda 29.000.

- [ ] **Paso 1:** tests que tienen que fallar:
  - `AdpcmBytes(8738) == 0x1382` y nibbles `0x2704` (valores de la frase de Jeter del banco 14);
  - seno de 440 Hz y 1 s generado en el test, `Decode(Encode(x))` con SNR ≥ 20 dB;
  - dos `Encode` de la misma entrada dan los mismos bytes;
  - `ReadWav` acepta un WAV estéreo de 44.100 Hz escrito por el test (salida de 22.050 muestras por segundo);
  - `ReadWav` rechaza, con texto en `error` y sin excepción, WAV de 8 bits, de 24 bits, float, vacíos, de más de 5 s y con cabecera cortada (punto de revisión 3).
- [ ] **Paso 2:** compilar y ejecutar `mkw_callnames_tests`. **Esperado:** FAIL (faltan los símbolos).
- [ ] **Paso 3:** implementar `ReadWav`, `Normalize` y `AdpcmBytes`, y `Encode`/`Decode` con el algoritmo estándar de coeficientes DSP (autocorrelación por bloques de 14 muestras y 8 pares de coeficientes, como el codificador público `DSPCorrelateCoefs`/gc-dspadpcm-encode). Por cada cuadro se prueba cada par con las escalas 0-11 y se elige el de menor error.
- [ ] **Paso 4:** ejecutar `mkw_callnames_tests`. **Esperado:** PASS.
- [ ] **Paso 5:** commit del árbol de trabajo. El parche se genera en la tarea 9.

### Tarea 4: lectura de los mods (`callnames.toml`)

**Archivos:**
- Modificar: `runtime/include/mods.h` (campo `bool callnames` en `Mod`, igual que `project`)
- Modificar: `runtime/src/mods.cpp` (detección de `callnames/`, igual que `mod.project`)
- Crear: `runtime/src/callnames/collect.cpp`
- Test: `runtime/tests/callnames_tests.cpp`

**Interfaces:**
- Produce:
  - `struct Callname { uint32_t voice; std::string list; std::u16string listGame; std::filesystem::path normal, excited, full; std::string mod; }`;
  - `std::vector<Callname> Collect(const std::vector<mods::Mod>& enabled, std::vector<std::string>& warnings)`;
  - `std::u16string ToGameText(const std::string& utf8, std::string& dropped)` con la tabla de caracteres del roster (la misma que usa `mods.cpp` para los nombres).

- [ ] **Paso 1:** tests, con carpetas temporales y WAV sintéticos:
  - `excited` y `full` toman el valor de `normal` cuando faltan;
  - `voice` 7222 y 10240 se rechazan con aviso;
  - `list` de 16 caracteres se rechaza;
  - dos mods con `voice = 7300`: gana el primero y queda un aviso;
  - `ToGameText("Acuña Jr.")` da el texto de "Acuna Jr." y `dropped` vacío;
  - `ToGameText("大谷")` deja `dropped` con los dos caracteres.
- [ ] **Paso 2:** ejecutar los tests. **Esperado:** FAIL.
- [ ] **Paso 3:** implementar con `toml::parse` (como el `mod.toml` de `mods.cpp`), las rutas relativas a `callnames/` y el orden de mods de `mods::Discover`.
- [ ] **Paso 4:** ejecutar los tests. **Esperado:** PASS.
- [ ] **Paso 5:** commit.

### Tarea 5: construcción del banco virtual y caché

**Archivos:**
- Crear: `runtime/src/callnames/bank.cpp`
- Test: `runtime/tests/callnames_tests.cpp`

**Interfaces:**
- Consume:
  - `Adpcm`, `Encode`, `ReadWav`, `Normalize` y `AdpcmBytes` (tarea 3);
  - `Callname` (tarea 4);
  - `DvdFstContract::Virtual::File` (tarea 7 lo mueve a PC; aquí basta con la estructura).
- Produce:
  - `struct BankBuild { DvdFstContract::Virtual::File file; uint32_t count; std::vector<uint32_t> added; std::vector<std::string> warnings; }`;
  - `bool BuildBank(int bank, const std::filesystem::path& original, const std::vector<std::pair<uint32_t, Adpcm>>& phrases, const std::filesystem::path& cacheDir, BankBuild& out, std::string& error)`.
- **Disposición del archivo virtual:**
  1. sector 0 nuevo (en `bytes`): la cabecera original con el sector del índice, el número de frases (en las dos copias) y los bytes del índice (= número × 4) cambiados;
  2. sectores `1 … índice_original − 1` del original (pieza de archivo);
  3. frases nuevas, cada una de `0x800` bytes de cabecera más los datos rellenos hasta el siguiente sector, en un archivo de caché `voiceN.ext`;
  4. índice nuevo: `u16 sector[count]` y después `u16 ms[count]`, con los locales originales iguales, los locales intermedios con el sector y los ms de la frase local 0 (relleno), y los nuevos con lo suyo; relleno hasta el siguiente sector;
  5. la cola original después del índice (los `0x80` bytes y el relleno) copiada igual.
- **Cabecera de una frase nueva:** copia de la cabecera de la frase local 3 del mismo banco, con `+0x00` = bytes, `+0x04` = (byte alto de la plantilla `<< 16`) `| 22050`, `+0x10` = ms (`round(samples × 1000 / 22050)`), `+0x88` = nibbles, `+0x8C` = 2, `+0x90` = coefs, `+0xB0` = 0, `+0xB2` = primer byte de datos y `+0xB4`/`+0xB6` = 0.
- **Caché:** en `UserData/Cache/mods/callnames/`, con un archivo `sources` que guarda la versión del formato y, por cada WAV, ruta, tamaño y un hash del contenido. No basta con la fecha (punto de revisión 4).

- [ ] **Paso 1:** tests sobre un banco sintético que construye el propio test (cabecera `0x005A8AAB`, 4 frases, índice al final):
  - con frases nuevas en los locales 6 y 9: `count == 10`, los locales 0-3 tienen los sectores originales, los locales 4, 5, 7 y 8 apuntan al sector del local 0, y leer el archivo virtual en el sector del local 9 da una cabecera con `+0x10` igual a los ms esperados;
  - un nombre que llevaría el sector a `0x10000` o más se descarta con aviso;
  - cambiar los bytes de un WAV sin cambiar ni el nombre ni el tamaño regenera la caché.
- [ ] **Paso 2:** ejecutar los tests. **Esperado:** FAIL.
- [ ] **Paso 3:** implementar `BuildBank`.
- [ ] **Paso 4:** ejecutar los tests. **Esperado:** PASS.
- [ ] **Paso 5:** commit.

### Tarea 6: nativos de rangos de bancos

**Archivos:**
- Crear: `runtime/src/hle/mlb/callnames_natives.cpp`
- Test: `runtime/tests/callnames_tests.cpp` y el arnés de comparación con el original (el de qemu de la Switch, `local/tmp/cpu/P23-*`)

**Interfaces:**
- Consume: `callnames::BankCount(int bank) -> uint32_t`: 0 si el banco no tiene callnames. Lo rellena la tarea 7 antes de `DVDInit`.
- Produce:
  - `uint32_t callnames::PhraseBank(uint32_t id)` y `int32_t callnames::PhraseLocal(uint32_t id)`, puras y probables en el host;
  - nativos `PPC_NATIVE_OVERRIDE(8003B398, …)` y `PPC_NATIVE_OVERRIDE(8003B530, …)`, que llaman a esas funciones con `r3`.
- **Reglas:**
  - Las dos funciones reproducen la tabla de rangos del original (`docs/ingenieria-inversa.md`, incluido el final de `0x8003B510`/`0x8003B6AC`).
  - Si `BankCount(b) > 0` para b de 14, 15 o 17, el límite de ese banco pasa a ser `BankCount(b) − 1`.
  - Si la tarea 1 decidió la variante con nativo de `0x8003ACF0`, se añade aquí con la misma regla: los locales `≥` del número de frases original van a la extensión.

- [ ] **Paso 1:** tests. Sin callnames, `PhraseBank`/`PhraseLocal` coinciden con la referencia en todos los ids `0 … 0x3FFFF`. La referencia son los resultados del original sacados con el arnés de qemu y guardados como tabla de tramos (inicio, fin, banco, base), no como volcado. Con `BankCount(15) = 0x1485`, `PhraseBank(0x14484) == 15` y `PhraseLocal(0x14484) == 0x1484`, y `PhraseBank(0x15000) == 16` no cambia.
- [ ] **Paso 2:** ejecutar los tests. **Esperado:** FAIL.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** ejecutar los tests. **Esperado:** PASS.
- [ ] **Paso 5:** commit.

### Tarea 7: archivos virtuales en PC, montaje al arrancar y prueba de partido

**Archivos:**
- Modificar: `runtime/include/hle/dvd_contract.h` (traer `DvdFstContract::Virtual` del parche Switch **sin** la guarda `__SWITCH__`, para que lo use también PC)
- Modificar: `runtime/src/hle/storage/dvd.cpp` (servir las entradas virtuales en `DVDReadPrio` y en el tamaño de la FST, como en Switch)
- Modificar: `runtime/src/mods.cpp` (en `mods::Apply`, antes de `DVDInit`: `callnames::Mount(enabled)`)
- Crear: `runtime/src/callnames/mount.cpp`

**Interfaces:**
- Consume: `Collect` (tarea 4), `BuildBank` (tarea 5), `Virtual::Register`.
- Produce: `void callnames::Mount(const std::vector<mods::Mod>& enabled)`. Codifica (con caché), construye los bancos 14/15/17 con callnames, registra sus `Virtual::File` con `dvdPath` `/sound/voiceN.bin`, rellena `BankCount` y escribe los avisos en el log con la etiqueta `[callnames]`. Sin callnames no registra nada.

- [ ] **Paso 1:** test del host: un `Virtual::File` con piezas de archivo, bytes y ceros se lee igual que la concatenación esperada en lecturas que cruzan piezas. Reutilizar las pruebas del contrato si ya existen en el parche Switch.
- [ ] **Paso 2:** ejecutar el test. **Esperado:** FAIL hasta mover `Virtual`; después, PASS.
- [ ] **Paso 3:** implementar `Mount` y el servicio de lecturas virtuales en PC. Traducir y compilar con `tools/wiicompiled-translate.ps1` y la compilación de siempre.
- [ ] **Paso 4: prueba de partido sin mods.** `WIICOMPILED_MATCH_BENCH=1 WIICOMPILED_DEBUG_DVD=1 WIICOMPILED_MATCH_SECONDS=90`. **Esperado:** las lecturas de `voice*.bin` son las mismas, en el mismo orden, que en el registro de referencia (repetir la ejecución de referencia con el binario anterior si hace falta).
- [ ] **Paso 5: prueba de partido con mod.** Mod local de prueba en `UserData/mods`: `local/tmp/callnames/mod-acuna/` (tarea 2) más `project/game_data.pack` hecho con `callnames.py set-voice … Jeter 7300`. **Esperado:** el log tiene `[callnames] 1 nombre` (o equivalente) y las lecturas de `voice14.bin`/`voice15.bin`/`voice17.bin` caen en sectores de la extensión, al menos la del banco 17 con el primer bateador; ni cuelgue ni aviso de lectura DVD.
- [ ] **Paso 6: MLB Life** (punto de revisión 1). Con el mismo mod, `live.py` hasta el primer bateador de una partida de MLB Life con Jeter en el equipo rival o en el propio. **Esperado:** lectura de la extensión. Si Jeter no sale, usar en el `set-voice` otro jugador del equipo de la partida de prueba.
- [ ] **Paso 7: mod quitado** (punto de revisión 2). Dejar el `game_data.pack` con 7300 y quitar `callnames/`. **Esperado:** el partido sigue, sin lecturas fuera del archivo y en silencio en ese nombre.
- [ ] **Paso 8:** commit.

### Tarea 8: lista del editor (`env_voice_data.pack`)

**Archivos:**
- Crear: `runtime/src/callnames/voice_list.cpp`
- Modificar: `runtime/src/hle/mlb/callnames_natives.cpp` (nativos de las funciones de la lista que encontró la tarea 1)
- Modificar: `runtime/src/mods.cpp` (añadir el pack regenerado a los reemplazos de `project/`, antes de `BuildProjectBin` en PC y de su equivalente virtual en Switch)

**Interfaces:**
- Consume: `Callname::listGame` (tarea 4) y las direcciones de la tarea 1.
- Produce:
  - `bool BuildVoiceList(const std::vector<uint8_t>& original, const std::vector<Callname>&, std::vector<uint8_t>& out, VoiceListLayout& layout)`;
  - `struct VoiceListLayout { uint32_t keysA, namesA, countA, keysB, namesB, countB, letters; }`: posiciones dentro del pack nuevo.
- **Reglas:**
  - Los nombres nuevos (clave `0x8000 + voice`) entran en la lista que enseña el editor, en orden alfabético.
  - En la lista B se meten dentro del grupo de su inicial y se recalcula la tabla de letras de `+0xFE80`. Una letra que vale -1 pasa a valer su posición (punto de revisión 5).
  - Las listas ampliadas van al final del pack.
  - Los nativos leen `VoiceListLayout` en lugar de las constantes. Sin callnames, el pack no se regenera y los nativos usan la disposición original, con resultados idénticos.

- [ ] **Paso 1:** tests sobre un pack sintético con la misma disposición (listas A, B y C pequeñas, tabla de letras y un -1 en la X):
  - "ACUNA" queda entre "ABAD…" y "B…";
  - "XANDER" en la B pone la X en su posición;
  - buscar la clave `0x8000 + 7300` devuelve "ACUNA";
  - todas las claves originales devuelven el mismo nombre que antes.
- [ ] **Paso 2:** ejecutar los tests. **Esperado:** FAIL.
- [ ] **Paso 3:** implementar `BuildVoiceList` y los nativos.
- [ ] **Paso 4:** ejecutar los tests. **Esperado:** PASS.
- [ ] **Paso 5:** prueba corta en el editor con `live.py`, con el mod de la tarea 7. **Esperado:** "ACUNA" aparece en su letra, y al elegirlo el `peek:` del jugador da `VoiceId` 7300; al jugar ese jugador en `MATCH_BENCH` se lee la extensión.
- [ ] **Paso 6:** commit.

### Tarea 9: parche del runtime y conversor de rosters

**Archivos:**
- Crear: `tools/patches/wiicompiled-callnames.patch` (diff de los archivos de las tareas 3-8 sobre el árbol con todos los parches anteriores)
- Modificar: `tools/wiicompiled-translate.ps1` (añadir `'wiicompiled-callnames'` después de `'wiicompiled-life-history-year'`)
- Modificar: `runtime/src/mods.cpp` (`Voice2008`: deja pasar 7223-10239), y por tanto el mismo parche
- Modificar: `tools/roster/powerup_to_2008.py` (`voice_2008`: deja pasar 7223-10239, y `voice_2007(v)` para ese rango devuelve `ONLY_2008_VOICE + v`)
- Test: el test existente del conversor (`tools/test_*` del roster) o uno nuevo `tools/roster/test_voice_passthrough.py`

- [ ] **Paso 1:** test: `voice_2008(7300) == 7300`, `voice_2007(7300) == 17300`, y la ida y vuelta de un registro con `VoiceId` 7300 sale idéntica por Python. El runtime tiene que dar los mismos bytes: el test existente de igualdad Python/runtime, si lo hay, o un caso en `mkw_callnames_tests`.
- [ ] **Paso 2:** ejecutar el test. **Esperado:** FAIL.
- [ ] **Paso 3:** implementar el paso directo en los dos sitios y subir `kRosterConverter` a 14 (comentario: `14: callname voices pass through`).
- [ ] **Paso 4:** ejecutar el test. **Esperado:** PASS.
- [ ] **Paso 5:** generar el parche y comprobarlo en limpio con `tools/wiicompiled-translate.ps1 -PrepareOnly` sobre un clon nuevo. **Esperado:** se aplican todos los parches y el árbol resultante coincide con el de trabajo (`diff -rq` de `runtime/`).
- [ ] **Paso 6:** commit de `tools/patches/wiicompiled-callnames.patch`, `tools/wiicompiled-translate.ps1`, `tools/roster/powerup_to_2008.py` y el test.

### Tarea 10: PowerUp+

**Archivos:**
- Modificar: en `local/powerupplus/src`, `PowerUp/PowerUp/GameSave/PowerPros2008/PowerPros2008Roster.cs` (`VoiceLibraryLines`)
- Modificar: `tools/patches/powerupplus-2008.patch` (regenerado)
- Test: `PowerUp/PowerUp.Tests/Libraries/VoiceLibraryTests.cs`

**Interfaces:**
- Produce: `VoiceLibraryLines` añade, con `ModsDirectory` definido, una línea `Callname_<list>,<10000 + voice>` por cada callname válido de los mods activos. La lista de activos sale del mismo sitio que ya usa PowerUp+ para `ModsDirectory`; si no hay ninguno, del `Config.toml` del juego. El prefijo `Callname_` mantiene el formato `Nombre_Apellido` que necesita el soundex.

- [ ] **Paso 1:** test con un `ModsDirectory` temporal y un mod con `voice = 7300` y `list = "ACUNA"`: la librería devuelve `"Callname ACUNA"` para 17300, y el `.dat` exportado en modo 2008 lleva `VoiceId` 7300.
- [ ] **Paso 2:** `tools/powerupplus.ps1` con los tests (`dotnet test` con el SDK de `local/tools/dotnet10`). **Esperado:** FAIL.
- [ ] **Paso 3:** implementar. El TOML se lee con un lector mínimo de las claves `voice` y `list` dentro de `[[callname]]`, sin dependencias nuevas.
- [ ] **Paso 4:** los tests pasan. Prueba en la interfaz (Playwright en `127.0.0.1:5124`, receta de `roster-2008`): el selector de voz enseña "Callname ACUNA".
- [ ] **Paso 5:** regenerar `tools/patches/powerupplus-2008.patch`, publicar con `tools/powerupplus.ps1 -Publish` y hacer commit del parche.

### Tarea 11: Switch

**Archivos:**
- Modificar: `tools/patches/wiicompiled-switch.patch`, `-2.patch` y `-3.patch` (regenerados con `local/tmp/mkswitch.sh`; quitar la copia de `Virtual`, que ahora está en el parche de callnames)

- [ ] **Paso 1:** `bash local/tmp/mkswitch.sh`, y después `bash local/tmp/treecheck.sh` + `diff -rq` (`docs/traspaso.md`). **Esperado:** se aplica la cadena con el parche nuevo y el árbol coincide.
- [ ] **Paso 2:** `MLB_SWITCH_JOBS=4 bash tools/switch/build-nro.sh --graphics vulkan`. **Esperado:** la NRO compila.
- [ ] **Paso 3:** prueba en consola (la hace el usuario, con nxlink): el mod de prueba en la SD y el partido de prueba. **Esperado:** `[callnames]` en el log de nxlink, lecturas de la extensión, ninguna copia de los bancos en la SD y un arranque igual de rápido que sin el mod.
- [ ] **Paso 4:** commit de los parches Switch.

### Tarea 12: documentación y escucha final

**Archivos:**
- Modificar: `docs/mods.md`, `docs/en/mods.md` (sección "callnames/": formato, límites, rango de `VoiceId`, texto sin tildes, cómo elegirlo en PowerUp+ y en el editor)
- Modificar: `docs/traspaso.md` (sobrescribir con el estado de este trabajo, como se hace en cada entrega)
- Modificar: `docs/superpowers/specs/2026-10-31-callnames-design.md` y el espejo, si algo cambió al construir

- [ ] **Paso 1:** escribir la documentación y ejecutar `tools/check_no_game_data.py`.
- [ ] **Paso 2:** escucha del usuario: el mod Piper de Acuña puesto en un jugador. El usuario confirma que el principal dice "Acuña" (normal y emocionado) y el de fondo, "Ronald Acuña Junior".
- [ ] **Paso 3:** commit.
