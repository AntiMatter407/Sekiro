# Motion Matching 基础移动与 GAS 动作系统（UE 5.8 RootMotion）

| 状态 | 创建 | 更新 | 工作目录 |
|------|------|------|----------|
| 🔄 UE 5.8 主力工程建设中 | 2026-09-07 | 2026-09-13 | `F:/ProjectAI/Sekiro5.8` |

技术方案：[UE 5.8 RootMotion Motion Matching + GAS 技术方案](../design/motion-matching-gas-action-system.md)

## 需求描述

在主力项目 `F:/ProjectAI/Sekiro5.8` 中，将从 UE 5.2 继承的 Motion Matching + GAS 基线升级为 Unreal Engine 5.8.2 的正式系统。在不修改历史参考项目 `F:/ProjectAI/Sekiro`，并保留本项目 Classic 回退链的前提下，实现：

- 基础 Locomotion 完全由所选动画的 RootMotion 生成水平位移；
- MoveIntent 只产生未来查询轨迹，不通过 `AddMovementInput` 或 CMC 加速度生成第二份水平位移；
- 采用 UE 5.8 Pose History、Chooser、Motion Matching、PostSelection 和分阶段数据库；
- 通过持久 Search Request 解决 Idle → Start/Run 不能可靠启动的问题；
- Gameplay 只发布 DesiredMoveYaw/DesiredFacingYaw，最终 ActorYaw 随 RootMotion 在唯一协调器中提交；
- 攻击、防御、闪避、处决和 Traversal 继续由 GAS/Montage 持有确定性动作生命周期；
- Locomotion 与全身 GA 使用互斥的 RootMotion 所有权。

## 固定边界

1. 本任务的工程文件、资产和文档全部位于 `F:/ProjectAI/Sekiro5.8`。
2. 独立旧项目 `F:/ProjectAI/Sekiro` 的 UE 5.2 代码与资产默认只读；只有用户另行明确要求时才能跨项目同步修改。
3. 本项目迁入的 `BP_SekiroMotionMatching`、`ABP_SekiroMotionMatching`、Pose Search 资产和 Lua 源是 UE 5.8 原位适配对象；独立旧项目中的同名文件不修改。
4. UE 5.8 Motion Matching 资产继续使用 canonical 目录 `/Game/Characters/SekiroMotionMatching/**`，不额外增加 `58` 后缀。
5. 始终使用现有 `ABP_SekiroMotionMatching`，以 `Content/Script/Animation/SekiroMotionMatching/ABP_SekiroMotionMatching.lua` 及同目录模块作为完整源码。不得为最小组、验证或后续扩展另建动画蓝图、Probe 或平行实现。先在同一套完整文件中实现最小动画组，再逐步扩展；“完整文件”指源码和动画图的完整描述，不代表立即接入全量动画。
6. 默认只完成代码、Lua 与文档编写，不执行编译、资产生成、测试或 PIE；每种验证都必须由用户明确授权，且授权范围不得互相扩展。

## 决策摘要

1. 动画 RootMotion 是基础地面水平位移的唯一生成源；进入空中后，水平运动只能继承上一阶段 RootMotion 经 CMC 碰撞后的实际速度，不能由输入重新生成。
2. CharacterMovement 只消费 RootMotion，并提供碰撞、坡面、台阶、重力、MovementMode、空中惯性积分和网络移动基础。
3. 轨迹历史读取上一轮 RootMotion 消费后的 Actor 实际结果；地面未来 XY 由 MoveIntent、语义步态配置和实际状态预测，空中未来 XY 保持 CMC 完成速度、Z 按重力预测；两者都不依赖本帧尚未选中的动画，也不写回移动。
4. `UpdateRootMotionLocomotionStates` 负责 Stationary、StartRequested、Moving、StopRequested、PivotRequested、Airborne、Landing 和 ActionOwned。
5. Chooser 负责 Phase/Gait/Mode/Stance 硬门禁；Motion Matching 只在合法数据库数组中比较 Pose Cost。
6. 状态切换通过持久 Search Request 和 PostSelection 确认，不使用单帧 ForceInterrupt 作为可靠协议。
7. ActorYaw 只在 RootMotion Coordinator 中随最终 RootMotion 提交一次。
8. Offset Root Bone Translation 默认关闭；Orientation Warping 和 Leg IK 只能修改表现 Pose。
9. Locomotion Animation 与 FullBody GA Montage 通过 RootMotion Owner Token 互斥。
10. 首先完成 Idle/Start/Run/Stop 最小闭环，再扩展 Pivot、锁定、GAS 和 Traversal。

## 状态说明

- ✅：已在 UE 5.8.2 中重新确认；
- 🟨：产物已从 UE 5.2 迁入，但尚未完成 UE 5.8 编译、资产重建或运行时复验；
- ⬜：尚未实施。

UE 5.2 的历史完成记录不直接等同于 UE 5.8 已完成。

## 当前工程基线核验（2026-09-08）

- 已存在 `USKMotionMatchingTrajectoryComponent`、`USKMotionMatchingAnimInstance` 和 `Content/Script/Animation/SekiroMotionMatching/**`，可作为阶段 1–2 的迁入基线；
- 当前 Lua AnimGraph 已使用 Chooser 驱动 Motion Matching，编辑器预览默认库也已改为 Stationary；正式 `.uasset` 仍待下一次明确授权后重新生成；
- 当前 AnimInstance 已能发布轨迹、步态、锁定状态、期望速度和 Facing 快照，并实现带 Generation 的持久 Search Request、PostSelection 邮箱及最小地面 Phase 状态；
- RootMotion Owner Token、Pivot/Airborne/Landing/ActionOwned 等完整语义，以及分阶段数据库资产的生成与运行复验仍未完成；
- Classic 输入路径仍保留 `AddMovementInput`。这本身不违反回退约束，但 Motion Matching 角色入口必须显式绕开该路径，并通过静态检查证明不会产生第二份基础水平位移；
- 因本轮只执行规划核验，未运行 UBT、资产生成、Cook 或 PIE；现有迁入产物继续保持 🟨，不得升级为 UE 5.8 已验证。

## 当前交接与最小动画组门禁（2026-09-09）

最小动画组只约束验证数据：Idle、Forward Start、Forward Run Loop、Forward Stop。动画蓝图、角色等其他蓝图资产、Lua 与 C++ 均按正式方案维护同一套实现，不另建最小版、测试版或平行系统，也不为四类动画硬编码一套临时架构。正式功能按任务步骤逐步完成，验收时先限制候选动画集合，通过后扩展原有数据配置。

执行方式遵守 AGENTS.md：助手默认只做当前步骤的小范围文档/代码编辑；编辑器查询、编译、生成和运行验证先交给用户执行。以下记录来自此前已执行的操作，本次不重新运行。

- UE 5.8.2 `SekiroEditor` 编译成功；仍有既有 UnLua、GAS API 警告。
- `Sekiro.MotionMatching.Trajectory` 七项自动化测试通过：FiniteValues、SamplingOrder、ZeroInput、SuddenReverse、StartFromRest、ReleaseToStop、GaitProfile。它们只覆盖轨迹组件，不代表动画选择和角色运动闭环通过。
- 轨迹组件已增加 CMC Tick 前置关系；实际 RootMotion 动画更新与采样顺序仍需运行时确认，不能仅凭该关系标记整帧时序已验证。
- SuddenReverse 的横向断言容差已调整为 0.01 cm；此前未记录具体误差值，尚不能把失败原因确定为浮点精度问题。
- 已确认正式 AnimBP 父类为 `SKMotionMatchingAnimInstance`、Skeleton 为 `Sekiro_Skeleton`，编辑器查询状态为 `UpToDate`；这不等于本轮重新编译或 PIE 验收通过。
- 已从迁入资产中确认旧数据库包含 Idle、Forward Start、左右 Pivot、Forward Run Loop、Forward Stop 六个条目，并带有 UE5.2 `PoseSearchExcludeFromDatabaseParameters` 到 UE5.8 `FFloatInterval` 的序列化类型不匹配；当前 Lua 源已改为只全量声明四个最小验收条目，正式资产仍待后续显式生成。

### 下一授权步骤：分步物化并核对正式分阶段数据库

1. ✅ 已编译并加载包含 Python 安全 `Detailed` UFUNCTION 的 `LuaAnimBlueprintEditor` 模块。
2. ✅ 已由 Agent 运行 `Script/rebuild_motion_matching_assets.py`；全部 Lua IR 预检、Stationary/Starts/Loops/Stops 数据库全量覆盖、保存和 `CHT_LocomotionDatabases` 重建均成功。
3. ✅ 已通过 Check → Generate → Save 重新生成正式 `ABP_SekiroMotionMatching`，节点编辑器预览默认库来自 Stationary；未调用原生蓝图编译。
4. 🔄 四个数据库已按 Lua 单项配置生成并开始构建索引；仍需在后续明确授权的编辑器检查中读取正式资产，确认条目、Schema 和索引状态。
5. 动画蓝图编译和 PIE 必须分别明确授权；不得把资产生成、动画蓝图编译和运行验证合并为默认动作。

预期结果：Chooser 在 Stationary、StartRequested、Moving、StopRequested 时分别只返回对应单项数据库；Motion Matching 不再能在 StartRequested 内选择 Idle。旧混合库只作为首次创建正式库的结构模板，不再进入运行时候选。

### 后续待办（当前不执行）

- 核对选定片段的 Skeleton、RootMotion 设置、时长与前向轴；移动片段应提供有效根位移，Idle 不应持续漂移。
- 完成最小组源配置与语义、Chooser、持久搜索确认代码后，由用户生成资产、编译正式 AnimBP。
- 用户在隔离场景验证 Idle → Start → Run → Stop → Idle，并检查碰撞后实际历史和 RootMotion 单次消费。
- 记录结果与失败项；最小组完整闭环验收前，不扩充全量 Locomotion。轨迹反向单测不等于 Pivot 动画已验收。

## 任务树

- ✅ 0. 固定可观察目标、工程边界与最终载体
  - ✅ 0.1 已确认独立目录 `F:/ProjectAI/Sekiro5.8`、UE 5.8.2 引擎关联和独立工作线
  - ✅ 0.2 已确认 `/Game/Characters/SekiroMotionMatching/ABP_SekiroMotionMatching` 是正式最终动画蓝图载体
  - ✅ 0.3 已确认 `Content/Script/Animation/SekiroMotionMatching/**` 对 AnimGraph 结构拥有全量所有权
  - ✅ 0.4 已固定下文可观察结果与失败判据；实际执行分别归入任务 11 的编译、资产和 PIE 门禁，不再把验收执行混入边界定义任务
  - ✅ 0.5 已核对并随实际接入更新项目插件声明、Sekiro 模块依赖及 UE 5.8 PoseSearch/Chooser/MotionTrajectory 接口；AnimationWarping、MotionWarping 和 GAS 所有权的未来调用适配归入任务 10
  - ✅ 0.6 已固定失败处理与本项目内回退流程；具体可恢复版本只在发生失败并取得候选提交或备份后登记，不为完成文档任务执行无目标的破坏性回退演练

- 🔄 1. 建立本帧事实快照与所有权边界（依赖：0）
  - ✅ 1.1 输入层移动档位入口同时持久发布独立的 `RequestedMotionMatchingGait/RequestedMotionMatchingStance`，Movement Lua 构造 Intent 时直接读取二者，不再从 `CurrentMovementTier` 或实际 `bIsCrouched` 反推；Idle 保留上次请求，Crouch 只改变 Stance，Walk/Run/Sprint 发布对应 Gait 并恢复 Standing。用户已完成 C++ 编译，未运行专项 PIE
  - ✅ 1.2 Motion Matching 查询与普通移动链不读取 CurrentAcceleration，也不以 CMC 加速度生成水平位移；未来速度只使用项目配置的查询加减速率逼近 Intent 目标，该配置不写回 Movement
  - ✅ 1.3 OnCharacterMovementUpdated 继续发布 CMC 完成后的 Transform、Velocity、MovementMode、实际步骤位移及 RootMotion 裁剪诊断；统一 Rebase 入口在传送、客户端服务器校正、模拟代理平滑校正及检测到未知位置不连续时清空旧历史，并从校正后的 Actor/CMC 状态发布零位移有效基线，禁止把校正跳变计作 RootMotion。用户已完成 C++ 编译，未运行传送或网络专项验证
  - ✅ 1.4 Intent 已分别保存世界 MovementDirection、DesiredMoveYaw、DesiredFacingYaw 与 RotationMode；任务 9.1 又关闭了 Motion Matching 普通移动中的 Lua ActorYaw、CMC 自动朝向和 Classic 方向重定向争抢
  - ✅ 1.5 已固定并实现时序：AnimInstance Update 从最新 Intent 与上一轮 CMC 完成样本构建本次 Query，最终 Pose 在 Evaluate 后由 Pose History Collector 记录并供下一轮搜索使用

- ✅ 2. 生成历史—当前—未来查询轨迹（依赖：1；传送/网络失效恢复仍由 1.3 追踪）
  - ✅ 2.0 `BuildMotionMatchingQuerySnapshot` 已接入 AnimInstance，整体复制正式 Intent/ActualState，以 GaitProfile 计算目标查询速度，拼接完成移动历史、零时刻与未来积分，并发布版本和明确失效原因；后续正式 AnimBP 与最小组 PIE 均已消费该快照
  - ✅ 2.1 历史段只使用 OnCharacterMovementUpdated 发布的碰撞约束后 ActorTransform，不使用动画原始曲线或未约束 RootMotion Delta 冒充实际结果
  - ✅ 2.2 当前点固定为时间 0、局部零位置和单位 Facing；构建末尾统一拒绝非有限或非严格递增样本，SamplingOrder 与 FiniteValues 自动化测试已通过
  - ✅ 2.3 未来段由 MoveIntent、RequestedGait 的动画 RootMotion 标称速度配置和当前实际状态预测，不读取本帧候选动画，不调用 `AddMovementInput`，也不写 CharacterMovement
  - ✅ 2.4 FiniteValues、SamplingOrder、ZeroInput、SuddenReverse、StartFromRest、ReleaseToStop、GaitProfile 七项轨迹自动化测试已覆盖持续/零输入、释放减速、静止启动、突然反向和步态配置
  - ✅ 2.5 未来位置/速度只朝 DesiredMoveYaw 积分，Facing 独立朝 DesiredFacingYaw 有限转向；Free/Locked 通过 RotationMode 选择面向语义，不把 Facing 当作位移方向
  - ✅ 2.6 全部历史、现在与未来样本只在 `TransformTrajectoryToQuerySpace` 执行一次 Sekiro Translation `-90°` 轴适配；Schema 契约和 34 维资产回读已确认没有第二转换点
  - ✅ 2.7 已固定并实现 `Query(N) → SelectedPose(N) → RootMotion(N) → CMC 完成样本/ActualState(N+1)`；本帧 Query 不读取本帧选择结果，完整碰撞与消费诊断继续由任务 9.5–9.6 验收

