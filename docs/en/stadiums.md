# Stadiums in Blender

*Versión en español: [../stadiums.md](../stadiums.md)*

The **"MLB Power Pros 2008 Stadiums"** («Estadios de MLB Power Pros 2008») add-on opens any stadium from your game in Blender: every piece in its place and with its textures, plus the field's collision mesh.

It also **exports**: whatever you change in the pieces and in the collision goes back to the game as a mod. And it can bring **a stadium of your own** made in SketchUp, Blender or another program into the game (see "Your own model"). The format is described in `docs/en/reverse-engineering.md`, section 5, "Stadiums".

## Install

1. You need **Blender 4.2 or newer** ([blender.org](https://www.blender.org/download/)).
2. Get the add-on, a zip called `powerpros_stadium-<version>.zip`: the latest released, `powerpros_stadium-0.8.2.zip`, is in [Releases](https://github.com/HopeAero/MLBPowerPros2008Recomp/releases) (v0.1.2); 0.8.3 (Two-sided, Separate stacked layers, Generate field, Add a game piece) is built from the repository for now. To build it from the repository: `python tools/blender/build_addon.py` (it ends up in `local/build/blender/`).
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
  - it comes as **one object per kind** ("Collision · Fences", "Collision · Stands", "Collision · Foul poles"…), with its "Superficie N · name" material in the same colour as in "View in game" (table of kinds in `docs/en/reverse-engineering.md`);
  - in the Power Pros panel, the **Collision** box shows the faces of each kind, with an eye to show or hide it and the **All** and **None** buttons (add-on 0.8.2).
- **Power Pros panel (N):** the selected piece, its original model, the Maya node and the placement flags. In the collision, the kind of the active face.

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
- **Collision:** if you changed the collision (moving an object, editing its faces, changing its kind, deleting an object or adding one), it joins every collision object into the game's one, writes it in full and rebuilds its tree. Shared points are welded.
  - The **kind** of each face comes from the number in its material's name: "Superficie 9 · Fences" (or "Superficie 9", in older files) is a fence. To change a face's kind, assign it another "Superficie N" material.
  - **Your own collision:** select any mesh and press **Make collision** (Collision box of the panel). You pick the kind (fence, stands, home run invisible wall…) and the mesh moves to the collision collection: it is no longer drawn and the ball and players hit it. Use it to close gaps in the generated collision or to make it by hand. Check the result with **C** in "View in game".
  - Each face's normal points to the side the ball hits: up on the ground and towards the field on fences.
- **Size limit:** an 80 MB graphics pack loads in a game and a 96 MB one doesn't. The original weighs 1.1 MB. If you go over 80 MB, the exporter warns you. (On the Wii the cap was about 21 MB: the game reserves a fixed memory heap; the recomp enlarges it.) HD textures don't count: they go separately, outside the pack.
- **Flags:** if you delete the base stadium's flags (`DP_FLG_*`), the exporter shrinks their placement until they are invisible. The game draws them from the table placement, not from their mesh, and without depth test: deleted or buried they still showed.
- **Stadium select menu** (new stadium): with **Menu photo** (on by default) the exporter renders your stadium from behind home plate, with a faded overhead view on the left like the game's photos, and writes `project\env_stadium_menu_<code>.pack`. **Line under the photo** is the green text below it, for example "Home of the Miami Marlins"; empty shows none. Before, the base stadium's photo and line showed.

In an imported stadium, loose objects that didn't come from the game are ignored: run them through **Convert objects to pieces** (**Convertir objetos en piezas**, «Another route» below) first, or make the stadium from scratch («Your own model»).

A new material with an image does get exported: it is created in the piece with that image as a new texture.

## View in game

What looks right in Blender may look different in the game. **View in game** (the **Power Pros** panel of the 3D view, under «Export stadium mod») opens the real game straight in the stadium you are making, without starting a match:

- It exports the stadium to a temp folder as a separate mod («Vista previa», stadium `pp_preview`). **Your mods folder** and your settings **are left alone**.
- The game opens without the launcher and, about 8 seconds later, shows the stadium with the camera **where your Blender view is**. No players or scoreboard: the match stands still, but the stadium's crowd and flags keep moving.
- **Keys:** W/A/S/D to move, Q/E down and up, Shift to go fast, right mouse button + mouse to look, wheel for speed. **1**, **2** and **3** show the match's real cameras (batter, play, wide; the players show in them, standing still); **F** goes back to the free camera, **C** shows the collision and **H** hides the help.
- **Collision (C):** draws the stadium's collision on top, the one the ball and players use, as lines seen through everything, with one colour per surface type: foul and home run invisible walls, outer limit, grass, dirt, mound, fences, stands, roofs, foul poles and backstop. Bottom left, a legend with the triangles of each type and a box to show or hide each (left click), with «All» and «None». The invisible walls (foul, home run and outer limit) start hidden: they are 70 m tall and cover everything. It works with the free camera and the match ones. It shows whether the fences and invisible walls match your model.
- Pressing the button again closes the game it opened before and opens it with your changes.
- It uses its own cache (about 400 MB in `UserData/Cache/preview`), so the normal game's cache is not rebuilt. It loads only the stadium you are making: your other mods are not enabled.
- It needs to know where the game is (the folder with `WiiCompiled.exe`). If you imported the stadium or created the stadium from scratch from the game folder, it already knows; otherwise the first time it opens a chooser for you to pick it (the folder, the `.exe` or the folder above all work) and remembers it in the add-on preferences (**Edit > Preferences > Add-ons > Power Pros > Game folder**), where you can change it (add-on 0.8.3).
- While the game window is active it gets your keys: if you type somewhere else without switching windows, the camera moves.
- **What it is for:** seeing what the game does with your model without playing a match. With the Polo Grounds (18 October 2026) it showed three faults Blender did not: textures lost when converting, grey stands and striped towers (see «Piece and texture limits»). All three are fixed in add-on 0.7.3.

## An OOTP ballpark in four steps

Out of the Park Baseball 3D ballparks (a `.zip` or `.rar` with an `.obj`, `.mtl` and textures) go in with nothing done by hand:

1. **New stadium from scratch**, picking the game folder.
2. **Import OOTP ballpark**, picking the `.zip` or `.rar` as you downloaded it (or its `.obj`). The `.rar` is opened by the `tar` that ships with Windows 10 and 11; elsewhere, extract it and pick the `.obj`.
3. **View in game** to check it.
4. **Export stadium mod** and enable it in the launcher's MODS tab. It shows up in Exhibition, after the game's stadiums.

What the button does:
- Extracts the `.zip` into a folder next to it and keeps its images inside the `.blend`.
- Turns it so centre field is ahead: it finds the field's axis of symmetry.
- Splits the ground (grass, dirt, lines) off with the «Ground» role.
- OOTP seating takes its pictures from OOTP's `attendance` folder, which the zips don't bring. If that folder is next to the park they come from there; otherwise a still picture of the game's people (taken from your `project.bin`) goes over the seat colour. The «The game's crowd on all the seating» box puts it everywhere.
- Generates the collision from what is near the field, within the game's limit.
- Deletes nothing of the model: what you see in Blender is what the game shows.

If a park comes out turned too far, turn it in Blender (R Z and the degrees) before exporting.

## Your own model (SketchUp, glTF, OBJ…)

You can bring a stadium that doesn't exist in the game into it. The add-on writes the stadium **entirely from your model** (since version 0.8.0): no game stadium has to be emptied and filled. Only a few things your model cannot provide are taken from the game (see «What is taken from the game»).

**Bring the model into Blender:**
- **Import it** into the scene: File > Import > glTF, OBJ, FBX…
   - **SketchUp** (`.skp`): convert it to `.glb` first. For example, with [OpenSKP](https://github.com/iamahsanmehmood/openskp) (open source, MIT): `pip install openskp pillow` and then `python -c "from openskp import SkpFile; from openskp.export import glb; s = SkpFile.open('estadio.skp'); s.parse(); glb.export(s, 'estadio.glb', textures=True)"`. It comes out in millimeters: scale it to 0.001.
     - OpenSKP gives faces with SketchUp's default material the layer's colour (`Layer0`, red) when it does not resolve their group's material. If you see red faces that are not red in SketchUp, give them their SketchUp colour in Blender before converting. In the Polo Grounds they were the roofs' green `[Color_G19]`.
     - OpenSKP also writes sRGB colours as if they were linear: in Blender they look lighter than in SketchUp.
   - Delete whatever isn't the stadium, such as aerial photos, distant terrain or people.
- **Place it like the game's stadium:**
   - in meters;
   - with home plate at the origin, the ground at height 0 and second base towards +Y.
   - Check it with your model's bases: from home plate to first and to third it is 27.43 m (90 feet), and to the pitcher's rubber 18.44 m. Many models are not to scale: if your bases are farther away, scale the whole model until they match.
   - This matters because the game doesn't read where the bases or the batter's box are from the stadium: it always puts the players in the same place, and your model has to match it.

**Create the stadium:**
1. **Power Pros** panel → **New stadium from scratch**. Pick the game folder (or `files/project.bin`) and a **data stadium**: the new stadium takes its saves, menu, sky and sounds, but none of its pieces.
2. A collection, «Stadium from scratch», is created that already holds **the game's bases, plate and mound** in their place: fit your model to them.
3. Put your objects (meshes, curves, texts…) in that collection, already placed.
4. With an object selected, the panel shows its **role** and the **Hide when batting and from above**, **Two-sided** and **Separate stacked layers** boxes.
5. Press **Generate collision** and review it (see «Stadium collision»).
6. **Export stadium mod** with the collection selected: always as a new stadium, with its code and name. Try it first with **View in game**.

**Generate field** (add-on 0.8.3): if your model has no field, or you want to start from just a field, press **Generate field** with the from-scratch collection selected. It asks for a few measurements, like OOTP's park generator:
- wall distances in feet: left and right field lines, left-centre, centre and right-centre; the wall runs through those five points on a smooth curve;
- outfield wall height and colour **per sector** (left, left-centre, right-centre and right; each runs from one distance to the next). A Green Monster like Fenway's: left sector 37 feet and green. Up to 50 feet; the collision takes the whole wall as a wall and the home run invisible wall starts on top of it. Each sector also has a **look**: its colour, or a game stadium's fence (the Metrodome's blue padding, say). It takes that fence's main texture from your `project.bin` and lays it the way that stadium does, in its proportions, scaled whole to your wall's height. The game's grass is not copied that way (at the Metrodome the rings round the mound are its ground's geometry, not a repeated texture) but with **Ground**;
- **ground**: generated (from these measurements) or **a game stadium's**: its ground pieces as they are (turf, dirt, track, stains and lines), with their transparency and draw mode, as they look in that stadium. Blender shows them as locked references (its view does not draw their transparent layers right; «View in game» does). With **With that stadium's measurements** (on) the five wall distances become that stadium's, measured on its collision, so its warning track meets your wall. The generator only lays a filler grass under it;
- foul wall height, warning track width, foul ground by first and third, home plate to backstop;
- surface (grass or turf), infield (grass with a dirt skin, all dirt, or turf with dirt cut-outs), mowing pattern (stripes, checker, diagonal, circles) with its width, and grass, dirt and wall colours;
- distance signs on the wall (optional);
- **chalk lines** (optional): foul lines to the poles, batter's, catcher's and coach's boxes, the running lane to first and the on-deck circles, at rule-book sizes. The game's own (`DP_ALP_MIP_line`) are made for each game stadium's distances, so the generator brings its own. They have no collision.

**Reference players** (button under «Generate field»): puts or takes away figures where the pitcher, catcher, the four infielders, three outfielders, umpire, both coaches and the on-deck batter stand, and both dugouts behind the foul wall, to design the stadium around them. It is a usual defence; the game moves the players with each play. They are not exported, have no collision and cannot be selected by accident, except the **dugouts**: move them (and turn or lower them) onto your own dugouts. On export the stadium gets the game's dugout spots (the people on the benches, 6 or 7 a side depending on the base stadium) where your dugouts are, at their floor height; without reference players, where the generator puts the dugouts, at ground level. Every game stadium brings its own; earlier from-scratch stadiums had them empty.

It makes objects with their role already set: ground layers (grass, track, dirt, infield grass, dirt circles, chalk), walls (one per sector), foul poles and signs. Press it again to change the measurements: it remembers the last ones and replaces the previous field (only the objects it made; the rest of the collection is left alone). Generated objects have **Separate stacked layers** off, so the wall and the signs stay where they were put. Then press **Generate collision**: grass and dirt get their surface and the walls come out as walls.

**Add a game piece** (add-on 0.8.3): brings stands, crowd, glass, scoreboards, trees or buildings from any of the game's stadiums, read from your `project.bin`. Pick the stadium and the piece (the list shows each one's role); with **With its whole section** the pieces of the same zone come too (for example `R2`: stand, seated and standing crowd, umbrellas). They arrive where they sit in their stadium with their role already set: move, turn or duplicate them like any object. That way you can join, say, the generated field with one stadium's centre stands and another's right field.
- Ground, bases and night lights are left out (lights only show at night and by day they would be on).
- The crowd on those pieces is the game's animated one, as with the Crowd role. The `AudienceC` crowd kind is drawn as upright crowd.
- The pieces keep their vertex colours (the light the game gave them): the exporter's sun is not applied to them. Their textures are not copied in HD; they are already at the game's size.
- They are game content: they stay in your `.blend` and your mod, don't share them.

**Roles**
| Role | What it does in the game |
|---|---|
| Automatic | Ground if the object is flat at height 0; stand otherwise. |
| Ground | Drawn before everything else (background layer). |
| Stand | Normal piece. |
| Fence | Normal piece; the real wall for the ball is the collision. |
| Lying crowd / Upright crowd | The game draws **its own animated crowd** over it (the people of the data stadium's stands). The object's texture is not used: its UVs decide how many people fit; each repeat is 8 people (4 × 2). Lying is for sloped stands, upright for standing boards. |
| Flags | Each object is a flag the game turns with the wind. Only its size (widest side across and height) and its first texture count; the pole goes at the object's centre. |
| Transparent | Blended with the texture's alpha: glass, shadows, soft edges. |
| Additive glow | Adds its colour to what is behind: lights, reflections. |

- **Hide when batting and from above:** the piece is not seen from the batter's view or from high cameras, like the outside buildings of the game's stadiums. For what is far away and would get in the way.
- **Two-sided** (add-on 0.8.3): the game hides the back of every stadium face and the pack has no way to ask otherwise. With the box on, each face is exported as it is plus a back copy 3 mm behind, so it shows from both sides. Use it for models from games that draw both sides, like MVP Baseball 2005 (without it the stands show holes with the sky through them), and for thin walls or roofs. It doubles the object's triangles. Flat ground is left as it is.
- **Vertex colours** (add-on 0.8.3): if the object has vertex colours, they are multiplied by the material's colour or texture and reach the game. They used to be ignored.
- **Separate stacked layers** (on by default): moves faces lying just in front of a parallel one (windows, signs) up to 25 cm out, because they flicker at a distance. On stepped stands it moves whole rows and opens gaps: turn it off for those objects.
- Objects with the same role (and the same box) are joined into one piece; past 64 textures it is split. Each flag goes alone.
- **The game's bases, plate and mound:** «New stadium from scratch» puts them in the collection (the data stadium's `BCL` pieces, locked), so you can fit your model to them: the players always stand there. They are exported as they are, in their place. If your model brings its own, delete your model's (they would overlap the game's) or delete the game piece you do not want: only the ones still in the collection are exported (add-on 0.8.1).
- **Ground in layers:** the game draws the ground with no depth test, in order. The exporter puts its layers from the bottom up (by the mean height of each material), so the infield dirt, over the grass, shows. Before 0.8.1 the Polo Grounds' dirt was covered by a grass plane 4 cm below it.
- **Collision:** press **Generate collision** (below, «Stadium collision»), or the exporter generates it if there is none.

**Checked in the game** (19 October 2026, a test collection with every role): it shows in the viewer from the batter's view, from above and from the stands, and a 2-minute match plays at 60 FPS with no errors. The crowd animates, the flags turn with the wind, transparent pieces are seen through and the glow adds light.

**The Polo Grounds from scratch** (19 October 2026): the SketchUp model as is (456 objects, 45,000 triangles), placed by its own bases and with AT&T Park as the data stadium, ends up in 5 pieces and 6,953 collision triangles. It shows in the viewer with its textures from every camera, and a 2-minute match plays at 60 FPS with no errors. The only changes to the model were moving the field's flat faces to an object of their own and removing its mound and bases, which overlapped the game's.
- **Tip:** if the field is joined to the stands in one object, separate it (edit mode, select the grass, P > Selection). Then «Automatic» makes it ground and it is drawn as the background layer.
- The model's flags come out as still geometry: for the wind to move them, give them the Flags role. The stands come out empty: the Crowd role puts in the game's animated people.

**Limits:**
- The crowd cannot have its own image: the game always animates its people.
- A flag is always a rectangle: its shape in Blender does not count.
- Up to 127 pieces: past that, the draw order is not guaranteed (the exporter warns).
- What the game does at night (floodlights, stand lights) has not been tested.

**Piece and texture limits** (also for «Another route»):
- The collision can have up to 65,535 triangles.
- Each piece takes up to 64 textures, counting its original ones: the game copies them into a table of 64 slots without checking how many fit. The exporter splits pieces to stay under it and refuses to write a piece with more. With 82 in one piece the game read past that table, the stadium showed no fences and the match ended up closing.
- Big meshes and collision (from the recomp version with the `wiicompiled-stadium-limits` patch): a piece can have hundreds of meshes and the collision up to 65,535 triangles, without the old 65,662-entry tree cap. With an older recomp, a stadium past those numbers shows pieces with missing parts or wrong collisions.
- In the pack, textures become RGB5A3 and powers of 2 up to 256 pixels. With **HD textures**, the game uses the image at its own size (up to 2048 pixels).
- Textures can repeat as much as needed. Each texture in the pack says how many times it repeats horizontally and vertically. With 1, the game does not repeat it: it stretches the edge. The exporter works those numbers out from your faces' UVs, after taking the spare whole repeats off each face; the picture does not change. Up to version 0.7.2 it copied them from the piece's first texture: the Polo Grounds on AT&T Park came out with grey stands and striped towers, and fine on Coors Field, by luck.
- The collision generator assumes a normal baseball field: ground at height 0, the center of the field about 45 m in front of home plate and walls around it.

## Stadium collision

**Generate collision** (Power Pros panel) creates your stadium's collision, one object per kind. It replaces any there was, also the one made with "Make collision".
- the ground (grass if it's green, dirt if not) and the mound;
- the fences: your model's own walls round the field, as they are, with their shape and height (the side walls of bullpens and corners too). A wall is added where the model has none. The invisible walls start at the top edge of the fence;
- the ground counts even if its faces look down;
- the invisible walls above the fences, like the game's: in fair territory (1, the home run) and in foul territory (0);
- the foul poles where the lines cross the fences;
- the stands, simplified to about 5,000 triangles, and an outer boundary behind the outfield.

In a stadium from scratch, flags, transparent and additive pieces do not count; curves and modifiers do. Review it: it is hidden, turn it on with the eye or with **All** in the Collision box. If something doesn't fit, edit it by hand or add pieces with **Make collision**, and check it with **C** in «View in game».

## Another route: inside a game stadium

Before version 0.8.0 this was the only way: the model went **inside one of the game's stadiums** (AT&T Park, Coors Field…), whose pieces were emptied and filled with your geometry. It is still available, but «Your own model» gives better results: with this route the model inherits the base stadium's crowd and flag pieces, the draw order and some unseen settings.

1. **Import a stadium from the game** as a base (it provides its game data: dimensions, menu, saves).
2. Import and place your model as above.
3. Select your model's objects and press **Convert objects to pieces** (Power Pros panel, N key):
   - The field's ground goes into the stadium's ground piece and everything else into a stands piece, because the game only draws the pieces the stadium already has.
   - Each material's color and a soft sun are stored in the vertex color, because the game draws stadiums without lighting.
   - Each face is kept only once and facing the field. Of two faces in the same place (the two sides of a wall, or a texture painted over a plain face, as SketchUp exports them) the textured one is kept and, when neither or both are textured, the one facing the field. The kept face is turned to the field.
   - Surfaces stuck less than 25 cm from another parallel one, such as windows or signs, are separated up to 25 cm, so they don't flicker.
   - With **Remove original stadium** (**Quitar el estadio original**) (on by default), the base stadium's pieces disappear and only your model remains. Pieces with the `pp_keep` property are kept, for example a crowd piece where you've put your own crowd.
   - The game's bases, plate and mound (`BCL` pieces) always stay: the players stand on them, and a model's own rarely match (a downloaded model's plate was 1.2 m).
   - Each piece gets at most 64 textures: the model's are spread over the base stadium's stands.
   - If the model has a texture painted over a plain face, on the same corners (SketchUp does this), the textured face is kept.
   - Converted pieces look in Blender as in the game: texture × vertex colour, unlit. Use the **Material** view (Z > Material Preview).
4. Press **Generate collision** (see «Stadium collision») and review it.
5. **Export** as a new stadium.

## What is taken from the game

A stadium from scratch is written entirely from your model: pieces, textures, materials, collision and the stadium table. From the **data stadium** you pick when creating it, only what the game needs and your model cannot provide is taken:
- **the bases, plate and mound** (`BCL` pieces), because the game always puts the players in the same place;
- **the preloaded textures** of the scoreboard, the video board and the crowd: the game animates its crowd only through them;
- **the menu photo, the saves, the sky and the sounds** of the stadium.

The game has a fixed list of 40 stadiums: a new stadium uses its data stadium's number for saves, menu and game data, so disabling the mod never breaks a save (that game goes back to the data stadium). How each field of the packs was worked out: `docs/en/reverse-engineering.md`, «Writing packs from scratch» and «What the fields do».

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

**Recommendation for modelling it in Blender:**
1. Create the stadium with **New stadium from scratch**: the game's bases, plate and mound give you the scale. To compare with a real game stadium, import it in another scene.
2. Model the stands as smooth ramps (about 18°, like the game's) and apply a small repeating seat texture to them.
3. For the crowd, put faces on the ramps with the **Lying crowd** role (stands) or **Upright crowd** (standing boards): the game draws its animated people there. The game's repeat the texture every 6 m in width and every 3–4 m in height.
4. Walls, roofs and facades: simple boxes with flat colors or small textures; large details, no centimeter-sized pieces. Far-off things with **Hide when batting and from above**.
5. Adjust the fences to the dimensions you want (lines, gaps, center) and put the distance signs 25 cm from the wall.
6. Generate the collision, check the fences with **C** in «View in game» and export.

**Solved since** (phases 2 and 3, 18-19 October 2026; details in `docs/en/reverse-engineering.md`):
- The crowd placed in AT&T's crowd pieces did not show because the game animates its crowd only through the stadium's preloaded textures: the **Crowd** roles use them.
- The placement flags: `0x1600`/`0x1700` are the animated crowd, `0x80000000` (`NOUPCAM`) hides the piece from the batter's view and high cameras, `0x500` are the flags. `0x40000000` ("2F") hides the piece in the play camera (fouls and pop-ups), which goes into the upper deck around home plate: on export, stand, crowd and transparent faces more than 8 m up and less than 60 m from home towards centre field go into a piece with that bit (from scratch) or into the base stadium's 2F piece (converting), so that camera does not end up inside the concrete.

**Not solved yet:**
- What `0x40000000` (upper stands behind home plate) and `0x20000000` (`EVC`) do: removing them changed nothing from the views tested.
- Whether the ball and the home run use the new collision or the base stadium's data: a home run to center at the Polo Grounds landed at 400 feet, which is what its fences measured there.
- What the game does at night (floodlights and stand lights).

## Rules

`.blend` files with imported stadiums contain models and textures from the game. Use them for your mod, but **don't share them**. A stadium mod will distribute only its modified `.pack` files, just like uniforms (`docs/en/mods.md`).

A stadium from scratch also carries something of the game: the data stadium's bases, plate and mound and its preloaded textures (scoreboard, video board and crowd) go inside its pack. It is little, but it is game content: keep it in mind before sharing the mod.

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
- **Stadium from scratch** (`tools/blender/test_scratch.py <zip> <project.bin> <temp folder>`): roles, grouping, draw order, the game's `BCL` pieces (shown and locked in the collection; only the ones left are exported), ground layers bottom-up, flags, crowd through the preload, collision with curves, menu photo, operators and View in game without opening the game.
- **Generate field** (`tools/blender/test_field.py <zip> <project.bin> <temp folder>`): wall distances and poles, ground layers, signs in front of the wall, grass/dirt/wall collision, wall sectors (a 37 ft Green Monster with its collision to the top), chalk lines, reference players left out of the mod, fields saved before the sectors, export and regeneration.
- **Game pieces** (`tools/blender/test_parts.py <zip> <project.bin> <temp folder>`): Yankee Stadium's piece list and roles, a whole section, vertex colours reaching the export, no HD copies, export.
- **OOTP ballpark** (`tools/blender/test_ootp.py <zip> <project.bin> <park .zip/.rar/.obj> <degrees> <temp folder>`): as a player uses it, with a downloaded park (they are not in the repository): turn against the one checked in game (Astrodome 45, Cargill Mill 0, Polo Grounds 45, Montaner 45, Chuckchansi Park 45 from .rar), ground, images kept in the .blend, collision and export.
- **Collision by kind** (`tools/blender/test_collision.py <zip> <project.bin> <temp folder>`): one object per surface, named and coloured, show or hide by kind, Make collision, export joining the objects (and no rewrite when nothing changed).
- **View in game** (`tools/blender/test_preview.py`): the preview export and the game launch, without opening windows.
- **Runtime viewer:** `WIICOMPILED_STADIUM_VIEWER=<mod folder>`, `WIICOMPILED_STADIUM_VIEWER_CODE=<code>`, `WIICOMPILED_STADIUM_VIEWER_CAMERA=x,y,z,yaw,pitch,fov`, `WIICOMPILED_STADIUM_VIEWER_COLLISION=1` (start with the collision shown) `WIICOMPILED_STADIUM_VIEWER_SPIN=<rad/s>` (the free camera turns by itself, to check that the collision lines do not slide) and `WIICOMPILED_STADIUM_VIEWER_TOUR="t,x,y,z,yaw,pitch,fov,collision;..."` (the free camera follows those keys, with the collision on or off at each, and the help is hidden: for recording videos; units as in `..._CAMERA`, decimetres).
- **From-scratch writer** (`python tools/test_stadium_writer.py [project.bin]`): without the `project.bin`, the synthetic tests; with it, also the ones on the game's 40 stadiums (about 45 s). Tools:
  - `python tools/stadium_rebuild.py <project.bin> <code> <mods folder> [--original]`: rebuilds a game stadium with the writer as a new stadium (`rb_<code>`), or copies it untouched (`og_<code>`) to compare it the same way;
  - `python tools/stadium_minimal.py <mods folder>`: the minimal stadium made from scratch (`minimo`).
  The packs they produce are game content (except the minimal one): keep them in `local/`.
