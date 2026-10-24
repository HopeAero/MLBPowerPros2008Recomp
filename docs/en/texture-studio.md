# Power Pros Wii 2008 Texture Studio

*Versión en español: [../texture-studio.md](../texture-studio.md)*

A program for creating **texture mods** for MLB Power Pros 2008. With it you can:

- see all of the game's textures, sorted by what they are;
- export them to PNG to edit them in another program (Photoshop, GIMP, Krita) or upscale them with AI right in the Studio;
- import texture packs made for Dolphin;
- put them back in, at the same size or bigger (HD).

The game is never modified: everything goes into a mod that you turn on or off in the launcher.

## Opening it

- From the game: launcher → SETTINGS (AJUSTES) → **TOOLS** (**HERRAMIENTAS**) → **TEXTURE EDITOR** (**EDITOR DE TEXTURAS**) → Open.
- Or with the **Power Pros Wii 2008 Texture Studio** desktop shortcut, if you created it during installation.

The first time it takes a second to read the textures from your copy of the game (about 9,700 different ones).

## The window

- **Top:** the mod you are working on, whether it is enabled in the game (**Enable** (**Activar**) button) and **+ New mod** (**+ Nuevo mod**).
- **Left:** categories.
  - Menus, Stadiums (by team, plus skies and original stadiums), Uniforms, Player cards, Logos, Faces, MLB Life, Season, Success…
  - At the end, the **captures** you've made.
- **Center:**
  - a search box by name, pack or hash;
  - the **Replaced only** (**Solo reemplazadas**) and **Seen in capture** (**Vistas en captura**) filters;
  - the thumbnails, with a green ✓ on the ones you've already replaced and an orange eye on the ones you saw while capturing. Double-click exports a texture.
- **Details panel:** the original texture next to your replacement.
  - Click either of them to see it full size.
  - Below you'll see the pack it comes from, its format, the file name and the **Export PNG** (**Exportar PNG**), **Import PNG...** (**Importar PNG...**), **Remove replacement** (**Quitar reemplazo**) and **Open folder** (**Abrir carpeta**) buttons.
- **Bottom:** **Export this view** (**Exportar esta vista**), **Import folder...** (**Importar carpeta...**), **Capture in game** (**Capturar en juego**) and **Package .zip** (**Empaquetar .zip**).

**Language:** the **ES | EN** pill in the top band switches the language right away and saves it in `Config.toml` (`[ui] language`), the same setting as the launcher. Until you choose, it follows Windows' language.

## Making an HD pack step by step

1. **+ New mod**: give it a name (it will be the `mods\<name>` folder).
2. Choose a category (for example, Menus › Main menu) and press **Export this view**. Choose a folder: subfolders with the original PNGs are created.
3. Edit those PNGs or run them through chaiNNer or Upscayl. **Don't change the file names**: the `tex1_…` name is what the game looks for.
4. **Import folder...** and choose the folder with your PNGs. It searches all subfolders and shows you a report: what was imported and what wasn't, and why.
5. **Enable** (top) and open the game.

For a single texture: select it, **Export PNG**, edit it and **Import PNG...**.

## Sizes

- You can make the texture bigger, but with the **same aspect ratio** (a 256×128 one can be 512×256, 1024×512…).
- Ideally, multiply by 2, 4 or 8. Another size with the same aspect ratio is accepted with a warning.
- Maximum: 4096 pixels per side.
- PNGs of any kind (paletted, grayscale, 16-bit) and the DDS files the game dumps are accepted. From Photoshop, save as PNG.

## Palettes

Many of the game's textures use a palette (16 or 256 colors). You don't have to worry about it: the replacement is a full-color image.

- About 300 textures have their palette in another pack. The Studio shows them **in gray**, but they can be exported and replaced all the same.
- If the game draws the same texture with several palettes, each variant is a separate replacement. **Apply to all palettes** (**Aplicar a todas las paletas**) (in the details panel) makes your image work for all of them.

## Upscaling with AI

**Upscale with AI** (at the bottom) exports the textures, upscales them on your graphics card and imports the result into the active mod, all at once.

