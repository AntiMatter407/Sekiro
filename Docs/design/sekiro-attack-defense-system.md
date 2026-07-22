# 角色攻击防御系统技术设计

> **状态**：草案
> **创建日期**：2026-07-22
> **需求文档**：[角色攻击防御系统需求](../gdd/sekiro-attack-defense-system.md)
> **关联资产表**：`Content/Script/Animation/Sekiro/AnimAssets.lua`

> **当前实施边界**：先实现玩家攻击、防御和弹反动画动作。AI、武器碰撞、真实命中、伤害、架势、Block 和攻击方回弹延后；本阶段以可注入的模拟来袭上下文验证弹反。

## 一、设计结论

当前阶段采用“通用 C++ 动作宿主 + Lua 动作状态机 + 动画曲线时间真源 + 模拟来袭测试入口”的结构：

```text
SKInputManager Started/Completed Events
                |
                v
USKCombatComponent <-> Gameplay.Sekiro.Combat.SKCombatComponent.lua
        |                         ^
        |                         |
        |              SimulatedIncomingAttack
        |              Type / Serial / Time Range
        v
CombatFullBodySlot / Guard StateMachine
        |
        v
Active Sequence Curves
  AttackSide / Input Windows
```

核心约束如下：

1. 动画曲线决定当前帧允许的行为，Lua 决定是否执行玩家输入意图；AI 后续通过相同事件契约接入。
2. 当前攻击侧和下一攻击侧分离，避免动画中途切侧污染当前命中方向。
3. 每段离散动作拥有独立动作序列号，所有输入、曲线、Montage 结束和来袭上下文回调必须携带并校验该编号。
4. 弹反不是普通 Guard 动画分支；只有有效来袭时间内的新 Guard Started 边沿才能触发。
5. 来袭攻击类型到 Deflect Type、连续弹反阶段到 Stage 的选择全部由 Lua 配置决定，C++ 不引用具体资产名。
6. 后续碰撞系统只负责生产与模拟接口相同的来袭上下文，不改变当前阶段验证后的弹反输入语义。

## 二、现有基础与缺口

### 2.1 可复用能力

| 模块 | 当前能力 | 本设计用途 |
| --- | --- | --- |
| `USKInputManager` | Attack 按下/保持时间、Guard 保持、输入缓冲 | 提供原始意图，不再拥有连段进度。 |
| `USKCameraManagerComponent` | 锁定目标查询和目标朝向 | 动作开始前确定攻击朝向。 |
| `USKWeaponManagerComponent` | 武器生成、挂载、动态 Slot Montage | 当前只参与收拔刀动画仲裁；碰撞控制后续接入。 |
| `ASKWeapon` | Capsule Overlap、命中去重 | 当前阶段不调用；后续扩展为连续刀刃 Sweep 和命中上报。 |
| `USKAnimInstance` | Root Motion、移动状态采集 | 增加战斗状态和曲线调试数据。 |
| Lua AnimBlueprint | 原生状态机、Slot、曲线 Gate | 增加防御层和全身战斗 Slot。 |

### 2.2 必须替换的旧行为

1. 输入层 `ComboIndex + TimeSinceLastAttack` 不能作为正式连段依据。
2. `ConsumeBufferedInput` 当前不负责跨 Action 的优先级裁决，战斗组件必须显式按动作优先级处理。
3. 当前上半身武器 Slot 不适合全身攻击和弹反，需要独立的 `CombatFullBodySlot`。
4. `ASKWeapon` 的硬编码伤害、Hitbox 和 Sweep 属于后续碰撞与伤害阶段，本阶段不调用。

## 三、模块划分

以下为建议新增或修改的位置，不代表当前已经存在：

```text
Source/Sekiro/Combat/
├── SKCombatTypes.h
├── SKCombatComponent.h
└── SKCombatComponent.cpp

Content/Script/Gameplay/Sekiro/Combat/
├── CombatConfig.lua
└── SKCombatComponent.lua

Content/Script/Animation/Sekiro/Layer/Combat/
├── Root.lua
└── Guard.lua
```

### 3.1 `USKCombatComponent`

C++ 组件负责：

- 保存可供 AnimBP、Blueprint 和 Lua 读取的战斗状态。
- 播放、停止和查询当前全身战斗动画。
- 为当前动作生成和校验动作序列号。
- 采样当前战斗动画的语义曲线。
- 接收带 Started/Completed、输入序列号和时间戳的攻击与防御输入事件。
- 保存、查询和一次性消费模拟来袭上下文。
- 在中断和 EndPlay 时停止自己拥有的 Montage 并清理旧动作回调。
- 派发 DeflectStarted、DeflectEnded 等通用动画事件。

C++ 不负责：

- 硬编码 `Right -> Left -> Combo` 动作链。
- 根据具体资产名选择动画。
- 硬编码伤害、架势、窗口时长或 `/Game` 路径。
- 决定某个角色是否使用玩家、Boss 或其他动作表。
- 依据来袭类型选择具体 Deflect Type 或 Stage。

