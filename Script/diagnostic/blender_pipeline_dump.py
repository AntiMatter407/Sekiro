"""复刻 Blender 管线逻辑，输出与 C++ 同格式诊断数据"""
import json, math, os

MODEL_JSON = "D:/Sekiro/Extracted/Sekiro_model_hkx.json"
ANIM_JSON  = "D:/Sekiro/Extracted/Sekiro_common_anims.json"
OUT_DIR    = "D:/Sekiro/Saved/Logs/SekiroSkinDiag/"

# ── 数学工具 ──────────────────────────────────────────────
def vec3(v): return (v[0], v[1], v[2])
def quat_xyzw_to_wxyz(q): return (q[3], q[0], q[1], q[2])  # JSON xyzw → Blender wxyz

def quat_mul(a, b):
    """a,b = (w,x,y,z)"""
    aw, ax, ay, az = a; bw, bx, by, bz = b
    return (aw*bw - ax*bx - ay*by - az*bz,
            aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw)

def quat_rot(q, v):
    """q=(w,x,y,z), v=(x,y,z)"""
    qv = (q[3], q[0], q[1], q[2])  # wxyz → xyzw
    # cross product
    t = (2*(qv[1]*v[1] + qv[2]*v[2]),  # was t=2*cross(qv.xyz, v)
         2*(qv[2]*v[0] - qv[0]*v[2]),
         2*(qv[0]*v[1] - qv[1]*v[0]))
    # Actually, let me do this correctly
    pass

def rotate_vector_by_quat(q_wxyz, v):
    """Rotate vector v by quaternion q=(w,x,y,z)"""
    qw, qx, qy, qz = q_wxyz
    # q*v*q^-1
    # First compute q * (0,v) * q_conj
    # cross = qv.xyz × v
    cx = qy*v[2] - qz*v[1]
    cy = qz*v[0] - qx*v[2]
    cz = qx*v[1] - qy*v[0]
    # 2*(qw*cross + qv.xyz×cross)
    rx = v[0] + 2*(qw*cx + qy*cz - qz*cy)
    ry = v[1] + 2*(qw*cy + qz*cx - qx*cz)
    rz = v[2] + 2*(qw*cz + qx*cy - qy*cx)
    return (rx, ry, rz)

def matrix_from_locrotscale(loc, rot_wxyz, sc):
    """Build 4x4 matrix (row-major for column-vector multiplication: M*v)"""
    qw, qx, qy, qz = rot_wxyz
    # Rotation matrix from quaternion
    m = [
        [1-2*(qy*qy+qz*qz),     2*(qx*qy-qw*qz),     2*(qx*qz+qw*qy), loc[0]],
        [    2*(qx*qy+qw*qz), 1-2*(qx*qx+qz*qz),     2*(qy*qz-qw*qx), loc[1]],
        [    2*(qx*qz-qw*qy),     2*(qy*qz+qw*qx), 1-2*(qx*qx+qy*qy), loc[2]],
        [0, 0, 0, 1]
    ]
    # Apply scale
    for i in range(3):
        for j in range(3):
            m[i][j] *= sc[j]
    return m

