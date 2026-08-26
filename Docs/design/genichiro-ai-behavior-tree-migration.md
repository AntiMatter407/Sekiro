# 弦一郎 UE Lua AI 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [弦一郎 UE Lua AI 复现](../plan/genichiro-ai-behavior-tree-migration.md) | 🟢 设计完成 | 2026-08-21 | 2026-08-24 |

## 架构设计

### 设计结论

采用“Lua 双源定义、UE 原生运行”的架构：

- `BT_Genichiro.lua` 生成原生 Blackboard 与 BehaviorTree，负责感知后的战术意图、优先级、中止和导航；
- `ABP_Genichiro.lua` 生成原生 AnimGraph 与 StateMachine，负责移动姿态、战斗 Slot、动画状态和 Root Motion 策略；
- Lua 战斗 Profile、Task 和 CombatComponent 连接决策与表现，但不取代 UE 的行为树调度和动画图求值。

不采用以下两种极端方案：

- 不把 63 个原脚本函数逐分支展开成巨型 BehaviorTree；
- 不把反编译 Lua 原样放入运行时并模拟 FromSoftware AI 虚拟机；
- 不把感知、导航、选招、中断和动画播放全部塞进一个 Lua Task；
- 不让 AnimBlueprint 决策招式，也不让 BehaviorTree 逐帧决定动画 Pose。

最终 `BT_Genichiro` 与 `ABP_Genichiro` 都是 UE 原生资产。高层反应、阶段、战术、移动和动作分支在
BehaviorTree 编辑器中可见；Idle、锁定移动、转身、全身战斗 Slot 和 Pose 合成在 AnimBlueprint 编辑器中可见。
原版脚本只提供“弦一郎应该怎么打”的证据，UE 架构决定“系统如何组织”。通用 C++/插件代码不引用弦一郎路径和数字 ID。

### 设计依据

基础版 `710000` 的核心结构是：

```text
Logic.Main / Logic.Interrupt
  → Goal.Activate 建立权重池
  → Common_Battle_Activate 选择 ActXX
  → ActXX 提交一个或多个 SubGoal
  → Goal.Interrupt / Parry / Damaged 随时清除 SubGoal
  → Kengeki_Activate 在拼刀后建立另一套权重池
```

这本质上是事件驱动的 Utility Selector，而不是每帧运行的传统状态机。迁移时保留其
“反应分支优先、普通决策低频执行、动作提交后锁定”的战斗节奏，但把 SubGoal 重新解释为 UE 的
战术意图、Blackboard 状态、原生导航节点和离散战斗动作，不复制原始 Goal 调度器。

现有 `SekiroLuaBehaviorTreeExt` 已支持：

- Lua DSL → 强类型 IR；
- 原生 Blackboard 和 BehaviorTree Graph 生成；
- 原生 Composite、Decorator、Service、Task 反射属性写入；
- 显式 Lua Task 宿主；
- 失败预检与不破坏已有资产的生成流程。

现有 `SekiroAnimBlueprintExt` 已支持 Lua DSL → 原生 AnimBlueprint Graph、状态机、反射变量、
Check/Generate 和资产内源模块元数据。现有 `ABP_SKAICharacter.lua` 展示了 AI 导航参数更新方式，但其目标是
`Sekiro_Skeleton` 且固定忽略 Root Motion；弦一郎拥有独立 `Genichiro_Skeleton`，因此必须建立专用 Lua 动画蓝图。

现有 `USKCombatComponent` 通过 `CombatFullBodySlot` 播放动态 Montage，并以 `ActionSerial`、动作状态和
动画结束回调管理生命周期；当前 Lua `RequestAIAttack` 只接受 `AutoLight`，弦一郎不能假设它支持任意命名动作。
第一里程碑优先由专用 Task 读取 ActionCatalog，再调用组件已有的通用 Begin/Play/Invalidate 接口提交动作；
只有能力审计证明生命周期无法闭环时，才增加通用接口，不把弦一郎动作表写入 `SKCombatComponent.lua`。

### 总体架构

```text
原版 Lua / TAE / 动画数据（离线参考与校准）
                 │
                 ▼
弦一郎 UE 语义层
  ├─ TacticalProfile       距离、阶段、冷却、权重与行为族
  ├─ ActionCatalog         语义战斗动作、连段和动画映射
  ├─ LegacySignals         原始 ID 溯源与确认度
  └─ CombatMemory          短期记忆与冷却
                 │
        ┌────────┴────────┐
        ▼                 ▼
BT_Genichiro.lua      ABP_Genichiro.lua
  │ Lua DSL             │ Lua Anim DSL
  ▼                     ▼
UE BT + Blackboard    UE AnimGraph + StateMachine
  │                     ▲
  ├─ AIController       │
  ├─ UE MoveTo/Nav      │
  ├─ Decorator/Abort    │
  └─ Lua Tasks ──► USKCombatComponent
                       │ 动态 Montage / ActionSerial / 事件
                       └────────► CombatFullBodySlot
```

### 模块边界

#### `Plugins/SekiroLuaBehaviorTreeExt`

仅保留通用能力：

- Lua BehaviorTree Task 宿主；
- Task 生命周期、Abort、Tick、Lua 模块加载和错误诊断；
- 不认识弦一郎、不读取 `Source/Sekiro/`，不包含动作编号和资产路径。

现有 `USekiroLuaBehaviorTreeTask` 已提供 Execute、Tick、Abort、AIController、Pawn 和 Blackboard
上下文，第一里程碑不需要新增插件节点类型。

#### `Plugins/SekiroAnimBlueprintExt`

仅使用现有通用能力：

- Lua AnimBlueprint DSL、强类型 IR、状态机与原生 Graph 生成；
- Check、原地 Generate、编译状态和源模块元数据；
- 不添加弦一郎骨架、动画路径、状态名或 Root Motion 业务规则。

第一里程碑默认不修改插件；若弦一郎图所需节点不在现有 NodeContracts 中，先验证能否用现有 UE 节点组合表达，
确认属于通用缺口后才进入独立插件任务。

#### `Source/Sekiro/`

只提供游戏通用接口：

- 战斗动作状态查询；
- 请求命名战斗动作；
- 消费或订阅通用 AI 战斗事件；
- 距离、朝向、架势、忍杀和目标状态查询；
- 导航空间查询优先复用引擎能力。

建议的通用事件语义：

```text
AttackThreat
GuardResult
DeflectResult
DamageReceived
ProjectileImpact
TargetUseItem
PostureBroken
PhaseChanged
ActionFinished
```

C++ 不保存 `200200`、`3000` 等弦一郎原始编号；编号映射位于 Lua 数据层。

#### `Content/Script/AI/Genichiro/`

负责全部弦一郎业务：

```text
BT_Genichiro.lua
GenichiroLegacySignals.lua
GenichiroActionCatalog.lua
GenichiroTacticalProfile.lua
GenichiroCombatMemory.lua
```

运行时节点统一放在 `Content/Script/AI/Tasks/`。`UpdateContext` 和 `SelectIntent` 只在决策边界执行；
`BuildMoveGoal` 只计算 Blackboard 位置，让 UE `MoveTo` 执行导航；`ExecuteCombatAction` 只提交并等待离散战斗动作。

#### `Content/Script/Animation/Genichiro/`

```text
ABP_Genichiro.lua
GenichiroAnimAssets.lua
Shared/GenichiroAnimTuning.lua
StateMachines/GenichiroLocomotion.lua
```

该目录是 `ABP_Genichiro` 的 Lua 源。动画资产表集中引用弦一郎序列；状态机只处理 Pose 和表现状态，
不依赖原版 `ActXX` 权重，也不访问 BehaviorTree 节点实例。

## 涉及文件

| 文件 | 操作 | 任务 | 说明 |
|------|------|------|------|
| `Content/Script/AI/Genichiro/BT_Genichiro.lua` | 修改 | 8.1 | 建立反应、阶段、战术、移动和动作分层的原生行为树拓扑 |
| `Content/Script/AI/Genichiro/GenichiroLegacySignals.lua` | 新增 | 5.1 | 集中保存原始 SpEffect、Interrupt 和确认度 |
| `Content/Script/AI/Genichiro/GenichiroActionCatalog.lua` | 新增 | 5.2 | 声明 UE 语义动作、连段动画和原脚本追溯信息 |
| `Content/Script/AI/Genichiro/GenichiroTacticalProfile.lua` | 新增 | 5.3 | 将原版权重校准为战术意图、条件、冷却和阶段修正 |
| `Content/Script/AI/Genichiro/GenichiroCombatMemory.lua` | 新增 | 5.4 | 保存短期状态和冷却截止时间 |
| `Content/Script/AI/Tasks/SKGenichiroUpdateContext.lua` | 新增 | 6.1 | 在决策边界采集距离、阶段、空间和战斗状态并写 Blackboard |
| `Content/Script/AI/Tasks/SKGenichiroSelectIntent.lua` | 新增 | 6.1 | 建立候选池并输出 `TacticalIntent` 与 `SelectedActionId` |
| `Content/Script/AI/Tasks/SKGenichiroBuildMoveGoal.lua` | 新增 | 6.4 | 计算横移、后撤等导航目标，实际寻路仍由 UE `MoveTo` 执行 |
| `Content/Script/AI/Tasks/SKGenichiroExecuteCombatAction.lua` | 新增 | 6.2 | 提交并等待离散战斗动作，不接管普通导航 |
| `Content/Script/AI/Tasks/SKGenichiroReactionRouter.lua` | 新增 | 6.3 | 处理拼刀、弹刀、受击、射击和道具事件 |
| `Content/Script/Animation/Genichiro/ABP_Genichiro.lua` | 新增 | 7.2、7.3 | 声明弦一郎原生 AnimGraph、状态机、战斗 Slot 与运行时表现变量 |
| `Content/Script/Animation/Genichiro/GenichiroAnimAssets.lua` | 新增 | 7.1 | 集中声明弦一郎 Idle、移动、转身和调试动画名 |
| `Content/Script/Animation/Genichiro/Shared/GenichiroAnimTuning.lua` | 新增 | 7.2、7.3 | 集中声明速度阈值、Slot 名和 Root Motion 策略 |
| `Content/Script/Animation/Genichiro/StateMachines/GenichiroLocomotion.lua` | 新增 | 7.2 | 声明 Idle、锁定方向移动和转身状态机 |
| `Content/Script/Gameplay/Sekiro/Combat/SKCombatComponent.lua` | 条件修改 | 6.3、6.5 | 仅在通用生产者需要脚本发布时调用事件接口，不登记弦一郎动作 |
| `Source/Sekiro/Combat/SKCombatTypes.h` | 修改 | 4.4 | 新增通用 AI 战斗事件类型与有效负载 |
| `Source/Sekiro/Combat/SKCombatComponent.h/.cpp` | 修改 | 4.4 | 新增通用有界事件队列、Owner 伤害生产者与生命周期清理 |
| `Source/Sekiro/Weapon/SKWeapon.cpp` | 修改 | 4.4 | 在已有接触裁决完成后向攻守双方发布通用 WeaponContact |
| `Plugins/SekiroLuaBehaviorTreeExt/*` | 默认不修改 | — | 现有 Lua Task 生命周期已覆盖第一里程碑 |
| `Plugins/SekiroAnimBlueprintExt/*` | 默认不修改 | — | 现有 Lua AnimBlueprint 生成链优先复用，通用节点缺口另行立项 |
| `/Game/Characters/Genichiro/ABP_Genichiro` | 原地生成/更新 | 7.4 | UE 原生弦一郎 AnimBlueprint 资产 |
| `/Game/Characters/Genichiro/AI/BB_Genichiro` | 生成/更新 | 8.2、8.3 | 跨 AI/战斗/调试的 Blackboard 契约 |
| `/Game/Characters/Genichiro/AI/BT_Genichiro` | 生成/更新 | 8.1、8.3 | UE 原生行为树资产 |

所有项目 Lua 修改前必须读取并遵守 `Docs/lua-code-style.md`。条件 C++ 修改开始前必须读取
`.codex/rules/cpp-workflow.md`，并由任务 4.3 的能力缺口提供明确依据。

## API 设计

### 复用的现有 C++/UnLua 接口

```cpp
bool USKCombatComponent::RequestAIAttack(FName AttackRequest);
int32 USKCombatComponent::BeginCombatAction(ESKCombatActionState NewState);
void USKCombatComponent::SetCombatActionState(ESKCombatActionState NewState);
void USKCombatComponent::InvalidateCombatAction(int32 ExpectedActionSerial);
bool USKCombatComponent::PlayCombatAnimationByPath(
    const FString& AnimationPath,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount);
ESKCombatActionState USKCombatComponent::GetCombatActionState() const;
int32 USKCombatComponent::GetActionSerial() const;
bool USKCombatComponent::IsActionSerialValid(int32 ExpectedActionSerial) const;
bool USKCombatComponent::IsCombatAnimationPlaying() const;
void USKCombatComponent::StopOwnerAIMovement();
```

`RequestAIAttack` 保留给现有通用 `AutoLight` 流程；弦一郎 Task 默认使用 ActionCatalog +
`BeginCombatAction` + `PlayCombatAnimationByPath`，失败时调用 `InvalidateCombatAction` 并恢复 Neutral。
`OnCombatAnimationEnded(ActionSerial, bInterrupted)`、`OnPostureChanged` 和
`OnPostureBrokenChanged` 用于能力缺口审计。若 Lua Task 通过轮询 ActionSerial 和动作状态即可完成
生命周期，不额外增加 C++ API。

### Lua 战术数据模块接口

```lua
---@return GenichiroActionDefinition|nil action
function GenichiroActionCatalog.GetAction(action_id)

---@return GenichiroCombatMemoryState memory
function GenichiroCombatMemory.GetOrCreate(pawn)

---@return nil result
function GenichiroCombatMemory.Reset(pawn)

---@return GenichiroDecisionResult result
function GenichiroTacticalProfile.SelectIntent(snapshot, memory, random_source)

---@return GenichiroDecisionResult result
function GenichiroTacticalProfile.SelectReaction(snapshot, memory, signal, random_source)
```

`SelectIntent` 返回 UE 语义结果，例如 `Approach`、`Reposition`、`PressureAttack`、`RangedPunish`、
`DefensiveHold`，并可附带 `SelectedActionId`。原始 `ActXX` 只保存在追溯字段，不作为 BehaviorTree 的公开协议。

### Lua Task 接口

所有运行时 Lua Task 继续使用 `USekiroLuaBehaviorTreeTask` 既有协议：

```lua
function Task.Execute(task, controller, pawn, blackboard, configuration)
function Task.Tick(task, controller, pawn, blackboard, configuration, delta_seconds)
function Task.Abort(task, controller, pawn, blackboard, configuration)
```

- `SKGenichiroUpdateContext`、`SKGenichiroSelectIntent`、`SKGenichiroBuildMoveGoal` 必须同步返回；
- `SKGenichiroExecuteCombatAction` 和 `SKGenichiroReactionRouter` 可以返回 `InProgress`；
- 所有 Task 在成功、失败和 Abort 路径都必须清理弱键实例状态。

### Lua AnimBlueprint 契约

```lua
local ABP_Genichiro = LuaAnimBlueprint:Extend("ABP_Genichiro", {
    SourceModule = "Animation.Genichiro.ABP_Genichiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Genichiro/Genichiro_Skeleton.Genichiro_Skeleton",
})

function ABP_Genichiro:DeclareVariables()
end

function ABP_Genichiro:AnimGraph(graph)
end

function ABP_Genichiro.BlueprintUpdateAnimation(inst, delta_seconds)
end
```

运行时更新只读取速度、水平移动方向、锁定状态、是否正在播放战斗动作和动作状态；所有 Pose、状态机、
SequencePlayer、Blend 与 Slot 都在编译期声明为 UE 原生节点。`CombatFullBodySlot` 必须与
`USKCombatComponent::CombatSlotName` 一致。

### 行为树与动画蓝图协作协议

| 所有者 | 写入 | 消费 | 约束 |
|--------|------|------|------|
| AIController | `TargetActor` | BehaviorTree、Focus | 只负责感知与控制器生命周期 |
| Lua BehaviorTree Task | `TacticalIntent`、`SelectedActionId`、`MoveGoal` | UE BT 分支、战斗 Task、调试 | 不直接选择 AnimSequence |
| `USKCombatComponent` | ActionState、ActionSerial、Montage 生命周期 | BehaviorTree Task、AnimInstance | 是战斗动作合法性和结束状态唯一权威 |
| Lua AnimBlueprint | 表现变量 | 原生 AnimGraph | 不回写战术意图，不启动 BehaviorTree |
| 动画 Curve/Notify | 攻击框、取消窗口、阶段事件 | CombatComponent | 不直接跳转 BehaviorTree 分支，通过战斗事件/状态传播 |

### 任务 4.4 新增的通用战斗事件接口

任务 4.3 已确认现有状态、序列号和委托无法无损表达高优先级瞬时反应，因此新增：

```cpp
UENUM(BlueprintType)
enum class ESKAICombatEventType : uint8;

USTRUCT(BlueprintType)
struct FSKAICombatEvent;

UFUNCTION(BlueprintCallable, Category = "Combat|AI")
bool PublishAICombatEvent(const FSKAICombatEvent& Event);

UFUNCTION(BlueprintCallable, Category = "Combat|AI")
bool ConsumeAICombatEvent(FSKAICombatEvent& OutEvent);

UFUNCTION(BlueprintCallable, Category = "Combat|AI")
void ClearAICombatEvents();

UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|AI")
int32 GetPendingAICombatEventCount() const;
```

事件结构只承载通用类型、来源 Actor、动作序列号、时间和可选标签，不包含弦一郎 Legacy ID。

## 行为树结构