- 🔄 3. 从连续事实更新离散语义状态（依赖：2）
  - ✅ 3.0 AnimInstance 已发布完整 Phase 状态快照；最小地面链 `Stationary → StartRequested → Moving → StopRequested → Stationary` 已通过持久请求、PostSelection 与后续 CMC 完成样本确认，并在最小组 PIE 中跑通
  - 🔄 3.1 Stationary、StartRequested、Moving、StopRequested 已实现并验证；大角度方向迟滞和 Start 回退源码已完成 C++ 编译。真正的 PivotRequested 仍需任务 10.1 先接入合法 Pivot 数据与 Chooser 候选后完成
  - ✅ 3.2 MovementMode 与细分 Airborne/Landing 状态已接入：状态快照独立发布 CMC MovementMode/CustomMovementMode，非地面按竖直速度推进 JumpStart/Ascending/Apex/Falling，首次接地进入 Landing。JumpStart 与 Landing 均已接入 Chooser 数据门禁、持久请求和 PostSelection 确认；JumpStart 必须完整保留选中动画剩余播放窗口并获得选择后的空中 CMC 完成样本才退出。Ascending/Apex/Falling 共享 `InAirPoseOnly` 数据并直接用同代 PostSelection 确认，Movement 在进入这些姿势阶段时显式接管空中惯性，避免出站混合留下的零位移 Anim RootMotion 参数覆盖 JumpStart 水平速度。`Run + Free + Standing` 专项 PIE 已完整经过 JumpStart/Ascending/Apex/Falling/Landing 并通过。无覆盖组合保留安全退出；旧 Airborne 与 ActionOwned 继续禁止基础 Locomotion 搜索，RecoveryRequested 因尚无数据仍不会启用搜索
  - ✅ 3.3 `RequestedGait` 只选择动画 RootMotion 标称速度族，`ActualPlanarSpeed` 独立来自 CMC 完成样本；状态机不把请求步态冒充实际速度
  - ✅ 3.4 无输入时 Intent 保留最后有效 `DesiredMoveYaw`，不再建立重复的 LastMovementDirection 状态；Stop、后续 Pivot/Turn-in-place 可从同一事实读取方向连续性
  - 🔄 3.5 `PreviousPhase + TransitionReason` 结构化原因码与未来轨迹条件已完成 C++ 编译。GAS 标签源码现已接入：AnimInstance 在游戏线程从 Pawn 的 ASC 复制完整标签快照，以 Lua 配置的 `Dying/Dead/Reviving` 集合做 Any 阻塞；独立 `GameplayTagRevision` 保证阻塞解除后创建新一代持久请求。标签阻塞只暂停基础 Locomotion 搜索，不伪装为 `ActionOwned` 或 RootMotion Owner。等待本批 C++ 编译及后续显式授权的 AnimBlueprint 重新生成，尚未运行 GAS 专项 PIE
  - ✅ 3.6 状态层只发布 Phase/Gait/Mode/Stance 与搜索请求事实；具体数据库由 Chooser 决定，具体动画、时间和 Cost 由 Motion Matching 决定

- 🔄 4. 用 Chooser 建立合法数据库集合（依赖：3）
  - ✅ 4.1 项目 Lua 已全量声明并生成 Stationary、Starts、Loops、Stops 四个正式单项数据库；资产回读确认各库只有对应 Idle、Forward Start、Forward Run Loop、Forward Stop，索引维数、姿势数及依赖正确，旧混合库不再进入运行时候选
  - ✅ 4.2 正式 `CHT_LocomotionDatabases` 已生成四条 Phase/Gait/Mode/Stance → 单项阶段数据库规则；最小组 PIE 已验证四个阶段依次只提交、选择和确认对应合法数据库，StartRequested 不再选择 Idle
  - ✅ 4.3 Chooser IR 已明确只输出合法数据库数组，不包含具体动画、PoseIdx、播放时间或 Cost；这些结果仍由 Motion Matching 在合法集合内决定
  - ✅ 4.4 有效移动意图的新 StartRequested/Moving/PivotRequested 持久请求会使用 UE5.8 `ForceInterruptAndInvalidateContinuingPose`，普通手动重搜仍使用 `ForceInterrupt`；源码、正式 AnimBP 和最小组 PIE 已验证 Idle Continuing Pose 不会阻塞启动
  - 🔄 4.5 Chooser IR 已固定 `EmptyResultPolicy = Error`；运行时代码会提交显式空数据库数组、失效 Continuing Pose，并按 Generation 限频输出 `NoEligibleDatabase`。正常候选 PIE 已通过且该错误计数为零，但尚未执行“故意制造无匹配语义”的运行时负例

- ✅ 5. 在正式 AnimBP 中建立 Provider 与最终姿势闭环（依赖：2、4；Chooser 空结果负例仍由 4.5/11.5 追踪）
  - ✅ 5.0 AnimInstance 已在游戏线程构建并保存完整 QuerySnapshot，轨迹、期望速度、请求步态和面向均派生于同一快照；无效查询清空本帧轨迹/标签并发布原因。该路径已完成 C++ 编译、正式 AnimBP 接线和最小组 PIE
  - ✅ 5.1 `ABP_SekiroMotionMatching` 与 `USKMotionMatchingAnimInstance` 已完成迁入基线的 UE 5.8 API 适配
  - ✅ 5.2 正式 Lua 图与生成资产均已连接父类 MotionMatchingTrajectory → HistoryCollector.TransformTrajectory，并显式关闭 bGenerateTrajectory；AnimInstance 发布 bMotionMatchingQueryValid，图中以零时长 Bool 门禁在 Motion Matching 与 Local Reference Pose 间切换，无效查询不继续更新搜索分支，Collector 包住门禁后的最终姿势
  - ✅ 5.3 正式 AnimGraph 已建立 `Output Pose ← Pose Search History Collector ← Query Gate ← Motion Matching/Reference Pose` 的单一拓扑，并把 AnimInstance 发布的 `MotionMatchingTrajectory` 接到 Collector 的 UE5.8 `TransformTrajectory` Pin；Canonical IR 回读确认 Collector 在 Source 更新前提供同一 Provider，在 Source Evaluate 后采集门禁后的最终混合姿势，Motion Matching 内部 Standalone Blend Stack 的输出不会绕过历史采集
  - ✅ 5.4 项目侧不存在用于“对齐” Pose History 的上一帧输入缓存：`SubmitMotionMatchingIntent()` 原子替换最新意图，AnimInstance 同次更新直接复制该意图并结合最近 CMC 完成状态构造 Query；只有最终 Pose 与碰撞后实际状态按反馈定义进入下一轮。BeginPlay 静止安全 Intent、分离的 MissingIntent/MissingActualState 诊断、Query 恢复及最小组 PIE 已验证初始化不会永久关闭查询
  - ✅ 5.5 正式 `ABP_SekiroMotionMatching` 已由 Lua 全量生成、原生编译并保存；只读 Canonical IR 回读确认实际 `.uasset` 包含 Motion Matching、零混合时间查询门禁、Local Reference Pose、安全回退与 Pose Search History Collector，六条连接和两个节点函数绑定均与 Lua 源一致，没有叠加 Slot/GAS、IK、Orientation/Offset Root、Warping 或其他后处理节点。当前 AnimGraph 已改用 Chooser 输出的阶段数据库数组，不再直接引用旧 `PSD_SekiroFreeRun_Minimal`；用户于 2026-09-11 报告当前最小动画组测试通过。该结论只覆盖 Idle、Forward Start、Forward Run Loop、Forward Stop，Pivot 和全量 Locomotion 不计入本轮验收

- ✅ 6. 固定 Schema 与 BuildQuery 契约（依赖：5）
  - ✅ 6.1 项目 Lua 已建立 `SchemaContracts.Locomotion` 纯值契约：Trajectory 采用 UE5.8 原生 Locomotion 基线 `-0.40/0/0.35/0.70s` 采样，按 PositionXY、VelocityXY、FacingDirectionXY 的原生 Finalize 顺序展开为 16 维；Pose 使用 Sekiro 实际骨骼 `Pelvis/L_Foot/R_Foot` 的 Position、Velocity，展开为 18 维；组权重分别为 7.0 与 1.0，DataPreprocessor 为 Normalize，总 Cardinality 固定为 34。四个数据库的 Schema 引用已统一改由该契约提供；当前仅完成源码声明，尚未物化或回读 Schema 资产
  - ✅ 6.2 已把坐标空间纳入项目 Lua 的共享 Schema 契约：查询原点唯一取最近完成移动反馈的无缩放 `ActorTransformWS`；Trajectory Position/Facing 分别在统一边界逆变换到 QueryOrigin 空间，当前样本为零位置/单位 Facing；Pose Position 相对 Schema Root，Pose Velocity 使用 Character Space。只狼动画轴差异只允许在唯一适配点对 Translation 派生特征应用 `-90°` Yaw，Facing 不重复补偿；世界速度、绝对地图位置与世界 Facing 只可作为构建中间值，`WorldSpaceFeaturePolicy=Reject` 禁止其直接进入最终 34 维向量。定向源码检查确认正式 `BuildMotionMatchingQuerySnapshot()` 已按该契约构建历史、现在与未来轨迹，无需修改 C++
  - ✅ 6.3 项目 Lua 已为 Stationary、Starts、Loops、Stops、Pivots、Lands 六个正式阶段族建立 `PhaseSchemaAssignments`，首版统一使用 Locomotion 34 维 Schema；新增 `PSN_FreeRun_Locomotion` Normalization Set 契约和版本化反射 IR，当前只纳入真实存在且含合法最小动画的四个阶段数据库，并让每个数据库补丁反向写入同一 `NormalizationSet`。Pivots、Lands 作为 `DeferredDatabaseFamilies` 保留，未建立合法数据库前不进入 Chooser、联合统计或生成空资产。IR 会拒绝成员 Schema/Set 不一致；资产重建脚本也已改为预检全部 IR，先创建 Set 与数据库引用目标，再依次写入 Set、数据库和 Chooser。当前仅完成 Lua、Python 与文档，Normalization Set 及更新后的数据库资产尚未生成
  - ✅ 6.4 项目 Lua 已通过 `CompileSchemaIR/CompileLocomotionSchemaAssetPatch` 从同一 `SchemaContracts.Locomotion` 全量生成 Skeleton、Trajectory/Pose Channel、采样点、特征位掩码、权重和 34 维布局；IR 按 UE5.8 Channel Finalize 顺序逐段核对 `FeatureVectorLayout`，固定关闭 Padding 与额外 Debug Channel。通用插件的项目无关 `InstancedObject` 反射值可创建任意 `EditInlineNew` 子对象，并以真实属性快照与原子对象保活保证安全回滚；资产脚本依次重建 Schema → Normalization Set → Database → Chooser。针对 UE5.8 强制压缩采样，项目资产管理插件在游戏线程同步获取、验证并保持数据库 IR 所列动画的当前平台 Residency，最终通过 `finally` 释放。用户完成编译与资产生成后，只读回查确认：Schema 为 Normalize、1 个 Permutation、无 Padding/Debug 注入；Trajectory 四个采样点为 `-0.40/0/0.35/0.70s` 且组权重 7.0；Pose 为 `Pelvis/L_Foot/R_Foot` 的 Position+Velocity、Character Space Velocity 且组权重 1.0；四库均为 BruteForce、Cardinality 34、各含一个正确动画依赖，实际姿势/Values 分别为 `92/3128`、`49/1666`、`32/1088`、`44/1496`，全部满足 `Values = Poses × 34`。Normalization Set 与 Chooser 只依赖这四库，未回连旧混合数据库。运行时搜索与阶段确认继续由任务 7 和最终最小组验收覆盖
  - ✅ 6.5 项目 Lua 已新增独立于 Database Eligibility、Schema Layout 与全部 Weight/Cost Bias 的 `AuthoringValidation` 验收策略，并让 Schema、Database、Normalization Set、Chooser 的每个公开 IR 编译入口先执行该门禁。当前最小组四个 `Phase + Run + Free + Standing` 组合必须由指定数据库唯一覆盖并启用指定语义动画；遗漏、额外组合、重叠、错误路由、全禁用数据库或跨阶段复用动画都会硬失败。Trajectory 另行校验查询原点/坐标空间、历史 PositionXY、当前 VelocityXY/FacingDirectionXY、未来 PositionXY/FacingDirectionXY、至少 0.70 秒远期及远期 VelocityXY，不读取任何权重。2026-09-11 纯 IR 预检确认 Schema、Normalization Set、四个数据库和 Chooser 共七个公开入口全部通过；临时内存负例确认缺失 Start 动画、Start 错接 Moving 门禁和未来轨迹缩短至 0.60 秒均被正式编译入口拒绝，预检模块随后删除。未重新生成资产、编译 Blueprint 或运行 PIE

