"""
AnimationImporter —— 只狼动画导入管线编排模块。

负责从 anibnd 解包到 UE AnimationSequence 的完整动画管线：

  1. [可选] 解包 anibnd.dcx → 得到 skeleton.hkx + *.tae 文件
  2. 调用 SekiroAnimExtractor C# 工具 → 读取 anibnd 解包目录 → 输出 Sekiro_animations.json
  3. 调用 Blender → 读取 model.json（骨架定义）+ anim.json（动画数据）→ 导出动画 FBX
  4. 调用 UE Commandlet → 导入 FBX 为 AnimationSequence

用法示例：

    from sekiro_asset_manager.animation_importer import AnimationImporter

    importer = AnimationImporter()
    result = importer.import_animation(
        asset_name="c0000",
        anibnd_dir="Extracted/c0000-a000_hi-anibnd-dcx",
        model_json="Extracted/Sekiro_model.json",
        anim_json="Extracted/Sekiro_animations.json",
        output_fbx="Extracted/Sekiro_Anim_High.fbx",
        target_ue_path="/Game/Sekiro/Characters/Sekiro/Animations/High",
    )

批量导入多个 anibnd 包：

    importer.import_all_anibnd(
        base_asset="c0000",
        model_json="Extracted/Sekiro_model.json",
        ue_root="/Game/Sekiro/Characters/Sekiro/Animations",
    )
"""

import os
import subprocess
import json
import glob
from typing import Optional

from sekiro_asset_manager.pipeline_config import PipelineConfig, config as default_config


# ======================================================================
# 默认路径推导
# ======================================================================

def _default_extracted_dir() -> str:
    """返回 Extracted/ 目录。"""
    return default_config.extracted_dir


def _default_anibnd_dir(asset_name: str, variant: str = "") -> str:
    """根据资产名推导默认的 anibnd 解包目录路径。

    Yabber 解包 anibnd.dcx 后生成的目录命名规则：
      文件名中的 "." 替换为 "-"。
    例如：
      c0000.anibnd.dcx        → Extracted/c0000-anibnd-dcx/
      c0000_a000_hi.anibnd.dcx → Extracted/c0000_a000_hi-anibnd-dcx/

    Args:
        asset_name: 资产名（如 "c0000", "c0000_a000_hi"）。
        variant: 变体后缀（如 "hi", "lo", "md"），为空时直接使用 asset_name。

    Returns:
        anibnd 解包目录路径。
    """
    base = asset_name
    if variant:
        base = f"{asset_name}_{variant}"
    dir_name = f"{base}-anibnd-dcx"
    return os.path.join(_default_extracted_dir(), dir_name)


def _default_skeleton_hkx(asset_name: str = "c0000") -> str:
    """推导 anibnd 解包目录中 skeleton.hkx 的路径。

    Yabber 解包 anibnd.dcx 后的骨架文件结构：
      Extracted/<anibnd_name>-anibnd-dcx/chr/<asset_name>/hkx/skeleton.hkx

    Args:
        asset_name: 资产代码（如 "c0000"）。

    Returns:
        skeleton.hkx 完整路径。
    """
    anibnd_dir = _default_anibnd_dir(asset_name)
    return os.path.join(anibnd_dir, "chr", asset_name, "hkx", "skeleton.hkx")


def _default_tae_dir(asset_name: str = "c0000") -> str:
    """推导 anibnd 解包目录中 TAE 文件的目录。

    Yabber 解包后的 TAE 文件结构：
      Extracted/<anibnd_name>-anibnd-dcx/Model/chr/<asset_name>/txt/<tae>.txt

    Args:
        asset_name: 资产代码（如 "c0000"）。

    Returns:
        TAE 目录路径。
    """
    anibnd_dir = _default_anibnd_dir(asset_name)
    return os.path.join(anibnd_dir, "Model", "chr", asset_name, "txt")


def _default_anim_json(asset_name: str, variant: str = "") -> str:
    """根据资产名推导默认的动画 JSON 输出路径（Output/<prefix>/Animation/）。"""
    base = asset_name
    if variant:
        base = f"{asset_name}_{variant}"
    d = default_config.output_anim_dir(asset_name)
    return os.path.join(d, f"{base}_animations.json")


