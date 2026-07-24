# ABP_Sekiro Lua AnimBlueprint V2 设计

> 状态：阶段 A-F 第一版已实现；2026-07-18 起地面层采用“共享阶段状态机 + 独立姿态 Pose 构建器”
> 基础设施：Lua AnimBlueprint Compiler Schema v2  
> 旧实现参考：提交 `f92d6833` 中的 `ABP_Sekiro.lua`、`GroundLocomotion.lua`

## 一、目标

新的 `ABP_Sekiro` 继续由 Lua 描述，但 Lua 的职责改为模拟动画蓝图编辑器：

```text
ABP_Sekiro.lua
    -> 声明 Graph / Node / State / Transition / Variable
    -> AnimGraph IR
    -> NodeFactory 生成原生 UAnimBlueprint
    -> UE 原生状态机、SequencePlayer、混合、RootMotion 和 Notify 运行
```

运行时 Lua 只做两件事：

1. 在游戏线程更新方向、步态和一次性动作选择等 AnimInstance 参数。
2. 以强类型 Rule AST 声明只读切换条件，由 Factory 生成 UE 原生 Transition Rule Graph。

Lua 不再返回 Pose，不再用动画名字符串驱动播放器，不再维护假的 `FPoseLink`、状态时间或播放时间。

## 二、旧逻辑保留与废弃

保留的行为：

- Standing/Crouching 共用 Idle、Start、Cycle、Stop 阶段流程，姿态资产仍由独立 Lua 模块声明。
- 非锁定时角色朝移动方向，锁定时四向移动并朝向目标。
- Step 按下立即响应，长按后进入 Sprint。
- Sprint 作为 Standing 的步态 Pose 分支，与 Walk/Run 在同一个 Cycle 状态内原生混合。
- Jump 分 Start、InAir、Land；InAir 位移由 CharacterMovement 物理负责。
- `CanEnterStop` 继续用于自然停止；`MovePhase`、`FootPlant` 保留资产数据，当前尚无正式运行时消费者。

废弃的旧机制：

- `PlaySequence()` 在每帧动态返回动画资产。
- `CurrentStateName`、`StateTime`、`EvaluatingResetTime` 等 Lua 模拟状态机字段。
- `GetCurrentAnimationNormalizedTime()` 等旧 Host 节点查询接口。
- 通过 `Anim.<前缀> .. Direction` 拼接资源名。
- Lua 自己执行 Pose 混合、RootMotion 混合或动画线程更新。

## 三、最终 AnimGraph

第一版最终输出：

```text
RootLocomotionSM
    -> Inertialization
    -> LocalToComponentSpace
    -> FootPlacement
    -> LegIK
    -> ComponentToLocalSpace
    -> OutputPose
```

后续战斗层加入后：

```text
RootLocomotionSM
    -> Inertialization
    -> Locomotion Cache Pose
    -> UpperBody Slot / LayeredBoneBlend
    -> FullBody Slot
    -> LocalToComponentSpace
    -> FootPlacement
    -> LegIK
    -> ComponentToLocalSpace
    -> OutputPose
```

`RootLocomotionSM` 只表达大状态：

```text
RootLocomotionSM
├── Grounded  -> GroundedModeSM
└── InAir     -> JumpSM
```

Transition：

| From | To | Lua 意图 |
|------|----|----------|
| Grounded | InAir | `Inst.bIsInAir == true` |
| InAir | Grounded | 有输入等待较早的 `CanResumeMovement`；无输入等待尾部 `CanExitLand` |

## 四、Grounded 状态层级

`GroundedModeSM` 只表达具有时间先后关系的运动阶段：

```text
GroundedModeSM
├── EntryRouter
├── Idle
├── Start
├── Cycle
├── Stop
└── Step
```

优先级固定为：

```text
InAir > Step > Grounded Phase
```

Standing/Crouching 姿态和 Walk/Run/Sprint 步态不是额外状态。它们由每个 StateGraph 内的 `BlendListByBool` 与 `BlendListByEnum` 选择，因此姿态或步态变化不会重进状态机 Entry。