- ✅ 7. 建立 Continuing Pose、搜索与确认生命周期（依赖：6）
  - ✅ 7.0 AnimInstance 已完成持久 Search Request、Generation/PhaseRevision/QueryRevision、PostSelection 单槽邮箱、同代数据库集合校验与“选择后新 CMC 完成样本”确认闭环。最小动画组 PIE 已验证 `Stationary → StartRequested → Moving → StopRequested → Stationary`，各代请求均获得合法选择并由后续实际状态确认。Pivot 不属于当前最小数据集，留待 10.1 扩展后验证
  - ✅ 7.1 UE5.8 原生 Motion Matching 节点以当前播放资产与时间建立 Continuing Pose；项目侧 `SekiroMotionMatchingConfig.lua` 统一声明四个阶段库、共享 Schema、节流、重选历史、Blend Stack 与 Cost Bias 基线。生成资产与运行时阶段切换均已验证
  - ✅ 7.2 最小数据集的合法范围已固定为 Stationary/StartRequested/Moving/StopRequested、Run、Free、Standing，四个数据库的 `.uasset` 回读已确认 `PoseSearchMode = BruteForce`。PivotRequested 只保留运行时枚举，暂不生成空数据库；PCAKDTree/VPTree 不在当前正确性基线范围
  - ✅ 7.3 PostSelection 快照已记录并校验 UE5.8 公开结果的 Database、Asset、SelectedTime、Cost、Mirror 和 WantedPlayRate，运行时已验证同代选择确认。公开 `FPoseSearchBlueprintResult` 不提供 PoseIdx，因此不从时间反推不稳定索引；该限制不阻塞当前生命周期验收
  - ✅ 7.4 已定义带独立 Generation、PhaseRevision、QueryRevision、Phase、Gait、Stance、Mode、请求时间、搜索等待时间、最短保持时间、显式生命周期状态和 Pending 的持久 Search Request；匹配选择后 Pending 仍保持，直到后续实际状态确认
  - ✅ 7.5 Motion Matching 基础 `UpdateFunction` 已绑定到线程安全 `Update_MotionMatching_SearchRequest`：每帧先通过 `EvaluateChooserMulti` 取得合法数据库集合，再以 `SetDatabasesToSearch` 和受请求生命周期控制的中断策略一次提交。正式 AnimBP 与 Chooser 资产已重建，PIE 已证明四个阶段库依次提交、选中并确认，且 RootMotion 确实推动 Actor
  - ✅ 7.6 `Update_MotionMatching_PostSelection` 只接收当前已发布 Generation、且 SelectedDatabase 属于同代 Chooser 实际提交集合的首个完整搜索结果；结果入邮箱前再次确认发布 Generation 未变化。游戏线程只在请求仍等待选择时校验 Generation 与 Phase/Gait/Stance/Mode，任何过期、重复或不匹配结果均在写入 SelectionSnapshot 前拒绝。合法结果仅将请求置为 `AwaitingActualStateConfirmation`。真正的请求确认与 Phase 推进必须等待晚于选择的 CMC 完成移动样本满足 Start 或 Settled 条件，PostSelection 本身不再冒充 RootMotion 已开始推进
  - ✅ 7.7 Search Acknowledgement 超时只在当前 Generation 已发布到 AnimGraph 后开始计时，且至少经过两次完整搜索评估机会才允许报错，避免低帧率下在首个 PostSelection 前用整帧 `DeltaSeconds` 制造假超时。低帧率 PIE 复验中 `MotionMatchingSearchNotAcknowledged = 0`、`MotionMatchingSearchAcknowledgedLate = 0`、`NoEligibleDatabase = 0`，迟到恢复和不清除 Pending 的故障语义仍保留

- ✅ 8. 由 Blend Stack 输出最终 Locomotion Pose（依赖：7）
  - ✅ 8.1 定向核对 UE5.8 `FAnimNode_MotionMatching::UpdateAssetPlayer`：节点每帧以 Blend Stack 当前样本的资产、累计时间、镜像与混合参数更新 Continuing Pose；只有搜索结果存在且 `bIsContinuingPoseSearch == false` 时才调用 `FAnimNode_BlendStack_Standalone::BlendTo(...SelectedTime...)`。Continuing Pose 胜出时不重复入栈，而是继续当前样本并更新 WantedPlayRate；项目无需重复实现该引擎语义
  - ✅ 8.2 已按 UE5.8 真实实现固定边界：当前 Motion Matching 节点的 BoundGraph 保持默认 `Blend Stack Input → Result` 直连，编译器判定为无操作后清空 `PerSampleGraphPoseLinks`，运行时由每个 `FBlendStackAnimPlayer` 直接评估自己的单个动画样本。未来只在需要逐样本动画处理时才扩展 BoundGraph；届时编译器会为每个活跃样本及一个存储姿势各克隆一份子图，每份仍只接收一个 Sample。`FAnimNode_BlendStack_Standalone` 始终独占 AnimPlayers 生命周期、混合权重、数量上限以及 Pose/Curve/Attribute 最终合成；不在 BoundGraph 内再建多样本 Blend Stack，也不把 Collector 放入逐样本子图
  - ✅ 8.3 已将首版基线集中到项目 Lua：`BlendTime = 0.18`、`MaxActiveBlends = 3`、无 BlendProfile、`BlendOption = Linear`、`PoseJumpThresholdTime = (0,0)`、`PoseReselectHistory = 0.30`、`SearchThrottleTime = 0.10`、`PlayRate = (1,1)`、`PlayRateMultiplier = 1.0`、`bUseInertialBlend = false`。固定 1.0 播放速率保证基础 Locomotion 位移仍由原始动画 RootMotion 提供；首版不叠加惯性化，也不在没有运行时证据前引入骨骼 BlendProfile。通用 Lua AnimBlueprint 插件已新增项目无关的 `Struct` IR 值，可按目标 `FStructProperty` 安全物化任意 UE 结构体，因而不需要为 `FFloatInterval` 或 Motion Matching 硬编码专用分支。正式资产逐字段 Canonical IR 回读全部通过；唯一 `Reader.EventGraphExcluded` 警告符合“Lua 只全量拥有动画图、EventGraph 保持蓝图侧所有权”的边界。UE 原生编译零诊断，资产状态为 `UpToDate`
  - ✅ 8.4 Canonical IR 回读确认最终链路为 `MotionMatching → MotionMatchingQueryGate → PoseSearchHistoryCollector → Output`，Collector 接收节点内部 Blend Stack 的最终混合姿势，作为下一帧 Pose Channel 历史反馈；轨迹仅接入 Collector 的 `TransformTrajectory`，未建立第二份预测或平行混合链

- 🟨 9. 提取、协调并单次消费 RootMotion（依赖：8）
  - ✅ 9.1 已以 `USKMotionMatchingTrajectoryComponent` 作为正式路径能力标记：`USKMovementComponent` 在原生 Tick 中强制关闭 `bOrientRotationToMovement`、`bUseControllerDesiredRotation` 和旧方向重定向运行时开关；脚本接口不能为 Motion Matching 角色重新启用这三项，RootMotion 后转换委托也做最终拒绝。Movement Lua 完成 Intent 发布后，仅在非全身动作的 Motion Matching 普通移动路径提前退出，不再调用 Lua ActorYaw 或 Classic 水平 RootMotion 重定向；Classic 角色和现有全身攻击曲线转向暂时保持原行为。用户已完成 C++ 编译，PIE 留待后续统一门禁
  - ✅ 9.2 已在现有 `USKMovementComponent` 内建立 RootMotion Coordinator 首版，不新增平行组件：UE5.8 `PreConvert` 回调只记录动画原始局部 Delta 并原样透传，Mesh 转换世界空间后由唯一 `PostConvert` 回调记录 Steering 修正和最终提交 Delta，再交给 CMC 计算速度、碰撞移动与根旋转。协调快照按 Movement Tick 递增 SampleId，无 RootMotion 的帧也发布有效空记录，避免诊断读取旧值；当前 Steering 修正固定为零、Final 等于动画世界 Delta，具体 Free/Locked 策略留给 9.3。Classic 路径继续保留旧方向重定向且不写正式协调快照。用户已完成 C++ 编译，PIE 留待后续统一门禁
  - ✅ 9.3 已在唯一 PostConvert Coordinator 中实现 Free/Locked 双通道 Steering：Free 以动画根旋转作用后的 ActorYaw 为基准，仅向 `DesiredMoveYaw` 有限收敛，并以同一目标小幅旋转动画水平 Delta；Locked 用 `DesiredFacingYaw` 修正最终根旋转、用 `DesiredMoveYaw` 独立修正水平平移，二者不互相替代。Lua 在 BeginPlay 发布首版调参：Free `120°/s`，Locked 面向/平移各 `360°/s`，所有通道单帧上限 `6°`；C++ 只保存安全值并执行双重限幅。最终只改同一个 RootMotion Transform，不调用 `SetActorRotation`、不改变平移长度或 Z；全身动作期间修正归零并原样保留 Montage RootMotion。大角度 Pivot 门禁留给 9.4。用户已完成 C++ 编译，PIE 留待后续统一门禁
  - ✅ 9.4 大角度方向差门禁已完成 C++ 编译：Moving 中实际方向与本次未来积分末端速度方向达到 `100°` 时退出 Loop，并以当前合法 Starts 数据承载 Pivot 回退；误差收敛到 `45°` 内且通过原有 PostSelection + 后续 CMC 推进确认后才回到 Moving。正式 Pivot 数据接入后沿用同一协议切换为 PivotRequested；未运行专项 PIE
  - ✅ 9.5 已按 UE5.8 源码和项目调用点完成单次消费边界核对：项目侧没有调用 `ConsumeRootMotion`、`ConsumeExtractedRootMotion`，也不直接写 `RootMotionParams`；PreConvert 只采集并原样返回局部 Delta，唯一 PostConvert 只返回同一份最终世界 Delta，随后由 `Super::TickComponent` 的原生 CharacterMovement 链完成一次 Mesh RootMotion 提取、速度换算、物理/碰撞和根旋转应用。Motion Matching Lua 在 Classic 分支前返回，不调用 `AddMovementInput`。现有相关 C++ 已完成编译，运行时重复消费负例仍归入 11.5
  - ✅ 9.6 已按 UE5.8 回调顺序和项目数据链完成闭环核对：派生 `OnMovementUpdated` 先记录本次 RootMotion 请求、最终 Actor 位移与裁剪量，原生 CharacterMovement 在 scoped movement 完成后广播 `OnCharacterMovementUpdated`；轨迹组件由该委托读取最终 Actor Transform、CMC Velocity/MovementMode 并追加 `CompletedActualHistory`。下一轮 `BuildMotionMatchingQuerySnapshot` 复制最新 ActualState，以其时间/Transform 为零点，仅把更早完成样本转换为负时间历史；传送、服务器校正和模拟代理平滑校正统一 Rebase 并清空旧连续性。现有相关 C++ 已完成编译，碰墙/网络运行时专项仍归入 11.5

