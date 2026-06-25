#!/usr/bin/env python3
"""
extract_tae_sdt.py — 基于 TAE.Template.SDT.xml 精确解析只狼 TAE 文件
"""
import json, os, struct, sys
from collections import defaultdict

# ── JT ID 名称映射 ──
JT_NAMES = {
    7:"DisableTurning", 8:"FlagAsDodging", 12:"InvokeDeath", 19:"DisableMapHit",
    25:"DodgeCancelStart", 26:"EndIfDodgeQueued", 27:"SetNoGravity",
    31:"ItemCancelEnd", 32:"EndIfWeaponSwitch", 34:"CancelEnd_General",
    51:"Invincible", 55:"Stagger", 63:"Special",
    87:"InvokeAttackAction", 89:"DisableMovement", 90:"LimitWalk", 91:"LimitDash",
    103:"AnimCancelEnd_L2", 105:"AnimCancelStart_L2",
    107:"AnimCancelEnd_Item", 108:"AnimCancelStart_Item",
    111:"AnimCancelStart_Emergency", 112:"AnimCancelEnd_Emergency",
    113:"HeightCorrection", 115:"AnimCancelEnd_R1", 117:"AnimCancelEnd_L1",
    118:"AnimCancelEnd_L2_Alt", 119:"TryInvokeForceParry",
    133:"DisableSpecial", 134:"DisableItem", 137:"DisableParry", 154:"ItemUseWindow",
}

# ── 二进制读取 ──
def ru64(d, p): return struct.unpack_from('<Q', d, p)[0], p + 8
def ru32(d, p): return struct.unpack_from('<I', d, p)[0], p + 4
def rf32(d, p): return struct.unpack_from('<f', d, p)[0], p + 4


def parse_events(d, eoff):
    """基于 SDT 模板解析事件表"""
    p = eoff
    if p + 32 > len(d): return [], 0

    ev_off = struct.unpack_from('<Q', d, p)[0]
    if ev_off < 0x100 or ev_off > len(d): return [], 0

    p += 8
    for _ in range(3): p += 8

    if p + 12 > len(d): return [], 0
    ev_count = struct.unpack_from('<I', d, p)[0]
    p += 12

    if ev_count == 0 or ev_count > 300: return [], 0
    if ev_off + ev_count * 8 > len(d): return [], 0

    events = []
    total_frames = 0

    for i in range(ev_count):
        ev_data, _ = ru64(d, ev_off + i * 8)
        if ev_data < 0x100 or ev_data + 28 > len(d): continue

        etype = struct.unpack_from('<I', d, ev_data)[0]
        if etype > 2000: continue

        st = struct.unpack_from('<f', d, ev_data + 4)[0]
        et = struct.unpack_from('<f', d, ev_data + 8)[0]
        if st != st or et != et: continue
        if abs(st) > 10000 or abs(et) > 10000: continue

        sf, ef = int(round(st * 30)), int(round(et * 30))
        if ef > total_frames: total_frames = ef

        raw = d[ev_data+12:ev_data+28]
        ev = {'Type': etype, 'SF': sf, 'EF': ef}

        # Type=0: JumpTable (16 bytes)
        # [0]s32 JT, [4]f32 ArgA, [8]s32 ArgB, [12]u8 ArgC, [13]u8 ArgD, [14]s16 StateInfo
        if etype == 0 and len(raw) >= 16:
            jt = struct.unpack_from('<I', raw, 0)[0]
            if 0 < jt < 200:  # 有效 JT
                ev['JT'] = jt
                ev['ArgA'] = struct.unpack_from('<f', raw, 4)[0]
                ev['ArgB'] = struct.unpack_from('<i', raw, 8)[0]
            else:
                continue  # JT=0 = Do Nothing

        # Type=1: InvokeAttackBehavior (16 bytes)
        # [0]s32 AttackType, [4]s32(unk), [8]s32 BID, [12]u8 Dir, [13]u8 Src, [14]s16 StateInfo
        elif etype == 1 and len(raw) >= 16:
            ev['AttackType'] = struct.unpack_from('<I', raw, 0)[0]
            ev['BID'] = struct.unpack_from('<i', raw, 8)[0]
            ev['Source'] = raw[13]

        # Type=2: InvokeBulletBehavior (24 bytes)
        elif etype == 2 and len(raw) >= 16:
            ev['DummyPoly'] = struct.unpack_from('<I', raw, 0)[0]
            ev['BID'] = struct.unpack_from('<i', raw, 8)[0]

        # Type=5: InvokeCommonBehavior (16 bytes)
        elif etype == 5 and len(raw) >= 12:
            ev['AttackIdx'] = struct.unpack_from('<I', raw, 0)[0]
            ev['BID'] = struct.unpack_from('<i', raw, 4)[0]

        else:
            # 非关键事件，但我们仍保留它(Avatar)让动画不被跳过
            ev['Skipped'] = True

        events.append(ev)

    return events, total_frames


