# Lua 动画蓝图编写手册

本文说明如何在当前项目里用 Lua 编写动画蓝图逻辑。目标是：动画蓝图只负责提供 UnLua 绑定和一个 AnimGraph Host 入口，Lua 负责状态机、状态切换、动画资源选择和必要的播放参数。主角地面移动已经切到 RootMotion 思路，Walk/Run/Sprint 不再用速度缩放播放速率。

当前示例是主角 Sekiro 的 `ABP_Sekiro`：

- 动画蓝图资产：`/Game/Characters/Sekiro/ABP_Sekiro`
- Lua 主模块：`Content/Script/Animation/Sekiro/ABP_Sekiro.lua`
- Lua 状态机：`Content/Script/Animation/Sekiro/GroundLocomotion.lua`
- 动画资源表：`Content/Script/Animation/Sekiro/AnimAssets.lua`
- 动画层库：`Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Library.lua`
- 通用动画蓝图宿主基类：`Content/Script/Animation/Base/LuaAnimBlueprint.lua`
- 通用状态机基类：`Content/Script/Animation/Base/LuaAnimStateMachine.lua`

## 一、整体运行方式

一帧动画更新大致是这样走的：

1. `USKAnimInstance::NativeUpdateAnimation` 更新 C++ 动画变量，例如 `Speed`、`MovementInputAmount`、`GroundedEntryState`、`Gait`、`TurnAngle`。
2. `USekiroLuaAnimInstance::UpdateLuaDrivenAnimation` 调用绑定到动画蓝图的 Lua 模块。
3. Lua 主模块继承 `LuaAnimBlueprint`，通过 `AnimGraph()` 声明根输出，并把更新分发到具体状态机。
4. 状态机在 `UpdateAnimation_<State>` 中调用 `PlaySequence` / `SampleBlendSpace1D`，基类通过 C++ Sequence / BlendSpace Pose 接口提交当前 Pose。RootMotion Locomotion 优先直接播放 Sequence，让动画资源自身决定位移和步频。
5. C++ 把 Lua 提交的 Pose 转换成 `FSekiroLuaAnimSnapshot`。
6. AnimGraph 中的 `Sekiro Lua Anim Blueprint Host` 节点读取快照，在动画线程评估动画序列或 BlendSpace。

Lua 不直接在动画线程里跑，也不直接生成 UE 动画节点。Lua 的职责是每帧在状态逻辑里调用“播放序列 / 采样 BlendSpace”这些 C++ 包装接口，给出“当前应该是什么状态、播放哪个动画、怎么混合”。除非明确在做 InPlace BlendSpace，Locomotion 不应该用角色速度去推导播放速率。

## 二、动画蓝图需要怎么接

动画蓝图必须满足三个条件：

1. 父类继承 `USekiroLuaAnimInstance`，项目里主角用的是 `USKAnimInstance`。
2. 实现 UnLua 的 `GetModuleName`，返回 Lua 主模块名，例如：

```text
Animation.Sekiro.ABP_Sekiro
```

3. AnimGraph 里放置 `Sekiro Lua Anim Blueprint Host` 节点，并把它接到最终 Pose 输出。作为根 Host 使用时，`LayerName` 可以留空，节点会读取 Lua 主模块设置的默认输出；也可以显式填写层名，例如：

```text
GroundLocomotion
```

模块名不是文件路径。它是从 `Content/Script` 开始的 Lua require 名：

```text
Content/Script/Animation/Sekiro/ABP_Sekiro.lua
=> Animation.Sekiro.ABP_Sekiro
```

## 三、推荐目录规范

不同角色的动画 Lua 放在 `Content/Script/Animation/<角色名>/` 下。通用基类放在 `Animation/Base`，不要放到具体角色目录。

推荐结构：

```text
Content/Script/Animation/
  Base/
    LuaAnimBlueprint.lua
    LuaAnimStateMachine.lua
  Sekiro/
    ABP_Sekiro.lua
    AnimAssets.lua
    GroundLocomotion.lua
    Layer/
      GroundLocomotion/
        Library.lua
  EnemyA/
    ABP_EnemyA.lua
    AnimAssets.lua
    GroundLocomotion.lua
```

