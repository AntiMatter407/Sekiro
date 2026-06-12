r"""
Sekiro character one-click import: textures + model FBX + material config

Usage (UE5 Output Log -> Python):
  import sys; sys.path.insert(0, r"D:/Sekiro/Script")
  import ue5_import_character

Result:
  /Game/Sekiro/{Character}/Materials/  <- all materials + textures
  /Game/Sekiro/{Character}/Models/     <- skeletal mesh + physics asset ONLY
"""

import unreal
import os
import json
import glob

# ============================================================
CHARACTER = "Wolf"
# ============================================================
MODEL_FBX = r"F:\ProjectAI\Sekiro\Extracted\Sekiro_Model.fbx"
MATERIAL_JSON = r"F:\ProjectAI\Sekiro\Extracted\Textures\Sekiro_Materials.json"
TEXTURE_SRC_DIR = r"F:\ProjectAI\Sekiro\Extracted\Textures"

BASE = f"/Game/Sekiro/{CHARACTER}"
PATH_MATERIALS = f"{BASE}/Materials"
PATH_MODELS = f"{BASE}/Models"


# ======================================================================
# Step 1 - Import textures with optimal compression
# ======================================================================

TEXTURE_PRESETS = {
    '_a':  {'srgb': True,  'compression': 'DEFAULT',    'sampler': 'COLOR'},
    '_n':  {'srgb': False, 'compression': 'NORMALMAP',  'sampler': 'NORMAL'},
    '_m':  {'srgb': False, 'compression': 'GRAYSCALE',  'sampler': 'GRAYSCALE'},
    '_r':  {'srgb': False, 'compression': 'GRAYSCALE',  'sampler': 'GRAYSCALE'},
    '_ao': {'srgb': False, 'compression': 'GRAYSCALE',  'sampler': 'GRAYSCALE'},
    '_1m': {'srgb': False, 'compression': 'GRAYSCALE',  'sampler': 'GRAYSCALE'},
    '_d':  {'srgb': False, 'compression': 'DEFAULT',    'sampler': 'LINEAR'},
    '_em': {'srgb': False, 'compression': 'DEFAULT',    'sampler': 'LINEAR'},
}

COMPRESSION_MAP = {
    'DEFAULT':    unreal.TextureCompressionSettings.TC_DEFAULT,
    'NORMALMAP':  unreal.TextureCompressionSettings.TC_NORMALMAP,
    'GRAYSCALE':  unreal.TextureCompressionSettings.TC_GRAYSCALE,
}

# Sampler type enum values (EMaterialSamplerType from C++)
# SAMPLERTYPE_Color=0, Grayscale=1, Alpha=2, Normal=3, Masks=4,
# DistanceFieldFont=5, LinearColor=6, LinearGrayscale=7, Data=8
SAMPLER_VALUES = {
    'COLOR':      0,
    'GRAYSCALE':  1,
    'NORMAL':     3,
    'LINEAR':     6,
    'GRAYSCALE_LINEAR': 7,
}


def _fix_material_samplers(mat):
    """Fix sampler types on all TextureSample nodes in a material via C++ plugin."""
    try:
        unreal.SekiroMaterialUtils.fix_texture_samplers_in_material(mat)
        return True
    except Exception:
        return False


def _detect_suffix(filename):
    stem = filename.rsplit('.', 1)[0].lower()
    for suffix in ('_ao', '_1m', '_d', '_em', '_a', '_n', '_m', '_r'):
        if stem.endswith(suffix):
            return suffix
    return '_a'


