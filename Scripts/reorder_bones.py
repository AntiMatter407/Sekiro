#!/usr/bin/env python3
"""
修复动画JSON骨骼顺序: 将IK Target骨骼和z_dummy移到末尾, 主骨架按层级排列。
用法: python reorder_bones.py <input.json> [output.json]
"""
import json, sys, os

# 判断是否为工具骨骼（应在主骨骼之后）
def is_utility(name):
    return '_Target' in name or name.startswith('z_')

def reorder_bones(data):
    names = data['BoneNames']
    parents = data['BoneParents']
    transforms = data.get('BoneLocalTransforms', [])
    bone_count = len(names)

    # 构建子骨骼列表
    children = [[] for _ in range(bone_count)]
    for i, p in enumerate(parents):
        if p >= 0 and p < bone_count:
            children[p].append(i)

    # DFS遍历: 先主骨骼, 再工具骨骼
    new_order = []

    def dfs(idx, include_utility):
        if include_utility == is_utility(names[idx]):
            new_order.append(idx)
        for c in children[idx]:
            dfs(c, include_utility)

    for include in (False, True):
        dfs(0, include)

    assert len(new_order) == bone_count, f"重排序不完整: {len(new_order)} != {bone_count}"

    # 构建 old→new 映射
    old_to_new = [-1] * bone_count
    for new_i, old_i in enumerate(new_order):
        old_to_new[old_i] = new_i

    # 重排 BoneNames
    new_names = [names[old_i] for old_i in new_order]
    data['BoneNames'] = new_names

    # 重排 & 更新 BoneParents
    new_parents = [0] * bone_count
    for new_i, old_i in enumerate(new_order):
        old_parent = parents[old_i]
        if old_parent >= 0:
            new_parents[new_i] = old_to_new[old_parent]
        else:
            new_parents[new_i] = -1
    data['BoneParents'] = new_parents

    # 重排 BoneLocalTransforms
    if transforms:
        new_transforms = [transforms[old_i] for old_i in new_order]
        data['BoneLocalTransforms'] = new_transforms

    # 重排动画帧数据
    animations = data.get('Animations', [])
    for anim in animations:
        frames = anim.get('Frames', [])
        for frame in frames:
            old_xforms = frame.get('BoneTransforms', [])
            if len(old_xforms) == bone_count:
                frame['BoneTransforms'] = [old_xforms[old_i] for old_i in new_order]

    return old_to_new, new_names

def main():
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <input.json> [output.json]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else input_path

    print(f"读取: {input_path}")
    with open(input_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    print(f"  骨骼总数: {len(data['BoneNames'])}")
    print(f"  动画片段: {len(data.get('Animations', []))}")

    # 打印修改前的顺序
    old_names = data['BoneNames']
    utility_before = sum(1 for name in old_names[:10] if is_utility(name))
    print(f"  前10根中的工具骨骼: {utility_before}")

    old_to_new, new_names = reorder_bones(data)

    # 打印修改后的顺序
    print(f"  重排后前10根:")
    for i in range(min(10, len(new_names))):
        util = '*' if is_utility(new_names[i]) else ''
        print(f"    [{i:3d}] {new_names[i]:35s}{util}")
    print(f"  ...")
    # 找到第一个工具骨骼的位置
    first_utility = next((i for i, n in enumerate(new_names) if is_utility(n)), -1)
    print(f"  第一个工具骨骼在索引: {first_utility}")

    # 备份
    if output_path == input_path:
        backup = input_path + '.bak2'
        if not os.path.exists(backup):
            print(f"备份: {backup}")
            os.rename(input_path, backup)
        else:
            print(f"  (备份已存在)")

    print(f"写入: {output_path}")
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False)

    print("完成!")

if __name__ == '__main__':
    main()
