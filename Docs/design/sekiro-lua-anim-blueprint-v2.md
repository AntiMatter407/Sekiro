# ABP_Sekiro Lua AnimBlueprint V2 设计

> 状态：设计完成，等待按阶段实现  
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
2. 实现 `CanEnter_*` 意图规则，结果发布到线程安全缓存。

Lua 不再返回 Pose，不再用动画名字符串驱动播放器，不再维护假的 `FPoseLink`、状态时间或播放时间。

## 二、旧逻辑保留与废弃

保留的行为：

- Standing/Crouching 的 Idle、Start、Cycle、Stop 流程。
- 非锁定时角色朝移动方向，锁定时四向移动并朝向目标。
- Step 按下立即响应，长按后进入 Sprint。
- Sprint 使用独立 Start、Loop、Stop，不混入 Walk/Run。
- Jump 分 Start、InAir、Land；InAir 位移由 CharacterMovement 物理负责。
- `CanEnterStop`、`MovePhase`、`FootPlant` 等动画数据继续用于自然过渡。

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
    -> OutputPose
```

后续战斗层加入后：

```text
RootLocomotionSM
    -> Inertialization
    -> Locomotion Cache Pose
    -> UpperBody Slot / LayeredBoneBlend
    -> FullBody Slot
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
| Grounded | InAir | `self.bIsInAir == true` |
| InAir | Grounded | `self.bIsInAir ~= true` 且 Land 已完成 |

## 四、Grounded 状态层级

```text
GroundedModeSM
├── Standing   -> StandingLocomotionSM
├── Crouching  -> CrouchLocomotionSM
├── Step       -> Step Pose Selector
└── Sprint     -> SprintSM
```

优先级固定为：

```text
InAir > Step > Sprint > Stance Locomotion
```

这意味着：

- 蹲姿按 Step 时先切站姿，再播放站立 Step。
- 蹲姿长按 Step/Sprint 键时先播放 Step，满足长按阈值后进入 Sprint。
- Jump 可以从 Standing、Crouching、Step 或 Sprint 进入 InAir。

### 4.1 StandingLocomotionSM

```text
Entry -> Idle <-> Start -> Cycle -> Stop -> Idle
                    ^        |       |
                    +--------+-------+
```

状态职责：

| State | Pose | 选择锁定时机 |
|-------|------|----------------|
| Idle | `Idle`，可扩展 Idle Turn | 每帧可更新 Aim 参数 |
| Start | Walk/Run × 四方向 Start | 进入状态时锁定 Gait、Direction |
| Cycle | Walk/Run × 四方向 Loop | Direction/Gait 可更新并做同步混合 |
| Stop | Walk/Run × 四方向 Stop | 进入状态时锁定最后移动方向和 Gait |

非锁定模式下 `CycleDirection` 始终为 Forward；大角度起步通过 Left/Right Turn Start 资产过渡，Movement 在动画期间快速把角色朝向移动方向。

锁定模式下 `CycleDirection` 使用 Forward/Back/Left/Right 四向滞回选择。斜向输入保留精确 `MoveDirectionAngle`，四向动画只提供最接近的下半身姿势；后续 Orientation Warping 使用剩余角度补偿，上半身继续朝向目标。

### 4.2 CrouchLocomotionSM

Crouch 与 Standing 共享同一套状态拓扑和 Transition 构建函数，只替换资产集合：

```text
Crouch Idle -> Crouch Start -> Crouch Cycle -> Crouch Stop -> Crouch Idle
```

允许 Walk/Run，不允许 Sprint。站立与蹲姿切换发生时：

- Idle 使用 Stand/Crouch 转换 Sequence。
- 移动中使用短 Inertialization，不强制先 Stop。
- Step、Sprint、Jump 由外层状态机接管。

### 4.3 SprintSM

```text
Entry -> SprintStart -> SprintCycle -> SprintStop
                ^             |             |
                +-------------+-------------+
```

- SprintStart 根据屏幕输入锁定 Forward/BackTurn/LeftTurn/RightTurn 资产。
- SprintCycle 只播放 `Sprint_Forward_Loop`。
- Sprint 时 Movement 朝移动方向快速转向；锁定目标只影响摄像机，不让角色继续四向平移。
- 小于大转向阈值时保持 SprintCycle，由 Movement 连续转向。
- 大角度反向时进入 SprintStop，再选择新的 SprintStart。

### 4.4 Step

Step 使用独立四向 Sequence Selector：

- 锁定：按 `DodgeDirection` 和 `DodgeDirectionLateral` 选择 Forward/Back/Left/Right。
- 非锁定：Movement 朝输入方向转向，动画使用 Forward；无输入时默认 Forward。
- Direction 在 Step 进入时锁定，播放期间输入转向不切换 Sequence。
- Step 播放完成且按键仍超过 Sprint 阈值时进入 Sprint，否则回 Standing/Crouching。

