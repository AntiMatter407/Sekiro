#!/usr/bin/env python3
"""
extract_anim_behavior.py — 从 TAE 二进制提取 Type=1/2/5 事件的 BehaviorJudgeID，
然后与 BehaviorParam 交叉验证生成 AnimID 分类。

用法:
    python extract_anim_behavior.py <tae_directory> <BehaviorVariationMap.json> <output.json>

输出: AnimID→Category 分类映射 + Stats
"""

import json, os, sys, struct
from collections import defaultdict


def read_u64(data, pos): return struct.unpack_from('<Q', data, pos)[0], pos + 8
def read_u32(data, pos): return struct.unpack_from('<I', data, pos)[0], pos + 4
def read_f32(data, pos): return struct.unpack_from('<f', data, pos)[0], pos + 4


def parse_tae_events(d, entry_off):
    """解析单个动画的事件表"""
    p = entry_off
    if p + 32 > len(d): return []
    # 快速合理性检查
    ev_off_table_raw = struct.unpack_from('<Q', d, p)[0]
    if ev_off_table_raw > len(d) or ev_off_table_raw < 0x100:
        return []

    if p + 32 > len(d): return []
    ev_off_table, p = read_u64(d, p)
    _, p = read_u64(d, p)
    _, p = read_u64(d, p)
    _, p = read_u64(d, p)
    if p + 12 > len(d): return []
    ev_count, p = read_u32(d, p)
    _, p = read_u32(d, p)
    _, p = read_u32(d, p)
    p += 4

    events = []
    if ev_count == 0 or ev_count > 200: return events  # 单动画事件数不会超过200
    if ev_off_table < 0x100 or ev_off_table >= len(d): return events
    if ev_off_table + ev_count * 8 > len(d): return events

    for i in range(ev_count):
        off_pos = ev_off_table + i * 8
        if off_pos + 8 > len(d): break
        ev_data_off, _ = read_u64(d, off_pos)
        if ev_data_off == 0 or ev_data_off >= len(d): continue
        if ev_data_off < 0x100: continue
        # 检查 event data 范围
        if ev_data_off + 24 > len(d): continue

        ep = ev_data_off
        ev_type, ep = read_u32(d, ep)
        if ev_type > 2000: continue
        start_time, ep = read_f32(d, ep)
        end_time, ep = read_f32(d, ep)
        if start_time != start_time or end_time != end_time: continue
        if abs(start_time) > 10000 or abs(end_time) > 10000: continue

        param_raw = d[ep:ep + 16]

        params = {}
        if ev_type == 0 and len(param_raw) >= 4:
            jt = struct.unpack_from('<I', param_raw, 0)[0]
            if jt >= 0 and jt <= 300:  # JT ID 在合理范围
                params['JumpTableID'] = jt
        elif ev_type in (1, 2, 5) and len(param_raw) >= 12:
            bid = struct.unpack_from('<I', param_raw, 8)[0]
            if bid >= 0 and bid <= 9999:  # BehaviorJudgeID 在合理范围
                params['BehaviorJudgeID'] = bid

        events.append({
            'Type': ev_type,
            'StartFrame': int(round(start_time * 30)),
            'EndFrame': int(round(end_time * 30)),
            'Parameters': params,
        })
    return events