```text
Root Selector: GenichiroBossBehavior
├─ Sequence: NoValidTarget
│  ├─ Decorator: TargetActor Is Not Set, Abort Both
│  └─ Wait: HoldArena
└─ Sequence: EngageTarget
   ├─ Decorator: TargetActor Is Set, Abort Both
   ├─ Service: DefaultFocus(TargetActor)
   ├─ LuaTask: UpdateCombatContext
   └─ Selector: CombatPriority
      ├─ Sequence: CriticalReaction
      │  ├─ Decorator: ReactionPriority >= Critical, Abort LowerPriority
      │  └─ LuaTask: ExecuteCriticalReaction
      ├─ Sequence: CombatReaction
      │  ├─ Decorator: ReactionType Is Set, Abort LowerPriority
      │  └─ LuaTask: SKGenichiroReactionRouter
      ├─ Sequence: PhaseTransition
      │  ├─ Decorator: bPhaseTransitionPending
      │  └─ LuaTask: ExecutePhaseTransition
      └─ Sequence: TacticalLoop
         ├─ LuaTask: SelectTacticalIntent
         └─ Selector: ExecuteIntent
            ├─ Sequence: CombatAction
            │  ├─ Decorator: TacticalIntent == CombatAction
            │  └─ LuaTask: ExecuteCombatAction
            ├─ Sequence: Reposition
            │  ├─ Decorator: TacticalIntent == Reposition
            │  ├─ LuaTask: BuildMoveGoal
            │  └─ UE Task: MoveTo(MoveGoal)
            ├─ Sequence: Approach
            │  ├─ Decorator: TacticalIntent == Approach
            │  └─ UE Task: MoveTo(TargetActor)
            └─ UE Task: Wait(ReassessDelay)
```

设计要点：

- `ReactionRouter` 没有待处理事件时返回 `Failed`，Selector 继续普通决策；
- 有反应时占据最高优先级，直到反应动作完成；
- `UpdateCombatContext` 和 `SelectTacticalIntent` 只在决策边界执行并立即完成；
- 接近和位置移动优先使用 UE 原生 `MoveTo`，Lua 只计算无法直接表达的 `MoveGoal`；
- `ExecuteCombatAction` 只在已选中离散战斗动作期间保持 `InProgress`，不接管普通导航；
- `TargetActor` Decorator 使用 `Both` Abort，目标变化立即终止当前链；
- 反应 Decorator 使用低优先级 Abort，使受击、拼刀、架势崩坏等事件能抢占战术分支；
- Task Abort 只释放调度状态，是否可打断动态 Montage 由战斗组件取消窗口决定。

## Blackboard 设计

Blackboard 只保存跨节点可观察状态：

| Key | 类型 | 写入方 | 用途 |
|-----|------|--------|------|
| `TargetActor` | Object/Pawn | AIController | 当前战斗目标 |
| `BossPhase` | Int | 阶段系统/Task | 当前语义阶段 |
| `TacticalIntent` | Enum/Name | 选择 Task | Approach/Reposition/CombatAction/Hold 等 UE 战术意图 |
| `SelectedActionId` | Name | 选择 Task | 当前语义战斗动作；非动作意图时为空 |
| `MoveGoal` | Vector | MoveGoal Task | 横移或后撤的导航目标 |
| `ReactionType` | Enum/Name | 反应路由 | 当前高优先级反应 |
| `ReactionPriority` | Int | 战斗事件同步 | 行为树 Abort 和反应分层依据 |
| `DistanceBand` | Enum/Name | 选择 Task | Close/Mid/Far/VeryFar |
| `bActionLocked` | Bool | 战斗 Task | 是否正在执行离散战斗动作 |
| `ActionSerial` | Int | 战斗 Task | 与 CombatComponent 生命周期对齐 |
| `bPhaseTransitionPending` | Bool | 阶段系统 | 阶段分支触发条件 |
| `DebugDecisionSerial` | Int | 选择 Task | 调试用决策序号 |

以下内容不进入 Blackboard：

- 每个动画的冷却；
- 原始 `Number(0..10)` 数组；
- 候选权重临时表；
- Task 的逐帧局部状态；
- 原始特殊效果全集。
- `SelectedLegacyAct`；该值保存在动作目录和决策日志中，不作为节点间控制协议。

这些数据由 `GenichiroCombatMemory` 与 Task 实例状态保存，避免黑板膨胀。

## 数据模型

### 原始信号映射

`GenichiroLegacySignals.lua` 集中声明：

```lua
local signal = GenichiroLegacySignals.GetByLegacyID(200200)
-- signal.SemanticEvent == "LegacyKengeki200200"
-- signal.Subject == "Self"
-- signal.Edge == "SharedResult"
-- signal.Confidence == "A/C"

local use_item = GenichiroLegacySignals.GetByName("Interupt_Use_Item")
-- use_item.SemanticEvent == "TargetUseItem"
```

模块分别以 `SignalByLegacyID` 和 `NamedSignalByName` 保存 40 个数值信号与 5 个符号化输入；每项固定包含
`LegacyID`、`Subject`、`Edge`、`SemanticEvent`、`Confidence`、`InputKind`、`DrivesBehavior`、
`SourceLocations` 和 `Notes`。`CanDriveBehavior()` 对 C 级与纯适配器条目失败关闭，避免未知语义进入动作选择。
映射仍保留 `Legacy` 中性命名和来源位置，不用推测名称覆盖原 ID。

### 动作目录

动作目录使用稳定的 UE 语义 ID，并保留原始追溯字段。普通导航不属于动作步骤：

```lua
{
    Id = "SwordPressureComboA",
    BehaviorFamily = "PressureAttack",
    LegacyKind = "Act",
    LegacyIndex = 1,
    SourceScript = "710000_battle",
    CooldownKey = "Anim3000",
    CooldownSeconds = 15.0,
    RequiredRangeCm = { Min = 0.0, Max = 350.0 },
    Combo = {
        { Action = "SwordSlashA", AnimationId = 3000 },
        { Action = "SwordSlashB", AnimationId = 3001 },
        { Action = "SwordSlashC", AnimationId = 3002 },
        { Action = "SwordComboFinisher", AnimationId = 3003 },
    },
}
```

行为目录分为两层：

| 层 | 示例 | UE 执行方式 |
|----|------|-------------|
| 战术意图 | `Approach`、`Reposition`、`PressureAttack`、`RangedPunish` | BehaviorTree 分支、Blackboard、UE `MoveTo` |
| 战斗动作 | 刀连段、弓箭、突刺、后撤斩、拼刀反击 | CombatComponent 动态 Montage 与 ActionSerial |

连段只允许包含连续战斗动作、记忆更新和明确的链间窗口，不包含寻路、感知或阶段循环。动画软路径集中在
弦一郎动作配置中，BehaviorTree 只传递语义 Action ID；CombatComponent 负责加载、播放和验证动态 Montage。

### 决策规则

战术规则分为三个阶段：

1. `Eligibility`：阶段、距离、角度和目标状态等硬条件；
2. `BaseWeight`：原脚本对应距离区间的基础权重；
3. `Modifiers`：冷却、SP、历史状态、空间和特殊信号修正。

示例：

```lua
{
    Intent = "Reposition",
    MoveStyle = "Strafe",
    LegacyAct = 23,
    Range = { MinCm = 0.0, MaxCm = 700.0 },
    BaseWeightByBand = {
        Close = 10.0,
        Mid = 15.0,
        Far = 100.0,
    },
    RequiredSpace = "EitherSide",
}
```

## 选择算法

```text
InputSnapshot = CaptureDecisionSnapshot()
Candidates = Profile.IntentsForPhase(InputSnapshot.Phase)

for Intent in Candidates:
    if not Eligibility(Intent, InputSnapshot):
        reject Intent with reason
    else:
        Weight = BaseWeight(Intent, DistanceBand)
        Weight = ApplyTargetModifiers(Weight)
        Weight = ApplyPhaseModifiers(Weight)
        Weight = ApplyMemoryModifiers(Weight)
        Weight = ApplyCooldown(Weight)
        Weight = ApplySpaceFilter(Weight)
        if Weight > 0:
            add Intent to WeightedPool

if WeightedPool is empty:
    return HoldOrApproachFallback

Selected = WeightedRandom(WeightedPool, SeededRandom)
RecordDecision(Selected, Snapshot, EffectiveWeights)
return Selected
```

### 距离换算

原脚本阈值为 `3/5/7`。第一版使用：

```text
Close:    <= 300 cm
Mid:      300～500 cm
Far:      500～700 cm
VeryFar:  >= 700 cm
```

换算比例保存在 Profile 中，不散落到 Task。最终值需结合角色胶囊、根运动和原地图实测校准。

### 原版基础权重

| 距离 | 候选 |
|------|------|
| `>=7` | Act10=300，Act15=600 |
| `5～7` | Act10=300，Act34=100，Act23=100，条件满足时 Act09=300 |
| `3～5` | Act01=5，Act02=10，Act06=30，Act11=15，Act23=15，条件满足时 Act09=300 |
| `<=3` | Act03=15，Act11=15，Act23=10，Act31=30，计时允许时 Act24=30 |

这些数字是校准输入而不是最终树结构。实施时先验证它们在相同快照下产生相近的行为分布，再根据 UE 导航半径、
动画位移和竞技场空间调整语义意图权重；不得为了数字一致而保留不适合 UE 的 SubGoal 组织方式。

### 冷却

冷却使用世界时间截止值：

```text
CooldownEnd[actionKey] = CurrentTime + CooldownSeconds
Available = CurrentTime >= CooldownEnd[actionKey]
```

避免为每个动作创建 UE Timer。阶段切换、UnPossess 和角色销毁时清理记忆。

## 战斗动作执行器

`SKGenichiroExecuteCombatAction` 只执行已经被 BehaviorTree 选中的离散战斗动作或紧密连段：

```text
Execute
  → 读取 SelectedActionId
  → 检查 CombatComponent 当前是否允许提交
  → 停止残留导航并请求语义战斗动作
  → 锁存返回的 ActionSerial

Tick
  → 检查目标和高优先级战斗事件
  → 通过 ActionSerial、ActionState 和动画结束状态确认生命周期
  → 若动作配置声明合法连段窗口，则提交下一段
  → 动作完成后记录冷却并 Succeeded

Abort
  → 释放 Task 状态
  → 只请求战斗组件按取消窗口终止，不直接强停任意 Montage
```

接近、横移和后撤不进入该执行器。它们由 `BuildMoveGoal` + UE `MoveTo` 执行，确保路径跟随、中止和目标观察
仍使用 UE AI 系统。Task 状态以弱键表按实例保存，必须在 `Succeeded`、`Failed` 和 `Abort` 三条路径清理。

## Lua 动画蓝图结构

### AnimGraph

```text
GenichiroLocomotion StateMachine
  ├─ Idle
  ├─ LockedMoveForward / Back / Left / Right
  └─ TurnInPlaceLeft / Right
          │
          ▼
CombatFullBodySlot
          │
          ▼
ComponentToLocal / OutputPose
```

第一里程碑保持图小而明确：基础姿势由导航速度和面向目标方向驱动；所有攻击、受击、弹刀、架势崩坏和阶段动作
通过 `CombatFullBodySlot` 覆盖基础姿势。后续只有在弓箭上半身动作确实需要边移动边播放时，才增加分层骨骼混合，
不预先复制只狼五层动画架构。

### 运行时变量

| 变量 | 来源 | 用途 |
|------|------|------|
| `Speed` / `bIsMoving` | `USKAnimInstance` | Idle 与移动切换 |
| `MovementDirection` | 速度相对角色/目标朝向 | 前后左右锁定移动选择 |
| `bHasMovementIntent` | AI 导航状态 | 避免速度临界帧错误回 Idle |
| `bCombatActionActive` | CombatComponent 动画状态 | 调试和 Root Motion 策略 |
| `CombatActionState` | CombatComponent | 受击、防御、攻击等表现分类 |
| `bTurnInPlaceRequested` | 控制器焦点与角度 | 原地转向状态 |

能直接复用 `USKAnimInstance` 的字段则不重复生成变量；缺失字段优先在 Lua 更新函数中由通用查询计算。

### Root Motion 所有权

```text
普通接近/横移/后撤：CharacterMovement + PathFollowing 拥有位移
战斗动作动态 Montage：CombatComponent + 动画取消窗口拥有动作生命周期
带有效 Root Motion 的已提交动作：动画拥有动作位移，AI 导航必须停止
动作结束/中断：CombatComponent 收敛状态，BehaviorTree 才能重新提交导航
```

任务 4.5 的编辑器只读审计结果如下：

- 56 个白名单动画全部绑定 `Genichiro_Skeleton`，且全部设置
  `EnableRootMotion=true`、`RootMotionRootLock=AnimFirstFrame`、`ForceRootLock=false`；
- 现有 `ABP_Genichiro` 的 ParentClass 为 `USKAnimInstance`、TargetSkeleton 正确，CDO 的
  `RootMotionMode=RootMotionFromMontagesOnly`，该枚举值作为弦一郎第一阶段默认策略；
- Lua AnimBlueprint 编译器已具备原生 StateMachine、SequencePlayer、Slot、Skeleton 兼容性预检和只读 Check，
  无需增加弦一郎专用编译器能力；
- `Genichiro_Skeleton` 当前只有 `DefaultSlot`，现有占位 ABP 也使用 `DefaultSlot`，因此任务 7.3 必须先给骨架增加
  `CombatFullBodySlot`，再由 Lua 图生成同名 Slot；
- `EnableRootMotion` 只说明资产允许抽取 Root Motion，不证明每个动作一定有非零根位移。第一阶段仍按“单一位移所有者”
  执行：普通导航使用 CharacterMovement/PathFollowing；提交战斗 Montage 前停止导航，由 Montage 消费有效 Root Motion；
  每个动作的实际根轨迹和中断后恢复在 9.4 静态契约检查及用户授权后的 11.3 PIE 验收中确认。

现有 `ABP_Genichiro` 只是手工占位资产：只有一个使用 `3000` 战斗动作的 Idle 状态，没有 Lua 源扩展，不能作为
基础移动图继续堆叠。任务 7.4 必须保留资产路径并原地配置 `Animation.Genichiro.ABP_Genichiro` 后重建图。

## 中断与反应模型

### 事件优先级

```text
Death / FinalDeathblow
  > PostureBroken / PhaseTransition
  > ForcedHitReaction
  > Parry / Kengeki
  > ProjectileImpact / DamageReceived
  > TargetUseItem
  > NormalDecision
```

优先级由 `ReactionRouter` 集中维护，不允许各动作 Task 私自定义相互冲突的优先级。

### 原始 Interrupt 映射

| 原脚本入口 | 语义 | UE处理 |
|------------|------|--------|
| `INTERUPT_ParryTiming` | 玩家攻击进入防御响应窗口 | `GuardResult/AttackThreat` 事件 |
| `INTERUPT_ShootImpact` | 远程命中 | `ProjectileImpact` 事件 |
| `5029` | 受击反应 | `DamageReceived` 事件 |
| `3710020` | 清空连续拼刀计数 | `DeflectChainReset` 事件 |
| `3710030 + 3710032` | 提交 `3092` 强制承受反应并写入 `T6=50s` | `ForcedReaction3092` 事件 |
| `5031` | 满足距离和忍杀次数条件时提交 `3017` | `ConditionalReaction3017` Legacy 事件 |
| `3710050` | 目标特殊动作响应；按阶段选择 `3023` 或横移 | `TargetSpecialAction` Legacy 事件 |
| `110620` | 强制重规划 | 清理当前调度并重新决策 |
| `Interupt_Use_Item` | 玩家使用道具 | `TargetUseItem` 事件 |

本表是运行时优先级摘要；完整 ID、边沿、确认度和保留策略以任务 3.3 清单为准。

### 动画中断约束

BehaviorTree Abort 不等于强制停止动画：

- 导航和待执行步骤可以立即取消；
- 已开始战斗动作是否中断由 `USKCombatComponent` 的取消窗口决定；
- 不可取消阶段收到反应时，记录 PendingReaction，在允许窗口到达后处理；
- 架势崩坏、死亡等最高优先级事件可以通过战斗组件的强制反应接口覆盖。

## 拼刀系统

`ReactionRouter` 读取语义化拼刀结果，再由 `GenichiroTacticalProfile` 建立反应候选池。

输入包括：

- Legacy Kengeki Signal；
- 目标距离；
- 连续拼刀计数；
- 自身 HP/SP；
- 阶段标记；
- 分支交替状态；
- Kengeki 动作冷却；
- 左右和后方空间。

输出是可追溯到 `KengekiXX` 的语义反应动作。普通攻击和拼刀反击共享 CombatComponent 动作生命周期，
但在 BehaviorTree 中分属普通战术与高优先级反应分支。

第一里程碑只实现 `200200/200201`，确认事件和动作链闭环后再扩展其余四类信号。

## 防御与弹刀

Parry 迁移规则：

1. 检查目标位于正面和有效距离；
2. 检查 0.1 秒响应抑制；
3. 根据攻击标签拒绝不可防御动作；
4. 根据阶段、连续防御次数和随机值选择普通格挡、弹刀、闪避或专用承受；
5. 更新连续防御计数和相关冷却；
6. 反应动画结束后重新进入普通选择。

原脚本 `109970/109980/110450/110500/110501` 等目标特殊效果在语义确认前统一通过
Legacy Signal 映射表引用，不直接散布在 Task 中。

## Lua 资产生成

源模块：

```text
AI.Genichiro.BT_Genichiro
Animation.Genichiro.ABP_Genichiro
```

生成目标：

```text
/Game/Characters/Genichiro/AI/BB_Genichiro
/Game/Characters/Genichiro/AI/BT_Genichiro
/Game/Characters/Genichiro/ABP_Genichiro
```

生成流程：

```text
BT Lua require                     AnimBlueprint Lua require
  → BehaviorTree CompileIR          → AnimBlueprint CompileIR
  → 结构/反射属性预检               → Graph/状态机/骨架预检
  → 原地生成 BB + BT                → 原地生成 ABP_Genichiro
  └──────────────┬───────────────────┘
                 → UE 编译并保存
```