约定：

- `ABP_xxx.lua` 是动画蓝图主模块，继承 `LuaAnimBlueprint`，负责创建状态机并声明 `AnimGraph()` 根输出。
- `AnimAssets.lua` 只放动画资源路径和清晰命名。
- `GroundLocomotion.lua` 这种文件是具体状态机类，负责状态逻辑。
- `Layer/<LayerName>/Library.lua` 放某一动画层的状态枚举、调参、动画设置等数据。
- 不再使用旧的 `Rule` 目录。

## 四、主模块怎么写

主模块是动画蓝图的入口。它继承 `Animation.Base.LuaAnimBlueprint`，负责导入状态机、创建 Lua 动画层，并用 `AnimGraph()` 声明根输出。

当前 `ABP_Sekiro.lua` 的核心形态如下：

```lua
local class = require("Animation.Base.Class")
local LuaAnimBlueprint = require("Animation.Base.LuaAnimBlueprint")

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local GroundLocomotionClass = require("Animation.Sekiro.GroundLocomotion")

local ABP_Sekiro = class("ABP_Sekiro", LuaAnimBlueprint, {
    AnimAssets = AnimAssets,
})

function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, nil, true)
end

function ABP_Sekiro:AnimGraph()
    return self:OutputPose(self:UseStateMachine(self.GroundLocomotion))
end

local Runtime = ABP_Sekiro()
return Runtime:Export()
```

`LuaAnimBlueprint` 已经封装了 `Configure`、`UpdateLayer`、`Update`、层注册和默认输出；`Runtime:Export()` 会把实例导出成 C++/UnLua 可以直接点调用的模块表。

实际编写时通常只改两处：

- `require` 新的动画层或子状态机文件。
- 在 `Initialize()` 里 `CreateStateMachine(...)`。
- 在 `AnimGraph()` 里声明最终输出。

如果一个动画蓝图有多个动画层，例如 `GroundLocomotion`、`UpperBodyCombat`、`AdditiveAim`，主模块可以这样组织：

```lua
function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, nil, true)
    self.UpperBodyCombat = self:CreateStateMachine(UpperBodyCombatClass)
    self.AdditiveAim = self:CreateStateMachine(AdditiveAimClass)
end
```

当前根输出由 `AnimGraph()` 返回。更复杂的 Slot、Layered Blend、Inertialization 会继续扩展为 Lua AnimBlueprint DSL；在此之前，Host 节点会读取默认输出层的快照。

## 五、动画资源表怎么写

动画资源统一放在 `AnimAssets.lua`，不要在状态机逻辑里到处硬写路径。

示例：

```lua
local M = {}

M.Locomotion = {
    Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",
    Walk_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000100.Anim_Sekiro_a000_000100",
    Run_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000400.Anim_Sekiro_a000_000400",
    Walk_Run_Blend_Forward_1D = "/Game/Characters/Sekiro/Anim/Sekiro_CycleForward1D.Sekiro_CycleForward1D",
    Walk_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000300.Anim_Sekiro_a000_000300",
    Run_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000600.Anim_Sekiro_a000_000600",
}

return M
```

状态机里直接使用你在 `AnimAssets.lua` 里定义的清晰动画名，例如 `Anim.Run_Forward_Start`、`Anim.Walk_Run_Blend_Forward_1D`。不要再额外写 `Start = Run_Forward_Start` 这种状态名到动画名的别名映射；状态是状态，动画资源名是动画资源名。

路径要写 UE 对象路径，不是磁盘路径：

```text
正确：/Game/Characters/Sekiro/Animations/Anim_xxx.Anim_xxx
错误：F:/ProjectAI/Sekiro/Content/Characters/...
```

## 六、动画层库怎么写

动画层库是某个状态机的数据定义。以 `GroundLocomotion` 为例：

