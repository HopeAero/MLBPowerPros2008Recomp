# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender menu entries, operators, side panel and preferences, in Spanish or English (i18n.py).

Blender reads labels, descriptions and properties when a class is registered, so they are set by
apply_language() before registering, and the classes are registered again when the language changes.
"""
from pathlib import Path

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

from . import collision_gen, convert, exporter, field_gen, game_parts, importer, names, ootp_park, preview, scratch, surfaces
from .i18n import CONTEXT, T


def set_props(cls, **props):
    """Replace the given properties of an operator class (it must be unregistered)."""
    cls.__annotations__ = {**cls.__dict__.get("__annotations__", {}), **props}


class PP_OT_import_stadium(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.powerpros_stadium"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    filename_ext = ""
    filter_glob: StringProperty(default="*.bin;*.exe;*.toml", options={"HIDDEN"})

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Importar estadio", "Import stadium")
        cls.bl_description = T("Importa un estadio de MLB Power Pros 2008 desde tu juego instalado",
                               "Import a stadium of MLB Power Pros 2008 from your installed game")
        set_props(cls,
                  stadium=EnumProperty(name=T("Estadio", "Stadium"), items=[(c, n, "") for c, n in names.stadiums()],
                                       default="col", translation_context=CONTEXT),
                  collision=BoolProperty(name=T("Incluir colisión", "Include collision"), default=True,
                                         description=T("Malla de colisión del campo, oculta, coloreada por tipo de superficie",
                                                       "The field's collision mesh, hidden, coloured by surface type"),
                                         translation_context=CONTEXT))

    def draw(self, context):
        layout = self.layout
        layout.label(text=T("Elige la carpeta del juego o files/project.bin", "Pick the game folder or files/project.bin"),
                     translate=False)
        layout.prop(self, "stadium")
        layout.prop(self, "collision")

    def execute(self, context):
        try:
            coll, objects, images = importer.import_stadium(context, self.filepath, self.stadium, self.collision)
            if preview.game_folder_of(coll.get("pp_game_folder")) is not None:
                preview.remember_game_folder(coll, coll["pp_game_folder"])
        except importer.ImportFailure as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, T(f"{coll.name}: {len(objects)} piezas, {len(images)} texturas",
                                f"{coll.name}: {len(objects)} pieces, {len(images)} textures"))
        return {"FINISHED"}


def stadium_collection(context):
    """The imported stadium the active object belongs to, or the only one in the scene."""
    obj = context.active_object
    found = [c for c in bpy.data.collections if "pp_stadium" in c]
    if obj is not None:
        for c in found:
            if obj.name in c.all_objects:
                return c
    return found[0] if len(found) == 1 else None


class PP_OT_export_stadium(bpy.types.Operator):
    bl_idname = "export_scene.powerpros_stadium"
    bl_options = {"REGISTER"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Exportar mod de estadio", "Export stadium mod")
        cls.bl_description = T("Escribe el estadio editado como mod: mods/<nombre>/project/stadium_<código>.pack y su tabla",
                               "Write the edited stadium as a mod: mods/<name>/project/stadium_<code>.pack and its table")
        set_props(cls,
                  mod_name=StringProperty(name=T("Nombre del mod", "Mod name"), default=T("Mi estadio", "My stadium"),
                                          translation_context=CONTEXT),
                  mods_folder=StringProperty(name=T("Carpeta de mods", "Mods folder"), subtype="DIR_PATH",
                                             description=T("La carpeta mods junto al juego instalado",
                                                           "The mods folder next to the installed game"),
                                             translation_context=CONTEXT),
                  mode=EnumProperty(name=T("Tipo", "Type"), translation_context=CONTEXT, items=[
                      ("new", T("Estadio nuevo", "New stadium"),
                       T("Se añade como una entrada más en la elección de estadio; no quita ninguno",
                         "Added as one more entry of the stadium select; removes none")),
                      ("replace", T("Reemplazar un estadio", "Replace a stadium"),
                       T("Sustituye a un estadio del juego", "Takes the place of a stadium of the game"))]),
                  new_code=StringProperty(name=T("Código", "Code"), default=T("miestadio", "mystadium"),
                                          description=T("Identificador corto: letras minúsculas, números o _ (p. ej. truist)",
                                                        "Short identifier: lowercase letters, digits or _ (e.g. truist)"),
                                          translation_context=CONTEXT),
                  new_name=StringProperty(name=T("Nombre", "Name"), default=T("Mi estadio", "My stadium"),
                                          translation_context=CONTEXT),
                  target=EnumProperty(name=T("Reemplaza a", "Replaces"), translation_context=CONTEXT,
                                      items=[("same", T("El mismo estadio", "The same stadium"), "")] +
                                      [(c, n, "") for c, n in names.stadiums()]),
                  hd=BoolProperty(name=T("Texturas HD", "HD textures"), default=True, translation_context=CONTEXT,
                                  description=T("Guarda además cada imagen nueva a su tamaño (hasta 2048 px) como "
                                                "textura de reemplazo: el recomp la usa en vez de la de 256 px del pack",
                                                "Also save each new image at its own size (up to 2048 px) as a "
                                                "replacement texture: the recomp uses it instead of the pack's 256 px one")),
                  menu=BoolProperty(name=T("Foto del menú", "Menu photo"), default=True, translation_context=CONTEXT,
                                    description=T("En la elección de estadio, muestra una foto renderizada de este "
                                                  "estadio en vez de la del estadio base",
                                                  "In the stadium select, show a rendered photo of this stadium "
                                                  "instead of the base stadium's")),
                  home_line=StringProperty(name=T("Línea bajo la foto", "Line under the photo"), default="",
                                           translation_context=CONTEXT,
                                           description=T("Por ejemplo «Home of the Miami Marlins». Vacía, no se "
                                                         "muestra ninguna",
                                                         "For example 'Home of the Miami Marlins'. Empty shows none")))

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def invoke(self, context, event):
        coll = stadium_collection(context)
        game = preview.game_folder(coll)
        if not self.mods_folder and game is not None:
            self.mods_folder = str(game / "mods")
        return context.window_manager.invoke_props_dialog(self, width=420)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "mod_name")
        layout.prop(self, "mods_folder")
        if scratch.is_scratch(stadium_collection(context)):
            layout.prop(self, "new_code")
            layout.prop(self, "new_name")
            layout.label(text=T("Estadio desde cero: se añade como estadio nuevo, con los datos de juego del estadio "
                                "de datos.", "Stadium from scratch: added as a new stadium, with the game data of the "
                                "data stadium."), translate=False)
            layout.prop(self, "menu")
            if self.menu:
                layout.prop(self, "home_line")
            layout.prop(self, "hd")
            return
        layout.prop(self, "mode", expand=True)
        if self.mode == "new":
            layout.prop(self, "new_code")
            layout.prop(self, "new_name")
            layout.label(text=T("Usa los datos de juego (medidas, partidas) del estadio importado.",
                                "Uses the game data (size, saves) of the imported stadium."), translate=False)
            layout.prop(self, "menu")
            if self.menu:
                layout.prop(self, "home_line")
        else:
            layout.prop(self, "target")
        layout.prop(self, "hd")
        layout.label(text=T("Solo se reescriben las piezas que cambiaste.", "Only the pieces you changed are rewritten."),
                     translate=False)

    def execute(self, context):
        coll = stadium_collection(context)
        if not self.mods_folder:
            self.report({"ERROR"}, T("Elige la carpeta mods del juego", "Pick the game's mods folder"))
            return {"CANCELLED"}
        default_name = T("Mi estadio", "My stadium")
        if scratch.is_scratch(coll):
            try:
                st = scratch.export_scratch(coll, bpy.path.abspath(self.mods_folder), self.mod_name.strip() or default_name,
                                            self.new_code.strip().lower(), self.new_name.strip() or default_name,
                                            self.hd, self.menu, self.home_line)
            except exporter.ExportFailure as e:
                self.report({"ERROR"}, str(e))
                return {"CANCELLED"}
            msg = T(f"Mod en {st['folder']}: {st['pieces']} piezas, {st['textures']} texturas ({st['hd']} HD), "
                    f"colisión de {st['collision']} triángulos, {st['size'] / 1048576:.1f} MB",
                    f"Mod in {st['folder']}: {st['pieces']} pieces, {st['textures']} textures ({st['hd']} HD), "
                    f"collision of {st['collision']} triangles, {st['size'] / 1048576:.1f} MB")
            for w in st["warnings"]:
                self.report({"WARNING"}, w)
            self.report({"WARNING"} if st["over_budget"] else {"INFO"},
                        msg + (T(". Pasa de 80 MB: puede que el partido no cargue.", ". Over 80 MB: the match may not load.")
                               if st["over_budget"] else T(". Actívalo en MODS del launcher.", ". Enable it in MODS in the launcher.")))
            return {"FINISHED"}
        if self.mode == "new":
            target, new_name = self.new_code.strip().lower(), self.new_name.strip() or default_name
        else:
            target, new_name = (coll["pp_stadium"] if self.target == "same" else self.target), None
        try:
            st = exporter.export_stadium(coll, bpy.path.abspath(self.mods_folder), self.mod_name.strip() or default_name,
                                         target, new_name, self.hd, self.menu, self.home_line)
        except (exporter.ExportFailure, ValueError) as e:            # tbl_write.TreeError is a ValueError
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        msg = T(f"Mod en {st['folder']}: {st['pieces']} piezas reescritas, {st['moved']} movidas, "
                f"{st['emptied']} quitadas, {st['textures']} texturas nuevas ({st['hd']} HD), {st['size'] / 1048576:.1f} MB",
                f"Mod in {st['folder']}: {st['pieces']} pieces rewritten, {st['moved']} moved, "
                f"{st['emptied']} removed, {st['textures']} new textures ({st['hd']} HD), {st['size'] / 1048576:.1f} MB")
        if st["collision"]:
            msg += T(f", colisión nueva de {st['collision']} triángulos", f", new collision of {st['collision']} triangles")
        if st["over_budget"]:
            self.report({"WARNING"}, msg + T(". Pasa de 80 MB: puede que el partido no cargue.",
                                             ". Over 80 MB: the match may not load."))
        else:
            self.report({"INFO"}, msg + T(". Actívalo en MODS del launcher.", ". Enable it in MODS in the launcher."))
        return {"FINISHED"}


class PP_OT_new_scratch(bpy.types.Operator, ImportHelper):
    bl_idname = "object.powerpros_new_scratch"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    filename_ext = ""
    filter_glob: StringProperty(default="*.bin;*.exe;*.toml", options={"HIDDEN"})

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Nuevo estadio desde cero", "New stadium from scratch")
        cls.bl_description = T("Crea una colección vacía para modelar un estadio sin estadio base: a cada objeto le "
                               "das su papel (suelo, grada, valla, público…) en este panel",
                               "Create an empty collection to model a stadium with no base stadium: give each object "
                               "its role (ground, stand, fence, crowd…) in this panel")
        set_props(cls, stadium=EnumProperty(
            name=T("Estadio de datos", "Data stadium"), items=[(c, n, "") for c, n in names.stadiums()], default="col",
            translation_context=CONTEXT,
            description=T("Sus partidas, menú, cielo y sonidos son los del estadio nuevo; sus piezas no se usan",
                          "Its saves, menu, sky and sounds are the new stadium's; its pieces are not used")))

    def draw(self, context):
        layout = self.layout
        layout.label(text=T("Elige la carpeta del juego o files/project.bin", "Pick the game folder or files/project.bin"),
                     translate=False)
        layout.prop(self, "stadium")

    def execute(self, context):
        try:
            project_bin, game_folder = importer.find_project_bin(self.filepath)
        except importer.ImportFailure as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        coll = bpy.data.collections.new(T("Estadio desde cero", "Stadium from scratch"))
        coll["pp_scratch"] = 1
        coll["pp_stadium"] = self.stadium
        coll["pp_project_bin"] = str(project_bin)
        if game_folder is not None:
            preview.remember_game_folder(coll, game_folder)
        context.scene.collection.children.link(coll)
        try:
            scratch.add_game_base_objects(coll)
        except scratch.ScratchFailure as e:
            bpy.data.collections.remove(coll)
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        context.view_layer.active_layer_collection = context.view_layer.layer_collection.children[coll.name]
        self.report({"INFO"}, T(f"{coll.name}: mete aquí tus objetos y dales su papel en el panel Power Pros",
                                f"{coll.name}: put your objects here and give them their role in the Power Pros panel"))
        return {"FINISHED"}


class PP_OT_preview_in_game(bpy.types.Operator):
    bl_idname = "view3d.powerpros_preview_in_game"
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Ver en el juego", "View in game")
        cls.bl_description = T("Abre el juego directamente en este estadio, con una cámara libre donde está esta vista "
                               "(WASD, Q/E, botón derecho para mirar). No toca tus mods",
                               "Open the game straight in this stadium, with a free camera where this view is "
                               "(WASD, Q/E, right mouse button to look). Your mods are left alone")

    # Where the game is, when neither the stadium nor the preferences know it: a folder chooser asks.
    directory: StringProperty(subtype="DIR_PATH", options={"HIDDEN", "SKIP_SAVE"})
    filepath: StringProperty(subtype="FILE_PATH", options={"HIDDEN", "SKIP_SAVE"})
    pose: FloatVectorProperty(size=6, options={"HIDDEN", "SKIP_SAVE"})
    has_pose: BoolProperty(default=False, options={"HIDDEN", "SKIP_SAVE"})

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def view_pose(self, context):
        space = context.space_data
        if space is not None and space.type == "VIEW_3D":
            window = next((r for r in context.area.regions if r.type == "WINDOW"), None)
            aspect = window.width / max(window.height, 1) if window is not None else 16 / 9
            return preview.camera_pose(space.region_3d.view_matrix, space.lens, aspect=aspect)
        return (0.0, 150.0, 600.0, 0.0, -0.2, 45.0)     # behind home plate

    def invoke(self, context, event):
        self.pose, self.has_pose = self.view_pose(context), True     # the file browser has no 3D view
        if preview.game_folder(stadium_collection(context)) is None:
            self.report({"INFO"}, T("Elige la carpeta del juego (la que tiene WiiCompiled.exe)",
                                    "Choose the game folder (the one with WiiCompiled.exe)"))
            context.window_manager.fileselect_add(self)
            return {"RUNNING_MODAL"}
        return self.execute(context)

    def execute(self, context):
        coll = stadium_collection(context)
        picked = self.filepath or self.directory
        if picked:
            folder = preview.game_folder_of(picked)
            if folder is None:
                self.report({"ERROR"}, T(f"No hay WiiCompiled.exe en {picked}: elige la carpeta donde está el juego",
                                         f"No WiiCompiled.exe in {picked}: choose the folder the game is in"))
                return {"CANCELLED"}
            preview.remember_game_folder(coll, folder)
        pose = tuple(self.pose) if self.has_pose else self.view_pose(context)
        try:
            preview.launch(coll, pose)
        except (exporter.ExportFailure, OSError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, T("Abriendo el juego en el estadio (unos segundos)…",
                                "Opening the game in the stadium (a few seconds)…"))
        return {"FINISHED"}


class PP_OT_convert_objects(bpy.types.Operator):
    bl_idname = "object.powerpros_convert"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Convertir objetos en piezas", "Convert objects to pieces")
        cls.bl_description = T("Reparte las caras de los objetos seleccionados (un modelo propio) entre las piezas del estadio",
                               "Split the faces of the selected objects (your own model) among the stadium pieces")
        set_props(cls,
                  replace=BoolProperty(name=T("Quitar el estadio original", "Remove the original stadium"), default=True,
                                       description=T("Las piezas pierden su geometría y las que no reciban nada se borran",
                                                     "Pieces lose their geometry; those that get nothing are deleted"),
                                       translation_context=CONTEXT),
                  bake_light=BoolProperty(name=T("Sombreado simple", "Simple shading"), default=True,
                                          description=T("El juego dibuja los estadios sin luz: guarda un sol suave en los colores",
                                                        "The game draws stadiums unlit: bakes a soft sun into the colours"),
                                          translation_context=CONTEXT))

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=420)

    def draw(self, context):
        layout = self.layout
        layout.label(text=T("El modelo debe estar en metros, con el plato en el origen",
                            "The model must be in metres, with home plate at the origin"), translate=False)
        layout.label(text=T("y la segunda base hacia +Y (como el estadio importado).",
                            "and second base towards +Y (like the imported stadium)."), translate=False)
        layout.prop(self, "replace")
        layout.prop(self, "bake_light")

    def execute(self, context):
        coll = stadium_collection(context)
        sources = [o for o in context.selected_objects if o.type == "MESH" and "pp_kind" not in o]
        if not sources:
            self.report({"ERROR"}, T("Selecciona los objetos de tu modelo (no las piezas del estadio)",
                                     "Select the objects of your model (not the stadium pieces)"))
            return {"CANCELLED"}
        warning = convert.check_frame(sources)
        if warning:
            self.report({"ERROR"}, warning)
            return {"CANCELLED"}
        try:
            result = convert.convert(coll, sources, self.replace, self.bake_light)
        except convert.ConvertFailure as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, T(f"Modelo repartido en {len(result)} piezas ({sum(result.values())} caras). "
                                "Genera la colisión si cambiaste el campo o las vallas.",
                                f"Model split into {len(result)} pieces ({sum(result.values())} faces). "
                                "Generate the collision if you changed the field or the fences."))
        return {"FINISHED"}


class PP_OT_generate_collision(bpy.types.Operator):
    bl_idname = "object.powerpros_generate_collision"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Generar colisión", "Generate collision")
        cls.bl_description = T("Crea la colisión del estadio a partir de sus piezas: suelo, vallas, postes de foul y gradas",
                               "Build the stadium's collision from its pieces: ground, fences, foul poles and stands")

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def execute(self, context):
        objs = collision_gen.generate(stadium_collection(context))
        if not objs:
            self.report({"ERROR"}, T("No encuentro el campo: hace falta suelo a la altura 0 y paredes alrededor del plato",
                                     "Field not found: it needs ground at height 0 and walls around home plate"))
            return {"CANCELLED"}
        n = sum(len(o.data.polygons) for o in objs)
        self.report({"INFO"}, T(f"Colisión nueva: {n} triángulos en {len(objs)} tipos",
                                f"New collision: {n} triangles of {len(objs)} kinds"))
        return {"FINISHED"}


NONE = "NONE"      # Blender enums have no empty item: the "" of FieldParams (colour, generated ground)


class PP_OT_generate_field(bpy.types.Operator):
    bl_idname = "object.powerpros_generate_field"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    FIELDS = ("lf", "lcf", "cf", "rcf", "rf", "wall_l", "wall_lc", "wall_rc", "wall_r", "color_l", "color_lc",
              "color_rc", "color_r", "style_l", "style_lc", "style_rc", "style_r", "ground", "foul_wall", "track", "foul", "backstop", "surface", "infield", "pattern",
              "stripe", "grass_color", "dirt_color", "signs", "lines")

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Generar campo", "Generate field")
        cls.bl_description = T("Crea el campo del estadio desde cero con unas medidas: césped o turf, tierra, pista de "
                               "aviso, vallas, postes de foul y carteles de distancia. Vuelve a pulsarlo para cambiarlo",
                               "Build the stadium's field from a few measurements: grass or turf, dirt, warning "
                               "track, walls, foul poles and distance signs. Press it again to change it")
        d = field_gen.FieldParams()
        feet = lambda es, en, v, lo, hi: FloatProperty(name=T(es, en), default=v, min=lo, max=hi, precision=0,
                                                       translation_context=CONTEXT)
        looks = [(NONE, T("Color", "Colour"), T("El color de abajo", "The colour below"))] + [
            (code, T(f"Valla de {name}", f"{name}'s fence"),
             T("Su textura principal, en su proporción, a la altura de esta valla",
               "Its main texture, in its proportions, at this wall's height")) for code, name in names.stadiums()]
        look = lambda es, en: EnumProperty(name=T(es, en), items=looks, translation_context=CONTEXT)
        colour = lambda es, en, v: FloatVectorProperty(name=T(es, en), subtype="COLOR", default=v, min=0, max=1,
                                                       translation_context=CONTEXT)
        set_props(cls,
                  lf=feet("Jardín izquierdo (línea, pies)", "Left field line (feet)", d.lf, 250, 450),
                  lcf=feet("Izquierdo-central (pies)", "Left-centre (feet)", d.lcf, 280, 480),
                  cf=feet("Central (pies)", "Centre field (feet)", d.cf, 300, 500),
                  rcf=feet("Derecho-central (pies)", "Right-centre (feet)", d.rcf, 280, 480),
                  rf=feet("Jardín derecho (línea, pies)", "Right field line (feet)", d.rf, 250, 450),
                  wall_l=feet("Valla: altura del sector izquierdo (pies)", "Wall: left sector height (feet)",
                              d.wall_l, 3, 50),
                  wall_lc=feet("Valla: altura del sector izquierdo-central (pies)",
                               "Wall: left-centre sector height (feet)", d.wall_lc, 3, 50),
                  wall_rc=feet("Valla: altura del sector derecho-central (pies)",
                               "Wall: right-centre sector height (feet)", d.wall_rc, 3, 50),
                  wall_r=feet("Valla: altura del sector derecho (pies)", "Wall: right sector height (feet)",
                              d.wall_r, 3, 50),
                  color_l=colour("Valla: color del sector izquierdo", "Wall: left sector colour", d.color_l),
                  color_lc=colour("Valla: color del sector izquierdo-central", "Wall: left-centre sector colour",
                                  d.color_lc),
                  color_rc=colour("Valla: color del sector derecho-central", "Wall: right-centre sector colour",
                                  d.color_rc),
                  color_r=colour("Valla: color del sector derecho", "Wall: right sector colour", d.color_r),
                  ground=EnumProperty(
                      name=T("Suelo", "Ground"), translation_context=CONTEXT,
                      items=[(NONE, T("Generado", "Generated"), T("Césped, tierra, pista y líneas de estas medidas",
                                                                "Grass, dirt, track and lines of these measurements"))]
                      + [(code, T(f"El de {name}", f"{name}'s"),
                          T("Sus piezas de suelo tal cual: turf, tierra, manchas y líneas como en el juego",
                            "Its ground pieces as they are: turf, dirt, stains and lines as in the game"))
                         for code, name in names.stadiums()]),
                  ground_sizes=BoolProperty(
                      name=T("Con las medidas de ese estadio", "With that stadium's measurements"), default=True,
                      translation_context=CONTEXT,
                      description=T("Pone las cinco distancias de la valla donde están en ese estadio, para que su "
                                    "pista de aviso y su césped casen con tu valla",
                                    "Puts the five wall distances where that stadium has them, so its warning "
                                    "track and grass meet your wall")),
                  style_l=look("Valla: estilo del sector izquierdo", "Wall: left sector look"),
                  style_lc=look("Valla: estilo del sector izquierdo-central", "Wall: left-centre sector look"),
                  style_rc=look("Valla: estilo del sector derecho-central", "Wall: right-centre sector look"),
                  style_r=look("Valla: estilo del sector derecho", "Wall: right sector look"),
                  foul_wall=feet("Altura de la valla de foul (pies)", "Foul wall height (feet)", d.foul_wall, 2, 15),
                  track=feet("Pista de aviso (pies)", "Warning track (feet)", d.track, 0, 30),
                  foul=feet("Territorio de foul junto a 1ª y 3ª (pies)", "Foul ground by 1st and 3rd (feet)",
                            d.foul, 10, 120),
                  backstop=feet("Del plato al backstop (pies)", "Home plate to backstop (feet)", d.backstop, 35, 120),
                  surface=EnumProperty(name=T("Superficie", "Surface"), default=d.surface, translation_context=CONTEXT,
                                       items=[("grass", T("Césped", "Grass"), ""), ("turf", T("Turf", "Turf"), "")]),
                  infield=EnumProperty(name=T("Cuadro", "Infield"), default=d.infield, translation_context=CONTEXT,
                                       items=[("grass", T("Césped con tierra", "Grass with a dirt skin"), ""),
                                              ("dirt", T("Todo tierra", "All dirt"), ""),
                                              ("turf", T("Turf con recortes de tierra", "Turf with dirt cut-outs"), "")]),
                  pattern=EnumProperty(name=T("Corte del césped", "Mowing pattern"), default=d.pattern,
                                       translation_context=CONTEXT,
                                       items=[("none", T("Ninguno", "None"), ""), ("stripes", T("Franjas", "Stripes"), ""),
                                              ("checker", T("Cuadros", "Checker"), ""),
                                              ("diagonal", T("Diagonal", "Diagonal"), ""),
                                              ("circles", T("Círculos", "Circles"), "")]),
                  stripe=feet("Ancho de franja (pies)", "Stripe width (feet)", d.stripe, 5, 40),
                  grass_color=FloatVectorProperty(name=T("Color del césped", "Grass colour"), subtype="COLOR",
                                                  default=d.grass_color, min=0, max=1, translation_context=CONTEXT),
                  dirt_color=FloatVectorProperty(name=T("Color de la tierra", "Dirt colour"), subtype="COLOR",
                                                 default=d.dirt_color, min=0, max=1, translation_context=CONTEXT),
                  signs=BoolProperty(name=T("Carteles de distancia", "Distance signs"), default=True,
                                     translation_context=CONTEXT),
                  lines=BoolProperty(name=T("Líneas de cal", "Chalk lines"), default=True, translation_context=CONTEXT,
                                     description=T("Líneas de foul, cajas de bateo, del catcher y de los coaches, "
                                                   "carril a primera y círculos de espera",
                                                   "Foul lines, batter's, catcher's and coach's boxes, the running "
                                                   "lane and the on-deck circles")))

    @classmethod
    def poll(cls, context):
        return scratch.is_scratch(stadium_collection(context))

    def invoke(self, context, event):
        saved = field_gen.FieldParams.from_json(stadium_collection(context).get("pp_field", ""))
        for k in self.FIELDS:
            setattr(self, k, getattr(saved, k) if getattr(saved, k) != "" else NONE)   # "" is no enum item
        return context.window_manager.invoke_props_dialog(self, width=460)

    def execute(self, context):
        values = {k: "" if getattr(self, k) == NONE else getattr(self, k) for k in self.FIELDS}
        for k in field_gen.FieldParams.COLORS:
            values[k] = tuple(values[k])
        if values["ground"] and self.ground_sizes:
            found = field_gen.game_distances(stadium_collection(context)["pp_project_bin"], values["ground"])
            for k, feet in zip(("lf", "lcf", "cf", "rcf", "rf"), found):
                if feet:
                    values[k] = feet
        made = field_gen.generate(stadium_collection(context), field_gen.FieldParams(**values))
        self.report({"INFO"}, T(f"Campo nuevo: {len(made)} objetos. Pulsa «Generar colisión» para la pelota",
                                f"New field: {len(made)} objects. Press «Generate collision» for the ball"))
        return {"FINISHED"}


class PP_OT_toggle_players(bpy.types.Operator):
    bl_idname = "object.powerpros_toggle_players"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Jugadores de referencia", "Reference players")
        cls.bl_description = T("Pone o quita figuras donde se colocan los jugadores, el árbitro, los coaches, el "
                               "bateador en espera y las bancas, para diseñar el estadio a su alrededor. No se "
                               "exportan ni tienen colisión",
                               "Put or take away figures where the fielders, umpire, coaches, on-deck batter and "
                               "dugouts are, to design the stadium around them. They are not exported and have no "
                               "collision")

    @classmethod
    def poll(cls, context):
        return scratch.is_scratch(stadium_collection(context))

    def execute(self, context):
        shown = field_gen.toggle_players(stadium_collection(context))
        self.report({"INFO"}, T("Jugadores de referencia puestos" if shown else "Jugadores de referencia quitados",
                                "Reference players shown" if shown else "Reference players removed"))
        return {"FINISHED"}


_part_items = []      # Blender needs the dynamic enum's strings kept alive


def part_items(self, context):
    coll = stadium_collection(context)
    try:
        found = game_parts.parts(coll["pp_project_bin"], self.stadium) if coll is not None else ()
    except (OSError, KeyError, ValueError):
        found = ()
    _part_items[:] = [(str(i), f"{n} — {T(*scratch.ROLES[r][1:])}" + (T(" · oculta desde arriba", " · hidden from above")
                                                                      if h else ""), "") for i, n, r, h in found]
    return _part_items or [("-1", T("(sin piezas)", "(no pieces)"), "")]


class PP_OT_import_ootp(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.powerpros_ootp_park"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    filename_ext = ""
    filter_glob: StringProperty(default="*.zip;*.rar;*.7z;*.obj", options={"HIDDEN"})

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Importar parque OOTP", "Import OOTP ballpark")
        cls.bl_description = T("Mete un parque de Out of the Park Baseball (su .zip o .rar tal cual, o su .obj) en este estadio "
                               "desde cero: lo gira, separa el suelo, pone el público y genera la colisión. Después, "
                               "«Ver en el juego» y «Exportar mod de estadio»",
                               "Put an Out of the Park Baseball ballpark (its .zip or .rar as downloaded, or its .obj) into this "
                               "from-scratch stadium: it turns it, splits the ground, sets the crowd and generates the "
                               "collision. Then «View in game» and «Export stadium mod»")
        set_props(cls, game_crowd=BoolProperty(
            name=T("Público del juego en todas las gradas", "The game's crowd on all the seating"), default=False,
            translation_context=CONTEXT,
            description=T("Pone a la gente del juego también en las gradas que traen su propia imagen. Sin marcar, solo "
                          "en las que no la traen (OOTP la saca de su carpeta «attendance»)",
                          "Puts the game's people also on the seating that brings its own picture. Unticked, only on "
                          "the seating without one (OOTP takes it from its «attendance» folder)")))

    @classmethod
    def poll(cls, context):
        return scratch.is_scratch(stadium_collection(context))

    def execute(self, context):
        coll = stadium_collection(context)
        try:
            r = ootp_park.import_park(context, coll, self.filepath, self.game_crowd)
        except (ootp_park.OotpFailure, scratch.ScratchFailure, OSError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        msg = T(f"Parque importado: girado {r['turned']}°, {r['ground']} caras de suelo, {r['crowd']} de público, "
                f"{r['collision']} triángulos de colisión",
                f"Ballpark imported: turned {r['turned']}°, {r['ground']} ground faces, {r['crowd']} crowd faces, "
                f"{r['collision']} collision triangles")
        if r["missing"]:
            msg += T(f". Sin imagen (quedan de color liso): {', '.join(r['missing'][:6])}",
                     f". No image (left plain colour): {', '.join(r['missing'][:6])}")
        self.report({"WARNING"} if r["missing"] else {"INFO"}, msg)
        return {"FINISHED"}


class PP_OT_add_game_part(bpy.types.Operator):
    bl_idname = "object.powerpros_add_game_part"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Añadir pieza del juego", "Add a game piece")
        cls.bl_description = T("Trae una grada, público, cristales, marcador o edificio de un estadio del juego (de tu "
                               "project.bin), en su sitio y con su papel; muévela, gírala o cópiala como quieras",
                               "Bring a stand, crowd, glass, scoreboard or building from one of the game's stadiums "
                               "(from your project.bin), in its place and with its role; move, turn or copy it as you like")
        set_props(cls,
                  stadium=EnumProperty(name=T("Estadio", "Stadium"), items=[(c, n, "") for c, n in names.stadiums()],
                                       default="nyy", translation_context=CONTEXT),
                  part=EnumProperty(name=T("Pieza", "Piece"), items=part_items, translation_context=CONTEXT),
                  whole_zone=BoolProperty(name=T("Con toda su sección (grada, público, cristales)",
                                                 "With its whole section (stand, crowd, glass)"),
                                          default=True, translation_context=CONTEXT))

    @classmethod
    def poll(cls, context):
        return scratch.is_scratch(stadium_collection(context))

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=560)

    def execute(self, context):
        coll = stadium_collection(context)
        index = int(self.part) if self.part.lstrip("-").isdigit() else -1
        if index < 0:
            self.report({"ERROR"}, T("Ese estadio no tiene piezas que añadir", "That stadium has no pieces to add"))
            return {"CANCELLED"}
        project_bin = coll["pp_project_bin"]
        indices = game_parts.with_zone(project_bin, self.stadium, index) if self.whole_zone else [index]
        made = game_parts.insert(coll, self.stadium, indices)
        for o in context.selected_objects:
            o.select_set(False)
        for o in made:
            o.select_set(True)
        if made:
            context.view_layer.objects.active = made[0]
        self.report({"INFO"}, T(f"{len(made)} piezas de {names.name(self.stadium)}",
                                f"{len(made)} pieces from {names.name(self.stadium)}"))
        return {"FINISHED"}


class PP_OT_collision_visibility(bpy.types.Operator):
    bl_idname = "object.powerpros_collision_visibility"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    show: BoolProperty(default=True, options={"HIDDEN"})

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Ver u ocultar la colisión", "Show or hide the collision")
        cls.bl_description = T("Muestra u oculta todos los tipos de colisión", "Show or hide every kind of collision")

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def execute(self, context):
        surfaces.show(context, stadium_collection(context), self.show)
        return {"FINISHED"}


class PP_OT_make_collision(bpy.types.Operator):
    bl_idname = "object.powerpros_make_collision"
    bl_options = {"REGISTER", "UNDO"}
    bl_translation_context = CONTEXT

    @classmethod
    def relabel(cls):
        cls.bl_label = T("Convertir en colisión", "Make collision")
        cls.bl_description = T("Convierte las mallas seleccionadas en colisión del tipo elegido: la pelota y los "
                               "jugadores chocan con ellas en el juego. Dejan de dibujarse",
                               "Turn the selected meshes into collision of the chosen kind: the ball and the players "
                               "hit them in the game. They are no longer drawn")
        set_props(cls, surface=EnumProperty(
            name=T("Tipo", "Kind"), translation_context=CONTEXT, default="9",
            items=[(str(sid), f"{surfaces.name(sid)} ({sid})", "") for sid in surfaces.SURFACES]))

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None and any(o.type == "MESH" for o in context.selected_objects)

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        coll = stadium_collection(context)
        meshes = [o for o in context.selected_objects if o.type == "MESH" and o.get("pp_kind") not in ("game_base",)]
        for o in meshes:
            surfaces.make_collision(o, int(self.surface), coll)
        surfaces.show(context, coll, True)
        self.report({"INFO"}, T(f"{len(meshes)} objetos son ahora colisión: {surfaces.name(int(self.surface))}",
                                f"{len(meshes)} objects are collision now: {surfaces.name(int(self.surface))}"))
        return {"FINISHED"}


class PP_PT_info(bpy.types.Panel):
    bl_label = "Power Pros"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Power Pros"
    bl_translation_context = CONTEXT

    def draw(self, context):
        layout = self.layout
        layout.operator(PP_OT_import_stadium.bl_idname, icon="IMPORT")
        layout.operator(PP_OT_new_scratch.bl_idname, icon="ADD")
        layout.operator(PP_OT_export_stadium.bl_idname, icon="EXPORT")
        layout.operator(PP_OT_preview_in_game.bl_idname, icon="PLAY")
        layout.operator(PP_OT_convert_objects.bl_idname, icon="MESH_DATA")
        if scratch.is_scratch(stadium_collection(context)):
            layout.operator(PP_OT_import_ootp.bl_idname, icon="IMPORT")
            layout.operator(PP_OT_generate_field.bl_idname, icon="MESH_GRID")
            shown = field_gen.players_collection(stadium_collection(context)) is not None
            layout.operator(PP_OT_toggle_players.bl_idname, icon="HIDE_OFF" if shown else "HIDE_ON", depress=shown)
            layout.operator(PP_OT_add_game_part.bl_idname, icon="HOME")
        layout.operator(PP_OT_generate_collision.bl_idname, icon="MOD_PHYSICS")
        obj = context.active_object
        coll = stadium_collection(context)
        if coll is not None:
            draw_collision(layout, coll)
        if obj is not None and scratch.is_scratch(coll) and obj.get("pp_kind") == "game_base":
            box = layout.box()
            box.label(text=T("Pieza del juego: ", "Game piece: ") + obj.name, translate=False)
            box.label(text=T("Se exporta tal cual y en su sitio. Bórrala para usar la tuya.",
                             "Exported as it is, in its place. Delete it to use your own."), translate=False)
            return
        if obj is not None and scratch.is_scratch(coll) and obj.name in coll.all_objects and obj.get("pp_kind") != "collision":
            box = layout.box()
            box.label(text=T("Pieza del estadio desde cero: ", "From-scratch stadium piece: ") + obj.name, translate=False)
            box.prop(obj, "powerpros_role")
            if obj.powerpros_role == "auto":
                role = scratch.role_of(obj)
                box.label(text=T("Ahora: ", "Now: ") + T(scratch.ROLES[role][1], scratch.ROLES[role][2]), translate=False)
            box.prop(obj, "powerpros_hide_above")
            box.prop(obj, "powerpros_two_sided")
            box.prop(obj, "powerpros_separate_layers")
            return
        if obj is None or "pp_kind" not in obj:
            layout.label(text=T("Selecciona una pieza de un estadio importado", "Select a piece of an imported stadium"),
                         translate=False)
            return
        box = layout.box()
        if obj["pp_kind"] == "collision":
            box.label(text=T("Colisión: ", "Collision: ") + obj.name, translate=False)
            face = obj.data.polygons.active
            if face is not None and face < len(obj.data.polygons):
                p = obj.data.polygons[face]
                mat = obj.data.materials[p.material_index] if p.material_index < len(obj.data.materials) else None
                sid = surfaces.surface_of(mat, obj.get("pp_surface", surfaces.DEFAULT))
                box.label(text=T("Cara activa: ", "Active face: ") + f"{surfaces.name(sid)} ({sid})", translate=False)
            box.label(text=T("Cambia el tipo de una cara con su material «Superficie N»",
                             "Change a face's kind with its «Superficie N» material"), translate=False)
            return
        box.label(text=T("Pieza: ", "Piece: ") + obj.name, translate=False)
        box.label(text=T("Modelo: ", "Model: ") + str(obj.get("pp_wmodel", "")), translate=False)
        box.label(text=T("Nodo de Maya: ", "Maya node: ") + str(obj.get("pp_node", "")), translate=False)
        if "pp_placement_flags" in obj:
            box.label(text=T("Flags de colocación: ", "Placement flags: ") + str(obj["pp_placement_flags"]),
                      translate=False)
        mat = obj.active_material
        if mat is not None:
            box.label(text=f"Material: {mat.name}", translate=False)


def draw_collision(layout, coll):
    """The collision box: triangles, All/None, one eye per kind, Make collision."""
    objs = surfaces.collision_objects(coll)
    box = layout.box()
    total = sum(len(o.data.polygons) for o in objs)
    box.label(text=T(f"Colisión: {total} caras", f"Collision: {total} faces"), icon="MOD_PHYSICS", translate=False)
    if objs:
        row = box.row(align=True)
        row.operator(PP_OT_collision_visibility.bl_idname, text=T("Todo", "All"), translate=False).show = True
        row.operator(PP_OT_collision_visibility.bl_idname, text=T("Nada", "None"), translate=False).show = False
        col = box.column(align=True)
        for o in objs:
            r = col.row(align=True)
            r.prop(o, "hide_viewport", text="", emboss=False, icon="HIDE_ON" if o.hide_viewport else "HIDE_OFF")
            r.label(text=f"{o.name}: {len(o.data.polygons)}", translate=False)
    box.operator(PP_OT_make_collision.bl_idname, icon="ADD")


def menu_import(self, context):
    self.layout.operator(PP_OT_import_stadium.bl_idname, text=T("Estadio de Power Pros 2008", "Power Pros 2008 stadium"),
                         translate=False)


def menu_export(self, context):
    self.layout.operator(PP_OT_export_stadium.bl_idname,
                         text=T("Mod de estadio de Power Pros 2008", "Power Pros 2008 stadium mod"), translate=False)


CLASSES = (PP_OT_import_stadium, PP_OT_new_scratch, PP_OT_export_stadium, PP_OT_preview_in_game,
           PP_OT_convert_objects, PP_OT_import_ootp, PP_OT_generate_field, PP_OT_toggle_players, PP_OT_add_game_part, PP_OT_generate_collision, PP_OT_collision_visibility, PP_OT_make_collision, PP_PT_info)


def apply_language():
    for c in CLASSES:
        if hasattr(c, "relabel"):
            c.relabel()


def relabel():
    """Register the classes again with the texts of the current language (no restart needed)."""
    for c in reversed(CLASSES):
        if c.is_registered:
            bpy.utils.unregister_class(c)
    apply_language()
    for c in CLASSES:
        bpy.utils.register_class(c)
    wm = bpy.context.window_manager
    for window in (wm.windows if wm is not None else ()):
        for area in window.screen.areas:
            area.tag_redraw()


def language_changed(self, context):
    relabel()


class PP_preferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    language: EnumProperty(
        name="Idioma / Language",
        description="Idioma de los textos del complemento; se aplica al momento. "
                    "Automático: el de la interfaz de Blender / Language of the add-on texts; applies at once. "
                    "Auto: Blender's interface language",
        items=[("AUTO", "Automático / Auto", "Español si Blender está en español / Spanish if Blender is in Spanish"),
               ("ES", "Español", ""),
               ("EN", "English", "")],
        default="AUTO", update=language_changed, translation_context=CONTEXT)

    game_folder: StringProperty(
        name=T("Carpeta del juego", "Game folder"), subtype="DIR_PATH",
        description=T("La carpeta donde está WiiCompiled.exe. «Ver en el juego» la pregunta la primera vez y la "
                      "recuerda aquí; también es donde se exportan los mods por defecto",
                      "The folder WiiCompiled.exe is in. «View in game» asks for it the first time and remembers it "
                      "here; mods are exported there by default"))

    def draw(self, context):
        self.layout.prop(self, "language")
        self.layout.prop(self, "game_folder")


_msgbus_owner = object()


def blender_language_changed():
    """In Auto, follow a change of Blender's interface language."""
    addon = bpy.context.preferences.addons.get(__package__)
    if addon is not None and addon.preferences is not None and addon.preferences.language == "AUTO":
        relabel()


def register():
    bpy.utils.register_class(PP_preferences)
    scratch.register_properties()
    apply_language()
    for c in CLASSES:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(menu_import)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)
    for prop in ("language", "use_translate_interface"):
        bpy.msgbus.subscribe_rna(key=(bpy.types.PreferencesView, prop), owner=_msgbus_owner, args=(),
                                 notify=blender_language_changed, options={"PERSISTENT"})


def unregister():
    bpy.msgbus.clear_by_owner(_msgbus_owner)
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    bpy.types.TOPBAR_MT_file_import.remove(menu_import)
    for c in reversed(CLASSES):
        bpy.utils.unregister_class(c)
    bpy.utils.unregister_class(PP_preferences)
    scratch.unregister_properties()