两个生成器都必须先 Check 再 Generate。若 BehaviorTree 编译器拒绝覆盖现有资产，应先补齐通用的事务性
原地重建能力，或生成临时资产验收后再替换；不得先删除正式资产。`ABP_Genichiro` 必须原地更新，避免角色引用失效。

## 数据流

### 普通决策流

```text
ASKAIController 感知 TargetActor
  → BT_Genichiro 进入 EngageTarget
  → UpdateContext 捕获距离、朝向、阶段、SP、空间和战斗状态
  → TacticalProfile 输出 TacticalIntent 与可选 SelectedActionId
  → Approach/Reposition 使用 Blackboard + UE MoveTo
  → CombatAction 使用 CombatComponent 提交语义动作
  → ActionSerial/动画状态确认动作完成
  → CombatMemory 写入冷却和短期记忆
  → BehaviorTree 重新决策
```

### 高优先级反应流

```text
战斗组件状态/委托/可选事件队列产生反应信号
  → ReactionRouter 映射为语义 ReactionType
  → Blackboard Decorator Abort 普通战术与导航分支
  → 战斗组件按取消窗口处理当前动画
  → TacticalProfile 选择 Kengeki/Parry/Damage 语义反应动作
  → 反应完成后清理 ReactionType
  → 回到普通决策
```

### 动画表现流

```text
CharacterMovement / AI PathFollowing
  → USKAnimInstance 提供速度、方向、空中和锁定状态
  → ABP_Genichiro.lua 运行时更新轻量表现变量
  → 原生 GenichiroLocomotion StateMachine 求值基础 Pose
  → CombatComponent 动态 Montage 写入 CombatFullBodySlot
  → AnimGraph 合成最终 Pose
  → 动画 Curve/Notify 回传攻击框、取消窗口和战斗事件
```

### 生成流

```text
BT_Genichiro.lua ──► BB_Genichiro + BT_Genichiro
ABP_Genichiro.lua ─► ABP_Genichiro
        │
        └─► 分别 Check IR/Graph/骨架/反射属性
             → 原地生成
             → 编译并保存
```

## 任务 3.1：`710000` Act/Kengeki 离线清单

### 清单边界与记号

来源为 `710000_battle.dec.lua`：普通权重池见第 8～191 行，Act 实现见第 195～612 行，
Kengeki 权重池见第 791～985 行，Kengeki 实现见第 989～1284 行。

- `D`：到目标距离，单位沿用原脚本；`SP`：`GetSp(TARGET_SELF)`；`SPR`：`GetSpRate(TARGET_SELF)`；
- `N0/N2/N3/N5/N6/N7/N10`：原脚本 `Number(index)`；`T0/T1/T3/T4/T6/T7`：AI Timer；
- “基础权重 0”表示函数已实现并注册，但 `Goal.Activate` 没有给它正常候选权重；
- 冷却记为 `动画键/秒数`，完全保留 `SetCoolTime` 原值，不把它自动等同于动作链首动画；
- 所有动作编号均为 Legacy ID，本任务不提前命名剑招、弓招或阶段语义。

脚本结构核对结果：

| 类别 | 实现数 | 注册数 | 异常槽位 |
|------|--------|--------|----------|
| Act | 23 | 33 | 已注册但无函数：07、08、18、19、40、41、42、45、46、47 |
| Kengeki | 29 | 33 | 已注册但无函数：05、06、36、37、41；Kengeki47 有实现但未注册 |

普通决策还有未形成有效函数的冷却残留：已注册空槽 Act07=`3020/15s`、Act08=`3021/15s`、
Act18=`3040/15s`；未注册槽 04=`3006/15s`、13=`3011/15s`、14=`3014/15s`、
32=`3006/15s`、38=`3006/15s`。Kengeki41 是已注册空槽，但仍配置 `3020/15s`。
这些条目不进入可用 ActionCatalog，只保留作变体比较和反编译完整性检查。

### 普通决策基础权重

| 决策条件 | 候选及原始权重 |
|----------|----------------|
| `D >= 7` | Act10=300，Act15=600 |
| `5 <= D < 7` | Act10=300，Act34=100，Act23=100；`SP <= 360` 时 Act09=300 |
| `3 < D < 5` | Act01=5，Act02=10，Act06=30，Act11=15，Act23=15；`SP <= 360` 时 Act09=300 |
| `D <= 3` | Act03=15，Act11=15，Act23=10，Act31=30；`T7` 完成时 Act24=30 |
| 目标有 `109031` 或 `110125` 且 `D <= 5` | 额外 Act16=100 |
| 目标有 `109900` | Act01=5，Act02=5，Act03=5，Act11=5，Act10=30，Act48=30；Act09/15/31=0 |
| `N2 == 1` | Act23=6000，随后立即把 `N2` 清零 |

高优先级特殊分支：

| 条件 | 候选及原始权重 |
|------|----------------|
| 目标有 `110060` 或 `110010`，且位于前方 90° | Act21=1，Act28=100 |
| 目标有 `110060` 或 `110010`，且不在前方 90° | Act21=100 |
| `N7 == 0` 且自身有 `200050` | Act15=600 |
| 目标有 `110030` | Act28=100 |
| 目标位于背后 180° | Act21=100，Act22=1 |
| 第二个 `110060/110010` 分支 | Act27=100；由于前面已有相同条件，该 `elseif` 按当前代码不可达 |

全局过滤：

| 过滤 | 结果 |
|------|------|
| `T0` 未完成 | Act03=0，Act06 降为 1 |
| `T1` 未完成 | Act02=0 |
| `T3` 未完成 | Act24=0 |
| `T6` 未完成 | Act09=0 |
| 自身有 `200051` | Act15/18/19/34/48=0；其中 18/19 在本文件没有函数实现 |
| 左右 45°、2 单位空间都失败 | Act22=0 |
| 左右 90°、1 单位空间都失败 | Act23=0 |
| 后方 2 单位空间失败 | Act24=0 |
| 后方 1 单位空间失败 | Act25=0 |
| 目标有 `110621` | Act23/24=0，Act31=10 |

### 23 个 Act 实现

| Act | 权重入口与条件 | 冷却 | 原始执行链与状态副作用 | UE 行为族候选 |
|-----|----------------|------|------------------------|---------------|
| 01 | `3<D<5:5`；`109900:5` | `3000/15s` | 接近 `3.6-R`；30% `3000→3001→3002→3003`，70% `3000→3001→3010→3025` | CombatAction/分支连段 |
| 02 | `3<D<5:10`；`109900:5`；受 `T1` 过滤 | `3004/15s` | 接近 `3.2-R`；`3004` | CombatAction |
| 03 | `D<=3:15`；`109900:5`；受 `T0` 过滤 | `3005/15s` | 接近 `2.2-R`；观察背后区域；`3005`；设置 `T0=7s` | CombatAction |
| 05 | 基础权重 0 | `3007/15s` | 接近 `5.9-R`；`3007` | 保留/待确认入口 |
| 06 | `3<D<5:30`；`T0` 未完成时降为 1 | `3016/15s` | 接近 `5.2-R`；`3016`；设置 `T0=5s` | CombatAction |
| 09 | `3<D<7` 且 `SP<=360:300`；受 `109900/T6` 过滤 | `3092/15s` | 接近 `4.5-R`；`3040→3041`；设置 `T6=30s` | CombatAction/两段连招 |
| 10 | `D>=5:300`；`109900:30` | `3006/15s` | 接近 `4.8-R`；`3006`；设置 `T3=10s` | CombatAction |
| 11 | `D<5:15`；`109900:5` | `3037/15s` | 接近 `3.2-R`；`3037→3020` | CombatAction/两段连招 |
| 15 | `D>=7:600` 或阶段开场 600；受 `109900/200051` 过滤 | `3014/15s` | 接近 `8.9-R`；`3014→3015`；设置 `N7=1` | PhaseOpening/CombatAction |
| 16 | 目标有 `109031/110125` 且 `D<=5:100` | — | 接近 `2.5-R`；`3022` | TargetStatePunish |
| 20 | 基础权重 0 | — | 接近 `3.2-R`；`3062` | 保留/反应动作复用 |
| 21 | 特殊目标状态或背后条件：1/100 | — | 原生 Turn，时限 3s、容差 45° | TurnToTarget |
| 22 | 目标在背后时 1；要求侧向空间 | — | 按左右空间和目标方位执行 `5202` 或 `5203` 侧闪 | Reposition |
| 23 | `5<=D<7:100`、`3<D<5:15`、`D<=3:10`；`N2:6000` | — | 侧移 1.5～3s；记录方向到 `N10` | Reposition/Strafe |
| 24 | `D<=3` 且 `T7` 完成：30；受 `T3`、后方空间过滤 | — | 后闪 `5201`；设置 `N2=1`、成功时 `T3=30s`；`SPR<=0.7` 且有 `200050` 时续 `3044` | RepositionAttack |
| 25 | 基础权重 0；要求后方空间 | — | 后退 2～4s，目标距离 1～3 | Retreat |
| 26 | 基础权重 0 | — | Wait 0.5s | Hold |
| 27 | 当前 `Activate` 中对应分支不可达 | — | `D>=8` 接近到 8；`D<=5` 拉开到 5；随后随机侧移 | MaintainRange/Reposition |
| 28 | 特殊目标状态时 100 | — | `D<=3` 侧移；`3<D<=8` 接近到 3；`D>8` 接近到 8 | ContextReposition |
| 30 | 基础权重 0 | `3006/15s` | `3009→3044` | CombatAction/两段连招 |
| 31 | `D<=3:30`；`110621:10`；受 `109900` 过滤 | `3045/15s` | 接近 `3.6-R`；`3003→3045` | CombatAction/反击链 |
| 34 | `5<=D<7:100`；受 `200051` 过滤 | `3007/15s` | 接近 `5.9-R`；`3007→3011` | CombatAction/两段连招 |
| 48 | `109900:30`；受 `200051` 过滤 | `3013/5s` | 接近 `8.9-R`；`3013→3015`；设置 `N7=1` | TargetStatePunish/CombatAction |

`Act09` 的冷却键 `3092`、`Act30` 的冷却键 `3006` 与其动作链首动画不一致。清单按源码保留，
后续任务只能在动画/TAE 和共享 CoolTime 语义确认后决定它们是共享冷却、复制残留还是反编译误差。

### Kengeki 信号权重矩阵

| 信号与条件 | 候选及原始权重 |
|------------|----------------|
| `200200`，`D>=2.5` | NoAction=100 |
| `200200`，`D<2.5`，递增后 `N0>=2` | K03=60，K20=60，K30=5，K38=50，K15=30，K43=50，K39=30；`N6==0` 时 K32=20，否则 K33=20 |
| `200200`，`D<2.5`，`N0<2` | `N3==0` 时 K01=50，否则 K04=100 |
| `200201`，`D>=2.5` | NoAction=100 |
| `200201`，`D<2.5`，`N0>=2` | K03=60，K20=60，K38=50，K15=30，K43=50，K39=30；K32/K33 按 `N6` 取 50 |
| `200201`，`D<2.5`，`N0<2` | `N3==0` 时 K01=50，否则 K04=100 |
| `200210` | K02=100，K17=100，K38=50，K31=50 |
| `200211` | K02=100，K10=50，K31=50，K38=50 |
| `200216`，`D>=2` | NoAction=10 |
| `200216`，`D<2`，递增后 `N0>=3` | K03=60，K20=20，K38=100，K43=50；K32/K33 按 `N6` 取 50 |
| `200216`，`D<2`，`N0<3` | K20=50；`N3==0` 时 K01=50，否则 K14=50 |
| `200215`，`D>=2` | NoAction=10 |
| `200215`，`D<2`，递增后 `N0>=3` | K20=20，K38=30，K31=15，K43=10，K39=10；K32/K33 按 `N6` 取 50 |
| `200215`，`D<2`，`N0<3` | K20=50；`N3==0` 时 K01=50，否则 K14=50 |

Kengeki 全局过滤：

| 条件 | 结果 |
|------|------|
| 自身有 `200051` | K03/09/15/32/33/39/46=0 |
| 否则自身有 `200050` | K20=0 |
| 左右 45°、2 单位空间都失败 | K20=0 |
| `T6` 未完成 | K40=0 |
| `T6` 完成且 HP 比例 `<=0.75` | 额外 K40=50 |

### 29 个 Kengeki 实现

| Kengeki | 权重入口 | 冷却 | 原始执行链与状态副作用 |
|---------|----------|------|------------------------|
| 01 | 200200/200201/200215/200216 的低连续次数分支 | `3050/8s` | 清链；`3050`；设置 `N3=1` |
| 02 | 200210=100，200211=100 | `5201/10s` | 清链；`5201`；设置 `N2=1`、成功时 `T3=30s`；`SPR<=0.7` 且有 `200050` 时续 `3044` |
| 03 | 200200/200201/200216 的高连续次数分支 | `3009/10s` | 清链；`3009`；按空间选择方向并侧移 4s |
| 04 | 200200/200201 低次数且 `N3!=0` | `3055/8s` | 清链；`3055`；设置 `N3=0` |
| 07 | 权重池未赋值 | `3060/8s` | 清链；`3060` |
| 09 | 权重池未赋值；受 `200051` 过滤 | `3018/8s` | 清链；`3018→3015` |
| 10 | 200211=50 | `3065/8s` | 清链；`3065` |
| 13 | 权重池未赋值 | `3075/8s` | 清链；`3075` |
| 14 | 200215/200216 低次数且 `N3!=0` | `3076/8s` | 清链；`3076` |
| 15 | 200200/200201 高次数=30；受 `200051` 过滤 | `3031/15s` | 清链；`3031` 后 50% `3019→3029`，否则 `3036`；另有 50% 设置 `N2=1` |
| 17 | 200210=100 | `3071/8s` | 清链；`3071` |
| 18 | 权重池未赋值 | `3004/8s` | 清链；`3004` |
| 19 | 权重池未赋值 | — | 清链；`3044` |
| 20 | 多个近距连续分支；受 `200050` 和侧向空间过滤 | `5202/15s` | 清链；后闪 `5202→3007` |
| 21 | 权重池未赋值 | — | 清链；`3010` |
| 30 | 200200 高次数=5 | `3063/15s` | 清链；`3063`；成功时 `T1=10s`；设置 `N5=0` |
| 31 | 200210/200211=50，200215 高次数=15 | `3068/15s` | 清链；`3068`；成功时 `T1=10s`；设置 `N5=0` |
| 32 | 高次数分支且 `N6==0` | `3018/15s` | 清链；`3018→3015`；设置 `N6=1` |
| 33 | 高次数分支且 `N6!=0` | `3007/15s` | 清链；`3018` 后 50% `3019→3029`，否则 `3019`；设置 `N6=0` |
| 34 | 权重池未赋值 | `3037/15s` | 清链；`3037→3020` |
| 35 | 权重池未赋值 | `3016/8s` | 清链；`3016`；设置 `N5=0` |
| 38 | 多数高次数分支，200210/200211 也可进入 | `3030/8s` | 清链；`3030`；忍杀数 `<=1` 时 75% 续 `3067`、25% 续 `3025`，否则固定 `3025` |
| 39 | 200200/200201 高次数=30，200215 高次数=10；受 `200051` 过滤 | `3034/15s` | 清链；`3034→3036→3015` |
| 40 | `T6` 完成且 HP 比例 `<=0.75:50` | `3028/15s` | 清链；`3028`；设置 `T6=50s` |
| 43 | 200200/200201/200216 高次数=50，200215 高次数=10 | `3062/15s` | 清链；`3062`；设置 `N2=1` |
| 44 | 权重池未赋值 | `3067/15s` | 清链；`3067`；设置 `N2=1` |
| 45 | 权重池未赋值 | `3032/15s` | 清链；`3045`；设置 `N0=0` |
| 46 | 权重池未赋值；受 `200051` 过滤 | — | 清链；`3039`；成功时 `T4=10s`；随后固定左侧移 2.5s |
| 47 | 有实现但未注册 | — | 清链；`3038`；成功时 `T4=10s` |

`Kengeki33` 的冷却键 `3007`、`Kengeki45` 的冷却键 `3032` 与动作链不一致；与 Act 表相同，
后续保持 Legacy 原值并单独验证。Kengeki07/09/13/18/19/21/34/35/44/45/46 在本权重池中没有正权重，
Kengeki47 未注册；在找到共享函数或外部直接入口前，不进入第一版候选集。

### Timer 与短期状态索引

| 索引 | 本文件中的写入与读取 | UE 迁移含义（暂定） |
|------|----------------------|---------------------|
| `N0` | 200200/200215/200216 累加；K45 清零 | 连续拼刀计数 |
| `N2` | Act24/K02/K15/K43/K44 置 1；普通决策消费后强制 Act23 并清零 | 动作后强制侧移标记 |
| `N3` | K01 置 1、K04 置 0；控制两者交替 | 拼刀反击交替状态 |
| `N5` | K30/K31/K35 清零 | 语义未知，保留 Legacy 状态 |
| `N6` | K32/K33 交替置 1/0 | 高连续拼刀分支交替状态 |
| `N7` | Act15/48 置 1；阶段开场判断读取 | 阶段开场动作已使用 |
| `N10` | Act23 写入左右侧移方向 | 最近侧移方向 |
| `T0` | Act03=7s、Act06=5s；过滤 Act03 并压低 Act06 | 近距动作互斥窗口 |
| `T1` | K30/K31 成功时 10s；过滤 Act02 | 拼刀反应后的动作抑制 |
| `T3` | Act10=10s；Act24/K02 成功时 30s；过滤 Act24 | 后闪动作抑制 |
| `T4` | K46/K47 成功时 10s | 本权重池未读取，语义待查 |
| `T6` | Act09=30s、K40=50s，Interrupt 也可能写 50s | Act09 冷却与低血量 K40 门控 |
| `T7` | 仅用于 Act24 是否可选；本文件没有写入 | 外部初始化/共享脚本状态，任务 3.3 继续追踪 |

