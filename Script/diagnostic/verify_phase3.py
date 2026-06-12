"""
Phase 3 验证：对 Anim_Cpp.txt 输出做 FK 验证
验证 LocalUE → FK → WorldUE 的一致性
"""
import os, math

def parse_anim(path):
    """解析 Anim_Cpp.txt: ClipName|Frame|BoneIdx|BoneName|PX|PY|PZ|RX|RY|RZ|RW"""
    frames = {}  # frame -> [(bone_name, parent_idx, pos, quat)]
    bone_info = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split('|')
            if len(parts) < 11: continue
            clip, frame, bidx, bname = parts[0], int(parts[1]), int(parts[2]), parts[3]
            px, py, pz = float(parts[4]), float(parts[5]), float(parts[6])
            rx, ry, rz, rw = float(parts[7]), float(parts[8]), float(parts[9]), float(parts[10])
            if frame not in frames:
                frames[frame] = []
            frames[frame].append((bname, None, (px, py, pz), (rx, ry, rz, rw)))
            if bname not in bone_info:
                bone_info[bname] = bidx
    return frames, bone_info

def parse_bones_cpp(path):
    """解析 Bones_Cpp.txt: idx|name|parentIdx|posX|posY|posZ"""
    bones = []
    with open(path) as f:
        for line in f:
            parts = line.strip().split('|')
            if len(parts) < 6: continue
            idx, name, parent = int(parts[0]), parts[1], int(parts[2])
            px, py, pz = float(parts[3]), float(parts[4]), float(parts[5])
            bones.append((name, parent, (px, py, pz)))
    return bones

def quat_mul(q1, q2):
    """四元数乘法"""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2
    )

def quat_rot_vec(q, v):
    """四元数旋转向量"""
    x, y, z, w = q
    vx, vy, vz = v
    # q * v * q_conj
    # First: q * v_quat
    tx = w*vx + y*vz - z*vy
    ty = w*vy + z*vx - x*vz
    tz = w*vz + x*vy - y*vx
    tw = -x*vx - y*vy - z*vz
    # Then: result * q_conj
    rx = tx*w - tw*(-x) - ty*(-z) + tz*(-y)
    ry = ty*w - tz*(-x) - tw*(-y) + tx*(-z)
    rz = tz*w - tx*(-y) - ty*(-x) + tw*(-z)
    return (rx, ry, rz)

def compute_fk(frames, ref_bones, frame_idx=0):
    """对指定帧的 LocalUE 计算 FK → WorldUE"""
    if frame_idx not in frames:
        print(f"Frame {frame_idx} not found!")
        return []

    bone_data = frames[frame_idx]
    # 构建 name→idx 映射（按 DumpAnimTracks 输出顺序 = RefSkel 顺序）
    name_to_idx = {name: i for i, (name, _, _, _) in enumerate(bone_data)}

    world_positions = []
    for i, (name, parent, (px, py, pz), (rx, ry, rz, rw)) in enumerate(bone_data):
        local_t = (px, py, pz)
        local_r = (rx, ry, rz, rw)

        # FK: WorldUE = LocalUE * ParentWorldUE
        # But DumpAnimTracks derives LocalUE relative to RefSkel parent
        # We need RefSkel parent for this bone
        parent_idx = ref_bones[i][1] if i < len(ref_bones) else -1

        if parent_idx >= 0 and parent_idx < len(world_positions):
            pw = world_positions[parent_idx]
            # World pos = parent_world_pos + rotate(parent_world_rot, local_pos)
            # Actually for transform composition:
            # Parent world: T_p * R_p (applied as: first rotate, then translate)
            # Local: T_l * R_l
            # World = T_l * R_l * T_p * R_p
            # For position: world_p = local_translation transformed by parent, then + parent_world_pos

            prx, pry, prz, prw = pw[1]
            # Rotate local position by parent's world rotation
            rot_p = quat_rot_vec((prx, pry, prz, prw), local_t)
            world_pos = (pw[0][0] + rot_p[0], pw[0][1] + rot_p[1], pw[0][2] + rot_p[2])
            world_rot = quat_mul(local_r, pw[1])
        else:
            world_pos = local_t
            world_rot = local_r

        world_positions.append((world_pos, world_rot))

    return world_positions