`EntryRouter` 只处理进入 Grounded 时已经存在的意图：Dodge 优先进入 Step，持续移动直接进入 Cycle，无输入进入 Idle。它不会改变正常起步；角色已在 Idle 后新按方向键仍然执行 `Idle -> Start -> Cycle`。因此落地时持续按方向不会重播 Start，也不会使用跳跃前锁存的旧方向误触发 RunTurn。

### 4.1 共享阶段状态机

```text
Entry -> EntryRouter
             ├-> Step（Dodge 生效）
             ├-> Cycle（进入 Grounded 时已有移动输入）
             └-> Idle（没有移动输入）

Idle -> Start -> Cycle -> Stop -> Idle
  |       |        |       |
  +------ Step <----+-------+
            |
            +-> Cycle（仍有移动输入）
            +-> Idle（没有移动输入）
```

状态职责：

| State | Pose | 选择锁定时机 |
|-------|------|----------------|
| EntryRouter | Standing/Crouching Idle | 仅按当前输入恢复地面阶段 |
| Idle | Standing/Crouching Idle | 姿态可更新 |
| Start | 姿态 × 步态 × 方向 Start | 进入动作时锁定 Gait、Direction |
| Cycle | 姿态 × 步态 × 方向 Loop | `PoseGait`、姿态和方向均可原生混合 |
| Stop | 姿态 × 步态 × 方向 Stop | 输入释放边沿锁定最后方向和步态 |
| Step | 四方向 Step | Dodge 上升沿锁定方向 |

非锁定模式下 `CycleDirection` 始终为 Forward；大角度起步使用 Movement Lua 旋转前锁存的方向角选择 Left/Right Turn Start。后方输入也按角度符号选择最近的左右转身，不使用表示“身体朝前向后退”的 Back 素材；Movement Lua 同时把角色朝向输入世界方向。

锁定模式下 `CycleDirection` 使用 Forward/Back/Left/Right 四向选择。基础素材按 `MoveInputX/MoveInputY` 的即时输入角选区：Forward 覆盖绝对角 `0°..60°`，Back 覆盖 `120°..180°`，中间扇区选择 Left/Right。滞回采用前后优先的非对称规则：Forward/Back 离开自身扇区时保留 `10°` 容差，Left/Right 进入前后基础扇区时则在 `60°/120°` 立即切换。Cycle 为四条 Sequence 分别计算 `MoveDirectionAngle - 素材主方向角`，在 `BlendListByEnum` 之前各自执行 Orientation Warping；新旧方向混合时两条素材因此都保持对齐真实轨迹。Start/Stop 方向在动作边沿锁存，继续使用选择器后的单节点对齐。原生节点通过 `Spine/Spine1/Spine2` 反向补偿，使上半身继续朝向目标。Sprint、Step 和空中状态不启用 Cycle 分支对齐。

### 4.2 Standing 与 Crouching Pose 构建器

`Standing.lua` 与 `Crouching.lua` 仍然分文件维护资产和节点声明，但不再分别拥有状态机或 Transition：

```text
StateGraph_Cycle
└── Stance Selector
    ├── Standing Builder
    │   └── Walk / Run / Sprint
    └── Crouching Builder
        └── Walk / Run
```

姿态改变时阶段保持不变，例如 `Standing Cycle -> Crouching Cycle` 只改变原生 Pose 分支。当前不播放 `Stand_Crouch_Idle` 或 `Crouch_Stand_Idle` 转换 Sequence；如后续需要明确的蹲下/起身动作，应新增 `EnterCrouch`、`ExitCrouch` 一次性阶段，而不是恢复嵌套姿态状态机。

### 4.3 Sprint 步态分支

- Sprint 不再拥有独立 `SprintSM`。
- Idle 直接起步时，Standing Start 选择 Sprint Forward/LeftTurn/RightTurn 资产。
- Cycle 使用 `Sprint_Forward_Loop`，与 Walk/Run 循环处于同一个同步组。
- 方向键仍按住时松开 Shift，阶段保持 Cycle，`PoseGait` 直接从 Sprint 切到 Run；同时按住 Alt 时直接切到 Walk。
- Sprint 时 Movement 朝移动方向快速转向；锁定目标只影响摄像机，不让角色继续四向平移。

### 4.4 Step

Step 使用独立四向 Sequence Selector：