def import_textures_to(target_path):
    unreal.log(f"\n  Importing textures -> {target_path}")
    unreal.EditorAssetLibrary.make_directory(target_path)

    png_files = glob.glob(os.path.join(TEXTURE_SRC_DIR, "*.png"))
    imported, skipped = 0, 0

    for src in png_files:
        basename = os.path.basename(src)
        asset_name = unreal.Paths.get_base_filename(basename)
        dest_path = f"{target_path}/{asset_name}"

        if unreal.EditorAssetLibrary.does_asset_exist(dest_path):
            skipped += 1
            continue

        suffix = _detect_suffix(basename)
        preset = TEXTURE_PRESETS.get(suffix, TEXTURE_PRESETS['_a'])

        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", target_path)
        task.set_editor_property("destination_name", asset_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("options", unreal.TextureFactory())

        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

        tex = unreal.load_asset(dest_path)
        if tex:
            tex.set_editor_property("srgb", preset['srgb'])
            comp = COMPRESSION_MAP.get(preset['compression'], unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property("compression_settings", comp)
            imported += 1

    unreal.log(f"  Imported: {imported}, Skipped: {skipped}")


# ======================================================================
# Step 2 - Import FBX (only model, no animation)
# ======================================================================

def import_model_fbx():
    unreal.log(f"\n--- Step 2: Import Model FBX ---")
    unreal.log(f"  Source: {MODEL_FBX}")
    unreal.log(f"  Dest:   {PATH_MODELS}")
    unreal.EditorAssetLibrary.make_directory(PATH_MODELS)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", MODEL_FBX)
    task.set_editor_property("destination_path", PATH_MODELS)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("replace_existing", True)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property("skeleton", None)

    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = list(task.get_editor_property("imported_object_paths"))
    unreal.log(f"  Imported: {len(imported)} assets")
    return imported


# ======================================================================
# Step 3 - Clean Models/ — move materials+textures to Materials/, delete rest
# ======================================================================

def clean_models_folder():
    """只保留 SkeletalMesh + PhysicsAsset 在 Models/，其余移到 Materials/ 或删除"""
    unreal.log(f"\n--- Step 3: Clean {PATH_MODELS} ---")

    all_assets = unreal.EditorAssetLibrary.list_assets(PATH_MODELS, recursive=False)
    unreal.EditorAssetLibrary.make_directory(PATH_MATERIALS)

    moved, deleted = 0, 0
    KEEP_TYPES = (unreal.SkeletalMesh, unreal.PhysicsAsset, unreal.Skeleton)

    for asset_path in all_assets:
        asset = unreal.load_asset(asset_path)
        if asset is None:
            continue

        name = asset_path.rsplit('/', 1)[-1]

        if isinstance(asset, KEEP_TYPES):
            continue  # keep in Models/

        if isinstance(asset, (unreal.Material, unreal.MaterialInstance, unreal.Texture)):
            dest = f"{PATH_MATERIALS}/{name}"
            if not unreal.EditorAssetLibrary.does_asset_exist(dest):
                unreal.EditorAssetLibrary.rename_asset(asset_path, dest)
                moved += 1
                unreal.log(f"  Moved -> Materials/: {name}")
            else:
                unreal.EditorAssetLibrary.delete_asset(asset_path)
                deleted += 1
                unreal.log(f"  Deleted (dup): {name}")
        else:
            # Animation, Skeleton, etc. — delete
            unreal.EditorAssetLibrary.delete_asset(asset_path)
            deleted += 1
            unreal.log(f"  Deleted: {name}")

    unreal.log(f"  Moved: {moved}, Deleted: {deleted}")
    # Verify Models/ only has mesh+physics
    remaining = unreal.EditorAssetLibrary.list_assets(PATH_MODELS, recursive=False)
    unreal.log(f"  Models/ now has {len(remaining)} assets:")
    for a in remaining:
        unreal.log(f"    {a.rsplit('/', 1)[-1]}")


# ======================================================================
# Step 4 - Configure materials
# ======================================================================

def find_material_asset(mat_name):
    safe_name = unreal.Paths.make_valid_file_name(mat_name).lower()
    all_assets = unreal.EditorAssetLibrary.list_assets(PATH_MATERIALS, recursive=False)
    candidates = []
    for asset_path in all_assets:
        asset_name = asset_path.rsplit("/", 1)[-1].rsplit(".", 1)[-1].lower()
        mat = unreal.load_asset(asset_path)
        if mat and isinstance(mat, unreal.Material):
            if asset_name == safe_name:
                return mat
            if safe_name in asset_name or asset_name in safe_name:
                candidates.append(mat)
    return candidates[0] if candidates else None




# MTD BlendMode string → UE5 BlendMode mapping (set via SekiroMaterialUtils.SetBlendMode)
# Values: Opaque=0, Masked=1, Translucent=2, Additive=3, Modulate=4
MTD_BLEND_INT_MAP = {
    'TRANSLUCENT': 2,
    'MASKED': 1,
    'OPAQUE': 0,
    'ADDITIVE': 3,
    'MODULATE': 4,
}


def configure_material(mat, cfg):
    """Set blend_mode, two_sided via Python; Alpha→Opacity + sampler fix via C++."""
    mat_name = cfg.get("name", "?")

    blend_str = cfg["blend_mode"].upper()
    blend_int = MTD_BLEND_INT_MAP.get(blend_str, 0)

    try:
        unreal.SekiroMaterialUtils.set_blend_mode(mat, blend_int)
    except Exception:
        # Fallback to Python property
        if blend_str == "TRANSLUCENT":
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        elif blend_str == "MASKED":
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        elif blend_str == "ADDITIVE":
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        elif blend_str == "MODULATE":
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
        else:
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

    mat.set_editor_property("two_sided", cfg["two_sided"])
    unreal.log(f"  {mat_name}: blend={blend_str} two_sided={cfg['two_sided']}")

    # Force recompile so blend mode takes effect
    try:
        unreal.MaterialEditingLibrary.recompile_material(mat)
    except Exception:
        pass

    if blend_str == "TRANSLUCENT":
        try:
            mat.set_editor_property("lighting_mode",
                unreal.MaterialShadingModel.MSM_SURFACE_TRANSLUCENT_VOLUME)
        except Exception:
            pass

    # Use C++ plugin for operations Python can't do
    if blend_str == "TRANSLUCENT":
        try:
            unreal.SekiroMaterialUtils.connect_alpha_to_opacity(mat)
            unreal.log(f"  {mat_name}: A→Opacity wired")
        except Exception as e:
            unreal.log_warning(f"  {mat_name}: connect_alpha_to_opacity failed: {e}")

    _fix_material_samplers(mat)


# ======================================================================
# Main
# ======================================================================

def _delete_folder_contents(folder_path):
    """Delete all assets in a content browser folder."""
    if not unreal.EditorAssetLibrary.does_directory_exist(folder_path):
        return
    assets = unreal.EditorAssetLibrary.list_assets(folder_path, recursive=False)
    for asset_path in assets:
        try:
            unreal.EditorAssetLibrary.delete_asset(asset_path)
        except Exception:
            pass
    unreal.log(f"  Cleaned {folder_path}: {len(assets)} assets deleted")


def main():
    unreal.log("\n" + "=" * 60)
    unreal.log(f"  Sekiro Import: {CHARACTER}")
    unreal.log("=" * 60)

    # Step 0: 清理旧资产
    unreal.log("\n--- Step 0: Clean old assets ---")
    _delete_folder_contents(PATH_MODELS)
    _delete_folder_contents(PATH_MATERIALS)

    # Step 1: FBX → Models/ (import_textures=True, UE5 自动导入纹理+链接材质节点)
    unreal.log("\n--- Step 1: Import FBX (textures auto-imported) ---")
    import_model_fbx()

    # Step 2: 修正 FBX 导入的纹理设置（sRGB, compression）
    unreal.log(f"\n--- Step 2: Fix texture settings in {PATH_MODELS} ---")
    all_assets = unreal.EditorAssetLibrary.list_assets(PATH_MODELS, recursive=False)
    fixed_tex = 0
    for ap in all_assets:
        tex = unreal.load_asset(ap)
        if not isinstance(tex, unreal.Texture):
            continue
        name = ap.rsplit('/', 1)[-1].lower()
        suffix = _detect_suffix(name)
        preset = TEXTURE_PRESETS.get(suffix, TEXTURE_PRESETS['_a'])
        tex.set_editor_property("srgb", preset['srgb'])
        comp = COMPRESSION_MAP.get(preset['compression'], unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("compression_settings", comp)
        fixed_tex += 1
    unreal.log(f"  Fixed {fixed_tex} textures")

    # Step 3: 纹理+材质移到 Materials/，只留 Skeleton+SkeletalMesh+PhysicsAsset
    clean_models_folder()

    # Step 4: Configure materials
    unreal.log(f"\n--- Step 4: Configure Materials ---")
    if not os.path.exists(MATERIAL_JSON):
        unreal.log_error(f"Config not found: {MATERIAL_JSON}")
        return

    with open(MATERIAL_JSON, "r", encoding="utf-8") as f:
        config = json.load(f)

    materials_cfg = config.get("materials", [])
    configured, not_found = 0, []

    for cfg in materials_cfg:
        mat = find_material_asset(cfg["name"])
        if mat is None:
            not_found.append(cfg["name"])
            continue
        try:
            configure_material(mat, cfg)
            configured += 1
        except Exception as e:
            unreal.log_error(f"  FAILED {cfg['name']}: {e}")

    # Decal summary
    unreal.log("\n--- Decal / Translucent ---")
    for cfg in materials_cfg:
        if cfg["blend_mode"] == "Translucent":
            t = cfg.get("textures", {})
            unreal.log(f"  {cfg['name']}")
            unreal.log(f"    {t.get('_a','?')} -> A→Opacity")

    # Step 4b: Fix sampler types on ALL materials (including _ncl — now properly linked)
    unreal.log(f"\n--- Step 4b: Fix samplers on all materials in {PATH_MATERIALS} ---")
    all_mat_paths = unreal.EditorAssetLibrary.list_assets(PATH_MATERIALS, recursive=False)
    sampler_fixed = 0
    for ap in all_mat_paths:
        asset = unreal.load_asset(ap)
        if isinstance(asset, unreal.Material):
            if _fix_material_samplers(asset):
                sampler_fixed += 1
    unreal.log(f"  Sampler fix applied to {sampler_fixed} materials")

    unreal.log(f"\n=== Result ===")
    unreal.log(f"Configured: {configured} / {len(materials_cfg)}")
    if not_found:
        unreal.log_warning(f"Not found: {len(not_found)}")
        for n in not_found[:5]:
            unreal.log_warning(f"  {n}")

    # Clean up redirectors (UE5 auto-creates them when moving assets)
    unreal.log(f"\n--- Cleanup: Fix Redirectors ---")
    ar_filter = unreal.ARFilter(
        class_names=["ObjectRedirector"],
        package_paths=[BASE],
        recursive_paths=True,
    )
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    redirectors = registry.get_assets(ar_filter)
    redirector_paths = [str(r.package_name) for r in redirectors]
    if redirector_paths:
        unreal.log(f"  Fixing {len(redirector_paths)} redirectors...")
        try:
            unreal.EditorAssetSubsystem().fixup_referencers_of_redirectors(redirector_paths)
        except Exception:
            pass
    else:
        unreal.log(f"  No redirectors to fix")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    unreal.log(f"\nSaved. Content Browser: {BASE}")


if __name__ == "__main__":
    main()