1. Choose a category (or select a texture) and press **Upscale with AI**.
2. Pick the model, the size (**x2** or **x4**) and whether you want **this view** or **only the selected one**.
3. Press **Upscale**. When it finishes the import report shows up; enable the mod and see it in the game.

**Models:**

- **AnimeSharp** (recommended): menus, logos, lettering and characters; keeps the shading and the drawing style.
- **Real-ESRGAN anime**: sharper, with strong outlines (more "cartoon").
- **Real-ESRGAN x4plus**: for photos, like the stadiums and the player cards.
- **Your own models**: any NCNN model (`<name>.param` + `<name>.bin`) you put in `UserData\Tools\upscale\models` shows up in the list.

**The first time** it downloads, free and from their authors, the **Real-ESRGAN ncnn-vulkan** engine (about 45 MB, MIT license, from xinntao's GitHub) and, if you pick AnimeSharp, **4x-AnimeSharp by Kim2091** (about 33 MB, CC BY-NC-SA 4.0 license: non-commercial use, from its Hugging Face page). They go to `UserData\Tools\upscale` and don't come with the game. It works on AMD, NVIDIA and Intel cards (Vulkan).

**Tips:**

- Grey textures (their palette is in another pack) are skipped: they would look grey in the game.
- At x4, a texture that would go past 4096 px is upscaled x2.
- Do it by category: a x4 makes 16 times the pixels, and the whole game at x4 is several GB. For reference, on an RX 5500 it is about 4 seconds per 512×512 texture with AnimeSharp.
- Repeating textures (grass, stands, walls) can show seams: check them in the game and, if they are noticeable, leave them as the original or at x2.
- You can also upscale outside the Studio (chaiNNer, Upscayl): export, upscale **without changing the file names** and use **Import folder...**.

## Dolphin texture packs

Texture names are the same as Dolphin's (checked with Dolphin 2609: the 695 names of the textures seen in both are identical), so **a pack made for Dolphin for MLB Power Pros 2008 (RL8E54) works as it is**.

1. Create or choose a mod at the top.
2. **Dolphin pack...** and pick the pack's **folder** (the one with the `tex1_…` files, usually `Load\Textures\RL8E54`) or the **.zip** itself.
3. The files are copied into the mod without renaming, with their subfolders, mipmaps (`_mip1`…) and DDS (BC1, BC3, BC5, BC7). The report says which ones are textures of the game; the ones not in the index are copied anyway (they may be textures made while playing).
4. **Enable** and start the game.

The other way round should work too (not tested): a mod made with the Studio, in Dolphin, by copying the mod's `textures` folder to `Load\Textures\RL8E54`.

## Capture in game

It's for finding the texture of something you see on screen.

1. Press **Capture in game**: the game opens without the launcher.
2. Go to the screen you're interested in and close the game.
3. The Studio creates **Captures › Session N** (**Capturas › Sesión N**) with the textures that appeared and marks them with the eye.

Some textures only exist while playing (they aren't in any pack). They show up in **Seen in game only** (**Solo vistas en juego**) and can also be replaced. Videos can't be replaced.

## Sharing your mod

**Package .zip** creates a zip with your `mod.toml` and your images, nothing else. Whoever receives it unzips it into their `mods` folder and enables it in the launcher (MODS).

Don't share unmodified original textures: they are game content. Share only what you made.

## For advanced users

`TextureStudio.exe` also works from the command line:

```
TextureStudio.exe --new-mod <mods folder> <name>
TextureStudio.exe --export <project.bin> "<group>[/<subgroup>]" <folder>
TextureStudio.exe --import-folder <project.bin> <mod folder> <PNG folder>
TextureStudio.exe --import-dolphin <project.bin> <mod folder> <pack folder>
TextureStudio.exe --upscale <tools folder> <model> <2|4> <PNG folder>   (writes <folder>-up)
TextureStudio.exe --selftest
```

The window can be opened on a specific view with `--view "Estadios/Yankees (NYY)"`, `--search <text>`, `--select <tex1_ name>` and `--mod <name>`.

To put textures inside the `.pack` files instead of using a texture mod, there is `tools/packwii.py` (see `docs/en/mods.md`).
