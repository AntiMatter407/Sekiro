#!/usr/bin/env python3
"""
generate_anim_category_map.py — 从 BehaviorParam + TAE JSON 交叉验证生成精确的 CategoryAnimMap

使用方式:
  python generate_anim_category_map.py <BehaviorVariationMap.json> <DirToSekiro_TAE_Logic_JSON(s)> <OutputDir>

流程:
  1. 加载 BehaviorParam variation map
  2. 扫描多个 TAE JSON 文件 (分块，避免9.5GB一次加载)
  3. 对每行按 AnimID 归类，提取 Type=1/2/5 事件的 BehaviorJudgeID
  4. 与 BehaviorParam 交叉验证 → 确定每个 AnimID 的分类
  5. 输出最终分类映射
"""

import json
import sys
import os
from pathlib import Path
from collections import defaultdict
import ijson  # streaming JSON parser

# ---- 分类常量 ----
CATEGORY_NAMES = {
    "Locomotion_Idle":     "Locomotion_Idle",
    "Locomotion_Walk":     "Locomotion_Walk",
    "Locomotion_Jog":      "Locomotion_Jog",
    "Locomotion_Run":      "Locomotion_Run",
    "Locomotion_Sprint":   "Locomotion_Sprint",
    "Locomotion":          "Locomotion",
    "Jump":                "Jump",
    "Combat":              "Combat",
    "Attack":              "Attack",
    "Guard":               "Guard",
    "Deflect":             "Deflect",
    "Mikiri":              "Mikiri",
    "Dodge":               "Dodge",
    "Sweep":               "Sweep",
    "Grab":                "Grab",
    "Defense":             "Defense",
    "Hit":                 "Hit",
    "Knockback":           "Knockback",
    "Death":               "Death",
    "HitReaction":         "HitReaction",
    "Deathblow":           "Deathblow",
    "Resurrection":        "Resurrection",
    "Prosthetic":          "Prosthetic",
    "Grapple":             "Grapple",
    "Item":                "Item",
    "CombatArt":           "CombatArt",
    "Other":               "Other",
}

# AnimID 高位范围 → 默认分组
HIGH_RANGE = [
    (0,          "Locomotion"),
    (20,         "Attack"),
    (30,         "Guard"),
    (40,         "Hit"),
    (50,         "Deathblow"),
    (60,         "Resurrection"),
    (70,         "Prosthetic"),
    (80,         "Grapple"),
    (90,         "Item"),
    (110,        "CombatArt"),
    (120,        "CombatArt"),
]

# 0xxxx 低位细分
ID_0_SUB = [
    (0, 13,     "Locomotion_Idle"),
    (100, 199,  "Locomotion_Walk"),
    (200, 299,  "Locomotion_Jog"),
    (300, 399,  "Locomotion_Run"),
    (400, 499,  "Locomotion_Sprint"),
    (600, 699,  "Jump"),
    (5000,5999, "Combat"),
]

# 30xxxx 低位细分
ID_30_MID = [
    (0, 9,      "Guard"),
    (10, 19,    "Deflect"),
    (20, 29,    "Mikiri"),
    (30, 31,    "Dodge"),
    (32, 39,    "Sweep"),
    (40, 49,    "Grab"),
]

# 40xxxx 低位细分
ID_40_MID = [
    (0, 19,     "Hit"),
    (20, 39,    "Knockback"),
    (40, 59,    "Death"),
]


