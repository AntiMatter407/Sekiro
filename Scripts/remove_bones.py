#!/usr/bin/env python3
"""
从Sekiro模型JSON中删除指定骨骼及其顶点引用。
用法: python remove_bones.py <input.json> [output.json]
"""
import json, sys, os

BONES_TO_REMOVE = {
    'オブジェクト002',
    'HD_L_bone1', 'HD_L_bone2', 'HD_L_bone3',
    'HD_R_bone1', 'HD_R_bone002', 'HD_R_bone03',
}

def process_mesh(mesh):
    """处理单个Mesh Section: 从BoneIdxToName删除骨骼, 重映射顶点索引"""
    bone_map = mesh.get('BoneIdxToName', {})
    if not bone_map:
        return

    # 找出要删除的局部索引
    indices_to_remove = set()
    for idx_str, bone_name in bone_map.items():
        if bone_name in BONES_TO_REMOVE:
            indices_to_remove.add(int(idx_str))

    if not indices_to_remove:
        return

    # 构建重映射表: old_index → new_index (-1表示删除)
    all_old_indices = sorted(int(k) for k in bone_map.keys())
    remap = {}
    new_idx = 0
    for old_idx in all_old_indices:
        if old_idx in indices_to_remove:
            remap[old_idx] = -1
        else:
            remap[old_idx] = new_idx
            new_idx += 1

    # 重建BoneIdxToName
    new_bone_map = {}
    for idx_str, bone_name in list(bone_map.items()):
        old_idx = int(idx_str)
        new_i = remap[old_idx]
        if new_i >= 0:
            new_bone_map[str(new_i)] = bone_name
    mesh['BoneIdxToName'] = new_bone_map

    # 更新顶点BoneIndices
    for vert in mesh.get('Vertices', []):
        old_indices = vert.get('BoneIndices', [])
        old_weights = vert.get('BoneWeights', [])

        new_indices = []
        new_weights = []
        for idx, w in zip(old_indices, old_weights):
            new_i = remap.get(idx, idx)  # 未知索引用原值(不该发生)
            if new_i >= 0 and w > 0:
                new_indices.append(new_i)
                new_weights.append(w)

        # 补齐到4个或截断
        while len(new_indices) < 4:
            new_indices.append(0)
            new_weights.append(0.0)
        new_indices = new_indices[:4]
        new_weights = new_weights[:4]

        # 归一化
        wsum = sum(new_weights)
        if wsum > 0:
            new_weights = [w / wsum for w in new_weights]

        vert['BoneIndices'] = new_indices
        vert['BoneWeights'] = new_weights

def main():
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <input.json> [output.json]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else input_path

    print(f"读取: {input_path}")
    with open(input_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    meshes = data.get('Meshes', [])
    print(f"  {len(meshes)} 个Mesh Section")

    total_removed = 0
    for i, mesh in enumerate(meshes):
        bone_map = mesh.get('BoneIdxToName', {})
        old_count = len(bone_map)
        process_mesh(mesh)
        new_count = len(mesh.get('BoneIdxToName', {}))
        removed = old_count - new_count
        total_removed += removed
        if removed > 0:
            # 找到被删除的骨骼名
            removed_names = set()
            for idx_str, name in bone_map.items():
                if name in BONES_TO_REMOVE:
                    removed_names.add(name)
            print(f"  Section[{i}] '{mesh.get('Part','?')}': 骨骼 {old_count}→{new_count}, 删除: {removed_names}")

    # 备份
    if output_path == input_path:
        backup = input_path + '.bak'
        print(f"备份原文件: {backup}")
        if not os.path.exists(backup):
            os.rename(input_path, backup)
        else:
            print(f"  (备份已存在, 跳过)")

    print(f"写入: {output_path}")
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False)

    print(f"完成: 共删除 {total_removed} 个骨骼映射条目")

if __name__ == '__main__':
    main()
