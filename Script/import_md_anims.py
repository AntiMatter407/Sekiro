"""
import_md_anims.py — 批量导入修复后的 MD 动画到 UE，
然后写入空的 FrameFlags/CancelActions/AttackHitbox 曲线。

用法：
  python import_md_anims.py

前提：
  - UE 编辑器正在运行，SekiroAIBridge 插件监听 9877
  - MD 动画 JSON 已由 fix_md_anim_bones.py 生成
    （在 Output/c0000/Animation/MD_Anims/Anim_Sekiro_a000_*.json）
"""

import json
import os
import subprocess
import sys
import glob

# ── 路径配置 ──────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BRIDGE_PY = os.path.join(SCRIPT_DIR, "aibridge", "bridge.py")
UE_PYTHON = r"D:\Program Files\Epic Games\UE_5.2\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
MD_ANIMS_DIR = os.path.join(PROJECT_DIR, "Output", "c0000", "Animation", "MD_Anims")

# 目标 UE 路径（对应现有动画资产所在位置）
# Anim_Sekiro_a000_100000 → /Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_100000
UE_BASE_PATH = "/Game/Characters/Sekiro/Animations"

# 三条空曲线
EMPTY_CURVES = ["FrameFlags", "CancelActions", "AttackHitbox"]


def call_bridge(args_list):
    """调用 bridge.py 并返回解析结果"""
    cmd = [
        UE_PYTHON,
        BRIDGE_PY,
    ] + args_list
    # 注意：MSYS2_ARG_CONV_EXCL 在 Bash 中需要，但在直接执行 Python 时不需要
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [错误] bridge 返回 {result.returncode}: {result.stderr.strip()[:200]}")
        return None
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        print(f"  [错误] 无法解析 bridge 输出: {result.stdout[:200]}")
        return None


