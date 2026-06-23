"""
只狼 TAE 曲线写入脚本
从 Sekiro_TAE_Logic.json 读取事件数据，调用 AIBridge add_curve 写入 UAnimSequence。

用法：
  python write_tae_curves.py --tae Extracted/Sekiro_TAE_Logic.json --animid 201010
  python write_tae_curves.py --tae Extracted/Sekiro_TAE_Logic.json --animid 201010 --anim_prefix a200

参数：
  --tae           TAE JSON 路径（必填）
  --animid        写入指定 AnimID 的曲线（默认 201010）
  --anim_prefix   动画资产前缀（a000/a010/a200...），不传则从 TAE 文件名推导
  --base_path     动画资产基础路径，默认 /Game/Characters/Sekiro/Animations
  --asset_prefix  资产名前缀，默认 Anim_Sekiro

流程：
  1. 读取 Sekiro_TAE_Logic.json
  2. 提取指定 AnimID 的事件列表
  3. 生成三条 Integer Curve 的关键帧
  4. 调用 bridge.py anim_blueprint add_curve 写入 UAnimSequence
"""
import argparse
import json
import os
import sys
import subprocess
import time

# ── 映射表 ──────────────────────────────────────────────────────

# FrameFlags: JT ID → 位掩码
JT_FRAME_FLAGS = {
    7:   1 << 0,   # DisableTurning
    89:  1 << 1,   # DisableMovement
    19:  1 << 2,   # DisableMapHit
    119: 1 << 3,   # EnableParry
    137: 1 << 4,   # DisableParry
    133: 1 << 5,   # DisableSpecial
    134: 1 << 6,   # DisableItem
    51:  1 << 7,   # Invincible
    27:  1 << 8,   # SetNoGravity
    8:   1 << 9,   # FlagAsDodging
    12:  1 << 10,  # InvokeDeath
    90:  1 << 11,  # LimitMoveSpeedWalk
    91:  1 << 12,  # LimitMoveSpeedDash
    32:  1 << 13,  # EnterMovement
    31:  1 << 14,  # ExitMovement
    55:  1 << 15,  # Staggered
}

# CancelActions: JT ID → ESKCancelAction 值
JT_CANCEL_ACTIONS = {
    115: 1,   # AnimCancelEnd_R1 → Attack
    26:  1,   # GenericCancelStart → Attack
    117: 2,   # AnimCancelEnd_L1 → Guard
    25:  3,   # AnimCancelStart_Dodge → Dodge
    118: 4,   # AnimCancelEnd_L2 → Prosthetic
    154: 5,   # ItemUseWindow → Item
}

# AttackHitbox: Type=1 的事件
# 值从 AttackType 参数映射
ATTACK_TYPE_MAP = {
    0:  1,    # Standard
    2:  4,    # ForwardR1
    62: 5,    # Plunging
    64: 0,    # Parry → None (弹刀框不算攻击)
}