- 🔄 10. 扩展 Locomotion、视觉后处理与 GAS 所有权（依赖：9）
  - 🔄 10.1 扩展完整 Locomotion 数据与阶段协议
    - ✅ 10.1.1 已在正式 `SekiroMotionMatchingConfig.lua` 固定目标覆盖契约：Chooser 只以 Phase/Gait/Mode/Stance 做合法性门禁；八向与斜向作为连续 Trajectory/Pose 特征在合法数据库内排序。目标族覆盖 Standing/Crouching、Free/Locked、Walk/Run/Sprint、Starts/Loops/Stops/Pivots、Airborne/Landing 与 Recovery；当前四库仍是唯一启用生成数据，不创建空库
    - ✅ 10.1.2 对现有 Locomotion 动画做定向清单与 RootMotion 离线统计，逐项映射覆盖族、阶段、步态和方向；脚相位首版由 Schema 双脚 Position/Velocity 连续特征表达，未识别素材必须保留缺口，禁止猜测资产语义
      - ✅ 10.1.2.1 已完成只读统计脚本：从旧正式 Lua 已人工命名的 `Locomotion/Jump` 路径取得有限候选，对实际 `rootmotion_all` 重导 JSON 做一次内存映射扫描，只解码每个候选的 Frames 前元数据与 RootMotionFrames，按导入器相同坐标变换输出位移、路径、速度、方向和总转角；脚本不自动断言阶段、步态、模式或姿态
      - ✅ 10.1.2.2 已对当前资产实际重导来源执行脚本并审查基础结果：119/119 候选命中，93 个含 RootMotion、26 个无 RootMotion；Standing Walk/Run 与 Crouching Walk/Run 循环均只有四向，Jump Start/Land 具备完整八向，Jump InAir 无 RootMotion，Sprint 只有 Forward Loop。报告写入 `Saved/MotionMatching`，未修改资产；旧工程只作为重导 JSON 的只读来源
      - ✅ 10.1.2.3 已形成事实覆盖表：Standing/Crouching Walk/Run 只有四向、Sprint 循环只有前向、Jump Start/Land 有完整八向、Jump InAir 无 RootMotion、Recovery 无已识别素材；禁止将四向登记成 Planar8 完成。左右脚相位保持由当前 Schema 双脚 Position/Velocity 隐式连续匹配，不预建人工枚举
      - ✅ 10.1.2.4 通过动画预览核对脚接触、循环首尾和 Start/Stop 有效区间；再把通过准入的候选迁入正式 Motion Matching `AnimAssets.lua`
        - ✅ 10.1.2.4.1 已通过 UE5.8 编辑器内已导入 `UAnimSequence` 完成 73 个定向候选的只读采样，资产全部可加载且 RootMotion 设置有效；18 个 Idle/Loop 的去累计 RootMotion 双脚首尾姿势与首尾根速度均通过连续性门禁，最大脚姿势差为 0.84 cm。Start/Stop/Jump 的根速度有效区间和双脚/脚趾接触候选已写入 `Saved/MotionMatching/animation_preview_verification.json/.md`，未编译、保存或修改资产
        - ✅ 10.1.2.4.2 已把当前通过准入的 67 个唯一动画编号迁入正式 Motion Matching `AnimAssets.lua`：包含既有最小组基线、Standing Walk、已通过的 Standing Run、Crouching Walk/Run、Sprint、八方向 Jump Start 与八方向 Landing。`Run_Forward_Loop` 因此前已通过最小组运行验证而继续保留；只登记源码资产路径，本次新增 Jump Start 尚未生成 Pose Search 资产
        - ✅ 10.1.2.4.3 已按动画类型修正准入判定：Start 只要求至少一侧有效支撑接触和连续有效 RootMotion，不要求与另一阶段的第 0 帧构成硬切；`Run_Start_Right` 已解除误报。Sprint Loop 的 UE RootMotion 平均速度约 877 cm/s，与旧源脚锁定估算 853 cm/s 仅差约 2.9%，且首尾姿势连续；Sprint Start 的旧脚锁定结果来自短时加速/转向片段，不作为否定资产原生 RootMotion 的依据
        - ✅ 10.1.2.4.4 已修复 Jump Start 准入门禁：`201100–201107` 八个 Start 首帧均有近地脚趾支撑，100 cm 水平位移分布在全部 5 个采样区间且没有单帧导入跳变，因此八向独立 JumpStart 族通过准入。原脚本把 Start 末帧与 InAir 第 0 帧按 5 cm 阈值当作硬切，错误地用后续阶段配对否决当前阶段；正式图存在交叉淡化，Motion Matching 也按当前 Pose 选择目标时间。四个基准方向的较大 RMS 继续作为 Ascending/Apex/Falling 数据缺口证据，不再否定 JumpStart
    - 🔄 10.1.3 在同一正式配置中先接入 Standing + Free 的 Walk/Run/Sprint 八向 Starts/Loops/Stops/Pivots，并扩展 Chooser 与联合归一化集合
      - ✅ 10.1.3.1 已在同一 Locomotion 联合归一化集合中接入已准入的 Standing + Free Walk/Run，并完成 Phase × Gait × Stance 数据库边界重构。重构后的四向 Run PIE 共采集 136 个样本，前/右/后/左分别只出现 `000400→000500`、`000403→000503`、`000401→000501`、`000402→000502`，数据库仅来自 `PSD_FreeRun_Starts` 与 `PSD_FreeRun_Loops`，没有 Walk 动画或数据库跨 Gait 命中。Stop 的有效采样终点配置保持不变；斜向、Pivot 等素材缺口继续由 10.1.3.3 跟踪
      - ✅ 10.1.3.2 已把当前可识别 Sprint 素材接入同一正式数据库族：Stationary 共享 Idle，Starts 包含 Forward 与 Back/Left/Right TurnStart，Loops 只有 Forward，Stops 包含 Forward 与 Left/Right TurnStop；四阶段 Eligibility 和 RequiredCoverage 均增加 Sprint。Sprint Stop 只索引前 0.1333–0.1667 秒的有效根运动。上述接入只代表现有素材可用，不代表完整方向覆盖
      - 🔄 10.1.3.3 补充 Standing + Free 的斜向 Walk/Run、非前向 Sprint Loop/Stop 与正式 Pivot 素材；缺少真实素材前不得用现有四向候选或 Steering 冒充八向完成
        - ✅ 10.1.3.3.1 已在 UE5.8 中对 `/Game/Characters/Sekiro/Animations` 的 1436 个 AnimSequence 做全量只读根轨迹端点扫描，并对 75 个正式/候选 Locomotion 动画逐帧复核脚接触、循环接缝和有效根运动区间。确认 `001402/001403` 均包含约 180° 根旋转、158.77 cm 平移和有效双脚接触，可作为 Standing Sprint 180° Pivot；其余已识别 Walk/Run/Crouch 族没有可承担正式 Pivot 的根旋转
        - ✅ 10.1.3.3.2 已将 `001402/001403` 接入独立 `PSD_FreeSprint_Pivots`：只允许 `PivotRequested + Sprint + Free + Standing`，禁止会争抢 DesiredFacing 的 Locked 复用。Moving 大角度反向现在先进入 PivotRequested；有合法库时等待同代 PostSelection 和后续 CMC ActorYaw 收敛确认，无合法库的 Walk/Run/Locked/Crouch 组合在创建请求前确定性回退 Start，并记录 `PivotCandidateUnavailable`
        - ⬜ 10.1.3.3.3 仍需外部补充或明确批准重定向的动画内容：Standing Walk/Run 斜向 Starts/Loops/Stops、非前向 Sprint Loop/Stop，以及 Walk/Run 的正式 Pivot。全工程根轨迹扫描中的其他斜向位移主要属于 Jump、动作或未识别族，不能仅凭端点数值并入 Locomotion
      - ✅ 10.1.3.4 已按 Phase × Gait × Stance 拆分实际搜索数据库并由 Chooser 对 Gait 做硬门禁；方向继续作为库内连续特征，Free/Locked 只有在同一 Gait 数据确实可共享时才复用。Stationary 继续复用唯一 Idle；Crouching Sprint→Crouch Run 的显式降级已在 Chooser 映射中表达，不依赖混库偶然选择
        - ✅ 10.1.3.4.1 正式 Lua 已把 Standing Starts/Loops/Stops 拆成 Walk、Run、Sprint 九库，把 Crouching 拆成 Walk 与 Run 六库；Run 复用既有路径，新增 Walk/Sprint/Crouch Walk 路径。Eligibility、RequiredCoverage、Schema 分配、联合 Normalization Set、生成顺序和无参数编译入口已同步；资产脚本也已改为物化全部 17 个地面数据库与 Landing。本步未生成资产、编译或运行 PIE
        - ✅ 10.1.3.4.2 已生成并保存 2 个 Schema、2 个 Normalization Set、18 个 Database 与正式 Chooser；66 个唯一动画的当前平台压缩数据驻留门禁通过。只读回读确认 18 库共 66 个动画条目，Schema、Normalization Set、BruteForce 模式和动画身份均与本轮 Lua IR 一致；地面联合集合含 17 库，Airborne 集合含 Landing。Chooser 实际为 4 列 18 行，瞬态 AnimInstance 穷举 156 个 Phase/Gait/Mode/Stance 组合：49 个合法组合各返回唯一预期数据库，107 个未覆盖组合均为空，没有跨 Gait 命中。本步未编译 C++/Blueprint 或运行 PIE
        - ✅ 10.1.3.4.3 已执行 Standing Free 四向 Run 身份 PIE 复验：136 个样本覆盖前、右、后、左移动与停步窗口；每个方向都命中对应 Run Start/Loop，预期 Loop 全部出现，动画身份与数据库均无意外项，整体判定 `passed=true`。测试后已停止 PIE
    - 🔄 10.1.4 接入 Standing + Locked 的 Walk/Run/Sprint 八向移动，保持 DesiredMove 与 DesiredFacing 分离
      - ✅ 10.1.4.1 Locked 在相同 Gait 内复用对应 Stationary/Start/Loop/Stop 数据库：Eligibility 同时接受 Free 与 Locked，独立 RequiredCoverage 为 Locked × Walk/Run/Sprint 保留 12 条显式契约；不同 Gait 不再混库。库内仍由 DesiredMove 查询位移方向，DesiredFacing 继续由独立 Steering 链提交。拆分资产已生成，瞬态 Chooser 穷举确认 Locked 组合结构路由正确；尚未运行 Locked PIE
      - ⬜ 10.1.4.2 当前 Locked 的 Walk/Run/Sprint Start/Loop/Stop 路由已随本轮 Lua 生成、资产回读和 AnimBlueprint 编译再次通过；仍需补齐与 10.1.3.3 共用的斜向、非前向 Sprint 与不旋转 ActorYaw 的 Locked Pivot 素材，最后只剩 PIE 运行时验收
    - 🔄 10.1.5 接入 Crouching + Free/Locked；蹲伏不覆盖持久 Gait，因此合法候选必须接受 Walk/Run/Sprint 请求并路由到蹲行动画族
      - ✅ 10.1.5.1 已建立 CrouchStationary、CrouchWalk Starts/Loops/Stops 与 CrouchRun Starts/Loops/Stops 七个正式数据库源码配置，并纳入同一 Locomotion 联合归一化集合和生成顺序；Free/Locked 均接受持久 Walk/Run/Sprint 请求。Walk 只进入四向 Crouch Walk 库，Run 只进入四向 Crouch Run 库，缺少专用蹲冲刺素材时 Sprint 由 Chooser Eligibility 显式降级到 Crouch Run，绝不回退到 Standing Sprint。24 条独立 RequiredCoverage 已同步新键，并按预览报告为八条 Crouching Stop 写入 0.2667–1.2 秒有效 RootMotion 采样终点；资产已生成且瞬态 Chooser 穷举确认结构路由正确，尚未运行 Crouching PIE
      - ⬜ 10.1.5.2 当前 Crouching 四向 Walk/Run 与 Sprint→CrouchRun 显式降级已随本轮 Lua 生成、资产回读和 AnimBlueprint 编译再次通过；仍需外部补充 Crouching 斜向、正式 Pivot 与专用 Sprint 素材，最后只剩 Free/Locked 蹲伏 PIE 运行时验收
    - ✅ 10.1.6 接入 JumpStart/Ascending/Apex/Falling/Landing，并以空中专用 Query/Schema 替换当前 Airborne/Landing 搜索关闭桥；首轮 `Run + Free + Standing` 空中链已完成运行时验收，其他 Mode/Gait/Stance 组合仍服从各自动画覆盖任务
      - ✅ 10.1.6.1 已保留旧 Airborne 枚举序号兼容并新增 JumpStart/Ascending/Apex/Falling 正式阶段；MovementMode 离地边沿和 CMC 实际竖直速度按带迟滞阈值的权威状态机推进空中阶段，走下平台直接进入 Falling，重新受向上冲量时允许返回 Ascending。TargetCoverage 已改用五个正式空中/落地 Phase；专用重力 Query/Schema 接通前，这些阶段仍全部失败关闭，不会误用地面查询或数据库。用户随后回复“继续”，按既有约定视为已完成本步 C++ 编译；未运行专项验证
      - ✅ 10.1.6.2 QuerySnapshot 新增 None/Grounded/Airborne 显式预测域；MOVE_Falling 不再返回 UnsupportedMovementMode，而是保留已完成实际三维历史。地面 XY 按 MoveIntent 构造查询意图；空中 XY 保持上一轮 CMC 完成速度作为继承惯性，Z 从 CMC 实际竖直速度按 GravityZ 积分并受当前 PhysicsVolume TerminalVelocity 限制。结果只供搜索，不写回 CMC；其他非地面模式继续失败关闭。查询完整性与 AnimGraph 搜索分支准入已拆分，空中 Query 即使有效，在细分空中 Phase 白名单和专用 Schema/数据库开放前仍输出安全姿势，不会继续沿用地面 Motion Matching 姿势
      - ✅ 10.1.6.3 已建立 Airborne 专用 Schema/Normalization Set，并接入已准入的 JumpStart/Ascending/Apex/Falling/Landing 数据库与 PostSelection 确认；JumpStart 使用独立 RootMotion 数据，无 RootMotion InAir 阶段按“姿势候选 + CMC 继承惯性”协议准入，不冒充 RootMotion 位移资产
        - ✅ 10.1.6.3.1 项目 Lua 已声明并生成独立的 42 维 Airborne Schema：Trajectory 使用完整三维 Position/Velocity 与水平 Facing，和 34 维地面 Schema 分开归一化。八方向 Jump Start 与 Landing 均已加入 `PSN_Airborne`，分别生成 `PSD_Airborne_JumpStarts` 与 `PSD_Airborne_Landing`，并重建 Chooser；二者暂保持 `Run + Free + Standing` 已验收组合。生成前补齐 `AuthoringValidation.RequiredCoverage` 的 JumpStart 覆盖契约，Lua IR 预检随后通过，AIBridge 回读确认新数据库资产存在。Ascending/Apex/Falling 仍在 Deferred 中；未编译蓝图或运行 PIE
        - ✅ 10.1.6.3.2 已通过 `Script/rebuild_motion_matching_assets.py` 完成全量预检、66 个动画压缩数据驻留、两个 Schema、两个 Normalization Set、九个阶段数据库和 Chooser 重建。预检发现并修复四个 Crouching 数据库缺少 `PhaseSchemaAssignments` 的项目 Lua 契约遗漏后重试成功。实际资产回读确认 Airborne Schema 保存 4 个 Trajectory 样本（Flags 2/129/131/131）与 Pelvis/L_Foot/R_Foot Pose 样本，按原生展开为 24+18=42 维；`PSN_Airborne` 唯一成员为 `PSD_Airborne_Landing`，数据库依赖 201140–201147 八个 Landing 动画、Airborne Schema 和 Normalization Set，Chooser 实际依赖该数据库且生成 IR 只有 `Landing + Run + Free + Standing` 一行。未编译 AnimBlueprint 或运行 PIE
        - ✅ 10.1.6.3.3 已在源码中仅开放 Landing 阶段白名单，并在游戏线程先用当前 Phase/Gait/Mode/Stance 评估 Chooser 是否确有有效数据库；因此首版只自然开放 Lua 已声明的 `Landing + Run + Free + Standing`，不在 C++ 复制该组合，未来扩充 Chooser 无需再改白名单。合法 Landing 请求必须获得同代 PostSelection，并由选择之后仍处于 Walking/NavWalking 的新 CMC 完成样本确认，才按意图与实际速度进入 StartRequested/Stationary/StopRequested；无 Chooser 覆盖的组合保留最短保持后的安全恢复。JumpStart/Ascending/Apex/Falling 继续失败关闭。用户随后回复“继续”，按既有约定视为已完成本步 C++ 编译；未运行 PIE
        - ✅ 10.1.6.3.4 JumpStart 运行时接缝已接入并完成 Live Coding 与专项 PIE：阶段白名单允许其进入 Chooser，发布新请求时无条件使离地前 Continuing Pose 失效；有合法候选时必须取得同代 PostSelection、等待选中动画从 SelectedTime 按 WantedPlayRate 播完剩余窗口，并观察到选择后的 MOVE_Falling 完成样本，才确认请求并按实际竖直速度进入 Ascending/Apex/Falling。`Run + Free + Standing` 采集 21 个完成样本，JumpStart 搜索持续 2 个样本，唯一命中 `PSD_Airborne_JumpStarts` 的 `201100`，确认样本 33 晚于选择基线 31，并记录 1 个仍关联该选择的 76.29 cm 最终水平 RootMotion 样本；随后按实际状态进入 Falling 并完成 Landing。无 Chooser 覆盖组合仍只在最短保持后安全退出；Ascending/Apex/Falling 保持失败关闭。报告为 `Saved/MotionMatching/jump_start_runtime_verification.json`
        - ✅ 10.1.6.3.5 已按 UE5.8 CMC 源码锁定并验证空中惯性协议：JumpStart RootMotion 先形成并经碰撞约束得到实际水平速度；后续无 RootMotion 的 Ascending/Apex/Falling 只提供姿势，由 Movement 显式清除姿势混合遗留的 Anim RootMotion 参数并让 `PhysFalling` 继续消费继承速度、重力与碰撞。空中 Query XY 保持最近完成实际速度，不按 MoveIntent 加速；输入仅影响 Facing 和候选语义。正式 Lua 共享 `InAirPoseOnly` 数据库只纳入八方向 `201110–201117` 与通用 `201030` 九个无 RootMotion 姿势，覆盖 `Ascending/Apex/Falling + Run + Free + Standing`；项目 IR 的 `InheritedAirborneMomentum` 语义门禁不写入插件资产补丁。资产回读确认 20 库/83 条动画、2 个 Normalization Set/20 个成员，Chooser 4 列 20 行，156 个组合中 53 个合法组合精确命中、103 个未覆盖组合为空。C++ 编译零错误；专项 PIE 采集 114 个完成样本，完整经过五个空中/落地阶段，74 个空中搜索样本全部启用，64 个 PoseOnly 惯性样本保持有效水平速度且 PendingInput 始终为零，最终判定通过
    - 🔄 10.1.7 RecoveryRequested 的通用运行时接缝已完成 C++ 编译：阶段白名单允许它进入 Chooser 数据门禁，与 Landing 共用同代 PostSelection + 后续地面 CMC 完成样本确认协议，并保留无候选时的最短保持安全恢复。1436 个动画的全量端点扫描仍没有提供可验证的 RecoverToIdle/RecoverToMove 语义，因此 Lua 不生成 Recovery 数据库，实际运行行为继续走安全恢复；以后补齐并准入资产后只扩展 Lua/Chooser 即可，无需再改 C++。未运行本轮 PIE
  - ✅ 10.2 Orientation Warping、Steering 和 Leg IK 只修正表现或最终 RootMotion 协调结果，不生成第二份胶囊位移
    - ✅ 10.2.1 RootMotion Steering 已位于 `USKMovementComponent` 唯一 PostConvert Coordinator：Free 同时有限修正根旋转与水平平移方向，Locked 分离 DesiredFacingYaw 与 DesiredMoveYaw，保持平移模长和 Z 不变；Lua 只配置每秒速率和单帧上限，全身排他所有权期间关闭。源码已经过此前 C++ 编译，本轮只重新核对调用链
    - ✅ 10.2.2 已通过正式 Lua AnimGraph 接入 pose-only Orientation Warping；不修改 Actor、胶囊或生成额外 RootMotion，并已在现有方向素材和 Steering 边界下完成反馈、停步、FullBody、碰墙、Landing 与四向 Run 身份门禁；10.1 的动画素材缺口仍独立保留
      - ✅ 10.2.2.1 已按 UE5.8 引擎源码确认：原生 Orientation Warping 的 Graph 模式会读取并覆盖输入 Pose 的 RootMotion 属性，因此不符合本项目单一 Coordinator 所有权；正式方案固定使用 Manual 模式
      - ✅ 10.2.2.2 AnimInstance 源码已把最近完成的 `FSKRootMotionCoordinationSnapshot.SteeringTranslationYawDeltaDegrees` 作为姿势残差发布；Movement 当前写入快照与最近完成快照分离，并在 `OnMovementUpdated` 后提交，避免下一帧 `BeginRootMotionCoordinationSample` 清空动画侧尚未消费的结果。只有搜索分支有效、当前有移动意图、Locomotion 仍持有所有权、协调样本有效且 Steering 实际参与时 Alpha 才为 1
      - ✅ 10.2.2.3 正式 Lua AnimGraph 已在 Pose History Collector 之后接入 `LocalToComponent → OrientationWarping(Manual) → ComponentToLocal`，Angle/Alpha 只读父类快照；C++ 已完成编译，用户已生成 `ABP_SekiroMotionMatching`，随后回复“继续”，按既定约定视为动画蓝图编译成功
      - ✅ 10.2.2.4 PIE 门禁已覆盖已有四向素材和需要有限 Steering 的中间方向：Angle 等于上一轮实际平移 Steering，Alpha 只在合法 Locomotion 样本开启，最终胶囊位移保持单一 RootMotion 所有权；碰墙、停步、Landing 与 FullBody 所有权期间均未残留姿势扭曲
        - ✅ 10.2.2.4.1 已用 `capture_orientation_warping_feedback.py` 完成 100 个唯一 Movement 样本的中间方向反馈门禁：强制制造 90° 朝向残差后实际单帧 Steering/Angle 峰值均为 6°；所有有效 RootMotion 样本的逐帧反馈错误数为 0，Alpha 错误数为 0，动画 RootMotion 与 Coordinator Final Delta 的水平模长错误数为 0
        - ✅ 10.2.2.4.2 停步后 MoveIntent、水平速度、Angle、Alpha、Steering 均归零并回到 Stationary；临时 FullBody 令牌在 14 个样本中全部进入 ActionOwned、关闭 Locomotion 并令 SteeringSuppressed=true，Angle/Alpha 始终为 0，释放后 20 个恢复样本全部回到 Locomotion 所有权。AIBridge 的 UE5.8 连续轴输入也已改用 Enhanced Input 原生 Continuous Injection，实测可持续驱动 Run 并由 `move_stop` 释放
        - ✅ 10.2.2.4.3 边沿 PIE 已全部通过：现有 +X 关卡边界墙产生 8 个明确裁剪样本，胶囊 X 停止且实际位移为 0、裁剪量峰值 55.82 cm、Orientation Alpha 始终为 0；正式 Jump 输入经过 Stationary→JumpStart→Falling→Landing，Landing 搜索开启并选中 `201143`，Landing 与恢复后 Alpha 均为 0；Gait 分库重构后四向 Run 的 136 个样本分别稳定命中 `000400/000500`、`000403/000503`、`000401/000501`、`000402/000502`，无 Walk 动画或数据库跨 Gait 命中
    - ✅ 10.2.3 在 Orientation Warping 后接入 Foot Placement + Leg IK；只修正最终骨骼姿势，不改变 RootMotion、Actor 或胶囊
      - ✅ 10.2.3.1 已复用当前 Skeleton 的正式脚部契约：`IK_Foot_Plane` 作为稳定参考平面，`Pelvis` 只允许垂直补偿，左右 `L/R_Foot_Target` 驱动两段 `L/R_Foot` 腿链；地面数据由 UE5.8 原生 Foot Placement Trace 获取，`PlantLockType=Unlocked` 且水平骨盆补偿为 0。正式 Lua 全量图已接成 `Orientation Warping → Foot Placement → Leg IK → Component To Local`；AnimInstance 新增两节点共享 Alpha，地面淡入、空中淡出，状态无效、Gameplay Tag 阻塞或 FullBody/Traversal 排他所有权时立即归零。本步只完成 C++/Lua 源码，未编译、生成资产或运行 PIE
      - ✅ 10.2.3.2 `SekiroEditor` 已完成 C++ 编译，正式 `ABP_SekiroMotionMatching` 已由 Lua 全量重建、执行一次 UE 原生蓝图编译并保存。Canonical IR 回读确认 `OrientationWarping → FootPlacement → LegIK → ComponentToLocalSpace` 各 1 个，两节点共享 `MotionMatchingFootIKAlpha`，`IK_Foot_Plane/Pelvis`、左右腿定义、Trace 与骨盆约束均与正式配置一致，类默认值地面淡入/空中淡出为 8/20；零编译诊断。回读仅报告 EventGraph 不属于 Lua 动画图所有权的预期警告，未运行 PIE
      - ✅ 10.2.3.3 PIE 已覆盖平地、斜面、台阶、离地/落地和 FullBody 所有权：平地 Alpha 稳定为 1 且 RootMotion Coordinator 水平模长零误差；约 14° 斜面双脚地面高度差 8.41 cm 时，脚相对各自地面的高度差不超过 0.30 cm；13.72 cm 正常台阶两脚相对地面高度差不超过 0.53 cm，两个定点夹具中的胶囊均保持不动。空中 Alpha 到达 0、落地恢复为 1；FullBody 的 14 个样本全部为 ActionOwned 且 Alpha=0，释放后 20 个样本恢复 Locomotion、最终 Alpha=1。修正测试脚本 `unreal.Rotator` 具名 Yaw 后重新执行 Orientation 验证，47 个 RootMotion 样本中有 34 个姿势扭曲活跃样本，最大 Steering/Angle 均为 6°，逐帧反馈、Alpha 和水平模长错误均为 0
  - ✅ 10.3 当前正式 Lua AnimGraph 未创建 Offset Root Bone 节点，Translation 与 Rotation 均由“节点不存在”确定性关闭；Lua 对 AnimGraph 的全量替换保证生成资产不会保留蓝图侧旧节点。以后若要启用 Rotation 必须新增独立方案与门禁，Translation 继续禁止
  - ✅ 10.4 RootMotion Owner Token 核心协议、现有 Combat 全身 Montage 迁移以及 AnimInstance `ActionOwned/RecoveryRequested` 权威边沿均已完成 C++ 编译：`USKMovementComponent` 作为唯一签发权威，Locomotion 是无令牌默认所有者；UpperBody 使用独立姿势通道且不夺取根运动，FullBody/Traversal 共用唯一排他覆盖通道。AnimInstance 在游戏线程复制所有权版本、有效 Owner、UpperBody 与 Locomotion 许可快照；FullBody/Traversal 获取边沿优先进入 `ActionOwned`，释放边沿进入 `RecoveryRequested`，其后由任务 10.1.7 的 Chooser 数据门禁决定使用正式恢复候选或安全恢复。所有权源缺失时已有 `ActionOwned` 保持不退。独立 Traversal/真正 GA 类与 UpperBody 持有者仍留待具体系统出现时接入
  - ⬜ 10.5 FullBody RootMotion GA 接管时暂停 Locomotion RootMotion，全部结束路径必须释放 Token
  - ⬜ 10.6 Motion Warping 仅用于具有显式目标与窗口的全身 GA Montage

