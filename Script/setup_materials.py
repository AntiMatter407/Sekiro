"""
Unified material setup for Sekiro project.
Supports two pipeline modes:
  type: "material"  — Create standalone UMaterial (weapon mode)
  type: "instance"  — Create/update UMaterialInstanceConstant (character mode)

Usage via bridge:
  bridge.py python --file <this_script> --args <config.json>
Or in UE5 Python console:
  import sys; sys.argv = ["setup_materials.py", "path/to/config.json"]
  exec(open(r"path/to/setup_materials.py", encoding="utf-8").read())
"""
import unreal
import json
import sys
import os

# ── MaterialProperty mapping ──────────────────────────────────────
_MP = {
    "BaseColor": unreal.MaterialProperty.MP_BASE_COLOR,
    "Metallic": unreal.MaterialProperty.MP_METALLIC,
    "Specular": unreal.MaterialProperty.MP_SPECULAR,
    "Roughness": unreal.MaterialProperty.MP_ROUGHNESS,
    "EmissiveColor": unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    "Opacity": unreal.MaterialProperty.MP_OPACITY,
    "OpacityMask": unreal.MaterialProperty.MP_OPACITY_MASK,
    "Normal": unreal.MaterialProperty.MP_NORMAL,
    "AmbientOcclusion": unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,
}


def _load_asset(path):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if not a:
        unreal.log_warning(f"Asset not found: {path}")
    return a


def _ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def _resolve_tex(config, tex_name):
    tex_dir = config["target"]["texture_dir"]
    path = f"{tex_dir}/{tex_name}"
    return _load_asset(path)


# ── Material mode ─────────────────────────────────────────────────

def _create_material_asset(mat_dir, mat_name):
    mat_path = f"{mat_dir}/{mat_name}"
    _ensure_dir(mat_dir)
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        unreal.EditorAssetLibrary.delete_asset(mat_path)
    at = unreal.AssetToolsHelpers.get_asset_tools()
    mat = at.create_asset(mat_name, mat_dir, None, unreal.MaterialFactoryNew())
    if not mat:
        unreal.log_error(f"Failed to create Material: {mat_name}")
        return None
    unreal.EditorAssetLibrary.save_asset(mat_path)
    unreal.log(f"Created Material: {mat_path}")
    return mat


def _add_texture_node(mat, tex, param_name, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    if node:
        node.set_editor_property("texture", tex)
        node.set_editor_property("parameter_name", param_name)
    return node


def _connect_binding(mat, node, binding):
    prop = _MP[binding["property"]]
    ch = binding["channel"]
    unreal.MaterialEditingLibrary.connect_material_property(node, ch, prop)


def _build_material(config, entry):
    mat_dir = config["target"]["material_dir"]
    mat = _create_material_asset(mat_dir, entry["name"])
    if not mat:
        return None

    textures = entry.get("textures", [])
    spacing = 300
    base_y = (len(textures) - 1) * spacing / 2

    for i, tex_entry in enumerate(textures):
        tex = _resolve_tex(config, tex_entry["texture"])
        if not tex:
            continue
        y_pos = int(base_y - i * spacing)
        node = _add_texture_node(mat, tex, tex_entry["param"], -800, y_pos)
        if not node:
            continue
        for binding in tex_entry.get("bindings", []):
            _connect_binding(mat, node, binding)

    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    mat_path = f"{mat_dir}/{entry['name']}"
    unreal.EditorAssetLibrary.save_asset(mat_path)
    unreal.log(f"Material configured: {entry['name']}")
    return mat


# ── Instance mode ─────────────────────────────────────────────────

def _create_or_update_instance(config, entry):
    mat_dir = config["target"]["material_dir"]
    tex_dir = config["target"]["texture_dir"]
    inst_path = f"{mat_dir}/{entry['name']}"
    parent_path = f"{mat_dir}/{entry['parent']}"

    parent = _load_asset(parent_path)
    if not parent:
        unreal.log_error(f"Parent material not found: {parent_path}")
        return None

    _ensure_dir(mat_dir)

    inst = unreal.EditorAssetLibrary.load_asset(inst_path)
    if not inst:
        at = unreal.AssetToolsHelpers.get_asset_tools()
        inst = at.create_asset(entry["name"], mat_dir, None,
                               unreal.MaterialInstanceConstantFactoryNew())
        if not inst:
            unreal.log_error(f"Failed to create MI: {entry['name']}")
            return None
        inst.set_editor_property("parent", parent)
        unreal.log(f"Created MaterialInstance: {inst_path}")
    else:
        unreal.log(f"Updating existing MaterialInstance: {inst_path}")

    for ov in entry.get("overrides", []):
        tex = _load_asset(f"{tex_dir}/{ov['texture']}")
        if tex:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                inst, ov["parameter"], tex)

    if "blend_mode" in entry:
        bm = {"Opaque": 0, "Masked": 1, "Translucent": 2, "Additive": 3,
              "Modulate": 4, "AlphaComposite": 5, "AlphaHoldout": 6}
        if entry["blend_mode"] in bm:
            unreal.MaterialEditingLibrary.set_material_instance_blend_mode_override(
                inst, bm[entry["blend_mode"]])

    if entry.get("two_sided", False):
        unreal.MaterialEditingLibrary.set_material_instance_two_sided_override(inst, True)

    unreal.EditorAssetLibrary.save_asset(inst_path)
    unreal.log(f"MaterialInstance configured: {entry['name']}")
    return inst