### 任务 3.1 结论

第一版 UE 数据层不能把注册表槽位直接等同于可用动作。应只把“有函数实现、存在正权重入口、动作链可解析”
的项目加入默认 Profile；零权重实现、空注册槽和 Kengeki47 进入 Legacy 保留区。原版权重矩阵作为分布校准输入，
移动类 Act21/22/23/25/27/28 转译为 BehaviorTree/MoveTo 行为族，攻击类 Act/Kengeki 才进入 Combat ActionCatalog。

Interrupt/Parry/Damaged 区域还直接使用 `3017`、`3023`、`3100`、`3101`、`3102`、`3103`，并通过
`SpinStep` 提交状态请求 `5211`；它们不属于 Act/Kengeki 函数清单，将在任务 3.3 的事件语义映射中处理。

## 任务 3.2：Legacy 动画编号到 UE 资产映射

### 映射规则与校验范围

本任务从任务 3.1 的 23 个 Act、29 个 Kengeki 及其 `SetCoolTime` 键提取所有 Legacy 动画编号，
共得到 50 个唯一编号。导入管线的稳定命名规则为：

```text
Legacy ID: 3000
文件名:    Anim_Genichiro_a000_003000.uasset
对象路径:  /Game/Characters/Genichiro/Animations/
           Anim_Genichiro_a000_003000.Anim_Genichiro_a000_003000
```

下表“资产名”均位于 `/Game/Characters/Genichiro/Animations/`；运行时配置必须使用完整对象路径，
不能使用文件系统相对路径。此处只验证包文件存在且命名唯一；Skeleton、Sequence 类型、Root Motion 和曲线
仍由任务 3.4/4.5 校验。

| Legacy ID | 引用来源 | UE 资产名 | 文件状态 |
|-----------|----------|-----------|----------|
| `3000` | Act01、Act01冷却 | `Anim_Genichiro_a000_003000` | ✅ 唯一 |
| `3001` | Act01 | `Anim_Genichiro_a000_003001` | ✅ 唯一 |
| `3002` | Act01 | `Anim_Genichiro_a000_003002` | ✅ 唯一 |
| `3003` | Act01、Act31 | `Anim_Genichiro_a000_003003` | ✅ 唯一 |
| `3004` | Act02、Act02冷却、Kengeki18、Kengeki18冷却 | `Anim_Genichiro_a000_003004` | ✅ 唯一 |
| `3005` | Act03、Act03冷却 | `Anim_Genichiro_a000_003005` | ✅ 唯一 |
| `3006` | Act10、Act10冷却、Act30冷却、空槽04/32/38冷却 | `Anim_Genichiro_a000_003006` | ✅ 唯一 |
| `3007` | Act05、Act05冷却、Act34、Act34冷却、Kengeki20、Kengeki33冷却 | `Anim_Genichiro_a000_003007` | ✅ 唯一 |
| `3009` | Act30、Kengeki03、Kengeki03冷却 | `Anim_Genichiro_a000_003009` | ✅ 唯一 |
| `3010` | Act01、Kengeki21 | `Anim_Genichiro_a000_003010` | ✅ 唯一 |
| `3011` | Act34、空槽13冷却 | `Anim_Genichiro_a000_003011` | ✅ 唯一 |
| `3013` | Act48、Act48冷却 | `Anim_Genichiro_a000_003013` | ✅ TAE 别名物化 |
| `3014` | Act15、Act15冷却、空槽14冷却 | `Anim_Genichiro_a000_003014` | ✅ 唯一 |
| `3015` | Act15、Act48、Kengeki09/32/39 | `Anim_Genichiro_a000_003015` | ✅ 唯一 |
| `3016` | Act06、Act06冷却、Kengeki35、Kengeki35冷却 | `Anim_Genichiro_a000_003016` | ✅ 唯一 |
| `3018` | Kengeki09/32/33、Kengeki09/32冷却 | `Anim_Genichiro_a000_003018` | ✅ 唯一 |
| `3019` | Kengeki15、Kengeki33 | `Anim_Genichiro_a000_003019` | ✅ 唯一 |
| `3020` | Act11、Kengeki34、空槽 Act07/Kengeki41 冷却 | `Anim_Genichiro_a000_003020` | ✅ 唯一 |
| `3021` | 空槽 Act08 冷却 | `Anim_Genichiro_a000_003021` | ✅ 唯一 |
| `3022` | Act16 | `Anim_Genichiro_a000_003022` | ✅ 唯一 |
| `3025` | Act01、Kengeki38 | `Anim_Genichiro_a000_003025` | ✅ 唯一 |
| `3028` | Kengeki40、Kengeki40冷却 | `Anim_Genichiro_a000_003028` | ✅ 唯一 |
| `3029` | Kengeki15、Kengeki33 | `Anim_Genichiro_a000_003029` | ✅ 唯一 |
| `3030` | Kengeki38、Kengeki38冷却 | `Anim_Genichiro_a000_003030` | ✅ 唯一 |
| `3031` | Kengeki15、Kengeki15冷却 | `Anim_Genichiro_a000_003031` | ✅ 唯一 |
| `3032` | Kengeki45冷却 | `Anim_Genichiro_a000_003032` | ✅ 唯一 |
| `3034` | Kengeki39、Kengeki39冷却 | `Anim_Genichiro_a000_003034` | ✅ 唯一 |
| `3036` | Kengeki15、Kengeki39 | `Anim_Genichiro_a000_003036` | ✅ 唯一 |
| `3037` | Act11、Act11冷却、Kengeki34、Kengeki34冷却 | `Anim_Genichiro_a000_003037` | ✅ 唯一 |
| `3038` | Kengeki47 | `Anim_Genichiro_a000_003038` | ✅ 唯一 |
| `3039` | Kengeki46 | `Anim_Genichiro_a000_003039` | ✅ 唯一 |
| `3040` | Act09、空槽 Act18 冷却 | `Anim_Genichiro_a000_003040` | ✅ 唯一 |
| `3041` | Act09 | `Anim_Genichiro_a000_003041` | ✅ 唯一 |
| `3044` | Act24、Act30、Kengeki02、Kengeki19 | `Anim_Genichiro_a000_003044` | ✅ 唯一 |
| `3045` | Act31、Act31冷却、Kengeki45 | `Anim_Genichiro_a000_003045` | ✅ 唯一 |
| `3050` | Kengeki01、Kengeki01冷却 | `Anim_Genichiro_a000_003050` | ✅ 唯一 |
| `3055` | Kengeki04、Kengeki04冷却 | `Anim_Genichiro_a000_003055` | ✅ 唯一 |
| `3060` | Kengeki07、Kengeki07冷却 | `Anim_Genichiro_a000_003060` | ✅ 唯一 |
| `3062` | Act20、Kengeki43、Kengeki43冷却 | `Anim_Genichiro_a000_003062` | ✅ 唯一 |
| `3063` | Kengeki30、Kengeki30冷却 | `Anim_Genichiro_a000_003063` | ✅ 唯一 |
| `3065` | Kengeki10、Kengeki10冷却 | `Anim_Genichiro_a000_003065` | ✅ 唯一 |
| `3067` | Kengeki38、Kengeki44、Kengeki44冷却 | `Anim_Genichiro_a000_003067` | ✅ 唯一 |
| `3068` | Kengeki31、Kengeki31冷却 | `Anim_Genichiro_a000_003068` | ✅ 唯一 |
| `3071` | Kengeki17、Kengeki17冷却 | `Anim_Genichiro_a000_003071` | ✅ 唯一 |
| `3075` | Kengeki13、Kengeki13冷却 | `Anim_Genichiro_a000_003075` | ✅ 唯一 |
| `3076` | Kengeki14、Kengeki14冷却 | `Anim_Genichiro_a000_003076` | ✅ 唯一 |
| `3092` | Act09冷却；Interrupt 也播放该编号 | `Anim_Genichiro_a000_003092` | ✅ TAE 别名物化 |
| `5201` | Kengeki02、Kengeki02冷却 | `Anim_Genichiro_a000_005201` | ✅ 唯一 |
| `5202` | Act22、Kengeki20、Kengeki20冷却 | `Anim_Genichiro_a000_005202` | ✅ 唯一 |
| `5203` | Act22 | `Anim_Genichiro_a000_005203` | ✅ 唯一 |

### TAE 别名资产处理

| Legacy ID | 原版 TAE 语义 | UE 处理结果 |
|-----------|----------------|-------------|
| `3013` | `ImportHKX → 3014`：复用 `3014` 动作曲线，但保留 `3013` 自身事件轨 | 生成 80 帧、2.63 秒的 `Anim_Genichiro_a000_003013`；事件来源保持 `3013` |
| `3092` | `ImportOtherAnim → 8603`：完整继承 `8603` 动作与事件 | 生成 176 帧、5.83 秒的 `Anim_Genichiro_a000_003092`；事件来源解析为 `8603` |

原始 `c7100.anibnd` 不包含这两个独立 HKX；缺失来自旧管线未解析 TAE MiniHeader，不是资源损坏。
管线现已输出 `ReferenceType/MotionSourceAnimID/EventSourceAnimID`，并在动画 JSON 阶段通用物化别名。
当前结果为 **50 个唯一存在、0 个项目缺失、0 个重复匹配**；Act48 与 `3092` Interrupt 不再需要禁用或替代。

## 任务 3.3：SpEffect / Interrupt 语义事件映射

### 范围与确认度

`710000_logic` 与 `710000_battle` 共出现 **40 个数值 SpEffect**，另有 3 个引擎 Interrupt、
`Interupt_Use_Item` 道具助手和 `COMMON_SP_EFFECT_PC_ATTACK_RUSH` 符号化攻击标签。`710300/711000/711300`
只用于交叉验证相同信号的控制流，不把变体独有行为并入基础 Profile。

| 等级 | 定义 | 迁移约束 |
|------|------|----------|
| A：控制流确认 | 原脚本入口、条件和直接结果完整可见，且同族脚本行为一致 | 可以使用稳定 UE 语义名；保留 Legacy ID 供追踪 |
| B：行为确认 | 对权重、动作或状态的影响明确，但原版业务名称只能从行为推断 | 使用中性语义名并附 Legacy ID，不声明剧情或招式含义 |
| C：仅保留 | 只有观察/轮询，或分支为空、共享助手实现不可见 | 仅使用 `Legacy<ID>` 名称，不驱动第一阶段行为 |

确认度描述的是“迁移语义证据”，不是对 FromSoftware 内部正式命名的还原。

### 引擎 Interrupt 与离散事件

| 输入 | Subject / Edge | 原脚本可观察结果 | UE 语义事件 | 确认度 |
|------|----------------|------------------|-------------|--------|
| `INTERUPT_ParryTiming` | Self / Event | 调用 `Parry`，在正面、距离、攻击标签和抑制定时器条件下选择防御反应 | `AttackThreat` → `GuardReactionRequested` | A |
| `INTERUPT_ShootImpact` | Self / Event | 清空当前链并播放 `3100` | `ProjectileImpact` | A |
| `INTERUPT_ActivateSpecialEffect` | Self/Target / Rising | 读取实际 SpEffect ID 后分派 | 仅作为适配层输入，不进入 Blackboard | A |
| `Interupt_Use_Item` | Target / Event | 清链并提交 `3023`；受阶段和忍杀次数过滤 | `TargetUseItem` | A |
| `220010` | Self / Rising | `ClearEnemyTarget()` | `TargetInvalidated` | A |
| `107710` | Self / Rising | `Replanning()` | `ForceReplan` | A |
| `110060` | Target / Rising | 写目标状态标记并提交接近目标；普通决策中还改变转向/移动权重 | `LegacyTargetState110060Changed` | B |
| `110015` | Target / Rising | 清目标状态标记并提交 Wait | `LegacyTargetState110015Changed` | B |
| `5029` | Self / Rising | 调用 `Damaged`；15% 后闪，阶段条件下另有反击分支 | `DamageReceived` | A |
| `3710020` | Self / Rising | `Number(0)=0` | `DeflectChainReset` | A |
| `3710030` + 持有 `3710032` | Self / Rising + State | 清链、提交 `3092`、设置 `Timer(6)=50s` | `ForcedReaction3092` | B |
| `5031` | Self / Rising | 忍杀次数不高于 1 且距离不小于 4.1 时提交 `3017` | `ConditionalReaction3017` | B |
| `3710050` | Target / Rising | 阶段 A 下 50% 提交 `3023`，否则横移 | `TargetSpecialAction` | B |
| `110620` | Target / Rising | `Replanning()` | `ForceReplan` | A |

基础 `710000` 虽读取 `GetSpecialEffectInactivateInterruptType(0)`，但没有检查
`INTERUPT_InactivateSpecialEffect`，因此第一阶段不生成 SpEffect Falling Edge 事件。变体中的 Falling Edge 重规划
属于后续 Profile 差异。

### 持续状态与决策修饰信号

| Legacy ID / 标签 | Subject | 对基础 `710000` 的作用 | UE 中性语义 | 确认度 |
|-------------------|---------|--------------------------|-------------|--------|
| `200004` | Self | `Goal.Interrupt` 总开关；不存在时拒绝所有战斗中断 | `ReactionHandlingEnabled` | A |
| `200050` | Self | 阶段开场、受击反击、拼刀候选和 `3710050` 响应修饰 | `LegacyPhaseFlagA` | B |
| `200051` | Self | 禁用 Act15/18/19/34/48 及多类 Kengeki；限制道具惩罚 | `LegacyPhaseFlagB` | B |
| `110010` | Target | 与 `110060` 一起改变转向和移动权重 | `LegacyTargetState110010` | B |
| `110030` | Target | 强制提高 Act28 权重 | `LegacyTargetState110030` | B |
| `109031` / `110125` | Target | 近距时强制提高 Act16 权重 | `ClosePunishWindow` | B |
| `109900` | Target | 重写候选权重，启用 Act48 并压低/禁用部分动作 | `TargetPunishWindow` | B |
| `110621` | Target | 禁用横移/后闪，提升 Act31 | `TargetRepositionRestricted` | B |
| `109970` | Target | 进入特殊 Parry 分支，并结合自身响应模式决定拒绝、防御或后闪 | `LegacyParryBranch109970` | B |
| `COMMON_SP_EFFECT_PC_ATTACK_RUSH` | Target | 播放 `3103` 专用承受反应 | `RushAttackTag` | A |
| `110450` / `110500` / `110501` | Target | 直接拒绝 Parry 处理 | `ParryForbiddenAttackTag` | A |
| `109980` | Target | Parry 时改为 `5201` 后闪 | `DodgePreferredAttackTag` | B |
| `221000` / `221001` | Self | 选择 `109970` 分支中的响应模式 0/1 | `LegacyGuardResponseMode0/1` | B |
| `3710032` | Self | 允许 `3710030` 触发 `3092` | `ForcedReaction3092Enabled` | B |
| `3710040` | Self | Parry 时强制选择 `3102` | `SpecialGuardVariant3102` | B |

### 拼刀结果信号

`ReturnKengekiSpecialEffect` 的共享实现不在已解包 Lua 中，但六个返回值只在 `Kengeki_Activate` 中使用，并稳定
驱动不同反应候选池。因此“它们是拼刀结果类别”可标为 A；左右、强弱、完美弹刀等更细含义仍为 C。

| Legacy ID | UE 名称 | 基础行为差异摘要 | 确认度 |
|-----------|---------|------------------|--------|
| `200200` | `LegacyKengeki200200` | 增加连续计数；按距离和计数进入普通/高连续反击池 | A/C |
| `200201` | `LegacyKengeki200201` | 与 `200200` 类似，但不在入口增加连续计数且候选权重不同 | A/C |
| `200210` | `LegacyKengeki200210` | 固定启用 K02/K17，并加入 K31/K38 | A/C |
| `200211` | `LegacyKengeki200211` | 固定启用 K02，并加入 K10/K31/K38 | A/C |
| `200215` | `LegacyKengeki200215` | 近距增加连续计数，进入较低权重的高连续池 | A/C |
| `200216` | `LegacyKengeki200216` | 近距增加连续计数，候选更偏 K03/K38/K43 | A/C |

### C 级保留项

| Legacy ID / 状态 | 证据 | 第一阶段处理 |
|-------------------|------|--------------|
| `5025`、`5026`、`5030` | 基础版仅注册观察，没有显式分支；部分变体使用 `5026/5030`，但行为不可反推到 `710000` | 记录原始激活日志，不产生基础 Profile 反应 |
| `3710010`、`3710031` | 只注册观察，没有显式分支 | 保留 ID，不产生行为 |
| `200002` | Logic 中只出现在空分支或恒假条件 | 不迁移为空操作事件 |
| `Timer(7)` | Act24 读取完成状态，但全部已解包弦一郎 Lua 中未找到写入方 | 保留为外部初始化依赖，任务 4 能力审计时决定默认值 |

### 反应动画补充映射

| Anim ID | 触发来源 | UE 语义用途 | 资产状态 | 确认度 |
|---------|----------|-------------|----------|--------|
| `3017` | `5031` 条件分支 | `ConditionalReaction3017` | ✅ 已存在，77 帧 | B |
| `3023` | `3710050` 阶段响应、目标使用道具 | `TargetActionPunish` | ✅ 已存在，96 帧 | B |
| `3092` | `3710030 + 3710032` | `ForcedEndureReaction` | ✅ 已存在，176 帧；完整继承 `8603` | B |
| `3100` | 普通 Parry 失败分支、`ShootImpact` | `StandardGuardHit` / `ProjectileImpactReaction` | ✅ 已存在，46 帧 | A |
| `3101` | 连续防御阈值或响应模式 0 | `StrongGuardVariant` | ✅ 本任务补导，46 帧；动作来源 `3100`、事件来源 `3101` | B |
| `3102` | 自身持有 `3710040` | `SpecialGuardVariant3102` | ✅ 本任务补导，46 帧；动作来源 `3100`、事件来源 `3102` | B |
| `3103` | 目标带突进攻击标签 | `RushGuardVariant` | ✅ 本任务补导，46 帧；动作来源 `3100`、事件来源 `3103` | A |
| `5211` | `109970` 响应模式 1、非正面概率后闪 | `DefensiveLongBackstep` | 🧭 EzState/动作请求；无独立动画属于正常情况，UE 表现源使用 `5201`，长后撤位移由语义策略承载 | 状态语义 A / 表现映射 B |

