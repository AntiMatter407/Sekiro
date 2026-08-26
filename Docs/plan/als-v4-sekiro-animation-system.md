# ALS V4 风格只狼动画系统改造

| 状态 | 创建 | 更新 |
|------|------|------|
| ✅ 已完成 | 2026-07-29 | 2026-07-30 |

## 需求描述

以经典 **Advanced Locomotion System V4（ALS V4）** 为学习和架构参照，改造当前只狼角色动画系统。

本需求不是把 ALS V4 的角色蓝图、移动组件和动画资产直接迁入项目，而是：

- 保留 ALS V4 的 Essential Values、Movement State、Movement Action、Rotation Mode、Gait、Stance、Overlay 和姿势修正分层思想。
- 保留项目现有 `ASKCharacter`、`USKMovementComponent`、摄像机、输入和 Root Motion 权威关系。
- 使用只狼原始动画替换 ALS V4 的动画表现层。
- 使用项目 Lua 动画蓝图描述状态机、动画选择和工作流。
- 使用 C++ 提供线程安全、通用的动画数据采集及引擎能力接口。

用户已确认采用方案中的默认动画取舍，不再逐项确认完整资产矩阵。

## 范围

### 首期包含

- Standing/Crouching 地面移动。
- Walk、Run、Sprint 步态。
- Idle、Start、Cycle、Stop、Turn、Pivot 状态。
- Standing/Crouching Turn In Place。
- Jump Start、InAir、Fall Loop、Light/Heavy Land。
- Root Yaw Offset、Lean、Land Prediction。
- Orientation Warping、Foot Placement、Leg IK。
- 普通、持刀、防御和战斗动作分层。

### 首期不包含

- ALS Community 的后续架构和功能。
- Mantle、Vault、攀爬系统。
- Ragdoll 和 Get Up。
- 网络移动系统重写。
- 使用 ALS 动画资产替换只狼动画。
- 无明确需求的 Aim Offset 或枪械 Overlay。

## 已确认的默认决策

| 类别 | 决策 |
|------|------|
| Idle、Start、Cycle、Stop | 使用只狼动画和 Root Motion，保留状态判断与过渡逻辑 |
| 四方向锁定移动 | 使用只狼四方向资源，Orientation Warping 只修正方向残差 |
| Sprint | 使用只狼前向 Cycle、方向起步和方向停止；角色旋转由 Movement Lua 负责 |
| Turn In Place | 使用只狼原地转身动画，重新实现 ALS V4 风格的触发、锁存和 Root Yaw Offset |
| Pivot | 先实现 ALS V4 风格的判断与状态接口；没有合适资产时复用转向起步或保持程序降级 |
| Stride Blend | 不照搬 ALS 的 In-Place Stride Blend；Root Motion 为主，只允许有限播放倍率修正 |
| Jump/Land | 使用只狼原地、方向和轻重落地动画；保留程序化 Land Prediction |
| Lean | 保留程序参数；未找到合适 Additive 资产前不强行生成上身倾斜 |
| Foot IK | 保留 UE Foot Placement 和 Leg IK；动画曲线负责允许、禁用和脚步阶段语义 |
| Overlay | 普通、持刀、防御使用项目动画层；攻击和弹反继续使用全身 Montage |

## 任务树

- ✅ 1. 确认 ALS V4 基线与功能取舍
  - ✅ 1.1 确认以经典 ALS V4 为参照，不使用 ALS Community 作为设计基线
  - ✅ 1.2 确认采用默认动画替换建议
  - ✅ 1.3 确认首期排除 Mantle、Ragdoll 和枪械 Aim Offset
- ✅ 2. 建立 ALS V4 对照数据层
  - ✅ 2.1 盘点并归类现有 `USKAnimInstance` 动画变量
  - ✅ 2.2 新增 Movement Action 和 Overlay State 数据契约
  - ✅ 2.3 实现 Root Yaw Offset、Lean、Fall Speed 和 Land Prediction 数据
  - ✅ 2.4 删除或收敛重复、常量和仅占位字段（依赖: 2.1, 2.2, 2.3）
  - ✅ 2.5 使用 UBT 编译验证数据层（依赖: 2.4）
- ✅ 3. 重构地面 Locomotion
  - ✅ 3.1 统一 Idle、Start、Cycle、Stop、Turn、Pivot 状态职责（依赖: 2.5）
  - ✅ 3.2 接入 Standing/Crouching Turn In Place 和 Root Yaw Offset（依赖: 3.1）
  - ✅ 3.3 接入 Walk/Run/Sprint Root Motion 动画集合（依赖: 3.1）
  - ✅ 3.4 实现方向切换、步态切换和 Pivot 降级策略（依赖: 3.2, 3.3）
  - ✅ 3.5 生成并编译动画蓝图资产（依赖: 3.4）
- ✅ 4. 重构 InAir 与 Land
  - ✅ 4.1 整理 Jump Start、Directional InAir、Fall Loop 和 Land 拓扑（依赖: 2.5）
  - ✅ 4.2 接入 Light/Heavy Land 选择和 Land Prediction（依赖: 4.1）
  - ✅ 4.3 完成落地动画中断与恢复移动曲线（依赖: 4.2）
  - ✅ 4.4 生成并编译动画蓝图资产（依赖: 4.3）
