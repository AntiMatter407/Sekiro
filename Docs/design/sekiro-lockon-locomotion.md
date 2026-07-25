# Sekiro 锁定状态 Locomotion 设计

> 状态：历史实现方案。四方向资源选择和输入语义仍有效，但本文所述 BlendSpace、MovePhase 匹配和 Stop 轨迹冻结等旧动态 Pose Graph 机制已经删除；Orientation Warping 后来以原生生成节点重新接入。
> 当前实现：明确 SequencePlayer + `BlendListByEnum`，Cycle 使用 Sync Group + Inertialization；Movement 选择四向素材并用剩余角旋转 Actor，使原始 Root Motion 对齐输入轨迹，AnimGraph 只对脊柱链做反向回正。
> 日期：2026-07-11  
> 关联文档：[角色摄像头与 Locomotion 方案](sekiro-camera-locomotion.md)、[动画曲线生成与使用手册](../animation-curve-authoring-guide.md)

## 一、目标

锁定目标后，非 Sprint 状态下角色始终逐渐面向锁定目标，移动输入只决定相对目标的前、后、左、右位移；摄像机继续跟踪锁定目标。进入 Sprint 后，角色改为面向移动方向并只播放前向 Sprint 动画，但摄像机仍保持锁定目标。

本方案只设计 GroundLocomotion，不改变目标搜索、锁定 UI、攻击吸附和目标切换规则。

## 二、已确认的资源与数据

### 2.1 四方向 Walk/Run

当前四个 BlendSpace1D 配置一致，X 轴使用移动档位：

| 方向 | BlendSpace | Walk 样本 | Run 样本 |
| --- | --- | --- | --- |
| Forward | `Sekiro_CycleForward1D` | `Walk_Forward_Loop @ 140` | `Run_Forward_Loop @ 400` |
| Back | `Sekiro_CycleBack1D` | `Walk_Back_Loop @ 140` | `Run_Back_Loop @ 400` |
| Left | `Sekiro_CycleLeft1D` | `Walk_Left_Loop @ 140` | `Run_Left_Loop @ 400` |
| Right | `Sekiro_CycleRight1D` | `Walk_Right_Loop @ 140` | `Run_Right_Loop @ 400` |

四方向 Start、Loop、Stop 和 Step 资源都已在 `AnimAssets.lua` 中声明。Sprint 只有前向循环，不能作为锁定四方向 BlendSpace 的第五种方向。

### 2.2 已有运行时数据

Lua 动画状态机已经可以直接读取：

| 字段 | 用途 |
| --- | --- |
| `bIsLockedOn` | 是否仍持有有效锁定目标 |
| `RotationMode` | `LookingDirection` 表示锁定环绕，`SprintAlign` 表示 Sprint 覆盖锁定朝向 |
| `MoveDirectionAngle` | 期望世界移动方向相对角色朝向的角度 |
| `MoveInputX/Y` | 屏幕空间原始移动输入 |
| `AimYawDelta` | 锁定目标/控制器方向相对角色朝向的角度 |
| `MovementTier` / `DesiredGait` | Walk、Run、Sprint 请求 |
| `DodgeDirection` / `DodgeDirectionLateral` | Step 按下时锁定的前后、左右方向 |

当前真正阻断锁定移动的是 `GroundLocomotion:WantsMove()`：它只允许非锁定模式，因此锁定时虽然资源和变量都存在，却无法进入 Start/Cycle。

## 三、正交模式

不增加 `LockIdle/LockStart/LockCycle/...` 一整套重复状态。状态仍保持：

```text
Idle / Start / Cycle / Turn / Stop / Step
```

另设正交朝向模式：

| 模式 | 判定优先级 | ActorYaw | 摄像机/ControllerYaw | 动画 |
| --- | ---: | --- | --- | --- |
| `SprintAlign` | 1 | 朝移动方向 | 有锁定目标时仍朝目标 | 前向 Sprint |
| `LockOn` | 2 | 朝锁定目标 | 朝锁定目标 | 四方向 Walk/Run/Step |
| `Free` | 3 | 朝移动方向 | 玩家自由控制 | 前向 Walk/Run/Step |

模式解析以 `RotationMode` 为主，`bIsLockedOn` 只表示目标仍有效：

```text
RotationMode == SprintAlign     -> SprintAlign
RotationMode == LookingDirection -> LockOn
其他                            -> Free
```

这样锁定状态长按 Sprint 时不会错误地继续选择后退或侧移动画。

## 四、Yaw 所有权

同一帧只能有一个系统修改 ActorYaw。

### LockOn 非 Sprint