## 五、JumpSM

```text
Entry -> JumpStart -> InAir -> Land -> Grounded
```

- 非锁定：角色先朝移动方向，使用 Forward Start/InAir/Land。
- 锁定：根据进入空中时锁定的八方向选择对应原始 Sequence。
- JumpStart 与 Land 使用动画 RootMotion。
- InAir Sequence 不提供位移，水平/垂直轨迹由 CharacterMovement 计算。
- Land 期间新移动输入可在取消窗口进入 Grounded Start，不能等待完整 Idle。

## 六、AnimInstance 变量

`USKAnimInstance` 已有字段继续作为事实来源，Lua 直接使用 `self.Speed`、`self.Gait`、`self.bIsLockedOn` 等字段。

新编译器需要支持在生成类中声明以下瞬态变量：

| Variable | 类型 | 用途 |
|----------|------|------|
| `CycleDirection` | Byte/Enum | 当前循环四方向，允许滞回更新 |
| `LatchedActionDirection` | Byte/Enum | Start/Stop/Step/Jump 进入时锁定方向 |
| `LatchedActionGait` | Byte/Enum | 一次性动作进入时锁定 Walk/Run |
| `DirectionResidualAngle` | Float | 精确方向减去四向素材主方向，用于 Warping |
| `bWasLockedOn` | Bool | 检测锁定模式切换 |

运行时统一入口：

```lua
---@param delta_seconds number
---@return nil
function ABP_Sekiro:BlueprintUpdateAnimation(delta_seconds)
    self:UpdateLocomotionDirection(delta_seconds)
    self:UpdateDirectionResidual()
end
```

这些变量必须是真实生成类属性。Lua 使用 `self.VariableName = value` 直接写入，不经过 `facts`、字符串键缓存或 `SetLuaXxx()` 包装函数。

## 七、Pose 选择节点

旧 `PlaySequenceByDirection()` 改为编译期明确节点：

```text
Walk Cycle Pose
├── Forward SequencePlayer
├── Back SequencePlayer
├── Left SequencePlayer
└── Right SequencePlayer
       -> BlendListByEnum(CycleDirection)
```

Walk/Run 外层再由 `BlendListByEnum(Gait)` 选择。所有 Cycle SequencePlayer 加入同一个 Sync Group，通过原生同步组保持相位；方向或步态切换后接 Inertialization。

Start、Stop、Step 和 Jump 使用 `LatchedActionDirection`，状态期间不更换一次性动画。Lua 资产表只在 `AnimGraph()` 与 `StateGraph_*()` 编译期读取，运行时 Graph 中保存的是实际资产引用。

## 八、Transition 规则

Transition 由两部分组成：

```text
Lua Intent Rule AND Native Animation Gate
```

Lua 只回答业务意图：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Idle 进入 Start。
function ABP_Sekiro.CanEnter_Standing_Idle_Start(Inst)
    return Inst.bHasMovementInput == true
        and Inst.bIsDodging ~= true
        and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end
```

动画门控由 Lua 编译期声明原生规则节点：

| Transition | Lua Intent | Native Gate |
|------------|------------|-------------|
| Idle -> Start | 有移动输入 | 无，立即进入 |
| Start -> Cycle | 仍有移动输入 | Sequence 剩余时间进入 BlendDuration |
| Start -> Stop | 输入释放 | `CanEnterStop >= 0.5`，异常时剩余时间兜底 |
| Cycle -> Stop | 输入释放 | `CanEnterStop >= 0.5` |
| Stop -> Idle | 无输入 | Sequence 剩余时间进入 BlendDuration |
| Stop -> Start | 重新输入 | Stop 取消窗口或 `CanEnterStop` |
| Step -> Grounded/Sprint | 动作意图 | Sequence 剩余时间进入 BlendDuration |

推荐 DSL：

```lua
machine:Transition("Start_Cycle", "Start", "Cycle", {
    RuleFunctionName = "CanEnter_Standing_Start_Cycle",
    BlendDuration = 0.12,
    Gate = Rule.TimeRemainingLessEqual(0.12),
})