```lua
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "GroundLocomotion"

M.State = {
    Idle = "Idle",
    Start = "Start",
    Cycle = "Cycle",
    Stop = "Stop",
}

M.Tuning = {
    StartToCycleMinTime = 0.12,
    StopToIdleMinTime = 0.10,
}

M.Assets = AnimAssets.Locomotion

M.AnimationSettings = {
    [M.State.Idle] = { BlendTime = 0.12, Loop = true },
    [M.State.Start] = { BlendTime = 0.12, Loop = false },
    [M.State.Cycle] = { BlendTime = 0.16, Loop = true },
    [M.State.Stop] = { BlendTime = 0.12, Loop = false },
}

return M
```

建议把下面这些数据放在库里：

- `LayerName`：动画层名，必须和 AnimGraph 节点的 `LayerName` 对应。
- `State`：状态名枚举，避免到处手写字符串。
- `Tuning`：切换时间、速度阈值、转身角度等调参。
- `Assets`：该层能用到的动画资源表。
- `AnimationSettings`：每个状态的默认混合时间和循环设置。

## 七、状态机类怎么写

具体状态机继承 `Animation.Base.LuaAnimStateMachine`：

```lua
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")
local LayerLibrary = require("Animation.Sekiro.Layer.GroundLocomotion.Library")

local State = LayerLibrary.State
local Tuning = LayerLibrary.Tuning

local GroundLocomotion = class("GroundLocomotion", BaseStateMachine, {
    LayerName = LayerLibrary.LayerName,
    EntryState = State.Idle,
    State = State,
    States = State,
    StateList = LayerLibrary.StateList,
    Tuning = Tuning,
    Assets = LayerLibrary.Assets,
    AnimationSettings = LayerLibrary.AnimationSettings,
})

return GroundLocomotion
```

`class("子类名", 父类, 定义表)` 是当前推荐写法，风格接近项目里常见的 Lua class / UnLua 类继承。类可以像函数一样实例化：

```lua
local GroundLocomotionClass = require("Animation.Sekiro.GroundLocomotion")
local GroundLocomotion = GroundLocomotionClass()
```

通常不需要在子类里调用父类函数。确实要复用基类逻辑时使用：

```lua
GroundLocomotion.super.Configure(self, context)
```

旧的 `Extend`、`:new()`、`CallSuper` 仍然兼容，但新代码优先使用 `class("X", Base)`、`ClassName()`、`ClassName.super.Method(self, ...)`。

状态机通常只需要实现三类内容：

1. `StateList` 和 `EntryState`：声明有哪些状态，以及默认入口状态。
2. `UpdateAnimation_<State>`：指定某个状态播放什么动画、PlayRate、BlendSpace 输入。
3. `CanEnter_<From>_<To>`：单条 Transition 的进入条件。

基类会先把 C++ 动画变量同步到 `self.Speed`、`self.Gait`、`self.GroundedEntryState` 等实例字段。业务函数里不需要传 `facts`，直接读 `self`，再调用 `self:PlaySequence(...)` 或 `self:SampleBlendSpace1D(...)` 提交 Pose。

状态更新流程由基类封装：第一次更新默认进入 `EntryState`，之后基类会根据 `CanEnter_<From>_<To>` 函数名自动推断 Transition，并在当前状态下按函数声明顺序尝试切换。子类不需要写 `UpdateState_<State>`。

### 代码风格约定

动画 Lua 统一使用接近项目 UI Lua 的 class 风格：

```lua
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")

local MyLayer = class("MyLayer", BaseStateMachine, {
    LayerName = "MyLayer",
    EntryState = State.Idle,
    State = State,
    StateList = {
        State.Idle,
        State.Move,
    },
})

function MyLayer:UpdateAnimation_Idle()
    return self:PlaySequence(Anim.Idle)
end
```

函数命名规则：

```text
Transition: CanEnter_<来源状态>_<目标状态>
动画更新:   UpdateAnimation_<状态>
```

示例：

```lua
function MyLayer:CanEnter_Idle_Move()
    return self:WantsMove()
end
```

基类仍兼容旧的 `CanEnterMoveFromIdle`、`UpdateIdleState` 和 `UpdateIdleAnimation`，但新代码不要再使用旧命名，也不要再手写 `UpdateState_<State>`。

## 八、C++ 变量和函数怎么用

