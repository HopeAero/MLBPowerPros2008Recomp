# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a Blender collection from a stadium of the player's project.bin."""
import tomllib
from pathlib import Path

import bpy
import numpy as np
from mathutils import Euler, Matrix

from . import fingerprint, names, pj7m, stadium_tbl, surfaces, textures
from .i18n import T

try:
    from . import projectbin                 # copied into the add-on zip by build_addon.py
except ImportError:
    import projectbin                        # repository: tools/ is on sys.path

# Game space: decimetres, Y up, -Z towards centre field. Blender: metres, Z up, +Y towards centre field.
TO_BLENDER = Matrix.Scale(0.1, 4) @ Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
FROM_BLENDER = TO_BLENDER.inverted()


class ImportFailure(Exception):
    pass


def find_project_bin(path):
    """(project.bin, game folder or None) from what the player picked: the file itself, the game
    folder (the one with UserData, where mods/ lives) or the data folder."""
    path = Path(path)
    if path.is_file() and path.name.lower() == "project.bin":
        return path, None
    folder = path if path.is_dir() else path.parent
    config = folder / "UserData" / "Config.toml"
    candidates = []
    if config.is_file():
        try:
            dvd = tomllib.loads(config.read_text(encoding="utf-8")).get("paths", {}).get("dvd_root")
        except (tomllib.TOMLDecodeError, UnicodeDecodeError):
            dvd = None
        if dvd:
            candidates.append(Path(dvd) / "files" / "project.bin")
    candidates += [folder / "files" / "project.bin", folder / "project.bin"]
    for c in candidates:
        if c.is_file():
            return c, folder if config.is_file() else None
    raise ImportFailure(T("No encuentro project.bin. Elige la carpeta del juego instalado con el Setup "
                          "(la que tiene UserData) o el archivo files/project.bin.",
                          "project.bin not found. Pick the game folder installed by the Setup "
                          "(the one with UserData) or the file files/project.bin."))


def to_blender(v):
    x, y, z = v
    return (0.1 * x, -0.1 * z, 0.1 * y)


def placement_matrix(pl):
    if pl is None:
        return Matrix.Identity(4)
    game = (Matrix.Translation(pl.translation) @ Euler(pl.rotation, "XYZ").to_matrix().to_4x4()
            @ Matrix.Diagonal((*pl.scale, 1.0)))
    return TO_BLENDER @ game @ FROM_BLENDER


def make_image(gfx, tex, cache):
    """(image, has transparent texels), decoded once per texture."""
    if tex.offset in cache:
        return cache[tex.offset]
    pixels = textures.rgba(gfx, tex)
    img = bpy.data.images.new(f"tex_{tex.offset:06X}", tex.width, tex.height, alpha=True)
    img.pixels.foreach_set((pixels[::-1].astype(np.float32) / 255.0).ravel())   # Blender rows go bottom-up
    img.pack()
    img["pp_texture_offset"] = tex.offset
    img["pp_pixels_hash"] = fingerprint.image_hash(img)
    cache[tex.offset] = (img, bool((pixels[..., 3] < 255).any()))
    return cache[tex.offset]


def make_material(gfx, model, index, images, cache):
    """Unlit like the stadium in the game: texture x vertex colour, alpha blended.
    Pieces named *_ADD_* are drawn additive (name convention of the game's artists; the flag
    that selects the blend mode is not decoded yet)."""
    mat_data = model.materials[index]
    tex = model.textures[model.texmaps[mat_data.texmaps[0]].texture] if mat_data.texmaps else None
    additive = "_ADD_" in model.name + "_"
    key = (mat_data.raw, tex.offset if tex else None, additive)
    if key in cache:
        return cache[key]
    mat = bpy.data.materials.new(f"{model.name}_mat{index}")
    mat["pp_material"] = mat_data.raw.hex()
    image, transparent = make_image(gfx, tex, images) if tex is not None else (None, False)
    shade(mat, image, transparent, additive)
    cache[key] = mat
    return mat


def shade(mat, image=None, transparent=False, additive=False):
    """Nodes that show mat as the game draws it: unlit, the image (if any) times the vertex colours."""
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (400, 0)
    vcol = nt.nodes.new("ShaderNodeVertexColor")
    vcol.layer_name = "Color"
    vcol.location = (-600, -100)
    color, alpha = vcol.outputs["Color"], vcol.outputs["Alpha"]
    if image is not None:
        node = nt.nodes.new("ShaderNodeTexImage")
        node.image = image
        node.location = (-600, 250)
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type, mix.blend_type = "RGBA", "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.location = (-300, 200)
        nt.links.new(node.outputs["Color"], mix.inputs[6])         # A and B of the RGBA mode
        nt.links.new(vcol.outputs["Color"], mix.inputs[7])
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.location = (-300, -100)
        nt.links.new(node.outputs["Alpha"], mul.inputs[0])
        nt.links.new(vcol.outputs["Alpha"], mul.inputs[1])
        color, alpha = mix.outputs[2], mul.outputs["Value"]
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.location = (-50, 150)
    nt.links.new(color, emit.inputs["Color"])
    clear = nt.nodes.new("ShaderNodeBsdfTransparent")
    clear.location = (-50, -100)
    if additive:
        nt.links.new(alpha, emit.inputs["Strength"])
        both = nt.nodes.new("ShaderNodeAddShader")
        nt.links.new(emit.outputs[0], both.inputs[0])
        nt.links.new(clear.outputs[0], both.inputs[1])
    else:
        both = nt.nodes.new("ShaderNodeMixShader")
        nt.links.new(alpha, both.inputs["Fac"])
        nt.links.new(clear.outputs[0], both.inputs[1])
        nt.links.new(emit.outputs[0], both.inputs[2])
    both.location = (200, 0)
    nt.links.new(both.outputs[0], out.inputs["Surface"])
    if transparent or additive:
        mat.surface_render_method = "BLENDED"
        mat.use_backface_culling = False