- ✅ 5. 完善姿势修正层
  - ✅ 5.1 收敛 Orientation Warping 的使用范围（依赖: 3.5, 4.4）
  - ✅ 5.2 增加 Grounded/InAir Lean 参数和可选 Additive 接口（依赖: 2.5）
  - ✅ 5.3 完善 Foot Placement、Leg IK 和脚步曲线契约（依赖: 3.5, 4.4）
  - ✅ 5.4 完成同步组、同步标记、惯性化和曲线门控（依赖: 5.1, 5.2, 5.3）
- ✅ 6. 建立 ALS V4 风格 Overlay 分层
  - ✅ 6.1 定义 Default、Sword、Guard、Combat Overlay 语义（依赖: 2.5）
  - ✅ 6.2 普通移动和持刀姿态接入分层图（依赖: 6.1）
  - ✅ 6.3 防御移动接入 Overlay/基础姿态层（依赖: 6.1）
  - ✅ 6.4 保持攻击、弹反和重反馈全身 Montage 优先级（依赖: 6.2, 6.3）
- ✅ 7. 集成验证
  - ✅ 7.1 执行 Lua 文档和静态规范检查
  - ✅ 7.2 重新生成并编译 `ABP_Sekiro`
  - ✅ 7.3 使用 UBT 编译项目目标
  - ✅ 7.4 输出待用户执行的 PIE 验收场景；未经明确授权不启动 PIE
- ✅ 8. 重构为 ALS V4 Animation Layer 蓝图架构
  - ✅ 8.1 新增 `ALI_Sekiro`，声明 `BasePoses`、`BaseLayer`、`OverlayLayer`、`LayerBlending`、`FootIK`
  - ✅ 8.2 扩展 Lua IR 与编辑器插件，支持 Animation Layer Interface、接口实现和 Linked Anim Layer
  - ✅ 8.3 将主 AnimGraph 收敛为五层串联，实际姿势逻辑迁入对应 Animation Layer Function Graph
  - ✅ 8.4 修复 UE5.2 接口函数签名的磁盘持久化，原路径重建主角与 AI 动画蓝图
  - ✅ 8.5 重启编辑器后重新加载并编译两个动画蓝图，确认动画层列表与磁盘资产一致

## 完成标准

- 动画状态命名和职责可以与 ALS V4 的核心概念逐项对照。
- ALS V4 仅作为架构参考，不成为项目运行时依赖。
- 普通、锁定、蹲姿、冲刺和空中状态均使用只狼动画。
- Root Motion、Movement Lua 和动画图之间只有一个明确的 ActorYaw 权威。
- Turn In Place 不再依赖固定为零的 `RootYawOffset`。
- Pivot 缺少专用动画时有稳定的降级路径，不阻塞状态机。
- 动画曲线、同步标记和程序姿势修正具有明确的数据契约。
- 动画蓝图侧栏可直接看到 `BasePoses`、`BaseLayer`、`OverlayLayer`、`LayerBlending`、`FootIK`，主 AnimGraph 只负责按顺序调用这些层。
- Lua 生成、动画蓝图编译和 UBT 编译通过。
- PIE 和玩家输入测试只在用户明确授权后执行。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-07-29 | 创建；确认以 ALS V4 为基线并采用默认动画取舍 |
| 2026-07-29 | 开始任务 2：建立 ALS V4 对照数据层 |
| 2026-07-29 | 完成任务 2.1~2.4；UHT 通过且 Live Coding 编译成功，等待关闭编辑器后执行完整 UBT 编译 |
| 2026-07-29 | 完整 UBT 编译通过，任务 2 全部完成 |
| 2026-07-29 | 开始任务 3.1：收敛地面 Locomotion 状态职责与拓扑 |
| 2026-07-29 | 完成任务 3.1：Turn 重命名为 TurnInPlace，删除失效 StopTurn 路径，建立 Pivot 状态骨架；Lua 文档检查 407/407 |
| 2026-07-29 | 完成任务 3.2：RootYawOffset 驱动原地转身触发、方向锁存与迟滞；Standing/Crouching Turn 资产负责姿势，Movement 独占 ActorYaw |
| 2026-07-29 | 完成任务 3.3~3.5：确认 Standing/Crouching Walk/Run 与 Standing Sprint Root Motion 资产；接入 Pivot 双阈值降级；生成并编译主角与 AI 动画蓝图 |
| 2026-07-29 | 完成任务 4：整理 Jump Start/DirectionalInAir/FallLoop/Land 拓扑；接入 LandPrediction 驱动的 Light/Heavy Land；补齐 Heavy Land 曲线并编译通过 |
| 2026-07-29 | 修复锁定待机未触发 TurnInPlace：进入判断与方向锁存改用未平滑 AimYawDelta，RootYawOffset 只保留为姿势偏差数据 |
| 2026-07-29 | 完成任务 5：主 AnimGraph 划分为 Locomotion、动作分层、姿势修正和最终输出四段；集中 Foot Placement/Leg IK，保留 Lean Additive 接入边界，并重新生成、编译主角与 AI 动画蓝图 |
| 2026-07-29 | 完成任务 6、7：主图展开四类 Overlay，接入武器展示状态，生成并编译角色与 AI 动画蓝图；保留旧 CombatBasePose 状态机作为 UE5.2 资产兼容底座 |
| 2026-07-30 | 开始任务 8：按 ALS V4 蓝图组织方式增加五个原生 Animation Layer；Lua IR 与插件链路已完成，正在修复接口签名重启后丢失的问题 |
| 2026-07-30 | 完成任务 8：接口图改用 UE5.2 Domain Specific Graph 注册并持久化真实 FunctionGuid；原路径重建 `ALI_Sekiro`，重启编辑器后主角与 AI 动画蓝图均重新加载并编译为 UpToDate |