C++ 暴露的 AnimInstance 变量会被基类同步到当前 Lua 状态机实例，因此业务逻辑里优先直接读 `self.Speed`、`self.Gait`、`self.GroundedEntryState` 这些和 C++ 同名的字段。

```lua
function GroundLocomotion:IsMoving()
    return self.bIsMoving == true or (self.Speed or 0) > 3
end

function GroundLocomotion:WantsCycle()
    return self.bHasMovementInput == true and self:IsMoving()
end
```

基类默认会同步这些实例字段：

- `self.CurrentState` / `self.CurrentStateName`：当前 Lua 状态名。
- `self.StateTime`：当前状态已播放时间。
- `self.DeltaSeconds`：本帧 DeltaSeconds。
- `self.HasPose`：该动画层是否已经有有效姿势。

如果某个字段没有被运行时上下文同步，仍然可以用读取函数兜底：

```lua
self:ReadContextNumber(context, "Speed", 0)
self:ReadContextBool(context, "bIsDodging", false)
self:ReadContextField(context, "GroundedEntryState")
```

字段名必须和 AnimInstance 里的 UPROPERTY 名一致，例如 `USKAnimInstance` 里有 `Speed`、`Gait`、`DesiredGait`、`GroundedEntryState`。

如果要调用 AnimInstance 上的 C++/蓝图函数，可以直接用类内方法风格，或者显式调用 `CallCpp`：

```lua
local owner = self:GetOwningActor()
local result = self:CallCpp("SomeBlueprintCallableFunction", arg0, arg1)
```

当状态机自身没有同名 Lua 函数时，`self:GetOwningActor()` 这种调用会尝试转发到当前 AnimInstance。为了保持动画线程安全，这些函数仍然只在游戏线程 Lua 更新阶段调用，AnimGraph 动画线程只读取 C++ 快照。

## 九、动画更新函数怎么写

函数命名规则：

```text
UpdateAnimation_状态名
```

例如状态叫 `Cycle`，函数就是：

```lua
function GroundLocomotion:UpdateAnimation_Cycle()
    if self.DesiredGait == "Walk" then
        return self:PlaySequence(Anim.Walk_Forward_Loop)
    end

    return self:PlaySequence(Anim.Run_Forward_Loop)
end
```

`UpdateAnimation_<State>` 里只写这个状态自己的动画逻辑。最简单的状态播放一个 Sequence：

```lua
function GroundLocomotion:UpdateAnimation_Idle()
    return self:PlaySequence(Anim.Idle)
end
```

如果要按逻辑选择不同动画，直接调用不同的播放函数，不要拼接字符串：

```lua
function GroundLocomotion:UpdateAnimation_Start()
    if self.DesiredGait == "Walk" then
        return self:PlaySequence(Anim.Walk_Forward_Start)
    end

    return self:PlaySequence(Anim.Run_Forward_Start)
end
```

如果要采样 BlendSpace，使用 `SampleBlendSpace1D`。这类写法适合明确的 InPlace 或上半身参数混合；RootMotion Locomotion 不建议把速度再映射成 PlayRate：

```lua
function GroundLocomotion:UpdateAnimation_Cycle()
    return self:SampleBlendSpace1D(Anim.Some_Aim_Offset_1D, self.AimYawDelta or 0)
end
```

`PlaySequence` / `SampleBlendSpace1D` 内部分别通过 C++ `SetLuaAnimSequencePoseByPath` / `SetLuaAnimBlendSpacePoseByPath` 提交当前状态名、动画路径、混合时间、播放速率、循环和 BlendSpace 输入。Lua 旧的返回字符串或返回表方式仍然兼容，但新状态机不要再使用它。

注意：Lua 仍然不直接生成 AnimGraph 内部节点。状态内复杂混合、Slot、Layered Blend、Inertialization 等应该放在 AnimGraph 里预置节点，Lua 只调用 C++ 接口驱动它们所需的状态、动画资源、BlendSpace 输入或后续扩展参数。

### RootMotion Locomotion 写法

RootMotion 状态机的核心规则：