- ⬜ 11. 按数据链自上而下诊断并执行授权门禁（依赖：4–10）
  - 🔄 11.1 已补充 Query 失效原因、Anim Node Function 节点引用转换失败、Chooser 空结果和 PostSelection 不完整字段的按状态/Generation 限频诊断；CMC/实际移动、候选 Cost、Blend Stack、Pose History 与 RootMotion Owner 的完整观测仍待后续步骤
  - ⬜ 11.2 固定诊断顺序：可观察症状 → 上游事实/轨迹 → 语义/Chooser → Query/Search → Blend/PoseHistory → RootMotion/CMC
  - ✅ 11.3 用户已授权本轮除 PIE 外的全部验证；Motion Matching 相关 C++ Live Coding 零错误，正式 `ABP_SekiroMotionMatching` 原生编译状态为 `UpToDate`
  - ✅ 11.4 已按当前磁盘 Lua 全量重建 2 个 Schema、2 个 Normalization Set、21 个 Database、Chooser 和正式 AnimGraph；静态回读与 Canonical IR 一致
  - ⬜ 11.5 只有用户明确要求 PIE 时才测试启动、停止、反向、锁定、碰墙和 GAS 仲裁
  - ⬜ 11.6 门禁通过后再讨论真实关卡迁移；不删除 Classic 回退系统，不修改独立旧项目

## 可观察结果与失败判据（任务 0.4）

本节是同一套正式系统的验收契约，不引入专用测试蓝图或临时代码。首轮只使用 Idle、Forward Start、Forward Run Loop、Forward Stop 数据，执行静止、前向启动、持续移动、松开停止与前向碰墙；反向、锁定和 GA 接管保留为后续门禁，不要求现在增加动画。以下均为预期，尚未运行验证。

| 操作 | 预期可观察结果 | 失败判据 |
|------|----------------|----------|
| 无输入保持 Idle | 无持续水平漂移；历史记录实际位置，未来查询收敛到静止 | 无输入仍持续移动，或查询产生无来源的前向速度 |
| 静止时按下前向输入 | 意图先产生未来轨迹；StartRequested 发布持久请求，Chooser 返回合法起步集合；选择确认后推进动画 RootMotion | 查询等待选中动画才能形成；请求未确认便消失；有有效请求却持续停留在不合法 Idle；动画尚未提供位移就由输入推动胶囊 |
| 持续前向输入 | 按正式 Phase 规则进入 Moving，合法循环动画持续输出 RootMotion；CMC 单次消费 | 起步/循环反复跳变；动画位移和输入位移叠加；候选集合与当前 Phase/Gait 不匹配 |
| 松开输入 | StopRequested 保持到合法选择确认；停止动画允许保留减速根位移，完成后回到 Stationary | 一松手直接截断动画位移；持续沿旧查询奔跑；未确认就丢失停止请求 |
| 前向碰墙 | 动画可继续提供根位移，CMC 碰撞限制最终位置；下一轮历史记录受阻后的实际结果 | 胶囊穿墙；历史按未约束根位移前进；用预测位置覆盖实际位置。不能仅因未来意图仍指向墙内判为失败 |
| 移动中突然反向（后续） | 按方向差和 Phase 规则请求合法 Pivot/Start；由最终协调链提交转向 | 强行扭曲原 Run Loop 绕过候选门禁；Lua、CMC 与 RootMotion 同时修改 ActorYaw |
| 锁定移动（后续） | DesiredMoveYaw 与 DesiredFacingYaw 可分离；Chooser 使用 Locked 合法集合，最终朝向仍由唯一协调链提交 | 移动方向被强行当作面向；选到不合法 Free 候选；多处提交 ActorYaw |
| FullBody GA 接管及退出（后续） | GA 持有 RootMotion 时 Locomotion 不提交根位移；完成、取消和中断都释放所有权，再恢复普通移动 | 两者同帧叠加根位移；退出后 Token 泄漏或普通移动无法恢复 |

后续运行时每项记录：输入操作、当前 Phase/Gait/Mode/Stance、请求 Generation/Pending 与确认结果、合法数据库及选中动画、动画根位移和 CMC 实际位移。缺少诊断接口时先记录缺口，不用肉眼正常代替所有权与请求协议验收。超时阈值在持久请求实现步骤统一配置，本节不虚构已测得的时限。

当前最小动画组已完成 Query/Search/PostSelection、Blend Stack/Pose History，以及 RootMotion Coordinator → CMC 单次消费 → 完成移动回采的源码闭环；任务 9.1–9.6、1.1、1.3、3.2 的 MovementMode/Airborne/Landing 与安全恢复协议，以及 3.5 的结构化原因码、未来轨迹条件和 GAS 标签 C++ 均已完成编译。任务 10.4 的 Movement 双通道 Owner Token、Coordinator 门禁、现有 Combat 全身 Montage 完整释放路径以及 AnimInstance `ActionOwned/RecoveryRequested` 权威边沿均已完成 C++ 编译。Traversal、未来独立 GA 类和 UpperBody 持有者仍随各系统实施；新增 GAS 标签 Lua 默认值仍等待后续显式授权的 AnimBlueprint 重新生成，碰墙、网络校正、GAS 阻塞与所有权仲裁等专项 PIE 也未执行。

