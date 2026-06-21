# 技术设计：模型 JSON 自洽（方案 B）

| 状态 | 创建 | 关联需求 |
|------|------|----------|
| ✅ 已完成 | 2026-06-19 | [sekiro-asset-manager](../breakdown/sekiro-asset-manager.md) 任务 2.2 |

## 1. 背景

`SekiroAssetManager` 插件的 C++ 模型导入模块 `SAModelImporter` 无法成功导入当前 `Sekiro_model.json`，与成熟的 `SekiroImport` 管线一起对比测试，两个 Commandlet 都崩溃在同一个断言：

```
Assertion failed: !bOnlyOneRootAllowed
[File: ReferenceSkeleton.h:204]
→ 只有索引 0 的骨骼可以是根(ParentIndex=INDEX_NONE)，其余骨骼 ParentIndex 必须 < BoneIndex
```

- `SekiroImport` 崩在 `SekiroSkeletonBuilder.cpp:274`
- `SAImport` 崩在 `SAModelImporter.cpp:344`

实测 `SekiroImport` Commandlet 从启动到崩溃 **仅 6 秒**，证明"慢"不是 C++ 导入逻辑的性能问题，而是**导入数据本身不合法**导致崩溃。

## 2. 根因分析

### 2.1 模型 JSON 缺完整骨架父子树

当前 `Output/Sekiro/Model/Sekiro_model.json` 的 `Bones` 来自部件 FLVER `BD_M_9000.flver` 经 `ModelJsonBuilder._merge_bones` 合并：

```
Total bones: 172 | 有 ParentName: 36 | 无 ParentName: 136
```

**136 根骨骼无父引用**（即 136 个根），触发 `bOnlyOneRootAllowed` 断言。

无父骨骼包括 `L_Finger*`、`L_Hand`、`R_Foot_Target*`、`BD_M_*_cloth`、`BD_M_*_sim` 等——这些骨骼**显然本该有父骨骼**（手指挂在手上），但 FLVER 文件里它们的 `parent_index` 就是 -1。

**根本原因**：只狼的完整骨架父子树**只存在于 `skeleton.hkx`（Havok 骨架）**，不在 FLVER 里。FLVER 的 nodes 数组只是网格引用到的骨骼子集，且大量骨骼 `parent_index = -1`（不维护层级）。

| 文件 | 骨骼数 | 有父 | 无父 |
|------|--------|------|------|
| `BD_M_9000.flver`（部件，当前数据源） | 172 | 36 | 136 |
| `c0000.flver`（骨架 FLVER） | 467 | 139 | 328 |
| `skeleton.hkx`（Havok 骨架） | 146 | 145 | **1（仅 Master 根）** |

### 2.2 WorldPos = LocalPos bug

`Script/sekiro_asset_manager/flver_parser.py:1435`：

```python
"LocalPos": list(node.translation),
"WorldPos": list(node.translation),  # 简化：本地=世界  ← BUG
```

FLVER 节点存的是**本地变换**（相对父骨骼），代码直接把本地当世界。实测 172 根骨骼 `WorldPos` 全部等于 `LocalPos`，未做 FK 累积。C++ 侧 `SAModelImporter::ParseBones` 读 `WorldPos` 当世界变换再做 World→Local 推导，数据全是错的。

### 2.3 SekiroImport 为何能跑通

`SekiroImportPipeline.cpp:72-128` 的成熟管线：

1. **步骤 1**：先解析**动画 JSON**（来自 `skeleton.hkx`，146 根完整层级）→ 骨架基础
2. **步骤 2**：`MergeModelWorldTransforms` 用模型 JSON 的 WorldPos 覆盖动画骨骼，`AppendModelOnlyBones` 追加模型独有骨骼
3. 模型 JSON 里 parent_index=-1 的骨骼，靠动画 JSON 的层级补全

即 SekiroImport **不靠模型 JSON 构建骨架**，模型 JSON 只提供 WorldPos + ModelOnly 骨骼。之前测试 SekiroImport 崩溃是因为没传 `-Anim=`，它 fallback 到直接用模型 JSON 构建骨架（`SekiroImportPipeline.cpp:130-138`），同样多根骨骼 → 断言。

## 3. 方案 B：模型 JSON 自洽

让 `Sekiro_model.json` 自带完整骨架树 + 正确 WorldPos，`SAModelImporter` 只读模型 JSON 即可构建合法骨架。符合"模型/动画/材质三模块独立导入"的设计目标。