- `UpdateAnimation_<State>` 直接选择明确的动画资源，例如 `Anim.Run_Forward_Start`、`Anim.Run_Left_Loop`。
- 不在 Lua 里用 `Speed` 计算 `PlayRate`；Walk、Run、Sprint 的距离和节奏由动画根运动决定。
- Start/Stop/SprintStart/SprintStop 这类非循环状态优先用 `MoveTransition` 曲线决定何时进入下一段；曲线缺失时才用归一化时间兜底。
- 非锁定移动默认播放 Forward 循环；当输入方向和角色朝向差距过大时，先进入 Start 并播放 `Walk_Left_Turn` / `Run_Right_Turn` 这类转身起步动画。
- 锁定移动使用 Forward/Back/Left/Right 四方向循环和停止动画，因为角色朝向目标，移动方向相对角色朝向可能不同。
- Sprint 只有 Forward Loop，开始时按屏幕输入选择 `Sprint_*_Turn_Start`，进入循环后不再按左右后方向切换循环动画。

## 十、Transition 怎么写

每条 Transition 推荐独立写成一个函数，命名规则：

```text
CanEnter_来源状态_目标状态
```

例如：

```lua
function GroundLocomotion:CanEnter_Start_Cycle()
    return self:WantsCycle() and self.StateTime >= Tuning.StartToCycleMinTime
end

function GroundLocomotion:CanEnter_Cycle_Stop()
    return self:WantsStop() or self:WantsIdle()
end
```

不需要再写 `UpdateState_Start`。基类会自动从函数名推断：

```text
CanEnter_Start_Cycle => Start -> Cycle
CanEnter_Cycle_Stop  => Cycle -> Stop
```

同一个来源状态下，Transition 会按函数声明顺序尝试。比如 `CanEnter_Start_Stop` 写在 `CanEnter_Start_Cycle` 前面，就会先判断 `Start -> Stop`，再判断 `Start -> Cycle`。

如果没有任何 Transition 命中，基类会自动保持当前状态，并调用当前状态的 `UpdateAnimation_<State>`。

基类内部进入新状态等价于：

```lua
self:Enter(State.Cycle)
```

保持当前状态用：

```lua
self:Keep(State.Cycle)
```

区别是：

- `Enter` 会让快照重置时间。
- `Keep` 不重置时间，只更新动画参数。

## 十一、状态机入口怎么写

入口状态写在类定义里：

```lua
local GroundLocomotion = class("GroundLocomotion", BaseStateMachine, {
    EntryState = State.Idle,
    StateList = LayerLibrary.StateList,
})
```

当状态机还没有有效姿势时，基类会自动 `Enter(EntryState)`。之后所有状态切换都由 `CanEnter_<From>_<To>` 函数驱动。

如果需要“任意状态都能进入某个状态”，使用 `Any` 作为来源状态：

```lua
function GroundLocomotion:CanEnter_Any_Idle()
    return self.IsInAir
end
```

这表示无论当前在哪个状态，只要 `self.IsInAir` 为 true，就可以切回 `Idle`。

## 十二、子状态机和动画层怎么拆

这里的“状态机”不等于一个动画蓝图一个 Lua 文件。推荐拆法是：

- 一个动画蓝图一个主模块，例如 `Animation.Sekiro.ABP_Sekiro`。
- 一个动画层或子状态机一个 Lua 类，例如 `Animation.Sekiro.GroundLocomotion`。
- 一个动画层的数据放一个 `Layer/<LayerName>/Library.lua`。
- 动画资源集中放角色级 `AnimAssets.lua`。

例如后续要加上半身战斗层：

```text
Content/Script/Animation/Sekiro/
  ABP_Sekiro.lua
  AnimAssets.lua
  UpperBodyCombat.lua
  Layer/
    UpperBodyCombat/
      Library.lua
```

然后在 `ABP_Sekiro.lua` 里创建：

```lua
local UpperBodyCombatClass = require("Animation.Sekiro.UpperBodyCombat")

function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, nil, true)
    self.UpperBodyCombat = self:CreateStateMachine(UpperBodyCombatClass)
end
```

蓝图里仍然只需要根 `Sekiro Lua Anim Blueprint Host` 节点。上半身层如何和下半身层混合，会继续扩展为 Lua AnimBlueprint DSL，例如 Slot / Layered Blend / Inertialization 接口。

