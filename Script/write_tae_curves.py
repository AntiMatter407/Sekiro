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
  3. 生成 DisableTurning、AttackTurnSpeed、CancelActions 和 AttackHitbox 曲线
  4. 调用 bridge.py anim_blueprint add_curve 写入 UAnimSequence
"""
import argparse
import json
import os
import sys
import subprocess
import time

# ── 映射表 ──────────────────────────────────────────────────────

# 未被 Type=224 覆盖时使用的原版常规攻击转向速度，单位为度/秒。
DEFAULT_ATTACK_TURN_SPEED = 360.0

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


def build_disable_turning_keys(events, total_frames):
    """从 JT=7 事件构建独立的 DisableTurning 阶跃曲线。"""
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
        if evt.get("Parameters", {}).get("JumpTableID") != 7:
            continue
        start = evt.get("StartFrame", 0)
        end = evt.get("EndFrame", 0)
        for frame in range(start, min(end, total_frames + 1)):
            frame_values[frame] = 1

    keys = [{"time": 0.0, "value": int(frame_values[0])}]
    previous_value = frame_values[0]
    for frame in range(1, total_frames + 1):
        value = frame_values[frame]
        if value != previous_value:
            keys.append({"time": round(frame / 30.0, 4), "value": int(value)})
            previous_value = value
    return keys


def build_attack_turn_speed_keys(events, total_frames):
    """从 Type=224 事件构建攻击转向速度曲线，未覆盖帧使用常规转向速度。"""
    if total_frames <= 0:
        for evt in events:
            end = evt.get("EndFrame", 0)
            start = evt.get("StartFrame", 0)
            total_frames = max(total_frames, end, start)
        total_frames = max(total_frames, 1)

    frame_values = [DEFAULT_ATTACK_TURN_SPEED] * (total_frames + 1)
    for evt in events:
        if evt.get("Type") != 224:
            continue
        turn_speed = max(float(evt.get("Parameters", {}).get("TurnSpeed", 0.0)), 0.0)
        start = evt.get("StartFrame", 0)
        end = evt.get("EndFrame", 0)
        for frame in range(start, min(end, total_frames + 1)):
            frame_values[frame] = turn_speed

    keys = [{"time": 0.0, "value": float(frame_values[0])}]
    previous_value = frame_values[0]
    for frame in range(1, total_frames + 1):
        value = frame_values[frame]
        if value != previous_value:
            keys.append({"time": round(frame / 30.0, 4), "value": float(value)})
            previous_value = value
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

    # 显式写入第 0 帧，避免 UE 在首个攻击键之前把曲线外推为首键的非零值。
    keys = [{"time": 0.0, "value": int(frame_values[0])}]
    prev_val = frame_values[0]
    for f in range(1, total_frames + 1):
        val = frame_values[f]
        if val != prev_val:
            time_sec = f / 30.0
            keys.append({"time": round(time_sec, 4), "value": int(val)})
            prev_val = val
    return keys


def write_curve(bridge_py, asset_path, curve_name, keys, overwrite=False, curve_type="float"):
    """调用 bridge add_curve 写入曲线（通过 CLI 参数传递）
    
    Args:
        curve_type: "int" 使用常量插值，适用于离散值和阶跃速度；"float" 使用线性插值
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


def process_single_anim(
    tae_path,
    anim_id,
    anim_prefix=None,
    base_path=None,
    asset_prefix=None,
    turning_only=False,
):
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

    # 写入独立语义曲线；不再生成需要运行时位运算解码的 FrameFlags。
    all_ok = True

    print("  写入 DisableTurning...")
    disable_turning_keys = build_disable_turning_keys(events, total_frames)
    if not write_curve(
        bridge_py,
        asset_path,
        "DisableTurning",
        disable_turning_keys,
        overwrite=True,
        curve_type="int"):
        all_ok = False

    print("  写入 AttackTurnSpeed...")
    attack_turn_speed_keys = build_attack_turn_speed_keys(events, total_frames)
    if not write_curve(
        bridge_py,
        asset_path,
        "AttackTurnSpeed",
        attack_turn_speed_keys,
        overwrite=True,
        curve_type="int"):
        all_ok = False

    if not turning_only:
        print("  写入 CancelActions...")
        ca_keys = build_cancel_actions_keys(events, total_frames)
        if not write_curve(
            bridge_py,
            asset_path,
            "CancelActions",
            ca_keys,
            overwrite=True,
            curve_type="int"):
            all_ok = False

        print("  写入 AttackHitbox...")
        ah_keys = build_attack_hitbox_keys(events, total_frames)
        if not write_curve(
            bridge_py,
            asset_path,
            "AttackHitbox",
            ah_keys,
            overwrite=True,
            curve_type="int"):
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
    parser.add_argument(
        "--turning_only",
        action="store_true",
        help="只更新 DisableTurning 与 AttackTurnSpeed，保留其他曲线和事件")
    args = parser.parse_args()

    process_single_anim(
        tae_path=args.tae,
        anim_id=args.animid,
        anim_prefix=args.anim_prefix,
        base_path=args.base_path,
        asset_prefix=args.asset_prefix,
        turning_only=args.turning_only,
    )


if __name__ == "__main__":
    main()
