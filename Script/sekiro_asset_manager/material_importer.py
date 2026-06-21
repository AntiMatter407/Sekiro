"""
MaterialImporter —— 只狼材质导入管线模块。

负责三个核心步骤的编排：

  1. 纹理导入（DDS → PNG → UE Texture2D）
  2. 材质创建（UMaterial + BlendMode/TwoSided + 纹理连接）
  3. 材质分配到 SkeletalMesh 材质槽（通过 setup_materials.py）

## 核心原则

**纹理匹配不再使用评分匹配，而是直接使用 FlverToJson 输出的 ResolvedMaterials 数据。**
从 Sekiro_model.json 的 ResolvedMaterials 中直接读取每个材质的纹理映射、BlendMode、TwoSided。

ResolvedMaterials 的数据结构（由新版 FlverToJson 输出）：

    {
      "Name": "BD_M_9000_tops1l",
      "Part": "BD_M_9000",
      "ResolvedTextures": {
        "albedo":    "BD_M_9000_tops_a.dds",
        "normal":    "BD_M_9000_tops_n.dds",
        "metallic":  "BD_M_9000_tops_m.dds",
        "roughness": "BD_M_9000_tops_r.dds"
      },
      "ResolvedBlendMode": "Opaque",
      "TwoSided": false,
      "DrawStep": "Opaque",
      "IsCloth": false,
      "IsHair": false,
      "IsFur": false,
      "IsDecal": false
    }

## 回退机制

对于旧格式 JSON（没有 ResolvedMaterials / ResolvedTextures），保留评分匹配回退逻辑。
评分匹配的代码来源于 common_blender.py 的简化版本。

## 依赖

- PipelineConfig（sekiro_asset_manager.pipeline_config）
- texconv.exe（Tools/texconv.exe）：DDS → PNG 转换
- bridge.py（.claude/skills/aibridge/bridge.py）：UE 编辑器交互
- setup_materials.py（Script/setup_materials.py）：UE 内材质创建 + 分配

用法示例：

    from sekiro_asset_manager.material_importer import MaterialImporter

    importer = MaterialImporter()
    result = importer.import_material(
        model_json="Extracted/Sekiro_model.json",
        target_ue_path="/Game/Sekiro/Characters/Sekiro",
        mesh_path="/Game/Sekiro/Characters/Sekiro/Mesh/SK_Sekiro",
    )
"""

import os
import json
import subprocess
import glob
import tempfile
import shutil
from typing import Optional

from sekiro_asset_manager.pipeline_config import PipelineConfig, config as default_config


# ======================================================================
# 语义键映射
# ======================================================================

# ResolvedTextures 语义键 → UE 材质属性 & 文件后缀
TEXTURE_SEMANTICS = {
    "albedo":       {"property": "BaseColor",       "suffix": "_a",       "non_color": False},
    "normal":       {"property": "Normal",          "suffix": "_n",       "non_color": True},
    "metallic":     {"property": "Metallic",        "suffix": "_m",       "non_color": True},
    "roughness":    {"property": "Roughness",       "suffix": "_r",       "non_color": True},
    "opacityMask":  {"property": "OpacityMask",     "suffix": "_mask",    "non_color": True},
    "emissive":     {"property": "EmissiveColor",   "suffix": "_em",      "non_color": False},
    "ao":           {"property": "AmbientOcclusion", "suffix": "_ao",     "non_color": True},
}

# BlendMode 字符串映射
BLEND_MODE_MAP = {
    "Opaque":       0,
    "Masked":       1,
    "Translucent":  2,
    "Additive":     3,
    "Modulate":     4,
    "AlphaComposite": 5,
    "AlphaHoldout":  6,
}

# ======================================================================
# 默认路径推导
# ======================================================================

def _default_extracted_dir() -> str:
    """返回 Extracted/ 目录。"""
    return default_config.extracted_dir


def _default_texture_dir(asset_name: str = "Textures") -> str:
    """返回纹理目录路径。

    Args:
        asset_name: 纹理种类名，默认 "Textures"。

    Returns:
        纹理目录完整路径。
    """
    return os.path.join(_default_extracted_dir(), asset_name)


def _default_model_json() -> str:
    """返回默认的模型 JSON 路径（Output/<prefix>/Model/）。"""
    return default_config.output_model_json("Sekiro")


def _default_ue_texture_path(target_ue_path: str) -> str:
    """根据 UE 目标路径推导纹理导入路径。

    Args:
        target_ue_path: 材质导入的 UE 根路径。

    Returns:
        纹理在 UE 中的导入目录。
    """
    return f"{target_ue_path}/Textures"