## 十三、可读取的上下文数据

C++ 会把 AnimInstance 上的简单 UPROPERTY 打包成 `runtime_context` 表传给 Lua。基类会在每帧开始时把它们同步成状态机实例字段，所以业务逻辑里通常直接写：

```lua
local speed = self.Speed
local gait = self.Gait
local wants_move = self:WantsMove()
```

当前 `USKAnimInstance` 常用字段：

| 字段 | 类型 | 用途 |
|------|------|------|
| `Speed` | number | 水平速度 |
| `Angle` | number | 速度方向相对角色朝向 |
| `MovementInputAmount` | number | 输入强度 0-1 |
| `MovementState` | enum/text | Grounded/InAir/Crouching |
| `Gait` | enum/text | 当前步态 |
| `DesiredGait` | enum/text | 目标步态 |
| `GroundedEntryState` | enum/text | 地面状态入口建议 |
| `TurnAngle` | number | 转身角度 |
| `bIsMoving` | bool | 是否有水平速度 |
| `bHasMovementInput` | bool | 是否有移动输入 |
| `bShouldTurnInPlace` | bool | 是否应该原地转身 |
| `bIsDodging` | bool | 是否正在闪避 |
| `InputIntent` | name/text | 输入意图 |

运行时上下文里还会有：

```lua
context.LayerName
context.Snapshot
```

`Snapshot` 常用字段：

| 字段 | 说明 |
|------|------|
| `CurrentStateName` | 当前状态 |
| `PreviousStateName` | 上一个状态 |
| `CurrentTime` | 当前状态时间 |
| `PreviousTime` | 上一个状态时间 |
| `BlendAlpha` | 当前混合权重 |
| `CurrentBlendInput` | 当前 BlendSpace 输入 |
| `bHasPose` | 是否已经有有效姿势 |

通常不要在业务逻辑里手写 `context.Speed`。优先读同步后的 `self.Speed`；如果确实需要从上下文兜底读取，使用：

```lua
self:ReadContextNumber(context, "Speed", 0)
```

这样即使上下文来源从 Lua table 变成 UObject fallback，也能继续工作。项目脚本里推荐把这些判断封装成类内函数，例如 `self:IsMoving()`、`self:WantsCycle()`，而不是再建一层中介表。

如果要调用 AnimInstance 上的函数，可以直接写：

```lua
local owner = self:GetOwningActor()
local value = self:CallCpp("SomeBlueprintCallableFunction", arg0, arg1)
```

当状态机没有同名 Lua 方法时，`self:GetOwningActor()` 会尝试转发到当前 AnimInstance；`CallCpp` 是显式转发写法，适合函数名来自变量或你想避免命名冲突时使用。

## 十四、从零新增一个 Lua 动画状态机

以新增 `UpperBodyCombat` 为例：

1. 在 `AnimAssets.lua` 加资源：

```lua
M.Combat = {
    Empty = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",
    AttackLight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_AttackLight.Anim_Sekiro_AttackLight",
}
```

2. 新建 `Layer/UpperBodyCombat/Library.lua`：

```lua
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "UpperBodyCombat"
M.State = {
    Empty = "Empty",
    AttackLight = "AttackLight",
}
M.StateList = {
    M.State.Empty,
    M.State.AttackLight,
}
M.Assets = AnimAssets.Combat
M.AnimationSettings = {
    [M.State.Empty] = { BlendTime = 0.08, Loop = true },
    [M.State.AttackLight] = { BlendTime = 0.05, Loop = false },
}

return M
```

3. 新建 `UpperBodyCombat.lua`：