## 最小门禁顺序

```text
可观察目标与正式 AnimBP 载体
  → 本帧事实快照
  → 历史/当前/未来轨迹
  → MovementMode/Phase/Gait/Mode/Stance 语义
  → Chooser 合法数据库集合
  → Collector Provider + Motion Matching
  → Schema BuildQuery
  → Continuing Pose + 候选搜索
  → PostSelection 确认 + Blend Stack
  → 最终 Pose History
  → RootMotion Coordinator + CMC 单次消费
  → 下一帧实际历史反馈
  → Pivot/Locked/GAS/IK/Traversal 扩展
```

任一门禁失败都停止后续扩展并保留诊断；是否恢复、恢复哪些文件及使用哪个版本，须按下节流程明确后由用户确认，不自动覆盖当前工作。不得通过修改独立旧项目 UE 5.2 文件来绕过失败。

## 插件与接口接入清单（任务 0.5）

初始核对只读取 `Sekiro.uproject`、`Source/Sekiro/Sekiro.Build.cs` 及 Motion Matching 头文件；下表现已按后续实际接入回填当前源码事实。已完成项的加载、编译或运行证据仍以任务树对应步骤为准。

| 功能 | 项目显式启用插件 | Sekiro.Build.cs 直接模块依赖 | 后续接口核对范围 |
|------|------------------|------------------------------------|------------------|
| Motion Matching / Pose History | PoseSearch | Public: PoseSearch | 已接入查询轨迹、History Provider、动态数据库集合与 PostSelection 回调 |
| 语义候选门禁 | Chooser | Private: Chooser | 已接入评估上下文、数据库数组输出及空结果处理 |
| Desired Trajectory | MotionTrajectory | 已声明 MotionTrajectory | 现有 FTransformTrajectory 的采样、坐标空间与查询消费者契约 |
| 多样本动画混合 | 由 PoseSearch 节点内部提供 | 无单独模块依赖 | 已确认使用 Motion Matching 内部 Standalone Blend Stack，不建立第二混合栈 |
| 视觉动画修正 | AnimationWarping | 当前 Sekiro 模块未直接使用 | 任务 10 接入实际节点时再按公开 API 增加依赖 |
| GA 目标对齐 | MotionWarping | 当前 Sekiro 模块未直接使用 | 任务 10 接入 Montage 窗口时再按公开 API 增加依赖 |
| 动作与标签 | GameplayAbilities | 已声明 GameplayAbilities、GameplayTags、GameplayTasks | FullBody RootMotion 所有权接管与所有退出路径 |

插件启用声明与 C++ 模块依赖是不同层次。未直接使用类型或符号的模块不批量添加依赖；Lua 反射节点也不构成 C++ 依赖理由。当前 PoseSearch 与 Chooser 的真实 C++ 使用已经分别落到 Public/Private 依赖，Blend Stack 由 PoseSearch 运行时内部拥有。

后续 AnimationWarping、MotionWarping 与 GAS API 只有进入任务 10 的实际调用点时才核对和添加；它们不是任务 0.5 的未完成尾项。

## 失败处理与回退流程（任务 0.6）

1. **停止扩展并记录现场**：记录失败操作、目标文件或资产完整路径、引擎版本、首条具体错误及关联日志。保留其余错误和警告；仅有“编译失败”的汇总不足以定位。不得将未执行的后续门禁标记为通过。
2. **按失败层定位源码**：插件加载问题先依据模块加载日志定位插件配置或模块；API 错误依据具体符号定位 C++ 调用；Lua 生成或动画图错误定位 Lua 描述及生成器；运行时失败按事实、轨迹、语义、Chooser、搜索、混合、RootMotion 消费的顺序检查。默认仅定向读取相关文件；需要广泛分析或编辑器诊断时先列用户待办。
3. **先提交源修复**：Lua 生成资产优先修 Lua；通用接口或生成器缺陷才修改对应 C++。不手工修补生成图，不另建替代蓝图，不以保留不支持节点的方式绕过全量图所有权。修改后明确列出尚未执行的编译、生成与运行验证。
4. **必要时提出精确回退清单**：写明本项目内的目标文件、候选提交或备份、恢复原因，以及将被覆盖的当前修改。源码与生成资产必须注明版本对应关系；不得假定磁盘上现有 `.uasset` 与当前 Lua 一致。没有明确版本或备份时报告缺口，不声称存在可恢复产物。
5. **用户确认后才恢复**：先保留将被覆盖的用户修改，再按确认清单恢复；不整库重置，不清理无关文件，不修改 `F:/ProjectAI/Sekiro`。恢复二进制资产须明确包含在授权范围内；仅恢复源码不等于完成资产重建。
6. **恢复与验证分别交接**：逐项列出用户需执行的对象、操作和预期结果。C++ 编译、Lua 生成、AnimBlueprint 编译和 PIE 分别授权；编译成功只证明相应编译门禁通过，不证明动画选择、请求确认或 RootMotion 所有权正确。

当前未登记可恢复提交或备份，未执行恢复及验证。Classic 仅保留为既有回退路径，不自动切换角色、AnimClass 或关卡配置，也不为此新增平行实现。

任务 0.6 的完成标准是流程、权限边界与回退清单格式已经固定；没有具体失败目标时不虚构“可恢复版本”，也不为了勾选任务执行会覆盖用户工作的回退演练。

## 验收标准

