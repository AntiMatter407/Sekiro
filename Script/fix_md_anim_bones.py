"""
?? Sekiro MD?????? JSON?

???MD ?? HKX ? BoneTransforms ???????? local delta?UE ?????
?? local pose???? delta ????? local pose ???????????????????

?????? a000_100000~199999 ? MD ???
    full_pose = reference_local * delta_local

???????? SekiroAnimExtractor ????
    Output/c0000/Animation/Sekiro_a000_md_reextract_delta.json

???
    Output/c0000/Animation/MD_Anims/Anim_Sekiro_a000_<id>.json
    Output/c0000/Animation/Sekiro_a000_md_fullpose_combined.json
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import os
import shutil
import time
from copy import deepcopy
from typing import Any

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(PROJECT_DIR, "Output", "c0000", "Animation", "Sekiro_a000_md_reextract_delta.json")
DEFAULT_OUT_DIR = os.path.join(PROJECT_DIR, "Output", "c0000", "Animation", "MD_Anims")
DEFAULT_COMBINED_OUT = os.path.join(PROJECT_DIR, "Output", "c0000", "Animation", "Sekiro_a000_md_fullpose_combined.json")
DEFAULT_BACKUP_ROOT = os.path.join(PROJECT_DIR, "Script", "temp")

MD_ANIM_RANGE = (100000, 199999)
RAW_DELTA_POSITION_RATIO = 0.35
RAW_DELTA_POSITION_EPS = 1e-4


def quat_normalize(q: list[float]) -> list[float]:
    length = math.sqrt(sum(v * v for v in q))
    if length <= 1e-8:
        return [0.0, 0.0, 0.0, 1.0]
    return [v / length for v in q]


def quat_multiply(a: list[float], b: list[float]) -> list[float]:
    """Return a * b for quaternions stored as [x, y, z, w]."""
    ax, ay, az, aw = quat_normalize([float(v) for v in a])
    bx, by, bz, bw = quat_normalize([float(v) for v in b])
    return quat_normalize([
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ])


def vec_len(v: list[Any]) -> float:
    return math.sqrt(sum(float(x) * float(x) for x in v))


def rounded(values: list[float], digits: int = 6) -> list[float]:
    return [round(float(v), digits) for v in values]


def parse_anim_id(name: str) -> int | None:
    try:
        return int(name.rsplit("_", 1)[-1])
    except (ValueError, IndexError):
        return None


def normalize_anim_name(name: str) -> str:
    return name if name.startswith("Sekiro_") else f"Sekiro_{name}"


def is_md_anim(anim: dict[str, Any]) -> bool:
    anim_id = parse_anim_id(normalize_anim_name(anim.get("Name", "")))
    return anim_id is not None and MD_ANIM_RANGE[0] <= anim_id <= MD_ANIM_RANGE[1]


def looks_like_raw_delta(anim: dict[str, Any], ref_locals: list[dict[str, Any]]) -> bool:
    """Detect whether position channels still look like MD deltas instead of full local pose."""
    frames = anim.get("Frames") or []
    if not frames:
        return False
    transforms = frames[0].get("BoneTransforms") or []
    checked = 0
    delta_like = 0
    for bone_idx, ref in enumerate(ref_locals[:len(transforms)]):
        ref_p = ref.get("P", [0, 0, 0])
        ref_len = vec_len(ref_p)
        if ref_len < 0.02:
            continue
        anim_p = transforms[bone_idx].get("P", [0, 0, 0])
        checked += 1
        if vec_len(anim_p) <= max(RAW_DELTA_POSITION_EPS, ref_len * RAW_DELTA_POSITION_RATIO):
            delta_like += 1
    return checked > 0 and (delta_like / checked) > 0.5


def compose_ref_delta(anim: dict[str, Any], ref_locals: list[dict[str, Any]]) -> dict[str, Any]:
    """Convert one MD animation from local delta to full local pose."""
    out_anim = deepcopy(anim)
    out_anim["Name"] = normalize_anim_name(out_anim.get("Name", ""))

    for frame in out_anim.get("Frames", []):
        bone_transforms = frame.get("BoneTransforms", [])
        for bone_idx, bone_transform in enumerate(bone_transforms[:len(ref_locals)]):
            ref = ref_locals[bone_idx]
            ref_p = ref.get("P", [0, 0, 0])
            ref_r = ref.get("R", [0, 0, 0, 1])
            ref_s = ref.get("S", [1, 1, 1])
            delta_p = bone_transform.get("P", [0, 0, 0])
            delta_r = bone_transform.get("R", [0, 0, 0, 1])
            delta_s = bone_transform.get("S", [1, 1, 1])

            bone_transform["P"] = rounded([float(ref_p[i]) + float(delta_p[i]) for i in range(3)])
            bone_transform["R"] = rounded(quat_multiply(ref_r, delta_r))
            bone_transform["S"] = rounded([float(ref_s[i]) * float(delta_s[i]) for i in range(3)])

    return out_anim


def build_single_json(root: dict[str, Any], anim: dict[str, Any]) -> dict[str, Any]:
    return {
        "AssetName": root.get("AssetName", "Sekiro"),
        "OriginalAssetName": root.get("OriginalAssetName", "c0000"),
        "SkeletonName": root.get("SkeletonName", "Sekiro_Skeleton"),
        "BoneNames": root["BoneNames"],
        "BoneParents": root["BoneParents"],
        "BoneLocalTransforms": root["BoneLocalTransforms"],
        "Animations": [anim],
    }


def backup_existing_outputs(out_dir: str, backup_root: str) -> str | None:
    existing = glob.glob(os.path.join(out_dir, "Anim_Sekiro_a000_*.json"))
    if not existing:
        return None
    backup_dir = os.path.join(backup_root, "md_backup_before_ref_delta_" + time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(backup_dir, exist_ok=True)
    for src in existing:
        shutil.copy2(src, os.path.join(backup_dir, os.path.basename(src)))
    return backup_dir


def fix_md_animations(src: str, out_dir: str, combined_out: str, write_single: bool, write_combined: bool, backup: bool) -> bool:
    if not os.path.exists(src):
        print(f"[??] ?? JSON ???: {src}")
        return False

    with open(src, "r", encoding="utf-8") as f:
        data = json.load(f)

    bone_names = data.get("BoneNames", [])
    ref_locals = data.get("BoneLocalTransforms", [])
    animations = [anim for anim in data.get("Animations", []) if is_md_anim(anim)]
    if not bone_names or not ref_locals:
        print("[??] ?? JSON ?? BoneNames ? BoneLocalTransforms")
        return False
    if not animations:
        print("[??] ??? MD ???? a000_100000~199999")
        return False

    raw_delta = looks_like_raw_delta(animations[0], ref_locals)
    print(f"??: {src}")
    print(f"???: {len(bone_names)}")
    print(f"MD ???: {len(animations)}")
    print(f"???????: {'raw delta??? ref*delta' if raw_delta else 'full local pose??? ref*delta'}")

    fixed_anims = [compose_ref_delta(anim, ref_locals) if raw_delta else deepcopy(anim) for anim in animations]
    for anim in fixed_anims:
        anim["Name"] = normalize_anim_name(anim.get("Name", ""))

    if backup and write_single:
        backup_dir = backup_existing_outputs(out_dir, DEFAULT_BACKUP_ROOT)
        if backup_dir:
            print(f"??????? JSON: {backup_dir}")

    if write_single:
        os.makedirs(out_dir, exist_ok=True)
        for anim in fixed_anims:
            anim_id = parse_anim_id(anim["Name"])
            if anim_id is None:
                continue
            out_path = os.path.join(out_dir, f"Anim_Sekiro_a000_{anim_id:06d}.json")
            with open(out_path, "w", encoding="utf-8") as f:
                json.dump(build_single_json(data, anim), f, separators=(",", ":"))
        print(f"?????? JSON: {out_dir}")

    if write_combined:
        combined = {
            "AssetName": data.get("AssetName", "Sekiro"),
            "OriginalAssetName": data.get("OriginalAssetName", "c0000"),
            "SkeletonName": data.get("SkeletonName", "Sekiro_Skeleton"),
            "BoneNames": data["BoneNames"],
            "BoneParents": data["BoneParents"],
            "BoneLocalTransforms": data["BoneLocalTransforms"],
            "Animations": fixed_anims,
        }
        os.makedirs(os.path.dirname(combined_out), exist_ok=True)
        with open(combined_out, "w", encoding="utf-8") as f:
            json.dump(combined, f, separators=(",", ":"))
        print(f"????? JSON: {combined_out}")

    print("???????????? SAImport ???? JSON???????? Commandlet?")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="?? Sekiro MD ???? delta ??")
    parser.add_argument("--src", default=DEFAULT_SRC, help="SekiroAnimExtractor ??? MD delta JSON")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR, help="??? JSON ????")
    parser.add_argument("--combined-out", default=DEFAULT_COMBINED_OUT, help="?? JSON ????")
    parser.add_argument("--no-single", action="store_true", help="?????? JSON")
    parser.add_argument("--no-combined", action="store_true", help="????? JSON")
    parser.add_argument("--no-backup", action="store_true", help="????? JSON ????")
    args = parser.parse_args()

    ok = fix_md_animations(
        src=args.src,
        out_dir=args.out_dir,
        combined_out=args.combined_out,
        write_single=not args.no_single,
        write_combined=not args.no_combined,
        backup=not args.no_backup,
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