def parse_tae(fp):
    with open(fp, 'rb') as f:
        d = f.read()
    if d[0:4] != b'TAE ': return None, []
    v = struct.unpack_from('<I', d, 8)[0]
    if v != 0x1000D: return None, []

    p = 0x10
    for _ in range(4): _, p = ru64(d, p)
    _, p = ru64(d, p); p += 8; _, p = ru64(d, p); p += 8

    anim_off, anim_count = 0, 0
    for off in range(p, min(p + 64, len(d)), 8):
        cand = struct.unpack_from('<Q', d, off)[0]
        if cand < 0x100 or cand >= len(d): continue
        for cnt_off in range(off + 8, min(off + 24, len(d)), 8):
            cnt = struct.unpack_from('<Q', d, cnt_off)[0]
            if 1 <= cnt <= 2000 and cand + 16 <= len(d):
                fa = struct.unpack_from('<Q', d, cand)[0]
                fe = struct.unpack_from('<Q', d, cand + 8)[0]
                if 0 <= fa <= 9999999 and 0x100 <= fe < len(d):
                    anim_off, anim_count = cand, cnt; break
        if anim_off: break
    if anim_off == 0: return None, []

    name = os.path.basename(fp).replace('.tae', '')
    anims = []
    pp = anim_off
    for _ in range(min(anim_count, 2000)):
        if pp + 16 > len(d): break
        aid, pp = ru64(d, pp)
        eoff, pp = ru64(d, pp)
        if eoff >= len(d) or eoff < 0x100: continue
        if eoff + 32 > len(d): continue
        evs, tf = parse_events(d, eoff)
        anims.append({'AnimID': aid, 'Frames': tf, 'Events': evs})
    return name, anims


# ── 分类 ──
def infer_cat(aid):
    h = aid // 10000
    m = (aid // 100) % 100
    if h == 0:
        if aid < 14: return "Locomotion_Idle"
        if aid < 100: return "Locomotion"
        if aid < 200: return "Locomotion_Walk"
        if aid < 300: return "Locomotion_Jog"
        if aid < 400: return "Locomotion_Run"
        if aid < 500: return "Locomotion_Sprint"
        if aid < 600: return "Locomotion"
        if aid < 700: return "Jump"
        if 5000 <= aid < 6000: return "Combat"
        return "Locomotion"
    if h == 20: return "Attack"
    if h == 30:
        for l,u,n in [(0,9,"Guard"),(10,19,"Deflect"),(20,29,"Mikiri"),
                       (30,31,"Dodge"),(32,39,"Sweep"),(40,49,"Grab")]:
            if l <= m <= u: return n
        return "Defense"
    if h == 40:
        for l,u,n in [(0,19,"Hit"),(20,39,"Knockback"),(40,59,"Death")]:
            if l <= m <= u: return n
        return "HitReaction"
    if h == 50: return "Deathblow"
    if h == 60: return "Resurrection"
    if h == 70: return "Prosthetic"
    if h == 80: return "Grapple"
    if h == 90: return "Item"
    if h in (1,2,3,4,25) or (12 <= h <= 19): return "CombatArt"
    if h == 31: return "Defense"
    if h in (41,42): return "HitReaction"
    if h == 51: return "Deathblow"
    if h == 61: return "Resurrection"
    if h == 71: return "Prosthetic"
    if h >= 110: return "CombatArt"
    if 5000 <= aid < 10000: return "Combat"
    return "Other"


def main():
    tae_dir = sys.argv[1]
    out_path = sys.argv[2]

    files = sorted(os.path.join(tae_dir, f) for f in os.listdir(tae_dir) if f.lower().endswith('.tae'))
    print(f"[1/2] Parsing {len(files)} TAE files...")

    db = {}
    for fp in files:
        name, anims = parse_tae(fp)
        if not name:
            print(f"  {os.path.basename(fp)}: skip (not SDT)")
            continue
        print(f"  {name}: {len(anims)} anims")
        for a in anims:
            aid = a['AnimID']
            if aid not in db:
                db[aid] = {'prefix': name, 'jts': set(), 'bids': [], 'types': set(), 'frames': 0}
            db[aid]['frames'] = max(db[aid]['frames'], a['Frames'])
            for e in a['Events']:
                if 'JT' in e: db[aid]['jts'].add(e['JT'])
                if 'BID' in e: db[aid]['bids'].append(e['BID'])
                db[aid]['types'].add(e['Type'])

    print(f"\n  Total: {len(db)}, With BID: {sum(1 for v in db.values() if v['bids'])}")

    # 分类
    cats = {}
    for aid, info in db.items():
        cat = infer_cat(aid)
        # JT 特殊修正
        jts = info['jts']
        if 25 in jts: cat = "Dodge"
        cats[aid] = cat

    groups = defaultdict(list)
    for aid, cat in sorted(cats.items()):
        groups[cat].append(aid)

    out = {
        "description": "AnimID→Category (SDT template)",
        "total": len(db),
        "with_bid": sum(1 for v in db.values() if v['bids']),
        "stats": {k: len(v) for k, v in sorted(groups.items())},
        "catmap": {k: sorted(v) for k, v in sorted(groups.items())},
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)

    print(f"\n[2/2] Done → {out_path}")
    for c, ids in sorted(groups.items()):
        print(f"  {c}: {len(ids)} [{min(ids)}-{max(ids)}]")

    # 显示 BID 覆盖统计
    bid_count = sum(1 for v in db.values() if v['bids'])
    # 显示哪些 AnimID 有 BID
    bid_anims = sorted([aid for aid, v in db.items() if v['bids']])
    print(f"\n  AnimIDs with BID ({bid_count}):")
    for aid in bid_anims[:20]:
        bids = db[aid]['bids']
        types = db[aid]['types']
        print(f"    {aid}: BIDs={bids} types={sorted(types)}")
    if len(bid_anims) > 20:
        print(f"    ... ({len(bid_anims)-20} more)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python extract_tae_sdt.py <tae_dir> <output.json>")
        sys.exit(1)
    main()
