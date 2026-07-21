"""按骨骼根分组拆分 Sekiro Model JSON。

该模块只处理 JSON 数据，不依赖 Unreal Editor。每个输出模型保留所选网格实际使用的
骨骼祖先链和材质，并重新映射骨骼、顶点和材质索引。原始 JSON 永远不会被覆盖。
"""

from __future__ import annotations

import argparse
import copy
import json
import os
from typing import Dict, Iterable, List, Sequence, Set, Tuple


def _parse_group(value: str) -> Tuple[str, str]:
    """解析 ``Name=RootBone`` 命令行分组。"""
    name, separator, root_bone = value.partition("=")
    if not separator or not name.strip() or not root_bone.strip():
        raise argparse.ArgumentTypeError("分组必须使用 Name=RootBone 格式")
    return name.strip(), root_bone.strip()


def _collect_weighted_bone_indices(mesh: dict) -> Set[int]:
    """收集网格顶点实际使用的全局骨骼索引，忽略零权重占位槽。"""
    used_indices: Set[int] = set()
    for vertex in mesh.get("Vertices", []):
        bone_indices = vertex.get("BoneIndices", [])
        bone_weights = vertex.get("BoneWeights", [])
        for bone_index, bone_weight in zip(bone_indices, bone_weights):
            if float(bone_weight) > 0.0:
                used_indices.add(int(bone_index))
    return used_indices


def _is_descendant_of(bones: Sequence[dict], bone_index: int, root_index: int) -> bool:
    """判断骨骼是否属于指定根骨骼分支，并防御非法父索引或循环层级。"""
    visited: Set[int] = set()
    current_index = bone_index
    while 0 <= current_index < len(bones) and current_index not in visited:
        if current_index == root_index:
            return True
        visited.add(current_index)
        current_index = int(bones[current_index].get("ParentIndex", -1))
    return False


def _collect_bones_with_ancestors(bones: Sequence[dict], used_indices: Iterable[int]) -> List[int]:
    """保留实际蒙皮骨骼及其完整祖先链，并保持原 JSON 中的稳定顺序。"""
    selected_indices: Set[int] = set()
    for used_index in used_indices:
        current_index = int(used_index)
        while 0 <= current_index < len(bones) and current_index not in selected_indices:
            selected_indices.add(current_index)
            current_index = int(bones[current_index].get("ParentIndex", -1))
    return [index for index in range(len(bones)) if index in selected_indices]


def _calculate_bounds(meshes: Sequence[dict]) -> dict:
    """从拆分后的顶点位置重新计算局部包围盒。"""
    positions = [
        vertex.get("Pos", [0.0, 0.0, 0.0])
        for mesh in meshes
        for vertex in mesh.get("Vertices", [])
    ]
    if not positions:
        return {"Min": [0.0, 0.0, 0.0], "Max": [0.0, 0.0, 0.0]}
    return {
        "Min": [min(float(position[axis]) for position in positions) for axis in range(3)],
        "Max": [max(float(position[axis]) for position in positions) for axis in range(3)],
    }


def _build_group_model(source_model: dict, group_name: str, mesh_indices: Sequence[int]) -> dict:
    """构建一个分组模型并重映射骨骼、材质以及顶点蒙皮索引。"""
    source_bones = source_model.get("Bones", [])
    source_meshes = source_model.get("Meshes", [])
    selected_meshes = [copy.deepcopy(source_meshes[index]) for index in mesh_indices]

    weighted_bones: Set[int] = set()
    for mesh in selected_meshes:
        weighted_bones.update(_collect_weighted_bone_indices(mesh))
    selected_bone_indices = _collect_bones_with_ancestors(source_bones, weighted_bones)
    bone_index_map = {
        old_index: new_index
        for new_index, old_index in enumerate(selected_bone_indices)
    }

    selected_bones = []
    for old_index in selected_bone_indices:
        bone = copy.deepcopy(source_bones[old_index])
        old_parent_index = int(bone.get("ParentIndex", -1))
        new_parent_index = bone_index_map.get(old_parent_index, -1)
        bone["ParentIndex"] = new_parent_index
        bone["ParentName"] = (
            selected_bones[new_parent_index].get("Name")
            if new_parent_index >= 0
            else None
        )
        selected_bones.append(bone)

    material_indices = sorted({int(mesh.get("MaterialIndex", 0)) for mesh in selected_meshes})
    material_index_map = {
        old_index: new_index
        for new_index, old_index in enumerate(material_indices)
    }

    fallback_bone_index = 0
    for mesh in selected_meshes:
        mesh["MaterialIndex"] = material_index_map[int(mesh.get("MaterialIndex", 0))]
        for vertex in mesh.get("Vertices", []):
            old_indices = [int(index) for index in vertex.get("BoneIndices", [])]
            weights = [float(weight) for weight in vertex.get("BoneWeights", [])]
            positive_new_indices = [
                bone_index_map[old_index]
                for old_index, weight in zip(old_indices, weights)
                if weight > 0.0 and old_index in bone_index_map
            ]
            vertex_fallback = positive_new_indices[0] if positive_new_indices else fallback_bone_index
            vertex["BoneIndices"] = [
                bone_index_map.get(old_index, vertex_fallback)
                for old_index in old_indices
            ]
        mesh["BoneIdxToName"] = {
            str(new_index): source_bones[old_index].get("Name", "")
            for old_index, new_index in bone_index_map.items()
        }

    result = copy.deepcopy(source_model)
    result["FileName"] = f"{group_name}_model.json"
    result["AssetName"] = f"{group_name}_Model"
    result["OriginalAssetName"] = group_name
    result["SkeletonName"] = f"{group_name}_Skeleton"
    result["Bones"] = selected_bones
    result["Meshes"] = selected_meshes
    result["Materials"] = [
        copy.deepcopy(source_model.get("Materials", [])[index])
        for index in material_indices
    ]
    result["ResolvedMaterials"] = [
        copy.deepcopy(source_model.get("ResolvedMaterials", [])[index])
        for index in material_indices
        if index < len(source_model.get("ResolvedMaterials", []))
    ]
    result["BoundingBox"] = _calculate_bounds(selected_meshes)
    return result