```lua
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")
local LayerLibrary = require("Animation.Sekiro.Layer.UpperBodyCombat.Library")

local State = LayerLibrary.State
local Anim = LayerLibrary.Assets

local UpperBodyCombat = class("UpperBodyCombat", BaseStateMachine, {
    LayerName = LayerLibrary.LayerName,
    EntryState = State.Empty,
    State = State,
    States = State,
    StateList = LayerLibrary.StateList,
    Assets = LayerLibrary.Assets,
    AnimationSettings = LayerLibrary.AnimationSettings,
})

function UpperBodyCombat:WantsAttackLight()
    return self.InputIntent == "AttackLight"
end

function UpperBodyCombat:UpdateAnimation_Empty()
    return self:PlaySequence(Anim.Empty)
end

function UpperBodyCombat:UpdateAnimation_AttackLight()
    return self:PlaySequence(Anim.AttackLight)
end

function UpperBodyCombat:CanEnter_Empty_AttackLight()
    return self:WantsAttackLight()
end

function UpperBodyCombat:CanEnter_AttackLight_Empty()
    return self.StateTime >= 0.6
end

return UpperBodyCombat
```

4. 在 `ABP_Sekiro.lua` 的 `Initialize()` 里创建状态机：

```lua
local UpperBodyCombatClass = require("Animation.Sekiro.UpperBodyCombat")

function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, nil, true)
    self.UpperBodyCombat = self:CreateStateMachine(UpperBodyCombatClass)
end
```

5. 如果它要成为根输出，在 `AnimGraph()` 里返回它；如果它是上半身叠加层，后续会通过 Lua AnimBlueprint DSL 的 Slot / Layered Blend 接口接入，而不是在蓝图里手画 UE StateMachine。

## 十五、调试方法

### 看 Lua 有没有加载

如果日志里出现：

```text
module 'Animation.Sekiro.ABP_Sekiro' not found
```

检查：

- 文件是否存在：`Content/Script/Animation/Sekiro/ABP_Sekiro.lua`
- `GetModuleName` 是否返回：`Animation.Sekiro.ABP_Sekiro`
- 有没有还在使用旧路径：`Sekiro.AnimBlueprint.ABP_Sekiro`
- 编辑器是否需要重启清掉 UnLua 热重载缓存。

### 看状态有没有变化

可以临时在 Lua 里打印：

```lua
print("[LuaAnim] state=", self.CurrentState, "speed=", self.Speed, "entry=", self.EntryState)
```

如果 `self.CurrentState` 一直为空，通常是 AnimGraph 的 `Sekiro Lua Anim Blueprint Host` 节点没有接到 Output Pose、`LayerName` 不匹配，或者 Lua 没有成功提交有效 `StateName`。

### 看动画为什么没播

重点检查 `UpdateAnimation_<State>` 是否调用了 pose 接口：

```lua
print("[LuaAnim] cycle", Anim.Run_Forward_Loop, self.CurrentStateName)
return self:PlaySequence(Anim.Run_Forward_Loop)
```

如果日志提示 `state has no valid animation`：

- `AnimationPath` 为空。
- `AnimAssets.lua` 里的 key 写错。
- 资源路径不是 UE 对象路径。
- 资源没有加载成功或已经被移动。

### Rider 调试 Lua

项目的 `Content/Script/Main.lua` 里已经有 EmmyLua 调试入口示例。调试时一般流程是：

1. Rider 安装 EmmyLua 或 Lua 调试插件。
2. 新建 Lua Attach 配置，端口使用 `9966`。
3. 启动编辑器或 PIE，让 UnLua 执行 `Main.lua`。
4. Rider Attach 到 `localhost:9966`。
5. 在 `ABP_Sekiro.lua`、`GroundLocomotion.lua` 等文件里下断点。

如果断点不进，优先确认 Lua 模块真的被加载。可以在模块顶部加一行临时打印：

```lua
print("[LuaAnim] load Animation.Sekiro.ABP_Sekiro")
```

## 十六、常见问题

### 1. 模块名到底填什么

填 Lua require 名，不填磁盘路径。

```text
Content/Script/Animation/Sekiro/ABP_Sekiro.lua
=> Animation.Sekiro.ABP_Sekiro
```

### 2. LayerName 填什么

填 Lua 动画层名，也就是 `Library.lua` 里的 `M.LayerName`。

```lua
M.LayerName = "GroundLocomotion"
```

根 Host 节点可以留空，让它读取 Lua 主模块的默认输出；也可以显式填写：

```text
GroundLocomotion
```

### 3. 一个动画蓝图能有多个 Lua 文件吗

