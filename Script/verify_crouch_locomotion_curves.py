# -*- coding: utf-8 -*-
"""逐键验证蹲姿 Locomotion 曲线写入结果。"""

import json
from pathlib import Path

import unreal


PROJECT_ROOT = Path(r"F:/ProjectAI/Sekiro")
PAYLOAD_PATH = PROJECT_ROOT / "Script/temp/set_crouch_locomotion_curves.json"
REPORT_PATH = PROJECT_ROOT / "Script/temp/crouch_locomotion_curve_report.json"
SEMANTIC_CURVES = {
    "CanEnterLoop",
    "CanEnterStop",
    "CanEnterIdle",
    "FootPlant",
    "MovePhase",
    "MoveTransition",
}


def nearly_equal(first, second, tolerance=0.0002):
    return abs(float(first) - float(second)) <= tolerance


def get_curve_names(animation):
    return {
        str(name)
        for name in unreal.AnimationLibrary.get_animation_curve_names(
            animation,
            unreal.RawCurveTrackTypes.RCT_FLOAT,
        )
    }


def main():
    payload = json.loads(PAYLOAD_PATH.read_text(encoding="utf-8"))
    report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    errors = []
    payload_paths = set()

    for animation_row in payload["animations"]:
        payload_paths.add(animation_row["path"])
        animation = unreal.EditorAssetLibrary.load_asset(animation_row["path"])
        if not isinstance(animation, unreal.AnimSequence):
            errors.append("missing_asset=" + animation_row["path"])
            continue

        actual_names = get_curve_names(animation)
        if "MoveTransition" in actual_names:
            errors.append("legacy_curve=" + animation_row["path"])

        for curve_row in animation_row["curves"]:
            curve_name = curve_row["name"]
            if curve_name not in actual_names:
                errors.append("missing_curve={0}:{1}".format(animation_row["path"], curve_name))
                continue

            actual_times, actual_values = unreal.AnimationLibrary.get_float_keys(animation, curve_name)
            expected_keys = curve_row["keys"]
            if len(actual_times) != len(expected_keys):
                errors.append("key_count={0}:{1}".format(animation_row["path"], curve_name))
                continue

            for index, expected_key in enumerate(expected_keys):
                if not nearly_equal(actual_times[index], expected_key["time"]):
                    errors.append("key_time={0}:{1}:{2}".format(animation_row["path"], curve_name, index))
                if not nearly_equal(actual_values[index], expected_key["value"]):
                    errors.append("key_value={0}:{1}:{2}".format(animation_row["path"], curve_name, index))

    for asset_row in report["assets"]:
        if asset_row["target"] in payload_paths:
            continue

        animation = unreal.EditorAssetLibrary.load_asset(asset_row["target"])
        unexpected_names = get_curve_names(animation).intersection(SEMANTIC_CURVES)
        if unexpected_names:
            errors.append("unexpected_curves={0}:{1}".format(
                asset_row["target"],
                sorted(unexpected_names),
            ))

    print("verified_assets={0}".format(len(report["assets"])))
    print("verified_curve_assets={0}".format(len(payload["animations"])))
    print("verified_curves={0}".format(sum(len(row["curves"]) for row in payload["animations"])))
    print("errors={0}".format(errors))


main()
