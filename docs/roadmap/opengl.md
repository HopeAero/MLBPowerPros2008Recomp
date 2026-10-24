# OpenGL

*English: [../en/roadmap/opengl.md](../en/roadmap/opengl.md)*

**Estado:** en curso (OpenGL ES en Linux, Direct3D 11 en Windows) · [Roadmap](../../ROADMAP.es.md)

## Por qué
El juego dibuja con Dawn (WebGPU), que hoy usa Direct3D 12 o Vulkan. Gráficas antiguas y algunos controladores no tienen ninguno de los dos funcionando bien. OpenGL / OpenGL ES es además el camino más directo a Switch, donde Dawn no tiene backend propio.

## Dónde estamos
- **Linux: el juego dibuja y juega un partido en OpenGL ES** (parche `wiicompiled-opengles`). El paquete precompilado de Dawn para Linux ya trae su backend GL/EGL; solo estaba apagado en aurora.
- **Windows: Direct3D 11 dibuja y juega un partido** (parche `wiicompiled-d3d11`). El paquete de Dawn para Windows viene sin GL, pero trae el backend Direct3D 11, que cubre las gráficas sin Direct3D 12 ni Vulkan (por ejemplo Intel HD 2000/3000). En la selección automática va después de Direct3D 12 y Vulkan.
  - Dawn compila los shaders de D3D11 con FXC y solo encuentra `d3dcompiler_47.dll` junto a `webgpu_dawn.dll` (por nombre falla con el error 87 de Windows). Todo Windows 10/11 la trae en System32; el build copia la del propio equipo junto al juego.
  - Se pide en el nivel *Compatibility*, como GL, para que sirva también en gráficas de nivel 11_0.
- Qué hizo falta en aurora:
  - Dawn solo ofrece adaptadores GL en el nivel de funciones *Compatibility*.
  - Un contexto GL solo puede estar activo en un hilo a la vez (`eglMakeCurrent` falla con `EGL_BAD_ACCESS`), así que con GL la preparación y presentación de cuadros se queda en el hilo que llama, sin los hilos auxiliares.
  - El swapchain EGL de Dawn no admite superficies Wayland: con OpenGL ES la ventana usa X11 / XWayland.
  - aurora llamaba `SDL_SyncWindow` en cada cuadro. En servidores X11 que no confirman la posición pedida por la ventana (XWayland en WSLg) cada cuadro esperaba el plazo entero. Ahora solo sincroniza después de que aurora cambia la ventana. Afecta a cualquier backend en X11, no solo a GL.
- `AURORA_DUMP_PRESENT=<cuadro>:<archivo.ppm>` guarda el cuadro presentado leído de la GPU, antes de la ventana: distingue una ventana negra de un render negro.

## Mediciones (WSL, no representativas)
WSL en Ryzen 5 5500 + RX 5500, partido de prueba Yankees–Colorado a x1. **No es rendimiento real**: en WSL Vulkan no tiene controlador de GPU y GL pasa por una traducción a Direct3D 12 y por el escritorio remoto de WSLg. Sirven para comparar los backends entre sí.

| Backend | Dónde dibuja | FPS | p50 / p95 |
|---|---|---|---|
| OpenGL ES | GPU (Mesa d3d12) | 40,7 | 23,8 / 29,9 ms |
| OpenGL ES | CPU (llvmpipe) | 16,7 | 57,9 / 65,8 ms |
| Vulkan | CPU (lavapipe) | 15,0 | 29,1 / 169,5 ms |

En WSLg la ventana de cualquier programa GL sobre X11 sale negra (también `es2gears` de Mesa: `Failed to attach to x11 shm`). El cuadro leído con `AURORA_DUMP_PRESENT` muestra el partido completo.

## Mediciones (Windows nativo)
Ryzen 5 5500 + RX 5500, mismo partido de prueba, 20 s a x4 (resolución interna 4x).

| Backend | FPS | p99 | GPU 3D | CPU del juego | VRAM |
|---|---|---|---|---|---|
| Direct3D 12 | 59,9 | 17,3 ms | 69,7 % | 4,7 % | 389 MB |
| Direct3D 11 | 60,0 | 17,3 ms | 71,0 % | 4,9 % | 329 MB |

Direct3D 11 rinde igual que Direct3D 12 y usa un 15 % menos de VRAM. Imagen comprobada con `AURORA_DUMP_PRESENT`: partido y launcher completos.

## Plan
1. ~~Activar el backend OpenGL ES en Linux y jugar un partido.~~
2. Medir en Linux nativo (Steam Deck o PC) contra Vulkan, con la misma prueba.
3. Revisar menús, MLB Life y cada estadio en OpenGL ES.
4. ~~Windows: Direct3D 11 para gráficas sin Direct3D 12 ni Vulkan.~~ Falta probarlo en una gráfica que solo tenga Direct3D 11.
5. Incluir OpenGL ES en los kits de Linux y Steam Deck.

## Cómo se verifica
La misma prueba de rendimiento y las mismas comprobaciones visuales (menús, un partido, cada estadio) en OpenGL que en Vulkan.

## Casillas
- [x] Dawn con OpenGL ES (Linux: viene en el paquete precompilado)
- [x] El juego arranca y juega un partido en OpenGL ES (Linux, WSL)
- [x] OpenGL ES elegible en el launcher (Linux)
- [ ] Medido en Linux nativo
- [x] Direct3D 11 en Windows (juega un partido, elegible en el launcher)
- [ ] Direct3D 11 probado en una gráfica sin Direct3D 12
