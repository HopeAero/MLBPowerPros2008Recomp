# Stadiums in Blender

*Versión en español: [../stadiums.md](../stadiums.md)*

The **"MLB Power Pros 2008 Stadiums"** («Estadios de MLB Power Pros 2008») add-on opens any stadium from your game in Blender: every piece in its place and with its textures, plus the field's collision mesh.

It also **exports**: whatever you change in the pieces and in the collision goes back to the game as a mod. And it can bring **a stadium of your own** made in SketchUp, Blender or another program into the game (see "Your own model"). The format is described in `docs/en/reverse-engineering.md`, section 5, "Stadiums".

## Install

1. You need **Blender 4.2 or newer** ([blender.org](https://www.blender.org/download/)).
2. Get the add-on, a zip called `powerpros_stadium-<version>.zip`. To build it from the repository: `python tools/blender/build_addon.py` (it ends up in `local/build/blender/`).
3. In Blender: **Edit > Preferences > Get Extensions**. In the ⌄ menu at the top right choose **Install from Disk…** and select the zip.

**Language:** in the add-on's preferences (**Edit > Preferences > Add-ons**), **Idioma / Language**: Auto (Blender's interface language), Español or English. It applies without restarting Blender.

## Import a stadium

1. **File > Import > Power Pros 2008 Stadium** (**Archivo > Importar > Estadio de Power Pros 2008**). It is also in the 3D view's sidebar (N key), **Power Pros** tab.
2. Choose the **game folder** installed with the Setup (the one that has `UserData`) or the `files/project.bin` file directly.
3. In the panel on the right choose the **stadium**, leave **Include collision** (**Incluir colisión**) checked if you want it and press **Import stadium** (**Importar estadio**).

It takes a few seconds. Nothing is extracted to disk: everything is read from your game's `project.bin`.

## What you get

- **One collection per stadium** (for example "Colorado Rockies — Coors Field") with **one object per piece**, with the name the game gave them:
  - `DP_ground`: dirt;
  - `DP_MIP_turf`: grass;
  - `DP_stand_*`: stands;
  - `DP_fence_*`: fences;
  - `DP_score`: scoreboard;
  - `DP_auroravision`: video screen;
  - `DP_Audience*`: crowd;
  - `DP_FLG_*`: flags.
- **Real scale:** 1 Blender unit = 1 meter. Z up, home plate at the origin and center field towards +Y.
- **Materials as in the game:** texture × vertex color, unlit. Pieces with transparency (`*_ALP_*`) are blended and `*_ADD_*` ones add light. To see them like that use the **Material** view (Z > Material Preview).
- **Textures** packed inside the `.blend`.
- **Collision**, in the "… · Collision" («… · Colisión») subcollection, hidden on import (turn it on with the eye):
  - it is the mesh the game uses to decide where the ball bounces;
  - each face has a `surface_id` and a color by type (grass, dirt, mound, fences, stands, foul poles…; see the table in `docs/en/reverse-engineering.md`).
- **Power Pros panel (N):** the selected piece, its original model, the Maya node and the placement flags. In the collision, the surface of the active face.

The `pp_*` custom properties on objects and materials store the original data the export will need. Don't delete them if you want to put the stadium back into the game.

## Export to the game

1. Edit whatever you want in the imported pieces:
   - move, rotate or scale pieces;
   - edit their mesh (including adding or removing faces within a piece);
   - paint or change their images;
   - assign them other materials from the same stadium;
   - delete pieces.
2. **File > Export > Power Pros 2008 Stadium Mod** (**Archivo > Exportar > Mod de estadio de Power Pros 2008**), or the **Export stadium mod** (**Exportar mod de estadio**) button in the Power Pros panel.
3. Fill in the dialog:
   - **Mod name** (**Nombre del mod**);
   - **Mods folder** (**Carpeta de mods**): if you imported from the game folder, it is already filled in (`<game>\mods`);
   - **Type** (**Tipo**):
     - **New stadium** (**Estadio nuevo**) (default): it is **added** to the Exhibition stadium selection and doesn't remove any. It asks for a short **code** (lowercase letters, numbers or _, for example `truist`) and a **name**. It uses the game data of the imported stadium: dimensions, menu and saves;
     - **Replace a stadium** (**Reemplazar un estadio**): replaces the same stadium or another one. For example, you can put your edited Coors Field in Turner Field's slot.
4. Enable the mod in the launcher's **MODS**. A new stadium appears in Exhibition when choosing a stadium, after the game's own (pressing left from the first one gets you there faster).

What exporting does:
- It writes `mods\<name>\project\stadium_<code>.pack` and `stadium_<code>tbl.pack`, plus a `mod.toml`.
- **It only rewrites the pieces you changed**; the rest stays identical to the original. New things are added at the end of the pack.
- A deleted piece is left without geometry.
- An edited or new image is saved in the pack as an RGB5A3 texture. If its sides are not powers of 2, it is rescaled to the nearest one, from 4 to 256 pixels, like the game's.
- **HD textures** (on by default in the dialog): the image is also saved at its own size, up to 2048 pixels and 8 bits per channel, in `mods\<name>\textures\stadium_<code>\`. The game uses it instead of the pack's and builds its mipmaps, so a large image looks sharp up close and clean from afar. The pack's 256 px one remains for when there are no replacement textures.
- If you move a piece, it updates its box and sphere in the table, so the game doesn't hide it when it comes into the camera's view, and rebuilds the tree the game uses to sort the pieces.
- **Collision:** if you changed the "Collision" mesh (moving it, editing its faces or changing its surface), it writes it in full and rebuilds the game's collision tree.
  - The **surface** of each face comes from the final number in its material's name: "Surface 9" («Superficie 9») is a fence. To change a face's type, assign it another "Surface N" material.
  - Each face's normal points to the side the ball hits: up on the ground and towards the field on fences.
- **Size limit:** an 80 MB graphics pack loads in a game and a 96 MB one doesn't. The original weighs 1.1 MB. If you go over 80 MB, the exporter warns you. (On the Wii the cap was about 21 MB: the game reserves a fixed memory heap; the recomp enlarges it.) HD textures don't count: they go separately, outside the pack.
- **Flags:** if you delete the base stadium's flags (`DP_FLG_*`), the exporter shrinks their placement until they are invisible. The game draws them from the table placement, not from their mesh, and without depth test: deleted or buried they still showed.
- **Stadium select menu** (new stadium): with **Menu photo** (on by default) the exporter renders your stadium from behind home plate, with a faded overhead view on the left like the game's photos, and writes `project\env_stadium_menu_<code>.pack`. **Line under the photo** is the green text below it, for example "Home of the Miami Marlins"; empty shows none. Before, the base stadium's photo and line showed.

Not yet: loose objects that didn't come from the game. The exporter ignores them: run them through **Convert objects to pieces** (**Convertir objetos en piezas**) first (below).

A new material with an image does get exported: it is created in the piece with that image as a new texture.

## View in game

What looks right in Blender may look different in the game. **View in game** (the **Power Pros** panel of the 3D view, under «Export stadium mod») opens the real game straight in the stadium you are making, without starting a match:

- It exports the stadium to a temp folder as a separate mod («Vista previa», stadium `pp_preview`). **Your mods folder** and your settings **are left alone**.
- The game opens without the launcher and, about 8 seconds later, shows the stadium with the camera **where your Blender view is**. No players or scoreboard: the match stands still, but the stadium's crowd and flags keep moving.
- **Keys:** W/A/S/D to move, Q/E down and up, Shift to go fast, right mouse button + mouse to look, wheel for speed. **1**, **2** and **3** show the match's real cameras (batter, play, wide; the players show in them, standing still); **F** goes back to the free camera and **H** hides the help.
- Pressing the button again closes the game it opened before and opens it with your changes.
- It uses its own cache (about 400 MB in `UserData/Cache/preview`), so the normal game's cache is not rebuilt. It loads only the stadium you are making: your other mods are not enabled.
- The stadium must have been imported from the installed game folder (the one with `WiiCompiled.exe`); otherwise the button says so.

## Your own model (SketchUp, glTF, OBJ…)

You can bring a stadium that doesn't exist in the game into it.

1. **Import a stadium from the game** as a base. Pick the one you want to provide its game data (dimensions, menu, saves); for example, AT&T Park for a Giants stadium.
2. **Import your model** into the same scene: File > Import > glTF, OBJ, FBX…
   - **SketchUp** (`.skp`): convert it to `.glb` first. For example, with [OpenSKP](https://github.com/iamahsanmehmood/openskp) (open source, MIT): `pip install openskp pillow` and then `python -c "from openskp import SkpFile; from openskp.export import glb; s = SkpFile.open('estadio.skp'); s.parse(); glb.export(s, 'estadio.glb', textures=True)"`. It comes out in millimeters: scale it to 0.001.
   - Delete whatever isn't the stadium, such as aerial photos, distant terrain or people.
3. **Place it like the game's stadium:**
   - in meters;
   - with home plate at the origin, the ground at height 0 and second base towards +Y.
   - Check it with your model's bases: from home plate to first and to third it is 27.43 m (90 feet), and to the pitcher's rubber 18.44 m. Many models are not to scale: if your bases are farther away, scale the whole model until they match.
   - This matters because the game doesn't read where the bases or the batter's box are from the stadium: it always puts the players in the same place, and your model has to match it.
4. Select your model's objects and press **Convert objects to pieces** (Power Pros panel, N key):
   - The field's ground goes into the stadium's ground piece and everything else into a stands piece, because the game only draws the pieces the stadium already has.
   - Each material's color and a soft sun are stored in the vertex color, because the game draws stadiums without lighting.
   - Each face is kept only once and facing the field. Of two faces in the same place (the two sides of a wall, or a texture painted over a plain face, as SketchUp exports them) the textured one is kept and, when neither or both are textured, the one facing the field. The kept face is turned to the field.
   - Surfaces stuck less than 25 cm from another parallel one, such as windows or signs, are separated up to 25 cm, so they don't flicker.
   - With **Remove original stadium** (**Quitar el estadio original**) (on by default), the base stadium's pieces disappear and only your model remains. Pieces with the `pp_keep` property are kept, for example a crowd piece where you've put your own crowd.
   - The game's bases, plate and mound (`BCL` pieces) always stay: the players stand on them, and a model's own rarely match (Mario Stadium's plate was 1.2 m).
   - Each piece gets at most 30 textures: the model's are spread over the base stadium's stands.
5. Press **Generate collision** (**Generar colisión**). It creates your stadium's "Collision" mesh:
   - the ground (grass if it's green, dirt if not) and the mound;
   - the fences, following the wall closest to the field in all directions;
   - the invisible walls above the fences, like the game's: in fair territory (1, the home run) and in foul territory (0);
   - the foul poles where the lines cross the fences;
   - the stands, simplified to about 5,000 triangles, and an outer boundary behind the outfield.

   Review it: it is hidden, turn it on with the eye. If something doesn't fit, you can edit it by hand.
6. **Export** as a new stadium.

Limits:
- The collision can have up to 65,535 triangles.
- Each piece takes up to 30 textures, counting its original ones; that is the most a piece has in the game's stadiums. «Convert objects into pieces» spreads the model's textures over the base stadium's stands to stay under it, and the exporter refuses to write a piece with more. With 82 in one piece the game read past a table, the stadium showed no fences and the match ended up closing.
- In the pack, textures become RGB5A3 and powers of 2 up to 256 pixels. With **HD textures**, the game uses the image at its own size (up to 2048 pixels).
- The collision generator assumes a normal baseball field: ground at height 0, the center of the field about 45 m in front of home plate and walls around it.

## How to make a model that works well in the game

What we learned bringing in the Polo Grounds, a downloaded SketchUp model (October 14 and 15, 2026). That model, made to be viewed in SketchUp, caused a lot of problems in the game; one designed for the game from the start avoids almost all of them.

**What the game needs (verified):**
- **The field in its place:** home plate at the origin, bases at 90 feet, pitcher's rubber at 60 feet 6 inches and the mound centered at 18.51 m. The game always puts the players there; your model doesn't change that.
- **Real scale in meters.** The fences stay wherever they are in your model: if the model is at a different scale, the distances change.
- **Colors in the vertex color or in textures:** the game doesn't light stadiums. The color of a material without a texture only gets through if the converter stores it in the vertices.
- **No stuck-together surfaces:** two parallel faces less than a few centimeters apart (a window on a wall, a sign, a seat texture over the stands) flicker at 100–150 m. Two faces in the same plane always flicker; ones a few centimeters apart should not in the recomp, which uses 32-bit depth, but it was seen with the Polo Grounds and has not been isolated. Leave at least 25 cm or make the detail with a texture.
- **One face per surface:** no double faces in the same place. In SketchUp, paint only the front face.
- **Textures:** with **HD textures** you can use images up to 2048 pixels: the game builds their mipmaps and they don't look noisy at a distance. Without HD textures, stay at 32 to 256 pixels, like the game's.
- **No thin geometry:** rows of chairs, railings or flagpoles a few cm thick are less than a pixel at a distance and come out broken. Seats are a ramp with a seat texture, and flagpoles should be at least about 30 cm.
- **Few triangles:** a game stadium has about 10,000. One with 40,000 works on PC, but it costs more to draw and to look clean.

**Recommendation for making one from scratch (in Blender):**
1. Import the game's base stadium. Its field, its dimensions and its pieces are your scale and style template.
2. Model the stands as smooth ramps (about 18°, like the game's) and apply a small repeating seat texture to them.
3. For the crowd, place faces on the ramps and inside the base stadium's crowd pieces (`DP_AudienceF_*`, lying on the stands, and `DP_AudienceS_*`, vertical boards), with the same materials. The game's repeat the texture every 6 m in width and every 3–4 m in height.
4. Walls, roofs and facades: simple boxes with flat colors or small textures; large details, no centimeter-sized pieces.
5. Adjust the fences to the dimensions you want (lines, gaps, center) and put the distance signs 25 cm from the wall.
6. Convert, generate the collision, check the fences and export as a new stadium.

**Not solved yet:**
- The crowd placed in AT&T's crowd pieces was never seen in a game (it was in Blender). It remains to find out when the game draws those pieces.
- The effect of the placement flags (`0x40000000` on the upper stands behind home plate, `0x80000000`/`0x20000000` on the `NOUPCAM`/`EVC` pieces, `0x1600`/`0x1700` on the crowd). The converter puts nothing on pieces with the first three.
- Whether the ball and the home run use the new collision or the base stadium's data: a home run to center at the Polo Grounds landed at 400 feet, which is what its fences measured there.

## Rules

`.blend` files with imported stadiums contain models and textures from the game. Use them for your mod, but **don't share them**. A stadium mod will distribute only its modified `.pack` files, just like uniforms (`docs/en/mods.md`).

## For developers

- `python tools/blender/powerpros_stadium/selftest.py`: self-test of the readers, without game data.
- `python tools/stadium_re.py check <project.bin>`: checks the 47 stadium packs. `info` and `stats` give summaries and histograms of what is still left to decode.
- **Full test inside Blender** (portable Blender 4.2 LTS in `local/tools/`):

  ```
  set BLENDER_USER_RESOURCES=<temp folder>
  blender --background --factory-startup --python tools/blender/test_import.py -- <zip> <project.bin> col <output folder>
  ```

  It installs the zip, imports Coors Field, compares pieces, faces, textures and collision with what the packs say, and renders a top-down view and another from home plate.
- **Round-trip export** (`tools/blender/test_export.py`, same arguments with the mods folder at the end):
  1. imports Coors Field and makes four changes to it: raises `DP_stand_C` 15 m, raises the geometry of `DP_score` 10 m, deletes a flag and tints the image of `DP_ground`;
  2. exports the `BlenderTest` mod;
  3. rereads the packs and checks each change, including that the tinted image comes out as an HD texture under the name the game looks for.

  The resulting mod, loaded in a game, shows the four changes.
  4. imports Coors Field again with collision, raises its fences (surface 9) 3 m, changes a face to foul pole and checks the written table: surfaces, face orientation and both trees.
- **Own model** (`tools/blender/test_convert.py`, same arguments as the export): builds a test stadium with simple shapes (grass, dirt, a circular fence and stands), converts it into Coors Field pieces, generates its collision, exports it as a new stadium and checks what was written.