- 输入不通过 `AddMovementInput` 或 CMC 加速度产生基础地面水平位移；
- Actor 实际水平位移来自最终动画 RootMotion，并只消费一次；
- 历史轨迹反映碰撞约束后的真实 Actor 结果，未来轨迹只表达意图；
- 本帧 Desired Trajectory 不依赖本帧选择结果；候选动画的离线 RootMotion 特征只参与与 Query 的比较；
- Run 请求不能因 Idle Continuing Pose 或搜索节流永久无法启动；
- Chooser 只返回当前 Phase/Gait/Mode/Stance 合法数据库；
- ActorYaw、动画根旋转和 Steering 每帧只有一个最终提交点；
- Locomotion 与 FullBody GA 不会同时拥有 RootMotion；
- Offset Root、Orientation Warping、Leg IK 和 Motion Warping 不产生未归属的第二份位移；
- 本项目的角色、资产和 Lua 源可在 UE 5.8.2 独立编译、Cook 和回退；
- 独立旧项目 UE 5.2 的计划、设计、代码、资产和关卡引用保持不变。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-09-13 | 用户授权完成除 PIE 外的全部当前 Locomotion 工作。新增 UE5.8 全量只读动画清单与根轨迹扫描：1436 个 AnimSequence 全部可测，75 个正式/候选动画逐帧预览指标零加载失败；确认 `001402/001403` 为带 180° 根旋转的 Standing Sprint Pivot。C++ 状态机改为正式 PivotRequested→PostSelection→后续 CMC 朝向确认，并对无 Pivot Chooser 行的组合同帧回退 Start、记录 `PivotCandidateUnavailable`。Lua 新增 `PSD_FreeSprint_Pivots` 且仅准入 Free，Locked 不复用根旋转素材。两次 C++ Live Coding 均 0 错误；最终重建并回读 2 Schema、2 Normalization Set、21 Database、85 动画条目与 4 列 21 行 Chooser，156 个组合中 54 个精确合法、102 个精确空结果；正式 AnimGraph 全量重建，AnimBlueprint 原生编译 `UpToDate`。未启动 PIE。剩余项已收敛为外部动画内容缺口与对应 PIE 验收 |
| 2026-09-13 | 完成 10.1.6.3.5 空中 PoseOnly 惯性交接修复与专项验收。根因是出站动画混合即使最终 RootMotion Delta 为 Identity，UE 仍可能保留 `HasAnimRootMotion`，随后 `ApplyRootMotionToVelocity` 把 CMC 水平速度覆盖为零。现由同代 Ascending/Apex/Falling PostSelection 激活 Movement 交接：最终动画 Delta 置空、约束阶段保留进入空中的实际 XY，并在 `PhysFalling` 前清除 Anim RootMotion 参数；接地、传送、网络校正、退出 Motion Matching 或 FullBody 排他时自动关闭。C++ 编译 0 错误。首次 PIE 因编辑器失焦节流为 3 FPS 跳过中间阶段而作废；临时关闭 Editor Background/Slate 两级节流后重跑，采集 114 个完成样本，完整经过 JumpStart/Ascending/Apex/Falling/Landing，74 个空中搜索样本全部启用、68 个获得选择、66 个直接确认，64 个 PoseOnly 惯性样本的最大平面速度为 505.30 cm/s、最大单帧位移 6.76 cm，PendingInput 平面值始终为 0，最终 `passed=true`。PIE、采集和临时节流设置均已恢复 |
| 2026-09-13 | 用户确认 InAirPoseOnly 运行时 C++ 已编译，随后执行正式资产脚本。83 个动画压缩数据驻留通过，2 个 Schema、2 个 Normalization Set、20 个 Database 与 Chooser 全部更新保存；新增 `PSD_Airborne_InAirPoseOnly` 并把它加入 `PSN_Airborne`。只读回读确认 20 库/83 条动画项、2 个集合/20 个成员与 Lua IR 一致；Chooser 为 4 列 20 行，穷举 156 个组合得到 53 个合法精确路由与 103 个预期空结果。未编译动画蓝图或运行 PIE |
| 2026-09-13 | 用户确认上一批空中惯性与输入边界 C++ 已编译。随后接入正式 `InAirPoseOnly` 源配置：从 15 个无 RootMotion Jump 候选中只选择八方向 InAir `201110–201117` 与通用 Jump Loop `201030` 九项，排除起跳和落地职责的另外六项；三个空中阶段共享同一 Airborne 数据库与 Normalization Set。项目 Database IR 升至 v5，新增只在项目侧校验、不写入通用资产补丁的 `InheritedAirborneMomentum` 策略；C++ 开放 Ascending/Apex/Falling 搜索并由 PostSelection 直接确认无位移姿势请求。未编译本轮 C++、未生成资产或运行 PIE |
| 2026-09-13 | 用户回复“继续”，开始 10.1.6.3.5 空中惯性协议。按 UE5.8 `ApplyRootMotionToVelocity/ConstrainAnimRootMotionVelocity/PhysFalling` 源码确认：JumpStart 动画 RootMotion 提供水平速度，CMC 在 Falling 保留竖直重力分量；后续无 RootMotion 帧从当前实际速度继续积分。正式 Airborne Query 因而改为保持最近完成实际 XY、仅对 Z 积分重力，MoveIntent 只影响 Facing/候选语义；同时收紧 C++ 默认输入回退和 `AddMovementInputFromScreen`，Motion Matching 模式下只写 Intent。方案允许无 RootMotion InAir 以 `PoseOnlyAirborne` 身份参与 Ascending/Apex/Falling，但本轮未接入数据、编译、生成资产或运行 PIE |
| 2026-09-13 | 用户回复“继续”，完成 JumpStart 运行时接缝及专项 PIE。首次运行通过状态与时间戳确认编辑器仍加载旧模块；随后由 AIBridge 发起 Live Coding，虽然客户端最终输出遇到 GBK 编码异常，但编辑器日志明确记录 `Live coding succeeded`。干净 PIE 的 `Run + Free + Standing` 路径采集 21 个完成样本：JumpStart 搜索开启并唯一选择 `PSD_Airborne_JumpStarts/201100`，同代选择由晚于基线的 CMC 样本确认，协调链记录 76.29 cm 最终水平 RootMotion，随后进入 Falling/Landing，收紧归属后的总判定 `passed=true`；PIE 已停止 |
| 2026-09-13 | 用户回复“继续”，执行 10.1.6.3.1 资产生成。先修复 `JumpStarts.Eligibility` 已扩展而 `AuthoringValidation.RequiredCoverage` 未同步导致的 Lua IR 安全预检失败，补入 `JumpStart + Run + Free + Standing` 八动画覆盖契约；随后通过 AIBridge 重建 74 个动画压缩数据驻留、两个 Schema、两个 Normalization Set、19 个 Database 与 Chooser。只读回读确认 `PSD_Airborne_JumpStarts` 为有效 `PoseSearchDatabase` 资产；未编译 C++/Blueprint、未运行 PIE |
| 2026-09-12 | 修复 10.1.2.4.4 的跨阶段误判：JumpStart 准入只检查当前片段的首帧支撑和 RootMotion 连续性，不再要求与 InAir 第 0 帧硬切小于 5 cm。`201100–201107` 八向均通过独立准入并登记到正式 Lua 的 `PSD_Airborne_JumpStarts` 源配置；基准方向 RMS 只保留为后续 Ascending/Apex/Falling 诊断。C++ 空中阶段白名单仍关闭，本轮未生成资产、编译或运行 PIE |
| 2026-09-12 | 用户回复“继续”，完成 10.2.3.3 Foot IK PIE 门禁。平地 Alpha=1 且 Coordinator 水平模长零误差；约 14° 斜面存在 8.41 cm 双脚地面高度差时，两脚相对各自地面的高度差不超过 0.30 cm；13.72 cm 正常台阶上该误差不超过 0.53 cm，两个定点夹具的胶囊位置均稳定。空中 Alpha 到 0、落地恢复 1；FullBody 14 帧严格为 0，释放后 20 帧恢复且最终为 1。发现并修复测试夹具把 `unreal.Rotator` 位置参数误当作 Pitch/Yaw/Roll 的问题，改为具名参数后在干净 PIE 重跑 Orientation：47 个 RootMotion 样本、34 个活跃样本，Steering/Angle 峰值 6°，反馈、Alpha 与水平模长均零误差。PIE 已停止；本轮未修改游戏代码或二进制资产 |
| 2026-09-12 | 用户回复“继续”，执行 10.2.3.2。先完成 `SekiroEditor` C++ 编译并重启编辑器确认新 Foot IK 反射属性；生成时修复通用 Lua 编译器 `ReflectedDataType.Float` 缺项，以及项目继承默认值把 `8.0/20.0` 误推断为 Integer 的问题。随后通过 Lua 全量重建、UE 原生编译并保存正式 `ABP_SekiroMotionMatching`；Canonical IR 回读确认 Orientation Warping、Foot Placement、Leg IK、空间转换、骨骼/腿定义、共享 Alpha 连线与 8/20 类默认值全部正确，零编译诊断。EventGraph 排除警告符合其非动画图所有权边界；未运行 PIE |
| 2026-09-12 | 用户将运行测试延后并授权继续源码工作，推进 10.2.3.1：确认现有 `IK_Foot_Plane/Pelvis/L/R_Foot_Target/L/R_Foot` 脚部契约与 Lua 插件 UE5.8 Foot Placement/Leg IK 生成能力，正式 Motion Matching Lua 全量图新增 `Orientation Warping → Foot Placement → Leg IK` 链；AnimInstance 发布共享 Foot IK Alpha，地面淡入、空中淡出，状态无效、标签阻塞或排他 RootMotion Owner 生效时立即归零。未编译 C++/Blueprint、未生成资产、未运行 PIE |
| 2026-09-12 | 用户回复“继续”，执行 10.1.3.4.3 Standing Free 四向 Run 身份 PIE 复验。共采集 136 个移动与停步窗口样本：前/右/后/左分别只命中 `000400→000500`、`000403→000503`、`000401→000501`、`000402→000502`，数据库仅为 `PSD_FreeRun_Starts/Loops`，无 Walk 动画、Walk 数据库或其他意外项，整体 `passed=true`。测试后停止 PIE；同时强化临时采集脚本，使其按方向校验预期 Start/Loop、允许数据库和意外身份并输出确定性总判定。10.1.3.1、10.1.3.4 与 10.2.2.4.3 据此完成，本轮未执行 C++ 或蓝图编译 |
| 2026-09-12 | 用户回复“继续”，执行 10.1.3.4.2 资产门禁：通过 AIBridge 运行正式重建脚本，66 个动画压缩数据驻留成功，2 个 Schema、2 个 Normalization Set、18 个 Database 与 Chooser 全部更新保存。新增只读验证脚本并回读实际资产：18 库/66 条动画、2 个集合/18 个成员与 Lua IR 完全一致；Chooser 为 4 列 18 行，瞬态 AnimInstance 穷举 156 个组合，49 个合法组合唯一命中预期库、107 个未覆盖组合为空，零跨 Gait 路由。未编译 C++/Blueprint 或运行 PIE |
| 2026-09-12 | 用户回复“继续”，执行 10.1.3.4.1 源码重构：Standing Start/Loop/Stop 按 Walk/Run/Sprint 拆为九库，Crouching 按 Walk 与 Run 拆为六库并把 Sprint 显式路由到 Crouch Run；Stationary 保持共享。同步更新 Eligibility、49 条 RequiredCoverage、Schema/Normalization Set 成员、生成顺序、Lua 无参数编译入口和 Python 资产清单。静态核对确认 18 个数据库配置、顺序、入口与脚本一一对应，未生成 `.uasset`、编译或运行 PIE |
| 2026-09-12 | 用户确认已编译后执行 10.2.2.4.3。碰墙裁剪与 Jump→Landing 姿势残差门禁通过，Landing 实际 PostSelection 为 `201143`。四向回读发现共享阶段数据库允许跨 Gait 选择：`RequestedGait=Run`、未来速度 407 cm/s 时分别选中四向 Walk `000200/203/201/202`；确认当前 `Starts/Loops/Stops` 把 Walk/Run/Sprint 放在同一数据库且 Chooser 的三个 Gait 都返回该库。已记录 Phase × Gait × Stance 分库修复方案，按未知 Bug 工作流等待确认，未修改正式 Lua 或生成资产 |
| 2026-09-12 | 用户明确“继续”代表授权上一最小步骤及必要同范围操作，项目规范已同步。完成 Orientation Warping 第一批 PIE 门禁，并修复三个运行时/诊断根因：Movement 将当前协调快照与最近完成快照分离，AnimInstance 因而可在下一动画更新读取上一完成 Steering；AIBridge 轴保持改用 UE5.8 Enhanced Input Continuous Injection；允许安全回退的 Landing/Recovery 空候选不再误报 Error，其他阶段缺库仍保留 Error。SekiroEditor 两次编译成功；100 样本反馈、停步归零和 FullBody 所有权互斥均通过，四向身份回读、碰墙与 Landing 仍待下一小步 |
| 2026-09-12 | 用户在生成 AnimBlueprint 后回复“继续”，按既定约定确认 `ABP_SekiroMotionMatching` 已编译成功；10.2.2 的源码、C++ 编译、资产生成和蓝图编译门禁完成，剩余 PIE 运行门禁未授权执行 |
| 2026-09-12 | 用户报告已生成 `ABP_SekiroMotionMatching`，10.2.2 进入蓝图编译门禁；尚未把“已生成”解释为动画蓝图编译或 PIE 验证成功 |
| 2026-09-12 | 用户回复“继续”，按既定约定确认 10.2.2 的 C++ 残差发布源码已完成编译；本轮仅回写状态，未把“继续”扩展解释为资产生成授权，尚未生成或编译 `ABP_SekiroMotionMatching`，也未运行 PIE |
| 2026-09-12 | 推进 10.2.2：UE5.8 引擎源码确认 Graph Orientation Warping 会覆盖 RootMotion 属性，故正式链固定为 pose-only Manual 模式。AnimInstance 从上一轮 Coordinator 快照发布实际施加的有限平移转向角与严格 Alpha，Lua 全量 AnimGraph 在 Pose History Collector 后接入空间转换和 Manual Orientation Warping；避免从输入角重复扭转现有四向素材。当前只完成源码，未编译、生成资产或运行 PIE |
| 2026-09-12 | Jump Start 尚无人工预览结论，未擅自准入；转而核对不依赖该素材的视觉修正边界。确认 RootMotion Steering 已在唯一 PostConvert Coordinator 完整实现并由 Lua 配置限速；正式 Lua AnimGraph 当前只有搜索门禁、Motion Matching、安全 Idle 与 Pose History Collector，不存在 Offset Root Bone、Orientation Warping 或 Leg IK。将 10.2 拆为 Steering/Orientation Warping/Leg IK 三步，并以节点不存在关闭 10.3 基线；未生成资产或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认 RecoveryRequested 通用运行时接缝已完成 C++ 编译。随后定向复核现有 Jump Start 报告：201100–201107 均仅 0.1667 秒/5 区间，水平位移固定 100 cm、竖直位移为 0，自动接触只在部分方向检出；无法以统计代替画面准入，已新增逐项预览门禁，未修改 Lua、生成资产或运行编辑器 |
| 2026-09-12 | 用户回复“继续”，确认 Landing 搜索与确认源码已完成 C++ 编译；开始 10.1.7 的运行时接缝：RecoveryRequested 允许进入通用 Chooser 可用性门禁，有合法数据时必须由同代 PostSelection 和后续地面 CMC 样本确认，无数据时保留定时安全恢复。当前没有已准入 Recovery 动画，因此未添加 Lua 数据库；本批未编译或运行 PIE |
| 2026-09-12 | 完成 10.1.6.3.3 源码：仅开放 Landing 阶段搜索；AnimInstance 在游戏线程以当前语义预检 Chooser，只有真实返回数据库才启用 AnimGraph 搜索并创建持久请求，不在 C++ 硬编码首版 Run/Free/Standing 组合。合法 Landing 由同代 PostSelection 和其后的地面 CMC 完成样本确认后恢复；未覆盖组合保留定时安全恢复。JumpStart/Ascending/Apex/Falling 继续关闭；未编译或运行 PIE |
| 2026-09-12 | 收口 10.1.2 统计结果：新增正式覆盖缺口表，确认 Standing/Crouching Walk/Run 仅四向、Sprint Loop 仅前向、Jump Start/Land 完整八向、Jump InAir 无 RootMotion、Recovery 缺失。纠正脚相位方案：首版由现有 Schema 的 L_Foot/R_Foot Position+Velocity 逐帧连续索引，不增加人工左右脚枚举；只有 Trace 证明不足才扩展接触曲线或 Phase Channel。本批未启动编辑器、预览动画或修改资产 |
| 2026-09-12 | 执行 10.1.2 RootMotion 统计：首次使用当前工程旧 `Extracted/Sekiro_animations.json` 时 119 项虽命中但 RootMotionFrames 全空，确认它不是当前资产重导权威源；随后依据导入日志只读使用 `F:/ProjectAI/Sekiro/Output/c0000/Animation/Sekiro_anims_c0000_rootmotion_all.json`，并修复脚本兼容合并阶段移除 `Sekiro_` 前缀的名称。最终 119/119 命中、93 项有 RootMotion、26 项无 RootMotion；脚本新增全缺失/全零硬失败门禁，未启动编辑器或修改资产 |
| 2026-09-12 | 推进 10.1.2 的低成本源码切口：新增独立只读统计脚本，从旧正式 Lua 的已命名 Locomotion/Jump 候选出发，对 1GB 动画 JSON 做单次内存映射扫描，只提取 RootMotion 元数据并按实际导入坐标变换计算 UE 厘米位移、路径、速度、语义方向与根转角；输出固定到 Saved，未知语义和脚相位保持 NeedsReview/Unmeasured。按默认验证规范未执行脚本、未启动编辑器或修改资产 |
| 2026-09-12 | 用户回复“继续”，确认 AnimInstance `ActionOwned/RecoveryRequested` 权威边沿已完成 C++ 编译，任务 10.4 关闭并暂停后续 GAS。回到 Motion Matching 任务 10.1：在正式项目 Lua 新增与当前生成清单分离的 `TargetCoverage`，固定 Phase/Gait/Mode/Stance Chooser 门禁、连续八向库内排序，以及 Standing/Crouching、Free/Locked、Walk/Run/Sprint、Pivot、空中、落地和恢复的完整目标族；本批未生成资产、编译 Lua/Blueprint 或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认 Combat FullBody Token 迁移已完成 C++ 编译；随后接入 AnimInstance 权威边沿：游戏线程复制 Movement 所有权版本、有效 Owner、UpperBody 和 Locomotion 许可纯值快照，FullBody/Traversal 优先进入 `ActionOwned`，释放后进入新增 `RecoveryRequested`，最短保持后回到已有 Start/Stop/Stationary 协议；所有权源丢失时保持 ActionOwned 失败关闭。本批未编译或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认 10.4 Movement Owner Token 核心协议已完成 C++ 编译；随后迁移现有 Combat 全身 Montage 中心路径：启动 `CombatFullBodySlot` 前申请 FullBody 令牌，换片幂等复用，播放失败、显式停止、自然/中断结束与 `EndPlay` 统一释放。Montage 身份仍属于组件但 ActionSerial 已失效时仍清理令牌，仅禁止迟到广播。Lua 各战斗分支无需重复申请；本批未编译或运行 PIE |
| 2026-09-12 | 开始任务 10.4 第一源码切口：在 `USKMovementComponent` 建立 Locomotion 隐式回退、UpperBody 独立共存、FullBody/Traversal 排他覆盖的 RootMotion Owner Token 协议；新增结果码、弱身份令牌、版本化快照、幂等申请、精确释放与弱引用失效回退，Coordinator 已记录所有权版本并在排他覆盖期间停止 Locomotion Steering。尚未迁移 Combat/GA 和 AnimInstance 状态边沿，未编译或运行 PIE |
| 2026-09-12 | 完成任务 9.6 定向核对：确认派生 `OnMovementUpdated` 在原生完成委托广播前写入当前裁剪诊断，轨迹组件随后从最终 Actor/CMC 事实发布 ActualState 和 `CompletedActualHistory`；下一轮 Query 以最新完成样本为零点消费负时间历史，校正路径统一 Rebase。无需新增 C++，未运行碰墙或网络专项 PIE |
| 2026-09-12 | 用户回复“继续”，确认 GAS 标签 C++ 已完成编译；定向核对并关闭任务 9.5：项目没有第二个 RootMotion 消费或 `RootMotionParams` 写入点，Pre/PostConvert 只构造并返回唯一 Final Delta，原生 UE5.8 CharacterMovement 是唯一消费、物理与根旋转执行者；Motion Matching Lua 不进入 Classic `AddMovementInput` 路径。未修改 C++、未生成 AnimBlueprint 或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认未来轨迹条件已完成 C++ 编译；继续补齐任务 3.5 GAS 标签接入：AnimInstance 从 Pawn ASC 复制并版本化标签快照，以 Lua 配置的 `State.Life.Dying/Dead/Reviving` 执行 Any 阻塞，阻塞时停用 Locomotion 搜索，解除后即使 Phase 未变也创建新 Generation；标签不冒充 `ActionOwned` 或 RootMotion Owner。未编译、生成 AnimBlueprint 或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认结构化 Phase 原因码已完成 C++ 编译；继续接入任务 3.5 未来轨迹条件：QuerySnapshot 发布未来积分末端速度与模长，Phase 以“有效输入且预测末端速度非零”作为移动请求，并用实际方向与预测末端方向误差驱动 `100°/45°` Start 回退迟滞。未编译或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认 Landing 安全恢复协议已完成 C++ 编译；继续补齐任务 3.5 的结构化状态诊断：LocomotionState 新增 PreviousPhase 与 TransitionReason，所有现有阶段变化分别记录初始化、输入边沿、方向突变、MovementMode 离地/落地、Landing 恢复目标和实际状态确认原因。未编译或运行 PIE |
| 2026-09-12 | 用户回复“继续”，确认 3.2 MovementMode/Airborne/Landing 边沿已完成 C++ 编译；继续补齐 Landing 恢复协议：集中定义不可搜索阶段，Airborne/Landing/ActionOwned 均禁止发布 Pending 或手动强搜，Landing 在无专用数据时最短保持后进入现有 Start/Stop/Stationary 请求链，防止沿用起跳前旧数据库。未编译或运行 PIE |
| 2026-09-12 | 用户回复“继续”，据既定约定确认任务 1.3 已完成 C++ 编译；随后开始 3.2 第一源码切口：LocomotionState 独立发布 CMC MovementMode/CustomMovementMode，按递增 ActualSampleId 检测非地面到地面边沿，进入 Airborne/Landing 时不把非地面状态伪装成地面有效查询。ActionOwned/RecoveryRequested 明确保留到正式 RootMotion Owner Token，未编译或运行 PIE |
| 2026-09-12 | 补齐任务 1.3 源码：轨迹组件新增统一 Rebase，从校正后的 Actor/CMC 状态发布零位移、递增 SampleId 的有效基线；MovementComponent 在 OnTeleported 后立即调用，在自主客户端校正落地后的下一 Tick 调用，并在模拟代理 SmoothCorrection 后调用；未知位置不连续也改为 Rebase，非法状态才彻底 Invalidate。未编译或运行传送/网络专项验证 |
| 2026-09-12 | 用户约定：在 Agent 请求 C++ 编译后回复“继续”，即表示已完成编译并允许进入下一源码步骤；据此将 1.1 标记为编译完成，同时确认同批 9.4 已进入新二进制，专项 PIE 仍未执行 |
| 2026-09-12 | 补齐任务 1.1 源码：把 Motion Matching Gait/Stance 枚举移动到 Movement 事实边界，MovementComponent 新增输入层持久请求字段；输入决策入口同步兼容 CurrentMovementTier 与独立请求，Idle 不清除请求、Crouch 只改 Stance、站立档位发布对应 Gait。Movement Lua 直接写入正式 Intent，不再从旧档位或实际蹲伏状态反推。未编译或运行 PIE |
| 2026-09-12 | 回查任务 3–5：任务 3 保留真实缺口——正式 PivotRequested、Airborne/Landing/ActionOwned/RecoveryRequested、未来轨迹/GAS 条件和状态原因码；最小地面状态、Gait 语义、最后方向和资产所有权边界按已有实现回填。任务 4 的四库、Chooser 与 Continuing Pose 门禁按资产回读和最小组 PIE 标记完成，仅保留空 Chooser 运行时负例。任务 5 的 Query Provider、正式 AnimGraph、Pose History 与最终姿势闭环已完成 |
| 2026-09-12 | 回查任务 0–2：确认原任务树没有随 5–9 阶段的实现、自动化测试、资产回读和最小组 PIE 结果回填。现将边界定义任务 0 和查询轨迹任务 2 按已有证据标记完成；任务 1 保留两个真实缺口——1.1 独立 RequestedGait/RequestedStance 输入源、1.3 传送/网络校正后的快照失效恢复。暂停向 9.5 推进，下一源码步骤回到 1.1 |
| 2026-09-12 | 推进任务 9.4：Moving 按碰撞后实际水平位移方向（位移不足 0.1 cm 时用 QueryOrigin 朝向）与 DesiredMoveYaw 的误差进入大角度门禁；达到 100° 时退出 Run Loop 并发布 StartRequested 回退，误差收敛到 45° 内且通过原有 PostSelection/CMC 推进确认后才恢复 Moving。当前无合法 Pivot 数据，不创建空库；仅完成源码，未编译或运行 PIE |
| 2026-09-12 | 用户确认任务 9.3 的 C++ 修改已编译；Free/Locked RootMotion Steering、双通道限幅和 Lua 参数发布已进入正式二进制，PIE 留待后续统一授权 |
| 2026-09-12 | 推进任务 9.3：PostConvert Coordinator 按 Free/Locked 分离根旋转与水平平移 Steering；Free 使用 DesiredMoveYaw，Locked 分别使用 DesiredFacingYaw/DesiredMoveYaw，双通道均受 Lua 发布的每秒速率与单帧角度限制。修正只合成唯一 Final RootMotion，全身动作原样透传；仅完成源码，未编译或运行 PIE |
| 2026-09-12 | 用户确认任务 9.2 的 C++ 修改已编译；RootMotion PreConvert/PostConvert 协调器与三段快照已进入正式二进制，当前 Steering 仍为零，PIE 留待后续统一授权 |
| 2026-09-12 | 推进任务 9.2：在现有 Movement 组件内绑定 UE5.8 RootMotion PreConvert/PostConvert 两个真实挂点，记录动画局部/世界 Delta、Steering 修正与最终提交 Delta；每个 Movement Tick 发布带 SampleId 的新快照，无根运动时不沿用旧值。当前 Steering 为零，Classic 保持旧逻辑；仅完成源码，未编译或运行 PIE |
| 2026-09-12 | 用户确认任务 9.1 的 C++ 修改已编译；Motion Matching 普通移动的 CMC 自动朝向、Lua ActorYaw 和 Classic RootMotion 方向重定向源码门禁进入正式二进制，PIE 留待后续统一授权 |
| 2026-09-12 | 推进任务 9.1：以轨迹组件作为 Motion Matching 能力标记，在原生 Movement Tick、Lua 设置接口和 RootMotion 后转换委托三层拒绝 CMC 自动旋转与 Classic 方向重定向；Movement Lua 在全身动作之外提前退出普通 ActorYaw 路径。Classic 与全身攻击曲线转向保持不变；仅完成源码，未编译或运行 PIE |
| 2026-09-12 | 完成任务 8：逐字段回读确认 Motion Matching 的 Blend Stack 参数与 Lua 契约完全一致，最终 Pose 经 QueryGate 进入 PoseSearchHistoryCollector 后输出；唯一 `Reader.EventGraphExcluded` 警告符合所有权边界。随后完成 UE 原生 AnimBlueprint 编译与保存，零诊断且状态 `UpToDate`，未运行 PIE |
| 2026-09-12 | 用户完成 `LuaAnimBlueprintEditor` 重新编译后，按授权对正式 `ABP_SekiroMotionMatching` 执行 Lua 检查、全量 Graph 重建并保存资产；生成入口返回零诊断，未调用 UE 原生 AnimBlueprint 编译或 PIE，参数回读仍待授权 |
| 2026-09-11 | 用户确认 `ABP_SekiroMotionMatching` 编译通过；任务 7 的两个原生节点函数绑定已通过蓝图编译门禁，下一门禁为 Idle → Start → Moving → Stop → Stationary 最小组 PIE 握手验证 |
| 2026-09-11 | 用户确认完成任务 7 本轮 C++ 编译；持久请求、同代 Chooser/PostSelection 门禁、超时诊断和旧 Bool 移除已进入运行时验证阶段，尚未编译 AnimBlueprint 或运行 PIE |
| 2026-09-11 | 删除未被正式 Lua AnimGraph 或其他源码消费的 `bForceMotionMatchingInterrupt` 兼容属性及刷新函数；手动 Interrupt 与持久请求现在只在 `PublishedSearchRequest.bForceSearch` 汇合，搜索控制不再维护第二份 Blueprint Bool。未编译、生成资产或运行 PIE |
| 2026-09-11 | 完成任务 7.7 的源码诊断：搜索确认超时只记录一次结构化错误且继续保持 Pending 重搜；同代迟到合法结果显式记录恢复事件并进入实际状态确认，不绕过后续 CMC 样本门禁。未编译或运行 PIE |
| 2026-09-11 | 收紧任务 7 的搜索握手：Chooser 提交集合带请求 Generation，PostSelection 只接受同代集合中的首个结果并在入邮箱前复核发布代次；过期、重复、跨 Generation 或语义不匹配消息在写 SelectionSnapshot 前拒绝；AnimInstance 重新初始化会整体清空请求状态与线程邮箱。仅完成源码与文档，未编译或运行 PIE |
| 2026-09-11 | 完成任务 6.5：七个正式 Lua IR 入口全部通过纯值预检；缺失 Start 动画、Start 错接 Moving 门禁、未来轨迹不足 0.70 秒三项内存负例均被作者配置门禁拒绝。临时预检模块已删除，未生成资产、编译 Blueprint 或运行 PIE |
| 2026-09-11 | 完成任务 6.4 资产回读：Schema 的 34 维 Channel 配置与 Lua 契约一致；四个 BruteForce 数据库的索引姿势数、Values、Weights、Deviation 均一致，数据库动画依赖、Normalization Set 成员及 Chooser 数据库依赖全部正确且不含旧混合库 |
| 2026-09-11 | 用户完成 `SekiroAssetManager` 编译后重新执行正式重建脚本：4 个最小动画均通过当前平台压缩 Residency 门禁，Schema、Normalization Set、四个阶段数据库与 Chooser 全部更新保存，未再出现 `bEnforceCompressedDataSampling` 断言；未执行 Blueprint 编译或 PIE |
| 2026-09-11 | 修复任务 6.4 资产重建在 `bEnforceCompressedDataSampling` 门禁暂停：新增当前平台动画压缩 Residency 的同步准备、逐项验证与幂等释放接口；重建脚本从 Lua 数据库 IR 提取动画路径并在完整写入事务外层持有，未改引擎、未切换 Raw Animation Data。源码尚未编译，资产尚未重试生成 |
| 2026-09-11 | 推进任务 6.4 源码契约：Lua 现可从唯一 Schema 配置生成 Skeleton 与两个 Instanced Channel 的完整反射 IR，并逐段验证 UE5.8 Finalize 顺序和 34 维 Cardinality；通用插件补充项目无关的 InstancedObject 创建与安全回滚，重建顺序改为 Schema → Set → Database → Chooser。未编译插件、未生成资产 |
| 2026-09-11 | 完成任务 6.3 源码契约：六个正式阶段族统一登记 Locomotion Schema，当前四个最小数据库共享 `PSN_FreeRun_Locomotion`；Pivots、Lands 延后到合法数据接入。重建脚本已按 Set → Database → Chooser 的依赖顺序更新，未执行脚本或修改二进制资产 |
| 2026-09-11 | 完成任务 6.2：把 QueryOrigin、Trajectory Position/Facing、Pose Position/Velocity、只狼 Translation 轴适配与世界空间拒绝策略写入 Lua Schema 契约；定向确认现有 BuildQuery 已在统一边界执行转换，不新增 C++ 修改 |
| 2026-09-11 | 用户报告分阶段数据库与 Chooser 测试通过后进入任务 6；以 UE5.8 原生 Locomotion Channel 默认时间点为基线，在项目 Lua 固定 Trajectory/Pose 特征、Sekiro 骨骼名、权重、严格向量展开顺序与 34 维 Cardinality。当前仅完成共享 Schema 契约，尚未物化资产 |
| 2026-09-11 | 用户完成插件编译后，确认三个 Detailed UFUNCTION 已加载；成功生成并保存 Stationary、Starts、Loops、Stops 四个正式数据库和 `CHT_LocomotionDatabases`，随后按 Check → Generate → Save 全量重建 `ABP_SekiroMotionMatching` Lua-owned Graph。未执行 AnimBlueprint 原生编译或 PIE |
| 2026-09-11 | 首次分阶段资产生成在新建 Stationary 数据库后被 Dirty Package 保护拒绝；脚本已改为先保存模板副本再应用 Lua 全量补丁，通用插件新增不破坏旧接口的 Python 安全 Detailed 包装，使失败时仍返回 Success、对象与错误文本。源码静态检查通过，等待插件编译后继续生成 |
| 2026-09-11 | 根据 PIE 中 StartRequested 仍选到约 2.999 秒 Idle 的证据，将旧混合数据库改为 Stationary、Starts、Loops、Stops 四个单阶段正式数据库；Chooser 每条规则只返回对应单项数据库，重建脚本增加全写入前目标路径校验与缺失目录创建；尚未生成资产或重新 PIE |
| 2026-09-11 | 定位无动画链中的 UE5.2 Pose Search Database 迁移残留；新增项目无关的递归 UObject 属性反射 IR 入口，Lua 全量声明四项最小动画与 UE5.8 区间字段，并补充 Query、节点绑定和 PostSelection 失败诊断；尚未编译或生成资产 |
| 2026-09-10 | 修复 UE5.8 JSON Writer 拒绝根级标量 Token 导致 Chooser IR 编码失败的问题；生成并保存正式 Chooser 与 `ABP_SekiroMotionMatching`，AnimBlueprint 零诊断且 Canonical IR 回读确认节点、连接、属性和函数绑定与 Lua 一致 |
| 2026-09-10 | 修复正式 AnimBlueprint 无法按 Lua 更新的源码阻断：Chooser CDO 配置改为可保存未解析路径的软引用并在游戏线程初始化时解析，反向读取器在无生成基线时恢复 Lua `SourceModule`；当前二进制蓝图仍待授权编译与重新生成 |
| 2026-09-10 | 静态确认正式最小 AnimGraph 未接入 Slot/GAS、IK 或 Warping；区分“图结构已收敛”与“数据库动画集合未验证”，首轮验收严格限定 Idle、Forward Start、Forward Run Loop、Forward Stop，现有 Pivot 不纳入验收 |
| 2026-09-10 | 确认 Pose History 的一帧滞后仅来自最终 Pose 反馈；当前 MoveIntent 在同次 QuerySnapshot 构建中直接消费，禁止为了与历史对齐再增加项目侧输入缓存 |
| 2026-09-10 | 静态核对正式 Lua AnimGraph 的 Provider 边界：History Collector 已包住查询门禁后的 Motion Matching 最终 Pose，轨迹只接入 Collector 的 UE5.8 TransformTrajectory Pin；无需新增平行动画图 |
| 2026-09-10 | 为有效移动阶段的持久请求接入 `ForceInterruptAndInvalidateContinuingPose`，显式排除并清空不兼容的 Idle Continuing Pose，同时保留普通手动重搜的非失效语义 |
| 2026-09-10 | 接通通用 Lua ChooserTable IR 的编辑器侧解析与事务性物化源码：支持可扩展 EnumAny 反射列、多结果行、瞬态预编译和可选保存，不在插件中引入项目字段或 PoseSearch 类型依赖 |
| 2026-09-09 | 明确 RootMotion Motion Matching 不求同帧循环精确解：未来 Query 独立于本帧候选，选中动画的 RootMotion 经 CMC 后只反馈到下一帧实际历史 |
| 2026-09-09 | 依据《CMC驱动的MotionMatching完整流程》按运行时因果链重排任务：事实快照 → 轨迹 → 语义 → Chooser → Provider/Query/Search → Blend/PoseHistory → RootMotion/CMC 反馈；保留本项目动画 RootMotion 权威边界 |
| 2026-09-08 | 明确 `ABP_SekiroMotionMatching` 是正式系统的最终动画蓝图载体；完成 Lua 全量图生成、角色与动画蓝图编译、Skeleton/AnimClass/RootMotion 模式及 CMC 旋转静态核验，阶段 1 仅余 Cook 依赖与 Classic 回退隔离验证 |
| 2026-09-08 | 对照当前源码与 Lua 基线复核任务状态；确认最小轨迹/AnimInstance/AnimGraph 已迁入，但持久搜索握手、Chooser 分库和 RootMotion 所有权仍待实施 |
| 2026-09-07 | 在独立项目 `F:/ProjectAI/Sekiro5.8` 建立 UE 5.8 RootMotion 适配任务；迁入产物标记为待复验，不继承 UE 5.2 的验证结论 |