### 3.2 `SKCombatComponent.lua`

Lua 运行时负责：

- 消费玩家输入事件；AI 动作请求在后续阶段接入同一入口。
- 管理轻重攻击候选和长按分类。
- 根据动作表和下一攻击侧选择后续动作。
- 处理主动 Guard/Dodge 取消。
- 根据曲线提交 `NextAttackSide`。
- 处理模拟来袭上下文与 Guard Started 的时间匹配。
- 根据来袭攻击类型和连续弹反阶段选择 Deflect 动画。
- 执行动作优先级、中断和统一清理流程。

### 3.3 后续模块

`CombatResolver.lua`、`ASKWeapon` Sweep、真实命中、Block、HP 和 Posture 在动画动作原型验收后实现。后续真实命中入口只能创建与本阶段 `FSKIncomingAttackAnimationContext` 等价的数据，再交给现有 Lua 状态机处理，不另建弹反触发旁路。

## 四、核心类型

### 4.1 攻击侧

```cpp
UENUM(BlueprintType)
enum class ESKAttackSide : uint8
{
    None,
    Left,
    Right,
};
```

运行时必须同时保存：

| 字段 | 含义 |
| --- | --- |
| `CommittedAttackSide` | 当前动作实际攻击侧；动作开始时固定，整段动作期间不变化。 |
| `NextAttackSide` | 当前动画曲线提交的下一动作侧。 |
| `SideSnapshot` | 输入候选创建时快照的下一动作侧。 |

### 4.2 动作状态

```cpp
UENUM(BlueprintType)
enum class ESKCombatActionState : uint8
{
    Neutral,
    PendingAttack,
    LightAttack,
    HeavyAttack,
    GuardRaise,
    Guarding,
    GuardLower,
    DeflectReaction,
};
```

枚举只表达当前动画动作阶段的通用状态。`Right`、`Combo_01` 等具体动作 ID 保存在 Lua 配置和运行时字符串/FName 中；BlockReaction、HitStun、PostureBroken 和 Dead 在对应玩法接入时再增加。

### 4.3 输入事件

```cpp
UENUM(BlueprintType)
enum class ESKCombatInputPhase : uint8
{
    Started,
    Completed,
};

USTRUCT(BlueprintType)
struct FSKCombatInputEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FName Action = NAME_None;

    UPROPERTY(BlueprintReadOnly)
    ESKCombatInputPhase Phase = ESKCombatInputPhase::Started;

    UPROPERTY(BlueprintReadOnly)
    int32 InputSerial = 0;

    UPROPERTY(BlueprintReadOnly)
    double EventTimeSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly)
    float HoldDuration = 0.0f;
};
```

Started 和 Completed 必须使用同一个 `InputSerial`。战斗组件按事件时间换算到活动 Sequence 本地时间，不能在后续 Tick 中用当前 Held 状态反推按键边沿。

### 4.4 模拟来袭上下文

```cpp
UENUM(BlueprintType)
enum class ESKIncomingAttackType : uint8
{
    Light,
    Heavy,
    Thrust,
    Special,
};

USTRUCT(BlueprintType)
struct FSKIncomingAttackAnimationContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    ESKIncomingAttackType AttackType = ESKIncomingAttackType::Light;

    UPROPERTY(BlueprintReadOnly)
    int32 ContextSerial = 0;

    UPROPERTY(BlueprintReadOnly)
    double ActiveStartTimeSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly)
    double ActiveEndTimeSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly)
    bool bConsumed = false;
};
```

测试入口创建该上下文时自动生成新的 `ContextSerial`。后续真实命中系统复用同一结构或其兼容扩展；当前结构故意不持有 Attacker、Target、伤害或碰撞数据。

## 五、Lua 动作数据

### 5.1 动作定义

```lua
---@class SKAttackActionConfig
---@field Animation string 动画对象路径。
---@field Kind string Light 或 Heavy。
---@field Side string 当前动作攻击侧。
---@field LightNext string|nil 有效轻攻击输入对应的下一动作。
---@field HeavyNextBySide table<string, string>|nil 下一侧到重攻击动作的映射。
```

建议配置结构：

```lua
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local AttackActions = {
    Right = {
        Animation = AnimAssets.Attack.Right,
        Kind = "Light",
        Side = "Right",
        LightNext = "Left",
        HeavyNextBySide = {
            Left = "Charged_Thrust_Left",
        },
    },
    Left = {
        Animation = AnimAssets.Attack.Left,
        Kind = "Light",
        Side = "Left",
        LightNext = "Combo_01",
        HeavyNextBySide = {
            Right = "Charged_Thrust_Right",
        },
    },
    Charged_Thrust_Right = {
        Animation = AnimAssets.Attack.Charged_Thrust_Right,
        Kind = "Heavy",
        Side = "Right",
        LightNext = "Left",
        HeavyNextBySide = {
            Left = "Charged_Thrust_Left",
        },
    },
    Charged_Thrust_Left = {
        Animation = AnimAssets.Attack.Charged_Thrust_Left,
        Kind = "Heavy",
        Side = "Left",
        LightNext = "Combo_01",
        HeavyNextBySide = {
            Right = "Charged_Thrust_Right",
        },
    },
}
```

