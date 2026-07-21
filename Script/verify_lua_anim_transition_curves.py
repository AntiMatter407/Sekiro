# -*- coding: utf-8 -*-
"""验证 Lua 动画蓝图实际引用的 Sequence 是否具备有效 Transition 语义曲线。"""

import json
import re
from collections import Counter, defaultdict
from pathlib import Path

import unreal


PROJECT_ROOT = Path(r"F:/ProjectAI/Sekiro")
ANIM_ASSETS_PATH = PROJECT_ROOT / "Content/Script/Animation/Sekiro/AnimAssets.lua"
CURVE_THRESHOLD = 0.5


def parse_anim_assets():
    """从集中资产表读取别名与对象路径，保持验证范围和 Lua 声明一致。"""
    text = ANIM_ASSETS_PATH.read_text(encoding="utf-8-sig")
    pattern = re.compile(
        r'^\s*(?P<alias>[A-Za-z0-9_]+)\s*=\s*"(?P<path>/Game/Characters/Sekiro/Animations/[^\"]+)"',
        re.MULTILINE,
    )
    return [match.groupdict() for match in pattern.finditer(text)]


def required_curves(alias):
    """返回当前状态图对指定语义别名要求的曲线集合。"""
    required = set()
    if re.match(r"^(Walk|Run)_(Forward|Back|Left|Right)_Start$", alias):
        required.update(("CanEnterLoop", "CanEnterStop"))
    elif re.match(r"^IdleTo(Walk|Run)_(Left|Right)_Turn$", alias):
        required.update(("CanEnterLoop", "CanEnterStop"))
    elif re.match(r"^Crouch_(Walk|Run)_(Forward|Back|Left|Right)_Start$", alias):
        required.update(("CanEnterLoop", "CanEnterStop"))
    elif re.match(r"^Sprint_.+_Start$", alias):
        required.update(("CanEnterLoop", "CanEnterStop"))

    if re.match(r"^(Walk|Run)_(Forward|Back|Left|Right)_Loop$", alias):
        required.add("CanEnterStop")
    elif re.match(r"^Crouch_(Walk|Run)_(Forward|Back|Left|Right)_Loop$", alias):
        required.add("CanEnterStop")
    elif alias == "Sprint_Forward_Loop":
        required.add("CanEnterStop")

    if re.match(r"^(Walk|Run)_(Forward|Back|Left|Right)_Stop$", alias):
        required.add("CanEnterIdle")
    elif re.match(r"^Crouch_(Walk|Run)_(Forward|Back|Left|Right)_Stop$", alias):
        required.add("CanEnterIdle")
    elif re.match(r"^Sprint_.+_Stop$", alias):
        required.add("CanEnterIdle")

    if alias.startswith("Step_"):
        required.add("CanExitStep")
    elif alias in (
            "Idle_Left_Turn",
            "Idle_Right_Turn",
            "Crouch_Idle_Left_Turn",
            "Crouch_Idle_Right_Turn"):
        required.add("CanExitTurn")
    elif alias.startswith("Jump_Start_"):
        required.add("CanEnterInAir")
    elif alias.startswith("Jump_InAir_") or alias in (
            "Stand_Jump_Start",
            "Crouch_Jump_Start",
            "Jump_Unlock_Forward_Start"):
        required.add("CanEnterLoop")
    elif alias.startswith("Jump_Land_") or alias == "Jump_Light_Stand":
        required.update(("CanResumeMovement", "CanExitLand"))

    return required


def get_curve_names(animation):
    """读取动画上的浮点曲线名称；UE 的 int bool 曲线也存放在该 Track 类型中。"""
    return {
        str(name)
        for name in unreal.AnimationLibrary.get_animation_curve_names(
            animation,
            unreal.RawCurveTrackTypes.RCT_FLOAT,
        )
    }


def main():
    """逐资产验证曲线存在性和值域，并以非零错误列表阻止错误资产进入 ABP。"""
    requirements_by_path = defaultdict(set)
    aliases_by_path = defaultdict(list)
    for row in parse_anim_assets():
        required = required_curves(row["alias"])
        if not required:
            continue
        requirements_by_path[row["path"]].update(required)
        aliases_by_path[row["path"]].append(row["alias"])

    errors = []
    verified_curve_counts = Counter()
    for asset_path, required in sorted(requirements_by_path.items()):
        animation = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(animation, unreal.AnimSequence):
            errors.append("missing_asset={0}".format(asset_path))
            continue

        actual_names = get_curve_names(animation)
        curve_keys = {}
        for curve_name in sorted(required):
            if curve_name not in actual_names:
                errors.append("missing_curve={0}:{1}".format(asset_path, curve_name))
                continue

            times, values = unreal.AnimationLibrary.get_float_keys(animation, curve_name)
            if not any(float(value) >= CURVE_THRESHOLD for value in values):
                errors.append("never_allowed={0}:{1}".format(asset_path, curve_name))
                continue
            curve_keys[curve_name] = list(zip(times, values))
            verified_curve_counts[curve_name] += 1

        if "CanEnterLoop" in required and "CanEnterStop" in required \
                and "CanEnterLoop" in curve_keys and "CanEnterStop" in curve_keys:
            loop_open_time = min(
                float(time)
                for time, value in curve_keys["CanEnterLoop"]
                if float(value) >= CURVE_THRESHOLD
            )
            stop_allowed_times = [
                float(time)
                for time, value in curve_keys["CanEnterStop"]
                if float(value) >= CURVE_THRESHOLD
            ]
            if max(stop_allowed_times) + 0.0001 < loop_open_time:
                errors.append("stop_window_ends_before_loop={0}".format(asset_path))
            elif float(curve_keys["CanEnterStop"][-1][1]) < CURVE_THRESHOLD:
                errors.append("stop_tail_blocked={0}".format(asset_path))

        if any(alias.endswith("_Stop") for alias in aliases_by_path[asset_path]) \
                and "CanEnterLoop" in actual_names:
            errors.append("unexpected_curve={0}:CanEnterLoop".format(asset_path))

    result = {
        "verified_assets": len(requirements_by_path),
        "verified_curves": dict(sorted(verified_curve_counts.items())),
        "errors": errors,
    }
    print(json.dumps(result, ensure_ascii=False))
    if errors:
        raise RuntimeError(json.dumps(result, ensure_ascii=False))


main()
