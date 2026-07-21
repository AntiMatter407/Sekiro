# 楔丸刀身与刀鞘分离设计

| 状态 | 创建 | 更新 |
|---|---|---|
| 🟡 实施中 | 2026-07-19 | 2026-07-19 |

## 一、需求

当前角色运行时为空手，现有 `Kusabimaru_Model` 把刀身与刀鞘合并在同一个 Skeletal Mesh 中。本次需要完成：

1. 将楔丸刀身与刀鞘拆成两个可独立挂载的 Skeletal Mesh 资产。
2. 刀身挂到角色右手 `R_Weapon` 骨骼，刀鞘挂到角色腰部 `Sheath` 骨骼。
3. 两部分仍由同一个 `ASKWeapon` 管理，不拆成两个独立武器 Actor。
4. 为以后拔刀、收刀提供 `Drawn` 与 `Sheathed` 两种通用挂载状态，但本次不接入动画状态机。
5. 攻击碰撞体跟随刀身骨骼，不再错误使用角色骨骼名作为武器内部挂点。

## 二、范围边界

本次允许修改：

- `Docs/design/` 与 `Docs/plan/` 中的武器文档。
- `Script/sekiro_asset_manager/` 中的模型 JSON 拆分管线。
- `Source/Sekiro/Weapon/` 中的武器 Actor 与武器组件。
- `/Game/Weapons/Kusabimaru/` 和 `BP_Kusabimaru` 的武器资产配置。

本次禁止修改：

- `Content/Script/Animation/**`。
- 动画蓝图、动画状态机和动画序列资源。
- 攻击、防御动画选择与过渡规则。

## 三、现有数据依据

`Output/Kusabimaru/Kusabimaru_model.json` 中的模型天然分为两套刚性骨骼分支：

| 分组 | Mesh | 骨骼分支 | 原材质槽 |
|---|---:|---|---:|
| Blade | 0–2 | `WP_A_0300 → Blade00 → Blade01` | 0–2 |
| Sheath | 3 | `Sheath_master → Sheath01` | 3 |

角色骨架中已经存在：

- `R_Weapon`：右手武器骨骼，父级为 `R_Hand`。
- `Sheath`：腰部刀鞘骨骼，父级为 `Pelvis`。
- `LargeSheath`：大型刀鞘挂点，本次不使用。

## 四、资产拆分方案

在 Python 管线中提供通用的“按骨骼根拆分模型 JSON”能力。分组配置只描述目标名称与根骨骼，不在 C++ 或插件中硬编码楔丸路径。

输出派生数据：

```text
Output/Kusabimaru/
├─ Kusabimaru_Blade_model.json
└─ Kusabimaru_Sheath_model.json
```

导入目标：

```text
/Game/Weapons/Kusabimaru/
├─ Kusabimaru_Blade_Model
├─ Kusabimaru_Blade_Skeleton
├─ Kusabimaru_Sheath_Model
└─ Kusabimaru_Sheath_Skeleton
```

拆分器必须：

- 按 Mesh 实际引用的骨骼集合选择分组。
- 保留所需祖先骨骼并重新映射 `ParentIndex`、顶点 `BoneIndices` 与 `BoneIdxToName`。
- 只保留分组使用的材质，并把 `MaterialIndex` 重映射为从零开始的连续索引。
- 保持顶点、三角形、Bind Pose、包围盒和导入比例不变。
- 不覆盖原始 `Kusabimaru_model.json`。

## 五、运行时结构

```text
ASKWeapon
├─ BladeMesh
│  └─ AttackHitbox
└─ SheathMesh
```

初始化后的组件挂载：

```text
角色 SkeletalMesh
├─ R_Weapon → BladeMesh
└─ Sheath   → SheathMesh
```

`ASKWeapon` 提供通用挂载接口，`USKWeaponComponent` 只负责生成武器并把角色 Mesh 与配置的骨骼名传入。资产引用继续在 `BP_Kusabimaru` 配置，C++ 不硬编码 `/Game` 路径。

初始状态使用 `Drawn`：刀身在右手、刀鞘在腰部。`Sheathed` 状态把刀身改挂到刀鞘组件的 `Sheath01` 骨骼，为以后动画事件驱动拔刀/收刀预留接口。

## 六、碰撞挂点

现有攻击胶囊把 `R_Weapon` 当作武器内部 Socket，但该名称只存在于角色骨架。拆分后攻击碰撞必须挂到刀身 `Blade01`，或由后续刀刃 Sweep 使用 `Blade00/Blade01` 两点计算。

本次保留现有 Capsule 碰撞机制，只修正其父组件和内部骨骼挂点；不扩展攻击判定玩法。

## 七、验收标准

1. PIE 中只生成一个 `BP_Kusabimaru` Actor。
2. 右手只显示刀身，腰部只显示刀鞘，没有重复整刀。
3. Locomotion、跳跃期间两个组件稳定跟随对应角色骨骼。
4. `SetWeaponPresentation(Drawn/Sheathed)` 能在不重新生成 Actor 的情况下切换刀身挂点。
5. `AttackHitbox` 跟随刀身而不是角色 `R_Weapon` 名称。
6. C++ 编译、`BP_Kusabimaru` 编译和 PIE 启动均成功。
7. 本次 Git 变更中不存在动画脚本、动画蓝图或动画序列文件。