`Combo_01~03` 的 `Side` 必须在人工核验动画后填写。配置加载时应检查动作 Side 与对应资产曲线的预期是否一致。

### 5.2 弹反动画映射

```lua
---@class SKDeflectTypeConfig
---@field Stages string[] 按连续弹反阶段排列的动画路径；数组长度必须与真实资产一致。

local DeflectByIncomingAttackType = {
    Light = { Stages = {
        AnimAssets.Deflect.Type_01_Stage_01,
        AnimAssets.Deflect.Type_01_Stage_02,
        AnimAssets.Deflect.Type_01_Stage_03,
    } },
    Heavy = { Stages = {
        AnimAssets.Deflect.Type_02_Stage_01,
        AnimAssets.Deflect.Type_02_Stage_02,
        AnimAssets.Deflect.Type_02_Stage_03,
    } },
    Thrust = { Stages = {
        AnimAssets.Deflect.Type_03_Stage_01,
        AnimAssets.Deflect.Type_03_Stage_02,
        AnimAssets.Deflect.Type_03_Stage_03,
    } },
    Special = { Stages = {
        AnimAssets.Deflect.Type_04_Stage_01,
    } },
}
```

上述 Light/Heavy/Thrust/Special 到 Type 01～04 的对应关系是当前可运行的占位配置，不代表资产语义已经确认。阶段 0 必须逐组人工播放核验；如果语义不一致，只调整 Lua 映射，不修改 C++ 枚举和测试接口。

### 5.3 运行时状态

```lua
---@class SKIncomingAttackContext
---@field AttackType string 来袭攻击类型，对应 DeflectByIncomingAttackType 的稳定键。
---@field ContextSerial number 来袭上下文序列号，运行时只使用整数值。
---@field ActiveStartTimeSeconds number 有效区间开始的游戏时间。
---@field ActiveEndTimeSeconds number 有效区间结束的游戏时间。
---@field bConsumed boolean 是否已经被一次 Guard Started 成功消费。

---@class SKAttackRuntime
---@field ActionId string|nil
---@field ActionSerial number 动作序列号，运行时只使用整数值。
---@field State string
---@field CommittedAttackSide string
---@field NextAttackSide string
---@field PendingInput SKPendingAttackInput|nil
---@field bSideCurveArmed boolean
---@field bSideCurveConsumed boolean
---@field PreviousSideCurveValue number
---@field IncomingAttackContext SKIncomingAttackContext|nil
---@field DeflectChainType string|nil
---@field DeflectChainStage number 当前连续弹反阶段，Lua 数组下标从 1 开始。
---@field LastDeflectTime number|nil 上次成功弹反时间，用于连续阶段超时重置。
```

### 5.4 输入候选

```lua
---@class SKPendingAttackInput
---@field SourceActionSerial number 来源动作序列号，运行时只使用整数值。
---@field InputSerial number 对应一次物理攻击按键的唯一编号。
---@field PressTime number
---@field ReleaseTime number|nil
---@field HoldDuration number|nil
---@field SideSnapshot string
---@field bLightAccepted boolean
---@field bHeavyAccepted boolean
---@field ResolvedKind string|nil
---@field bConsumed boolean
```

## 六、动画曲线契约

### 6.1 曲线登记

实现前必须同步更新 `Docs/animation-curve-authoring-guide.md` 和 `Content/Script/Animation/Sekiro/Shared/CurveNames.lua`。

| 曲线 | 类型 | 值域 | 缺失行为 |
| --- | --- | --- | --- |
| `AttackHitbox` | int enum | `0=None`，其他值为攻击盒类型 | 当前只登记；后续碰撞阶段缺失时禁止产生攻击命中。 |
| `AttackSide` | int enum | `-1=Left`、`0=Keep`、`1=Right` | 不切换下一攻击侧。 |
| `CanAcceptLightAttack` | int bool | `0/1` | 不允许轻攻击续段。 |
| `CanAcceptHeavyAttack` | int bool | `0/1` | 不允许重攻击续段。 |
| `CanCancelToGuard` | int bool | `0/1` | 不允许主动防御取消。 |
| `CanCancelToDodge` | int bool | `0/1` | 不允许主动闪避取消。 |

禁止使用单值 `CancelActions` 同时表达多个取消条件。轻攻击、重攻击、防御和闪避窗口可能重叠，必须使用独立布尔曲线。

### 6.2 `AttackSide` 作者规则

`AttackSide` 只负责提交下一攻击侧：

```text
0：本帧不改变 NextAttackSide
-1：将 NextAttackSide 提交为 Left
1：将 NextAttackSide 提交为 Right
```