def split_model_by_bone_roots(
    source_model: dict,
    asset_prefix: str,
    groups: Sequence[Tuple[str, str]],
) -> Dict[str, dict]:
    """按根骨骼拆分模型，无法归属或同时归属多个分组的网格会直接报错。"""
    bones = source_model.get("Bones", [])
    meshes = source_model.get("Meshes", [])
    bone_name_to_index = {
        str(bone.get("Name", "")): index
        for index, bone in enumerate(bones)
    }
    root_indices: Dict[str, int] = {}
    for group_name, root_bone_name in groups:
        if root_bone_name not in bone_name_to_index:
            raise ValueError(f"根骨骼不存在: {root_bone_name}")
        root_indices[group_name] = bone_name_to_index[root_bone_name]

    meshes_by_group: Dict[str, List[int]] = {group_name: [] for group_name, _ in groups}
    for mesh_index, mesh in enumerate(meshes):
        used_indices = _collect_weighted_bone_indices(mesh)
        matched_groups = [
            group_name
            for group_name, root_index in root_indices.items()
            if used_indices
            and all(_is_descendant_of(bones, index, root_index) for index in used_indices)
        ]
        if len(matched_groups) != 1:
            raise ValueError(
                f"Mesh {mesh_index} 必须唯一归属一个分组，当前匹配: {matched_groups or '无'}"
            )
        meshes_by_group[matched_groups[0]].append(mesh_index)

    outputs: Dict[str, dict] = {}
    for group_name, _ in groups:
        if not meshes_by_group[group_name]:
            raise ValueError(f"分组没有匹配到网格: {group_name}")
        output_name = f"{asset_prefix}_{group_name}"
        outputs[group_name] = _build_group_model(
            source_model,
            output_name,
            meshes_by_group[group_name],
        )
    return outputs


def split_model_file(
    source_path: str,
    output_directory: str,
    asset_prefix: str,
    groups: Sequence[Tuple[str, str]],
) -> Dict[str, str]:
    """读取源 JSON 并原子式写出各分组文件；源文件路径不得与输出路径相同。"""
    with open(source_path, "r", encoding="utf-8") as source_file:
        source_model = json.load(source_file)
    outputs = split_model_by_bone_roots(source_model, asset_prefix, groups)
    os.makedirs(output_directory, exist_ok=True)

    output_paths: Dict[str, str] = {}
    normalized_source_path = os.path.normcase(os.path.abspath(source_path))
    for group_name, model in outputs.items():
        output_path = os.path.join(output_directory, model["FileName"])
        if os.path.normcase(os.path.abspath(output_path)) == normalized_source_path:
            raise ValueError("输出文件不得覆盖源模型 JSON")
        with open(output_path, "w", encoding="utf-8", newline="\n") as output_file:
            json.dump(model, output_file, ensure_ascii=False, separators=(",", ":"))
        output_paths[group_name] = output_path
    return output_paths


def main() -> int:
    """提供可复用的命令行入口。"""
    parser = argparse.ArgumentParser(description="按骨骼根拆分 Sekiro Model JSON")
    parser.add_argument("source", help="源 Model JSON")
    parser.add_argument("output_directory", help="派生 JSON 输出目录")
    parser.add_argument("--asset-prefix", required=True, help="输出资产名前缀")
    parser.add_argument(
        "--group",
        action="append",
        required=True,
        type=_parse_group,
        help="分组，格式为 Name=RootBone，可重复指定",
    )
    args = parser.parse_args()
    output_paths = split_model_file(
        args.source,
        args.output_directory,
        args.asset_prefix,
        args.group,
    )
    for group_name, output_path in output_paths.items():
        print(f"{group_name}: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
