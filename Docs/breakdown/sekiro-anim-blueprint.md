# 只狼动画逻辑 → UE 动画蓝图

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 进行中 | 2026-06-14 | 2026-06-14 |
> Task 1 ✅ 完成（有已知问题），Task 2-7 待实施
> 发现 AnimID 映射问题：300=垫步非循环，400=奔跑循环；BlendSpace 需修正 |

## 需求描述

将只狼的 TAE 动画逻辑系统（状态机、取消窗口、攻击盒、行为标志）映射为 UE Animation Blueprint + DataAsset 驱动架构。

角色动画已导入 UE（`/Game/Characters/Sekiro/Animations/`，1434 个动画），TAE 事件数据已提取（`Sekiro_TAE_Logic.json`，939+ 动画）。

项目已有：
- `USKAnimInstance` 运行时 AnimInstance（Speed/Angle/MovementTier、JumpTable 标志、InputIntent、CanCancelTo 查询）
- `USKAnimationLogicData` DataAsset（CancelRules / AttackHitboxConfigs）
- `FSekiroAnimBlueprintBuilder` 自动构建管线（`SekiroImport.BuildAnimBlueprint`）
- `ASKCharacter` + `USKMovementComponent`（EnhancedInput 绑定、Walk/Jog/Run/Sprint/Crouch 移动层级）

需要逐步验证和手动完善各模块。

## 任务树

- 🔄 1. Locomotion + Input 集成（角色按键移动）(依赖: 无)
  - ✅ 1.1 确认 Locomotion 动画资产存在于 UE Content 中
  - ✅ 1.2 运行 SekiroImport.BuildAnimBlueprint 生成/更新 ABP + BlendSpace
  - ✅ 1.3 验证 ABP_Sekiro 绑定 USKAnimInstance 为父类
  - ✅ 1.4 验证 AnimInstance 的 Speed/Direction 正确驱动 BlendSpace
  - ✅ 1.5 验证 MovementTier 切换逻辑（Walk↔Jog↔Run↔Sprint↔Crouch）
  - ✅ 1.6 PIE 测试：按键移动时动画平滑过渡（无运行时错误，需手动测试WASD）
  - 🔴 1.7 动画映射修正（AnimID→NameMap 问题）
    - 🔴 1.7.1 BlendSpace 移除 AnimID 300（垫步，非循环移动），改用 AnimID 400 作为最高速
    - 🔴 1.7.2 AnimID 400（Run_Fast_Fwd）首尾帧不连贯修复（DAE 帧范围截断？）
    - 🔴 1.7.3 方向性混合缺失：升级 BlendSpace1D→2D（添加 Walk_Bwd/L/R、Jog_Fwd_L/R、Run_Fast_Fwd_L/R）
    - 🔴 1.7.4 全面审核 SekiroAnimationNameMap.cpp 各 AnimID 映射正确性
- ⬜ 2. Combat 战斗状态机 (依赖: 1)
  - ⬜ 2.1 攻击 Montage 槽位（DefaultSlot）+ Attack_R1 连段
  - ⬜ 2.2 CanCancelTo 查询集成（取消窗口 → Transition Rule）
  - ⬜ 2.3 优先级打断系统（受击>闪避>防御>义手>攻击）
  - ⬜ 2.4 蓄力/跳斩/蹲斩变体
  - ⬜ 2.5 义手忍具（Prosthetic）槽位
  - ⬜ 2.6 道具使用（ItemUse）槽位
- ⬜ 3. Defense 防御/弹刀状态机 (依赖: 2)
  - ⬜ 3.1 Guard/Deflect 状态 + bCanDeflect 标志
  - ⬜ 3.2 弹刀成功 → 反击过渡
  - ⬜ 3.3 闪避状态（Dodge Fwd/Back/Left/Right）
- ⬜ 4. Reaction 受击/死亡状态机 (依赖: 1)
  - ⬜ 4.1 Hit（轻/重/击退）状态
  - ⬜ 4.2 Death + Resurrection 状态
  - ⬜ 4.3 Deathblow 忍杀演出
- ⬜ 5. AnimNotify 注入 (依赖: 2)
  - ⬜ 5.1 CancelWindow Notify（取消窗口开始/结束）
  - ⬜ 5.2 AttackHitbox Notify（攻击盒生成/销毁）
  - ⬜ 5.3 SetFlag/ClearFlag Notify（bCanDeflect 等行为标志）
  - ⬜ 5.4 Sound/FFX/Footstep Notify
- ⬜ 6. DataAsset 完善 (依赖: 2)
  - ⬜ 6.1 CancelRules 数据完整性验证
  - ⬜ 6.2 AttackHitboxConfigs 帧级数据验证
  - ⬜ 6.3 SpEffectConfigs 状态效果配置
- ⬜ 7. 集成测试 (依赖: 1,2,3,4,5,6)
  - ⬜ 7.1 全管线端到端测试（JSON→资产→ABP→PIE）
  - ⬜ 7.2 性能验证（AnimInstance::Tick < 0.1ms）
  - ⬜ 7.3 边界情况验证（25 个边界条件）

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-14 | 创建文档，Task 1 扩展为 Locomotion + Input 集成 |
| 2026-06-14 | 1.1/1.4/1.5 ✅；修复 InferCategoryFromAnimID + BuildLocomotionBlendSpace；方案文档写入 tech-designs |
| 2026-06-14 | Task 1 全部完成 ✅（AI Bridge 驱动）；ABP_Sekiro 父类=SKAnimInstance、SK_Locomotion_BS 5 采样点正确、绑定到 BP_SKCharacter、PIE 无运行时错误 |
| 2026-06-14 | 发现映射问题：通过 TAE 事件分析，AnimID 300（Sprint_Fwd）缺少 BlendToIdleOrMovementAnim + 有 SetTurnSpeed=0，确认为垫步动画非循环移动。AnimID 400（Run_Fast_Fwd）才是真奔跑循环。BlendSpace 错误地将垫步动画用作 Speed=600 采样点。新增子任务 1.7.1-1.7.4 |
