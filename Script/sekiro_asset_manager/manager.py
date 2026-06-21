"""
SekiroAssetManager —— 只狼资产导入总管理器。

协调三个子模块（ModelImporter、AnimationImporter、MaterialImporter），
提供统一的导入 API 和全自动管线编排。

支持：
  - 独立导入：import_model() / import_animation() / import_material()
  - 全自动导入：import_all() — 按依赖顺序执行模型→动画→材质
  - 批量操作：批量动画导入委派给 AnimationImporter.import_all_anibnd()
  - 配置查询：所有路径和工具配置通过 PipelineConfig 统一管理

用法示例：

    from sekiro_asset_manager.manager import SekiroAssetManager

    mgr = SekiroAssetManager()

    # 独立导入
    mgr.import_model("WP_A_0300_Kusabimaru")
    mgr.import_animation("c0000_a000_hi")

    # 全自动导入
    mgr.import_all("c0000")
"""

from sekiro_asset_manager.model_importer import ModelImporter
from sekiro_asset_manager.animation_importer import AnimationImporter
from sekiro_asset_manager.material_importer import MaterialImporter
from sekiro_asset_manager.pipeline_config import PipelineConfig, config as default_config


class SekiroAssetManager:
    """Sekiro 资产导入管理器。

    协调 ModelImporter、AnimationImporter、MaterialImporter 三个子模块，
    提供独立导入和全自动导入两种工作模式。

    全自动导入（import_all）的执行顺序：
      1. 模型（模型 + 骨架，动画和材质的依赖）
      2. 动画（依赖模型的骨架定义）
      3. 材质（依赖模型的 mesh 路径和材质槽）

    每个步骤的失败不会阻止后续步骤执行（但动画和材质依赖模型成功）。
    """

    def __init__(self, config: PipelineConfig = None):
        """初始化管理器，创建三个子模块实例。

        Args:
            config: PipelineConfig 实例。为 None 时使用全局默认配置。
        """
        self.config = config or default_config
        self.model = ModelImporter(self.config)
        self.animation = AnimationImporter(self.config)
        self.material = MaterialImporter(self.config)

    # ======================================================================
    # 独立导入 API
    # ======================================================================

    def import_model(self, asset_name: str, **kwargs) -> dict:
        """仅导入模型（模型 + 骨架），不包含动画和材质。

        参数透传给 ModelImporter.import_model()。

        Args:
            asset_name: 资产名（如 "WP_A_0300_Kusabimaru"）。
            **kwargs: 其他关键字参数，参见 ModelImporter.import_model()。

        Returns:
            dict: {
                "success": bool,
                "asset_name": str,
                "steps": {步骤名: bool},
                "error": str,           # 仅失败时
                "output_json": str,     # 最终 JSON 路径
                "output_fbx": str,      # 最终 FBX 路径
                "target_ue_path": str,  # UE 导入目标
            }
        """
        return self.model.import_model(asset_name=asset_name, **kwargs)

    def import_animation(self, asset_name: str, **kwargs) -> dict:
        """仅导入动画。

        参数透传给 AnimationImporter.import_animation()。

        Args:
            asset_name: 资产名（如 "c0000"、"c0000_a000_hi"）。
            **kwargs: 其他关键字参数，参见 AnimationImporter.import_animation()。

        Returns:
            dict: {
                "success": bool,
                "asset_name": str,
                "steps": {步骤名: bool},
                "error": str,              # 仅失败时
                "anibnd_dir": str,
                "anim_json": str,
                "output_fbx": str,
                "target_ue_path": str,
            }
        """
        return self.animation.import_animation(asset_name=asset_name, **kwargs)

    def import_material(self, asset_name: str, **kwargs) -> dict:
        """仅导入材质。

        参数透传给 MaterialImporter.import_material()。

        Args:
            asset_name: 资产名（仅用于日志和路径推导）。
            **kwargs: 其他关键字参数，参见 MaterialImporter.import_material()。

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
        return self.material.import_material(asset_name=asset_name, **kwargs)

    # ======================================================================
    # 全自动管线
    # ======================================================================

    def import_all(
        self,
        asset_name: str,
        model_kwargs: dict = None,
        anim_kwargs: dict = None,
        mat_kwargs: dict = None,
    ) -> dict:
        """全自动管线：按依赖顺序导入模型 + 动画 + 材质。

        执行顺序：
          1. 导入模型（骨架 + 网格体）—— 动画和材质的依赖
          2. 导入动画（依赖模型的骨架）
          3. 导入材质（依赖模型的 mesh 路径和材质槽）

        后两个步骤即使上游失败也会尝试执行，
        但最终 success 状态由 model 和 material 共同决定。

        Args:
            asset_name: 资产名。
            model_kwargs: 透传给 import_model 的参数字典。
            anim_kwargs: 透传给 import_animation 的参数字典。
            mat_kwargs: 透传给 import_material 的参数字典。

        Returns:
            dict: {
                "success": bool,
                "asset_name": str,
                "model": dict,      # ModelImporter 结果
                "animation": dict,  # AnimationImporter 结果
                "material": dict,   # MaterialImporter 结果
                "error": str,       # 仅模型或材质失败时
            }
        """
        model_kwargs = model_kwargs or {}
        anim_kwargs = anim_kwargs or {}
        mat_kwargs = mat_kwargs or {}

        print(f"\n{'='*60}")
        print(f"全自动导入管线: {asset_name}")
        print(f"{'='*60}")

        # ── 步骤 1：导入模型（骨架 + 网格体） ──────────────────────────
        print(f"\n{'─'*40}")
        print(f"阶段 1/3：模型导入")
        print(f"{'─'*40}")
        model_result = self.import_model(asset_name, **model_kwargs)

        # 模型失败是严重的，但仍尝试后续步骤
        model_ok = model_result.get("success", False)
        if not model_ok:
            error_msg = model_result.get("error", "未知错误")
            print(f"  [警告] 模型导入失败，继续尝试导入动画和材质")
            print(f"  [原因] {error_msg}")

        # 从模型结果提取动画和材质需要的默认参数
        model_json = model_result.get("output_json", "")
        target_ue_path = model_result.get("target_ue_path", "")

        # ── 步骤 2：导入动画（依赖模型骨架） ────────────────────────────
        print(f"\n{'─'*40}")
        print(f"阶段 2/3：动画导入")
        print(f"{'─'*40}")

        # 如果模型导入成功且没有显式传入 model_json，
        # 自动将模型的 JSON 传递给动画步骤
        if model_ok and "model_json" not in anim_kwargs and model_json:
            anim_kwargs.setdefault("model_json", model_json)

        anim_result = self.import_animation(asset_name, **anim_kwargs)

        # 动画失败不影响整体 success（动画可能后期单独补充）
        if not anim_result.get("success", False):
            print(f"  [警告] 动画导入失败，继续执行材质导入")
            print(f"  [原因] {anim_result.get('error', '未知错误')}")

        # ── 步骤 3：导入材质（依赖模型网格体） ──────────────────────────
        print(f"\n{'─'*40}")
        print(f"阶段 3/3：材质导入")
        print(f"{'─'*40}")

        # 如果模型导入成功，自动推导材质需要的参数
        if model_ok:
            if "model_json" not in mat_kwargs and model_json:
                mat_kwargs.setdefault("model_json", model_json)
            if "target_ue_path" not in mat_kwargs and target_ue_path:
                mat_kwargs.setdefault("target_ue_path", target_ue_path)

        mat_result = self.import_material(asset_name, **mat_kwargs)

        # ── 汇总结果 ────────────────────────────────────────────────────
        mat_ok = mat_result.get("success", False)

        # 整体 success：模型和材质都成功（动画可独立补导）
        all_ok = model_ok and mat_ok

        summary = {
            "success": all_ok,
            "asset_name": asset_name,
            "model": model_result,
            "animation": anim_result,
            "material": mat_result,
        }

        if not all_ok:
            if not model_ok:
                summary["error"] = f"模型导入失败: {model_result.get('error', '未知错误')}"
            elif not mat_ok:
                summary["error"] = f"材质导入失败: {mat_result.get('error', '未知错误')}"

        # ── 完成 ────────────────────────────────────────────────────────
        print(f"\n{'='*60}")
        status = "成功" if all_ok else "部分失败"
        print(f"全自动管线完成: {asset_name} — {status}")
        print(f"  模型:    {'OK' if model_ok else 'FAIL'}")
        print(f"  动画:    {'OK' if anim_result.get('success') else 'FAIL/WARN'}")
        print(f"  材质:    {'OK' if mat_ok else 'FAIL'}")
        print(f"{'='*60}")

        return summary

    # ======================================================================
    # 批量操作
    # ======================================================================

    def import_all_anibnd(
        self,
        base_asset: str = "c0000",
        model_json: str = None,
        ue_root: str = None,
        **kwargs,
    ) -> dict:
        """批量导入指定角色的所有 anibnd 包。

        委派给 AnimationImporter.import_all_anibnd()。

        Args:
            base_asset: 基础资产代码（如 "c0000"）。
            model_json: 模型 JSON 路径（复用同一骨架定义）。
            ue_root: UE 导入根路径。
            **kwargs: 其他参数透传给 AnimationImporter.import_all_anibnd()。

        Returns:
            dict: {
                "success": bool,
                "base_asset": str,
                "results": [每个子包的 import_animation 结果],
                "error": str,
            }
        """
        return self.animation.import_all_anibnd(
            base_asset=base_asset,
            model_json=model_json,
            ue_root=ue_root,
            **kwargs,
        )

    # ======================================================================
    # 信息查询
    # ======================================================================

    def get_summary(self, asset_name: str) -> dict:
        """获取指定资产在各阶段的摘要信息。

        从各子模块收集模型、动画、材质的摘要信息，
        用于在导入前确认资产状态。

        Args:
            asset_name: 资产名。

        Returns:
            dict: {
                "asset_name": str,
                "model": dict,       # 模型 JSON 摘要（骨骼数、网格数、材质数）
                "animation": dict,   # 动画摘要（骨骼数、动画数、分类统计）
                "material": dict,    # 材质摘要（材质数、纹理数）
                "config": str,       # PipelineConfig 关键配置展示
            }
        """
        from sekiro_asset_manager.model_importer import _default_output_json

        model_json = _default_output_json(asset_name)
        anim_json = f"{default_config.extracted_dir}/{asset_name}_animations.json"

        summary = {
            "asset_name": asset_name,
            "model": self.model._guess_skeleton_name(model_json),
            "animation": self.animation.get_animation_summary(anim_json),
            "material": self.material.get_texture_summary(model_json),
            "config": f"游戏目录: {self.config.game_dir}",
        }

        return summary

    def verify_prerequisites(self) -> dict:
        """验证管线前置条件是否满足。

        检查项：
          - 游戏目录是否存在
          - 引擎目录是否存在
          - 各工具是否可执行
          - Blender 路径是否有效
          - EU Python 是否可执行

        Returns:
            dict: {
                "all_ok": bool,
                "checks": {
                    "game_dir": bool,
                    "engine_dir": bool,
                    "flver_to_fbx": bool,
                    "sekiro_anim_extractor": bool,
                    "yabber": bool,
                    "texconv": bool,
                    "blender": bool,
                    "ue_python": bool,
                    "uproject": bool,
                },
            }
        """
        checks = {}

        # 游戏目录
        checks["game_dir"] = bool(
            self.config.game_dir and os.path.exists(self.config.game_dir)
        )

        # 引擎目录
        checks["engine_dir"] = bool(
            self.config.engine_dir and os.path.exists(self.config.engine_dir)
        )

        # 工具检查
        tool_names = ["flver_to_fbx", "sekiro_anim_extractor", "yabber", "texconv"]
        for name in tool_names:
            path = self.config.tool_path(name)
            checks[name] = bool(path and os.path.exists(path))

        # Blender
        checks["blender"] = bool(
            self.config.get_blender() and (
                os.path.exists(self.config.get_blender())
                or self.config.get_blender() == "blender"
            )
        )

        # UE Python
        ue_py = self.config.get_python()
        checks["ue_python"] = bool(ue_py and os.path.exists(ue_py))

        # .uproject
        uproject = os.path.join(self.config.project_dir, "Sekiro.uproject")
        checks["uproject"] = os.path.exists(uproject)

        checks["all_ok"] = all(checks.values())

        return checks

    # ======================================================================
    # 显示信息
    # ======================================================================

    def print_summary(self, result: dict) -> None:
        """友好打印导入结果。

        Args:
            result: import_* 或 import_all 返回的结果 dict。
        """
        if not result:
            print("空结果")
            return

        success = result.get("success", False)
        asset_name = result.get("asset_name", "")

        status_symbol = "OK" if success else "FAIL"
        print(f"\n[{status_symbol}] {result.get('command', 'import')} {asset_name}")

        # 缩进展示各字段
        for key, value in result.items():
            if key in ("success", "asset_name", "command"):
                continue
            if isinstance(value, dict):
                print(f"  {key}:")
                for k, v in value.items():
                    if k == "steps" and isinstance(v, dict):
                        print(f"    steps: {v}")
                    elif k == "error" and v:
                        print(f"    error: {v}")
                    elif k == "success":
                        print(f"    success: {'OK' if v else 'FAIL'}")
            elif isinstance(value, bool):
                print(f"  {key}: {'OK' if value else 'FAIL'}")
            else:
                print(f"  {key}: {value}")


import os  # 用于 verify_prerequisites / __init__