def _default_model_json() -> str:
    """返回默认的模型 JSON 路径（用于 Blender 骨架构建）。"""
    return default_config.output_model_json("Sekiro")


def _default_output_fbx(asset_name: str) -> str:
    """根据资产名推导默认的动画 FBX 输出路径。"""
    d = default_config.output_anim_dir(asset_name)
    return os.path.join(d, f"{asset_name}_Anim.fbx")


def _default_ue_path(asset_name: str, category: str = "Characters") -> str:
    """推导动画在 UE 内容浏览器中的默认导入路径。

    Args:
        asset_name: 资产名（如 "Sekiro"）。
        category: 类别（"Characters", "Weapons" 等）。

    Returns:
        UE 路径，如 "/Game/Sekiro/Characters/<asset_name>/Animations"。
    """
    return f"/Game/Sekiro/{category}/{asset_name}/Animations"


def _find_anibnd_dirs(base_asset: str = "c0000") -> list[dict]:
    """在 Extracted/ 下查找所有已解包的 anibnd 目录。

    扫描 Extracted/ 下所有 <base_asset>* -anibnd-dcx/ 目录，
    按名称优先级排序（完整 anibnd 在前，子包在后）。

    Args:
        base_asset: 基础资产代码（如 "c0000"）。

    Returns:
        目录信息列表，每项为 {"dir": str, "name": str, "is_main": bool}。
    """
    extracted = _default_extracted_dir()
    pattern = os.path.join(extracted, f"{base_asset}*anibnd-dcx")
    dirs = sorted(glob.glob(pattern))

    result = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        dir_name = os.path.basename(d)
        # 去掉 "-anibnd-dcx" 后缀得到名称
        name = dir_name.replace("-anibnd-dcx", "")
        is_main = name == base_asset
        result.append({"dir": d, "name": name, "is_main": is_main})

    # 主包排前面，子包按字母序
    result.sort(key=lambda x: (not x["is_main"], x["name"]))
    return result


# ======================================================================
# 动画导入器
# ======================================================================

