# Callnames nuevos: diseño

Fecha: 31 de octubre de 2026. Estado: pendiente de aprobación.

## Para qué

El usuario quiere añadir nombres de jugadores actuales (Ronald Acuña Jr., Jackson Chourio, Shohei Ohtani, Yoshinobu Yamamoto…) que el juego de 2008 no tiene. Tienen que cumplir tres cosas:

1. **Se oyen en el partido:** el comentarista principal dice el apellido (en tono normal y emocionado) y el de fondo, el nombre completo, igual que con los nombres originales.
2. **Aparecen en la lista de nombres al crear o editar un jugador dentro del juego.**
3. **Se pueden asignar desde PowerUp+**, en su selector de voz.

Tiene que funcionar en PC y en Switch, y en todos los modos que usan el comentario, incluida MLB Life.

## De dónde sale el audio

Lo elige el usuario: grabado por él, TTS genérico, recortado de otro juego, etc. La herramienta acepta cualquier WAV y no genera voces.

- No se clona con IA la voz de los comentaristas reales.
- El repositorio nunca lleva audio del juego ni audio del usuario.

## Qué ya se sabe (ingeniería inversa)

Todo está en `docs/ingenieria-inversa.md`, sección "Bancos de voz y nombres de jugador (callnames)". Lo esencial:

- **Bancos de voz.** `sound/voiceN.bin` tiene sector 0 de cabecera, frases (1 sector de cabecera + datos DSP-ADPCM mono a 22.050 Hz) e índice al final (`u16 sector[]`, `u16 ms[]`).
- **Id de frase.** Es `base_del_banco + índice_local`. `0x8003B398` (banco), `0x8003B530` (local) y `0x8003ACF0` (sector) traducen el id, con los rangos escritos en el código.
- **Grabaciones de cada jugador.** Índice local = `VoiceId − 0x800`. Hay tres por jugador:

| Banco | Base | Quién | Qué dice |
|---|---|---|---|
| 14 | `0x11000` | principal | apellido, normal |
| 15 | `0x13000` | principal | apellido, emocionado |
| 17 | `0x16000` | de fondo | nombre completo |

- **Lista del editor.** Está en `env_voice_data.pack` (archivo 3081 de `project.bin`). Tiene tres listas (A: 1915 apellidos genéricos; B: 970 jugadores MLB agrupados por letra; C: 61 leyendas) con clave `0x8000 + VoiceId`. La leen las funciones del REL `0x807EDF4C`-`0x807EE354`, que llevan los tamaños de las listas escritos en el código.
- **Restricción.** El banco 14 tiene otras 1523 frases después del último nombre, así que no se puede añadir al final del rango de ids sin pisarlas.
- **Sin localizar.** No se sabe dónde convierte el juego `VoiceId` en id de frase. El diseño no lo necesita (ver el rango de `VoiceId`).

## Decisiones del usuario

- Audio propio con cualquier fuente; tres variantes por nombre (apellido normal, apellido emocionado, nombre completo).
- Muchos nombres, no solo un puñado.
- Se ven en la lista del editor del juego y en PowerUp+.
- PC y Switch.

## Diseño (decisiones de Claude, revisables)

### Rango de `VoiceId` nuevos: 7223-10239

Si la conversión del juego es lineal (`id = base + VoiceId − 0x800`, comprobado con Jeter en el banco 17), un `VoiceId` de este rango cae en ids de frase que **ningún banco usa**:

| Banco | Ids que salen | Hueco |
|---|---|---|
| 14 | `0x12437`-`0x12FFF` | entre el final del banco 14 y el principio del 15 |
| 15 | `0x14437`-`0x14FFF` | entre el final del 15 y el principio del 16 |
| 17 | `0x17437`-`0x17FFF` | después del final del 17 |

- Así las funciones nativas reconocen un nombre nuevo solo por el id, sin ambigüedad con frases existentes, y no hace falta encontrar el código de conversión.
- Caben 3017 nombres nuevos. El campo es de 14 bits (máximo 16383), así que entra.
- Cada nombre nuevo lleva un `VoiceId` fijo que declara el mod (ver formato). Un roster puede apuntar a él y sigue funcionando aunque cambie el orden de los mods.
- Si dos mods activos declaran el mismo `VoiceId`, gana el primero en orden de carga y se avisa en el log.

### Formato del mod

```
mods/<mod>/callnames/callnames.toml
mods/<mod>/callnames/*.wav
```

```toml
[[callname]]
voice = 7300                 # VoiceId, 7223-10239
list = "ACUNA"               # texto de la lista del editor, máximo 15 caracteres
normal = "acuna.wav"         # banco 14: apellido, tono normal (obligatorio)
excited = "acuna_hype.wav"   # banco 15: apellido emocionado (opcional; si falta, se usa normal)
full = "acuna_full.wav"      # banco 17: nombre completo (opcional; si falta, se usa normal)
```

- **WAV admitidos:** PCM de 16 bits, mono o estéreo (se mezcla a mono), a cualquier frecuencia (se remuestrea a 22.050 Hz).
- **Duración:** como máximo 5 s por variante. Lo que no cumpla se rechaza con un aviso en el log, y el resto del mod sigue cargando.
- **Texto:** la tabla de caracteres del juego no tiene tildes ni Ñ. Las letras con tilde se cambian por la letra sin ella ("Acuña" pasa a "Acuna") y cualquier otro carácter que no exista se quita. La conversión sale en el log.

### Runtime (PC y Switch, mismo código)