### `5211` EzState 映射修正

`GOAL_COMMON_SpinStep` 的首个数值参数最终传入 `SetAttackRequest`，其含义是行为请求 ID，不能默认等同于 TAE
动画 ID。c9997 行为图中存在独立状态 `W_Step5211`，但 c7100 TAE 没有 5211 时间线；因此只能确认它是独立
行为变体，不能再把 `5211→5201` 写成原版物理动画等价关系。

UE 侧采用以下显式映射，不生成虚假的 `Anim_Genichiro_a000_005211`：

| 字段 | 值 | 说明 |
|------|----|------|
| `ActionRequestID` | `5211` | 保留原脚本请求与日志诊断 |
| `ActionID` | `DefensiveLongBackstep` | UE 行为树使用的语义动作 |
| `BehaviorStateName` | `W_Step5211` | c9997 中的独立行为状态 |
| `TAEAnimID` | `nil` | c7100 TAE 没有同号时间线 |
| `ResolvedAssetID` | `5201` | UE 暂用的可播放表现资产，不宣称是原版物理动作源 |
| `bUERepresentationApproximation` | `true` | 强制暴露近似边界，避免后续再次把编号合并 |
| `MovementPolicy` | `NavigationSafeLongBackstep` | UE 负责可导航、可中断的长后撤位移；距离和时长在任务 5.2/11.3 调参 |
| `AssetPath` | `/Game/Characters/Genichiro/Animations/Anim_Genichiro_a000_005201.Anim_Genichiro_a000_005201` | 实际加载的 AnimSequence |

原版行为文件内的精确位移参数当前尚未解出，因此位移数值确认度为 C；这只影响后续手感调校，不再构成动画资产阻塞。

### UE 数据契约

`GenichiroLegacySignals.lua` 的每个条目包含：`LegacyID`、`Subject`、`Edge`、`SemanticEvent`、
`Confidence`、`InputKind`、`DrivesBehavior`、`SourceLocations` 和 `Notes`。Raw SpEffect 只在事件适配层出现；
BehaviorTree 读取 `ReactionType/Priority`，CombatMemory 保存持续阶段与拼刀类别。C 级条目只进入诊断日志，
不能直接选择动作。

### ActionCatalog 路径生成约束

`GenichiroActionCatalog.lua` 不手写散落路径。动作步骤先通过 `ActionRequestResolutionByID`，再按
`ResolvedAssetID` 生成路径：

```lua
local function animation_path(playable_asset_id)
    local asset_name = string.format(
        "Anim_Genichiro_a000_%06d",
        playable_asset_id)
    return string.format(
        "/Game/Characters/Genichiro/Animations/%s.%s",
        asset_name,
        asset_name)
end
```

生成前必须以 UE 可播放资产白名单校验 `ResolvedAssetID`；`ActionRequestID` 不直接参与路径拼接。解析记录同时保存
`BehaviorStateName`、`TAEAnimID`、`MotionSourceAnimID` 和 `EventSourceAnimID`。例如 3013 的四层编号分别是
`3013 / 3013 / 3014 / 3013`，而 UE 播放物化后的 3013 别名资产。只有解析缺失或解析后的可播放资产缺失才使动作
不可用并输出诊断，不允许运行时静默尝试错误路径。

## 任务 3.4：TAE 攻击框、取消窗口与动作完成覆盖率

### 审计范围与口径

以 `m11_01`、`m11_02` 两份 `710000_battle.dec.lua` 的直接 `AddSubGoal` 动画参数为准；两份脚本得到相同集合：

- 47 个攻击或连段动画；
- 5 个 `EndureAttack` 反应动画；
- 4 个 `SpinStep` 动作请求，其中 `5201` 同时被 `ComboRepeat` 引用，`5211` 是状态请求并复用 `5201` 表现；
- 去重后共 55 个运行时动作请求，对应 54 个 UE 可播放 AnimSequence；底层动作源 HKX 和事件源必须另行统计。

任务 3.2 清单中的 `3021`、`3032` 仅作为冷却键出现，不计入运行时播放覆盖率；它们的 TAE 和 UE 资产均存在。
覆盖率分成三层计算：原始 TAE 是否有事件、事件能否转换为项目曲线/语义、生成后的 UE 资产是否实际携带数据。

### 总体结果

| 项目 | 原始数据覆盖 | UE 资产/运行时覆盖 | 结论 |
|------|--------------|-------------------|------|
| 动作请求到表现源 | `55/55` 已解析 | 55 个请求映射到 54 个 UE 可播放 AnimSequence | `5211` 使用显式标记的 5201 表现近似，无缺失资产 |
| 近战攻击框 | 34 个动作、57 个 Type 1 窗口 | `0/158` 个 Genichiro 动画包检出 `AttackHitbox` 曲线 | 原始数据完整，尚未写入资产 |
| 弹射物 | 18 个动作、23 个 Type 2 窗口 | 未发现 Genichiro 弹射物事件消费者 | 12 个纯弓箭动作当前无法造成伤害，6 个混合动作会丢失箭矢段 |
| TAE 输入取消 | `0/54` 个可播放时间线命中当前取消映射 | `CancelActions` 与 `CanCancelTo*` 均为 `0/158` | 不能复用玩家输入取消映射 |
| 动作完成 | TAE 无独立完成事件 | 现有动态 Montage 可对 `54/54` 个有效资产产生结束委托 | 完成信号应由 Montage + ActionSerial 承担 |

UE 包内曲线检查采用与现有 Sekiro 动画相同的名称表扫描；Sekiro 资产可检出 `AttackHitbox`、
`CanCancelToGuard` 等名称，而 158 个 Genichiro 动画均未检出，故后续生成前按“未写入”处理。编辑器本轮未运行，
没有启动 PIE 或修改动画资产。

### 攻击事件分类

47 个攻击/连段动画按 TAE 伤害事件分为：

| 分类 | 数量 | 编号或说明 |
|------|------|------------|
| 仅 Type 1 近战 | 28 | 可直接转换为 `AttackHitbox`；全部 `AttackType=0`，现有 Standard 映射可表达 |
| Type 1 + Type 2 混合 | 6 | `3007`、`3020`、`3022`、`3025`、`3062`、`3067` |
| 仅 Type 2 弹射物 | 12 | `3009`、`3011`、`3013`、`3014`、`3017`、`3018`、`3023`、`3031`、`3034`、`3036`、`3039`、`3044` |
| 无伤害事件 | 1 | `5201`；同时承担 `ComboRepeat` 与 `SpinStep`，按位移/退避段处理，不判定为攻击框缺失 |

`3092` 是反应动画，但其别名来源 `8603` 带有 7 个 Type 1 窗口。ActionCatalog 不得仅因原始 TAE 存在攻击事件
就把反应动作标成可伤害；必须由语义动作显式决定是否消费这些窗口。当前 CombatComponent 仅在 Light/Heavy 状态
开启武器碰撞，能够失败关闭这一异常来源。

### 取消窗口结论

现有导入规则只把 JumpTable `26/115`、`117`、`25`、`118`、`154` 映射为攻击、防御、闪避、义手、道具取消。
54 个物理动画中没有任何一个出现这些 ID；高频 ID 是 `122`、`73`、`23`、`63`、`78`、`79`、`86`，
且大量动作共享相同区间。它们不能在没有语义证据时强行改名为玩家 R1/L1/Dodge 取消。

另外，`CancelActions` 是单值枚举曲线，同一帧只能保留一种取消目标；项目当前真正消费的是独立的
`CanCancelToGuard`、`CanCancelToJump`、`CanCancelToDodge`，并没有 `CancelActions` 运行时消费者。因此弦一郎应在
ActionCatalog 中建立 UE 语义 `InterruptPolicy` 和可中断窗口，由 BehaviorTree Abort 只提交 `PendingReaction`，
再由战斗执行器在合法窗口切换动作。该策略在任务 4.1—4.3 做运行时能力审计，不在本离线任务中臆造曲线。

### 动作完成信号结论

TAE 不需要额外伪造“最后一帧完成”事件。`USKCombatComponent::PlayCombatAnimation` 已给每个动态 Montage 绑定
`HandleCombatMontageEnded`，并以 Montage 身份和启动时 `ActionSerial` 校验后广播
`OnCombatAnimationEnded(ActionSerial, bInterrupted)`。因此 54 个有效资产的自然结束和中断都具有统一完成源；
`5211` 通过 `5201` 的 Montage 完成信号进入同一链路，执行器再把 Montage 完成与
`NavigationSafeLongBackstep` 位移策略的完成结果汇合为语义动作完成。

当前通用 `SKCombatAttack.lua` 仍通过 `IsCombatAnimationPlaying` 轮询完成，Tick 中没有再次验证已锁存的
`ActionSerial`，Abort 也不会主动取消动画。这不是 TAE 数据缺失，归入任务 4.1/4.2 的执行器生命周期审计。

### 后续实施约束

1. 生成 Genichiro 动画曲线前，先补 Type 2 弹射物的通用事件消费方案；只写 `AttackHitbox` 不能闭合弓箭动作。
2. Type 1 近战窗口可批量写入，但必须由 ActionCatalog 的动作类型决定是否启用，尤其是 `3092`。
3. 不把弦一郎高频 JumpTable ID 猜测成玩家输入取消；取消政策由 UE 语义层显式定义并保留 Legacy 溯源。
4. 动作 Task 以 Montage 结束委托和 ActionSerial 为完成权威，轮询只能作为故障兜底。
5. `5211` 必须按“行为请求 → `W_Step5211` → `DefensiveLongBackstep` → `5201` UE 表现近似 + 位移策略”解析，禁止拼接或生成 `005211` 资产路径。

## 任务 3.5：动画 UE 用途分类

### 分类范围与规则

当前 Genichiro 动画目录共有 158 个 AnimSequence。第一版 `710000` 只对白名单中的 56 个 UE 可播放资产建立用途：

- 54 个来自任务 3.4 的运行时动作请求解析结果；
- `3021`、`3032` 只作为原脚本冷却键出现，但保留为禁用状态的战斗动作候选；
- 其余 102 个包内动画没有 `710000` 脚本引用证据，保持 `Unclassified`，不得因编号相近自动进入 AnimBlueprint 或 ActionCatalog。

主分类按 UE 消费者定义，而不是按编号区间猜测动作名称。每个可播放资产只有一个 `PrimaryUsage`，同时允许使用
`UsageTags` 记录“由反应触发”“受阶段门控”等交叉语义。分类只决定资产所有权，不代替任务 4.5 的 Skeleton、
Slot、Root Motion 和视觉方向校验。

### 分类结果

| PrimaryUsage | 数量 | UE 可播放资产 ID | UE 消费者与执行方式 |
|--------------|------|------------------|----------------------|
| `Locomotion` | 3 | `5201`、`5202`、`5203` | 离散战术位移；由移动/动作执行器提交并与导航空间校验协作，不进入普通攻击伤害窗口 |
| `CombatAction` | 48 | 见下方白名单 | ActionCatalog + CombatComponent 动态 Montage；攻击/弹射物事件由动作语义决定是否消费 |
| `Reaction` | 5 | `3092`、`3100`、`3101`、`3102`、`3103` | ReactionRouter 抢占普通战术分支，通过 CombatComponent 播放并等待 ActionSerial 收敛 |
| `Phase` | 0 | — | 基础 `710000` 没有独立阶段切换动画；不得把阶段条件或阶段开场攻击误分类为 Phase 动画 |

这里的 `Locomotion` 是一次性 `DiscreteMovement`，不等同于 `GenichiroLocomotion` 状态机中的 Idle/持续移动/转身
基础 Pose。基础 Pose 已按 c9997 行为图状态和 c7100 TAE 双重核对：`IdleBattle → 400000`、
`RunFrontBattle → 405010`、`WalkBack/Left/RightBattle → 405001/405002/405003`、
`TurnBattle_Left90/Right90 → 405400/405401`。这些条目均为 Direct 且 UE 资产存在。`7010`、`8010～8013`、
`8400/8401` 实际分别是坠落、四向受击和弹反受击，禁止再按编号邻近关系用于基础状态机。

### CombatAction 白名单

```text
3000, 3001, 3002, 3003, 3004, 3005, 3006, 3007, 3009, 3010, 3011, 3013,
3014, 3015, 3016, 3017, 3018, 3019, 3020, 3021, 3022, 3023, 3025, 3028,
3029, 3030, 3031, 3032, 3034, 3036, 3037, 3038, 3039, 3040, 3041, 3044,
3045, 3050, 3055, 3060, 3062, 3063, 3065, 3067, 3068, 3071, 3075, 3076
```

特殊标签约束：

| 可播放资产 ID | PrimaryUsage | UsageTags | 约束 |
|-------------|--------------|-----------|------|
| `3014`、`3015` | `CombatAction` | `PhaseGated`、`PhaseOpening` | Act15 可由阶段开场条件提高权重，但动画仍是可伤害的战斗连段 |
| `3017` | `CombatAction` | `ReactionTriggered`、`Projectile` | 由 `5031` Interrupt 触发，但自身含弹射物事件，不能当作无伤害受击 Pose |
| `3023` | `CombatAction` | `ReactionTriggered`、`Projectile`、`TargetActionPunish` | 可由 `3710050` 或玩家使用道具触发，仍由战斗动作执行器负责伤害事件 |
| `3021`、`3032` | `CombatAction` | `ReservedCooldownOnly`、`DisabledByDefault` | 基础脚本没有直接播放入口；只保留追溯和后续变体覆盖，不进入默认候选池 |
| `5201` | `Locomotion` | `DiscreteMovement`、`CombatChainCompatible` | 既被 `SpinStep` 使用，也可作为 `ComboRepeat` 链段；`PrimaryUsage` 保持位移动作 |
| `5201`（请求 `5211`） | `Locomotion` | `DefensiveLongBackstep`、`UEApproximation` | 保留 `W_Step5211` 独立语义；`5201` 仅是 UE 表现近似并附加长后撤位移策略 |

### 后续数据契约

`GenichiroActionCatalog.lua` 和 `GenichiroAnimAssets.lua` 共享同一份用途事实，但分别暴露不同视图：

- ActionCatalog 只读取 `CombatAction`、`Reaction` 和离散 `Locomotion` 动作，不读取 `Unclassified`；
- AnimAssets 只登记任务 4.5 已验证的基础 Pose，以及调试时需要显示的白名单动作；
- `Phase` 分支只有在后续脚本或事件数据给出独立物理动画证据后才能新增；
- `PrimaryUsage` 决定执行器，`UsageTags` 只修饰选择条件，不能绕过资产、伤害事件或完成信号校验。

任务 3.5 结果为 **56/56 白名单资产存在，分类总数 3 + 48 + 5 + 0 = 56，102 个未引用资产保持隔离**。

## 任务 4.1：现有战斗动作提交能力审计

### 审计结论

现有 `USKCombatComponent` 足以作为弦一郎的**单段全身动作传输层**：Lua 可以停止 AI 导航、分配新的
`ActionSerial`、按对象路径播放 AnimSequence，并在加载失败时使动作失效。动态 Montage 的结束委托同时锁存
Montage 身份和启动时序列号，旧动作回调不会结束新动作；全身 Montage 活动期间，Movement Lua 也会关闭普通
Locomotion Root Motion 方向修正。

但现有接口尚不能独立完成全部弦一郎语义。它负责动作所有权和动画生命周期，不负责 ActionCatalog 连段编排、
Type 2 弹射物生成、通用 AI 反应分类或 `5211` 的额外长后撤位移。因此任务 6.2 应复用这组接口作为底座，
而不是继续调用只支持 `AutoLight` 的 `RequestAIAttack`。

### 接口能力矩阵

| 能力 | 现有证据 | 结论 | 弦一郎约束 |
|------|----------|------|------------|
| 停止导航 | `StopOwnerAIMovement()` 调用 AIController `StopMovement()` | ✅ 已具备 | 每个全身动作提交前调用，避免导航与 Root Motion 同帧争夺 |
| 分配动作身份 | `BeginCombatAction(State)` 递增并返回正整数 `ActionSerial` | ✅ 已具备 | Task 必须锁存返回值，不能只观察 ActionState |
| 按路径播放 | `PlayCombatAnimationByPath()` 同步加载 UAnimSequence，并在 `CombatFullBodySlot` 创建动态 Montage | ✅ 已具备 | ActionCatalog 提供完整对象路径；首次同步加载可能产生非功能性卡顿风险 |
| 播放失败回滚 | `InvalidateCombatAction(ExpectedSerial)` + `SetCombatActionState(Neutral)` | ✅ 已具备 | 必须按现有 `StartAction` 的事务顺序回滚，禁止留下已分配但未播放的动作 |
| 旧回调隔离 | Montage 结束时同时校验 `ActiveMontage` 与 `EndedActionSerial` | ✅ 已具备 | 后续动作不会被旧 Montage 结束回调错误完成 |
| 全身动画所有权 | `IsCombatFullBodyActionActive()` 以自有 Montage 为准，Movement Lua 在活动期间关闭普通移动方向修正 | ✅ 已具备 | 攻击、反应和离散位移动画都能取得单一 Root Motion 所有权 |
| 主动停止 | `StopCombatAnimation()` 只停止本组件自有 Montage | ✅ 已具备 | 停止后必须同时使 Serial 失效；是否合法中断由任务 4.2/4.3 决定 |
| 动画完成通知 | `OnCombatAnimationEnded(ActionSerial, bInterrupted)` | ✅ 已具备 | 委托是权威事件；轮询只作为 Task 故障兜底 |
| 多段连段 | 播放接口一次只接收一个 Sequence | 🟡 由 Lua 编排可补齐 | 同一 BT Task 按 ActionCatalog 顺序提交各段，不要求 C++ 理解 Act/Kengeki |
| 近战伤害窗口 | Lua 仅在 `LightAttack/HeavyAttack` 状态消费 `AttackHitbox` | 🟡 数据缺口 | 传输可用，但 Genichiro 曲线尚未写入，当前播放不会产生正确近战命中窗口 |
| 弹射物事件 | 尚无 Genichiro Type 2 事件消费者 | ❌ 缺口 | 18 个带弹射物动作只能播放表现，不能完整产生箭矢伤害 |
| 通用 AI Reaction 状态 | 枚举只有 `DeflectReaction`、`PostureBroken` 等具体状态 | ❌ 缺口 | 需要不附带玩家防御刀侧或架势打崩副作用的通用反应表达 |
| 长后撤位移 | 现有接口可播放 `5201`，Root Motion 方向修正接口不缩放总距离 | 🟡 待 4.5 | 若 `5201` 位移不足以表达 `5211`，需补通用、导航安全的位移策略接口 |