- 锁定：按 `DodgeDirection` 和 `DodgeDirectionLateral` 选择 Forward/Back/Left/Right。
- 非锁定：Movement 朝输入方向转向，动画使用 Forward；无输入时默认 Forward。
- Direction 在 Step 进入时锁定，播放期间输入转向不切换 Sequence。
- `CanExitStep` 曲线开启后，有移动输入直接进入 Cycle，没有移动输入返回 Idle；不会再次播放 Start 或 Sprint Start。

## 五、JumpSM

```text
Entry -> JumpStart -> InAir -> Land -> Grounded
```

- 非锁定：角色先朝移动方向，使用 Forward Start/InAir/Land。
- 锁定：根据进入空中时的实际水平运动角选择最近八方向 Sequence，并锁存最多 22.5 度的量化残差。
- Jump Start、定向 InAir 和 Land 通过共享方向对齐链把下半身对齐物理轨迹，同时反向补偿脊柱使上半身继续朝向锁定目标。
- Jump Start 在离地期间忽略动画 RootMotion，水平惯性和垂直轨迹完全交给 CharacterMovement；物理接地后恢复 RootMotion，由 Land 资产完成落地位移。
- InAir Sequence 不提供位移，水平/垂直轨迹由 CharacterMovement 计算。
- Land 使用两个互不冲突的退出窗口：有移动输入时，原地/非锁定共用 Land 在归一化 35%、锁定八方向 Land 在归一化 50% 开放 `CanResumeMovement`，随后 `EntryRouter` 直接恢复 Cycle；无输入时仍等待尾部 `CanExitLand`，完整保持 Land 后回到 Idle。原地与非锁定前向 Land 共用 `Jump_Light_Stand`，不保留重复资产别名。再次离地时 `Land -> Start`，支持连续跳跃或台阶边缘情况。

## 六、AnimInstance 变量

`USKAnimInstance` 已有字段继续作为事实来源。Transition Rule 通过原生 Bool 属性 Getter 读取这些字段；`BlueprintUpdateAnimation` 仍通过显式 `Inst` 参数更新生成变量。

运行时数据顺序固定为：

```text
Input Lua 发布 MoveIntent / MovementTier
    -> Movement Lua 在 CharacterMovement 原生求值前计算 DesiredMoveYaw
    -> Movement Lua 发布 MoveDirectionAngleBeforeRotation 并更新 ActorYaw
    -> UE CharacterMovement 应用 Root Motion、物理、碰撞和网络预测
    -> USKAnimInstance 采集 Movement 快照
    -> ABP_Sekiro.BlueprintUpdateAnimation 选择方向与动画
```

`MoveDirectionAngle` 表示角色更新朝向后的当前局部移动角，锁定四向循环用它计算四条分支各自的轨迹对齐残差；`MoveInputX/MoveInputY` 用于即时基础素材选区。`MoveDirectionAngleBeforeRotation` 表示同帧 Movement Lua 转向前的输入角，只用于自由起步和 Sprint 转身动画锁存。

新编译器需要支持在生成类中声明以下瞬态变量：