# ── Mesh assignment ───────────────────────────────────────────────

def _assign_to_mesh(config, materials):
    mesh_path = config["target"]["mesh"]
    mat_dir = config["target"]["material_dir"]

    mesh = _load_asset(mesh_path)
    if not mesh:
        unreal.log_error(f"Mesh not found: {mesh_path}")
        return

    cls_name = mesh.get_class().get_name()

    if cls_name == "SkeletalMesh":
        _assign_to_skeletal_mesh(mesh, config, mat_dir)
    elif cls_name == "StaticMesh":
        _assign_to_static_mesh(mesh, config, mat_dir)
    else:
        unreal.log_error(f"Unsupported mesh type: {cls_name} ({mesh_path})")


def _assign_to_skeletal_mesh(mesh, config, mat_dir):
    # UE5 Python: sk.materials returns a copy. Must create new SkeletalMaterial,
    # replace in array, set_editor_property back.
    mats = mesh.get_editor_property("materials")
    for entry in config["materials"]:
        slot = entry["slot"]
        mat = _load_asset(f"{mat_dir}/{entry['name']}")
        if not mat:
            continue
        if slot >= len(mats):
            unreal.log_warning(f"Slot [{slot}] out of range (max {len(mats)-1})")
            continue
        sm = unreal.SkeletalMaterial()
        sm.set_editor_property("material_interface", mat)
        mats[slot] = sm
        unreal.log(f"Slot [{slot}] <- {entry['name']}")

    mesh.set_editor_property("materials", mats)
    mesh.modify()
    unreal.EditorAssetLibrary.save_asset(config["target"]["mesh"])
    unreal.log("Mesh saved with material assignments")


def _assign_to_static_mesh(mesh, config, mat_dir):
    # StaticMesh: same copy-by-value issue as SkeletalMesh.
    # Try the array approach; fall back to set_material if it exists.
    prop_name = "static_materials" if hasattr(mesh, "static_materials") else "materials"
    try:
        cls = unreal.StaticMaterial
    except AttributeError:
        cls = unreal.SkeletalMaterial

    mats = mesh.get_editor_property(prop_name)
    for entry in config["materials"]:
        slot = entry["slot"]
        mat = _load_asset(f"{mat_dir}/{entry['name']}")
        if not mat:
            continue
        if slot >= len(mats):
            unreal.log_warning(f"Slot [{slot}] out of range (max {len(mats)-1})")
            continue
        sm = cls()
        sm.set_editor_property("material_interface", mat)
        mats[slot] = sm
        unreal.log(f"Slot [{slot}] <- {entry['name']}")

    mesh.set_editor_property(prop_name, mats)
    mesh.modify()
    unreal.EditorAssetLibrary.save_asset(config["target"]["mesh"])
    unreal.log("Mesh saved with material assignments")


# ── Entry ─────────────────────────────────────────────────────────

def run(config_path):
    if not os.path.isfile(config_path):
        unreal.log_error(f"Config file not found: {config_path}")
        return

    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    ptype = config.get("pipeline", {}).get("type", "")
    desc = config.get("pipeline", {}).get("description", "")

    if not desc:
        desc = ptype
    unreal.log(f"=== setup_materials: {desc} ===")

    results = []
    for entry in config.get("materials", []):
        if ptype == "material":
            mat = _build_material(config, entry)
        elif ptype == "instance":
            mat = _create_or_update_instance(config, entry)
        else:
            unreal.log_error(f"Unknown pipeline type: {ptype}. Must be 'material' or 'instance'.")
            return
        if mat:
            results.append(mat)

    if config.get("pipeline", {}).get("skip_mesh_assign", False):
        unreal.log("Skipping mesh assignment (skip_mesh_assign=true)")
    else:
        unreal.log(f"=== Assigning {len(results)} materials to mesh ===")
        _assign_to_mesh(config, results)
    unreal.log("=== setup_materials DONE ===")


def main():
    if len(sys.argv) < 2:
        unreal.log_error("Usage: setup_materials.py <config.json>")
        return
    run(sys.argv[1])


main()
