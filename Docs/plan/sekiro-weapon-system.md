# 只狼武器系统（Kusabimaru 全流程）

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔵 进行中 | 2026-06-17 | 2026-07-19 |

## 需求描述

实现只狼武器系统的完整流程：武器模型导入 → 武器挂载 → 武器碰撞受击处理。先以 **Kusabimaru（楔丸）** 这一个武器跑通全部流程，后续再扩展其他武器。

**已有基础**：
- 武器资产：`Content/Weapons/Kusabimaru/`（BP_Kusabimaru、SkeletalMesh、PhysicsAsset、Skeleton、Textures、Materials）
- 武器代码：`ASKWeapon`（武器 Actor 基类）+ `USKWeaponComponent`（武器管理组件，挂载到角色骨骼）
- 角色 SKCharacter 已集成 WeaponComponent（BeginPlay 时 InitWeapon）
- TAE DataAsset 已有 `AttackHitboxConfigs` 帧级攻击盒数据（StartFrame/EndFrame/BehaviorJudgeID/AttackType）
- USKAnimationController 已有攻击状态机（连段/蓄力/移动变体）
- C++ 碰撞系统已实现（CapsuleComponent + Overlap + 已命中集合）
- 插件 Python stdout 捕获已修复（LogOutput → output 字段）

**核心原则**：
1. Kusabimaru 跑通全部流程（导入→挂载→碰撞→受击），不提前做其他武器
2. 武器碰撞基于 TAE DataAsset 的 AttackHitboxConfigs 帧级数据激活/关闭
3. 刀身使用角色 `R_Weapon` 骨骼，刀鞘使用角色 `Sheath` 骨骼
4. 刀身与刀鞘在导入阶段拆成独立资产，运行时仍由一个 `ASKWeapon` 管理
5. 本阶段不修改动画脚本、动画蓝图、动画序列或动画状态机

详细需求和技术方案见 `Docs/design/sekiro-weapon-separation.md`。

## 任务树

- ✅ 1. 武器资产验证与导入确认
  - ✅ 1.1 验证 Kusabimaru 现有资产完整性
    - BP_Kusabimaru 父类: `/Script/Sekiro.SKWeapon` ✅
    - SkeletalMesh: `WP_A_0300_Kusabimaru` ✅
    - Skeleton: `WP_A_0300_Kusabimaru_Skeleton` ✅
    - PhysicsAsset: `WP_A_0300_Kusabimaru_PhysicsAsset` ✅
  - ✅ 1.2 验证 R_Weapon 存在性
    - Python API 查 SkeletalMesh socket 返回 0，但骨骼名 `R_Weapon` 可通过 AttachToComponent 直接传骨骼名
    - **结论**：不需要额外创建 Socket，骨骼名可直接做 attach
  - ⬜ 1.3 确认原版只狼武器数量/类型分布（为后续扩展摸底）

- 🔵 2. 武器挂载完善（依赖: 1）
  - ✅ 2.1 完善 `ASKWeapon`：删除 `AttachSocketName`（移到 WeaponComponent），`HitboxSocketName` 改为 ReadOnly
  - ✅ 2.2 完善 `USKWeaponComponent`：
    - 新增 `WeaponAttachSocket`（默认 R_Weapon）、`WeaponAttachBoneFallback`（默认 hand_r）
    - `InitWeapon` 挂载时先尝试 Socket，不存在则回退到骨骼名
  - ✅ 2.3 BP_Kusabimaru 蓝图配置
    - 武器模型缩放问题修复：原版武器尺寸比 UE 角色大 50-100 倍
    - 通过 `set_property` 设置 `WeaponMesh.RelativeScale3D = (0.02, 0.02, 0.02)`
  - ✅ 2.4 BP_SKCharacter 配置 WeaponComponent
    - 修复插件 `blueprint set_property` 崩溃（FObjectProperty 的 TSubclassOf 处理）
    - 成功设置 `DefaultWeaponClass` → `BP_Kusabimaru`
    - 成功设置 BP_SKGameMode `DefaultPawnClass` → `BP_SKCharacter`
  - ✅ 2.5 PIE 验证：Kusabimaru 正确生成（`obj list class=SKWeapon` 显示 BP_Kusabimaru_C 已实例化）
  - ⬜ 2.6 可视化验证：挂载位置/旋转需要目测确认，调 WeaponsMesh 缩放