### 四类动画提交结果

| PrimaryUsage | 提交方式 | 4.1 结果 |
|--------------|----------|----------|
| `CombatAction` | `LightAttack/HeavyAttack` + 动态 Montage；连段由 Task 顺序编排 | 动画可提交；近战曲线和 Type 2 弹射物仍阻止完整伤害闭环 |
| `Reaction` | ReactionRouter 抢占后播放动态 Montage | 动画可提交；现有具体枚举有业务副作用，尚缺通用 AI Reaction 状态 |
| `Locomotion` | 停止导航后以离散全身动作播放 `5201/5202/5203` | 基础位移可提交；`5211` 额外位移需任务 4.5 验证 |
| `Phase` | 基础 `710000` 无独立资产 | 当前无需提交接口 |

`3092` 禁止使用 `PostureBroken` 作为临时状态：`HandleAnimationFinished()` 对该状态直接返回，真正的退出依赖
`bPostureBroken` 和最短锁定流程；只写枚举会造成动作状态无法回到 Neutral。`3100～3103` 也不应长期复用
`DeflectReaction`，因为其结束逻辑包含玩家防御姿态和刀侧恢复语义。任务 4.3 应决定通用 Reaction 是新增枚举，
还是由不污染玩家规则的通用动作域表达。

### 弦一郎执行器提交事务

任务 6.2 的 `SKGenichiroExecuteCombatAction` 必须遵循以下固定顺序：

```text
校验目标、ActionCatalog、ActionState 和当前 Montage
  → StopOwnerAIMovement
  → BeginCombatAction(映射后的通用 State)，锁存 ActionSerial
  → PlayCombatAnimationByPath
      ├─ 失败：InvalidateCombatAction(Serial) → Neutral → Task Failed
      └─ 成功：Task 保持 InProgress
  → 每次 Tick 先校验目标与 IsActionSerialValid(Serial)
  → 以 OnCombatAnimationEnded 为主、IsCombatAnimationPlaying 为兜底推进连段或完成
  → 最后一段结束后统一收敛 Neutral、冷却和 CombatMemory
```

现有 `SKCombatAttack.lua` 只提交 `RequestAIAttack("AutoLight")`，虽然记录了 `ActionSerial`，Tick 却没有再次调用
`IsActionSerialValid`。该 Task 可保留给通用 AI 轻攻击，但不能直接复用为弦一郎动作执行器；序列号、Abort 和完成
状态的完整生命周期归任务 4.2 继续审计。

任务 4.1 最终结论为：**核心提交接口可复用，不新增弦一郎专用 C++ 播放 API；完整语义执行仍有 2 个确定缺口
（通用 Reaction、Type 2 弹射物）和 1 个条件缺口（`5211` 长后撤距离）**。

## 任务 4.2：执行器生命周期、架势与拼刀状态审计

### 审计结论

现有底层已经能保证“旧动画不会完成新动作”：每次普通动作、弹反反应或架势打崩都会分配新的
`ActionSerial`，动态 Montage 回调还会校验 Montage 身份。架势打崩流程会先停止旧 Montage、使旧 Serial 失效，
再启动 `PostureBroken` 动作，并在“最短锁定时间已到且动画不再播放”后恢复 Neutral。

但当前 BehaviorTree Task 还不能正确区分“自己的动作自然完成”和“自己的动作被拼刀/打崩抢占”：
`SKCombatAttack.lua` 虽然锁存了 `ActionSerial`，Tick 没有调用 `IsActionSerialValid`；反应覆盖后它会一直等到新反应
动画结束，再把原攻击误报为成功。它的 Abort 和超时路径也只删除弱键状态，不停止、失效或显式移交活动动作。

因此任务 4.2 的结论是：**ActionSerial + Montage + PostureBroken 足以实现可靠生命周期，但现有 Task 尚未按该契约消费；
普通架势状态可覆盖，原版 Kengeki 语义状态不能覆盖。**

### 生命周期路径矩阵

| 路径 | 现有底层行为 | 执行器应返回 | 状态 |
|------|--------------|--------------|------|
| 单段自然结束 | Montage 回调清除活动引用；Combat Lua 下一次 Tick 将动作状态收敛为 Neutral | 当前 Serial 有效且 Neutral 后 `Succeeded` | ✅ 可表达 |
| 多段进入下一段 | Lua 可为下一段再次 Begin 并更新 ExpectedSerial；旧段回调因 Montage/Serial 不匹配失效 | 保持 `InProgress` | ✅ 可表达 |
| 播放失败 | Begin 后 Play 返回 false，可按 ExpectedSerial 失效并恢复 Neutral | `Failed` | ✅ 可表达 |
| 攻击被弹开 | `StartAttackDeflected()` 启动 `DeflectReaction` 并分配新 Serial | 原动作 `InterruptedByDeflect`，不得报成功 | 🟡 底层可表达，当前 Task 误判 |
| 架势打崩 | 停止旧 Montage、无条件失效旧 Serial，再启动 PostureBroken Serial | 原动作 `InterruptedByPostureBreak` | ✅ Serial 可判定 |
| 目标失效 | 当前 Task 只删除自身状态，活动 Montage 继续播放 | 按动作 AbortPolicy 取消或移交，不能留下黑板锁 | ❌ 当前未闭合 |
| BT 同步 Abort | Lua Abort 可同步清理，宿主立即返回 Aborted | 见下方所有权规则 | 🟡 宿主支持，动作策略缺失 |
| BT 异步 Abort | 宿主允许 Abort 返回 InProgress，但 Aborting 状态不再调用 Lua Tick | 只能由外部事件调用 `FinishLuaTask` | 🟡 可用但容易永久等待 |
| Task 超时 | 当前 Task 只返回 Failed，不处理仍活动的 Serial/Montage | 故障路径必须强制失效自己拥有的动作 | ❌ 当前未闭合 |
| Pawn/组件 EndPlay | C++ 无条件失效 Serial、停止自有 Montage并恢复 Neutral | Task 随树结束，不再提交结果 | ✅ 已具备 |

### 完成判定契约

弦一郎执行器不能仅以 `IsCombatAnimationPlaying()==false` 判成功。每次 Tick 必须按以下顺序判定：

```text
1. TaskState 和目标仍有效？否则进入 AbortPolicy
2. IsActionSerialValid(ExpectedSerial)？
   ├─ false + IsPostureBroken()       → InterruptedByPostureBreak
   ├─ false + DeflectReaction         → InterruptedByDeflect
   └─ false + 其他状态                → Superseded
3. 当前 Montage 仍播放？是 → InProgress
4. 当前动作是否还有下一段？是 → 提交下一段并更新 ExpectedSerial
5. CombatActionState 是否已收敛到 Neutral/声明的完成状态？
   ├─ 是 → Succeeded，写冷却和 CombatMemory
   └─ 否 → 等待一个状态收敛帧；超时按故障处理
```

`OnCombatAnimationEnded(ActionSerial, bInterrupted)` 仍是 Montage 结束的权威事件源，但它广播时 Combat Lua 可能尚未把
ActionState 收敛为 Neutral。事件监听器只能锁存“本段动画已结束”，最终 Task 结果仍需同时校验 ExpectedSerial 和
稳定状态。第一版也可以使用相同三条件的轮询实现；不得回退成只看动画是否播放。

### Abort 与动作所有权

`USekiroLuaBehaviorTreeTask` 使用节点实例并支持 Execute/Tick/Abort；Lua 的弱键 `TaskStates[task]` 模式可以隔离多个 AI。
同步 Abort 会立即停止 Task Tick；异步 Abort 返回 `InProgress` 后，宿主不会再调用 Lua Tick，只能依赖外部委托调用
`FinishLuaTask`。第一里程碑采用同步 Abort，避免引入未验证的委托解绑生命周期。

同步 Abort 按 ActionCatalog 的 `AbortPolicy` 处理：

| AbortPolicy | 处理 |
|-------------|------|
| `Immediate` | 若 ExpectedSerial 仍有效，`StopCombatAnimation` → `InvalidateCombatAction` → Neutral；清理 Task/Blackboard 状态 |
| `AtCancelWindow` | 记录组件级 PendingReaction/PendingAbort；Task 同步退出，活动动作继续由 CombatComponent 持有，不能由弱键 Task 状态继续管理 |
| `Uninterruptible` | Task 同步退出但不停止 Montage；Blackboard 的 `bActionLocked` 必须从 CombatComponent 状态派生，动画结束后自动解除 |
| `FaultTimeout` | 无视普通取消窗口，只清理由该 ExpectedSerial 拥有的 Montage，并输出稳定诊断 |

这要求 `bActionLocked` 不能只在 Task Execute/Abort 中手写 true/false；应由 `IsCombatFullBodyActionActive`、ActionState
和 ExpectedSerial 的可观察结果刷新，否则同步 Abort 后会留下永久黑板锁。

### 架势与拼刀覆盖率

| 状态源 | 已有能力 | 对弦一郎的覆盖 |
|--------|----------|----------------|
| 当前/最大架势与归一化值 | Getter + `OnPostureChanged` | ✅ 可写决策快照、阈值和调试信息 |
| 架势打崩布尔与通知 | `IsPostureBroken` + `OnPostureBrokenChanged` | ✅ 可抢占普通战术并等待恢复 |
| Guarded / Deflected / Hit 瞬时结果 | `ESKWeaponContactResult` 仅作为武器碰撞调用的同步返回值 | 🟡 当帧裁决成立，但 BT 无持久事件可读 |
| 攻击方被弹开反应 | `HandlePostureImpact("AttackDeflected")` 可立即启动反应 Montage | ✅ 表现抢占成立；原攻击 Task 必须靠 Serial 识别中断 |
| 连续拼刀次数 | 无通用计数或事件序列 | ❌ 无法复现 Kengeki 的连续次数权重 |
| `200200/200201` 等 Kengeki 信号 | 无持久语义事件 | ❌ ReactionRouter 无输入 |
| 拼刀交替状态与上次结果 | 当前只有玩家刀侧、DeflectStage 等局部战斗状态 | ❌ 不能替代 Genichiro CombatMemory 的 N3/N6 等状态 |

当前 `SKCombatReactionWait.lua` 只轮询 `PostureBroken/DeflectReaction` 并阻止下层决策，它没有锁存 Serial、反应类型、
攻击者或 Kengeki 信号，因此只能作为通用安全等待节点，不能充当弦一郎 ReactionRouter。

### 任务 4.2 后续约束

1. `SKGenichiroExecuteCombatAction` 必须在所有 Tick、完成、超时和 Abort 路径验证 ExpectedSerial。
2. 每段连招更新 ExpectedSerial；任何不匹配都作为明确中断结果，不能继续写普通成功冷却。
3. 架势打崩继续复用现有组件流程，不新增弦一郎专用 Posture C++ 状态。
4. Kengeki 瞬时结果必须在任务 4.3 转换为可消费的通用战斗事件；只轮询 ActionState 不足以恢复原版权重逻辑。
5. 第一版使用同步 Abort；若未来需要异步等待取消窗口，必须先建立组件级 Pending 状态和确定的委托解绑方案。
6. Task 弱键状态在 Succeeded、Failed、Abort、超时、目标失效和播放失败六类路径全部清除。

任务 4.2 最终结果为：**动作/架势生命周期底座可复用；现有 Task 生命周期实现不合格，Kengeki 状态覆盖不足，
两者分别进入任务 6.2 的执行器实现和任务 4.3 的通用事件缺口清单。**

## 任务 4.3：通用 AI 战斗事件能力缺口

### 审计结论

现有 ActionState、ActionSerial、Montage 和架势委托适合表达“持续状态”和“动作是否仍属于当前执行器”，但不能可靠
表达只存在于一次函数调用或单帧流程中的战斗事实。`ResolveIncomingWeaponContact()` 会同步返回 Hit、Guarded 或
Deflected，Lua 也能立即播放攻防反应；该结果随后没有进入 Blackboard、委托或队列，BehaviorTree 无法知道发生过
哪一种接触，也无法在抢占普通动作后恢复本次反应上下文。

因此任务 4.3 确认存在 C++ 通用能力缺口，**任务 4.4 必须实施有界、按序、由 AI Owner 单消费者读取的战斗事件队列**。
队列只解决可靠传输，不在 C++ 中选择弦一郎动作、不维护 Kengeki 权重，也不解释 Legacy ID。

### 能力分类矩阵

| 运行时事实 | 当前来源 | 现有 Lua/委托能否可靠表达 | 迁移决策 |
|------------|----------|--------------------------|----------|
| Montage 自然结束或中断 | `OnCombatAnimationEnded` + ActionSerial | ✅ 可以 | 继续由动作 Task 消费，不进入新队列 |
| 架势数值与打崩边沿 | Getter、`OnPostureChanged`、`OnPostureBrokenChanged` | ✅ 可以 | 继续作为持续状态和最高优先级抢占条件 |
| 目标获得、丢失或更换 | AI Perception 写 `TargetActor` Blackboard | ✅ 可以 | 由 Blackboard Decorator Abort，不重复发布战斗事件 |
| 武器接触 Hit/Guarded/Deflected | `ResolveIncomingWeaponContact` 同步返回值 | ❌ 调用结束后丢失 | 发布攻方/守方各自的 `WeaponContact` 事件，并保留接触结果、攻击类型和对方 Actor |
| 攻击威胁进入可响应窗口 | 当前只有真实接触和测试用 IncomingAttackContext | ❌ 接触时再防御已经过晚 | 新队列预留 `AttackThreat`；由攻击动作事件或命中前窗口生产者发布 |
| 实际承受伤害 | `AActor::TakeDamage`，CombatComponent 未监听 | ❌ BT 无伤害事实、来源和数值 | 新队列发布 `DamageReceived`；不能只用 ActionState 推断 |
| 弹射物撞击 | 尚无弦一郎弹射物消费者/命中生产者 | ❌ 无事件源 | 新队列预留 `ProjectileImpact`，实际弹射物模块落地时发布 |
| 目标使用道具或特殊动作 | InputManager 只有 Owner 本地一次性输入标记 | ❌ AI 不能安全消费目标输入 | 新队列预留 `TargetAction`；道具动作成立后由观察者路由发布，不能读取并清空玩家输入队列 |
| 外部强制反应或重规划 | 无通用入口 | ❌ 只能依赖下一次普通 Tick | 新队列提供 `ReactionRequested`、`ForceReplan`，具体优先级由 Lua ReactionRouter 决定 |
| `200200/200201` 等语义信号 | 原版共享助手返回，当前工程无生产者 | ❌ 普通 Deflected 不能无损区分六类信号 | 使用通用 `SemanticSignal` + 可选 EventTag；Legacy ID 只由 Lua 适配层写入和解释 |

`ProjectileImpact`、`TargetAction` 和 `SemanticSignal` 在任务 4.4 完成后只具备传输通道，不代表上游玩法生产者已经存在。
后续任务必须在真实攻击窗口、弹射物命中、道具动作成立或 Legacy 适配点发布事件；禁止为通过验收在 ReactionRouter
中按距离或随机数伪造这些输入。

### 可由 Lua CombatMemory 派生的状态

以下内容不新增为 C++ 字段：

- 连续拼刀次数：ReactionRouter 消费按序 `WeaponContact`/`SemanticSignal` 后更新并设置衰减或 Reset 条件；
- 上次拼刀结果、N3/N6 等分支交替：保存在弦一郎实例级 CombatMemory；
- Kengeki 冷却、候选权重和阶段修饰：继续由 TacticalProfile 与 ActionCatalog 管理；
- `200200/200201` 的 Legacy 追溯：只保存于 Lua Signal 映射和 EventTag，不进入 C++ 枚举名；
- 反应优先级：由 Lua ReactionRouter 统一比较，同帧多个事件不能由 C++ FIFO 顺序直接决定胜负。

这使通用事件队列只记录“发生了什么”，Lua 决定“弦一郎如何理解并回应”。

### 任务 4.4 的最小事件协议

`SKCombatTypes.h` 新增的类型应保持项目无关：

```cpp
UENUM(BlueprintType)
enum class ESKAICombatEventType : uint8
{
    None,
    AttackThreat,
    WeaponContact,
    DamageReceived,
    ProjectileImpact,
    TargetAction,
    ReactionRequested,
    ForceReplan,
    SemanticSignal
};

USTRUCT(BlueprintType)
struct FSKAICombatEvent
{
    int32 EventSerial;
    ESKAICombatEventType EventType;
    AActor* SourceActor;
    AActor* TargetActor;
    int32 RelatedActionSerial;
    ESKIncomingAttackType AttackType;
    ESKWeaponContactResult ContactResult;
    FName EventTag;
    float Magnitude;
    double EventTimeSeconds;
};
```

