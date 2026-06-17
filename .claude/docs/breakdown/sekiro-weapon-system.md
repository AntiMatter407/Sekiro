# 只狼武器系统（Kusabimaru 全流程）

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔴 未开始 | 2026-06-17 | 2026-06-17 |

## 需求描述

实现只狼武器系统的完整流程：武器模型导入 → 武器挂载 → 武器碰撞受击处理。先以 **Kusabimaru（苦无）** 这一个武器跑通全部流程，后续再扩展其他武器。

**已有基础**：
- 武器资产：`Content/Weapons/Kusabimaru/`（BP_Kusabimaru、SkeletalMesh、PhysicsAsset、Skeleton、Textures、Materials）
- 武器代码：`ASKWeapon`（武器 Actor 基类）+ `USKWeaponComponent`（武器管理组件，挂载到角色骨骼）
- 角色 SKCharacter 已集成 WeaponComponent（BeginPlay 时 InitWeapon）
- TAE DataAsset 已有 `AttackHitboxConfigs` 帧级攻击盒数据（StartFrame/EndFrame/BehaviorJudgeID/AttackType）
- USKAnimationController 已有攻击状态机（连段/蓄力/移动变体）

**核心原则**：
1. Kusabimaru 跑通全部流程（导入→挂载→碰撞→受击），不提前做其他武器
2. 武器碰撞基于 TAE DataAsset 的 AttackHitboxConfigs 帧级数据激活/关闭
3. 武器 Socket 挂载使用现有的 `R_Weapon` Socket

## 任务树

- ⬜ 1. 武器资产验证与导入确认（依赖: 无）
  - ⬜ 1.1 验证 Kusabimaru 现有资产完整性（BP_Kusabimaru 蓝图父类=ASKWeapon、SkeletalMesh 引用正确）
  - ⬜ 1.2 验证角色骨骼上存在 `R_Weapon` Socket，或确认 Socket 挂载配置路径
  - ⬜ 1.3 确认原版只狼武器数量/类型分布（为后续扩展摸底）

- ⬜ 2. 武器挂载完善（依赖: 1）
  - ⬜ 2.1 完善 `ASKWeapon`：添加武器类型枚举（主手/副手/大太刀等），添加拔出/收回状态
  - ⬜ 2.2 完善 `USKWeaponComponent`：支持 InitWeapon 时指定 Socket、添加 Show/Hide 武器接口
  - ⬜ 2.3 BP_Kusabimaru 蓝图配置确认（WeaponMesh、AttachSocketName、PhysicsAsset）
  - ⬜ 2.4 PIE 验证：Kusabimaru 正确生成并挂载到角色 R_Weapon Socket

- ⬜ 3. 武器碰撞体创建（依赖: 2）
  - ⬜ 3.1 确定碰撞方案：PhysicsAsset 碰撞体 vs 运行时动态碰撞体
  - ⬜ 3.2 若选择 PhysicsAsset：验证 WP_A_0300_Kusabimaru_PhysicsAsset 的碰撞体覆盖刀身/刀尖
  - ⬜ 3.3 若选择动态碰撞体：在 USKWeaponComponent 中创建和管理碰撞体
  - ⬜ 3.4 实现碰撞体的激活/禁用接口

- ⬜ 4. 攻击盒碰撞系统（依赖: 3）
  - ⬜ 4.1 USKAnimationController 新增 Tick 时从 AttackHitboxConfigs 读取当前帧攻击盒
  - ⬜ 4.2 实现攻击盒帧级驱动：根据 CurrentAnimTime 计算帧号，查询 DataAsset 是否有活跃攻击盒
  - ⬜ 4.3 攻击盒激活时，启用武器碰撞体；攻击盒关闭时，禁用碰撞体
  - ⬜ 4.4 Overlap 事件处理：检测攻击碰撞体与目标 Actor 的碰撞
  - ⬜ 4.5 多盒支持：同动画内多个攻击盒按帧顺序激活

- ⬜ 5. 受击处理（依赖: 4）
  - ⬜ 5.1 命中检测后触发伤害事件（基于 AttackType / BehaviorJudgeID）
  - ⬜ 5.2 伤害接口：通用 `ApplyDamage` + 武器特定伤害参数
  - ⬜ 5.3 命中反馈：受击动画播放（Reaction 层）、暂不做音效/粒子
  - ⬜ 5.4 受击优先级打断：命中 → 触发 Reaction 动作（Priority 8）
  - ⬜ 5.5 同一攻击不重复命中同一目标

- ⬜ 6. 防护/弹刀基础（依赖: 5）
  - ⬜ 6.1 攻击方与被攻击方武器碰撞交互（武器碰武器）
  - ⬜ 6.2 Deflect 弹刀判定接口预留（基于 Guard 状态 + 弹刀窗口）
  - ⬜ 6.3 弹刀成功 → 触发 Deflect 动画（Priority 6）

- ⬜ 7. 集成测试（依赖: 全部）
  - ⬜ 7.1 PIE 测试：Kusabimaru 可视化挂载正确（位置/旋转/缩放）
  - ⬜ 7.2 PIE 测试：攻击连段时攻击盒按帧正确激活/关闭（碰撞体显示开关验证）
  - ⬜ 7.3 PIE 测试：攻击盒碰撞敌人触发受击和伤害
  - ⬜ 7.4 PIE 测试：多攻击盒动画正确轮换激活
  - ⬜ 7.5 PIE 测试：受击打断正常工作

## 变更记录
| 日期 | 变更 |
|------|------|
| 2026-06-17 | 创建文档，Kusabimaru 跑通武器全流程（导入→挂载→碰撞→受击） |