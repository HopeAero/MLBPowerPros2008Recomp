# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender menu entries, operators, side panel and preferences, in Spanish or English (i18n.py).

Blender reads labels, descriptions and properties when a class is registered, so they are set by
apply_language() before registering, and the classes are registered again when the language changes.
"""
from pathlib import Path

import bpy
from bpy.props import BoolProperty, EnumProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

from . import collision_gen, convert, exporter, importer, names
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
                                                "replacement texture: the recomp uses it instead of the pack's 256 px one")))

    @classmethod
    def poll(cls, context):
        return stadium_collection(context) is not None

    def invoke(self, context, event):
        coll = stadium_collection(context)
        if not self.mods_folder and coll.get("pp_game_folder"):
            self.mods_folder = str(Path(coll["pp_game_folder"]) / "mods")
        return context.window_manager.invoke_props_dialog(self, width=420)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "mod_name")
        layout.prop(self, "mods_folder")
        layout.prop(self, "mode", expand=True)
        if self.mode == "new":
            layout.prop(self, "new_code")
            layout.prop(self, "new_name")
            layout.label(text=T("Usa los datos de juego (medidas, menú) del estadio importado.",
                                "Uses the game data (size, menu) of the imported stadium."), translate=False)
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
        if self.mode == "new":
            target, new_name = self.new_code.strip().lower(), self.new_name.strip() or default_name
        else:
            target, new_name = (coll["pp_stadium"] if self.target == "same" else self.target), None
        try:
            st = exporter.export_stadium(coll, bpy.path.abspath(self.mods_folder), self.mod_name.strip() or default_name,
                                         target, new_name, self.hd)
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
            self.report({"WARNING"}, msg + T(". Pasa de 20 MB: puede que el partido no cargue.",
                                             ". Over 20 MB: the match may not load."))
        else:
            self.report({"INFO"}, msg + T(". Actívalo en MODS del launcher.", ". Enable it in MODS in the launcher."))
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
        obj = collision_gen.generate(stadium_collection(context))
        if obj is None:
            self.report({"ERROR"}, T("No encuentro el campo: hace falta suelo a la altura 0 y paredes alrededor del plato",
                                     "Field not found: it needs ground at height 0 and walls around home plate"))
            return {"CANCELLED"}
        self.report({"INFO"}, T(f"Colisión nueva: {len(obj.data.polygons)} triángulos",
                                f"New collision: {len(obj.data.polygons)} triangles"))
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
        layout.operator(PP_OT_export_stadium.bl_idname, icon="EXPORT")
        layout.operator(PP_OT_convert_objects.bl_idname, icon="MESH_DATA")
        layout.operator(PP_OT_generate_collision.bl_idname, icon="MOD_PHYSICS")
        obj = context.active_object
        if obj is None or "pp_kind" not in obj:
            layout.label(text=T("Selecciona una pieza de un estadio importado", "Select a piece of an imported stadium"),
                         translate=False)
            return
        box = layout.box()
        if obj["pp_kind"] == "collision":
            box.label(text=T("Malla de colisión", "Collision mesh"), translate=False)
            face = obj.data.polygons.active
            attr = obj.data.attributes.get("surface_id")
            if attr is not None and face is not None and face < len(attr.data):
                box.label(text=T("Superficie de la cara activa: ", "Surface of the active face: ")
                          + str(attr.data[face].value), translate=False)
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


def menu_import(self, context):
    self.layout.operator(PP_OT_import_stadium.bl_idname, text=T("Estadio de Power Pros 2008", "Power Pros 2008 stadium"),
                         translate=False)


def menu_export(self, context):
    self.layout.operator(PP_OT_export_stadium.bl_idname,
                         text=T("Mod de estadio de Power Pros 2008", "Power Pros 2008 stadium mod"), translate=False)


CLASSES = (PP_OT_import_stadium, PP_OT_export_stadium, PP_OT_convert_objects, PP_OT_generate_collision, PP_PT_info)


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

    def draw(self, context):
        self.layout.prop(self, "language")


_msgbus_owner = object()


def blender_language_changed():
    """In Auto, follow a change of Blender's interface language."""
    addon = bpy.context.preferences.addons.get(__package__)
    if addon is not None and addon.preferences is not None and addon.preferences.language == "AUTO":
        relabel()


def register():
    bpy.utils.register_class(PP_preferences)
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