每个攻击动画遵守以下时间约束：

1. 动作启动和攻击盒期间保持 `0`。
2. 最后一个 `AttackHitbox` 关闭后才能进入非零值。
3. 非零值必须覆盖对应输入窗口的开始帧。
4. 输入窗口结束后回到 `0`，避免淡出阶段污染下一 Montage。
5. 使用阶梯关键帧，禁止线性表达枚举语义。
6. 非零区间至少覆盖两个源动画采样帧，降低低帧率漏采样风险。

“最后一个 AttackHitbox 关闭后”是资产作者约束；当前动画动作阶段不在运行时读取 AttackHitbox，因此不把它作为 `AttackSide` 提交的运行时 Gate。后续碰撞阶段接入后再启用该校验。

推荐示例：

```text
Right：       HitboxEnd -> AttackSide=-1 -> Light/HeavyWindow -> 0
Left：        HitboxEnd -> AttackSide= 1 -> Light/HeavyWindow -> 0
Charged_Thrust_Right： HitboxEnd -> AttackSide=-1 -> Light/HeavyWindow -> 0
Charged_Thrust_Left：  HitboxEnd -> AttackSide= 1 -> Light/HeavyWindow -> 0
```

### 6.3 曲线采样位置

战斗逻辑不得直接信任最终混合 Pose 的枚举曲线值。全身 Slot 淡入淡出时，Left、Right 和 0 可能被混合成中间浮点值。

由 `USKCombatComponent` 保存当前战斗 `UAnimSequence`、动态 Montage、动作开始时间和上次采样位置。Montage Track 时间必须通过活动 `FAnimSegment::ConvertTrackPosToAnimPos` 换算到 Sequence 本地时间，不能直接把 `Montage_GetPosition` 当作 Sequence 时间。

普通逐帧更新读取当前动作位置；判断 Started/Completed 输入窗口时，根据事件时间还原对应的动作本地时间并在该时间采样，避免输入发生在窗口内、战斗 Tick 消费时窗口已经关闭。组件提供：

```cpp
UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
float SampleActiveActionCurve(FName CurveName) const;

UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
float SampleActiveActionCurveAtEventTime(
    FName CurveName,
    double EventTimeSeconds) const;
```

`UAnimInstance::GetCurveValue` 只允许用于临时诊断，不作为当前阶段验收实现；如果为了定位问题临时启用，必须同时使用：

- 稳定阈值：`>= 0.75` 视为 Right，`<= -0.75` 视为 Left。
- 零值武装：每个新动作先观察到 `Abs(Value) < 0.25` 才允许提交。
- 每动作一次性消费。
- 动作序列号校验。
- 后续碰撞阶段再增加 `AttackHitbox == 0` 校验；当前阶段不读取该曲线。

## 七、攻击侧状态机

### 7.1 动作开始

```lua
local function begin_attack(self, action_id, expected_serial)
    local action = self.AttackActions[action_id]
    if action == nil then
        return false
    end

    local new_serial = self:BeginCombatAction(expected_serial)
    if new_serial <= 0 then
        return false
    end

    self.Runtime.ActionId = action_id
    self.Runtime.ActionSerial = new_serial
    self.Runtime.CommittedAttackSide = action.Side
    self.Runtime.NextAttackSide = action.Side
    self.Runtime.PendingInput = nil
    self.Runtime.bSideCurveArmed = false
    self.Runtime.bSideCurveConsumed = false
    self.Runtime.PreviousSideCurveValue = 0.0
    return true
end
```

`CommittedAttackSide` 在当前阶段用于状态、日志和后续动作验证，不得被本动作后续曲线修改；后续命中接入时再写入命中数据。

### 7.2 曲线提交下一侧

```lua
local function update_next_attack_side(self)
    local runtime = self.Runtime
    if runtime.ActionId == nil then
        return
    end

    local curve_value = self:SampleActiveActionCurve("AttackSide")
    if runtime.bSideCurveArmed ~= true then
        if math.abs(curve_value) < 0.25 then
            runtime.bSideCurveArmed = true
        end
        runtime.PreviousSideCurveValue = curve_value
        return
    end

    if runtime.bSideCurveConsumed == true then
        return
    end

    if curve_value >= 0.75 then
        runtime.NextAttackSide = "Right"
        runtime.bSideCurveConsumed = true
    elseif curve_value <= -0.75 then
        runtime.NextAttackSide = "Left"
        runtime.bSideCurveConsumed = true
    end

    runtime.PreviousSideCurveValue = curve_value
end
```

### 7.3 输入侧快照

攻击 Started 被窗口接受时立即保存。窗口值必须在事件对应的动作本地时间采样：