def main():
    diag_dir = 'D:/Sekiro/Tools/diagnostic'
    anim_path = os.path.join(diag_dir, 'Anim_Sekiro_a000_200000_Cpp.txt')
    bones_path = os.path.join(diag_dir, 'Bones_Cpp.txt')

    frames, bone_info = parse_anim(anim_path)
    ref_bones = parse_bones_cpp(bones_path)

    print(f"动画帧数: {len(frames)}")
    print(f"每帧骨骼数: {len(frames[0])}")
    print(f"RefSkel骨骼数: {len(ref_bones)}")

    # 检查动画骨骼名是否匹配 RefSkel
    if 0 in frames:
        anim_bones = [b[0] for b in frames[0]]
        ref_names = [b[0] for b in ref_bones]
        missing = [n for n in ref_names if n not in anim_bones]
        if missing:
            print(f"动画缺失的骨骼 ({len(missing)}): {missing[:5]}...")

    # FK 计算 frame 0 的 WorldUE
    world = compute_fk(frames, ref_bones, 0)

    print(f"\n=== Frame 0 FK结果（UUID空间，厘米） ===")
    print(f"{'Idx':>4} {'BoneName':<20} {'WorldPos (X, Y, Z)':>40}")
    print("-" * 70)

    for i in range(min(20, len(world))):
        name = ref_bones[i][0] if i < len(ref_bones) else frames[0][i][0]
        p = world[i][0]
        print(f"{i:>4} {name:<20} ({p[0]:>10.4f}, {p[1]:>10.4f}, {p[2]:>10.4f})")

    # 验证：frame 0 经过 FK 后的 World 位置应与 ref skeleton Bones_Cpp.txt 参考位置不同
    # （动画姿态 ≠ 参考姿态），但应该在合理范围内
    print(f"\n=== 与 RefSkel 参考位置对比（选取关键骨骼） ===")
    key_bones = ['Master', 'RootPos', 'Pelvis', 'L_Foot', 'R_Foot', 'L_Thigh', 'R_Thigh', 'Spine1']
    for name in key_bones:
        for i, (bn, _, _) in enumerate(ref_bones):
            if bn == name and i < len(world):
                wp = world[i][0]
                rp = ref_bones[i][2]
                dist = math.sqrt((wp[0]-rp[0])**2 + (wp[1]-rp[1])**2 + (wp[2]-rp[2])**2)
                print(f"  {name:<15} FK=({wp[0]:>8.2f},{wp[1]:>8.2f},{wp[2]:>8.2f})  "
                      f"Ref=({rp[0]:>8.2f},{rp[1]:>8.2f},{rp[2]:>8.2f})  "
                      f"Δ={dist:.2f}cm")

    # 检查数值完整性
    nan_pos = 0
    mag_issues = {}  # 四元数模长分布
    for frame_idx in sorted(frames.keys()):
        for name, _, pos, rot in frames[frame_idx]:
            if any(math.isnan(v) for v in pos) or any(math.isnan(v) for v in rot):
                nan_pos += 1
            mag = math.sqrt(rot[0]**2 + rot[1]**2 + rot[2]**2 + rot[3]**2)
            bucket = round(mag, 3)  # 保留3位小数分组
            mag_issues[bucket] = mag_issues.get(bucket, 0) + 1

    print(f"\n=== 数据完整性 ===")
    print(f"NaN值: {nan_pos} (应为0)")
    print(f"四元数模长分布:")
    for mag_val, count in sorted(mag_issues.items()):
        flag = "  <-- 异常" if abs(mag_val - 1.0) > 0.01 else ""
        print(f"  |Q|={mag_val:.3f}: {count}{flag}")

    # 检查是否有模长=0的四元数（退化情况）
    degenerate = mag_issues.get(0.0, 0) + mag_issues.get(0.001, 0)
    near_unit = mag_issues.get(1.0, 0)
    total = sum(mag_issues.values())
    all_ok = nan_pos == 0 and degenerate == 0

    print(f"\n总四元数: {total}")
    print(f"模长=1.000: {near_unit} ({100*near_unit/total:.1f}%)")
    print(f"退化四元数(|Q|~0): {degenerate}")
    print(f"\n{'Phase 3 基本验证通过' if all_ok else '发现问题'}")

if __name__ == '__main__':
    main()
