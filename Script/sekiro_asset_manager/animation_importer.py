"""
AnimationImporter - Sekiro 动画 JSON 导入管线。

标准流程：
1. [可选] 解包 anibnd.dcx
2. 调用 `SekiroAnimExtractor` 生成动画 JSON
3. 若为 MD 动画包，执行 `fix_md_anim_bones.py` 将 delta 转为完整 local pose
4. 调用 `SAImport` Commandlet 一次性导入 JSON
"""

from __future__ import annotations

import glob
import json
import os
import subprocess
import sys
from typing import Optional

from sekiro_asset_manager.bone_name_normalizer import (
    normalize_internal_root_bone_names,
)
from sekiro_asset_manager.animation_aliases import materialize_tae_animation_aliases
from sekiro_asset_manager.pipeline_config import PipelineConfig, config as default_config


def _base_asset_name(asset_name: str) -> str:
    return asset_name.split("_", 1)[0] if "_" in asset_name else asset_name


def _anim_suffix(asset_name: str) -> str:
    base = _base_asset_name(asset_name)
    return asset_name[len(base) + 1:] if asset_name.startswith(base + "_") else asset_name


def _default_extracted_dir() -> str:
    return default_config.extracted_dir


def _default_anibnd_dir(asset_name: str) -> str:
    return os.path.join(_default_extracted_dir(), f"{asset_name}-anibnd-dcx")


def _default_anim_output_dir(asset_name: str) -> str:
    return default_config.output_anim_dir(_base_asset_name(asset_name))


def _default_anim_json(asset_name: str) -> str:
    suffix = _anim_suffix(asset_name)
    if suffix == asset_name:
        file_name = f"{asset_name}_animations.json"
    else:
        file_name = f"Sekiro_{suffix}_animations.json"
    return os.path.join(_default_anim_output_dir(asset_name), file_name)


def _default_md_combined_json(asset_name: str) -> str:
    suffix = _anim_suffix(asset_name)
    return os.path.join(_default_anim_output_dir(asset_name), f"Sekiro_{suffix}_fullpose_combined.json")


def _default_ue_path(asset_name: str) -> str:
    base = _base_asset_name(asset_name)
    if base == "c0000":
        return "/Game/Characters/Sekiro"
    return f"/Game/Characters/{base}"


def _guess_anim_asset_name_from_output(target_ue_path: str, asset_name: str) -> str:
    last_segment = target_ue_path.rstrip("/").split("/")[-1] if target_ue_path else ""
    if last_segment and last_segment.lower() != "main":
        return last_segment
    return _base_asset_name(asset_name)


def _find_anibnd_dirs(base_asset: str = "c0000") -> list[dict]:
    extracted = _default_extracted_dir()
    pattern = os.path.join(extracted, f"{base_asset}*anibnd-dcx")
    dirs = sorted(glob.glob(pattern))
    result = []
    for directory in dirs:
        if not os.path.isdir(directory):
            continue
        name = os.path.basename(directory).replace("-anibnd-dcx", "")
        result.append({"dir": directory, "name": name, "is_main": name == base_asset})
    result.sort(key=lambda item: (not item["is_main"], item["name"]))
    return result