| Variable | 类型 | 用途 |
|----------|------|------|
| `CycleDirection` | Byte/Enum | 当前循环四方向，允许滞回更新 |
| `LatchedActionDirection` | Byte/Enum | Start/Stop/Step/Sprint 进入或退出边沿锁定方向 |
| `LatchedFreeStartDirection` | Byte/Enum | 非锁定 Start 的前后左右起步方向 |
| `PoseGait` | Byte/Enum | Cycle 当前 Walk/Run/Sprint Pose 分支；直接追随输入目标步态 |
| `LatchedActionGait` | Byte/Enum | 一次性动作进入时锁定 Walk/Run/Sprint |
| `bPoseCrouching` | Bool | StateGraph 内 Standing/Crouching 原生姿态选择 |
| `CycleForward/Back/Left/RightResidualAngle` | Float | 精确方向分别减去四条素材主方向；连接 Cycle 各分支混合前的独立方向对齐链 |
| `LockOnWarpingAlpha` | Float | 锁定地面 Walk/Run 有输入时为 1；Sprint、空中和非锁定模式为 0 |
| `StartDirectionResidualAngle` / `StartWarpingAlpha` | Float | Start 使用的实时四向量化残差和启用强度；基础动画方向锁存，但残差持续追随输入 |
| `StopDirectionResidualAngle` | Float | Stop 使用的四向量化残差；输入释放边沿锁存，播放期间不被后续 Start/Cycle 改写 |
| `StopWarpingAlpha` | Float | Stop 的 Orientation Warping 强度；斜向 Stop 全程保持，不再无动作撤销 |
| `StopTurnDirection` / `bStopTurnRequested` | Enum/Bool | 残差角达到阈值时选择左右换脚动画并请求进入 StopTurn |
| `StopTurnWarpingAlpha` / `bStopTurnAlignmentCurveSeen` | Float/Bool | StopTurn 的补偿强度，以及当前 Turn Sequence 的连续曲线是否已经接管权重 |
| `JumpDirection` | Byte/Enum | 离地上升沿锁定的八方向；非锁定当前固定 Forward |
| `JumpDirectionResidualAngle` / `JumpWarpingAlpha` | Float | Jump 八向素材的量化残差和启用强度；与 `JumpDirection` 同时锁存 |
| `bLatchedActionLockedOn` | Bool | Standing Start 选择锁定/非锁定资产集合 |
| `bHadMovementInput` / `bWasDodging` / `bWasSprintRequested` / `bWasInAir` | Bool | 检测输入、动作和离地边沿 |
| `bWasLockedOn` | Bool | 当前保留字段；已写入但尚无消费者 |

运行时统一入口：

```lua
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@param _delta_seconds number 本帧时长；当前方向分类不依赖帧率但保留标准事件签名。
---@return nil result 直接更新生成变量。
function ABP_Sekiro.BlueprintUpdateAnimation(Inst, _delta_seconds)
    local input_direction_angle = Direction.GetAngleFromAxes(
        Inst.MoveInputY,
        Inst.MoveInputX)
    Inst.CycleDirection = Direction.ResolveCardinalWithHysteresis(
        input_direction_angle,
        Inst.CycleDirection,
        Tuning.LockedDirectionHysteresisAngle,
        Tuning.LockedDirectionForwardBoundaryAngle,
        Tuning.LockedDirectionBackBoundaryAngle)
    Inst.CycleForwardResidualAngle = Direction.GetCardinalResidual(
        Inst.MoveDirectionAngle,
        Direction.Cardinal.Forward)
end
```

这些变量必须是真实生成类属性。Lua 使用 `Inst.VariableName = value` 直接写入，不经过 `facts`、字符串键缓存或 `SetLuaXxx()` 包装函数。

## 七、Pose 选择节点

旧 `PlaySequenceByDirection()` 改为编译期明确节点：

```text
Walk Cycle Pose
├── Forward SequencePlayer -> Warp(MoveAngle - 0°)
├── Back SequencePlayer    -> Warp(MoveAngle - 180°)
├── Left SequencePlayer    -> Warp(MoveAngle + 90°)
└── Right SequencePlayer   -> Warp(MoveAngle - 90°)
                              -> BlendListByEnum(CycleDirection)
```

Standing 的 Walk/Run/Sprint 外层由 `BlendListByEnum(PoseGait)` 选择，Crouching 只暴露 Walk/Run；两套姿态再由 `BlendListByBool(bPoseCrouching)` 选择。锁定 Cycle 的四向 Sequence 在选择器前分别完成量化残差对齐，内部 `RotationInterpSpeed=0`，只保留 `DirectionBlendDuration` 的 Pose 混合，避免旧方向素材被新方向残差错误旋转。Start/Stop 仍使用锁存方向和选择器后的单节点对齐。所有 Cycle SequencePlayer 加入同一个 Sync Group，通过原生同步组保持相位；方向、步态或姿态切换后接 Inertialization。