字段含义遵循以下约束：Owner CombatComponent 是事件收件人；`SourceActor` 是事件发起者；`TargetActor` 是本次事件
直接作用对象，通常等于 Owner；`RelatedActionSerial` 只关联动作身份，不承担事件排序；`EventTag` 可为空并保持
中性命名。结构中的实际 UPROPERTY、默认值和弱/强引用形式由任务 4.4 按 UE5.2/UHT 约束确定。

`USKCombatComponent` 提供：

```cpp
bool PublishAICombatEvent(const FSKAICombatEvent& Event);
bool ConsumeAICombatEvent(FSKAICombatEvent& OutEvent);
void ClearAICombatEvents();
int32 GetPendingAICombatEventCount() const;
```

队列契约：

1. 发布时由组件覆盖 `EventSerial` 和 `EventTimeSeconds`，调用者不能伪造排序；
2. FIFO 只保证事件到达顺序，ReactionRouter 每次应排空当前批次，再按既定优先级和 EventSerial 选取反应；
3. 队列容量固定为 32，溢出时丢弃最旧事件并输出限频诊断，防止无 BehaviorTree 消费者时无限增长；
4. 只有 Owner 的 ReactionRouter 消费该队列，动画蓝图、普通动作 Task 和其他 AI 不得竞争消费；
5. `EndPlay`、重新 Possess 或显式重置战斗上下文时清空队列；普通动作完成不得清空尚未处理的高优先级事件；
6. C++ 不写 Blackboard、不执行动作、不保存 Legacy ID，所有反应选择仍由 Lua 完成。

### 生产者接入边界

| 生产者 | 发布到 | 第一阶段职责 |
|--------|--------|--------------|
| `ASKWeapon::ResolveSweepHit` | 攻方与守方 CombatComponent | 在一次接触裁决后各发布一个 `WeaponContact`，保留双方视角所需 Actor 与 Result |
| Owner `OnTakeAnyDamage` | 受击者 CombatComponent | 发布 `DamageReceived`，保留伤害值、Causer 和当前 ActionSerial；与同帧 WeaponContact 由 Lua 合并而非 C++ 猜测 |
| 攻击动作窗口/Notify | 当前攻击目标的 CombatComponent | 发布 `AttackThreat`；只在目标已锁定且响应窗口有效时发送 |
| 弹射物命中解析 | 被命中者 CombatComponent | 发布 `ProjectileImpact`，并可同时由伤害系统产生 `DamageReceived` |
| 道具或特殊动作系统 | 观察者 AI 的 CombatComponent | 发布 `TargetAction`；路由必须显式确定观察者，不允许多个 AI 竞争消费玩家组件队列 |
| Lua Legacy 适配层 | 当前弦一郎 CombatComponent | 仅在上游确实识别信号时发布 `SemanticSignal(EventTag)` |

任务 4.4 的最小可交付范围是事件类型、组件有界队列、武器接触和 Owner 伤害两个已存在生产者，以及编译验证。
AttackThreat、ProjectileImpact、TargetAction 与精确 Kengeki EventTag 的生产者分别跟随对应玩法系统实现；队列 API
必须允许这些系统以后接入而无需修改 ReactionRouter 协议。

### 任务 4.3 最终决策

1. **执行任务 4.4**，新增通用 C++ 战斗事件队列；仅靠当前 Lua 轮询和委托不能恢复高优先级反应输入。
2. 不新增弦一郎专用 C++ Component、Kengeki 枚举或 Legacy 计数器。
3. 现有 Montage、ActionSerial、Posture 和 Blackboard 事件继续走原通道，避免队列重复表达持续状态。
4. 第一里程碑可基于真实 `WeaponContact` 建立通用拼刀反应与连续计数；在精确 EventTag 生产者出现前，不宣称
   已无损区分 `200200` 与 `200201`。
5. 任务 6.3 的 ReactionRouter 必须批量消费、按优先级选择并保留未处理的内存语义，不能把 FIFO 首项直接当最终决策。

## 任务 4.4.1：通用事件类型与有界 FIFO

### 实现结果

本任务已经在 `USKCombatComponent` 内建立独立于玩家输入队列的通用 AI 战斗事件收件箱，未接入任何生产者。
接口只负责发布、排序、保存和消费事实，不写 Blackboard、不启动动画，也不包含弦一郎、Kengeki 或 Legacy ID。

| 能力 | 实现 | 结果 |
|------|------|------|
| 通用类型 | `ESKAICombatEventType` 包含 None 与八类中性事件 | ✅ UHT 可见 |
| 通用有效负载 | Actor、动作 Serial、攻击/接触类型、Tag、Magnitude 和时间 | ✅ BlueprintType；Actor 使用 `TObjectPtr` |
| 发布 | `PublishAICombatEvent` 拒绝 None，覆盖事件 Serial 与世界时间 | ✅ 调用者不能伪造排序字段 |
| 顺序消费 | `ConsumeAICombatEvent` 弹出最旧事件，空队列重置输出 | ✅ FIFO |
| 容量 | 固定 32 条，满时淘汰最旧事件 | ✅ 单组件只记录一次溢出警告 |
| 序列号边界 | 正整数单调递增；达到 `int32` 上限后拒绝新事件 | ✅ 不回绕破坏顺序 |
| 生命周期 | `ClearAICombatEvents` 显式清理，`EndPlay` 自动清理 | ✅ 不影响 ActionSerial 或输入队列 |
| 只读观测 | `GetPendingAICombatEventCount` | ✅ 不消费队列 |

实现文件：

- `Source/Sekiro/Combat/SKCombatTypes.h`：新增事件枚举与结构；
- `Source/Sekiro/Combat/SKCombatComponent.h`：新增四个通用 Blueprint 接口与私有队列状态；
- `Source/Sekiro/Combat/SKCombatComponent.cpp`：实现 32 条有界 FIFO、组件本地序列号、限频诊断和 EndPlay 清理。

UBT 已使用 `SekiroEditor Win64 Development -Module=Sekiro` 验证，UHT、编译与链接产物均已生成；主 Agent 复核命令
返回退出码 0。三个源文件保持 UTF-8 with BOM，未启动 PIE。任务 4.4.2 才会接入 WeaponContact 与
Owner DamageReceived，当前队列为空是预期状态。

## 任务 4.4.2：WeaponContact 与 DamageReceived 生产者

### 实现结果

现有真实武器 Sweep 和 Actor 伤害边沿已经接入任务 4.4.1 的 Owner 事件收件箱。生产者只发布通用事实，原有
Lua 接触裁决、100 点基础伤害、命中去重与动作反应流程保持不变；事件队列不可用或发布失败时也不改变战斗结果。

### WeaponContact 时序

`ASKWeapon::ResolveSweepHit` 现在按以下顺序处理：

```text
解析 AttackerCombat / DefenderCombat / AttackType
  → Defender ResolveIncomingWeaponContact
  → Ignored：立即返回，不发布、不去重、不伤害
  → 向 AttackerCombat 发布 WeaponContact（若存在）
  → 向 DefenderCombat 发布 WeaponContact（若存在）
  → Hit：继续原有 100 点 TakeDamage
  → Guarded / Deflected：不产生伤害
  → 记录本攻击窗口已处理目标
```

攻守双方收到的接触有效负载保持一致：`SourceActor` 为攻击者、`TargetActor` 为被命中 Actor、`AttackType` 和
`ContactResult` 来自本次真实裁决，`RelatedActionSerial` 为攻击方动作序列号；攻守组件再各自分配本地
`EventSerial` 与时间戳。没有守方 CombatComponent 时，存在的攻击方组件仍会收到默认 Hit 接触事实。

### DamageReceived 时序

`USKCombatComponent::BeginPlay` 使用 `AddUniqueDynamic` 监听 Owner `OnTakeAnyDamage`，`EndPlay` 在父类生命周期前
解除监听并清空事件队列。回调只接受有限且大于零的实际伤害，事件字段为：

| 字段 | 来源 |
|------|------|
| `SourceActor` | `DamageCauser`；缺失时依次回退到 `InstigatedBy` Pawn 和 Controller |
| `TargetActor` | `DamagedActor`；缺失时回退到组件 Owner |
| `RelatedActionSerial` | 受击 Owner 当前 ActionSerial |
| `Magnitude` | 引擎确认的实际伤害值 |
| `EventTag` | 保持 None；C++ 不解释 `DamageType` |

由于 `TakeDamage` 同步触发 `OnTakeAnyDamage`，普通武器命中在守方 FIFO 中稳定形成：

```text
WeaponContact(Hit) → DamageReceived(100)
```

这是两个互补事实而非重复 Bug：前者用于拼刀/接触分类，后者覆盖非武器、弹射物及其他伤害来源。C++ 不尝试合并；
任务 6.3 的 ReactionRouter 必须批量消费当前事件，并按来源、目标、时间和优先级避免对同一次 Hit 播放两次反应。
Guarded 与 Deflected 只产生 WeaponContact，不产生 DamageReceived。

### 验证结果

- 修改范围：`SKCombatComponent.h/.cpp`、`SKWeapon.cpp`；
- 三个源码文件保持 UTF-8 with BOM，`git diff --check` 通过；
- `SekiroEditor Win64 Development -Module=Sekiro` 经 gameplay-programmer 和主 Agent 分别验证，最终退出码均为 0；
- 未启动 PIE，AttackThreat、ProjectileImpact、TargetAction 与 SemanticSignal 生产者仍保持未实现。

## 任务 4.4.3：事件传输层收尾验证

### 覆盖结论

任务 4.4 的完成范围是“通用事件能够由真实玩法点发布并可靠保存在 AI Owner 收件箱”，不是“弦一郎已经消费并响应
全部事件”。最终静态调用点审计结果如下：

| 层级 | 当前状态 | 结论 |
|------|----------|------|
| `ESKAICombatEventType` / `FSKAICombatEvent` | 八类中性事件和通用有效负载已通过 UHT | ✅ 传输协议完成 |
| Publish/FIFO/Clear/Count | 32 条有界队列、组件本地 Serial、世界时间与 EndPlay 清理 | ✅ 收件箱完成 |
| WeaponContact 生产者 | `SKWeapon.cpp` 向攻守双方发布 | ✅ 已连接真实 Sweep |
| DamageReceived 生产者 | Owner `OnTakeAnyDamage` 发布 | ✅ 覆盖通用 Actor 伤害 |
| Lua/BT 消费者 | 工程内尚无 `ConsumeAICombatEvent` 调用点 | ⏳ 按计划由任务 6.3 实现 |
| 其他六类生产者 | 当前没有发布调用点 | ⏳ 按下表分配，不属于 4.4 |

无消费者时队列最多保留最近 32 条并单次告警，不会无限增长；但旧高优先级事件可能被覆盖。因此任务 6.3 接入后必须
在 ReactionRouter 更新边界排空当前批次，不能等到普通战术动作结束后才消费。`EventSerial` 只在单个 CombatComponent
内有序，禁止比较不同 Actor 的 EventSerial 以推断全局先后。

### 后续生产者与消费者归属

| 事件 | 后续任务 | 接入边界 |
|------|----------|----------|
| `WeaponContact` | 6.3、6.5 | 6.3 批量消费；6.5 根据攻守视角和 Result 更新拼刀计数、弹刀反应与 CombatMemory |
| `DamageReceived` | 6.3、6.5 | 6.3 去重同批 Hit 接触；6.5 选择普通受击或更高优先级反应 |
| `AttackThreat` | 6.5 | 由真实攻击响应窗口/Notify 向锁定目标发布；接触发生后不得补造威胁事件 |
| `TargetAction` | 6.5 | 仅在道具动作实际成立后路由到观察者 AI，禁止消费玩家一次性输入标记 |
| `SemanticSignal` | 5.1、6.5、10.3 | 5.1 集中映射；有上游证据时发布；其余 Legacy 特殊信号在 10.3 扩展 |
| `ReactionRequested` | 6.3、6.5 | 通用反应适配器发布，ReactionRouter 决定优先级和取消策略 |
| `ForceReplan` | 6.3、10.3 | 6.3 消费并清理当前调度；剩余原版强制重规划信号在 10.3 接入 |
| `ProjectileImpact` | 6.7 | 由第一阶段 Type 2 弹射物真实命中点发布，不从 DamageReceived 或动画播放状态反推 |

Type 2 弹射物不与 4.4 通用队列耦合实现：任务 6.7 负责生成/命中玩法和 `ProjectileImpact` 生产者，事件传输仍复用
本组件接口。这样任务 4.4 不需要预先建立尚未设计的弦一郎专用 Projectile C++ 类型。

### 最终验证

- `PublishAICombatEvent` 仅有 WeaponContact 和 DamageReceived 两类真实生产调用点；未发现越界的 Legacy/弦一郎 C++ 逻辑；
- `ConsumeAICombatEvent` 当前只有接口定义，没有提前隐藏在通用 Task 或 AnimBlueprint 中消费；
- 四个相关源码文件均为 UTF-8 with BOM，`git diff --check` 无格式错误；
- `SekiroEditor Win64 Development -Module=Sekiro` 于 2026-08-24 再次返回退出码 0；
- 按项目验证规范未启动 PIE，运行时反应与事件批处理测试归任务 9.3 和经授权后的任务 11。

任务 4.4 至此完成。下一项运行时能力审计是任务 4.5；事件传输层的首个实际消费者由任务 6.3 实现。

## 任务 5.1：Legacy Signal 集中映射

### 实现结果

新增 `Content/Script/AI/Genichiro/GenichiroLegacySignals.lua`，把任务 3.3 的证据表转为可消费的纯 Lua 数据契约：

- 数值表严格覆盖 40 个 Legacy ID，包括一次性事件、持续状态、六类拼刀结果和 C 级保留项；
- 符号表保留 `INTERUPT_ParryTiming`、`INTERUPT_ShootImpact`、`INTERUPT_ActivateSpecialEffect`、
  `Interupt_Use_Item` 和 `COMMON_SP_EFFECT_PC_ATTACK_RUSH` 五个原始名称，不伪造数值 ID；
- `GetByLegacyID()` 与 `GetByName()` 是适配层的查询边界，后续 BehaviorTree/Task 只传播 `SemanticEvent`；
- `CanDriveBehavior()` 同时检查显式开关与确认度，六个 C 级条目和纯 SpecialEffect 分派器均失败关闭；
- 表中不包含动画路径、动作权重或 C++ 枚举，保持任务 5.2 ActionCatalog 与任务 5.3 TacticalProfile 的职责独立。

### 静态验收

`Script/tests/test_genichiro_legacy_signals.py` 固定校验 40 个数值 ID、5 个符号化输入、键值一致性、C 级失败关闭和
语义查询边界。2026-08-24 执行 4 项测试全部通过；`Script/check_lua_function_docs.py` 同时确认项目 Lua 函数文档
为 `428/428` 完整。模块为 UTF-8 无 BOM、无 Tab。本任务不需要 UE 资产生成、C++ 编译或 PIE。

## 任务 5.2：ActionCatalog 语义动作目录

### 实现结果

新增 `Content/Script/AI/Genichiro/GenichiroActionCatalog.lua`，将原脚本函数转译为 UE 可分层执行的数据契约，
而不是让单个 Lua Task 解释 `GOAL_COMMON_*`：

- 固化任务 3.5 的 56 个 UE 可播放动画资产及 `CombatAction/Reaction/Locomotion` 主用途；这里的资产号可能是
  已物化的 TAE 别名，不再错误称为物理 HKX 编号；
- 第一里程碑登记 11 个动作：6 个代表 Act、4 个 `200200/200201` 拼刀响应和 1 个 `5211` 语义别名；
- 每个动作显式保存 `BehaviorFamily`、Legacy 追溯、候选入口、执行模式、中断策略、移动策略、动画分支、
  伤害通道、能力依赖、冷却和 CombatMemory 写入；
- `Act23 → TimedStrafe` 只有 `NavigationStrafe`，不携带动画步骤；普通侧移由 BuildMoveGoal + UE MoveTo 执行；
- `ActionRequestResolutionByID` 显式区分行为请求、c9997 状态、TAE 时间线、动作源、事件源和 UE 可播放资产；
  其中 3013 从 3014 导入动作，3092 从 8603 导入动作与事件，3101～3103 共享 3100 动作源但保留各自事件源；
- `5211 → W_Step5211 → DefensiveLongBackstep → 5201 + NavigationSafeLongBackstep` 保留独立行为请求；由于
  c7100 TAE 没有 5211，同号动画不可伪造，当前 5201 仅作为带显式标记的 UE 表现近似；
- 含 Type 2 TAE 的步骤显式声明 `ProjectileImpact` 依赖，任务 6.7 完成前不得宣称完整伤害闭环；
- `GetAction()`、`GetActionByLegacy()`、`GetActionRequestResolution()`、`GetLegacyStateAlias()`、`GetAnimationPath()` 和
  `GetAnimationUsage()` 构成后续 Profile、Task 与诊断工具的只读查询边界。

当前目录只覆盖第一里程碑。剩余普通 Act 与 Kengeki 仍由任务 10.1/10.2 增量加入，避免数据尚未经过执行器闭环时
一次性开放全部候选。

### 第一里程碑动作

| UE Action ID | Legacy | 行为族 | 执行方式 |
|--------------|--------|--------|----------|
| `ClosePressureCombo` | Act01 | PressureAttack | 30/70 两条四段 CombatSequence |
| `CloseBackAwarenessAttack` | Act03 | PressureAttack | 接近后播放 3005，空间观察由上下文层负责 |
| `FarGapCloser` | Act10 | PressureAttack | UE 接近到目标范围后播放 3006 |
| `PhaseOpeningAssault` | Act15 | PhaseOpening | 3014→3015；阶段只修饰候选，不改变动画用途 |
| `TimedStrafe` | Act23 | Reposition | 纯 NavigationStrafe，记录最近侧移方向 |
| `DefensiveBackstepCounter` | Act24 | RepositionAttack | 5201 位移，可条件追加 3044 |
| `ClashAlternatingResponseA/B` | Kengeki01/04 | ClashResponse | 3050/3055 交替响应 |
| `ClashProjectileReposition` | Kengeki03 | ClashResponse | 3009 后由 UE 导航侧移 |
| `ClashBackstepCounter` | Kengeki20 | ClashResponse | 5202 位移后衔接 3007 |
| `DefensiveLongBackstep` | State 5211 | Reposition | 复用 5201 表现与 UE 长后撤策略，仅由反应入口提交 |

