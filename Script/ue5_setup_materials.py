"""
UE5 材质配置：Blend Mode + Two Sided + 纹理节点连接（含 Alpha→Opacity）

用法（Output Log → Python 控制台）：
  import sys; sys.path.insert(0, r"D:/Sekiro/Script")
  import ue5_setup_materials
"""

import unreal
import os
import json

# ============================================================
CHARACTER = "Wolf"
MATERIAL_BASE_PATH = f"/Game/Sekiro/{CHARACTER}/Materials"
CONFIG_PATH = r"D:\Sekiro\Extracted\Textures\Sekiro_Materials.json"


def sanitize_ue_name(s):
    import re
    return re.sub(r"[/\\:*?\"<>|#]", "_", s)


def find_material_asset(mat_name):
    """按名称查找材质"""
    safe_name = sanitize_ue_name(mat_name).lower()
    all_assets = unreal.EditorAssetLibrary.list_assets(MATERIAL_BASE_PATH, recursive=False)
    candidates = []

    for asset_path in all_assets:
        asset_name = asset_path.rsplit("/", 1)[-1].rsplit(".", 1)[-1].lower()
        mat = unreal.load_asset(asset_path)
        if mat is None or not isinstance(mat, unreal.Material):
            continue
        if asset_name == safe_name:
            return mat
        if safe_name in asset_name or asset_name in safe_name:
            candidates.append(mat)

    return candidates[0] if candidates else None


def find_texture_in_material(mat, suffix):
    """按后缀查找材质中已有的 TextureSample 节点"""
    target = suffix.strip('_')
    for expr in mat.get_editor_property("expressions"):
        if isinstance(expr, unreal.MaterialExpressionTextureSample):
            desc = expr.get_editor_property("desc") or ""
            if target.lower() in desc.lower():
                return expr
    return None


def configure_material(mat, cfg):
    """配置 Blend Mode + 连接 Alpha→Opacity"""
    mat_lib = unreal.MaterialEditingLibrary

    blend_str = cfg["blend_mode"].upper()
    if blend_str == "TRANSLUCENT":
        blend_enum = unreal.BlendMode.BLEND_TRANSLUCENT
    elif blend_str == "MASKED":
        blend_enum = unreal.BlendMode.BLEND_MASKED
    else:
        blend_enum = unreal.BlendMode.BLEND_OPAQUE

    mat.set_editor_property("blend_mode", blend_enum)
    mat.set_editor_property("two_sided", cfg["two_sided"])

    if blend_enum == unreal.BlendMode.BLEND_TRANSLUCENT:
        try:
            mat.set_editor_property("lighting_mode",
                unreal.MaterialShadingModel.MSM_SURFACE_TRANSLUCENT_VOLUME)
        except Exception:
            pass

    # ---- 纹理节点连接 ----
    textures = cfg.get("textures", {})
    if not textures:
        return

    tex_files = list(textures.values())

    for expr in mat.get_editor_property("expressions"):
        if not isinstance(expr, unreal.MaterialExpressionTextureSample):
            continue

        tex = expr.get_editor_property("texture")
        if tex is None:
            continue
        tex_name = tex.get_name().lower()

        # 匹配纹理文件名
        matched_suffix = None
        for suffix, fname in textures.items():
            fname_no_ext = fname.rsplit(".", 1)[0].lower()
            if fname_no_ext == tex_name:
                matched_suffix = suffix
                break

        if matched_suffix is None:
            continue

        # 根据后缀连到对应材质输入
        if matched_suffix == '_a':
            # Albedo → Base Color
            try:
                mat_lib.connect_material_property(expr, "", unreal.MaterialProperty.MP_BASE_COLOR)
            except Exception:
                pass
            # Translucent: Alpha → Opacity
            if blend_enum == unreal.BlendMode.BLEND_TRANSLUCENT:
                try:
                    mat_lib.connect_material_property(expr, "A", unreal.MaterialProperty.MP_OPACITY)
                except Exception:
                    pass

        elif matched_suffix == '_n':
            try:
                mat_lib.connect_material_property(expr, "", unreal.MaterialProperty.MP_NORMAL)
            except Exception:
                pass

        elif matched_suffix == '_m':
            try:
                mat_lib.connect_material_property(expr, "", unreal.MaterialProperty.MP_METALLIC)
            except Exception:
                pass

        elif matched_suffix == '_r':
            try:
                mat_lib.connect_material_property(expr, "", unreal.MaterialProperty.MP_ROUGHNESS)
            except Exception:
                pass


def main():
    if not os.path.exists(CONFIG_PATH):
        unreal.log_error(f"Config not found: {CONFIG_PATH}")
        return

    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        config = json.load(f)

    materials_cfg = config.get("materials", [])
    configured = 0
    not_found = []

    unreal.log(f"\n=== Material Config: {CHARACTER} ===")
    unreal.log(f"Path: {MATERIAL_BASE_PATH}")

    for cfg in materials_cfg:
        mat_name = cfg["name"]
        mat = find_material_asset(mat_name)
        if mat is None:
            not_found.append(mat_name)
            continue
        try:
            configure_material(mat, cfg)
            configured += 1
        except Exception as e:
            unreal.log_error(f"  FAILED {mat_name}: {e}")

    # 列出 Decal 材质
    unreal.log("\n--- Decal / Translucent materials ---")
    for cfg in materials_cfg:
        if cfg["blend_mode"] == "Translucent":
            t = cfg.get("textures", {})
            unreal.log(f"  {cfg['name']}")
            unreal.log(f"    blend={cfg['blend_mode']} two_sided={cfg['two_sided']}")
            unreal.log(f"    tex={t.get('_a','?')}  (A→Opacity)")

    unreal.log(f"\n=== Result ===")
    unreal.log(f"Configured: {configured} / {len(materials_cfg)}")
    if not_found:
        unreal.log_warning(f"Not found: {len(not_found)}")
        for n in not_found[:5]:
            unreal.log_warning(f"  {n}")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    unreal.log("Saved.")


if __name__ == "__main__":
    main()
