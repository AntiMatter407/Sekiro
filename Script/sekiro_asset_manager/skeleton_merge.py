"""
MergeModelWorldTransforms + AppendModelOnlyBones 的 Python 复现，
对齐 SekiroImport 的 SekiroSkeletonBuilder.cpp 实现。

用于在 ModelJsonBuilder.build 中合并动画骨骼(146根)和模型骨骼(87根)，
生成最终骨架 Bones 数组。
"""

import math
from typing import Any


def _quat_mul(a, b):
    """四元数乘法 q = a * b (xyzw)。"""
    return [
        a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
        a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
        a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
        a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2],
    ]


def _quat_rotate(q, v):
    """用四元数 q (xyzw) 旋转向量 v (xyz)。"""
    qx, qy, qz, qw = q
    qv_x = qw*v[0] + qy*v[2] - qz*v[1]
    qv_y = qw*v[1] + qz*v[0] - qx*v[2]
    qv_z = qw*v[2] + qx*v[1] - qy*v[0]
    qv_w = -qx*v[0] - qy*v[1] - qz*v[2]
    q_conj = [-qx, -qy, -qz, qw]
    rx = qv_w*q_conj[0] + qv_x*q_conj[3] + qv_y*q_conj[2] - qv_z*q_conj[1]
    ry = qv_w*q_conj[1] - qv_x*q_conj[2] + qv_y*q_conj[3] + qv_z*q_conj[0]
    rz = qv_w*q_conj[2] + qv_x*q_conj[1] - qv_y*q_conj[0] + qv_z*q_conj[3]
    return [rx, ry, rz]


def merge_model_world_transforms(
    anim_bones: list[dict],
    model_bones: list[dict],
) -> None:
    """
    动画骨骼(146根) + 模型骨骼WorldPos覆盖 → 最终骨骼。
    对应 SekiroSkeletonBuilder::MergeModelWorldTransforms。

    1. 对动画骨骼中模型也有的，用模型骨骼的 WorldPos/WorldRot/WorldScale 覆盖
    2. 对动画骨骼中模型没有的(IK Target等)，通过完整 FK 累积算出正确 WorldPos
    3. 重新推导 Local 变换
    """
    # 构建模型骨骼 World 查找表
    model_world = {}
    for b in model_bones:
        t = (
            tuple(b.get("WorldPos", [0, 0, 0])),
            tuple(b.get("WorldRot", [0, 0, 0, 1])),
            tuple(b.get("WorldScale", [1, 1, 1])),
        )
        model_world[b["Name"]] = t

    # 1. WorldPos 覆盖
    replaced = 0
    for b in anim_bones:
        if b["Name"] in model_world:
            wp, wr, ws = model_world[b["Name"]]
            b["WorldPos"] = list(wp)
            b["WorldRot"] = list(wr)
            b["WorldScale"] = list(ws)
            replaced += 1

    # 2. FK 累积（对动画中有但模型中没有的骨骼）
    # 动画骨骼按层级排列（父<子），按顺序 FK
    # 使用完整旋转累积：World[i] = Local[i] * World[parent]
    for i, b in enumerate(anim_bones):
        if b["Name"] in model_world:
            continue  # 已覆盖

        # 本地变换
        local_p = b.get("LocalPos", [0, 0, 0])
        local_s = b.get("LocalScale", [1, 1, 1])

        # 父骨骼 World
        parent_idx = b.get("ParentIndex", -1)
        parent_wp = [0, 0, 0]
        parent_wr = [0, 0, 0, 1]
        parent_ws = [1, 1, 1]
        if parent_idx >= 0 and parent_idx < len(anim_bones):
            p = anim_bones[parent_idx]
            parent_wp = p.get("WorldPos", [0, 0, 0])
            parent_wr = p.get("WorldRot", [0, 0, 0, 1])
            parent_ws = p.get("WorldScale", [1, 1, 1])

        # 完整 FK：WorldPos = ParentWorldPos + ParentRot * (LocalPos * ParentScale)
        local_scaled = [local_p[j] * parent_ws[j] for j in range(3)]
        rotated_offset = _quat_rotate(parent_wr, local_scaled)
        world_pos = [parent_wp[j] + rotated_offset[j] for j in range(3)]
        b["WorldPos"] = [round(v, 10) for v in world_pos]
        # WorldRot = ParentRot * LocalRot（LocalRot 在动画骨骼中已有正确值）
        local_r = b.get("LocalRot", [0, 0, 0, 1])
        if len(local_r) == 4:
            b["WorldRot"] = [round(v, 10) for v in _quat_mul(parent_wr, local_r)]
        else:
            b["WorldRot"] = list(parent_wr)
        b["WorldScale"] = [parent_ws[j] * local_s[j] for j in range(3)]

    # 3. 重新推导 Local 变换
    for i, b in enumerate(anim_bones):
        parent_idx = b.get("ParentIndex", -1)
        if parent_idx >= 0 and parent_idx < len(anim_bones):
            p = anim_bones[parent_idx]
            pw = p.get("WorldPos", [0, 0, 0])
            bw = b.get("WorldPos", [0, 0, 0])
            local_pos = [bw[j] - pw[j] for j in range(3)]
        else:
            local_pos = b.get("WorldPos", [0, 0, 0])
        b["LocalPos"] = [round(v, 10) for v in local_pos]
        b["LocalRot"] = b.get("WorldRot", [0, 0, 0, 1])
        b["LocalScale"] = b.get("WorldScale", [1, 1, 1])