### 静态验收

`Script/tests/test_genichiro_action_catalog.py` 固定校验白名单 48/5/3 分类、11 个动作清单、动画步骤白名单、
纯导航 Act23、5211 别名和只读查询边界。2026-08-24 执行 6 项测试全部通过；Lua 函数文档检查为
`437/437` 完整。新增模块为 UTF-8 无 BOM、无 Tab、无尾随空白。本任务不需要 UE 资产生成、C++ 编译或 PIE。

## 任务 5.3～5.5：战术 Profile、CombatMemory 与确定性测试

新增 `GenichiroTacticalProfile.lua` 与 `GenichiroCombatMemory.lua`：

- Profile 将 3/5/7 Legacy 单位换算为 300/500/700 cm 的 Close/Mid/Far/VeryFar 档位；
- 第一里程碑恢复 Act01/03/10/15/23/24 的基础权重、`109900` 覆盖、阶段开场 600 权重、动作后强制侧移、
  左右/后方空间过滤、`200051/110621` 过滤和 Timer0/Timer3 语义冷却；
- 首组拼刀 Profile 恢复 `200200/200201` 的距离门槛、连续次数、K01/K04 交替及高连续 K03/K20 候选；
- 所有候选在进入随机池前检查 ActionCatalog、能力依赖和冷却，空池由 BT 进入 Hold，不伪造默认攻击；
- `SelectWeighted()` 使用显式 31 位 LCG 种子，不依赖全局 `math.random`，同一快照与种子可重放；
- CombatMemory 以 Owner 弱键保存冷却、短期状态、最近动作和决策序号，并按 OnStart/OnMovementSuccess/OnComplete
  提交目录副作用，失败或 Abort 不会错误提交完成冷却。

`Script/tests/lua/test_genichiro_decision.lua` 使用项目 UnLua 同版本 Lua 5.4.3 独立解释器实际加载模块并执行断言，
覆盖白名单别名、近距权重、空间失败、强制侧移、阶段开场、ProjectileImpact 能力失败关闭、拼刀低/高连续分支、
固定种子和冷却截止；2026-08-24 返回 `Genichiro decision tests: OK`。该验证不依赖 UE 世界或 PIE。

## 实施顺序

### 阶段 A：离线语义清单

1. 解析 `710000` 的 Act、Kengeki、权重、冷却和内部状态；
2. 输出原始 ID → 语义动作 → 动画/TAE 映射；
3. 标记已确认、推测和未知语义；
4. 不修改运行时代码。

### 阶段 B：Lua 动画蓝图闭环

1. 建立弦一郎 AnimAssets、Tuning 和专用 Lua AnimBlueprint；
2. 生成 Idle、锁定方向移动、转身与 `CombatFullBodySlot`；
3. 明确导航与战斗 Root Motion 的单一位移所有者；
4. Check、原地 Generate 并编译 `ABP_Genichiro`。

### 阶段 C：最小战术闭环

1. 实现 Context、Memory 和 TacticalProfile；
2. 在原生树中展开反应、阶段、战术、移动和动作分支；
3. 接近/横移/后撤使用 UE 导航，六个代表攻击使用语义战斗 Task；
4. 完成固定种子决策和 Task 生命周期测试。

### 阶段 D：战斗事件闭环

1. 接入受击、弹刀、拼刀、射击和使用道具事件；
2. 实现 PendingReaction 和动画取消约束；
3. 让动作状态、ActionSerial、Montage 和 BehaviorTree Abort 正确收敛；
4. 完成目标失效和阶段切换清理测试。

### 阶段 E：完整 `710000`

1. 录入全部常规 Act 对应的 UE 语义动作；
2. 录入全部有效 Kengeki 响应；
3. 对齐冷却、短期记忆和动画表现；
4. 生成正式 ABP/BT/BB 并编译验证。

### 阶段 F：变体

1. 比较 `710300/711000/711300` 的差异；
2. 确认变体对应关系；
3. 选择 Profile 继承或阶段分支；
4. 只录入差异数据，避免复制基础实现。

## 验证策略

### 无 PIE 验证

- Lua 文档与编码规范检查；
- 行为树与动画蓝图 Lua Check；
- BehaviorTree IR 与 AnimBlueprint IR 确定性测试；
- 固定种子权重选择测试；
- 动作目录、动画资产、目标骨架和 Slot 引用检查；
- Task Execute/Tick/Abort 状态清理测试；
- BehaviorTree/CombatComponent/AnimBlueprint 动作状态契约检查；
- 对应 C++、AnimBlueprint、Blueprint 和 BehaviorTree 编译。

### 决策回放测试

建立纯数据输入快照：

```text
Distance=250cm
TargetInFront=true
Phase=A
SP=300
AvailableSpace={Left=true, Right=false, Back=true}
Cooldowns={...}
Seed=12345
```

测试输出：

- 候选战术意图和语义动作集合；
- 每个意图/动作过滤原因；
- 最终权重；
- 选中动作；
- 记忆状态变化。

该测试不依赖世界和 PIE，是对齐原脚本最重要的回归测试。

### PIE 验证

仅在用户明确授权后执行：

- 四档距离行为分布；
- 场地边缘空间选择；
- 连续拼刀响应；
- 玩家喝药惩罚；
- 受击和射击中断；
- 阶段切换和目标死亡；
- Idle/移动/转身/动作/反应的 AnimGraph 切换；
- Root Motion、攻击框、取消窗口与导航一致性。

## 依赖与风险

依赖：

- `ASKAIController` 的感知、Blackboard 和 BehaviorTree 启动流程；
- `USKCombatComponent` 的命名 AI 攻击、动作序列号、动画状态和架势状态；
- `USekiroLuaBehaviorTreeTask` 的 Execute/Tick/Abort 生命周期；
- `SekiroLuaBehaviorTreeExtEditor` 的 Lua DSL → 原生资产生成链；
- `SekiroAnimBlueprintExtEditor` 的 Lua DSL → 原生 AnimBlueprint 生成链；
- `USKAnimInstance` 的移动、锁定和战斗表现字段；
- 弦一郎动画、TAE 事件、攻击框和取消窗口数据。

| 风险 | 影响 | 缓解 |
|------|------|------|
| 反编译变量名丢失 | 阶段和信号语义可能误判 | 保留 Legacy ID，分级标注确认度 |
| TAE Notify 不完整 | 攻击框和取消窗口失真 | 先做动作调度，再按 TAE 数据补帧级事件 |
| `GOAL_COMMON_*` 语义差异 | 移动手感不一致 | 转译为 UE MoveTo/MoveGoal/CharacterMovement 并实测校准 |
| 行为树过度展开 | 难读且难维护 | 树只展开 UE 语义层级，权重与紧密连段保留在数据层 |
| Lua Task 吞掉全部可视化 | 调试困难 | 战术意图和移动分支显式进入 Blackboard 与原生树 |
| 弦一郎骨架无法复用只狼 ABP | 生成失败或 T Pose | 新建专用 Lua ABP 与 AnimAssets，不继承只狼图资产引用 |
| Root Motion 与导航争夺位移 | 滑步、瞬移或持续推行 | 以 CombatComponent 动作状态切换单一位移所有权 |
| AnimBlueprint 承担战术逻辑 | 状态循环和职责耦合 | ABP 只读表现变量，战术只由 BT/Profile 决定 |
| Abort 强停动画 | 破坏战斗时序 | 动画取消权归战斗组件，Task 只请求中止 |
| 全动作一次迁移 | 问题定位困难 | 先做六动作垂直切片，再扩充 |
| 变体重复代码 | 后续难维护 | 共享树和执行器，Profile 只覆盖差异 |

## Agent 派发

| 子任务 ID | 任务描述 | 负责 Agent | 状态 |
|-----------|---------|-----------|------|
| 3.1 | Act/Kengeki 权重、条件、冷却和动作链清单 | 主 Agent | ✅ |
| 3.2 | Legacy 动画编号到 Genichiro UE 资产映射 | 主 Agent | ✅ |
| 3.3 | SpEffect/Interrupt 到 UE 语义事件映射 | 主 Agent | ✅ |
| 3.4～3.5 | TAE 覆盖率与 UE 动画用途离线映射 | 主 Agent | ✅ |
| 4.1～4.3 | Lua BT 与战斗接口能力审计 | 主 Agent | ✅ |
| 4.4 | 新增通用有界战斗事件队列与现有生产者 | gameplay-programmer | ✅ |
| 4.5 | Lua ABP、骨架、Slot 与 Root Motion 能力审计 | 主 Agent | ✅ |
| 5.1 | Legacy Signal 集中映射与离线契约测试 | 主 Agent | ✅ |
| 5.2 | 第一里程碑 ActionCatalog 与离线契约测试 | 主 Agent | ✅ |
| 5.3～5.5 | TacticalProfile、CombatMemory 与固定种子测试 | 主 Agent | ✅ |
| 6.1～6.6 | 弦一郎运行时 Lua Task | 主 Agent | ⬜ |
| 7.1～7.4 | 弦一郎 Lua 动画蓝图与资产生成 | 主 Agent | ⬜ |
| 8.1～8.4 | Lua 行为树定义、资产生成与编译 | 主 Agent | ⬜ |
| 9、10.5 | 规范、编译与回归审查 | review-agent | ⬜ |

Agent 派发必须遵守 `AGENTS.md`：`gameplay-programmer` 不修改插件，`plugin-programmer`
不修改 `Source/Sekiro/`，弦一郎专用编排不得进入插件 C++。

## 任务 6～10 实施结果

截至 2026-08-24，基础版 `710000` 已按 UE 分层架构完成静态迁移：

- `GenichiroActionCatalog` 登记 23 个有效 Act 实现和 29 个 Kengeki 实现；零权重实现、未注册 K47、异常冷却键均保留追溯，但默认候选池只接纳原版正权重入口；
- `GenichiroTacticalProfile` 覆盖四档距离权重、目标特殊状态、空间过滤、Timer 冷却、六类 Kengeki 信号矩阵和 K32/K33 交替；K38 使用 `BossPhase` 表达基础弦一郎已跨阶段次数；
- BehaviorTree 为 30 Key / 44 主节点结构，优先级固定为 Reaction → Phase → Tactical，导航统一由原生 `MoveTo(MoveGoal)` 承担，Lua Task 只负责快照、选择、离散动作和提交点；
- `ABP_Genichiro` 使用 7 状态锁定移动图、`CombatFullBodySlot` 和 `RootMotionFromMontagesOnly`；战斗动作仍由 CombatComponent 动态 Montage 与 ActionSerial 统一管理；
- Type 2 事件从 c7100 TAE 提取 20 个动画、23 个发射窗口，由通用 `ASKAIBattleProjectile` 及项目侧箭矢 Blueprint 产生 `ProjectileImpact`；
- 外部 SpEffect、Boss 生命属性和目标朝向判定仍是数据生产者边界。它们通过集中 Legacy 映射与 Blackboard Key 接入，不在 C++ 或 BehaviorTree 节点中硬编码弦一郎编号；无生产者时生命比例安全默认为 1，避免误触发低生命分支。

本轮验收仅包含 Lua 回归、资产契约、原生 Blueprint/BehaviorTree 生成和 UBT 编译；按项目规则没有执行 PIE。任务 11 保持未开始，等待用户明确授权。

## 最终交付物

- 可从 Lua 源确定性生成的 `ABP_Genichiro`、`BB_Genichiro`、`BT_Genichiro`；
- `710000` 动作、权重、冷却、信号和动画映射清单；
- 弦一郎 AnimAssets、Locomotion StateMachine、Combat Slot 与 Root Motion 契约；
- 弦一郎 ActionCatalog、TacticalProfile 和 CombatMemory；
- 上下文更新、意图选择、MoveGoal、战斗动作和反应路由 Lua Task；
- 必要的通用战斗事件接口；
- 固定种子决策测试、Task 生命周期测试和资产引用检查；
- 经授权后的 PIE 行为验收记录；
- 后续 `710300/711000/711300` 差异 Profile。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-25 | 完成一致性复核 V2～V7：建立 40 个原版 Golden 场景，TacticalProfile 为 32 一致/8 差异；验证 60 动作、52 冷却、17 状态写入；确认 Blackboard 17/30 有 Producer、事件 3/8 有真实 Producer，且当前 BehaviorTree 无动作运行中的即时抢占路径；按约定暂停在需要用户操作编辑器的 V8 |
| 2026-08-25 | 启动原版/UE 一致性分阶段复核 V1：新增机器可读清单，固定原版 `710000` 权威源哈希、20 项行为契约、四级一致性分类与 OFFLINE/EDITOR_STATIC/PIE 门禁；编辑器和 PIE 阶段明确交由用户执行并反馈 |
| 2026-08-21 | 创建原版 AI 到 UE 行为树的初始混合迁移技术方案 |
| 2026-08-21 | 按 `/plan` 技能模板补齐文件范围、API、数据流、依赖、Agent 派发和变更记录 |
| 2026-08-21 | 改为 Lua BehaviorTree + Lua AnimBlueprint 双源、UE 原生运行方案；原版 Lua 仅作行为参考和权重校准 |
| 2026-08-21 | 完成任务 3.1：新增 `710000` 的 23 个 Act、29 个 Kengeki、权重矩阵、全局过滤、冷却和短期状态清单 |
| 2026-08-21 | 完成任务 3.2：新增 50 个 Legacy 动画编号的 UE 资产映射并确认 `3013`、`3092` 两项缺失 |
| 2026-08-21 | 修复任务 3.2：支持 TAE `ImportHKX/ImportOtherAnim`，生成 `3013`、`3092` 并将映射收敛为 50/50 |
| 2026-08-21 | 完成任务 3.3：建立 SpEffect/Interrupt 的 A/B/C 语义映射，补导 `3101/3102/3103`；当时暂将 `5211` 列为未解析共享动画阻塞 |
| 2026-08-21 | 修复 `5211` 分类：确认其为 EzState/动作请求，映射为 `DefensiveLongBackstep`，复用 `5201` 表现并由 UE 位移策略承载长后撤 |
| 2026-08-21 | 完成任务 3.5：将 56 个 `710000` 白名单资产归为 3 个离散 Locomotion、48 个 CombatAction、5 个 Reaction、0 个独立 Phase，隔离其余 102 个未引用资产 |
| 2026-08-21 | 完成任务 4.1：确认 Begin/Play/ActionSerial 可复用为单段动作传输层，记录通用 Reaction、Type 2 弹射物两个确定缺口和 `5211` 位移条件缺口 |
| 2026-08-21 | 完成任务 4.2：确认 ActionSerial/Montage/PostureBroken 可闭合底层生命周期；识别当前 Task 未校验 Serial、Abort/超时未移交动作和 Kengeki 无持久语义事件三类缺口 |
| 2026-08-24 | 完成任务 4.5：确认 Lua AnimBlueprint 编译器、弦一郎 Skeleton、Slot 与 Root Motion 能力边界；将缺失 `CombatFullBodySlot` 和占位 ABP 修复下沉到任务 7.3/7.4 |
| 2026-08-24 | 完成任务 5.1：实现 40 个数值 Legacy Signal、5 个符号化输入的纯 Lua 集中映射、失败关闭查询边界和 4 项离线契约测试 |
| 2026-08-24 | 完成任务 5.2：实现 56 项动画白名单、6 个代表 Act、4 个首组 Kengeki 响应及 `5211→5201` 语义别名；6 项 ActionCatalog 契约测试通过 |
| 2026-08-25 | 修复动作索引模型：拆分行为请求、c9997 状态、TAE 时间线、动作源、事件源和 UE 可播放资产；纠正 3013/3092/3101～3103 引用元数据，并将 5211 明确为 `W_Step5211` 的 UE 表现近似 |
| 2026-08-24 | 完成任务 5.3～5.5：实现距离/阶段/空间/冷却/拼刀 Profile、弱键 CombatMemory 与显式种子选择；Lua 5.4.3 纯数据回归测试通过 |
| 2026-08-21 | 完成任务 4.3：确认瞬时战斗事实需要 Owner 单消费者有界事件队列；定义通用事件协议、生产者边界与 Lua CombatMemory 职责，并决定执行任务 4.4 |
| 2026-08-21 | 完成任务 4.4.1：实现通用事件枚举、有效负载和 32 条有界 FIFO；事件 Serial/时间由组件生成，EndPlay 清理，Sekiro 模块编译通过 |
| 2026-08-24 | 完成任务 4.4.2：真实 WeaponContact 在 TakeDamage 前向攻守双方发布，Owner AnyDamage 发布 DamageReceived；事件时序、解绑与模块编译验证通过 |
| 2026-08-24 | 完成任务 4.4.3：复核事件协议、调用点、BOM 和 Sekiro 模块编译；关闭 4.4，并将 ReactionRouter、威胁、道具、Legacy 和 Type 2 弹射物接入分配到后续任务 |
| 2026-08-24 | 完成任务 6～10：落地完整 Lua BT/AnimBP、23 Act/29 Kengeki 数据、Type 2 弹射物、UE 资产生成和无 PIE 静态验收；明确外部属性/SpEffect 生产者边界 |
| 2026-08-24 | 完成 AnimBlueprint 资产收尾：移除旧占位状态机，确认落盘资产只含 Lua 生成的 7 状态移动图且编译 UpToDate |
| 2026-08-25 | 全量修复基础动画映射：以 c9997 行为状态与 c7100 TAE 为权威，将误用的坠落/受击动画替换为 400000、405001～405003、405010、405400/405401，并增加显式解析记录与资产回归测试 |