1. **Al arrancar**, se reúnen los `callnames/` de los mods activos, se codifican los WAV a DSP-ADPCM (codificador propio en C++, determinista) y se guarda el resultado en caché. La caché se vuelve a generar solo si cambian los WAV, el toml o la versión del codificador, como hace `project.bin.sources`.
2. **Audio:** `voice14/15/17.bin` se sirven como **archivo virtual = original + extensión añadida al final**, mapeado por rangos como el `PROJECT.BIN` virtual de Switch (fase 102). No se copian los 170 MB. Cada frase nueva lleva la misma cabecera de `0x800` que las originales: duración en `+0x10` y el byte alto de `+0x04` igual al de las frases de su banco.
   - En PC, la capa DVD hoy solo sirve archivos enteros (`disc/`, y la copia de `project.bin`). Hay que darle el mismo mapeo por rangos, limitado a estos archivos.
3. **Búsqueda de frases** (variante elegida en el plan, 31 de octubre):
   - El banco virtual lleva una **cabecera y un índice ampliados**: el número de frases crece hasta el local del `VoiceId` nuevo más alto, los locales intermedios apuntan a la frase de relleno y los nuevos, a sus sectores en la extensión. El índice ampliado va al final del archivo virtual.
   - Así `0x8003ACF0` y la carga del índice del juego funcionan sin cambios.
   - Solo hay que sustituir por nativos los rangos escritos en el código: `0x8003B398` (banco) y `0x8003B530` (local). Toman el número de frases real de los bancos 14/15/17 y, para cualquier otro id, se comportan **bit a bit igual** que el original.
   - Los sectores tienen que quedar por debajo de `0x10000`: los nombres que no quepan se descartan con un aviso.
   - Si el índice más grande no cabe en el montón del juego, se vuelve a la otra variante: nativo de `0x8003ACF0` y el índice original.
4. **Lista del editor:** `env_voice_data.pack` se regenera con los nombres nuevos metidos por orden alfabético en la lista que muestre el editor (a confirmar, ver pendientes). Se sirve con el mecanismo `project/` existente. Los tamaños escritos en el código de `0x807EDF4C`-`0x807EE354`, y en cualquier otro sitio que los use, pasan a nativos que leen el tamaño real.
5. **Sin mods de callnames**, todo funciona como ahora: ni copias virtuales ni nativos activos que cambien algo.

### PowerUp+

- Con `--PowerPros2008ModsDirectory` (ya lo pasa el launcher), PowerUp+ lee los `callnames.toml` de los mods activos y añade sus nombres al selector de voz con el `VoiceId` declarado.
- El nombre de la lista se escribe en el registro `.dat` con la numeración que ya usa PowerUp+ para voces solo de 2008 (`10000 + id`).
- Un `VoiceId` nuevo tiene que llegar intacto del `.dat` exportado al jugador del juego. Para eso el conversor de rosters del runtime (`Voice2008`/`kVoiceRuns` en `mods.cpp` y `tools/roster/powerup_to_2008.py`) tiene que dejar pasar sin tocar los `VoiceId` de 7223-10239. Hoy convierte en 0 las voces que no conoce.
- Cambio en `tools/patches/powerupplus-2008.patch`.

### Herramientas y documentación

- `tools/callnames.py`: decodificar una frase del juego a WAV (para escuchar en local), y codificar y comprobar un WAV con el mismo algoritmo que el runtime, con un autotest de ida y vuelta.
- Documentación: `docs/mods.md` y su espejo `docs/en/mods.md` (formato del mod); RE nueva en `docs/ingenieria-inversa.md` y su espejo, con el PDF reconstruido; `docs/traspaso.md`.

## Objetivos, no objetivos, anti-objetivos

- **Objetivos:** los tres requisitos de arriba en PC y Switch, en todos los modos, también MLB Life.
- **No objetivos:**
  - generar o clonar voces;
  - cambiar los nombres que dicen otros textos del juego (rótulos, comentarios escritos);
  - un editor gráfico de callnames (se puede añadir más tarde a TOOLS del launcher).
- **Anti-objetivos:**
  - que algún archivo del juego (bancos, pack) acabe dentro de un mod o del repo;
  - que cambie el comportamiento sin mods de callnames;
  - que un nombre original deje de sonar o suene otro;
  - copiar bancos completos al arrancar en Switch.

## Pendiente de averiguar (lo resuelve quien construye)

1. **Qué lista enseña el editor** al crear o editar un jugador (A, B o las dos) y cómo recorre las letras (tabla `+0xFE80`). Se averigua con una prueba corta en el modo Edición, sin jugar partidos, y decide dónde se insertan los nombres nuevos.
2. **Que el juego acepta un `VoiceId` de 7223-10239** en el editor y en el partido (sin recortarlo ni validarlo contra una tabla). Si lo recorta, buscar el sitio y añadirlo a los nativos.
3. **Que la conversión es lineal** también en los bancos 14 y 15. Basta una prueba con `WIICOMPILED_DEBUG_DVD=1` y un jugador con `VoiceId` nuevo.

## Cómo se comprueba

- **Autotest del codificador:** WAV de prueba sintético, codificar y decodificar, con error acotado. Y el mismo resultado byte a byte en Python y en C++.
- **Prueba de partido** con `WIICOMPILED_MATCH_BENCH=1` y un mod de prueba en `local/` que dé a Jeter un `VoiceId` nuevo. Las lecturas DVD del log tienen que ir a la extensión de los bancos 14/15/17. Sin el mod, las lecturas de un partido tienen que ser idénticas a las de ahora.
- **Prueba del editor**, corta con `tools/probe/live.py`: el nombre nuevo aparece en su letra y, al elegirlo, el jugador queda con el `VoiceId` nuevo.
- **Switch:** la misma prueba de partido en la NRO.
- **Escucha final del usuario:** los dos comentaristas dicen el nombre nuevo.