- `SKMovementComponent.lua` 在原生 CharacterMovement 求值前把 ActorYaw 插值到锁定目标。
- GroundLocomotion 对 Idle、Start、Cycle、Stop、锁定 Step 提交 `RootMotionRotationMode.Ignore`。
- 动画只贡献 RootMotion 平移，不用根旋转争抢朝向。

### SprintAlign

- 角色朝向移动方向。
- 普通 Sprint Start/Cycle/Stop 都由 Movement Lua 持续插值 ActorYaw。
- 当前动画资产不贡献角色转向；后方输入选择最近的 Left/Right Turn，而不是依赖 BackTurn 或 RootMotion 旋转。
- 即使处于锁定状态，ControllerYaw 仍持续跟踪目标，镜头不改为看移动方向。

### Free

Movement Lua 把角色朝向移动方向，摄像机不被移动输入修改。动画 RootMotion 只提供相对角色朝向的平移；Back 动画语义始终是身体保持朝前时向后退，只在锁定模式使用。

## 五、锁定方向解析

Cycle 的素材选区与轨迹对齐承担不同职责：`MoveInputX/MoveInputY` 表达玩家相对锁定视角的即时方向意图，用于快速选择 Forward/Back/Left/Right；`MoveDirectionAngle` 表达实际世界移动方向相对当前角色朝向的角度，用于把每条素材分别对齐真实轨迹。两者不能在方向选择器之后共用一个残差，而要在各条 Sequence 分支混合前分别应用。

基础分区：

| 角度 | 方向 |
| --- | --- |
| `[-60°, 60°]` | Forward |
| `(60°, 120°)` | Right |
| `(-120°, -60°)` | Left |
| `[120°, 180°]` 或 `[-180°, -120°]` | Back |

方向解析必须优先保持当前素材，避免斜向输入同时触发换腿与 ActorYaw 转向。四方向按相邻主轴中点使用 `45°/135°` 初始分类；当前素材相对输入方向的残差不超过基础 `45°` 加 `10°` 滞回时继续保持，超过 `55°` 才重新分类。这样先向左再加入后退时仍使用 Left，并由 ActorYaw 平滑形成后左轨迹；只有接近纯后退、当前素材残差超过保持范围时才切换 Back。Start、Stop 或 Step 的基础动画方向一旦进入状态就锁定到动作结束；Cycle 可以在超出保持范围后更新基础动画方向。

四向分区只决定播放哪条 Sequence，不得量化真实移动轨迹。Lua 为四条分支同时计算 `实际移动角 - 各素材主方向角`：Forward、Back、Left、Right 分别以 `0°/180°/-90°/90°` 为主轴。每条 Sequence 先通过自己的 Orientation Warping 对齐，再由 `BlendListByEnum` 混合；因此 Left 淡出和 Forward 淡入期间不会误用同一个残差。分支内部角度立即应用，平滑只由方向 Pose 混合承担；`Spine/Spine1/Spine2` 继续反向补偿，使角色胸口朝向锁定目标。

无移动输入时不把方向重置为 Forward。停止过程中保留最后一个有效 `LockedMoveDirection`，用于选择匹配的 Stop 动画。

## 六、状态行为

### 6.1 Idle

- 没有输入时播放 Idle，CameraManager 继续让角色缓慢面向目标。
- `AimYawDelta` 超过 Idle Turn 阈值时，可进入现有 `Idle -> Idle Turn`。
- 有输入时根据锁定方向进入 Start，不根据移动方向旋转角色。
- 锁定目标丢失后，下一个更新立即回到 Free 模式，但不强制重置状态。

### 6.2 Start

- 进入时锁定方向和步态。
- Walk 选择 `Walk_<Direction>_Start`。
- Run 选择 `Run_<Direction>_Start`。
- RootMotion 旋转设为 `Ignore`，ActorYaw 继续由锁定相机逻辑朝目标插值。
- `Start -> Cycle` 仍由 `CanEnterLoop >= 0.5` 决定。
- Start 播放期间输入改变不更换一次性动画，但方向残差实时读取最新输入并驱动下半身对齐；进入 Cycle 后基础四向动画和残差都读取最新方向。

### 6.3 Cycle

- Walk/Run 根据锁定方向选择四个明确的 Loop Sequence，不再使用速度 BlendSpace。
- Walk/Run 步态切换先匹配 `MovePhase`，再使用 UE Inertialization；四方向素材切换使用普通短交叉混合。
- 方向改变不创建 Turn 状态，因为角色始终面向目标；四向素材只提供最近的下半身姿势，连续残差负责精确斜向轨迹。
- `Cycle -> Stop` 必须先确认输入释放，再等待当前 Sequence 的 `CanEnterStop >= 0.5`。
- 锁定期间 Walk/Run 不修改 PlayRate，速度和节奏由当前 Root Motion Sequence 表达。