def mat_mul(a, b):
    """a * b (4x4 mat mul). Blender uses column-major: parent @ local"""
    result = [[0]*4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            result[i][j] = sum(a[i][k] * b[k][j] for k in range(4))
    return result

def mat_translation(m):
    return (m[0][3], m[1][3], m[2][3])

def mat_to_quat(m):
    """Extract quaternion (w,x,y,z) from 3x3 rotation part of 4x4 matrix"""
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        w = 0.25 * s
        x = (m[2][1] - m[1][2]) / s
        y = (m[0][2] - m[2][0]) / s
        z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        w = (m[2][1] - m[1][2]) / s
        x = 0.25 * s
        y = (m[0][1] + m[1][0]) / s
        z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        w = (m[0][2] - m[2][0]) / s
        x = (m[0][1] + m[1][0]) / s
        y = 0.25 * s
        z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        w = (m[1][0] - m[0][1]) / s
        x = (m[0][2] + m[2][0]) / s
        y = (m[1][2] + m[2][1]) / s
        z = 0.25 * s
    return (w, x, y, z)

# ── OrientQ: RotZ(180°) * RotX(90°) ─────────────────────
def build_orient_q():
    """RotZ(180°) * RotX(90°) = (w=0.5, x=0.5, y=0.5, z=0.5)"""
    rz = (math.cos(math.pi/2), 0, 0, math.sin(math.pi/2))  # RotZ(180°): w,x,y,z = (0,0,0,1) wait...
    # RotZ(θ): (cos(θ/2), 0, 0, sin(θ/2))
    rz_wxyz = (math.cos(math.pi/2), 0, 0, math.sin(math.pi/2))  # cos90=0, sin90=1 → (0,0,0,1)
    # RotX(θ): (cos(θ/2), sin(θ/2), 0, 0)
    rx_wxyz = (math.cos(math.pi/4), math.sin(math.pi/4), 0, 0)  # cos45=0.7071, sin45=0.7071
    # RotZ * RotX
    return quat_mul(rz_wxyz, rx_wxyz)

ORIENT_Q = build_orient_q()

# ── 加载数据 ──────────────────────────────────────────────
with open(MODEL_JSON, encoding='utf-8') as f:
    model = json.load(f)
with open(ANIM_JSON, encoding='utf-8') as f:
    anim = json.load(f)

print(f"Model: {len(model['Bones'])} bones, {len(model['Meshes'])} meshes")
print(f"Anim: {len(anim['BoneNames'])} bones, {anim.get('AnimationCount', '?')} anims")
print(f"OrientQ: w={ORIENT_Q[0]:.4f} x={ORIENT_Q[1]:.4f} y={ORIENT_Q[2]:.4f} z={ORIENT_Q[3]:.4f}")

# ── 复刻 build_armature: FK World from animation Local ──
anim_names = anim['BoneNames']
anim_parents = anim['BoneParents']
anim_locals = anim.get('BoneLocalTransforms', [])
model_bones = {b['Name']: b for b in model['Bones']}
model_only = [n for n in model_bones if n not in set(anim_names)]

print(f"Model-only bones: {len(model_only)} -> {model_only}")

# FK compute (Blender: parent_mat @ local_mat)
anim_world = {}  # name -> (pos, rot_wxyz, scale, matrix)
for i in range(len(anim_names)):
    name = anim_names[i]
    if name in anim_world:
        continue
    # Build all ancestors first
    stack = [i]
    ancestors = []
    while stack:
        idx = stack[-1]
        n = anim_names[idx]
        if n in anim_world:
            stack.pop()
            continue
        p = anim_parents[idx]
        if p >= 0 and anim_names[p] not in anim_world:
            stack.append(p)
            ancestors.append(p)
        else:
            # Compute FK
            lt = anim_locals[idx]
            loc = vec3(lt['P'])
            rot = quat_xyzw_to_wxyz(lt['R'])  # xyzw → wxyz
            sc = vec3(lt['S'])
            local_mat = matrix_from_locrotscale(loc, rot, sc)
            if p < 0:
                world_mat = local_mat
            else:
                _, _, _, parent_mat = anim_world[anim_names[p]]
                world_mat = mat_mul(parent_mat, local_mat)
            pos = mat_translation(world_mat)
            rot_q = mat_to_quat(world_mat)
            sc_v = (1.0, 1.0, 1.0)  # simplified
            anim_world[n] = (pos, rot_q, sc_v, world_mat)
            stack.pop()

# ── 输出 Blender 骨骼 FK (HKX Y-up 空间, cm) ──
os.makedirs(OUT_DIR, exist_ok=True)

# Bones in animation order
hkx_lines = []
for i, name in enumerate(anim_names):
    pos, _, _, _ = anim_world[name]
    parent = anim_parents[i]
    # m→cm
    px, py, pz = pos[0]*100, pos[1]*100, pos[2]*100
    hkx_lines.append(f"{i}|{name}|{parent}|{px:.4f}|{py:.4f}|{pz:.4f}")

# Model-only bones (use model WorldPos directly, m→cm)
for name in model_only:
    b = model_bones[name]
    wp = b['WorldPos']
    pn = b.get('ParentName')
    parent = -1  # simplified
    px, py, pz = wp[0]*100, wp[1]*100, wp[2]*100
    hkx_lines.append(f"{len(hkx_lines)}|{name}|{parent}|{px:.4f}|{py:.4f}|{pz:.4f}")

with open(OUT_DIR + "Bones_Blender_HKX.txt", 'w', encoding='utf-8') as f:
    f.write('\n'.join(hkx_lines))
print(f"Bones_Blender_HKX.txt: {len(hkx_lines)} bones (HKX Y-up space)")

# ── 复刻 apply_scene_orientation_fix ──
# ExportRoot RotZ(180°): rotate all 180 around Z
# Then Armature RotX(90°): rotate entire armature 90° around X
# Combined: OrientQ = RotZ(180°) * RotX(90°)
# Applied to: (1) armature object, (2) mesh vertices

# Bones after OrientQ (Z-up UE space)
ue_lines = []
for i, name in enumerate(anim_names):
    pos, rot, _, _ = anim_world[name]
    parent = anim_parents[i]
    # m→cm
    p = (pos[0]*100, pos[1]*100, pos[2]*100)
    # Apply OrientQ to position
    up = rotate_vector_by_quat(ORIENT_Q, p)
    ue_lines.append(f"{i}|{name}|{parent}|{up[0]:.4f}|{up[1]:.4f}|{up[2]:.4f}")

for name in model_only:
    b = model_bones[name]
    wp = b['WorldPos']
    p = (wp[0]*100, wp[1]*100, wp[2]*100)
    up = rotate_vector_by_quat(ORIENT_Q, p)
    pn = b.get('ParentName')
    ue_lines.append(f"{len(ue_lines)}|{name}|{-1 if pn is None else 0}|{up[0]:.4f}|{up[1]:.4f}|{up[2]:.4f}")

with open(OUT_DIR + "Bones_Blender_UE.txt", 'w', encoding='utf-8') as f:
    f.write('\n'.join(ue_lines))
print(f"Bones_Blender_UE.txt: {len(ue_lines)} bones (UE Z-up space)")

# ── 输出 Blender 顶点 (OrientQ 后, Z-up UE 空间) ──
# Blender build_meshes: vertices are in Y-up HKX space
# apply_scene_orientation_fix applies OrientQ to armature AND mesh vertices
all_verts = []
vert_count = 0
for mi, mdata in enumerate(model['Meshes']):
    verts = mdata['Vertices']
    if not verts:
        continue
    for v in verts:
        raw_pos = (v['Pos'][0]*100, v['Pos'][1]*100, v['Pos'][2]*100)  # m→cm
        ue_pos = rotate_vector_by_quat(ORIENT_Q, raw_pos)
        all_verts.append(f"{mi}|{vert_count}|{ue_pos[0]:.4f}|{ue_pos[1]:.4f}|{ue_pos[2]:.4f}")
        vert_count += 1

with open(OUT_DIR + "Verts_Blender_UE.txt", 'w', encoding='utf-8') as f:
    f.write('\n'.join(all_verts))
print(f"Verts_Blender_UE.txt: {len(all_verts)} vertices (UE Z-up space)")

# ── 对比总结 ──
print(f"\n=== Summary ===")
print(f"Blender bones (union): {len(anim_names) + len(model_only)}")
print(f"Blender vertices: {vert_count}")
print(f"Output files: {OUT_DIR}")