Start、Stop 和 Step 使用 `LatchedActionDirection`；Jump 使用独立的八方向 `JumpDirection`。Start 只锁存基础四向动画，`StartDirectionResidualAngle` 持续根据最新输入相对该基础方向计算，因此起步中追加斜向输入会立即开始对齐且不会重启 Sequence；Stop 把四向残差锁存到独立的 `StopDirectionResidualAngle` 并保持到制动结束，明显残差随后进入 StopTurn，由 Turn 动画和 `StopTurnDirectionAlignment` 共同完成换脚回正。Jump Start/InAir/Land 锁存八向残差。Lua 资产表只在 `AnimGraph()` 与 `StateGraph_*()` 编译期读取，运行时 Graph 中保存的是实际资产引用。

### 7.1 双脚 Foot IK

最终 Locomotion Pose 在主 AnimGraph 末端转换到组件空间，依次经过 UE 原生 `FootPlacement` 与 `LegIK`：

- `FootPlacement` 使用 `Root` 下不蒙皮、单位旋转的 `IK_Foot_Plane` 作为参考骨骼，以其局部 Z 轴提供稳定向上法线；该骨骼不插入 `Master -> RootPos -> Pelvis` 或左右脚目标链，因此不会改变现有蒙皮和动画姿势。
- 节点直接读取 CharacterMovement 接地状态，以原生球扫检测双脚地面，计算脚底高度、坡面旋转和骨盆垂直补偿；Lua 不执行射线或骨骼求解。
- `PlantLockType` 由 Lua 配置为 `Unlocked`：保留地面检测、坡面旋转和骨盆求解，但不把脚固定在世界空间，避免 Stop、Land 和斜面姿势被旧脚目标拉扯。
- `LegIK` 消费调整后的 `L_Foot_Target/R_Foot_Target`，以两段腿链驱动左右 FK 脚骨骼。
- `FootPlacement` 与 `LegIK` 共用生成变量 `FootIKAlpha`：空中快速淡出为 0，落地后平滑恢复为 1，避免 JumpInAir/Land 仍以满权重拉扯双腿。
- 骨骼名、检测长度、骨盆最大偏移、权重切换速度、种植阈值和求解精度集中在 `Tuning.FootIK`，调整这些值无需重新编译 C++；当前骨盆最大垂直偏移限制为 20 cm。
- 骨盆水平重心补偿当前为 0，避免 IK 横向推移身体并放大第三人称摄像机抖动；上下坡高度补偿和脚底坡面旋转仍保持启用。

## 八、Transition 规则

Transition 由两部分组成：

```text
Lua Intent Rule AND Native Animation Gate
```

Lua 只回答业务意图：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Idle 进入 Start。
function GroundedMode.CanEnter_Idle_Start(Inst)
    return Inst.bHasMovementInput == true
        and Inst.bIsDodging ~= true
end
```

动画门控由 Lua 编译期声明原生规则节点：

| Transition | Lua Intent | Native Gate |
|------------|------------|-------------|
| Idle -> Start | 有移动输入 | 无，立即进入 |
| Start -> Cycle | 仍有移动输入 | `CanEnterLoop >= 0.5` |
| Start -> Stop | 输入释放 | `CanEnterStop >= 0.5` |
| Cycle -> Stop | 输入释放 | `CanEnterStop >= 0.5` |
| Stop -> Idle | 无输入 | `CanEnterIdle >= 0.5` |
| Stop -> Start | 重新输入 | 无 Gate，立即打断 Stop |
| Step -> Cycle/Idle | 是否仍有移动输入 | `CanExitStep >= 0.5` |
| Cycle 内 Sprint -> Run/Walk | `PoseGait` 改变 | 非 Transition；原生 BlendList 混合 |

推荐 DSL：

```lua
Machine:Transition("Start_Cycle", "Start", "Cycle", {
    BlendDuration = Tuning.CycleBlendDuration,
    Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterLoop, Tuning.CurveThreshold),
})

Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
    BlendDuration = Tuning.StopBlendDuration,
    Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterStop, Tuning.CurveThreshold),
})
```

完整 Rule AST 由 NodeFactory 生成原生 Transition Rule 节点，支持 BoolProperty、Curve、TimeRemaining、All/Any/Not。兼容旧模块仍可把 Lua Rule boolean 与附加 Gate 组合；正式 `ABP_Sekiro` 的状态切换不再进入 Lua Runtime。Lua 来源 AnimBlueprint 仍因每帧 `BlueprintUpdateAnimation` 关闭多线程 Update。

## 九、当前混合与相位边界

- Start -> Cycle：等待源 Sequence 的 `CanEnterLoop` 曲线窗口。
- Cycle 方向/步态/姿态变化：所有循环播放器加入 `SekiroLocomotion` Sync Group，输出后使用 Inertialization。
- Start/Cycle -> Stop：等待 `CanEnterStop` 曲线窗口。
- Stop -> StopTurn/Idle：等待 `CanEnterIdle`；明显残差进入 StopTurn，较小残差直接进入 Idle。
- StopTurn -> Idle：Turn 动画换脚期间由 `StopTurnDirectionAlignment` 撤销补偿，再等待 `CanExitTurn`。
- Step -> Cycle/Idle：等待 `CanExitStep` 曲线窗口。

`MovePhase` 与 `FootPlant` 已写入部分动画资产，但当前 Graph 没有正式运行时消费者。精确相位反查仍是后续增强，不能因此恢复运行时动态播放器或 Pose 快照架构。

## 十、Lua 文件结构

```text
Content/Script/Animation/Sekiro/
├── ABP_Sekiro.lua
├── AnimAssets.lua
├── Shared/
│   ├── Direction.lua
│   ├── PoseSelectors.lua
│   └── Tuning.lua
├── Layer/GroundLocomotion/
│   ├── Root.lua
│   ├── GroundedMode.lua
│   ├── Standing.lua
│   └── Crouching.lua
└── Layer/Airborne/
    └── Jump.lua