```lua
runtime.PendingInput = {
    SourceActionSerial = runtime.ActionSerial,
    InputSerial = attack_event.InputSerial,
    PressTime = attack_event.EventTimeSeconds,
    SideSnapshot = runtime.NextAttackSide,
    bLightAccepted = self:SampleActiveActionCurveAtEventTime(
        "CanAcceptLightAttack",
        attack_event.EventTimeSeconds) > 0.5,
    bHeavyAccepted = self:SampleActiveActionCurveAtEventTime(
        "CanAcceptHeavyAttack",
        attack_event.EventTimeSeconds) > 0.5,
    ResolvedKind = nil,
    bConsumed = false,
}
```

后续侧向曲线不得改变 `SideSnapshot`。Attack Completed 必须通过 `InputSerial` 找到同一候选，再写入 `ReleaseTime` 和 `HoldDuration`；Serial 不匹配的释放事件直接丢弃。

## 八、轻重输入分类

### 8.1 Neutral 输入

Neutral 状态没有前一动画窗口：

- Attack Started 创建初始候选。
- 阈值前释放：播放 `Right`。
- 达到阈值：播放 `InitialHeavySide` 对应重攻击，默认 `Charged_Thrust_Right`。

初始输入的等待时间是玩法层明确接受的轻重判定成本。后续可通过 Enhanced Input Tap/Hold Trigger 或共享起手动画降低体感延迟。

### 8.2 动作内输入

```text
Attack Started
  |
  +-- LightWindow=0 且 HeavyWindow=0 -> 忽略
  |
  +-- 保存 PressTime、SideSnapshot 和两个窗口许可
          |
          +-- 当前动作不允许 Heavy -> 立即解析为 Light
          |
          +-- 阈值前释放且 LightAccepted -> Light
          |
          +-- 达到阈值且 HeavyAccepted -> Heavy
          |
          +-- 动作结束仍未解析 -> 丢弃
```

当 Light 和 Heavy 都可用时，不得在 Started 瞬间立即执行 Light，否则该输入无法升级为 Heavy。

### 8.3 后续动作选择

```lua
local function resolve_followup(self, pending)
    local current = self.AttackActions[self.Runtime.ActionId]
    if current == nil then
        return nil
    end

    if pending.ResolvedKind == "Heavy" then
        local heavy_map = current.HeavyNextBySide
        return heavy_map and heavy_map[pending.SideSnapshot] or nil
    end

    if pending.ResolvedKind == "Light" then
        return current.LightNext
    end

    return nil
end
```

进入新动作前必须再次检查：

1. `SourceActionSerial == Runtime.ActionSerial`。
2. 当前动作仍在播放且未被中断。
3. 候选尚未消费。
4. 目标动作存在。
5. Heavy 目标侧与 `SideSnapshot` 一致。

## 九、动作转换表

| 当前动作 | 当前侧 | 曲线提交侧 | Light | Heavy |
| --- | --- | --- | --- | --- |
| `Right` | Right | Left | `Left` | `Charged_Thrust_Left` |
| `Left` | Left | Right | `Combo_01` | `Charged_Thrust_Right` |
| `Charged_Thrust_Right` | Right | Left | `Left` | `Charged_Thrust_Left` |
| `Charged_Thrust_Left` | Left | Right | `Combo_01` | `Charged_Thrust_Right` |
| `Combo_01` | 待核验 | 待核验 | `Combo_02` | 不允许 |
| `Combo_02` | 待核验 | 待核验 | `Combo_03` | 不允许 |
| `Combo_03` | 待核验 | 无 | 结束 | 不允许 |

Light 转换使用固定动作链；SideSnapshot 用于验证目标动作侧、选择 Heavy 资产以及命中方向，不允许仅凭 SideSnapshot 跳过固定 Combo 节点。

## 十、动画图集成

### 10.1 图结构

建议根图调整为：

```text
RootLocomotion
-> Inertialization
-> WeaponUpperBodySlot + Spine Layer Blend
-> Guard Pose Blend/StateMachine
-> CombatFullBodySlot
-> FootPlacement / LegIK
-> Result
```

`CombatFullBodySlot` 必须覆盖完整骨架，当前承担轻攻击、重攻击和 Deflect 等离散动作。该 Slot 由 `USKCombatComponent` 独占；开始战斗动作前必须停止或拒绝与其冲突的收拔刀 Montage，并明确配置 SlotGroup，不能让武器管理器和战斗组件同时认为自己拥有活动 Montage。

持续 Guard 使用状态机而不是循环动态 Montage：

```text
Raise -> GuardIdle/GuardMove -> Lower
```

### 10.2 Foot IK

全身攻击和 Deflect 期间，将 `FootIKAlpha` 淡出到 0，避免 IK 修改攻击脚步和 Root Motion。回到 Neutral/GuardMove 后再平滑恢复。

### 10.3 Root Motion

- 地面轻重攻击：启用 Root Motion。
- Guard Idle/Move：按资产实际情况核验，禁止同时由 Root Motion 和 MovementComponent 重复位移。
- Deflect：默认禁用位移型 Root Motion，只保留姿势；最终以资产核验结果为准。
- 空中攻击：空中轨迹继续由 CharacterMovement 驱动，忽略水平 Root Motion。