class AnimationImporter:
    """动画导入管线编排器。

    封装完整动画导入流程：
      解包 anibnd → SekiroAnimExtractor → Blender 导出 FBX → UE 导入。
    每个步骤可单独调用，也可通过 import_animation() 一键执行。

    支持传入 model.json（骨架定义），不传入则使用默认路径自动查找。
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

    def import_animation(
        self,
        asset_name: str = "",
        anibnd_dir: Optional[str] = None,
        model_json: Optional[str] = None,
        anim_json: Optional[str] = None,
        output_fbx: Optional[str] = None,
        target_ue_path: Optional[str] = None,
        skeleton_hkx: Optional[str] = None,
        skip_unpack: bool = False,
        skip_extractor: bool = False,
        skip_blender: bool = False,
        skip_ue_import: bool = False,
    ) -> dict:
        """执行完整动画导入管线。

        管线步骤：
          1. [可选] 解包 anibnd.dcx（如果尚未解包）
          2. 调用 SekiroAnimExtractor → 生成 anim JSON
          3. 调用 Blender → 导出动画 FBX
          4. 调用 UE Commandlet → 导入 AnimationSequence

        Args:
            asset_name: 资产名（如 "c0000"、"c0000_a000_hi"）。
                        用于推导所有未显式传入的路径参数。
            anibnd_dir: anibnd 解包目录路径。
                        为 None 时根据 asset_name 自动推导。
            model_json: 模型 JSON 路径（包含骨架定义），Blender 导出必需。
            anim_json: 动画 JSON 路径（SekiroAnimExtractor 的输出）。
            output_fbx: Blender 导出的 FBX 路径。
            target_ue_path: UE 内容浏览器中的目标路径。
            skeleton_hkx: 骨架 HKX 路径（可选，某些提取器可能需要）。
            skip_unpack: 跳过 anibnd 解包步骤。
            skip_extractor: 跳过 SekiroAnimExtractor 步骤。
            skip_blender: 跳过 Blender 导出 FBX 步骤。
            skip_ue_import: 跳过 UE 导入步骤。

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
        result = {
            "success": False,
            "asset_name": asset_name,
            "steps": {},
            "anibnd_dir": "",
            "anim_json": "",
            "output_fbx": "",
            "target_ue_path": "",
            "error": "",
        }

        # ── 推导默认路径 ──────────────────────────────────────────────
        if not anibnd_dir:
            anibnd_dir = _default_anibnd_dir(asset_name)
        if not anim_json:
            anim_json = _default_anim_json(asset_name)
        if not output_fbx:
            output_fbx = _default_output_fbx(asset_name)
        if not model_json:
            model_json = _default_model_json()
        if not target_ue_path:
            target_ue_path = _default_ue_path(asset_name)

        result["anibnd_dir"] = anibnd_dir
        result["anim_json"] = anim_json
        result["output_fbx"] = output_fbx
        result["target_ue_path"] = target_ue_path

        # ── 确保输出目录存在 ───────────────────────────────────────────
        os.makedirs(os.path.dirname(anim_json), exist_ok=True)
        os.makedirs(os.path.dirname(output_fbx), exist_ok=True)

        # ── 步骤 1：解包 anibnd ────────────────────────────────────────
        if not skip_unpack:
            step_name = "unpack_anibnd"
            print(f"\n{'='*60}")
            print(f"步骤 1：解包 anibnd（{asset_name}）")
            print(f"{'='*60}")
            try:
                ok = self._unpack_anibnd(asset_name, anibnd_dir)
                result["steps"][step_name] = ok
                if not ok:
                    result["error"] = "anibnd 解包失败"
                    return result
            except Exception as e:
                result["steps"][step_name] = False
                result["error"] = f"anibnd 解包异常: {e}"
                return result

        # ── 步骤 2：SekiroAnimExtractor ───────────────────────────────
        if not skip_extractor:
            step_name = "anim_extractor"
            print(f"\n{'='*60}")
            print(f"步骤 2：SekiroAnimExtractor 提取动画")
            print(f"{'='*60}")
            try:
                ok = self.run_anim_extractor(
                    anibnd_dir=anibnd_dir,
                    output_json=anim_json,
                    model_json=model_json,
                )
                result["steps"][step_name] = ok
                if not ok:
                    result["error"] = "SekiroAnimExtractor 提取失败"
                    return result
            except Exception as e:
                result["steps"][step_name] = False
                result["error"] = f"SekiroAnimExtractor 异常: {e}"
                return result

        # ── 步骤 3：Blender 导出 FBX ──────────────────────────────────
        if not skip_blender:
            step_name = "blender_export"
            print(f"\n{'='*60}")
            print(f"步骤 3：Blender 导出动画 FBX")
            print(f"{'='*60}")
            try:
                ok = self.run_blender_export(
                    model_json=model_json,
                    anim_json=anim_json,
                    output_fbx=output_fbx,
                )
                result["steps"][step_name] = ok
                if not ok:
                    result["error"] = "Blender 导出 FBX 失败"
                    return result
            except Exception as e:
                result["steps"][step_name] = False
                result["error"] = f"Blender 导出异常: {e}"
                return result

        # ── 步骤 4：UE 导入 ───────────────────────────────────────────
        if not skip_ue_import:
            step_name = "ue_import"
            print(f"\n{'='*60}")
            print(f"步骤 4：UE 导入动画 FBX")
            print(f"{'='*60}")
            try:
                ok = self.run_ue_import(
                    fbx_path=output_fbx,
                    target_path=target_ue_path,
                    model_json=model_json,
                    asset_name=asset_name,
                )
                result["steps"][step_name] = ok
                if not ok:
                    result["error"] = "UE 动画导入失败"
                    return result
            except Exception as e:
                result["steps"][step_name] = False
                result["error"] = f"UE 导入异常: {e}"
                return result

        # ── 完成 ───────────────────────────────────────────────────────
        result["success"] = True
        print(f"\n{'='*60}")
        print(f"动画导入完成: {asset_name}")
        print(f"  anibnd:     {anibnd_dir}")
        print(f"  FBX:        {output_fbx}")
        print(f"  UE 路径:    {target_ue_path}")
        print(f"{'='*60}")
        return result

    # ------------------------------------------------------------------
    # 批量导入
    # ------------------------------------------------------------------

    def import_all_anibnd(
        self,
        base_asset: str = "c0000",
        model_json: Optional[str] = None,
        ue_root: Optional[str] = None,
        include_main: bool = True,
        skip_unpack: bool = False,
        skip_extractor: bool = False,
        skip_blender: bool = False,
        skip_ue_import: bool = False,
    ) -> dict:
        """批量导入指定角色的所有 anibnd 包。

        自动扫描 Extracted/ 下所有 <base_asset>* -anibnd-dcx/ 目录，
        对每个子包依次执行完整导入管线。

        Args:
            base_asset: 基础资产代码（如 "c0000"）。
            model_json: 模型 JSON 路径（复用同一骨架定义）。
            ue_root: UE 导入根路径，子包自动追加子目录。
            include_main: 是否包含主 anibnd 包（如 c0000-anibnd-dcx）。
            skip_unpack: 跳过解包。
            skip_extractor: 跳过提取器。
            skip_blender: 跳过 Blender 导出。
            skip_ue_import: 跳过 UE 导入。

        Returns:
            dict: {
                "success": bool,
                "base_asset": str,
                "results": [每个子包的 import_animation 结果],
                "error": str,
            }
        """
        result = {
            "success": False,
            "base_asset": base_asset,
            "results": [],
            "error": "",
        }

        if not model_json:
            model_json = _default_model_json()
        if not ue_root:
            ue_root = _default_ue_path(base_asset)

        # 查找所有 anibnd 解包目录
        dirs = _find_anibnd_dirs(base_asset)
        if not dirs:
            result["error"] = f"未找到 {base_asset} 的 anibnd 解包目录"
            return result

        print(f"\n{'='*60}")
        print(f"批量导入动画: {base_asset}")
        print(f"  找到 {len(dirs)} 个 anibnd 包")
        print(f"  模型 JSON: {model_json}")
        print(f"  UE 根路径: {ue_root}")
        print(f"{'='*60}")

        all_success = True
        for entry in dirs:
            if entry["is_main"] and not include_main:
                print(f"\n[跳过主包] {entry['name']}")
                continue

            dir_name = entry["name"]
            # 构建子路径名，取最后一段
            # c0000_a000_hi → 取 "a000_hi" 或完整名
            sub_path = dir_name.replace(f"{base_asset}_", "", 1) if dir_name != base_asset else "Main"
            target_path = f"{ue_root}/{sub_path}"

            print(f"\n{'─'*60}")
            print(f"[{entry['name']}] → {target_path}")
            print(f"{'─'*60}")

            sub_result = self.import_animation(
                asset_name=dir_name,
                anibnd_dir=entry["dir"],
                model_json=model_json,
                anim_json=_default_anim_json(dir_name),
                output_fbx=_default_output_fbx(dir_name),
                target_ue_path=target_path,
                skip_unpack=skip_unpack,
                skip_extractor=skip_extractor,
                skip_blender=skip_blender,
                skip_ue_import=skip_ue_import,
            )

            result["results"].append(sub_result)
            if not sub_result["success"]:
                all_success = False
                print(f"  [失败] {sub_result.get('error', '未知错误')}")

        result["success"] = all_success
        if not all_success:
            result["error"] = "部分 anibnd 包导入失败"

        print(f"\n{'='*60}")
        print(f"批量导入完成: {'成功' if all_success else '部分失败'}")
        print(f"  处理 {len(result['results'])} 个包")
        print(f"{'='*60}")

        return result

    # ------------------------------------------------------------------
    # 解包 anibnd
    # ------------------------------------------------------------------

    def _unpack_anibnd(self, asset_name: str, anibnd_dir: str) -> bool:
        """解包 anibnd.dcx 到 Extracted/ 目录。

        流程：
          1. 从游戏目录复制 anibnd.dcx 到 Extracted/
          2. 调用 Yabber 解包 → 得到 skeleton.hkx + TAE 文件

        Args:
            asset_name: 资产名（如 "c0000", "c0000_a000_hi"）。
            anibnd_dir: 解包后的目录路径（用于判断是否已解包）。

        Returns:
            bool: 是否成功。
        """
        # 如果解包目录已存在，跳过
        if os.path.isdir(anibnd_dir):
            print(f"  解包目录已存在，跳过: {anibnd_dir}")
            return True

        yabber = self.config.tool_path("yabber")
        if not yabber or not os.path.exists(yabber):
            print(f"  [错误] Yabber 未找到: {yabber}")
            return False

        game_dir = self.config.game_dir
        if not game_dir or not os.path.exists(game_dir):
            print(f"  [错误] 游戏目录未找到: {game_dir}")
            return False

        # 查找 anibnd.dcx 文件
        # 主包：chr/c0000.anibnd.dcx
        # 子包：chr/c0000_a000_hi.anibnd.dcx
        anibnd_filename = f"{asset_name}.anibnd.dcx"
        anibnd_src = os.path.join(game_dir, "chr", anibnd_filename)

        if not os.path.exists(anibnd_src):
            print(f"  [错误] anibnd 未找到: {anibnd_src}")
            return False

        # 复制到 Extracted/
        extracted_dir = self.config.extracted_dir
        anibnd_dst = os.path.join(extracted_dir, anibnd_filename)
        if not os.path.exists(anibnd_dst):
            print(f"  复制 anibnd: {anibnd_filename}")
            os.makedirs(os.path.dirname(anibnd_dst), exist_ok=True)
            import shutil
            shutil.copy2(anibnd_src, anibnd_dst)

        # 用 Yabber 解包
        print(f"  用 Yabber 解包: {anibnd_filename}")
        try:
            result = subprocess.run(
                [yabber, anibnd_dst],
                capture_output=True, text=True, timeout=120,
            )
            if result.returncode != 0:
                print(f"  [错误] Yabber 解包失败: {result.stderr.strip()}")
                return False
        except subprocess.TimeoutExpired:
            print(f"  [错误] Yabber 解包超时")
            return False

        # 验证解包结果
        if not os.path.isdir(anibnd_dir):
            print(f"  [错误] 解包后目录未生成: {anibnd_dir}")
            return False

        # 检查骨架文件
        skeleton_hkx_path = _default_skeleton_hkx(asset_name.split("_")[0])
        # 子包和主包共用同名的 chr 子目录
        chr_name = asset_name.split("_")[0]  # 从 "c0000_a000_hi" 取 "c0000"
        actual_skeleton = os.path.join(anibnd_dir, "chr", chr_name, "hkx", "skeleton.hkx")
        if os.path.exists(actual_skeleton):
            print(f"  骨架文件: {actual_skeleton}")
        else:
            print(f"  [警告] 骨架文件未找到，部分提取可能需要单独的主包骨架")

        # 统计 HKX 动画文件数量
        hkx_files = glob.glob(os.path.join(anibnd_dir, "**", "*.hkx"), recursive=True)
        print(f"  解包完成: {len(hkx_files)} 个 HKX 文件")

        return True

    # ------------------------------------------------------------------
    # SekiroAnimExtractor 调用
    # ------------------------------------------------------------------

    def run_anim_extractor(
        self,
        anibnd_dir: str,
        output_json: str,
        model_json: Optional[str] = None,
        sample_rate: int = 30,
    ) -> bool:
        """调用 SekiroAnimExtractor C# 工具从 anibnd 解包目录提取动画数据。

        命令格式（C# Program.cs）：
          SekiroAnimExtractor.exe <skeleton.hkx> <anim_dir> <output.json> [--sample-rate 30]

        Args:
            anibnd_dir: anibnd 解包目录的完整路径。
            output_json: 输出的 JSON 文件路径。
            model_json: 可选的模型 JSON 路径（未使用，保留接口兼容）。
            sample_rate: 动画采样帧率，默认 30fps。

        Returns:
            bool: 是否成功。
        """
        # 工具路径：内建 ext_tools 目录
        extractor_dir = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "ext_tools"
        )
        extractor = os.path.join(extractor_dir, "SekiroAnimExtractor.exe")
        if not os.path.exists(extractor):
            print(f"  [错误] SekiroAnimExtractor 未找到: {extractor}")
            return False

        # 验证输入目录
        if not os.path.isdir(anibnd_dir):
            print(f"  [错误] anibnd 解包目录不存在: {anibnd_dir}")
            return False

        # 查找 skeleton.hkx
        # 主包：c0000-anibnd-dcx/chr/c0000/hkx/skeleton.hkx
        # 子包：c0000_a000_hi-anibnd-dcx/chr/c0000/hkx/skeleton.hkx
        skeleton_hkx = None
        for root, dirs, files in os.walk(anibnd_dir):
            for f in files:
                if f.lower() == "skeleton.hkx":
                    skeleton_hkx = os.path.join(root, f)
                    break
            if skeleton_hkx:
                break

        if not skeleton_hkx:
            print(f"  [错误] 在 {anibnd_dir} 中未找到 skeleton.hkx")
            return False

        # 查找动画 HKX 目录（与 skeleton.hkx 同目录或子目录）
        anim_dir = os.path.dirname(skeleton_hkx)

        # 构建命令行（匹配 C# Program.cs 参数格式）
        cmd = [
            extractor, skeleton_hkx, anim_dir, output_json,
            "--sample-rate", str(sample_rate),
        ]

        print(f"  执行 SekiroAnimExtractor...")
        print(f"    骨架: {skeleton_hkx}")
        print(f"    动画: {anim_dir}")
        print(f"    输出: {output_json}")
        print(f"    帧率: {sample_rate}fps")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True, text=True, timeout=300,
            )
        except subprocess.TimeoutExpired:
            print(f"  [错误] SekiroAnimExtractor 超时（5分钟）")
            return False
        except FileNotFoundError:
            print(f"  [错误] 无法执行 SekiroAnimExtractor，请确认 .NET 运行时已安装")
            return False

        if result.returncode != 0:
            print(f"  [错误] SekiroAnimExtractor 返回码 {result.returncode}")
            if result.stderr:
                print(f"  stderr: {result.stderr[:2000]}")
            if result.stdout:
                lines = result.stdout.strip().split("\n")
                for line in lines[-10:]:
                    print(f"  {line}")
            return False

        # 输出工具日志（尾部）
        if result.stdout:
            lines = result.stdout.strip().split("\n")
            for line in lines[-5:]:
                print(f"  {line}")

        # 验证输出
        if not os.path.exists(output_json):
            print(f"  [错误] 动画 JSON 未生成: {output_json}")
            return False

        # 后处理：去掉动画名的 "Sekiro_" 前缀，改用原版 HKX 文件名
        try:
            with open(output_json, "r", encoding="utf-8") as f:
                anim_data = json.load(f)
            modified = False
            for anim in anim_data.get("Animations", []):
                old_name = anim.get("Name", "")
                # C# 工具输出 "Sekiro_{animName}"，改为纯原名
                if old_name.startswith("Sekiro_"):
                    anim["Name"] = old_name[7:]
                    modified = True
            if modified:
                with open(output_json, "w", encoding="utf-8") as f:
                    json.dump(anim_data, f, indent=2, ensure_ascii=False)
                print(f"  动画名前缀已清理: Sekiro_ → 原版")
        except Exception as e:
            print(f"  [警告] 动画名后处理失败: {e}")

        bone_count = len(anim_data.get("Bones", anim_data.get("BoneNames", [])))
        anim_count = len(anim_data.get("Animations", []))
        fsize_mb = os.path.getsize(output_json) / (1024 * 1024)
        print(f"  JSON 生成成功: {anim_count} 动画, {bone_count} 骨骼 ({fsize_mb:.1f} MB)")

        return True
        if not os.path.exists(output_json):
            print(f"  [错误] 输出 JSON 未生成: {output_json}")
            return False

        # 验证 JSON 内容
        try:
            with open(output_json, "r", encoding="utf-8") as f:
                data = json.load(f)

            bone_count = data.get("BoneCount", 0)
            anim_count = data.get("AnimationCount", 0)
            anim_list = data.get("Animations", [])

            print(f"  JSON 提取成功: 骨骼 {bone_count}, 动画 {anim_count}")
            if anim_list:
                # 显示前 3 个动画名称作为示例
                names = [a.get("Name", a.get("OriginalName", "?")) for a in anim_list[:3]]
                print(f"  示例动画: {', '.join(names)}...")

        except (json.JSONDecodeError, KeyError) as e:
            print(f"  [错误] JSON 解析失败: {e}")
            return False

        return True

    # ------------------------------------------------------------------
    # Blender 调用
    # ------------------------------------------------------------------

    def run_blender_export(
        self,
        model_json: str,
        anim_json: str,
        output_fbx: str,
    ) -> bool:
        """调用 Blender 导出动画 FBX。

        命令格式：
          blender --background --python Script/import_anims.py -- \\
              <model.json> <anim.json> <output.fbx>

        Blender 脚本（Script/import_anims.py）负责：
          - 清空场景
          - 从 model.json 构建骨骼层级（骨名、层级关系）
          - 从 anim.json 加载动画数据并绑定到骨骼
          - 应用朝向修正（Y-up → Z-up UE5 坐标系）
          - 导出 FBX（包含骨骼动画，不包含网格体）

        Args:
            model_json: 模型 JSON 路径（包含骨架定义：骨名、父级关系）。
            anim_json: 动画 JSON 路径（包含动画关键帧数据）。
            output_fbx: 导出的 FBX 路径。

        Returns:
            bool: 是否成功。
        """
        blender = self.config.get_blender()
        if not blender or not os.path.exists(blender):
            print(f"  [错误] Blender 未找到: {blender}")
            return False

        # 验证输入文件
        if not os.path.exists(anim_json):
            print(f"  [错误] 动画 JSON 未找到: {anim_json}")
            return False

        if not os.path.exists(model_json):
            print(f"  [错误] 模型 JSON 未找到: {model_json}")
            print(f"  model.json 是必需的，因为 Blender 需要根据骨架定义构建骨骼")
            return False

        # Blender 动画导入脚本路径
        import_script = os.path.join(self.config.scripts_dir, "import_anims.py")
        if not os.path.exists(import_script):
            # 回退：相对于 project_dir
            import_script = os.path.join(
                self.config.project_dir, "Script", "import_anims.py",
            )
        if not os.path.exists(import_script):
            print(f"  [错误] Blender 导入脚本未找到: {import_script}")
            return False

        cmd = [
            blender,
            "--background",
            "--python", import_script,
            "--",
            model_json,
            anim_json,
            output_fbx,
        ]

        print(f"  执行 Blender 导出动画 FBX...")
        print(f"    model.json: {model_json}")
        print(f"    anim.json:  {anim_json}")
        print(f"    output:     {output_fbx}")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True, text=True, timeout=600,
            )
        except subprocess.TimeoutExpired:
            print(f"  [错误] Blender 超时（10分钟）")
            return False

        if result.returncode != 0:
            print(f"  [错误] Blender 返回码 {result.returncode}")
            if result.stderr:
                err_lines = result.stderr.strip().split("\n")
                for line in err_lines[-20:]:
                    print(f"  stderr: {line}")
            # stdout 也输出尾部，可能包含 Python 堆栈
            if result.stdout:
                out_lines = result.stdout.strip().split("\n")
                for line in out_lines[-10:]:
                    print(f"  {line}")
            return False

        # 输出 Blender 日志（尾部）
        if result.stdout:
            lines = result.stdout.strip().split("\n")
            for line in lines[-5:]:
                print(f"  {line}")

        if not os.path.exists(output_fbx):
            print(f"  [错误] FBX 未生成: {output_fbx}")
            return False

        fbx_size_mb = os.path.getsize(output_fbx) / (1024 * 1024)
        print(f"  FBX 导出成功: {output_fbx} ({fbx_size_mb:.1f} MB)")

        return True

    # ------------------------------------------------------------------
    # UE 导入
    # ------------------------------------------------------------------

    def run_ue_import(
        self,
        fbx_path: str,
        target_path: str,
        model_json: Optional[str] = None,
        asset_name: Optional[str] = None,
        skeleton_name: Optional[str] = None,
    ) -> bool:
        """将动画 FBX 导入 UE 为 AnimationSequence。

        通过 SekiroImportCommandlet 导入：
          UnrealEditor-Cmd.exe Sekiro.uproject \\
              -run=SekiroImport \\
              -Model=<model.json> \\
              -Anim=<fbx_path> \\
              -Output=<target_path> \\
              -SkeletonName=<name> \\
              -unattended

        如果当前 SekiroImportCommandlet 的 -Anim 参数尚不支持，
        也可以使用 AIBridge 通过编辑器导入。

        Args:
            fbx_path: 动画 FBX 文件路径。
            target_path: UE 内容浏览器中的目标路径
                         （如 "/Game/Sekiro/Characters/Sekiro/Animations/Locomotion"）。
            model_json: 模型 JSON 路径（可选，提供骨架配置信息）。
            asset_name: 资产名（可选，用于日志）。
            skeleton_name: 骨架名称（可选，从模型 JSON 推断）。

        Returns:
            bool: 是否成功。
        """
        engine_dir = self.config.engine_dir
        if not engine_dir or not os.path.exists(engine_dir):
            print(f"  [错误] UE 引擎目录未找到: {engine_dir}")
            return False

        cmdlet_exe = os.path.join(
            engine_dir,
            "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe",
        )
        if not os.path.exists(cmdlet_exe):
            print(f"  [错误] UnrealEditor-Cmd.exe 未找到: {cmdlet_exe}")
            return False

        uproject = os.path.join(self.config.project_dir, "Sekiro.uproject")
        if not os.path.exists(uproject):
            print(f"  [错误] .uproject 未找到: {uproject}")
            return False

        # 验证输入
        if not os.path.exists(fbx_path):
            print(f"  [错误] FBX 未找到: {fbx_path}")
            return False

        # 推断骨架名称
        if not skeleton_name:
            skeleton_name = self._guess_skeleton_name(model_json or "")

        # 构建 Commandlet 参数
        cmd = [
            cmdlet_exe,
            uproject,
            "-run=SekiroImport",
            f"-Anim={fbx_path}",
            f"-Output={target_path}",
            f"-SkeletonName={skeleton_name}",
            "-unattended",
            "-NoSplash",
            "-NoP4",
        ]

        if model_json and os.path.exists(model_json):
            cmd.append(f"-Model={model_json}")

        label = asset_name or os.path.basename(fbx_path)
        print(f"  执行 UE 动画导入 Commandlet...")
        print(f"    Anim:        {fbx_path}")
        print(f"    UE 路径:     {target_path}")
        print(f"    骨架名称:    {skeleton_name}")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True, text=True, timeout=600,
            )
        except subprocess.TimeoutExpired:
            print(f"  [错误] UE 导入超时（10分钟）")
            return False

        if result.returncode != 0:
            print(f"  [错误] UE 导入返回码 {result.returncode}")
            # 从输出中提取关键错误信息
            output = (result.stdout or "") + (result.stderr or "")
            for line in output.split("\n"):
                if any(kw in line.lower() for kw in ["error", "fail", "exception", "fatal"]):
                    print(f"  {line.strip()}")
            return False

        # 输出日志尾部
        if result.stdout:
            lines = result.stdout.strip().split("\n")
            for line in lines[-10:]:
                print(f"  {line}")

        print(f"  UE 导入成功: {target_path}")
        return True

    # ------------------------------------------------------------------
    # 工具方法
    # ------------------------------------------------------------------

    def _guess_skeleton_name(self, model_json: str) -> str:
        """从模型 JSON 中推断骨架名称。

        Args:
            model_json: 模型 JSON 路径。

        Returns:
            骨架名称字符串。默认返回 "Sekiro"。
        """
        if os.path.exists(model_json):
            try:
                with open(model_json, "r", encoding="utf-8") as f:
                    data = json.load(f)
                name = data.get("SkeletonName", "")
                if name:
                    return name
            except Exception:
                pass
        return "Sekiro"

    def get_animation_summary(self, anim_json: str) -> dict:
        """读取动画 JSON 并返回摘要信息。

        Args:
            anim_json: 动画 JSON 路径。

        Returns:
            dict: {
                "exists": bool,
                "bone_count": int,
                "animation_count": int,
                "animation_names": list[str],
                "categories": dict[str, int],  # 按前缀分类统计
            }
        """
        summary = {
            "exists": False,
            "bone_count": 0,
            "animation_count": 0,
            "animation_names": [],
            "categories": {},
        }

        if not os.path.exists(anim_json):
            return summary

        try:
            with open(anim_json, "r", encoding="utf-8") as f:
                data = json.load(f)

            summary["exists"] = True
            summary["bone_count"] = data.get("BoneCount", 0)
            animations = data.get("Animations", [])
            summary["animation_count"] = len(animations)

            names = []
            categories = {}
            for anim in animations:
                name = anim.get("Name", anim.get("OriginalName", ""))
                names.append(name)
                # 按名称前缀分类（如 "Sekiro_Idle", "Sekiro_Attack"）
                if name:
                    parts = name.split("_")
                    if len(parts) >= 2:
                        cat = parts[1]  # 取第二个段作为类别
                        categories[cat] = categories.get(cat, 0) + 1

            summary["animation_names"] = names
            summary["categories"] = categories

        except (json.JSONDecodeError, KeyError, FileNotFoundError):
            pass

        return summary