def build_mesh(gfx, model, images, materials):
    verts, loops_v, loop_nrm, loop_uv, loop_col, face_mat = [], [], [], [], [], []
    me = bpy.data.meshes.new(model.name)
    slots = {}
    for mesh in model.meshes:
        base = len(verts)
        pos = pj7m.array(gfx, mesh, "pos")
        nrm = pj7m.array(gfx, mesh, "nrm")
        clr = pj7m.array(gfx, mesh, "clr")
        tex = pj7m.array(gfx, mesh, "tex")
        verts += [to_blender(p) for p in pos]
        if mesh.material not in slots:
            slots[mesh.material] = len(slots)
            me.materials.append(make_material(gfx, model, mesh.material, images, materials))
        for tri in pj7m.triangles(mesh):
            for v in (tri[0], tri[2], tri[1]):          # GX is clockwise-front; Blender counter-clockwise
                loops_v.append(base + v[1])
                loop_nrm.append(to_blender(nrm[v[2]]))
                loop_col.append(clr[v[3]])
                u, t = tex[v[4]]
                loop_uv.append((u, 1.0 - t))
            face_mat.append(slots[mesh.material])
    nface = len(face_mat)
    me.vertices.add(len(verts))
    me.vertices.foreach_set("co", np.asarray(verts, np.float32).ravel())
    me.loops.add(len(loops_v))
    me.loops.foreach_set("vertex_index", np.asarray(loops_v, np.int32))
    me.polygons.add(nface)
    me.polygons.foreach_set("loop_start", np.arange(0, 3 * nface, 3, dtype=np.int32))
    me.polygons.foreach_set("material_index", np.asarray(face_mat, np.int32))
    uv = me.uv_layers.new(name="UVMap")
    uv.data.foreach_set("uv", np.asarray(loop_uv, np.float32).ravel())
    col = me.color_attributes.new("Color", "FLOAT_COLOR", "CORNER")      # exact round trip of the RGBA8 values
    col.data.foreach_set("color", (np.asarray(loop_col, np.float32) / 255.0).ravel())
    nrm_attr = me.attributes.new("pp_normal", "FLOAT_VECTOR", "CORNER")
    nrm_attr.data.foreach_set("vector", np.asarray(loop_nrm, np.float32).ravel())
    me.validate(clean_customdata=False)
    me.update()
    normals = np.empty(len(me.loops) * 3, np.float32)
    me.attributes["pp_normal"].data.foreach_get("vector", normals)
    me.attributes.remove(me.attributes["pp_normal"])
    me.normals_split_custom_set(normals.reshape(-1, 3))
    return me


def build_collision(table, parent):
    """The table's collision as one object per surface (surfaces.py)."""
    return surfaces.make_objects([to_blender(v) for v in table.vertices], table.triangles, parent)


def import_stadium(context, path, code, collision=True):
    project_bin, game_folder = find_project_bin(path)
    archive = projectbin.Archive(project_bin)
    gfx = archive.read(f"stadium_{code}.pack")
    tbl = archive.read(f"stadium_{code}tbl.pack")
    if gfx is None:
        raise ImportFailure(T(f"project.bin no tiene stadium_{code}.pack", f"project.bin has no stadium_{code}.pack"))
    table = stadium_tbl.parse_table(tbl) if tbl else None
    if table is None:
        offsets = pj7m.find_models(gfx)
        placements = [None] * len(offsets)
    else:
        placements = table.placements
        offsets = [pl.model for pl in placements]

    coll = bpy.data.collections.new(names.name(code))
    coll["pp_stadium"] = code
    coll["pp_project_bin"] = str(project_bin)
    if game_folder is not None:
        coll["pp_game_folder"] = str(game_folder)
    context.scene.collection.children.link(coll)
    images, materials, objects = {}, {}, []
    for off, pl in zip(offsets, placements):
        model = pj7m.parse_model(gfx, off)
        obj = bpy.data.objects.new(model.name, build_mesh(gfx, model, images, materials))
        obj.matrix_world = placement_matrix(pl)
        obj["pp_kind"] = "model"
        obj["pp_model_offset"] = off
        obj["pp_wmodel"] = model.wname
        obj["pp_node"] = model.node_name
        if pl is not None:
            obj["pp_placement_flags"] = f"0x{pl.flags:08X}"
            obj["pp_translation"] = pl.translation
            obj["pp_rotation"] = pl.rotation
            obj["pp_scale"] = pl.scale
        coll.objects.link(obj)
        obj["pp_signature"] = fingerprint.mesh_signature(obj)
        obj["pp_matrix"] = fingerprint.matrix_signature(obj)
        objects.append(obj)

    if collision and table and table.triangles:
        sub = bpy.data.collections.new(f"{coll.name} · Colisión")
        sub.hide_render = True           # its 70 m invisible walls would hide the stands in renders
        coll.children.link(sub)
        surfaces.remember(coll, build_collision(table, sub))
        layer = context.view_layer.layer_collection.children[coll.name].children[sub.name]
        layer.hide_viewport = True
    return coll, objects, images
