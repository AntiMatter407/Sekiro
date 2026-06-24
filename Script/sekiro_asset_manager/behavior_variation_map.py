#!/usr/bin/env python3
"""
behavior_variation_map.py — 从 BehaviorParam_PC 生成 AnimID 行为分类映射

输入: Output/BehaviorParam_PC.json (ParamReader 输出)
输出: Output/BehaviorVariationMap.json

编码规则（已验证）:
  variation_id = AnimID / 100
  Row.ID = 100000000 + variation_id * 1000 + behaviorJudgeID

用途:
  替代 SATAEImporter::InferCategoryFromAnimID() 的硬编码数字范围，
  用 FromSoftware 官方 BehaviorParam 数据做权威分类。
"""

import json
import sys
from pathlib import Path
from collections import defaultdict

# ── 路径 ──────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent  # Sekiro/
DEFAULT_INPUT = REPO_ROOT / "Output" / "BehaviorParam_PC.json"
DEFAULT_OUTPUT = REPO_ROOT / "Output" / "BehaviorVariationMap.json"

# ── BehaviorParam category → 行为大类映射 ──────────────
CATEGORY_NAME = {
    0: "Behavior_Common",
    1: "Behavior_Attack",
    2: "Behavior_Projectile",
    5: "Behavior_System",
    9: "Behavior_Special",
}

# ── RefType → 名称 ────────────────────────────────────
REF_TYPE_NAME = {0: "Attack", 1: "Bullet", 2: "SpEffect"}

# ── HighPart → 默认分类（来自 InferCategoryFromAnimID 的规则） ──
HIGH_PART_DEFAULT = {
    0: "Locomotion",
}

# 30xxxx 子分类（MidPart 范围）
MID_PART_30 = [
    (0, 9, "Guard"),
    (10, 19, "Deflect"),
    (20, 29, "Mikiri"),
    (30, 31, "Dodge"),
    (31, 32, "Dodge"),  # Dodge 方向细分在子脚本处理
    (32, 39, "Sweep"),
    (40, 49, "Grab"),
]

# 40xxxx 子分类
MID_PART_40 = [
    (0, 19, "Hit"),
    (20, 39, "Knockback"),
    (40, 59, "Death"),
]