```

`ABP_Sekiro.lua` 只声明蓝图、根图、生成变量和运行时总入口。每个动画层目录提供 Graph 构建函数和对应规则方法；`AnimAssets.lua` 保持纯资源表。

## 十一、编译器能力演进记录

以下内容是实施前的历史能力清单。阶段 A-F 第一版目前均已完成：

1. **生成变量与 Lua Update Bridge**：IR Variable、Blueprint Member Variable、`BlueprintUpdateAnimation` Lua override。
2. **非 Pose 数据连接**：AnimInstance Property Getter、Bool/Float/Byte/Enum Pin 和 Link。
3. **选择与混合节点**：`BlendListByBool`、`BlendListByEnum`、Sync Group、Inertialization 请求。
4. **Transition Rule AST**：Bool Property、Curve Compare、Time Remaining、All/Any/Not；Lua bool 只保留兼容。
5. **状态生命周期**：进入状态时锁定 Action Direction/Gait，离开后清理一次性选择。
6. **调试映射**：稳定 State/Transition ID 映射到生成节点，PIE 显示 Lua Rule、Native Gate、BlendAlpha 和活跃 Sequence。
7. **后续层节点**：Save/Use Cached Pose 与 Orientation Warping 已接入；Slot、LayeredBoneBlend 仍待战斗层实现。

正式 `ABP_Sekiro.uasset` 已由 Lua 重新生成并接管角色原引用；上述清单保留用于说明能力演进，不再表示当前阻塞项。

## 十二、实施阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| A | Variable、Update Bridge、数据 Pin | Lua 可直接写生成属性，动画线程读到同帧快照 |
| B | BlendList、Sync Group、Transition Gate | 自动测试生成并编译带方向选择和时间/曲线门控的最小 ABP |
| C | Standing Walk/Run | 非锁定和锁定四向 Idle/Start/Cycle/Stop PIE 通过 |
| D | Crouching | 与 Standing 分文件维护 Pose，共享阶段拓扑；站蹲移动切换无强制 Stop |
| E | Step/Sprint | Step 曲线退出后直达 Cycle；Sprint 作为 Standing 步态分支直接与 Walk/Run 混合 |
| F | Jump | Start/InAir/Land 与 RootMotion/CharacterMovement 分工正确 |
| G | Slot 与上半身补偿 | 锁定 Cycle 上下身方向补偿已接入；战斗 Montage 与 Slot 仍待实现 |

每阶段都必须生成全新的测试资产，不覆盖旧 `ABP_Sekiro`；最后通过重建替换旧资产，彻底移除已删除 Host 节点的序列化残留。

## 十三、当前实现状态

截至 2026-07-18，阶段 A-F 的结构已经迁移到共享地面阶段状态机：

`/Game/Characters/Sekiro/Generated/ABP_Sekiro_LuaV2`（历史验证资产，正式迁移后已删除）

当前生成图包含 Root、统一 Grounded Phase 与 Jump 状态机。Standing/Crouching 是独立 Pose 构建模块，Sprint 是 Standing 的步态分支；Lua 更新入口、生成变量、方向/步态/姿态选择、Sync Group、Inertialization 和曲线 Transition Gate 均接入原生 AnimBlueprint 节点。

历史验证资产已经通过生成、编译和 PIE 运行时冒烟测试；正式 `/Game/Characters/Sekiro/ABP_Sekiro` 随后已由 Lua 源模式接管并保持原角色引用。当前仍需逐项验收 Start/Cycle/Stop 步态相位、锁定上半身朝向、Step/Sprint 转向、Jump RootMotion/空中物理分工，以及调试信息映射。

## 十四、Lua 源语言模式

最终方案不把 Lua 定位成“批量创建动画蓝图的工具”，而是把 Lua 文件本身作为动画蓝图源码。SequencePlayer、StateMachine、Transition、PoseLink 和 EventGraph 只是编译器根据 Lua IR 物化的中间结果；使用者不创建、不连接也不维护这些节点。

UE5.2 的 AnimBlueprint 编译器按 Blueprint 对象的精确类型注册。直接派生 `USekiroLuaAnimBlueprint : UAnimBlueprint` 会绕过引擎为 `UAnimBlueprint` 注册的原生编译器，因此固定资产仍使用标准 `UAnimBlueprint`。Lua 模块名、编译版本、`SourceRevision`、`SuccessfulSourceRevision`、`bSourceDirty` 和最近编译结果存放在专用 Blueprint Extension 中，既保留原生 `FAnimBlueprintCompilerContext`，也能识别该资产由 Lua 独占管理。

正式工作流为：

```text
固定 UAnimBlueprint 资产
    + Lua 编译扩展（模块名、版本、源码哈希）
    + Lua 源文件（唯一业务真相）
        -> CompileIR
        -> Validator / Preflight
        -> 原地重建编译器拥有的 Graph、变量和 EventGraph
        -> UE 原生 AnimBlueprint 编译器
        -> 同一 GeneratedClass 路径重新实例化