## 十一、攻击盒和命中（后续阶段）

本阶段不采样 `AttackHitbox`、不开启武器碰撞、不执行 Sweep，也不创建伤害或 Block 结果。曲线可以随资产审计一起登记，但缺少 `AttackHitbox` 不得阻止当前动画动作原型运行。

后续接入真实命中时，目标侧只把攻击类型、唯一上下文编号和有效时间范围转换成 `FSKIncomingAttackAnimationContext`。真实来源不得直接播放 Deflect，也不得绕过 Guard Started 边沿验证。

## 十二、模拟来袭与弹反动画

### 12.1 通用接口

```cpp
UFUNCTION(BlueprintCallable, Category = "Combat|Animation Test")
int32 BeginIncomingAttackAnimationTest(
    ESKIncomingAttackType AttackType,
    float ActiveDuration);

UFUNCTION(BlueprintCallable, Category = "Combat|Animation Test")
void ClearIncomingAttackAnimationTest(int32 ExpectedContextSerial);

UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation Test")
bool IsIncomingAttackAnimationActiveAt(double EventTimeSeconds) const;
```

`BeginIncomingAttackAnimationTest` 使用当前游戏时间作为开始时间，将负时长限制为零，并生成单调递增的 `ContextSerial`。新上下文覆盖旧上下文时必须让旧 Serial 失效。接口可由 PIE、自动测试或 AIBridge 调用，不生成 AI、攻击 Actor、碰撞和伤害。

### 12.2 Guard Started 裁决

Guard Started 事件必须携带 `InputSerial` 和 `EventTimeSeconds`。无论测试接口和输入回调在同一帧的调用先后如何，都按绝对事件时间裁决：

```lua
local function try_begin_deflect(self, guard_event)
    local incoming = self.Runtime.IncomingAttackContext
    if incoming == nil or incoming.bConsumed == true then
        return false
    end

    if guard_event.EventTimeSeconds < incoming.ActiveStartTimeSeconds
        or guard_event.EventTimeSeconds > incoming.ActiveEndTimeSeconds then
        return false
    end

    incoming.bConsumed = true
    return self:BeginDeflectAnimation(
        incoming.AttackType,
        incoming.ContextSerial,
        guard_event.InputSerial)
end
```

裁决顺序固定为：

```text
Guard Started
-> 尝试消费有效模拟来袭
-> 成功：进入 DeflectReaction
-> 失败：按普通 Guard Raise 或 CanCancelToGuard 处理
```

已经处于 Held 的 Guard 不会在来袭上下文创建时自动调用该流程。只有新的 Started 边沿可以触发。

### 12.3 Type 与 Stage 选择

连续弹反阶段只在成功播放 Deflect 时推进：

1. 与上次 Deflect Type 不同，或距上次成功弹反超过 `DeflectChainResetTime`：选择 Stage 01。
2. Type 相同且仍在连续窗口内：阶段递增，但不得超过该 Type 的 `Stages` 数组长度。
3. Type 01～03 最多推进到 Stage 03；Type 04 数组长度为 1，因此始终选择 Stage 01。
4. 普通 Guard、攻击、闪避、显式测试清理或动作强制中断会重置连续弹反阶段。

### 12.4 弹反结束

弹反 Montage 结束回调必须携带开始播放时的 `ActionSerial` 和 `ContextSerial`。两者任一不匹配时丢弃。有效结束时：

- Guard 仍按住：进入 `Guarding`，由 Guard 状态机输出 Idle/Move。
- Guard 已释放：直接进入 `Neutral`，不补播已经过时的 Guard Lower。
- 弹反播放期间的 Guard Completed 只更新 Held 状态，不中断当前 Montage。

## 十三、中断和清理

所有攻击退出统一经过：

```lua
local function interrupt_attack(self, reason)
    local old_serial = self.Runtime.ActionSerial

    self:InvalidateCombatAction(old_serial)
    self:StopCombatAnimation(self.Config.InterruptBlendOutTime)

    self.Runtime.ActionId = nil
    self.Runtime.PendingInput = nil
    self.Runtime.bSideCurveArmed = false
    self.Runtime.bSideCurveConsumed = false
    self.Runtime.State = "Neutral"
end
```

弹反中断攻击后立即以新的 ActionSerial 进入 `DeflectReaction`，而不是先暴露一个可被普通输入消费的 Neutral 帧。清理函数必须幂等，允许 Montage End、输入和测试上下文回调在同一帧重复调用。

### 13.1 序列号规则

- 开始动作：递增 `ActionSerial`。
- 输入候选：保存创建时的 Serial。
- 弹反开始：同时保存动作 Serial、来袭 ContextSerial 和 Guard InputSerial。
- 中断动作：立即使当前 Serial 失效。
- 迟到输入、曲线、Montage End 或来袭上下文回调：任一对应 Serial 不匹配时丢弃。

### 13.2 状态重置