def get_range_category(anim_id: int) -> str:
    """旧版 InferCategoryFromAnimID 的逻辑，作为 fallback"""
    high = anim_id // 10000
    mid = (anim_id // 100) % 100

    if high == 0:
        if anim_id < 14:
            return "Locomotion_Idle"
        if anim_id < 100:
            return "Locomotion"
        if anim_id < 200:
            return f"Locomotion_Walk{_dir_suffix(anim_id)}"
        if anim_id < 300:
            return f"Locomotion_Jog{_dir_suffix(anim_id)}"
        if anim_id < 400:
            return f"Locomotion_Run{_dir_suffix(anim_id)}"
        if anim_id < 500:
            return f"Locomotion_Sprint{_dir_suffix(anim_id)}"
        if anim_id < 600:
            return "Locomotion"
        if anim_id < 700:
            return f"Jump{_dir_suffix(anim_id)}"
        if anim_id < 1000:
            return "Locomotion"
        if 5000 <= anim_id < 6000:
            return "Combat"
        return "Locomotion"

    if 20 <= high < 30:
        return "Attack"
    if 30 <= high < 40:
        for lo, hi, name in MID_PART_30:
            if lo <= mid <= hi:
                return name
        return "Defense"
    if 40 <= high < 50:
        for lo, hi, name in MID_PART_40:
            if lo <= mid <= hi:
                return name
        return "HitReaction"
    if 50 <= high < 60:
        return "Deathblow"
    if 60 <= high < 70:
        return "Resurrection"
    if 70 <= high < 80:
        return "Prosthetic"
    if 80 <= high < 90:
        return "Grapple"
    if 90 <= high < 100:
        return "Item"
    if 110 <= high < 120 or 121 <= high < 130:
        return "CombatArt"
    if high >= 100:
        return "CombatArt"

    return "Other"


def _dir_suffix(anim_id: int) -> str:
    """方向后缀：个位数 0=Fwd, 1=Bwd, 2=L, 3=R"""
    d = anim_id % 10
    return {0: "_Fwd", 1: "_Bwd", 2: "_L", 3: "_R"}.get(d, "")


def build_variation_map(bp_path: Path) -> dict:
    """构建 variation_id → 行为配置映射"""
    with open(bp_path, "r", encoding="utf-8") as f:
        bp_data = json.load(f)

    variations = defaultdict(list)
    for entry in bp_data:
        vid = entry["variation_id"]
        variations[vid].append(entry)

    result = {}
    for vid, entries in sorted(variations.items()):
        anim_lo = vid * 100
        anim_hi = vid * 100 + 99
        high = vid // 100  # AnimID / 10000

        # 统计该 variation 的行为类型
        ref_types = defaultdict(int)
        categories = defaultdict(int)
        for e in entries:
            ref_types[e["ref_type_name"]] += 1
            categories[e["category"]] += 1

        # 主要 RefType
        primary_ref = max(ref_types, key=ref_types.get) if ref_types else "None"
        # 主要 category
        primary_cat = max(categories, key=categories.get) if categories else -1

        # 获取范围分类作为参考
        sample_anim = anim_lo if anim_lo > 0 else anim_lo + 1
        range_cat = get_range_category(sample_anim)

        result[str(vid)] = {
            "variation_id": vid,
            "anim_range": [anim_lo, anim_hi],
            "high_part": high,
            "entry_count": len(entries),
            "primary_ref_type": primary_ref,
            "bp_category": primary_cat,
            "bp_category_name": CATEGORY_NAME.get(primary_cat, f"Unknown({primary_cat})"),
            "range_category": range_cat,
            "ref_type_dist": dict(ref_types),
            "category_dist": {str(k): v for k, v in categories.items()},
            "behaviors": [
                {
                    "judge_id": e["judge_id"],
                    "ref_type": e["ref_type_name"],
                    "ref_id": e["ref_id"],
                    "sfx_id": e["sfx_id"],
                    "stamina": e["stamina"],
                    "mp": e["mp"],
                }
                for e in entries
            ],
        }

    return result


def build_anim_lookup(variations: dict) -> dict:
    """构建 AnimID → 分类 的扁平查找表（仅 BehaviorParam 覆盖的 AnimID）"""
    lookup = {}
    for vid_str, var in variations.items():
        for aid in range(var["anim_range"][0], var["anim_range"][1] + 1):
            lookup[str(aid)] = {
                "variation_id": var["variation_id"],
                "primary_ref_type": var["primary_ref_type"],
                "bp_category": var["bp_category"],
                "bp_category_name": var["bp_category_name"],
            }
    return lookup


def main():
    bp_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_INPUT
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUTPUT

    if not bp_path.exists():
        print(f"Error: {bp_path} not found. Run ParamReader first.")
        sys.exit(1)

    variations = build_variation_map(bp_path)
    anim_lookup = build_anim_lookup(variations)

    output = {
        "description": "BehaviorVariationID → AnimID 行为分类映射",
        "source": "BehaviorParam_PC.param (FromSoftware official behavior config)",
        "encoding_rules": {
            "variation_id": "AnimID / 100 (integer division)",
            "row_id": "100000000 + variation_id * 1000 + behaviorJudgeID",
        },
        "total_variations": len(variations),
        "covered_anim_ids": len(anim_lookup),
        "variations": variations,
        "anim_lookup": anim_lookup,
        # 默认分类规则（供 C++ fallback 使用）
        "default_rules": {
            "high_part": {
                "0": "Locomotion (细分见 mid_part_rules_0)",
                "20-29": "Attack",
                "30-39": "Guard/Deflect/Mikiri/Dodge (见 mid_part_rules_30)",
                "40-49": "Hit/Knockback/Death (见 mid_part_rules_40)",
                "50-59": "Deathblow",
                "60-69": "Resurrection",
                "70-79": "Prosthetic",
                "80-89": "Grapple",
                "90-99": "Item",
                "110-129": "CombatArt",
            },
            "mid_part_rules_0": [
                {"id_range": [0, 13], "category": "Locomotion_Idle"},
                {"id_range": [14, 99], "category": "Locomotion"},
                {"id_range": [100, 199], "category": "Locomotion_Walk"},
                {"id_range": [200, 299], "category": "Locomotion_Jog"},
                {"id_range": [300, 399], "category": "Locomotion_Run"},
                {"id_range": [400, 499], "category": "Locomotion_Sprint"},
                {"id_range": [500, 599], "category": "Locomotion"},
                {"id_range": [600, 699], "category": "Jump"},
                {"id_range": [700, 999], "category": "Locomotion"},
                {"id_range": [5000, 5999], "category": "Combat"},
            ],
            "mid_part_rules_30": [
                {"mid_range": [0, 9], "category": "Guard"},
                {"mid_range": [10, 19], "category": "Deflect"},
                {"mid_range": [20, 29], "category": "Mikiri"},
                {"mid_range": [30, 31], "category": "Dodge"},
                {"mid_range": [32, 39], "category": "Sweep"},
                {"mid_range": [40, 49], "category": "Grab"},
            ],
            "mid_part_rules_40": [
                {"mid_range": [0, 19], "category": "Hit"},
                {"mid_range": [20, 39], "category": "Knockback"},
                {"mid_range": [40, 59], "category": "Death"},
            ],
        },
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

    print(f"Done: {len(variations)} variations, {len(anim_lookup)} AnimIDs → {out_path}")

    # 统计
    by_ref = defaultdict(int)
    for v in variations.values():
        by_ref[v["primary_ref_type"]] += 1
    print(f"  By primary RefType: {dict(by_ref)}")

    by_bp_cat = defaultdict(int)
    for v in variations.values():
        by_bp_cat[v["bp_category_name"]] += 1
    print(f"  By BP Category: {dict(by_bp_cat)}")


if __name__ == "__main__":
    main()