def infer_category_from_range(anim_id: int) -> str:
    """AnimID 数字范围 fallback 分类"""
    high = anim_id // 10000
    mid = (anim_id // 100) % 100

    if high == 0:
        if anim_id < 14: return "Locomotion_Idle"
        if anim_id < 100: return "Locomotion"
        if 100 <= anim_id < 200: return "Locomotion_Walk"
        if 200 <= anim_id < 300: return "Locomotion_Jog"
        if 300 <= anim_id < 400: return "Locomotion_Run"
        if 400 <= anim_id < 500: return "Locomotion_Sprint"
        if 600 <= anim_id < 700: return "Jump"
        if 5000 <= anim_id < 6000: return "Combat"
        return "Locomotion"
    if high == 20: return "Attack"
    if high == 30:
        for lo, hi, name in ID_30_MID:
            if lo <= mid <= hi: return name
        return "Defense"
    if high == 40:
        for lo, hi, name in ID_40_MID:
            if lo <= mid <= hi: return name
        return "HitReaction"
    if high == 50: return "Deathblow"
    if high == 60: return "Resurrection"
    if high == 70: return "Prosthetic"
    if high == 80: return "Grapple"
    if high == 90: return "Item"
    if high >= 110: return "CombatArt"
    return "Other"


def main():
    var_map_path = sys.argv[1]
    tae_json_path = sys.argv[2]
    out_dir = sys.argv[3]

    # 1. 加载 BehaviorVariationMap
    with open(var_map_path, encoding="utf-8") as f:
        var_data = json.load(f)

    variations = var_data["variations"]

    # 构建 variation_id → primary_ref_type 快速查表
    var_to_reftype = {}
    for vid, v in variations.items():
        var_to_reftype[int(vid)] = v["primary_ref_type"]

    print(f"[1/3] Loaded BehaviorVariationMap: {len(var_to_reftype)} variations")

    # 2. 遍历 TAE JSON（流式解析，避免9.5GB一次加载到内存）
    # TAE JSON 结构:
    # {
    #   "TAE_Files": [ { "Animations": [ { "AnimID": N, "Events": [...] }, ... ] }, ... ]
    # }

    anim_behavior_map = {}  # AnimID → { behavior_judge_ids: set, anim_prefix: str, inferred_category: str }

    print(f"[2/3] Scanning TAE JSON: {tae_json_path}")

    # 用 ijson 流式解析顶层
    total_anims = 0
    anims_with_behavior = 0
    anims_behavior_by_animid = defaultdict(list)  # AnimID → list of behavior events

    # 用 ijson 流式解析
    with open(tae_json_path, "rb") as f:
        # 遍历 TAE_Files 数组
        for tae_file in ijson.items(f, "TAE_Files.item"):
            tae_id = tae_file.get("TAE_ID", -1)
            for anim in tae_file.get("Animations", []):
                anim_id = anim.get("AnimID", 0)
                total_anims += 1
                prefix = tae_file.get("FileName", "a00").replace(".tae", "")

                if anim_id not in anim_behavior_map:
                    anim_behavior_map[anim_id] = {
                        "behavior_judge_ids": set(),
                        "behavior_types": set(),
                        "anim_prefix": prefix,
                    }

                # 提取 Type=1/2/5 事件的 BehaviorJudgeID
                for evt in anim.get("Events", []):
                    etype = evt.get("Type", -1)
                    if etype in (1, 2, 5):
                        params = evt.get("Parameters", {})
                        judge_id = params.get("BehaviorJudgeID", -1)
                        if judge_id >= 0:
                            anim_behavior_map[anim_id]["behavior_judge_ids"].add(judge_id)
                            anim_behavior_map[anim_id]["behavior_types"].add(etype)
                            anims_behavior_by_animid[anim_id].append({
                                "type": etype,
                                "judge_id": judge_id,
                            })

                if anim_id in anims_behavior_by_animid:
                    anims_with_behavior += 1

    print(f"  Total anims scanned: {total_anims}")
    print(f"  Anims with Type=1/2/5 events: {anims_with_behavior}")

    # 3. 交叉验证
    print(f"[3/3] Cross-validating with BehaviorParam...")

    final_categories = {}  # AnimID → { category, source, ref_type }

    for anim_id, info in anim_behavior_map.items():
        judge_ids = info["behavior_judge_ids"]

        if judge_ids:
            # 有 Type=1/2/5 事件 → 查 BehaviorParam
            # 用 BehavioralParam 的 variation_id 覆盖范围
            # variation_id = AnimID / 100
            variation_id = anim_id // 100
            ref_type = var_to_reftype.get(variation_id, None)

            if ref_type:
                # BehaviorParam 覆盖
                if ref_type == "Attack":
                    final_categories[str(anim_id)] = {"category": "Attack", "source": "BP_Attack"}
                elif ref_type == "Bullet":
                    # Bullet → 检查是否主要是义手（70xxxx）
                    high = anim_id // 10000
                    if high == 70:
                        final_categories[str(anim_id)] = {"category": "Prosthetic", "source": "BP_Bullet"}
                    else:
                        final_categories[str(anim_id)] = {"category": "Attack", "source": "BP_Bullet"}
                elif ref_type == "SpEffect":
                    final_categories[str(anim_id)] = {"category": "Hit", "source": "BP_SpEffect"}
                else:
                    final_categories[str(anim_id)] = {"category": infer_category_from_range(anim_id), "source": "Range"}
            else:
                # BehaviorParam 不覆盖 → 用范围
                final_categories[str(anim_id)] = {"category": infer_category_from_range(anim_id), "source": "Range"}

            # 特殊规则：Type=5 (CommonBehavior) 和 JT=8/25/51 (Dodge标记) 的覆盖
            # 这里简化处理
        else:
            # 无 Type=1/2/5 → 范围分类
            final_categories[str(anim_id)] = {"category": infer_category_from_range(anim_id), "source": "Range"}

    # 4. 分组整理
    category_groups = defaultdict(list)
    for anim_id_str, info in sorted(final_categories.items(), key=lambda x: int(x[0])):
        cat = info["category"]
        category_groups[cat].append(int(anim_id_str))

    # 5. 输出结果
    output = {
        "description": "AnimID → Category mapping generated from BehaviorParam + TAE cross-validation",
        "total_anims": total_anims,
        "anims_with_behavior_events": anims_with_behavior,
        "total_categories": len(category_groups),
        "category_stats": {k: len(v) for k, v in sorted(category_groups.items())},
        "category_anim_map": {k: sorted(v) for k, v in sorted(category_groups.items()) if v},
        "anim_category_map": final_categories,
    }

    # 写入 AnimCategoryMap.json
    out_path = Path(out_dir) / "AnimCategoryMap.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

    print(f"\nDone! Output: {out_path}")
    print(f"  Total categories: {len(category_groups)}")
    for cat, ids in sorted(category_groups.items()):
        print(f"  {cat}: {len(ids)} anims  [{min(ids)}-{max(ids)}]")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python generate_anim_category_map.py <BehaviorVariationMap.json> <DirToSekiro_TAE_Logic_JSON(s)> <OutputDir>")
        sys.exit(1)
    main()