def parse_one_tae(fp):
    """解析一个 .tae 文件，返回 (tae_name, anims_list)"""
    with open(fp, 'rb') as f:
        d = f.read()

    # 验证文件头
    if d[0:4] != b'TAE ':
        return os.path.basename(fp), []
    version = struct.unpack_from('<I', d, 8)[0]
    if version != 0x1000D:
        return os.path.basename(fp), []

    # 使用 tae_extractor.py 相同的头部解析
    p = 0x10
    _, p = read_u64(d, p)      # headerSize
    _, p = read_u64(d, p)      # constant
    _, p = read_u64(d, p)      # field offset
    _, p = read_u64(d, p)      # field offset
    event_bank, p = read_u64(d, p)  # eventBank
    p += 8                      # zero
    tae_id, p = read_u64(d, p) # TAE ID
    p += 8                      # flag

    # Scan anim table （严格扫描，只接受 1-2000 范围）
    anim_off, anim_count = 0, 0
    for base_off in range(p, min(p + 64, len(d)), 8):
        candidate = struct.unpack_from('<Q', d, base_off)[0]
        if candidate < 0x100 or candidate >= len(d): continue
        for cnt_off in range(base_off + 8, min(base_off + 24, len(d)), 8):
            cnt = struct.unpack_from('<Q', d, cnt_off)[0]
            if 1 <= cnt <= 2000:
                # 验证：candidate 处第一个 AnimID 是否合理
                if candidate + 16 <= len(d):
                    first_aid = struct.unpack_from('<Q', d, candidate)[0]
                    first_eoff = struct.unpack_from('<Q', d, candidate + 8)[0]
                    # AnimID 应在 0~9999999 范围，事件表偏移应在文件范围内
                    if 0 <= first_aid <= 9999999 and 0x100 <= first_eoff < len(d):
                        anim_off = candidate
                        anim_count = cnt
                        break
        if anim_off:
            break

    if anim_off == 0:
        return os.path.basename(fp), []

    anims = []
    pp = anim_off
    for _ in range(min(anim_count, 2000)):
        if pp + 16 > len(d): break
        aid, pp = read_u64(d, pp)
        eoff, pp = read_u64(d, pp)
        if eoff >= len(d) or eoff < 0x100: continue
        # 检查事件表头部至少32字节
        if eoff + 32 > len(d): continue
        events = parse_tae_events(d, eoff)
        # 只保留有关键事件（Type=0/1/2/5）的动画，减少输出体积
        filtered = [e for e in events if e['Type'] in (0, 1, 2, 5)]
        if filtered:
            anims.append({'AnimID': aid, 'Events': filtered})

    return os.path.basename(fp), anims


# ── 范围分类逻辑 ──
ID_30_MID = [(0, 9, "Guard"), (10, 19, "Deflect"), (20, 29, "Mikiri"),
             (30, 31, "Dodge"), (32, 39, "Sweep"), (40, 49, "Grab")]
ID_40_MID = [(0, 19, "Hit"), (20, 39, "Knockback"), (40, 59, "Death")]