def import_single_anim(json_path):
    """通过 Python exec 在 UE 中导入单个动画"""
    anim_id = os.path.basename(json_path).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    asset_name = f"Anim_Sekiro_a000_{anim_id}"
    ue_asset_path = f"{UE_BASE_PATH}/{asset_name}.{asset_name}"

    # 读取 JSON
    with open(json_path, "r", encoding="utf-8") as f:
        anim_data = json.load(f)

    clip = anim_data["Animations"][0]
    bone_names = anim_data["BoneNames"]
    bone_parents = anim_data["BoneParents"]
    ref_locals = anim_data["BoneLocalTransforms"]

    frames = clip.get("Frames", [])
    frame_count = clip.get("FrameCount", len(frames))
    sample_rate = clip.get("SampleRate", 30)
    duration = clip.get("Duration", frame_count / sample_rate)

    # 构造 Python 代码字符串在 UE 中执行
    py_code = f"""
import unreal

# 准备动画数据
bone_names = {json.dumps(bone_names)}
bone_parents = {json.dumps(bone_parents)}
ref_locals = {json.dumps(ref_locals)}
frames_data = {json.dumps(frames)}
frame_count = {frame_count}
sample_rate = {sample_rate}

# 加载 skeleton
skeleton_path = "/Game/Characters/Sekiro/Models/Sekiro_Skeleton.Sekiro_Skeleton"
skeleton = unreal.load_asset(skeleton_path)
if not skeleton:
    # 尝试其他路径
    skeleton_path = "/Game/Characters/Sekiro/Animations/Sekiro_Skeleton.Sekiro_Skeleton"
    skeleton = unreal.load_asset(skeleton_path)

if not skeleton:
    skeleton = unreal.EditorAssetLibrary.load_asset("/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton")

if not skeleton:
    # 遍历查找
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    skel_assets = reg.get_assets_by_class("Skeleton")
    for sa in skel_assets:
        pkg = str(sa.package_name)
        if 'Sekiro' in pkg and 'Skeleton' in pkg:
            skeleton = unreal.load_asset(pkg)
            unreal.log(f"Found skeleton: {{pkg}}")
            break

if not skeleton:
    unreal.log_error("Skeleton not found!")
    raise Exception("Skeleton not found")

ref_skel = skeleton.get_editor_property("reference_skeleton")
skel_bone_names = [ref_skel.get_bone_name(i).to_string() for i in range(ref_skel.get_num())]
unreal.log(f"Skeleton bones: {{len(skel_bone_names)}}")
unreal.log(f"Anim bone names: {{len(bone_names)}}")

# 构建骨骼名称映射 (anim bone index → skeleton bone index)
anim_to_skel = {{}}
for i, name in enumerate(bone_names):
    for j, sname in enumerate(skel_bone_names):
        if name == sname:
            anim_to_skel[i] = j
            break
unreal.log(f"Bone mapping: {{len(anim_to_skel)}}/{{len(bone_names)}}")

# 创建 AnimationSequence
package_path = "{UE_BASE_PATH}/{asset_name}"
package = unreal.EditorAssetLibrary.make_directory(package_path.rsplit('/', 1)[0])

# 删除旧资产
if unreal.EditorAssetLibrary.does_asset_exist(package_path + "." + "{asset_name}"):
    unreal.EditorAssetLibrary.delete_asset(package_path + "." + "{asset_name}")

anim_seq = unreal.load_asset(package_path + "." + "{asset_name}")
if not anim_seq:
    anim_factory = unreal.AnimSequenceFactory()
    anim_factory.set_editor_property("skeleton", skeleton)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    anim_seq = tools.create_asset("{asset_name}", package_path.rsplit('/', 1)[0], None, anim_factory)

if not anim_seq:
    unreal.log_error("Failed to create anim sequence")
    raise Exception("Create failed")

anim_seq.set_editor_property("skeleton", skeleton)

# 获取控制器并写入数据
controller = anim_seq.get_controller()
controller.open_bracket(unreal.Text("Import MD Animation"))

# 设置帧率和帧数
controller.set_frame_rate(unreal.FrameRate(sample_rate, 1))
controller.set_number_of_frames(frame_count - 1)
controller.remove_all_bone_tracks()

ref_skel = skeleton.get_editor_property("reference_skeleton")
num_skel_bones = ref_skel.get_num()

# 为每根 skeleton 骨骼准备关键帧数组
all_pos = [[] for _ in range(num_skel_bones)]
all_rot = [[] for _ in range(num_skel_bones)]
all_scale = [[] for _ in range(num_skel_bones)]

for fi in range(frame_count):
    frame_bt = frames_data[fi]["BoneTransforms"]
    for si in range(num_skel_bones):
        # 查找该骨骼在动画数据中的索引
        sname = ref_skel.get_bone_name(si).to_string()
        ai = -1
        for j, n in enumerate(bone_names):
            if n == sname:
                ai = j
                break
        if ai >= 0 and ai < len(frame_bt):
            bt = frame_bt[ai]
            # FK 累积到世界空间
            pos = unreal.Vector(bt["P"][0], bt["P"][1], bt["P"][2])
            rot = unreal.Quat(bt["R"][0], bt["R"][1], bt["R"][2], bt["R"][3])
            scale = unreal.Vector(bt["S"][0] if "S" in bt else 1.0,
                                  bt["S"][1] if "S" in bt else 1.0,
                                  bt["S"][2] if "S" in bt else 1.0)
        else:
            pos = unreal.Vector(0, 0, 0)
            rot = unreal.Quat(0, 0, 0, 1)
            scale = unreal.Vector(1, 1, 1)

        # FK 累积到世界空间
        parent_idx = ref_skel.get_parent_index(si)
        if parent_idx >= 0 and parent_idx < num_skel_bones:
            parent_world = anim_seq.get_editor_property("raw_data").get_bone_track_keys(parent_idx)
            # 简化：用 skeleton 的参考姿势累积
            ref_pose = ref_skel.get_ref_bone_pose(si)
            t = ref_pose.get_translation()
            r = ref_pose.get_rotation()
            s = ref_pose.get_scale3d()
            parent_ref = ref_skel.get_ref_bone_pose(parent_idx) if parent_idx >= 0 else None
            if parent_ref:
                pt = parent_ref.get_translation()
                pr = parent_ref.get_rotation()
                # World = Local * ParentWorld
                local_mat = unreal.Matrix.make_transform(pos, rot, scale)
                parent_mat = unreal.Matrix.make_transform(pt, pr, unreal.Vector(1,1,1))
                world_mat = local_mat * parent_mat
                loc = world_mat.get_origin()
                quat = unreal.Quaterion(loc.x, loc.y, loc.z, 0)  # use actual rotation
            else:
                loc = pos
        else:
            loc = pos

        all_pos[si].append(loc)
        all_rot[si].append(rot)
        all_scale[si].append(scale)

# 写入骨骼曲线
for si in range(num_skel_bones):
    sname = ref_skel.get_bone_name(si).to_string()
    try:
        controller.add_bone_curve(sname)
        controller.set_bone_track_keys(sname, all_pos[si], all_rot[si], all_scale[si])
    except Exception as e:
        unreal.log_warning(f"Bone {{sname}}: {{e}}")

controller.notify_populated()
controller.close_bracket()
anim_seq.mark_package_dirty()

# 保存
asset_path = package_path + "." + "{asset_name}"
unreal.EditorAssetLibrary.save_asset(asset_path)
unreal.log(f"Imported: {asset_name}")
print(f"IMPORTED:{asset_name}")
"""
    # 写入临时脚本
    script_path = os.path.join(MD_ANIMS_DIR, "_temp_import.py")
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(py_code)

    # 执行 UE Python
    result = call_bridge(["python", "--file", script_path])
    return result