machine:Transition("Cycle_Stop", "Cycle", "Stop", {
    RuleFunctionName = "CanEnter_Standing_Cycle_Stop",
    BlendDuration = 0.08,
    Gate = Rule.CurveGreaterEqual("CanEnterStop", 0.5),
})
```

Gate 由 NodeFactory 生成原生 Transition Rule 节点，不在动画线程执行 Lua。若 Gate 未声明，Transition 只读取 Lua bool 缓存。

## 九、MovePhase 与混合

- Start -> Cycle：源 Start 在淡出期间继续播放到末尾，目标 Cycle 根据 `MovePhase` 找到最接近的起播相位。
- Cycle 方向/步态变化：优先使用 UE Sync Group；资源缺少同步标记时用 `MovePhase` 生成同步标记或目标起播位置。
- Cycle -> Stop：只在 `CanEnterStop` 窗口切换，Stop 资产按当前 `FootPlant/MovePhase` 选择匹配起点。
- 普通 BlendAlpha 只负责 Pose 交叉混合，不代替步态相位同步。

第一阶段先实现 Sync Group + Inertialization；`MovePhase` 反查起播位置作为第二阶段增强，不能因此恢复运行时动态播放器。

## 十、Lua 文件结构

```text
Content/Script/Animation/Sekiro/
├── ABP_Sekiro.lua
├── AnimAssets.lua
├── Shared/
│   ├── Direction.lua
│   ├── LocomotionRules.lua
│   └── Tuning.lua
├── Layer/GroundLocomotion/
│   ├── Graph.lua
│   ├── Standing.lua
│   ├── Crouching.lua
│   ├── Sprint.lua
│   └── Step.lua
└── Layer/Airborne/
    └── Jump.lua
```

`ABP_Sekiro.lua` 只声明蓝图、根图、生成变量和运行时总入口。每个动画层目录提供 Graph 构建函数和对应规则方法；`AnimAssets.lua` 保持纯资源表。

## 十一、编译器前置任务

当前编译器还不能无损生成上述 Graph，必须依次补齐：

1. **生成变量与 Lua Update Bridge**：IR Variable、Blueprint Member Variable、`BlueprintUpdateAnimation` Lua override。
2. **非 Pose 数据连接**：AnimInstance Property Getter、Bool/Float/Byte/Enum Pin 和 Link。
3. **选择与混合节点**：`BlendListByBool`、`BlendListByEnum`、Sync Group、Inertialization 请求。
4. **Transition Gate AST**：Lua bool、Curve Compare、Time Remaining、All/Any/Not。
5. **状态生命周期**：进入状态时锁定 Action Direction/Gait，离开后清理一次性选择。
6. **调试映射**：稳定 State/Transition ID 映射到生成节点，PIE 显示 Lua Rule、Native Gate、BlendAlpha 和活跃 Sequence。
7. **后续层节点**：Save/Use Cached Pose、Slot、LayeredBoneBlend、Orientation Warping。

在 1-4 完成前，不生成正式 `ABP_Sekiro.uasset`，因为仅靠当前固定 SequencePlayer 和 bool Rule 会让 Start/Cycle/Stop 立即互切，无法复刻旧逻辑。

## 十二、实施阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| A | Variable、Update Bridge、数据 Pin | Lua 可直接写生成属性，动画线程读到同帧快照 |
| B | BlendList、Sync Group、Transition Gate | 自动测试生成并编译带方向选择和时间/曲线门控的最小 ABP |
| C | Standing Walk/Run | 非锁定和锁定四向 Idle/Start/Cycle/Stop PIE 通过 |
| D | Crouching | 复用拓扑，站蹲移动切换无强制 Stop |
| E | Step/Sprint | 按下 Step、长按 Sprint、锁定方向和大角度 Sprint 转向通过 |
| F | Jump | Start/InAir/Land 与 RootMotion/CharacterMovement 分工正确 |
| G | Slot 与上半身补偿 | 战斗 Montage 和锁定上半身朝向接入 |

每阶段都必须生成全新的测试资产，不覆盖旧 `ABP_Sekiro`；最后通过重建替换旧资产，彻底移除已删除 Host 节点的序列化残留。

## 十三、当前实现状态

截至 2026-07-15，阶段 A-F 的第一版结构已经生成到：

`/Game/Characters/Sekiro/Generated/ABP_Sekiro_LuaV2`（历史验证资产，正式迁移后已删除）

当前生成图已经包含 Root、Grounded、Standing、Crouching、Step、Sprint 与 Jump 的嵌套状态机，Lua 更新入口、生成变量、方向/步态选择、Sync Group、Inertialization，以及曲线/剩余时间 Transition Gate 均已接入原生 AnimBlueprint 节点。

该资产已经通过生成、编译和 PIE 运行时冒烟测试，但这只证明结构与数据链闭环，不代表动画观感已经与旧 `ABP_Sekiro` 完全一致。正式替换前仍需逐项验收 Start/Cycle/Stop 步态相位、锁定上半身朝向、Step/Sprint 转向、Jump RootMotion/空中物理分工，以及调试信息映射。旧 `ABP_Sekiro` 当前仍是角色默认 AnimClass。

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