| 退出原因 | 清除输入 | 重置轻攻击链 | 保留最后攻击侧 |
| --- | --- | --- | --- |
| 自然结束 | 是 | 是 | 可保留，仅用于调试。 |
| Deflect | 是 | 是 | 可保留，仅用于调试。 |
| Guard/Dodge 主动取消 | 是 | 是 | 可保留。 |
| 进入下一攻击 | 只消费当前候选 | 否 | 更新为新动作侧。 |

## 十四、Tick 与执行顺序

推荐顺序：

```text
1. 接收并排队 Started/Completed 输入事件
2. 更新或清理模拟来袭上下文
3. 校验 ActionSerial、InputSerial 和 ContextSerial
4. 按输入事件时间采样活动 Sequence 的窗口
5. 先处理 Guard Started 与来袭上下文，决定是否进入 Deflect
6. 未触发 Deflect 时处理普通 Guard 和 Guard 释放
7. 处理 AttackSide 提交
8. 创建或更新攻击输入候选
9. 解析 Light/Heavy 后续动作
10. 输出 AnimBP 状态和调试信息
```

Deflect 必须在执行攻击续段前处理，确保同帧 Guard Started 可以作废已排队攻击。组件 Tick 需显式晚于输入事件发布、早于 AnimInstance 数据采集；不得依赖同一 TickGroup 内的组件注册顺序。

## 十五、配置

第一阶段可使用 `CombatConfig.lua`：

```lua
local CombatConfig = {
    HeavyHoldThreshold = 0.30,
    InitialHeavySide = "Right",
    DeflectChainResetTime = 0.75,
    DefaultIncomingAttackTestDuration = 0.25,
    InterruptBlendOutTime = 0.05,
}
```

动画路径只从 `AnimAssets.lua` 引用。`WeaponConfig.lua` 中重复的 PrimaryAttack、SecondaryAttack、Guard 和 Deflect 路径应在实现阶段移除或改为引用统一资产表。

## 十六、日志和调试

开发期开关启用时，每次动作转换至少输出：

```text
ActionSerial
PreviousAction -> NewAction
CommittedAttackSide
NextAttackSide
CurveName/CurveValue
PendingInput Kind/PressTime/SideSnapshot
InputSerial
IncomingAttackType/ContextSerial/ActiveTimeRange
DeflectType/DeflectStage
InterruptReason
```

建议扩展 `ShowDebug Animation` 或新增 `ShowDebug Combat`：

```text
State=HeavyAttack Action=Charged_Thrust_Right Serial=42
CommittedSide=Right NextSide=Left
LightWindow=1 HeavyWindow=1
Pending=Heavy PressAge=0.31 SideSnapshot=Left
Incoming=Heavy ContextSerial=7 Remaining=0.12 Consumed=false
DeflectChain=Type_02 Stage=2
```

## 十七、验证方案

### 17.1 纯逻辑测试

- `Right + Light(Left)` 返回 `Left`。
- `Right + Heavy(Left)` 返回 `Charged_Thrust_Left`。
- `Charged_Thrust_Left + Heavy(Right)` 返回 `Charged_Thrust_Right`。
- `Charged_Thrust_Right + Light(Left)` 返回 `Left`。
- Combo 中 Heavy 输入不能返回 Heavy 动作。
- Serial 失效后 PendingInput 不可执行。
- 无来袭时 Guard Started 返回普通 Guard。
- 有效来袭期间 Guard Started 返回对应 Type/Stage 的 Deflect 动作。
- 已 Held 的 Guard 和重复 ContextSerial 不能触发 Deflect。
- Deflect 与续段同帧时优先中断。

### 17.2 曲线测试

- 每个攻击动画包含必需曲线。
- AttackSide 在输入窗口开始前已经稳定。
- Right/Charged_Thrust_Right 提交 Left。
- Left/Charged_Thrust_Left 提交 Right。
- Combo 曲线侧向与人工核验结果一致。
- 动画末尾 AttackSide 回零。

### 17.3 PIE 集成测试

1. 完成五段轻攻击链。
2. 在每一段窗口外按键，验证不续段。
3. Right/Left 分别长按进入相反侧重攻击。
4. 连续长按验证重攻击左右交替。
5. 在重攻击窗口短按，验证返回正确轻攻击节点。
6. 无来袭时按 Guard，验证 Raise、Idle/Move 和 Lower。
7. 分别注入 Light、Heavy、Thrust、Special 来袭，在有效期内按 Guard，验证不同 Deflect Type。
8. 连续注入同类型来袭，验证 Stage 推进、封顶和超时重置。
9. Guard 已按住后再注入来袭，验证不会自动弹反。
10. 在来袭有效区间外按 Guard，验证只进入普通防御。
11. 在低帧率模式验证曲线、输入时间和来袭时间边界不漂移。
12. 在 Montage 淡入淡出期间验证 AttackSide 不重复提交。
13. 连续中断一百次，验证没有残留候选、旧 Serial 和旧上下文。