def find_bridge_py():
    """找到 bridge.py 路径"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(script_dir, "aibridge", "bridge.py"),
        os.path.join(script_dir, "..", "Script", "aibridge", "bridge.py"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise FileNotFoundError("找不到 bridge.py，请确认 Script/aibridge/bridge.py 存在")


def call_bridge(bridge_py, args_list):
    """调用 bridge.py 并返回解析结果"""
    cmd = [sys.executable, bridge_py] + args_list
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [错误] bridge 返回 {result.returncode}: {result.stderr.strip()}")
        return None
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        print(f"  [错误] 无法解析 bridge 输出: {result.stdout[:200]}")
        return None


def build_anim_asset_path(anim_id, anim_prefix, base_path, asset_prefix):
    """构建 UAnimSequence 资产路径"""
    asset_name = f"{asset_prefix}_{anim_prefix}_{anim_id:06d}"
    package_path = f"{base_path}/{asset_name}"
    return f"{package_path}.{asset_name}"


def extract_tae_events(tae_data, target_anim_id):
    """从 TAE JSON 提取指定 AnimID 的事件列表"""
    for tae_file in tae_data.get("TAE_Files", []):
        for anim in tae_file.get("Animations", []):
            if anim["AnimID"] == target_anim_id:
                return anim.get("Events", []), tae_file["FileName"]
    return None, None


def derive_anim_prefix(tae_filename):
    """从 TAE 文件名推导动画前缀: a00.tae → a000, a10.tae → a010"""
    prefix = tae_filename.replace(".tae", "")
    num_part = prefix[1:]
    while len(num_part) < 3:
        num_part = "0" + num_part
    return "a" + num_part


def build_frame_flags_keys(events, total_frames):
    """从事件列表构建 FrameFlags 关键帧

    FrameFlags 是位掩码，每一帧 OR 累积所有活跃 JT 标志。
    为了节约关键帧数量，只在值发生变化时写入关键帧。
    """
    if total_frames <= 0:
        # 从最大事件帧推断
        for evt in events:
            end = evt.get("EndFrame", 0)
            if end > total_frames:
                total_frames = end
        total_frames = max(total_frames, 1)

    # 每帧计算位掩码
    frame_values = [0] * (total_frames + 1)
    for evt in events:
        if evt.get("Type") != 0:
            continue
        params = evt.get("Parameters", {})
        jt_id = params.get("JumpTableID")
        if jt_id is None:
            continue
        bit = JT_FRAME_FLAGS.get(jt_id)
        if bit is None:
            continue
        start = evt.get("StartFrame", 0)
        end = evt.get("EndFrame", 0)
        for f in range(start, min(end + 1, total_frames + 1)):
            frame_values[f] |= bit

    # 压缩：只写值发生变化的帧
    keys = []
    prev_val = 0
    for f in range(0, total_frames + 1):
        val = frame_values[f]
        if val != prev_val:
            time_sec = f / 30.0  # 假设 30fps
            keys.append({"time": round(time_sec, 4), "value": int(val)})
            prev_val = val
    # 确保曲线至少有一个关键帧（值为 0 的起始帧）
    if not keys:
        keys.append({"time": 0.0, "value": 0})
    return keys


def build_cancel_actions_keys(events, total_frames):
    """从事件列表构建 CancelActions 关键帧

    CancelActions 值是 ESKCancelAction 枚举：
      0=None, 1=Attack, 2=Guard, 3=Dodge, 4=Prosthetic, 5=Item

    每个取消窗口覆盖的帧范围内设置对应值。
    多个取消窗口重叠时，取最后一个（优先级更高的）值。
    """
    if total_frames <= 0:
        for evt in events:
            end = evt.get("EndFrame", 0)
            start = evt.get("StartFrame", 0)
            total_frames = max(total_frames, end, start)
        total_frames = max(total_frames, 1)

    frame_values = [0] * (total_frames + 1)
    for evt in events:
        if evt.get("Type") != 0:
            continue
        params = evt.get("Parameters", {})
        jt_id = params.get("JumpTableID")
        if jt_id is None:
            continue
        cancel_val = JT_CANCEL_ACTIONS.get(jt_id)
        if cancel_val is None:
            continue
        start = evt.get("StartFrame", 0)
        end = evt.get("EndFrame", 0)
        for f in range(start, min(end + 1, total_frames + 1)):
            frame_values[f] = cancel_val

    keys = []
    prev_val = 0
    for f in range(0, total_frames + 1):
        val = frame_values[f]
        if val != prev_val:
            time_sec = f / 30.0
            keys.append({"time": round(time_sec, 4), "value": int(val)})
            prev_val = val
    # 确保曲线至少有一个关键帧（值为 0 的起始帧）
    if not keys:
        keys.append({"time": 0.0, "value": 0})
    return keys


def build_attack_hitbox_keys(events, total_frames):
    """从事件列表构建 AttackHitbox 关键帧

    AttackHitbox 值是 ESKAttackHitboxType：
      0=None, 1=Standard, 2=Thrust, 3=Sweep, 4=ForwardR1, 5=Plunging

    从 Type=1 (AttackBehavior) 事件中提取。
    """
    if total_frames <= 0:
        for evt in events:
            end = evt.get("EndFrame", 0)
            start = evt.get("StartFrame", 0)
            total_frames = max(total_frames, end, start)
        total_frames = max(total_frames, 1)

    frame_values = [0] * (total_frames + 1)
    for evt in events:
        if evt.get("Type") != 1:
            continue
        params = evt.get("Parameters", {})
        attack_type = params.get("AttackType", 0)
        hitbox_val = ATTACK_TYPE_MAP.get(attack_type, 1)  # 默认 Standard
        if hitbox_val == 0:
            continue
        start = evt.get("StartFrame", 0)
        end = evt.get("EndFrame", 0)
        for f in range(start, min(end + 1, total_frames + 1)):
            frame_values[f] = hitbox_val

    keys = []
    prev_val = 0
    for f in range(0, total_frames + 1):
        val = frame_values[f]
        if val != prev_val:
            time_sec = f / 30.0
            keys.append({"time": round(time_sec, 4), "value": int(val)})
            prev_val = val
    # 确保曲线至少有一个关键帧（值为 0 的起始帧）
    if not keys:
        keys.append({"time": 0.0, "value": 0})
    return keys


def write_curve(bridge_py, asset_path, curve_name, keys, overwrite=False, curve_type="float"):
    """调用 bridge add_curve 写入曲线（通过 CLI 参数传递）
    
    Args:
        curve_type: "int" 用于整数曲线（FrameFlags/CancelActions/AttackHitbox），"float" 用于其他
    """
    keys_json = json.dumps(keys)
    args = [
        "anim_blueprint", "add_curve",
        asset_path,
        curve_name,
        "--keys", keys_json,
        "--type", curve_type,
    ]
    if overwrite:
        args += ["--overwrite", "true"]

    result = call_bridge(bridge_py, args)
    if result and result.get("success"):
        print(f"  [OK] {curve_name}: {len(keys)} 个关键帧")
        return True
    else:
        err_msg = result.get("error", "未知错误") if result else "无返回"
        print(f"  [失败] {curve_name}: {err_msg}")
        return False


def process_single_anim(tae_path, anim_id, anim_prefix=None, base_path=None, asset_prefix=None):
    """处理单个动画的曲线写入"""
    base_path = base_path or "/Game/Characters/Sekiro/Animations"
    asset_prefix = asset_prefix or "Anim_Sekiro"

    # 读取 TAE JSON
    with open(tae_path, "r", encoding="utf-8") as f:
        tae_data = json.load(f)

    # 提取事件
    events, tae_filename = extract_tae_events(tae_data, anim_id)
    if events is None:
        print(f"错误: AnimID {anim_id} 未在 TAE JSON 中找到")
        return False

    if anim_prefix is None:
        anim_prefix = derive_anim_prefix(tae_filename)

    # 计算总帧数
    total_frames = 0
    for evt in events:
        end = evt.get("EndFrame", 0)
        start = evt.get("StartFrame", 0)
        total_frames = max(total_frames, end, start)
    total_frames = max(total_frames, 1)

    print(f"\n处理 AnimID={anim_id} (前缀={anim_prefix}, 共{len(events)}个事件, {total_frames}帧)")

    # 构建资产路径
    asset_path = build_anim_asset_path(anim_id, anim_prefix, base_path, asset_prefix)
    print(f"  资产: {asset_path}")

    # 查找 bridge.py
    try:
        bridge_py = find_bridge_py()
    except FileNotFoundError as e:
        print(f"错误: {e}")
        return False

    # 写入三条曲线
    # 写入三条整数曲线（FrameFlags / CancelActions / AttackHitbox 均为阶跃型整数曲线）
    all_ok = True

    print("  写入 FrameFlags...")
    ff_keys = build_frame_flags_keys(events, total_frames)
    if not write_curve(bridge_py, asset_path, "FrameFlags", ff_keys, overwrite=True, curve_type="int"):
        all_ok = False

    print("  写入 CancelActions...")
    ca_keys = build_cancel_actions_keys(events, total_frames)
    if not write_curve(bridge_py, asset_path, "CancelActions", ca_keys, overwrite=True, curve_type="int"):
        all_ok = False

    print("  写入 AttackHitbox...")
    ah_keys = build_attack_hitbox_keys(events, total_frames)
    if not write_curve(bridge_py, asset_path, "AttackHitbox", ah_keys, overwrite=True, curve_type="int"):
        all_ok = False

    if all_ok:
        print("  完成!")
    else:
        print("  部分曲线写入失败，请检查上方错误信息")
    return all_ok


def main():
    parser = argparse.ArgumentParser(description="TAE 曲线写入工具")
    parser.add_argument("--tae", required=True, help="TAE JSON 路径")
    parser.add_argument("--animid", type=int, default=201010, help="AnimID（默认 201010）")
    parser.add_argument("--anim_prefix", default=None, help="动画资产前缀（如 a200），不传则从 TAE 文件名推导")
    parser.add_argument("--base_path", default="/Game/Characters/Sekiro/Animations", help="动画资产基础路径")
    parser.add_argument("--asset_prefix", default="Anim_Sekiro", help="资产名前缀")
    args = parser.parse_args()

    process_single_anim(
        tae_path=args.tae,
        anim_id=args.animid,
        anim_prefix=args.anim_prefix,
        base_path=args.base_path,
        asset_prefix=args.asset_prefix,
    )


if __name__ == "__main__":
    main()
