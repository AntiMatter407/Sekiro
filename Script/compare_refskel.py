"""对比 RefSkel_Cpp.txt vs RefSkel_Blender.txt 全部骨骼的Local变换差异"""
import os

DIAG_DIR = r"F:\ProjectAI\Sekiro\Saved\Logs\SekiroSkinDiag"

def load_refskel(path):
    """加载参考骨架文件，返回 {name: (idx, parent_idx, px, py, pz, qx, qy, qz, qw)}"""
    bones = {}
    order = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split('|')
            if len(parts) >= 9:
                idx = int(parts[0])
                name = parts[1]
                parent = int(parts[2])
                px, py, pz = float(parts[3]), float(parts[4]), float(parts[5])
                qx, qy, qz, qw = float(parts[6]), float(parts[7]), float(parts[8]), float(parts[9])
                bones[name] = (idx, parent, px, py, pz, qx, qy, qz, qw)
                order.append(name)
    return bones, order

def blender_to_ue5_pos(x, y, z):
    """Blender Y-up(m) → UE5 Z-up(cm): (-100*x, 100*z, 100*y)"""
    return (-100.0 * x, 100.0 * z, 100.0 * y)

def blender_to_ue5_quat(qx, qy, qz, qw):
    """Blender quat → UE5 space: apply OrientQ = RotZ(180°)*RotX(90°)
    OrientQ = (X=0, Y=0.707107, Z=0.707107, W=0)
    Apply: q_ue5 = OrientQ * q_blender * Inverse(OrientQ)  (conjugate bl quat)
    Actually need: q_ue5 = OrientQ * q_blender  for world rotation in UE5 space
    """
    # OrientQ = FQuat(0, 0.70710678, 0.70710678, 0)
    import math
    O_x, O_y, O_z, O_w = 0.0, 0.70710678, 0.70710678, 0.0

    # q_ue = OrientQ * q_bl (in UE convention: apply bl first, then OrientQ)
    # Hamilton product
    a_w, a_x, a_y, a_z = O_w, O_x, O_y, O_z
    b_w, b_x, b_y, b_z = qw, qx, qy, qz
    r_w = a_w*b_w - a_x*b_x - a_y*b_y - a_z*b_z
    r_x = a_w*b_x + a_x*b_w + a_y*b_z - a_z*b_y
    r_y = a_w*b_y - a_x*b_z + a_y*b_w + a_z*b_x
    r_z = a_w*b_z + a_x*b_y - a_y*b_x + a_z*b_w
    return (r_x, r_y, r_z, r_w)


cpp, cpp_order = load_refskel(os.path.join(DIAG_DIR, "RefSkel_Cpp.txt"))
bld, bld_order = load_refskel(os.path.join(DIAG_DIR, "RefSkel_Blender.txt"))

print(f"C++ bones: {len(cpp)}, Blender bones: {len(bld)}")

# Compare by name
common = sorted(set(cpp.keys()) & set(bld.keys()))
only_cpp = set(cpp.keys()) - set(bld.keys())
only_bld = set(bld.keys()) - set(cpp.keys())
if only_cpp: print(f"Only C++: {sorted(only_cpp)}")
if only_bld: print(f"Only Blender: {sorted(only_bld)}")

# Compare local transforms for common bones
print("\n=== Local Transform Comparison (Blender→UE5 space) ===")
pos_diffs = []
quat_diffs = []

for name in common:
    c = cpp[name]  # (idx, parent, px, py, pz, qx, qy, qz, qw)
    b = bld[name]

    # Convert Blender local pos to UE5 space
    b_px, b_py, b_pz = blender_to_ue5_pos(b[2], b[3], b[4])

    # Convert Blender local quat to UE5 space
    b_qx, b_qy, b_qz, b_qw = blender_to_ue5_quat(b[5], b[6], b[7], b[8])

    cp = (c[2], c[3], c[4])
    bp = (b_px, b_py, b_pz)
    cq = (c[5], c[6], c[7], c[8])
    bq = (b_qx, b_qy, b_qz, b_qw)

    # Position diff
    dx = cp[0] - bp[0]; dy = cp[1] - bp[1]; dz = cp[2] - bp[2]
    pd = (dx*dx + dy*dy + dz*dz)**0.5
    pos_diffs.append((name, pd, dx, dy, dz))

    # Quat diff: dot product (0=opposite, 1=same)
    qdot = abs(cq[0]*bq[0] + cq[1]*bq[1] + cq[2]*bq[2] + cq[3]*bq[3])
    # Convert to angle diff: theta = 2*acos(|dot|)
    import math
    qdot = min(qdot, 1.0)
    angle_diff = 2.0 * math.acos(qdot) * 180.0 / math.pi
    # Normalize to [0, 180] where 180 = opposite
    if angle_diff > 180.0: angle_diff = 360.0 - angle_diff
    quat_diffs.append((name, angle_diff, cq, bq))

pos_diffs.sort(key=lambda x: -x[1])
quat_diffs.sort(key=lambda x: -x[1])

print(f"\n--- Top 20 Local Position Diffs ---")
for i, (name, pd, dx, dy, dz) in enumerate(pos_diffs[:20]):
    print(f"  {name}: Δ={pd:.4f}cm (dx={dx:.2f}, dy={dy:.2f}, dz={dz:.2f})")

print(f"\n--- Top 20 Local Quaternion Diffs (angle in degrees) ---")
for i, (name, angle, cq, bq) in enumerate(quat_diffs[:20]):
    print(f"  {name}: Δ={angle:.1f}°")
    if angle > 1.0:
        print(f"       C++: ({cq[0]:.4f}, {cq[1]:.4f}, {cq[2]:.4f}, {cq[3]:.4f})")
        print(f"       Bld: ({bq[0]:.4f}, {bq[1]:.4f}, {bq[2]:.4f}, {bq[3]:.4f})")

big_quat = [q for q in quat_diffs if q[1] > 1.0]
print(f"\nBones with >1° quat diff: {len(big_quat)}")
print(f"Bones with >10° quat diff: {len([q for q in quat_diffs if q[1] > 10.0])}")