### 3.1 数据源

动画 JSON（`Sekiro_common_anims.json`，由 `SekiroAnimExtractor.exe` 从 `skeleton.hkx` 提取）已含完整骨架：

```json
{
  "BoneCount": 146,
  "BoneNames": ["Master", "L_Foot_Target2", ..., "RootPos", ...],
  "BoneParents": [-1, 0, 1, 2, 0, 4, 5, 0, ...],
  "BoneLocalTransforms": [
    {"P": [x,y,z], "R": [qx,qy,qz,qw], "S": [sx,sy,sz]},
    ...
  ]
}
```

- 146 根骨骼，**仅 1 根根骨骼**（Master, parent=-1），层级完整
- `P` 单位米，`R` 是 xyzw 四元数，`S` 缩放
- Master 的 R=`[0,0.707,0,0.707]` 是 Y-up→Z-up 根朝向修正

字段格式与 `SekiroAnimationParser::ParseBoneLocalTransform` 完全一致，可直接复用。

### 3.2 ModelJsonBuilder 改造

`Script/sekiro_asset_manager/model_importer.py` 的 `ModelJsonBuilder.build`：

**新增参数**：
- `skeleton_source: str` — 骨架数据源路径。优先动画 JSON（已含 146 根完整骨架），其次 skeleton.hkx（需调提取器生成）。

**改造点**：

1. **骨架基础改为来自 skeleton_source**（替代当前从部件 FLVER 合并）：
   - 读动画 JSON 的 `BoneNames` + `BoneParents` + `BoneLocalTransforms`
   - 构建 146 根骨骼的 `Bones` 数组（Name / ParentName / LocalPos / LocalRot / LocalScale）

2. **FK 累积算 WorldPos**（修复 bug）：
   - 骨骼已按层级顺序排列（父 < 子），可从前向后累乘
   - `WorldTransform[i] = LocalTransform[i] * WorldTransform[ParentIndex[i]]`（根骨骼 World = Local）
   - 输出 `WorldPos` / `WorldRot`(xyzw) / `WorldScale`
   - **复用** `FSekiroSkeletonBuilder::ApplyExportRootOrientation` 的坐标转换逻辑（OrientQ = RotZ(180°) * RotX(90°)）—— 但注意：Master 的本地 R 已含根朝向，是否重复施加需验证（见 3.4 风险点）

3. **部件 FLVER 降级为网格/材质来源**：
   - `Meshes` / `Materials` / `ResolvedMaterials` / `Dummies` / `BoundingBox` 仍来自部件 FLVER（不变）
   - 网格 `BoneIdxToName`：部件 FLVER 的本地骨骼索引 → 骨骼名（`flver_parser._build_output` 已做），C++ 侧再按名映射到 146 根骨架索引
   - 部件 FLVER 独有的骨骼（不在 146 根里，如布料 sim）→ 由 C++ 侧 `SAModelImporter` 追加（对齐 `AppendModelOnlyBones` 逻辑，见 3.3）

4. **`_merge_bones` 弃用**：骨架不再从 FLVER 合并，改为直接用 146 根 HKX 骨架。`_merge_bones` 仅保留给"无 skeleton_source"的回退场景（行为不变，但会输出警告）。

### 3.3 SAModelImporter C++ 改造（最小）

`SAModelImporter.cpp` 需对齐 `SekiroSkeletonBuilder` 的两个成熟做法：

1. **ModelOnly 骨骼追加**（对齐 `AppendModelOnlyBones`）：
   - 当网格 `BoneIdxToName` 里的骨骼名不在 146 根骨架中时，从部件 FLVER 的 `Bones` 补充这些骨骼
   - 当前 `SAModelImporter` 直接丢弃映射不到的骨骼 → 蒙皮缺失
   - 改为：拓扑追加（父骨骼先于子骨骼加入），父骨骼缺失则挂到 Root(0)

2. **多根骨骼防御**（`BuildSkeleton` L336-337 增强）：
   ```cpp
   // 当前：if (i != 0 && (ParentIdx == INDEX_NONE || ParentIdx >= i)) ParentIdx = 0;
   // 增强：记录已见根数量，非首根的 INDEX_NONE 全部强制挂到 0
   ```
   方案 B 修好数据后此防御自然不触发，但作为兜底防止再次断言。