可以，而且推荐这样做。动画蓝图绑定一个主模块，主模块再 `require` 多个子状态机或动画层。

### 4. 子状态机也能独立一个 Lua 文件吗

可以。子状态机按动画层或功能拆文件。例如：

```text
Animation.Sekiro.GroundLocomotion
Animation.Sekiro.UpperBodyCombat
Animation.Sekiro.AdditiveAim
```

### 5. Lua 能创建动画蓝图里的节点吗

运行时 Lua 不应该直接创建 UE AnimGraph 节点。当前蓝图里只需要摆好 `Sekiro Lua Anim Blueprint Host` 作为根入口；状态机、Transition 和动画选择由 Lua 创建并驱动。这样动画线程评估姿势时可以使用 C++ 快照，避免在动画线程调用 Lua。

### 6. 为什么状态切换函数没被调用

检查命名是否符合：

```text
CanEnter_<来源状态>_<目标状态>
```

如果状态名是 `TurnInSpace`，函数必须是：

```lua
function GroundLocomotion:CanEnter_Idle_TurnInSpace()
```

基类会自动把它识别为 `Idle -> TurnInSpace`。如果没被调用，优先检查当前状态是否真的是 `Idle`，以及 `TurnInSpace` 是否在 `StateList` 里。

### 7. 为什么一直保持 Idle

通常是这些原因：

- C++ 同名字段没有同步到 `self`，例如 `self.Speed`、`self.MovementInputAmount` 为空。
- `WantsStart`、`WantsCycle` 等条件一直是 false。
- `CanEnter_Idle_Start` 返回 false。
- `StateList` 没有包含目标状态。
- Transition 函数名写反了，比如误写成 `CanEnter_Start_Idle`。
- `Sekiro Lua Anim Blueprint Host` 的 `LayerName` 和 Lua 注册层名不一致；根 Host 留空时则检查 `CreateStateMachine(..., true)` 是否设置了默认输出。

## 十七、编写建议

- 每个 Transition 尽量一个函数，不要把所有切换条件塞进一个大 if。
- 先写 `StateList` 和 `EntryState`，再写类内判断函数，最后写 `CanEnter_<From>_<To>` 和 `UpdateAnimation_<State>`。
- 状态名、动画资源名、LayerName 都放到库或资源表里，减少手写字符串。
- C++ 变量优先用同名 `self` 字段读取，例如 `self.Speed`、`self.DesiredGait`、`self.bIsLockedOn`；读取函数只作为兜底。
- 动画资源路径统一放 `AnimAssets.lua`，状态机里只写语义名。
- 新层先用最小状态跑通，例如 `Idle -> Attack -> Idle`，再逐步加复杂切换。
- 调试时先打印模块加载，再打印 `self.CurrentState` / `self.Speed` 等实例字段，最后打印 `UpdateAnimation_<State>` 里提交的动画路径和 BlendInput。

## 十八、最小模板

新建状态机时可以从这个模板开始：

```lua
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")
local LayerLibrary = require("Animation.Sekiro.Layer.MyLayer.Library")

local State = LayerLibrary.State
local Anim = LayerLibrary.Assets

local MyLayer = class("MyLayer", BaseStateMachine, {
    LayerName = LayerLibrary.LayerName,
    EntryState = State.Idle,
    State = State,
    States = State,
    StateList = LayerLibrary.StateList,
    Assets = LayerLibrary.Assets,
    AnimationSettings = LayerLibrary.AnimationSettings,
})

function MyLayer:HasMovementInput()
    return self.bHasMovementInput == true or (self.MovementInputAmount or 0) > 0.1
end

function MyLayer:WantsIdle()
    return not self:HasMovementInput()
end

function MyLayer:WantsMove()
    return self:HasMovementInput()
end

function MyLayer:UpdateAnimation_Idle()
    return self:PlaySequence(Anim.Idle)
end

function MyLayer:UpdateAnimation_Move()
    return self:PlaySequence(Anim.Move)
end

function MyLayer:CanEnter_Idle_Move()
    return self:WantsMove()
end

function MyLayer:CanEnter_Move_Idle()
    return self:WantsIdle()
end

return MyLayer
```