- 🔵 2A. 楔丸刀身/刀鞘分离（依赖: 1）
  - ✅ 2A.1 核对源 JSON：刀身为 Mesh 0–2、刀鞘为 Mesh 3
  - ✅ 2A.2 核对角色挂点：`R_Weapon` 属于右手，`Sheath` 属于 Pelvis
  - 🔵 2A.3 实现通用模型 JSON 骨骼分组拆分器
  - ⬜ 2A.4 生成并导入 `Kusabimaru_Blade_Model` 与 `Kusabimaru_Sheath_Model`
  - ⬜ 2A.5 将 `ASKWeapon` 改为 `BladeMesh + SheathMesh + AttackHitbox`
  - ⬜ 2A.6 将 `USKWeaponComponent` 的默认挂点改为 `R_Weapon + Sheath`
  - ⬜ 2A.7 配置 `BP_Kusabimaru` 的刀身/刀鞘资产
  - ⬜ 2A.8 PIE 验证右手刀身、腰部刀鞘和单 Actor 生命周期
  - ⬜ 2A.9 验证本阶段未修改任何动画相关文件

- ⬜ 3. 武器碰撞体创建（依赖: 2）
  - ⬜ 3.1 C++ 碰撞体已有 CapsuleComponent，验证 PhysicsAsset 碰撞体覆盖
  - ⬜ 3.2 WeaponAttachment 后碰撞体位置/旋转验证

- ⬜ 4. 攻击盒碰撞系统（依赖: 3）
  - ✅ 4.1 AnimationController 已有 `UpdateAttackHitbox()` 从 AttackHitboxConfigs 读取帧级数据
  - ✅ 4.2 实现了帧级驱动：根据 CurrentAnimTime 计算帧号，查询 DataAsset
  - ✅ 4.3 攻击盒激活时启用碰撞体，关闭时禁用 + ClearHitActors
  - ✅ 4.4 Overlap 事件处理 + 已命中集合
  - ⬜ 4.5 多盒支持验证

- ⬜ 5. 受击处理（依赖: 4）
  - ⬜ 5.1 BehaviorJudgeID 映射伤害值
  - ⬜ 5.2 伤害接口完善
  - ⬜ 5.3 命中反馈：受击动画播放
  - ⬜ 5.4 受击优先级打断
  - ⬜ 5.5 同一攻击不重复命中同一目标（✅ 已有 AlreadyHitActors）

- ⬜ 6. 防护/弹刀基础（依赖: 5）
- ⬜ 7. 集成测试（依赖: 全部）

## 变更记录
| 日期 | 变更 |
|------|------|
| 2026-06-17 | 创建文档，Kusabimaru 跑通武器全流程 |
| 2026-06-18 | 完成资产验证(任务1)，修复 Python stdout 捕获 |
| 2026-06-18 | 完善武器挂载(任务2.1-2.2)，C++ Socket回退支持 |
| 2026-06-18 | 修复插件 `HandleSetProperty` 三处崩溃（FClassProperty/UClass断言/FObjectProperty TSubclassOf处理） |
| 2026-06-18 | 配置 BP_SKCharacter/BossSkillGameMode → PIE 验证武器生成成功 |
| 2026-06-18 | 修复武器模型缩放问题（0.02x），原版武器尺寸比 UE 角色大 50-100 倍 |
| 2026-06-18 | 从 FBX 导入改为 JSON 管线导入，通过 SekiroToUEScale=100 自动正确缩放 |
| 2026-07-19 | 增加楔丸刀身/刀鞘分离需求：导入阶段拆分、运行时双 Mesh 独立挂载；明确不修改动画文件 |