```

资产路径和角色引用保持稳定，不再生成 `V2/V3` 资产。编辑器将来显示的生成图只用于只读调试；任何手工修改都不会成为源码，并会在下次 Lua 编译时被覆盖。

正式资产 `/Game/Characters/Sekiro/ABP_Sekiro` 现采用与原生动画蓝图一致的显式编译工作流：

1. `DirectoryWatcher` 只监听 `Content/Script/Animation` 的 Lua 新增、修改和删除事件。
2. 文件事件不执行 HotReload 或 Graph 编译，只递增源修订、设置 `bSourceDirty`，并把 Blueprint 状态标为 `BS_Dirty`。
3. 打开 Lua 动画蓝图后，插件仅对该资产重绑定原工具栏 Compile 命令；按钮和 `F7` 都执行 `HotReload -> staging -> 原地 Graph 重建 -> UE 原生编译`。普通 AnimBlueprint 不受影响。
4. UE5.2 的全局 `OnBlueprintPreCompile` 在编译流水线第 IX 阶段才广播，变量骨架等准备已经发生，不能安全用于前置结构重建，因此采用编辑器命令接管而不是该晚期钩子。
5. `PreBeginPIE` 在创建 PlayWorld 前同步编译全部已加载 Dirty Lua AnimBlueprint。PIE 中发生的 Lua 变化仍只标 Dirty，`EndPIE` 不自动编译，等待下一次 Compile 或 PIE。
6. 编译成功令 `SuccessfulSourceRevision == SourceRevision` 并清除 Dirty；Lua Import、IR 预检、staging 或提交失败时保留上次成功的 Graph/GeneratedClass，同时保持 Dirty 供修复后重试。

旧 `ABP_Sekiro_LuaV2`、`ABP_Sekiro_Lua` 和 `ABP_Sekiro_V2_Standing` 只用于早期验证，正式资产迁移成功后均已删除。