## 十八、实施顺序

### 阶段 0：资产与曲线审计

1. 人工确认全部攻击动画的当前挥刀侧。
2. 确认 Root Motion、循环属性和源采样率。
3. 人工播放 Deflect Type 01～04，核验每组 Type 的攻击语义和 Stage 连续关系。
4. 标注 AttackSide 和四条窗口曲线；AttackHitbox 只登记供后续使用。
5. 生成曲线与弹反资产审计报告。

### 阶段 1：动作宿主与输入契约

1. 新增 `FSKCombatInputEvent`、来袭类型、上下文和 `USKCombatComponent` 通用宿主。
2. 让 Attack/Guard Started 与 Completed 携带 InputSerial、时间戳和 HoldDuration。
3. 增加由战斗组件独占的 `CombatFullBodySlot`，明确与收拔刀 Montage 的抢占和 SlotGroup 规则。
4. 实现活动 Sequence 本地时间采样、动作序列号和统一中断。

### 阶段 2：轻重攻击与侧向

1. 实现 Lua 动作表并跑通五段轻攻击链。
2. 实现窗口内按下候选和长按分类。
3. 实现 AttackSide 曲线提交和 SideSnapshot。
4. 跑通左右重攻击循环和重转轻。

### 阶段 3：Guard 与模拟弹反

1. 实现 Guard Raise、Idle/Move、Lower 状态机。
2. 实现模拟来袭创建、清理、过期和 ContextSerial 一次性消费。
3. 实现来袭类型到 Deflect Type 的 Lua 映射，以及连续 Stage 推进和重置。
4. 接入 Deflect 对攻击续段的优先中断和动画结束回 Guarding/Neutral。
5. 使用 PIE/AIBridge 完成无 AI、无碰撞条件下的全部弹反验收。

### 阶段 4：碰撞、真实命中与 Block（后续）

1. 移除武器硬编码伤害并实现连续刀刃 Sweep。
2. 将真实命中转换为已验证的来袭上下文。
3. 实现方向、Block、伤害、HP/Posture 和攻击方反馈。

### 阶段 5：表现和扩展

1. 命中停顿、VFX、SFX 和镜头反馈。
2. 空中攻击和落地攻击。
3. 专用攻击回弹、普通受击和架势崩坏动画。

## 十九、风险与待确认项

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| Combo 动画左右侧尚未确认 | 后续 Side 曲线和弹反方向可能错误 | 阶段 0 人工逐帧核验。 |
| Deflect Type 编号缺少明确攻击语义 | 可能为来袭类型选择错误动画 | 阶段 0 逐组播放核验，映射只放 Lua，禁止 C++ 硬编码。 |
| 无 AI 和真实命中来源 | 无法通过实战触发弹反 | 使用带类型、Serial 和绝对时间范围的模拟来袭接口完成当前验收。 |
| Guard 已按住时来袭自动触发 | 会把持续防御错误判为弹反 | 只允许 Guard Started 边沿消费来袭上下文。 |
| 同帧测试注入与输入回调顺序不稳定 | 边界条件在不同帧率下结果不一致 | 按绝对事件时间裁决，并校验 InputSerial/ContextSerial。 |
| 当前 TAE 提取数据与曲线消费者不完整 | 无法直接批量生成可靠窗口 | 先小批量人工标注和审计，修复源数据后再批量。 |
| 枚举曲线参与 Montage 混合 | 可能得到中间值或旧曲线残留 | 优先采样活动 Sequence；否则使用武装、阈值、Serial 和一次性消费。 |
| Root Motion 与 Foot IK 冲突 | 攻击脚步漂移或角色位移异常 | 全身动作关闭 IK，并逐资产核验 Root Motion。 |
| 同键轻重识别造成轻攻击延迟 | 首段攻击手感迟滞 | 先验证 0.30s 方案，再评估 Tap/Hold Trigger 或共享起手动画。 |

## 二十、完成定义

满足以下条件后，当前动画动作原型可以判定完成：

1. 需求文档中 15.1 至 15.4 的全部验收项通过。
2. 所有攻击动作由曲线驱动 AttackSide 和输入窗口；本阶段不要求消费 AttackHitbox。
3. 当前攻击侧在一段动作期间稳定，下一攻击侧只提交一次。
4. 五段轻攻击、连续重攻击和重转轻全部按转换表运行。
5. 普通 Guard 和模拟来袭 Deflect 均按输入边沿运行；不同来袭类型选择不同 Type，连续弹反正确选择 Stage。
6. Deflect 可以优先中断合法攻击动作，且弹反结束后按 Guard Held 状态进入 Guarding 或 Neutral。
7. 所有中断路径均无残留输入候选、旧动作回调、旧 InputSerial 或旧 ContextSerial。
8. C++ 源码中不存在具体攻击资产路径、Deflect Type/Stage 名称或连段名称。
9. 不依赖 AI、碰撞和伤害即可通过 PIE/AIBridge 重复验证全部动画动作。