### 6.4 Stop

- 输入释放时锁定最后一个有效 Cycle 方向。
- 同时冻结输入释放前的精确角度。Stop Root Motion 在整个动作中继续沿该斜向轨迹，不能退化为四向直线。
- Stop 全程保持释放时的 Actor 朝向、方向素材和脊柱回正，禁止在没有换脚动作时直接把补偿收回到零。
- 残差角绝对值达到 `StopTurnMinResidualAngle` 时，Stop 在 `CanEnterIdle` 窗口进入专用 StopTurn；站立/蹲姿分别播放对应左右 Idle Turn，并由 `StopTurnDirectionAlignment` 在换脚阶段撤销补偿。
- 残差角较小时直接进入 Idle，避免很小的角度也播放完整转身动作。
- 输入释放时把角度和权重分别锁存到 Stop 专用变量。后续重新输入只更新 Start/Cycle，不得改写仍在混合退出的 Stop 方向。
- Walk 选择 `Walk_<Direction>_Stop`，Run 选择 `Run_<Direction>_Stop`。
- `Stop -> Idle` 使用 `CanEnterIdle >= 0.5`。
- `StopTurn -> Idle` 使用 `CanExitTurn >= 0.5`。
- Stop 中重新输入时，根据新方向进入对应 Start；锁定环绕内不创建面向移动方向的 Turn。

### 6.5 Step

- 沿用已经实现的按下立即 Step。
- 锁定时保持面向目标，并按 `DodgeDirection/DodgeDirectionLateral` 选择 `Step_Forward/Back/Left/Right`。
- 非锁定时快速面向输入方向并使用 `Step_Forward`。
- 长按超过 Sprint 阈值后，Step 完成再进入 SprintAlign；不能让 Sprint 抢掉 Step 的第一帧响应。

### 6.6 Turn

锁定模式只保留两类 Turn：

1. 无移动时的大角度 `Idle -> Idle Turn`。
2. 进入或退出 SprintAlign 的 Sprint Start/TurnStop。

锁定 Walk/Run 的 Forward/Back/Left/Right 切换不进入 Turn。否则角色会先停下转身，破坏围绕目标移动的连续性。

## 七、方向切换与相位

当前 C++ 在同一个 State 内更换 AnimationAsset 时会把新资产时间重置为 0。锁定 Cycle 在四个 BlendSpace 之间切换时若沿用该行为，会产生明显脚步跳变。

第一阶段必须增加通用的“循环资产切换保留进度”能力：

```text
同一 Layer + 同一 State + 旧新资产都循环
    -> NewTime = OldNormalizedTime * NewPlayLength
    -> 保留 PreviousAsset 做短交叉混合
```

该能力属于插件通用接口，不能硬编码 Sekiro 资源或方向名。Lua 在 Cycle 方向改变时显式声明 `PreserveNormalizedTimeOnAssetChange = true`。

`MovePhase` 可作为第二阶段精确同步：若归一化时间仍出现左右脚错相，则读取旧动画 `MovePhase`，在目标 BlendSpace 中寻找最接近相位的起始时间。此阶段继续使用现有 `MovePhase`，不新增方向专用曲线。

## 八、Sprint 与锁定交互

### 进入 Sprint

```text
LockOn Walk/Run/Step
    -> Dodge 保持超过阈值
    -> 根据移动方向建立 Sprint TurnPlan
    -> 角色朝移动方向
    -> 摄像机继续朝锁定目标
    -> Sprint Forward Cycle
```

Sprint 只有前向动画，因此锁定时按左、右、后 Sprint 不是播放侧向/后向 Sprint，而是快速转向该屏幕移动方向后前向冲刺。

### 退出 Sprint

```text
Sprint Forward Cycle
    -> 松开 Sprint
    -> 目标仍锁定：目标朝向角使用 AimYawDelta
    -> 播放 Sprint Forward/Left/Right Turn Stop
    -> ActorYaw 回到锁定目标
    -> 进入 LockOn Walk/Run/Idle
```

这里不能继续使用 Sprint 运动方向作为退出目标角，否则角色会停在输入方向，造成锁定移动方向与目标产生偏差。

## 九、相机规则