3. **三角形合法性过滤**（对齐 `SekiroSkeletalMeshBuilder`）：
   - 退化三角形（两顶点相同）/ 零面积三角形（三顶点共线）/ 无效索引检测
   - 当前 `SAModelImporter::BuildSkeletalMesh` 只检测两顶点相同，缺零面积和无效索引

### 3.4 风险点

1. **根朝向重复施加**：Master 本地 R=`[0,0.707,0,0.707]` 已是 Y-up→Z-up 修正。若 Python 侧 FK 累积后再用 `ApplyExportRootOrientation` 的 OrientQ 旋转，朝向会错。需验证：
   - 方案 B-1：Python 侧不做 OrientQ 旋转，直接输出 FK 世界变换（含 Master 的根朝向），C++ 侧 `SAModelImporter::BuildSkeleton` 移除 OrientQ 旋转
   - 方案 B-2：Python 侧做 OrientQ，C++ 不做——需确认 Master 的本地 R 是否要清掉根朝向
   - **推荐 B-1**：保持 HKX 原始世界变换，朝向由 Master 根骨骼一次性携带，C++ 侧不再二次旋转。需对比 `SekiroImport` 最终骨架朝向验证。

2. **布料/IK 骨骼是否纳入**：146 根含 IK Target（`L_Foot_Target*`），但部件 FLVER 还有 `BD_M_*_cloth`/`BD_M_*_sim` 布料骨骼不在 146 根里。模型导入是否需要这些布料骨骼？
   - 布料 section（fray/frary）旧管线 `RemoveClothSections` 会过滤掉，对应骨骼可不追加
   - 但 `BD_M_*_cloth` 蒙皮骨骼若被非布料 section 引用，必须追加（对齐 `AppendModelOnlyBones` 的"被网格引用的零位骨骼必须保留"）

3. **skeleton_source 缺失时**：武器模型（WP_*）没有 anibnd/skeleton.hkx，其骨架就是 FLVER 自身的几根骨骼。此时 fallback 到当前逻辑（FLVER 合并），但需修 WorldPos=LocalPos 的 FK 累积（部件 FLVER 内部 parent_index 仍有部分有效，能 FK 的就 FK，不能的保持 LocalPos 作世界）。

## 4. 任务拆分

| # | 任务 | 执行者 | 说明 |
|---|------|--------|------|
| 1 | `ModelJsonBuilder` 增加 skeleton_source 参数 + 读动画 JSON 骨架 | script-agent | Python，`model_importer.py` |
| 2 | FK 累积算 WorldPos（修复 flver_parser WorldPos=LocalPos） | script-agent | Python，新增 FK 工具函数 |
| 3 | 部件 FLVER 降级为网格/材质来源，`_merge_bones` 回退化 | script-agent | Python |
| 4 | `SAModelImporter` ModelOnly 骨骼追加 + 多根防御 | plugin-programmer | C++，`SAModelImporter.cpp` |
| 5 | 三角形合法性过滤 | plugin-programmer | C++，对齐 SekiroSkeletalMeshBuilder |
| 6 | 风险点 1 验证：根朝向是否重复 | function-validator | 对比 SekiroImport 骨架朝向 |
| 7 | 全链路验证：重新生成 JSON + SAImport 导入 | function-validator | 模型+骨架+网格体无断言 |

依赖：1→2→3（Python 串行），4、5 可并行（C++ 独立），6 依赖 1，7 依赖全部。

## 5. 验证标准

- [ ] 重新生成的 `Sekiro_model.json`：`Bones` 为 146 根，仅 1 根根骨骼，`WorldPos != LocalPos`
- [ ] `SAImport` Commandlet 导入无 `bOnlyOneRootAllowed` 断言
- [ ] 导入的 `USkeleton` 骨骼数 = 146（+ ModelOnly 追加）
- [ ] 导入的 `USkeletalMesh` 在视口可见、朝向正确（面朝 +X，Z-up）
- [ ] 蒙皮无长刺（无顶点丢失骨骼影响）
- [ ] SAImport 导入耗时与 SekiroImport 相当（~6 秒级）

## 6. 与父文档的更新关系

实施完成后，更新 [sekiro-asset-manager](../breakdown/sekiro-asset-manager.md)：
- 任务 2.2（SAModelImporter）补充"依赖 skeleton_source 注入完整骨架"
- 任务 1.2（ModelImporter）补充"skeleton_source 参数 + FK 累积"子项
- 变更记录追加本次根因与方案 B