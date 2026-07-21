# -*- coding: utf-8 -*-
"""按站姿 Locomotion 曲线的归一化时间生成对应蹲姿曲线。"""

from __future__ import annotations

import json
from pathlib import Path

import unreal


PROJECT_ROOT = Path(r"F:/ProjectAI/Sekiro")
PAYLOAD_PATH = PROJECT_ROOT / "Script/temp/set_crouch_locomotion_curves.json"
REPORT_PATH = PROJECT_ROOT / "Script/temp/crouch_locomotion_curve_report.json"
SOURCE_PAYLOAD_PATH = PROJECT_ROOT / "Script/temp/set_locomotion_move_curves.json"
ANIMATION_ROOT = "/Game/Characters/Sekiro/Animations"

CURVE_TYPES = {
    "CanEnterLoop": "int",
    "CanEnterStop": "int",
    "CanEnterIdle": "int",
    "FootPlant": "int",
    "MovePhase": "float",
}

DIRECTIONS = ("Forward", "Back", "Left", "Right")
DIRECTION_SUFFIXES = ("0", "1", "2", "3")


def animation_path(animation_id: str) -> str:
    asset_name = "Anim_Sekiro_a000_{0}".format(animation_id)
    return "{0}/{1}.{1}".format(ANIMATION_ROOT, asset_name)


def build_entries():
    entries = [
        {"alias": "Crouch_Idle", "target": "005000", "source": None},
    ]

    turn_names = ("Forward", "Back", "Left", "Right")
    for direction, suffix in zip(turn_names, DIRECTION_SUFFIXES):
        entries.append({
            "alias": "Crouch_Idle_{0}_Turn".format(direction),
            "target": "00501" + suffix,
            "source": None,
        })

    groups = (
        ("Crouch_Walk_{0}_Start", "00510", "00010"),
        ("Crouch_Walk_{0}_Start_2", "00511", "00010"),
        ("Crouch_Walk_{0}_Loop", "00520", "00020"),
        ("Crouch_Walk_{0}_Stop", "00530", "00030"),
        ("Crouch_Run_{0}_Start", "00540", "00040"),
        ("Crouch_Run_{0}_Start_2", "00541", "00040"),
        ("Crouch_Run_{0}_Loop", "00550", "00050"),
        ("Crouch_Run_{0}_Stop", "00560", "00060"),
    )
    for alias_pattern, target_prefix, source_prefix in groups:
        for direction, suffix in zip(DIRECTIONS, DIRECTION_SUFFIXES):
            entries.append({
                "alias": alias_pattern.format(direction),
                "target": target_prefix + suffix,
                "source": source_prefix + suffix,
            })

    return entries


def get_play_length(animation) -> float:
    return max(float(animation.get_play_length()), 0.0)


def scale_curve_keys(source_keys, source_length: float, target_length: float):
    if source_length <= 0.0 or target_length <= 0.0:
        return []

    result = []
    for source_key in source_keys:
        normalized_time = max(
            0.0,
            min(float(source_key["time"]) / source_length, 1.0),
        )
        result.append({
            "time": round(normalized_time * target_length, 4),
            "value": float(source_key["value"]),
        })
    return result


def ensure_terminal_constant_key(keys, target_length: float):
    """UE 的末尾阶梯键没有后续区间，必须在动画末尾重复一次最终值。"""
    if not keys:
        return keys

    if abs(keys[-1]["time"] - target_length) < 0.0001:
        if len(keys) < 2 or keys[-1]["value"] == keys[-2]["value"]:
            return keys

        # 动画结束点的孤立状态变化没有可持续区间，UE 会保留前一段值；生成时显式归一化成同样语义。
        keys.pop()

    keys.append({
        "time": round(target_length, 4),
        "value": keys[-1]["value"],
    })
    return keys


def main():
    with SOURCE_PAYLOAD_PATH.open("r", encoding="utf-8") as handle:
        source_payload = json.load(handle)
    source_curves_by_path = {
        animation["path"]: animation.get("curves", [])
        for animation in source_payload.get("animations", [])
    }

    payload = {
        "action": "set_anim_curves",
        "path": ANIMATION_ROOT,
        "dry_run": False,
        "save": True,
        "animations": [],
    }
    report = {
        "method": "normalized_key_copy_from_standing_locomotion",
        "assets": [],
        "missing_assets": [],
    }

    for entry in build_entries():
        target_path = animation_path(entry["target"])
        target_animation = unreal.EditorAssetLibrary.load_asset(target_path)
        if not isinstance(target_animation, unreal.AnimSequence):
            report["missing_assets"].append(target_path)
            continue

        asset_report = {
            "alias": entry["alias"],
            "target": target_path,
            "source": None,
            "curves": [],
        }
        if entry["source"] is None:
            asset_report["notes"] = ["no semantic locomotion curve required"]
            report["assets"].append(asset_report)
            continue

        source_path = animation_path(entry["source"])
        source_animation = unreal.EditorAssetLibrary.load_asset(source_path)
        if not isinstance(source_animation, unreal.AnimSequence):
            report["missing_assets"].append(source_path)
            report["assets"].append(asset_report)
            continue

        source_length = get_play_length(source_animation)
        target_length = get_play_length(target_animation)
        curves = []
        for source_curve in source_curves_by_path.get(source_path, []):
            curve_name = source_curve.get("name")
            if curve_name not in CURVE_TYPES:
                continue

            curve_type = CURVE_TYPES[curve_name]
            target_keys = scale_curve_keys(
                source_curve.get("keys", []),
                source_length,
                target_length,
            )
            if curve_type == "int":
                target_keys = ensure_terminal_constant_key(target_keys, target_length)
            if target_keys:
                curves.append({
                    "name": curve_name,
                    "curve_type": curve_type,
                    "keys": target_keys,
                })

        asset_report["source"] = source_path
        asset_report["source_length"] = round(source_length, 4)
        asset_report["target_length"] = round(target_length, 4)
        asset_report["curves"] = [curve["name"] for curve in curves]
        report["assets"].append(asset_report)
        if curves:
            payload["animations"].append({
                "path": target_path,
                "curves": curves,
            })

    PAYLOAD_PATH.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    REPORT_PATH.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    unreal.log("[CrouchLocomotionCurves] assets={0} payload={1} missing={2}".format(
        len(report["assets"]),
        len(payload["animations"]),
        len(report["missing_assets"]),
    ))


main()