def fk_accumulate_appended(
    merged_bones: list[dict],
    start_idx: int,
) -> None:
    """
    对 merged_bones 中从 start_idx 开始的骨骼做 FK 累积。
    这些骨骼是追加的 ModelOnly 骨骼，其 WorldPos 来自 FLVER 解析器的占位值
    （WorldPos = LocalPos），需要根据父骨骼的正确 WorldPos 重新累积。

    父骨骼（索引 < start_idx）已有正确的 WorldPos（来自 HKX 骨架的 FK 累积）。
    追加的骨骼按父<子拓扑顺序排列，可以顺序计算：
      World[i] = Local[i] * World[Parent[i]]

    注意：使用完整 FK 累积（旋转 + 平移），
    与 merge_model_world_transforms 中的 _quat_mul / _quat_rotate 一致。
    """
    n = len(merged_bones)
    for i in range(start_idx, n):
        b = merged_bones[i]

        # 本地变换（FLVER 空间）
        local_pos = b.get("LocalPos", [0, 0, 0])
        local_scale = b.get("LocalScale", [1, 1, 1])

        # 父骨骼在 merged_bones 中的索引
        parent_idx = b.get("ParentIndex", -1)
        if parent_idx >= 0 and parent_idx < n:
            p = merged_bones[parent_idx]
            parent_wp = p.get("WorldPos", [0, 0, 0])
            parent_wr = p.get("WorldRot", [0, 0, 0, 1])
            parent_ws = p.get("WorldScale", [1, 1, 1])

            # WorldPos = ParentWorldPos + ParentRot * (LocalPos * ParentScale)
            local_scaled = [local_pos[j] * parent_ws[j] for j in range(3)]
            rotated_offset = _quat_rotate(parent_wr, local_scaled)
            world_pos = [parent_wp[j] + rotated_offset[j] for j in range(3)]
            world_rot = _quat_mul(parent_wr, b.get("WorldRot", [0, 0, 0, 1]))
            world_scale = [parent_ws[j] * local_scale[j] for j in range(3)]
        else:
            # 无父骨骼：World = Local
            world_pos = list(local_pos)
            world_rot = b.get("WorldRot", [0, 0, 0, 1])
            world_scale = list(local_scale)

        b["WorldPos"] = [round(v, 10) for v in world_pos]
        b["WorldRot"] = [round(v, 10) for v in world_rot]
        b["WorldScale"] = [round(v, 10) for v in world_scale]

    fk_count = n - start_idx
    print(f"  FK 累积完成: {fk_count} 根 ModelOnly 骨骼的 WorldPos 已修正")


def append_model_only_bones(
    anim_bones: list[dict],
    model_bones: list[dict],
    mesh_bone_names: set[str],
) -> None:
    """
    追加模型独有的骨骼到动画骨骼列表。
    对应 SekiroSkeletonBuilder::AppendModelOnlyBones。

    - 跳过孤立零位骨骼（WorldPos≈0、无父、未被网格引用）
    - 拓扑追加：父骨骼先加入
    - 超过 3 轮无法解析父骨骼则强制挂到 Root(0)
    """
    anim_names = {b["Name"] for b in anim_bones}

    # 找出模型独有骨骼
    pending = []
    for b in model_bones:
        if b["Name"] in anim_names:
            continue
        wp = b.get("WorldPos", [0, 0, 0])
        pn = b.get("ParentName", "") or ""
        near_zero = all(abs(v) < 0.001 for v in wp)
        if near_zero and not pn and b["Name"] not in mesh_bone_names:
            continue  # 孤立零位
        pending.append(dict(b))

    if not pending:
        return

    name_to_idx = {b["Name"]: i for i, b in enumerate(anim_bones)}
    stuck = 0

    while pending:
        added_any = False
        retry = []
        for b in pending:
            pn = b.get("ParentName", "") or ""
            if pn in name_to_idx:
                b["ParentIndex"] = name_to_idx[pn]
            elif pn and pn != "Master":
                retry.append(b)
                continue
            else:
                b["ParentIndex"] = 0

            # 保留 FLVER 骨骼的原始 LocalPos/LocalRot/LocalScale 不变。
            # WorldPos 此时仍是 FLVER 解析器的占位值（=LocalPos），
            # 后续由 fk_accumulate_appended 用原始 Local 变换 + 父骨骼正确 WorldPos 重新累积。
            # 不在此处修改 LocalPos，避免用占位 WorldPos 和正确父 WorldPos 混合计算。
            pi = b.get("ParentIndex", -1)
            if pi < 0 or pi >= len(anim_bones):
                b["ParentIndex"] = 0

            name_to_idx[b["Name"]] = len(anim_bones)
            anim_bones.append(b)
            added_any = True

        pending = retry
        if not added_any:
            stuck += 1
            if stuck > 3:
                for b in pending:
                    b["ParentIndex"] = 0
                    # 保留原始 LocalPos，FK 累积由 fk_accumulate_appended 处理
                    name_to_idx[b["Name"]] = len(anim_bones)
                    anim_bones.append(b)
                break