class AnimationImporter:
    def __init__(self, config: Optional[PipelineConfig] = None):
        self.config = config or default_config

    def import_animation(
        self,
        asset_name: str,
        anibnd_dir: Optional[str] = None,
        model_json: Optional[str] = None,
        anim_json: Optional[str] = None,
        target_ue_path: Optional[str] = None,
        skeleton_hkx: Optional[str] = None,
        skip_unpack: bool = False,
        skip_extractor: bool = False,
        skip_md_fix: bool = False,
        skip_ue_import: bool = False,
        sample_rate: int = 30,
    ) -> dict:
        result = {
            "success": False,
            "asset_name": asset_name,
            "steps": {},
            "anibnd_dir": "",
            "anim_json": "",
            "target_ue_path": "",
            "error": "",
            "import_json": "",
        }

        anibnd_dir = anibnd_dir or _default_anibnd_dir(asset_name)
        anim_json = anim_json or _default_anim_json(asset_name)
        target_ue_path = target_ue_path or _default_ue_path(asset_name)
        import_json = anim_json

        result["anibnd_dir"] = anibnd_dir
        result["anim_json"] = anim_json
        result["target_ue_path"] = target_ue_path

        os.makedirs(os.path.dirname(anim_json), exist_ok=True)

        if not skip_unpack:
            try:
                ok = self._unpack_anibnd(asset_name, anibnd_dir)
                result["steps"]["unpack_anibnd"] = ok
                if not ok:
                    result["error"] = "anibnd 解包失败"
                    return result
            except Exception as exc:
                result["steps"]["unpack_anibnd"] = False
                result["error"] = f"anibnd 解包异常: {exc}"
                return result

        if not skip_extractor:
            try:
                ok = self.run_anim_extractor(
                    anibnd_dir=anibnd_dir,
                    output_json=anim_json,
                    model_json=model_json,
                    sample_rate=sample_rate,
                    skeleton_hkx=skeleton_hkx,
                )
                result["steps"]["anim_extractor"] = ok
                if not ok:
                    result["error"] = "SekiroAnimExtractor 提取失败"
                    return result
            except Exception as exc:
                result["steps"]["anim_extractor"] = False
                result["error"] = f"SekiroAnimExtractor 异常: {exc}"
                return result

        if self._is_md_asset(asset_name) and not skip_md_fix:
            try:
                md_output = _default_md_combined_json(asset_name)
                ok = self.run_md_fix(anim_json, md_output)
                result["steps"]["md_fix"] = ok
                if not ok:
                    result["error"] = "MD 动画修复失败"
                    return result
                import_json = md_output
            except Exception as exc:
                result["steps"]["md_fix"] = False
                result["error"] = f"MD 动画修复异常: {exc}"
                return result

        result["import_json"] = import_json

        if not skip_ue_import:
            try:
                ok = self.run_ue_import(
                    anim_json=import_json,
                    target_path=target_ue_path,
                    asset_name=_guess_anim_asset_name_from_output(target_ue_path, asset_name),
                    skeleton_name=self._guess_skeleton_name(model_json or import_json),
                )
                result["steps"]["ue_import"] = ok
                if not ok:
                    result["error"] = "UE 动画导入失败"
                    return result
            except Exception as exc:
                result["steps"]["ue_import"] = False
                result["error"] = f"UE 导入异常: {exc}"
                return result

        result["success"] = True
        return result

    def import_all_anibnd(
        self,
        base_asset: str = "c0000",
        model_json: Optional[str] = None,
        ue_root: Optional[str] = None,
        include_main: bool = True,
        skip_unpack: bool = False,
        skip_extractor: bool = False,
        skip_md_fix: bool = False,
        skip_ue_import: bool = False,
    ) -> dict:
        result = {"success": False, "base_asset": base_asset, "results": [], "error": ""}
        ue_root = ue_root or _default_ue_path(base_asset)
        dirs = _find_anibnd_dirs(base_asset)
        if not dirs:
            result["error"] = f"未找到 {base_asset} 的 anibnd 解包目录"
            return result

        all_success = True
        for entry in dirs:
            if entry["is_main"] and not include_main:
                continue
            sub_path = entry["name"].replace(f"{base_asset}_", "", 1) if entry["name"] != base_asset else "Main"
            sub_result = self.import_animation(
                asset_name=entry["name"],
                anibnd_dir=entry["dir"],
                model_json=model_json,
                anim_json=_default_anim_json(entry["name"]),
                target_ue_path=f"{ue_root}/{sub_path}",
                skip_unpack=skip_unpack,
                skip_extractor=skip_extractor,
                skip_md_fix=skip_md_fix,
                skip_ue_import=skip_ue_import,
            )
            result["results"].append(sub_result)
            if not sub_result["success"]:
                all_success = False

        result["success"] = all_success
        if not all_success:
            result["error"] = "部分 anibnd 包导入失败"
        return result

    def _unpack_anibnd(self, asset_name: str, anibnd_dir: str) -> bool:
        if os.path.isdir(anibnd_dir):
            return True

        yabber = self.config.tool_path("yabber")
        if not yabber or not os.path.exists(yabber):
            print(f"  [错误] Yabber 未找到: {yabber}")
            return False

        game_dir = self.config.game_dir
        anibnd_src = os.path.join(game_dir, "chr", f"{asset_name}.anibnd.dcx")
        if not os.path.exists(anibnd_src):
            print(f"  [错误] anibnd 未找到: {anibnd_src}")
            return False

        extracted_copy = os.path.join(self.config.extracted_dir, f"{asset_name}.anibnd.dcx")
        if not os.path.exists(extracted_copy):
            os.makedirs(os.path.dirname(extracted_copy), exist_ok=True)
            import shutil
            shutil.copy2(anibnd_src, extracted_copy)

        run = subprocess.run([yabber, extracted_copy], capture_output=True, text=True, timeout=180)
        if run.returncode != 0:
            print(run.stderr[-1000:])
            return False
        return os.path.isdir(anibnd_dir)

    def run_anim_extractor(
        self,
        anibnd_dir: str,
        output_json: str,
        model_json: Optional[str] = None,
        sample_rate: int = 30,
        skeleton_hkx: Optional[str] = None,
        filter_name: Optional[str] = None,
    ) -> bool:
        del model_json
        extractor = self.config.tool_path("sekiro_anim_extractor")
        if not extractor or not os.path.exists(extractor):
            print(f"  [错误] SekiroAnimExtractor 未找到: {extractor}")
            return False
        if not os.path.isdir(anibnd_dir):
            print(f"  [错误] anibnd 解包目录不存在: {anibnd_dir}")
            return False

        skeleton_hkx = skeleton_hkx or self._find_skeleton_hkx(anibnd_dir)
        if not skeleton_hkx:
            print(f"  [错误] 未找到 skeleton.hkx: {anibnd_dir}")
            return False

        anim_dir = os.path.dirname(skeleton_hkx)
        cmd = [extractor, skeleton_hkx, anim_dir, output_json, "--sample-rate", str(sample_rate)]
        if filter_name:
            cmd.extend(["--filter", filter_name])

        run = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if run.returncode != 0:
            print(run.stdout[-2000:])
            print(run.stderr[-2000:])
            return False
        if not os.path.exists(output_json):
            print(f"  [错误] 动画 JSON 未生成: {output_json}")
            return False

        tae_ok, tae_logic_json = self._extract_tae_logic(anibnd_dir, output_json)
        if not tae_ok:
            return False

        self._strip_sekiro_prefix(output_json, tae_logic_json)
        return True

    def _extract_tae_logic(self, anibnd_dir: str, animation_json: str) -> tuple[bool, str]:
        """提取同一 anibnd 中的 TAE 引用关系，供逻辑动画别名物化。"""
        has_tae = any(
            file_name.lower().endswith(".tae")
            for _root, _dirs, files in os.walk(anibnd_dir)
            for file_name in files
        )
        if not has_tae:
            return True, ""

        extractor = self.config.tool_path("sekiro_tae_extractor")
        if not extractor or not os.path.exists(extractor):
            print(f"  [错误] SekiroTAEExtractor 未找到: {extractor}")
            return False, ""

        base_name, _extension = os.path.splitext(animation_json)
        tae_logic_json = f"{base_name}_tae.json"
        run = subprocess.run(
            [extractor, anibnd_dir, tae_logic_json],
            capture_output=True,
            text=True,
            timeout=300,
        )
        if run.returncode != 0 or not os.path.exists(tae_logic_json):
            print((run.stdout or "")[-2000:])
            print((run.stderr or "")[-2000:])
            print("  [错误] TAE 动画引用提取失败")
            return False, ""
        return True, tae_logic_json

    def run_md_fix(self, src_json: str, combined_out: str) -> bool:
        fix_script = os.path.join(self.config.scripts_dir, "fix_md_anim_bones.py")
        if not os.path.exists(fix_script):
            print(f"  [错误] MD 修复脚本不存在: {fix_script}")
            return False

        cmd = [
            sys.executable,
            fix_script,
            "--src",
            src_json,
            "--combined-out",
            combined_out,
            "--no-single",
        ]
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if run.returncode != 0:
            print(run.stdout[-2000:])
            print(run.stderr[-2000:])
            return False
        return os.path.exists(combined_out)

    def run_ue_import(
        self,
        anim_json: str,
        target_path: str,
        asset_name: Optional[str] = None,
        skeleton_name: Optional[str] = None,
        anim_names: Optional[list[str]] = None,
    ) -> bool:
        ue_cmd = self.config.get_unreal_editor_cmd()
        if not os.path.exists(ue_cmd):
            print(f"  [错误] UnrealEditor-Cmd.exe 未找到: {ue_cmd}")
            return False

        uproject = os.path.join(self.config.project_dir, "Sekiro.uproject")
        if not os.path.exists(anim_json):
            print(f"  [错误] 动画 JSON 未找到: {anim_json}")
            return False

        skeleton_name = skeleton_name or "Sekiro_Skeleton"
        cmd = [
            ue_cmd,
            uproject,
            "-run=SAImport",
            f"-Anim={anim_json}",
            f"-Output={target_path}",
            f"-Skeleton={skeleton_name}",
            "-unattended",
            "-NoSplash",
            "-NoP4",
        ]
        if asset_name:
            cmd.append(f"-AssetName={asset_name}")
        if anim_names:
            cmd.append(f"-AnimName={','.join(anim_names)}")

        run = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
        if run.returncode != 0:
            output = (run.stdout or "") + "\n" + (run.stderr or "")
            print(output[-4000:])
            return False
        return True

    def _find_skeleton_hkx(self, anibnd_dir: str) -> str:
        for root, _dirs, files in os.walk(anibnd_dir):
            for file_name in files:
                if file_name.lower() == "skeleton.hkx":
                    return os.path.join(root, file_name)
        return ""

    def _strip_sekiro_prefix(self, output_json: str, tae_logic_json: str = "") -> None:
        with open(output_json, "r", encoding="utf-8") as file:
            data = json.load(file)
        changed = False
        bone_names = data.get("BoneNames", [])
        had_internal_root = "Root" in bone_names
        rename_map = normalize_internal_root_bone_names(
            bone_names, data.get("BoneParents", [])
        )
        if rename_map and had_internal_root:
            print("  Normalized internal source bone: Root -> RootPos")
            changed = True
        for anim in data.get("Animations", []):
            name = anim.get("Name", "")
            if name.startswith("Sekiro_"):
                anim["Name"] = name[7:]
                changed = True

        if tae_logic_json:
            with open(tae_logic_json, "r", encoding="utf-8") as file:
                tae_data = json.load(file)
            alias_report = materialize_tae_animation_aliases(data, tae_data)
            materialized = alias_report["materialized"]
            skipped = alias_report["skipped"]
            if materialized:
                print(f"  Materialized {len(materialized)} TAE animation aliases")
                changed = True
            if skipped:
                print(f"  [警告] {len(skipped)} TAE animation aliases could not be materialized")
        if changed:
            with open(output_json, "w", encoding="utf-8") as file:
                json.dump(data, file, indent=2, ensure_ascii=False)

    def _is_md_asset(self, asset_name: str) -> bool:
        return asset_name.endswith("_md") or "_md_" in asset_name

    def _guess_skeleton_name(self, json_path: str) -> str:
        if os.path.exists(json_path):
            try:
                with open(json_path, "r", encoding="utf-8") as file:
                    return json.load(file).get("SkeletonName", "Sekiro_Skeleton")
            except Exception:
                pass
        return "Sekiro_Skeleton"

    def get_animation_summary(self, anim_json: str) -> dict:
        summary = {
            "exists": False,
            "bone_count": 0,
            "animation_count": 0,
            "animation_names": [],
            "categories": {},
        }
        if not os.path.exists(anim_json):
            return summary

        with open(anim_json, "r", encoding="utf-8") as file:
            data = json.load(file)
        animations = data.get("Animations", [])
        summary["exists"] = True
        summary["bone_count"] = len(data.get("BoneNames", []))
        summary["animation_count"] = len(animations)
        for anim in animations:
            name = anim.get("Name", "")
            summary["animation_names"].append(name)
            if "_" in name:
                category = name.split("_", 1)[0]
                summary["categories"][category] = summary["categories"].get(category, 0) + 1
        return summary