- LockOn 非 Sprint：ActorYaw 和 ControllerYaw 都朝目标，但使用各自插值速度。
- LockOn Sprint：ActorYaw 朝移动方向，ControllerYaw 仍朝目标。
- 移动输入绝不直接修改相机视角。
- Look 输入在锁定时用于后续目标切换，不直接解除目标跟踪。
- ActorYaw 始终由 Movement Lua 更新；Camera Lua 只更新 ControllerYaw，不与 Movement 争抢角色旋转。

本阶段不修改镜头构图、Pitch、SpringArm Offset 和目标切换算法。若锁定镜头仍显得生硬，应单独调整相机位置/阻尼，不能通过修改动画方向补偿。

## 十、建议配置

| 配置 | 初始值 | 作用 |
| --- | ---: | --- |
| `LockedDirectionForwardBoundaryAngle` | `60°` | Forward 与 Left/Right 的基础分界 |
| `LockedDirectionBackBoundaryAngle` | `120°` | Left/Right 与 Back 的基础分界 |
| `LockedDirectionHysteresisAngle` | `10°` | 防止方向边界抖动 |
| `LockOnWarpingMaxAngle` | `70°` | 覆盖基础边界加滞回后的最大方向残差 |
| `DirectionBlendDuration` | `0.06s` | 四向 Sequence 分支切换混合 |
| `LockOnActorInterpSpeed` | `14`（Movement Lua） | 非 Sprint 身体跟随目标 |
| `LockOnCameraYawInterpSpeed` | 保持 `8` | 镜头跟随目标 |
| `SprintActorInterpSpeed` | `12`（Movement Lua） | Sprint 身体追随移动方向 |

先保持现有相机参数，只在完成动画方向接入后基于日志和录像调优，避免同时改变两个系统而无法定位问题。

## 十一、调试输出

GroundLocomotion 的 Context/MotionPlan 日志增加：

```text
facingMode=LockOn
lockedDirection=Left
moveDirectionAngle=-82.4
directionAsset=Sekiro_CycleLeft1D
gaitBlendInput=263.0
yawOwner=MovementLua
phasePreserved=true
```

只在模式、方向、资产或 Yaw 所有者变化时记录，禁止每帧刷同一条方向日志。

## 十二、实施阶段

### 阶段一：锁定 Walk/Run 主链

状态：已完成。

1. 放开 `WantsMove()` 对 LockOn 的排除。
2. 增加正交朝向模式和带滞回的锁定方向解析。
3. 接入四方向 Start/Cycle/Stop。
4. 锁定非 Sprint 状态统一使用 `RootMotionRotationMode.Ignore`。
5. 验证 `CanEnterLoop/Stop/Idle`。

### 阶段二：方向切换相位

状态：已完成 normalized time 保持方案；`MovePhase` 精确匹配保留为出现可见错相时的后续优化。

1. 插件增加循环资产切换保留 normalized time 的通用选项。
2. Lua 切换方向 BlendSpace 时显式开启。
3. 验证 Walk/Run 混合和四方向切换没有脚步跳变。
4. 必要时再实现 `MovePhase` 精确匹配。

### 阶段三：Sprint 与模式切换

状态：已完成代码接入；仍需结合手柄和实际目标距离做体验验证。

1. 锁定进入 Sprint 时角色朝移动方向，镜头继续跟踪目标。
2. Sprint 退出锁定时改用 `AimYawDelta` 建立返回目标的 TurnPlan。
3. 验证 Step 长按到 Sprint、Sprint 松开到锁定 Run/Idle。

### 阶段四：调试和相机调优

状态：进行中。PIE 已验证 Forward/Right/Back/Left 均选择正确 BlendSpace，切换保持在 Cycle 且不会重置到第 0 帧。

1. 增加模式、方向、资产、相位和 Yaw 所有者日志。
2. 录制 Forward/Back/Left/Right、斜向、方向边界和绕目标一圈。
3. 动画稳定后再调相机阻尼与构图。

## 十三、验收标准

- 锁定非 Sprint 时角色持续面向目标，WASD 分别播放正确的前后左右动画。
- 斜向输入经过方向滞回后稳定，不在两个 BlendSpace 之间快速切换。
- 从任意方向松开输入时，Stop 动画与最后移动方向一致。
- 四方向 Cycle 切换不从第 0 帧重新起步，不出现明显脚步跳变。
- 锁定 Step 按下立即响应，并播放正确方向动画。
- 锁定 Sprint 时角色朝移动方向，镜头仍看目标。
- 松开 Sprint 后角色快速、平滑地重新面向目标。
- 锁定和解锁过程中没有 Camera 与 RootMotion 同帧争抢 ActorYaw。
- 正常过渡不触发 `CurveFallback`。