def infer_category(anim_id):
    high = anim_id // 10000
    mid = (anim_id // 100) % 100
    if high == 0:
        if anim_id < 14: return "Locomotion_Idle"
        if anim_id < 100: return "Locomotion"
        if anim_id < 200: return "Locomotion_Walk"
        if anim_id < 300: return "Locomotion_Jog"
        if anim_id < 400: return "Locomotion_Run"
        if anim_id < 500: return "Locomotion_Sprint"
        if anim_id < 600: return "Locomotion"
        if anim_id < 700: return "Jump"
        if 5000 <= anim_id < 6000: return "Combat"
        return "Locomotion"
    if high == 20: return "Attack"
    if high == 30:
        for lo, hi, n in ID_30_MID:
            if lo <= mid <= hi: return n
        return "Defense"
    if high == 40:
        for lo, hi, n in ID_40_MID:
            if lo <= mid <= hi: return n
        return "HitReaction"
    if high == 50: return "Deathblow"
    if high == 60: return "Resurrection"
    if high == 70: return "Prosthetic"
    if high == 80: return "Grapple"
    if high == 90: return "Item"
    # high=1-4, 12-19, 25 = CombatArt
    if high in (1, 2, 3, 4, 25) or (12 <= high <= 19):
        return "CombatArt"
    # high=31=Defense, 41-42=HitReaction, 51=Deathblow, 61=Resurrection, 71=Prosthetic
    if high == 31: return "Defense"
    if high in (41, 42): return "HitReaction"
    if high == 51: return "Deathblow_extra"
    if high == 61: return "Resurrection"
    if high == 71: return "Prosthetic"
    if high >= 110: return "CombatArt"
    # 5xxx-9xxx 战斗或特殊
    if 5000 <= anim_id < 10000:
        return "Combat"
    return "Other"


def main():
    tae_dir = sys.argv[1]
    var_map_path = sys.argv[2]
    out_path = sys.argv[3]

    # 加载 BehaviorVariationMap
    with open(var_map_path, encoding="utf-8") as f:
        var_data = json.load(f)
    var_to_reftype = {}
    for vid, v in var_data["variations"].items():
        var_to_reftype[int(vid)] = v["primary_ref_type"]
    print(f"[1/3] Loaded {len(var_to_reftype)} BehaviorParam variations")

    # 遍历 TAE 文件
    tae_files = sorted([os.path.join(tae_dir, f) for f in os.listdir(tae_dir) if f.lower().endswith('.tae')])
    print(f"[2/3] Parsing {len(tae_files)} TAE files...")

    total_anims = 0
    anim_behavior = {}  # AnimID → {judge_ids, types, prefix}

    for fp in tae_files:
        name, anims = parse_one_tae(fp)
        prefix = name.replace(".tae", "")
        print(f"  {name}: {len(anims)} anims (with events)")
        total_anims += len(anims)

        for a in anims:
            aid = a["AnimID"]
            if aid not in anim_behavior:
                anim_behavior[aid] = {"judge_ids": set(), "types": set(), "prefix": prefix, "jt_flags": set()}

            for evt in a["Events"]:
                etype = evt["Type"]
                if etype in (1, 2, 5):
                    bid = evt["Parameters"].get("BehaviorJudgeID", -1)
                    if bid >= 0:
                        anim_behavior[aid]["judge_ids"].add(bid)
                        anim_behavior[aid]["types"].add(etype)
                elif etype == 0:
                    jt = evt["Parameters"].get("JumpTableID", -1)
                    if jt >= 0:
                        anim_behavior[aid]["jt_flags"].add(jt)

    has_behavior = sum(1 for v in anim_behavior.values() if v["judge_ids"])
    print(f"\n  Total anims: {total_anims}, Unique AnimIDs: {len(anim_behavior)}, With Type=1/2/5: {has_behavior}")

    # 交叉验证
    print(f"[3/3] Cross-validating...")
    categories = {}
    cross_check = {"bp_attack": 0, "bp_bullet": 0, "bp_speffect": 0, "range": 0}
    corrections = []

    for aid, info in anim_behavior.items():
        judge_ids = info["judge_ids"]
        jt_flags = info["jt_flags"]

        if judge_ids:
            variation_id = aid // 100
            ref_type = var_to_reftype.get(variation_id)

            if ref_type == "Attack":
                categories[aid] = "Attack"
                cross_check["bp_attack"] += 1
            elif ref_type == "Bullet":
                high = aid // 10000
                categories[aid] = "Prosthetic" if high == 70 else "Attack"
                cross_check["bp_bullet"] += 1
            elif ref_type == "SpEffect":
                categories[aid] = "Hit"
                cross_check["bp_speffect"] += 1
            else:
                categories[aid] = infer_category(aid)
                cross_check["range"] += 1

            range_cat = infer_category(aid)
            if range_cat != categories[aid]:
                corrections.append({
                    "anim_id": aid, "range_cat": range_cat,
                    "bp_cat": categories[aid],
                    "judge_ids": sorted(judge_ids),
                    "ref_type": ref_type,
                })
        else:
            cat = infer_category(aid)
            # 特殊规则: JT=25(DodgeCancel) 标记 Dodge
            if 25 in jt_flags:
                cat = "Dodge"
            categories[aid] = cat
            cross_check["range"] += 1

    # 分组
    groups = defaultdict(list)
    for aid, cat in sorted(categories.items()):
        groups[cat].append(aid)

    output = {
        "description": "AnimID → Category from BehaviorParam cross-validation",
        "total_anims": total_anims,
        "unique_anim_ids": len(anim_behavior),
        "anims_with_behavior_events": has_behavior,
        "cross_check_stats": cross_check,
        "corrections_count": len(corrections),
        "corrections": corrections[:200],
        "category_stats": {k: len(v) for k, v in sorted(groups.items())},
        "category_anim_map": {k: sorted(v) for k, v in sorted(groups.items())},
        "anim_category_map": categories,
    }

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

    print(f"\nDone! → {out_path}")
    print(f"  Source: BP_Attack={cross_check['bp_attack']}, BP_Bullet={cross_check['bp_bullet']}, SpEffect={cross_check['bp_speffect']}, Range={cross_check['range']}")
    print(f"  Corrections: {len(corrections)}")
    print(f"  Categories: {len(groups)}")
    for c, ids in sorted(groups.items()):
        print(f"    {c}: {len(ids)} [{min(ids)}-{max(ids)}]")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python extract_anim_behavior.py <tae_directory> <BehaviorVariationMap.json> <output.json>")
        sys.exit(1)
    main()