def add_empty_curves(anim_id):
    """给动画写入三条空曲线"""
    asset_name = f"Anim_Sekiro_a000_{anim_id:06d}" if isinstance(anim_id, int) else f"Anim_Sekiro_a000_{anim_id}"
    ue_asset_path = f"{UE_BASE_PATH}/{asset_name}.{asset_name}"

    for curve_name in EMPTY_CURVES:
        # 单关键帧: time=0, value=0
        keys_json = json.dumps([{"time": 0.0, "value": 0}])
        args = [
            "anim_blueprint", "add_curve",
            ue_asset_path,
            curve_name,
            "--keys", keys_json,
            "--type", "int",
            "--overwrite", "true",
        ]
        result = call_bridge(args)
        if result and result.get("success"):
            print(f"    [OK] {curve_name}")
        else:
            err = result.get("error", "未知错误") if result else "无返回"
            print(f"    [失败] {curve_name}: {err}")


def main():
    """主流程"""
    # 获取所有 MD 动画 JSON 文件
    json_files = sorted(glob.glob(os.path.join(MD_ANIMS_DIR, "Anim_Sekiro_a000_*.json")))
    print(f"找到 {len(json_files)} 个 MD 动画 JSON 文件")

    # ── 逐个导入 ──
    for i, json_path in enumerate(json_files):
        anim_id = os.path.basename(json_path).replace("Anim_Sekiro_a000_", "").replace(".json", "")
        asset_name = f"Anim_Sekiro_a000_{anim_id}"

        print(f"\n[{i+1}/{len(json_files)}] 导入 {asset_name}...", end="", flush=True)

        # 导入动画
        result = import_single_anim(json_path)
        if result and result.get("success"):
            print(" OK")
        else:
            err = result.get("error", "未知错误") if result else "无返回"
            print(f" 失败: {err}")
            continue

        # 写入三条空曲线
        print(f"  写入曲线...")
        add_empty_curves(anim_id)

        # 每 10 个动画暂停一下，防止 UE 过载
        if (i + 1) % 10 == 0 and (i + 1) < len(json_files):
            print(f"\n[进度] 已处理 {i+1}/{len(json_files)}，继续...")

    print(f"\n{'='*60}")
    print(f"完成! 共处理 {len(json_files)} 个动画")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