def _default_ue_material_dir(target_ue_path: str) -> str:
    """根据 UE 目标路径推导材质存放目录。

    Args:
        target_ue_path: 材质导入的 UE 根路径。

    Returns:
        材质在 UE 中的存放目录。
    """
    return f"{target_ue_path}/Materials"


# ======================================================================
# 材质导入器
# ======================================================================

class MaterialImporter:
    """材质导入管线编排器。

    封装完整材质导入流程：
      解析 ResolvedMaterials → DDS→PNG 转换 → UE 纹理导入 → 材质创建 → 材质分配。

    每个步骤可单独调用，也可通过 import_material() 一键执行。
    """

    def __init__(self, config: Optional[PipelineConfig] = None):
        """初始化导入器。

        Args:
            config: PipelineConfig 实例。为 None 时使用全局默认配置。
        """
        self.config = config or default_config

    # ------------------------------------------------------------------
    # 完整管线
    # ------------------------------------------------------------------

    def import_material(
        self,
        model_json: str = "",
        target_ue_path: str = "",
        mesh_path: str = "",
        texture_dir: Optional[str] = None,
        texture_ue_path: Optional[str] = None,
        material_ue_dir: Optional[str] = None,
        skip_texconv: bool = False,
        skip_ue_texture_import: bool = False,
        skip_material_create: bool = False,
        skip_mesh_assign: bool = False,
        material_mode: str = "instance",
    ) -> dict:
        """执行完整材质导入管线。

        管线步骤：
          1. 解析 ResolvedMaterials（或回退评分匹配）
          2. [可选] DDS → PNG 转换
          3. [可选] PNG 导入 UE 为 Texture2D
          4. 生成 Sekiro_Materials.json 配置 → 调用 setup_materials.py

        Args:
            model_json: Sekiro_model.json 路径。
            target_ue_path: UE 中的目标根路径。
            mesh_path: UE 中 SkeletalMesh 的路径（用于材质分配）。
            texture_dir: 本地纹理目录（存放 DDS/PNG）。
            texture_ue_path: UE 中纹理的导入路径。
            material_ue_dir: UE 中材质的存放路径。
            skip_texconv: 跳过 DDS→PNG 转换（已在 PNG 时使用）。
            skip_ue_texture_import: 跳过 UE 纹理导入。
            skip_material_create: 跳过材质创建。
            skip_mesh_assign: 跳过材质分配到网格槽。
            material_mode: "material"（创建 UMaterial）或 "instance"（创建 MI）。
                          默认 "instance"。

        Returns:
            dict: {
                "success": bool,
                "steps": {步骤名: bool},
                "error": str,           # 仅失败时
                "model_json": str,
                "target_ue_path": str,
                "material_count": int,
                "config_path": str,     # 输出的 Sekiro_Materials.json 路径
            }
        """
        result = {
            "success": False,
            "steps": {},
            "model_json": model_json,
            "target_ue_path": target_ue_path,
            "material_count": 0,
            "config_path": "",
        }

        # ── 推导默认路径 ──────────────────────────────────────────────
        if not model_json:
            model_json = _default_model_json()
        if not target_ue_path:
            target_ue_path = "/Game/Sekiro"
        if not texture_dir:
            texture_dir = _default_texture_dir()
        if not texture_ue_path:
            texture_ue_path = _default_ue_texture_path(target_ue_path)
        if not material_ue_dir:
            material_ue_dir = _default_ue_material_dir(target_ue_path)

        # 验证模型 JSON
        if not os.path.exists(model_json):
            result["error"] = f"模型 JSON 未找到: {model_json}"
            return result

        # ── 步骤 0：解析材质数据 ──────────────────────────────────────
        try:
            materials_data, resolved_materials = self.get_resolved_materials(model_json)
        except Exception as e:
            result["error"] = f"解析材质数据失败: {e}"
            return result

        if not resolved_materials:
            # 尝试从 materials_data 构建 resolved（回退逻辑在内部处理）
            resolved_materials = self._build_resolved_from_legacy(
                materials_data, texture_dir
            )

        if not resolved_materials:
            result["error"] = "未找到任何材质数据，请确认 JSON 格式正确"
            return result

        result["material_count"] = len(resolved_materials)
        print(f"解析到 {len(resolved_materials)} 个材质")

        # ── 步骤 1：收集所有需要导入的纹理 ────────────────────────────
        all_textures = []  # [(纹理文件名, 语义键, 材质索引)]
        for mi, rm in enumerate(resolved_materials):
            resolved_tex = rm.get("ResolvedTextures", {}) or rm.get("textures", {})
            for semantic_key, filename in resolved_tex.items():
                if filename:
                    all_textures.append((filename, semantic_key, mi))

        if not all_textures:
            print("  [警告] 所有材质都没有关联纹理，跳过纹理步骤")
        else:
            # 去重纹理文件
            unique_texture_files = list(dict.fromkeys(t[0] for t in all_textures))
            print(f"需要处理 {len(unique_texture_files)} 个唯一纹理文件")

            # ── 步骤 2：DDS → PNG ──────────────────────────────────────
            if not skip_texconv:
                step_name = "texconv"
                print(f"\n{'='*60}")
                print(f"步骤 1：DDS → PNG 转换")
                print(f"{'='*60}")
                try:
                    dds_files = [
                        os.path.join(texture_dir, f)
                        for f in unique_texture_files
                        if f.lower().endswith(".dds")
                    ]
                    existing_dds = [f for f in dds_files if os.path.exists(f)]
                    if existing_dds:
                        ok = self.run_texconv(existing_dds, texture_dir)
                        result["steps"][step_name] = ok
                        if not ok:
                            result["error"] = "DDS→PNG 转换失败"
                            return result
                    else:
                        print("  没有需要转换的 DDS 文件，跳过")
                        result["steps"][step_name] = True
                except Exception as e:
                    result["steps"][step_name] = False
                    result["error"] = f"texconv 异常: {e}"
                    return result

            # ── 步骤 3：UE 纹理导入 ────────────────────────────────────
            if not skip_ue_texture_import:
                step_name = "ue_texture_import"
                print(f"\n{'='*60}")
                print(f"步骤 2：纹理导入 UE")
                print(f"{'='*60}")
                try:
                    ok = self.import_textures_to_ue(
                        texture_dir=texture_dir,
                        target_ue_path=texture_ue_path,
                        texture_names=unique_texture_files,
                    )
                    result["steps"][step_name] = ok
                    if not ok:
                        result["error"] = "UE 纹理导入失败"
                        return result
                except Exception as e:
                    result["steps"][step_name] = False
                    result["error"] = f"纹理导入异常: {e}"
                    return result

            # ── 步骤 4：材质创建 ────────────────────────────────────────
            if not skip_material_create:
                step_name = "material_create"
                print(f"\n{'='*60}")
                print(f"步骤 3：UE 材质创建")
                print(f"{'='*60}")
                try:
                    ok = self.create_materials_in_ue(
                        resolved_materials=resolved_materials,
                        target_ue_path=target_ue_path,
                        texture_ue_path=texture_ue_path,
                        material_ue_dir=material_ue_dir,
                        material_mode=material_mode,
                        mesh_path=mesh_path if not skip_mesh_assign else "",
                    )
                    result["steps"][step_name] = ok
                    if not ok:
                        result["error"] = "UE 材质创建失败"
                        return result
                except Exception as e:
                    result["steps"][step_name] = False
                    result["error"] = f"材质创建异常: {e}"
                    return result

        # ── 完成 ───────────────────────────────────────────────────────
        result["success"] = True
        print(f"\n{'='*60}")
        print(f"材质导入完成")
        print(f"  材质数:    {result['material_count']}")
        print(f"  UE 路径:   {target_ue_path}")
        print(f"{'='*60}")
        return result

    # ------------------------------------------------------------------
    # ResolvedMaterials 解析
    # ------------------------------------------------------------------

    def get_resolved_materials(self, model_json: str) -> tuple[list[dict], list[dict]]:
        """从 Sekiro_model.json 提取材质数据。

        解析 Sekiro_model.json 中的 "Materials" 列表，
        返回 (materials, resolved_materials) 两套数据：

          - materials：原始 JSON 中的完整材质数据（含 Textures/MTDInfo 等）
          - resolved_materials：包含 ResolvedTextures 扁平映射的材质列表

        ResolvedTextures 语义键 → 文件后缀映射：
          - albedo      → _a  → BaseColor
          - normal      → _n  → Normal
          - metallic    → _m  → Metallic
          - roughness   → _r  → Roughness
          - opacityMask → _mask → Opacity Mask
          - emissive    → _em → Emissive Color
          - ao          → _ao → Ambient Occlusion

        Args:
            model_json: Sekiro_model.json 完整路径。

        Returns:
            tuple[list[dict], list[dict]]:
                (materials_data, resolved_materials_list)
        """
        if not os.path.exists(model_json):
            raise FileNotFoundError(f"模型 JSON 未找到: {model_json}")

        with open(model_json, "r", encoding="utf-8") as f:
            data = json.load(f)

        materials_data = data.get("Materials", [])
        if not materials_data:
            return [], []

        resolved_materials = []
        for mat in materials_data:
            resolved = {
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),
                "ResolvedBlendMode": mat.get("ResolvedBlendMode", "Opaque"),
                "TwoSided": mat.get("TwoSided", False),
                "DrawStep": mat.get("DrawStep", "Opaque"),
                "IsCloth": mat.get("IsCloth", False),
                "IsHair": mat.get("IsHair", False),
                "IsFur": mat.get("IsFur", False),
                "IsDecal": mat.get("IsDecal", False),
            }

            # 尝试提取 ResolvedTextures（新版 FlverToJson 格式）
            resolved_tex = mat.get("ResolvedTextures", None)
            if resolved_tex and isinstance(resolved_tex, dict):
                resolved["ResolvedTextures"] = resolved_tex
            else:
                # 尝试从 Textures[] + AvailableTextures 推断
                resolved["ResolvedTextures"] = self._resolve_textures_from_legacy(
                    mat, materials_data
                )

            resolved_materials.append(resolved)

        return materials_data, resolved_materials

    def _resolve_textures_from_legacy(
        self, mat: dict, all_materials: list[dict]
    ) -> dict:
        """从旧格式材质数据推断 ResolvedTextures。

        旧格式的 "Textures" 是 {ParamName, Path} 列表，"AvailableTextures" 是文件名列表。
        通过 ParamName 关键词匹配语义后缀。

        Args:
            mat: 单个材质数据 dict。
            all_materials: 完整材质列表（未使用，保留接口一致性）。

        Returns:
            dict: 语义键 → 纹理文件名的映射。
        """
        resolved = {}
        textures_list = mat.get("Textures", [])
        available = mat.get("AvailableTextures", [])
        part = mat.get("Part", "").lower()

        if not textures_list and not available:
            return resolved

        # 从 common_blender.py 的 PARAM_NAME_SEMANTIC_MAP 提取关键词映射
        keyword_to_semantic = [
            ("Albedo",     "albedo"),
            ("Diffuse",    "albedo"),
            ("BaseColor",  "albedo"),
            ("NormalMap",  "normal"),
            ("BumpMap",    "normal"),
            ("DetailBump", "normal"),
            ("Metallic",   "metallic"),
            ("Reflectance","metallic"),
            ("Specular",   "metallic"),
            ("Roughness",  "roughness"),
            ("Shininess",  "roughness"),
            ("AmbientOcclusion", "ao"),
            ("Displacement","displacement"),
            ("Emissive",   "emissive"),
        ]

        # Pass 1：通过 ParamName 关键词匹配
        found_semantics = set()
        for tex_entry in textures_list:
            if not isinstance(tex_entry, dict):
                continue
            param_name = tex_entry.get("ParamName", "")
            if not param_name:
                continue

            for keyword, semantic in keyword_to_semantic:
                if keyword.lower() in param_name.lower():
                    # 从 AvailableTextures 中找到对应文件
                    matched_file = self._find_texture_by_semantic(
                        available, part, semantic
                    )
                    if matched_file:
                        resolved[semantic] = matched_file
                        found_semantics.add(semantic)
                    break

        # Pass 2：对未匹配的语义，用 part 前缀 + 后缀补全
        for semantic, info in TEXTURE_SEMANTICS.items():
            if semantic in found_semantics:
                continue
            suffix = info["suffix"]
            matched = self._find_texture_by_suffix(available, part, suffix)
            if matched:
                resolved[semantic] = matched

        return resolved

    def _find_texture_by_semantic(
        self, available: list[str], part: str, semantic: str
    ) -> str:
        """从 AvailableTextures 列表中按语义查找纹理文件。

        Args:
            available: 可用纹理文件名列表。
            part: 部件名（小写）。
            semantic: 语义键（如 "albedo"、"normal"）。

        Returns:
            匹配的纹理文件名，未找到返回空字符串。
        """
        suffix = TEXTURE_SEMANTICS.get(semantic, {}).get("suffix", "")
        if not suffix:
            return ""
        return self._find_texture_by_suffix(available, part, suffix)

    def _find_texture_by_suffix(
        self, available: list[str], part: str, suffix: str
    ) -> str:
        """从 AvailableTextures 中按后缀查找最佳匹配纹理文件。

        优先匹配 part 前缀 + 后缀的纹理，其次是任意后缀匹配。

        Args:
            available: 可用纹理文件名列表。
            part: 部件名（小写）。
            suffix: 文件后缀（如 "_a"、"_n"）。

        Returns:
            匹配的纹理文件名，未找到返回空字符串。
        """
        if not available:
            return ""

        # 优先匹配 part 前缀 + 后缀
        part_prefix = part + "_" if part else ""
        for fname in available:
            stem = os.path.splitext(fname)[0].lower()
            if stem.startswith(part_prefix) and stem.endswith(suffix):
                return fname

        # 回退：任意文件匹配后缀
        for fname in available:
            stem = os.path.splitext(fname)[0].lower()
            if stem.endswith(suffix):
                return fname

        return ""

    # ------------------------------------------------------------------
    # 旧格式回退：评分匹配
    # ------------------------------------------------------------------

    def _build_resolved_from_legacy(
        self, materials_data: list[dict], texture_dir: str
    ) -> list[dict]:
        """当 ResolvedMaterials / ResolvedTextures 均不存在时，
        使用评分匹配回退逻辑为材质分配纹理。

        这是 common_blender.py 中 assign_textures_globally() 的简化 Python 版本。
        仅用于旧格式 JSON（如 wp_a_0300_model.json 没有 ResolvedMaterials）。
        使用时会记录警告，提示应重新用新版 FlverToJson 生成。

        Args:
            materials_data: JSON 中的 "Materials" 列表。
            texture_dir: 纹理目录，用于扫描 DDS/PNG 文件。

        Returns:
            list[dict]: resolved_materials 格式的列表，每项包含
                        Name, Part, ResolvedBlendMode, TwoSided, textures 等。
        """
        print("  [警告] 使用评分匹配回退逻辑（旧格式 JSON）")
        print("  [建议] 重新用新版 FlverToJson 生成以获取准确匹配")

        # 扫描纹理缓存
        dds_cache = self._build_texture_cache(texture_dir)
        if not dds_cache:
            print("  [错误] 未找到任何纹理文件")
            return []

        # 为每个材质做评分匹配
        resolved_list = []
        for mat in materials_data:
            resolved = {
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),
                "ResolvedBlendMode": mat.get("ResolvedBlendMode", "Opaque"),
                "TwoSided": mat.get("TwoSided", False),
                "DrawStep": mat.get("DrawStep", "Opaque"),
                "IsCloth": mat.get("IsCloth", False),
                "IsHair": mat.get("IsHair", False),
                "IsFur": mat.get("IsFur", False),
                "IsDecal": mat.get("IsDecal", False),
                "textures": {},  # 评分匹配结果放在这里
            }

            # 评分匹配
            scored = self._score_textures_for_material(mat, dds_cache)
            # 转换语义
            for src_suffix, tex_path in scored.items():
                semantic = self._suffix_to_semantic(src_suffix)
                if semantic:
                    resolved["textures"][semantic] = os.path.basename(tex_path)

            resolved_list.append(resolved)

        return resolved_list

    def _build_texture_cache(self, texture_root: str) -> dict[str, str]:
        """构建 stem → 完整路径 的纹理缓存。

        优先使用 PNG（与 UE 兼容），DDS 作为回退。

        Args:
            texture_root: 纹理根目录。

        Returns:
            dict: stem（小写无后缀文件名）→ 完整路径。
        """
        texture_root = os.path.abspath(texture_root)
        cache = {}
        if not os.path.isdir(texture_root):
            return cache

        for root, _dirs, files in os.walk(texture_root):
            for f in files:
                low = f.lower()
                if not (low.endswith(".png") or low.endswith(".dds")):
                    continue
                stem = os.path.splitext(f)[0].lower()
                path = os.path.normpath(os.path.join(root, f))
                # PNG 优先覆盖 DDS
                if stem not in cache or low.endswith(".png"):
                    cache[stem] = path
        return cache

    def _strip_texture_suffix(self, stem: str) -> tuple[str, str]:
        """从纹理文件 stem 中剥离语义后缀。

        Args:
            stem: 文件名（无扩展名，小写）。

        Returns:
            tuple: (core_name, suffix)，如 ("bd_m_9000_tops", "_a")。
        """
        for suffix in ("_a", "_n", "_m", "_r", "_mask", "_em", "_ao"):
            if stem.endswith(suffix):
                return stem[: -len(suffix)], suffix
        return stem, ""

    def _suffix_to_semantic(self, suffix: str) -> str:
        """文件后缀 → 语义键映射。

        Args:
            suffix: 文件后缀（如 "_a"）。

        Returns:
            语义键（如 "albedo"），未匹配返回空字符串。
        """
        reverse_map = {info["suffix"]: key for key, info in TEXTURE_SEMANTICS.items()}
        return reverse_map.get(suffix, "")

    def _score_textures_for_material(
        self, mat_data: dict, dds_cache: dict[str, str]
    ) -> dict[str, str]:
        """对单个材质进行评分匹配，返回后缀 → 纹理路径的映射。

        简化版评分逻辑（来源于 common_blender.py 的 score_texture_candidate）：

          1. 优先匹配 part 前缀 + 语义后缀
          2. 按名称 token 重叠评分
          3. 每材质每后缀只匹配一个最佳纹理

        Args:
            mat_data: 材质数据 dict（含 Name、Part）。
            dds_cache: stem → 路径 的纹理缓存。

        Returns:
            dict: 后缀（如 "_a"）→ 纹理路径。
        """
        part = mat_data.get("Part", "").lower()
        mat_name = mat_data.get("Name", "").lower()
        mtd = os.path.splitext(
            os.path.basename(mat_data.get("MTD", "").replace("\\", "/"))
        )[0].lower()

        suffixes = ("_a", "_n", "_m", "_r")
        result = {}
        candidates = []  # (score, suffix, path)

        for stem, path in dds_cache.items():
            core, suffix = self._strip_texture_suffix(stem)
            if suffix not in suffixes:
                continue

            # 部件前缀过滤
            if part and not stem.startswith(part):
                continue

            score = 0

            # 名称核心匹配
            part_prefix = part + "_" if part else ""
            tex_core = core[len(part_prefix):] if core.startswith(part_prefix) else core
            mat_core = mat_name[len(part_prefix):] if mat_name.startswith(part_prefix) else mat_name

            haystack = f"{mat_core} {mtd}"
            if tex_core and tex_core in haystack:
                score += 100

            # Token 重叠
            ignored = {"p", "fb", "m", "e", "a", "cloth", "material", "new", "9000", "9510", "00"}
            tex_tokens = [
                t for t in tex_core.replace("-", "_").split("_")
                if len(t) >= 2 and t not in ignored
            ]
            for token in tex_tokens:
                if token in haystack:
                    score += 20
                else:
                    for ht in haystack.replace("-", "_").split("_"):
                        if len(token) >= 3 and len(ht) >= 3:
                            if token.startswith(ht) or ht.startswith(token):
                                score += 10
                                break

            if score > 0:
                candidates.append((score, suffix, path))

        # 排序取最佳
        candidates.sort(key=lambda x: -x[0])
        used_suffixes = set()
        for score, suffix, path in candidates:
            if suffix not in used_suffixes:
                result[suffix] = path
                used_suffixes.add(suffix)

        return result

    # ------------------------------------------------------------------
    # DDS → PNG 转换（texconv）
    # ------------------------------------------------------------------

    def run_texconv(
        self,
        dds_files: list[str],
        output_dir: str,
        overwrite: bool = False,
    ) -> bool:
        """调用 texconv.exe 将 DDS 文件转换为 PNG。

        命令格式：
          texconv.exe -f R8G8B8A8_UNORM -ft png [-y] -o <output_dir> <dds_file1> <dds_file2> ...

        Args:
            dds_files: DDS 文件路径列表。
            output_dir: PNG 输出目录。
            overwrite: 是否覆盖已存在的 PNG 文件。

        Returns:
            bool: 是否全部转换成功。
        """
        texconv = self.config.tool_path("texconv")
        if not texconv or not os.path.exists(texconv):
            print(f"  [错误] texconv.exe 未找到: {texconv}")
            return False

        # 检查 DDS 对应的 PNG 是否已存在
        pending = []
        for dds in dds_files:
            png = dds[:-4] + ".png"
            if overwrite or not os.path.exists(png):
                pending.append(dds)

        if not pending:
            print(f"  所有 PNG 已存在，跳过转换（{len(dds_files)} 个）")
            return True

        os.makedirs(output_dir, exist_ok=True)

        cmd = [
            texconv,
            "-f", "R8G8B8A8_UNORM",
            "-ft", "png",
            "-o", output_dir,
        ]
        if overwrite:
            cmd.append("-y")

        # texconv 支持传多个文件
        cmd.extend(pending)

        print(f"  转换 {len(pending)} 个 DDS → PNG...")
        try:
            result = subprocess.run(
                cmd,
                capture_output=True, text=True, timeout=300,
            )
        except subprocess.TimeoutExpired:
            print(f"  [错误] texconv 超时（5分钟）")
            return False
        except FileNotFoundError:
            print(f"  [错误] 无法执行 texconv.exe")
            return False

        if result.returncode != 0:
            print(f"  [错误] texconv 返回码 {result.returncode}")
            if result.stderr:
                print(f"  stderr: {result.stderr[:1000]}")
            return False

        # 验证输出
        converted = 0
        for dds in pending:
            png = os.path.join(output_dir, os.path.basename(dds)[:-4] + ".png")
            if os.path.exists(png):
                converted += 1

        print(f"  转换成功: {converted}/{len(pending)} 个文件")
        return converted == len(pending)

    # ------------------------------------------------------------------
    # UE 纹理导入（通过 bridge.py）
    # ------------------------------------------------------------------

    def import_textures_to_ue(
        self,
        texture_dir: str,
        target_ue_path: str,
        texture_names: Optional[list[str]] = None,
    ) -> bool:
        """批量导入纹理到 UE。

        通过 bridge.py asset import_file 将 PNG 纹理导入为 UE Texture2D。

        Args:
            texture_dir: 本地纹理目录。
            target_ue_path: UE 中的纹理导入路径（如 "/Game/Sekiro/Textures"）。
            texture_names: 要导入的纹理文件名列表。
                           为 None 时扫描 texture_dir 下所有 PNG。

        Returns:
            bool: 是否所有纹理导入成功。
        """
        # 收集 PNG 文件
        if texture_names:
            png_files = []
            for fname in texture_names:
                if fname.lower().endswith(".png"):
                    png_files.append(fname)
                elif fname.lower().endswith(".dds"):
                    # 尝试对应的 PNG
                    png_name = fname[:-4] + ".png"
                    if os.path.exists(os.path.join(texture_dir, png_name)):
                        png_files.append(png_name)
        else:
            # 扫描目录下所有 PNG
            png_files = sorted([
                f for f in os.listdir(texture_dir)
                if f.lower().endswith(".png")
            ])

        if not png_files:
            print("  没有找到 PNG 文件需要导入")
            return True

        bridge_py = self.config.get_bridge_py()
        ue_python = self.config.get_python()
        if not os.path.exists(ue_python):
            print(f"  [错误] UE Python 解释器未找到: {ue_python}")
            return False

        # 逐个导入纹理
        success_count = 0
        for png_file in png_files:
            asset_name = os.path.splitext(png_file)[0]
            ue_asset_path = f"{target_ue_path}/{asset_name}"
            local_path = os.path.join(texture_dir, png_file)

            if not os.path.exists(local_path):
                print(f"  [警告] 纹理文件不存在: {local_path}")
                continue

            # 构造 bridge.py 命令
            cmd = [
                ue_python, bridge_py,
                "asset", "import_file",
                ue_asset_path, local_path,
            ]

            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True, text=True, timeout=60,
                )
                if result.returncode == 0:
                    success_count += 1
                    # 每 20 个打印一次进度
                    if success_count % 20 == 0:
                        print(f"  纹理导入进度: {success_count}/{len(png_files)}")
                else:
                    print(f"  [警告] 纹理导入失败: {png_file}")
                    if result.stderr:
                        print(f"    {result.stderr.strip()[:200]}")
            except subprocess.TimeoutExpired:
                print(f"  [警告] 纹理导入超时: {png_file}")
            except Exception as e:
                print(f"  [警告] 纹理导入异常: {png_file}: {e}")

        all_success = success_count == len(png_files)
        print(f"  纹理导入: {success_count}/{len(png_files)} 成功"
              f"{'（全部）' if all_success else '（部分失败）'}")
        return all_success

    # ------------------------------------------------------------------
    # UE 材质创建（通过 setup_materials.py）
    # ------------------------------------------------------------------

    def create_materials_in_ue(
        self,
        resolved_materials: list[dict],
        target_ue_path: str,
        texture_ue_path: str,
        material_ue_dir: str,
        material_mode: str = "instance",
        mesh_path: str = "",
    ) -> bool:
        """在 UE 中创建材质并分配纹理。

        工作流程：
          1. 从 resolved_materials 构建 Sekiro_Materials.json 配置
          2. 将配置写入 UE 可访问的临时路径
          3. 通过 bridge.py python --file 调用 setup_materials.py 执行创建

        Args:
            resolved_materials: 解析后的材质数据列表。
            target_ue_path: UE 目标根路径（未使用，保留接口）。
            texture_ue_path: UE 纹理导入路径。
            material_ue_dir: UE 材质存放路径。
            material_mode: "material"（创建独立 UMaterial）或 "instance"（创建 MI）。
            mesh_path: UE 中 SkeletalMesh 路径。非空时执行材质分配。

        Returns:
            bool: 是否全部创建成功。
        """
        # 构建 setup_materials.py 可识别的配置
        entries = []
        for rm in resolved_materials:
            mat_name = rm.get("Name", "")
            if not mat_name:
                continue

            resolved_tex = rm.get("ResolvedTextures", {}) or rm.get("textures", {})
            blend_mode = rm.get("ResolvedBlendMode", "Opaque")
            two_sided = rm.get("TwoSided", False)

            if material_mode == "material":
                # UMaterial 模式：构建纹理绑定列表
                textures_bindings = []
                for semantic_key, filename in resolved_tex.items():
                    if not filename:
                        continue
                    semantic_info = TEXTURE_SEMANTICS.get(semantic_key)
                    if not semantic_info:
                        continue
                    tex_name = os.path.splitext(filename)[0]
                    tex_path = f"{texture_ue_path}/{tex_name}"
                    textures_bindings.append({
                        "texture": tex_name,
                        "param": f"Param_{semantic_info['property']}",
                        "bindings": [
                            {
                                "property": semantic_info["property"],
                                "channel": "A" if semantic_key == "opacityMask" else "",
                            }
                        ],
                    })

                entry = {
                    "name": mat_name,
                    "textures": textures_bindings,
                }
                entries.append(entry)

            else:
                # MaterialInstance 模式：构建 texture parameter overrides
                overrides = []
                for semantic_key, filename in resolved_tex.items():
                    if not filename:
                        continue
                    tex_name = os.path.splitext(filename)[0]
                    overrides.append({
                        "parameter": tex_name,
                        "texture": f"{tex_name}.{tex_name}",
                        # 实际参数名需要匹配材质父级中的参数名
                    })

                entry = {
                    "name": mat_name,
                    "parent": "M_Sekiro_Base",
                    "overrides": overrides,
                    "blend_mode": blend_mode,
                    "two_sided": two_sided,
                }
                entries.append(entry)

        # 构建完整配置
        config = {
            "pipeline": {
                "type": material_mode,
                "description": f"MaterialImporter 自动生成 ({len(entries)} 个材质)",
            },
            "target": {
                "material_dir": material_ue_dir,
                "texture_dir": texture_ue_path,
                "mesh": mesh_path,
                "skip_mesh_assign": not bool(mesh_path),
            },
            "materials": entries,
        }

        # 写入临时配置文件
        config_dir = os.path.join(
            self.config.project_dir, "Script", "temp"
        )
        os.makedirs(config_dir, exist_ok=True)
        config_path = os.path.join(config_dir, "Sekiro_Materials_auto.json")
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)

        print(f"  Material config 写入: {config_path}")
        print(f"  材质数: {len(entries)}")

        # 调用 setup_materials.py
        bridge_py = self.config.get_bridge_py()
        ue_python = self.config.get_python()
        setup_script = os.path.join(
            self.config.scripts_dir, "setup_materials.py"
        )
        if not os.path.exists(setup_script):
            # 回退：相对 project_dir
            setup_script = os.path.join(
                self.config.project_dir, "Script", "setup_materials.py"
            )
        if not os.path.exists(setup_script):
            print(f"  [错误] setup_materials.py 未找到: {setup_script}")
            return False

        if not os.path.exists(ue_python):
            print(f"  [错误] UE Python 解释器未找到: {ue_python}")
            return False

        cmd = [
            ue_python, bridge_py,
            "python", "--file", setup_script,
            "--args", config_path,
        ]

        print(f"  执行材质创建脚本...")
        try:
            result = subprocess.run(
                cmd,
                capture_output=True, text=True, timeout=300,
            )
        except subprocess.TimeoutExpired:
            print(f"  [错误] 材质创建超时（5分钟）")
            return False
        except Exception as e:
            print(f"  [错误] 材质创建异常: {e}")
            return False

        if result.returncode != 0:
            print(f"  [错误] 材质创建返回码 {result.returncode}")
            if result.stderr:
                for line in result.stderr.strip().split("\n")[-15:]:
                    print(f"  {line}")
            return False

        # 输出结果
        if result.stdout:
            lines = result.stdout.strip().split("\n")
            for line in lines[-10:]:
                print(f"  {line}")

        print(f"  材质创建/更新完成: {len(entries)} 个材质")
        return True

    # ------------------------------------------------------------------
    # 工具方法
    # ------------------------------------------------------------------

    def get_texture_summary(self, model_json: str) -> dict:
        """读取模型 JSON 并返回纹理映射摘要。

        Args:
            model_json: 模型 JSON 路径。

        Returns:
            dict: {
                "exists": bool,
                "material_count": int,
                "total_unique_textures": int,
                "textures_by_semantic": dict[str, list[str]],
                "has_resolved_materials": bool,
            }
        """
        summary = {
            "exists": False,
            "material_count": 0,
            "total_unique_textures": 0,
            "textures_by_semantic": {},
            "has_resolved_materials": False,
        }

        if not os.path.exists(model_json):
            return summary

        try:
            with open(model_json, "r", encoding="utf-8") as f:
                data = json.load(f)

            materials = data.get("Materials", [])
            summary["exists"] = True
            summary["material_count"] = len(materials)

            # 检查是否有 ResolvedMaterials
            has_resolved = any(
                "ResolvedTextures" in m or "ResolvedBlendMode" in m
                for m in materials
            )
            summary["has_resolved_materials"] = has_resolved

            # 统计纹理
            textures_by_semantic = {}
            for mat in materials:
                resolved_tex = mat.get("ResolvedTextures", {})
                if resolved_tex and isinstance(resolved_tex, dict):
                    for semantic, fname in resolved_tex.items():
                        if fname:
                            textures_by_semantic.setdefault(semantic, set()).add(fname)

            summary["textures_by_semantic"] = {
                k: sorted(v) for k, v in textures_by_semantic.items()
            }
            summary["total_unique_textures"] = sum(
                len(v) for v in textures_by_semantic.values()
            )

        except (json.JSONDecodeError, KeyError, FileNotFoundError):
            pass

        return summary