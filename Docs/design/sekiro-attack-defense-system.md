# 角色攻击防御系统技术设计

> **状态**：草案
> **创建日期**：2026-07-22
> **需求文档**：[角色攻击防御系统需求](../gdd/sekiro-attack-defense-system.md)
> **关联资产表**：`Content/Script/Animation/Sekiro/AnimAssets.lua`

## 一、设计结论

系统采用“通用 C++ 宿主 + Lua 战斗状态机 + 动画曲线时间真源”的结构：

```text
SKInputManager / AI Action Request
                |
                v
USKCombatComponent <-> Gameplay.Sekiro.Combat.SKCombatComponent.lua
                |
                v
CombatFullBodySlot / Guard StateMachine
                |
                v
Active Sequence Curves
  AttackHitbox / AttackSide / Input Windows
                |
                v
ASKWeapon Blade Sweep -> Target USKCombatComponent
                |
                v
Evade / Deflect / Block / Hit -> 双方状态更新
```

核心约束如下：

1. 动画曲线决定当前帧允许的行为，Lua 决定是否执行玩家或 AI 意图。
2. 当前攻击侧和下一攻击侧分离，避免动画中途切侧污染当前命中方向。
3. 每段攻击拥有独立动作序列号，所有异步输入、曲线和命中反馈必须携带并校验该编号。
4. 武器只负责检测和上报命中，不直接计算伤害或决定格挡、弹反结果。
5. Block 和 Deflect 是目标返回给攻击方的命中结果，可以无条件中断当前攻击。

## 二、现有基础与缺口

### 2.1 可复用能力

| 模块 | 当前能力 | 本设计用途 |
| --- | --- | --- |
| `USKInputManager` | Attack 按下/保持时间、Guard 保持、输入缓冲 | 提供原始意图，不再拥有连段进度。 |
| `USKCameraManagerComponent` | 锁定目标查询和目标朝向 | 动作开始前确定攻击朝向。 |
| `USKWeaponManagerComponent` | 武器生成、挂载、碰撞开关 | 提供当前武器和碰撞控制。 |
| `ASKWeapon` | Capsule Overlap、命中去重 | 扩展为连续刀刃 Sweep 和命中上报。 |
| `USKAnimInstance` | Root Motion、移动状态采集 | 增加战斗状态和曲线调试数据。 |
| Lua AnimBlueprint | 原生状态机、Slot、曲线 Gate | 增加防御层和全身战斗 Slot。 |

### 2.2 必须替换的旧行为

1. 输入层 `ComboIndex + TimeSinceLastAttack` 不能作为正式连段依据。
2. `ConsumeBufferedInput` 当前不负责跨 Action 的优先级裁决，战斗组件必须显式按动作优先级处理。
3. `ASKWeapon::OnHitboxOverlap` 中硬编码的 `100` 点伤害必须移出武器层。
4. 旧 `AttackHitboxConfigs` 已废弃，运行时改为消费动画曲线。
5. 当前上半身武器 Slot 不适合全身攻击，需要独立的 `CombatFullBodySlot`。

## 三、模块划分

以下为建议新增或修改的位置，不代表当前已经存在：

```text
Source/Sekiro/Combat/
├── SKCombatTypes.h
├── SKCombatComponent.h
└── SKCombatComponent.cpp

Content/Script/Gameplay/Sekiro/Combat/
├── CombatConfig.lua
├── CombatResolver.lua
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
- 接收武器上报的命中并转发给 Lua。
- 保存和修改 HP、Posture 等通用数值。
- 在中断和 EndPlay 时强制关闭攻击盒。
- 派发 Block、Deflect、Hit、PostureBroken 等通用事件。

C++ 不负责：

- 硬编码 `Right -> Left -> Combo` 动作链。
- 根据具体资产名选择动画。
- 硬编码伤害、架势、窗口时长或 `/Game` 路径。
- 决定某个角色是否使用玩家、Boss 或其他动作表。

### 3.2 `SKCombatComponent.lua`

Lua 运行时负责：

- 消费玩家输入或 AI 动作请求。
- 管理轻重攻击候选和长按分类。
- 根据动作表和下一攻击侧选择后续动作。
- 处理主动 Guard/Dodge 取消。
- 根据曲线提交 `NextAttackSide`。
- 处理目标返回的 Block/Deflect/Hit 结果。
- 执行动作优先级、中断和统一清理流程。

### 3.3 `CombatResolver.lua`

纯逻辑模块负责：

- 防御方向计算。
- Deflect、Block 和普通命中的优先级裁决。
- HP 和 Posture 结果计算。
- Deflect/Block 对攻击方的反作用结果。

该模块不加载资产、不播放动画、不持有 UObject，便于独立测试。

### 3.4 `ASKWeapon`

武器层保留以下职责：

- 在动画曲线开启期间执行刀刃 Sweep。
- 排除 Owner、自身、无效阵营和已经命中的目标。
- 生成原始 `FSKCombatHit`。
- 将命中提交给 Owner 的 `USKCombatComponent`。

武器层不再直接调用 `TakeDamage`。

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
| `CommittedAttackSide` | 当前动作实际攻击侧；动作开始时固定，命中期间不变化。 |
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
    BlockReaction,
    DeflectReaction,
    HitStun,
    PostureBroken,
    Dead,
};
```

枚举只表达通用状态。`Right`、`Combo_01` 等具体动作 ID 保存在 Lua 配置和运行时字符串/FName 中。

### 4.3 原始命中

```cpp
USTRUCT(BlueprintType)
struct FSKCombatHit
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Attacker;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> Target;

    UPROPERTY(BlueprintReadOnly)
    FVector HitLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly)
    FVector HitDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly)
    FName AttackId = NAME_None;

    UPROPERTY(BlueprintReadOnly)
    ESKAttackSide AttackSide = ESKAttackSide::None;

    UPROPERTY(BlueprintReadOnly)
    int32 ActionSerial = 0;
};
```

### 4.4 命中结果

```cpp
UENUM(BlueprintType)
enum class ESKCombatHitOutcome : uint8
{
    Invalid,
    Evaded,
    Deflected,
    Blocked,
    Hit,
};
```

结果数据还应包含 HPDamage、TargetPostureDamage、AttackerPostureDamage、反应级别和中断原因。

## 五、Lua 动作数据

### 5.1 动作定义

```lua
---@class SKAttackActionConfig
---@field Animation string 动画对象路径。
---@field Kind string Light 或 Heavy。
---@field Side string 当前动作攻击侧。
---@field LightNext string|nil 有效轻攻击输入对应的下一动作。
---@field HeavyNextBySide table<string, string>|nil 下一侧到重攻击动作的映射。
---@field bBlockable boolean 是否允许格挡。
---@field bDeflectable boolean 是否允许弹反。
---@field HPDamage number 基础 HP 伤害。
---@field PostureDamage number 基础架势伤害。
---@field DeflectReactionType integer 弹反动作类型。
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
            Left = "Heavy_Left",
        },
        bBlockable = true,
        bDeflectable = true,
    },
    Left = {
        Animation = AnimAssets.Attack.Left,
        Kind = "Light",
        Side = "Left",
        LightNext = "Combo_01",
        HeavyNextBySide = {
            Right = "Heavy_Right",
        },
        bBlockable = true,
        bDeflectable = true,
    },
    Heavy_Right = {
        Animation = AnimAssets.Attack.Charged_Thrust_Right,
        Kind = "Heavy",
        Side = "Right",
        LightNext = "Left",
        HeavyNextBySide = {
            Left = "Heavy_Left",
        },
        bBlockable = true,
        bDeflectable = true,
    },
    Heavy_Left = {
        Animation = AnimAssets.Attack.Charged_Thrust_Left,
        Kind = "Heavy",
        Side = "Left",
        LightNext = "Combo_01",
        HeavyNextBySide = {
            Right = "Heavy_Right",
        },
        bBlockable = true,
        bDeflectable = true,
    },
}
```

`Combo_01~03` 的 `Side` 必须在人工核验动画后填写。配置加载时应检查动作 Side 与对应资产曲线的预期是否一致。

### 5.2 运行时状态

```lua
---@class SKAttackRuntime
---@field ActionId string|nil
---@field ActionSerial integer
---@field State string
---@field CommittedAttackSide string
---@field NextAttackSide string
---@field PendingInput SKPendingAttackInput|nil
---@field bSideCurveArmed boolean
---@field bSideCurveConsumed boolean
---@field PreviousSideCurveValue number
---@field HitActors table
```

### 5.3 输入候选

```lua
---@class SKPendingAttackInput
---@field SourceActionSerial integer
---@field PressTime number
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
| `AttackHitbox` | int enum | `0=None`，其他值为攻击盒类型 | 禁止产生攻击命中。 |
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

推荐示例：

```text
Right：       HitboxEnd -> AttackSide=-1 -> Light/HeavyWindow -> 0
Left：        HitboxEnd -> AttackSide= 1 -> Light/HeavyWindow -> 0
Heavy_Right： HitboxEnd -> AttackSide=-1 -> Light/HeavyWindow -> 0
Heavy_Left：  HitboxEnd -> AttackSide= 1 -> Light/HeavyWindow -> 0
```

### 6.3 曲线采样位置

战斗逻辑不得直接信任最终混合 Pose 的枚举曲线值。全身 Slot 淡入淡出时，Left、Right 和 0 可能被混合成中间浮点值。

推荐由 `USKCombatComponent` 保存当前战斗 `UAnimSequence` 和动态 Montage，通过 Montage 当前时间换算到 Sequence 本地时间，再提供通用接口：

```cpp
UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
float SampleActiveActionCurve(FName CurveName) const;
```

如果第一阶段只能读取 `UAnimInstance::GetCurveValue`，必须同时使用：

- 稳定阈值：`>= 0.75` 视为 Right，`<= -0.75` 视为 Left。
- 零值武装：每个新动作先观察到 `Abs(Value) < 0.25` 才允许提交。
- 每动作一次性消费。
- 动作序列号校验。
- `AttackHitbox == 0` 校验。

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

`CommittedAttackSide` 随当前攻击写入 `FSKCombatHit`。它不得被本动作后续曲线修改。

### 7.2 曲线提交下一侧

```lua
local function update_next_attack_side(self)
    local runtime = self.Runtime
    if runtime.ActionId == nil then
        return
    end

    if self:SampleActiveActionCurve("AttackHitbox") > 0.5 then
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

攻击按下被窗口接受时立即保存：

```lua
runtime.PendingInput = {
    SourceActionSerial = runtime.ActionSerial,
    PressTime = current_time,
    SideSnapshot = runtime.NextAttackSide,
    bLightAccepted = light_curve > 0.5,
    bHeavyAccepted = heavy_curve > 0.5,
    ResolvedKind = nil,
    bConsumed = false,
}
```

后续侧向曲线不得改变 `SideSnapshot`。

## 八、轻重输入分类

### 8.1 Neutral 输入

Neutral 状态没有前一动画窗口：

- Attack Started 创建初始候选。
- 阈值前释放：播放 `Right`。
- 达到阈值：播放 `InitialHeavySide` 对应重攻击，默认 `Heavy_Right`。

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
| `Right` | Right | Left | `Left` | `Heavy_Left` |
| `Left` | Left | Right | `Combo_01` | `Heavy_Right` |
| `Heavy_Right` | Right | Left | `Left` | `Heavy_Left` |
| `Heavy_Left` | Left | Right | `Combo_01` | `Heavy_Right` |
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

`CombatFullBodySlot` 必须覆盖完整骨架，承担轻攻击、重攻击、Block、Deflect 和 HitStun 等离散动作。

持续 Guard 使用状态机而不是循环动态 Montage：

```text
Raise -> GuardIdle/GuardMove -> Lower
```

### 10.2 Foot IK

全身攻击、Block、Deflect 和 HitStun 期间，将 `FootIKAlpha` 淡出到 0，避免 IK 修改攻击脚步和 Root Motion。回到 Neutral/GuardMove 后再平滑恢复。

### 10.3 Root Motion

- 地面轻重攻击：启用 Root Motion。
- Guard Idle/Move：按资产实际情况核验，禁止同时由 Root Motion 和 MovementComponent 重复位移。
- Deflect/Block：默认禁用位移型 Root Motion，只保留姿势；最终以资产核验结果为准。
- 空中攻击：空中轨迹继续由 CharacterMovement 驱动，忽略水平 Root Motion。

## 十一、攻击盒和命中

### 11.1 曲线边沿

战斗组件逐帧采样 `AttackHitbox`：

```text
0 -> 非0：
  ClearWeaponHitActors
  保存 ActiveAttackId、CommittedAttackSide、ActionSerial
  ActivateWeaponHitbox

非0 -> 0：
  DeactivateWeaponHitbox
```

动作自然结束或任何中断都必须执行幂等的 `DeactivateWeaponHitbox`。

### 11.2 连续刀刃 Sweep

现有 BeginOverlap 可作为初期回退，但正式实现建议使用刀刃根部和尖端的前后帧 Sweep：

```text
PreviousBladeBase -> CurrentBladeBase
PreviousBladeTip  -> CurrentBladeTip
```

使用 Sphere/Capsule Trace 合并覆盖刀刃扫过区域，并对高速位移执行可配置子步。命中后仍使用当前 `AlreadyHitActors` 语义去重。

### 11.3 命中提交

攻击方流程：

```text
Weapon DetectHit
-> AttackerCombat.SubmitWeaponHit
-> TargetCombat.ResolveIncomingHit
-> Target 返回 FSKCombatResult
-> AttackerCombat.ApplyOutgoingHitResult
```

`FSKCombatHit.AttackSide` 必须使用 `CommittedAttackSide`，不得使用已经切换的 `NextAttackSide`。

## 十二、防御裁决

### 12.1 防御方向

```text
ToAttacker = Normalize(AttackerLocation - DefenderLocation)
FacingDot = Dot(DefenderForward, ToAttacker)
bInsideGuardArc = FacingDot >= cos(GuardHalfAngle)
```

`GuardHalfAngle`、DeflectWindow 和攻击的 bBlockable/bDeflectable 均来自配置。

### 12.2 结果优先级

```lua
if target:IsInvulnerable() then
    return "Evaded"
end

if not inside_guard_arc or attack.bBlockable ~= true then
    return "Hit"
end

if attack.bDeflectable == true
    and target:IsGuardHeld()
    and time_since_guard_press <= config.DeflectWindow then
    return "Deflected"
end

if target:IsGuardHeld() then
    return "Blocked"
end

return "Hit"
```

### 12.3 双方结果

| 结果 | 防御方 | 攻击方 |
| --- | --- | --- |
| Evaded | 无伤害 | 当前攻击可继续或按配置结束。 |
| Deflected | 无 HP 伤害，播放左右弹反反馈 | 增加架势、攻击立即中断、进入较强硬直。 |
| Blocked | 增加架势，播放 Shake/Interrupted | 攻击立即中断、进入较短回弹。 |
| Hit | 扣除 HP 和架势，播放受击 | 当前攻击命中成功并按配置继续。 |

用户规则要求 Block 和 Deflect 均打断轻攻击与重攻击，因此这两个结果不检查攻击方的主动取消曲线。

## 十三、中断和清理

所有攻击退出统一经过：

```lua
local function interrupt_attack(self, reason)
    local old_serial = self.Runtime.ActionSerial

    self:InvalidateCombatAction(old_serial)
    self:DeactivateWeaponHitbox()
    self:ClearWeaponHitActors()
    self:StopCombatAnimation(self.Config.InterruptBlendOutTime)

    self.Runtime.ActionId = nil
    self.Runtime.PendingInput = nil
    self.Runtime.bSideCurveArmed = false
    self.Runtime.bSideCurveConsumed = false
    self.Runtime.State = "Neutral"
end
```

实际目标状态由中断原因覆盖，例如 DeflectedStagger、BlockRecoil、HitStun 或 PostureBroken。清理函数必须幂等，允许 Montage End、曲线关闭和命中反馈在同一帧重复调用。

### 13.1 序列号规则

- 开始动作：递增 `ActionSerial`。
- 输入候选：保存创建时的 Serial。
- 命中数据：保存攻击发生时的 Serial。
- 中断动作：立即使当前 Serial 失效。
- 迟到输入、曲线边沿、Montage End 或命中结果：Serial 不匹配时丢弃。

### 13.2 状态重置

| 退出原因 | 清除输入 | 重置轻攻击链 | 保留最后攻击侧 |
| --- | --- | --- | --- |
| 自然结束 | 是 | 是 | 可保留，仅用于调试。 |
| Block | 是 | 是 | 是，用于左右回弹。 |
| Deflect | 是 | 是 | 是，用于左右硬直。 |
| HitStun | 是 | 是 | 可保留。 |
| Guard/Dodge 主动取消 | 是 | 是 | 可保留。 |
| 进入下一攻击 | 只消费当前候选 | 否 | 更新为新动作侧。 |

## 十四、Tick 与执行顺序

推荐顺序：

```text
1. 采集输入边沿和保持时长
2. 校验当前动作/序列号
3. 采样 AttackHitbox 和窗口曲线
4. 处理 Hitbox 开关边沿
5. 处理 AttackSide 提交
6. 创建或更新攻击输入候选
7. 按优先级处理外部中断
8. 解析 Light/Heavy 后续动作
9. 更新 HP/Posture 恢复
10. 输出 AnimBP 状态和调试信息
```

Block、Deflect、Hit 和 PostureBroken 等外部中断必须在执行续段前处理，确保同帧防御结果可以作废已排队攻击。

## 十五、配置

第一阶段可使用 `CombatConfig.lua`：

```lua
local CombatConfig = {
    HeavyHoldThreshold = 0.30,
    InitialHeavySide = "Right",
    DeflectWindow = 0.18,
    GuardHalfAngle = 120.0,
    BlockHPDamageRatio = 0.0,
    BlockPostureRatio = 0.7,
    DeflectAttackerPostureDamage = 20.0,
    BlockRecoilDuration = 0.10,
    DeflectStaggerDuration = 0.15,
    PostureRecoveryDelay = 1.50,
    PostureRecoveryRate = 30.0,
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
HitOutcome
InterruptReason
```

建议扩展 `ShowDebug Animation` 或新增 `ShowDebug Combat`：

```text
State=HeavyAttack Action=Heavy_Right Serial=42
CommittedSide=Right NextSide=Left
Hitbox=0 LightWindow=1 HeavyWindow=1
Pending=Heavy PressAge=0.31 SideSnapshot=Left
HP=100/100 Posture=35/100
```

## 十七、验证方案

### 17.1 纯逻辑测试

- `Right + Light(Left)` 返回 `Left`。
- `Right + Heavy(Left)` 返回 `Heavy_Left`。
- `Heavy_Left + Heavy(Right)` 返回 `Heavy_Right`。
- `Heavy_Right + Light(Left)` 返回 `Left`。
- Combo 中 Heavy 输入不能返回 Heavy 动作。
- Serial 失效后 PendingInput 不可执行。
- Block/Deflect 与续段同帧时优先中断。

### 17.2 曲线测试

- 每个攻击动画包含必需曲线。
- Hitbox 非零区间与 AttackSide 非零区间不重叠。
- AttackSide 在输入窗口开始前已经稳定。
- Right/Heavy_Right 提交 Left。
- Left/Heavy_Left 提交 Right。
- Combo 曲线侧向与人工核验结果一致。
- 动画末尾 AttackSide 回零。

### 17.3 PIE 集成测试

1. 完成五段轻攻击链。
2. 在每一段窗口外按键，验证不续段。
3. Right/Left 分别长按进入相反侧重攻击。
4. 连续长按验证重攻击左右交替。
5. 在重攻击窗口短按，验证返回正确轻攻击节点。
6. 对轻攻击执行 Block 和 Deflect，验证攻击立即停止。
7. 对重攻击执行 Block 和 Deflect，验证攻击立即停止。
8. 在低帧率模式验证曲线、命中和侧向不漏采样。
9. 在 Montage 淡入淡出期间验证 AttackSide 不重复提交。
10. 连续中断一百次，验证没有残留攻击盒和旧输入。

## 十八、实施顺序

### 阶段 0：资产与曲线审计

1. 人工确认全部攻击动画的当前挥刀侧。
2. 确认 Root Motion、循环属性和源采样率。
3. 标注 AttackHitbox、AttackSide 和四条窗口曲线。
4. 生成曲线审计报告并验证互斥关系。

### 阶段 1：攻击状态机

1. 新增 Combat 类型和 `USKCombatComponent` 通用宿主。
2. 增加 `CombatFullBodySlot`。
3. 实现 Lua 动作表、动作序列号和统一中断。
4. 跑通五段轻攻击链。

### 阶段 2：重攻击和侧向

1. 实现窗口内按下候选和长按分类。
2. 实现 AttackSide 曲线提交和 SideSnapshot。
3. 跑通左右重攻击循环和重转轻。

### 阶段 3：碰撞与伤害

1. 移除武器硬编码伤害。
2. 实现连续刀刃 Sweep。
3. 实现通用命中结果和 HP/Posture。

### 阶段 4：防御与弹反

1. 实现 Guard 状态机和方向检查。
2. 实现 Block/Deflect 裁决。
3. 接入双方中断、架势和反馈动画。

### 阶段 5：表现和扩展

1. 命中停顿、VFX、SFX 和镜头反馈。
2. 空中攻击和落地攻击。
3. 专用攻击回弹、普通受击和架势崩坏动画。

## 十九、风险与待确认项

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| Combo 动画左右侧尚未确认 | 后续 Side 曲线和弹反方向可能错误 | 阶段 0 人工逐帧核验。 |
| 当前 TAE 提取数据与曲线消费者不完整 | 无法直接批量生成可靠窗口 | 先小批量人工标注和审计，修复源数据后再批量。 |
| 枚举曲线参与 Montage 混合 | 可能得到中间值或旧曲线残留 | 优先采样活动 Sequence；否则使用武装、阈值、Serial 和一次性消费。 |
| BeginOverlap 高速漏判 | 快速重攻击可能穿透目标 | 改为刀刃连续 Sweep。 |
| 缺少攻击方回弹专用动画 | Block/Deflect 反馈不完整 | 第一阶段逻辑硬直和短 BlendOut 占位，后续补资产。 |
| Root Motion 与 Foot IK 冲突 | 攻击脚步漂移或角色位移异常 | 全身动作关闭 IK，并逐资产核验 Root Motion。 |
| 同键轻重识别造成轻攻击延迟 | 首段攻击手感迟滞 | 先验证 0.30s 方案，再评估 Tap/Hold Trigger 或共享起手动画。 |

## 二十、完成定义

满足以下条件后，本系统核心版本可以判定完成：

1. 需求文档中 15.1 至 15.4 的全部验收项通过。
2. 所有攻击动作由曲线驱动 Hitbox、AttackSide 和输入窗口。
3. 当前攻击侧在命中期间稳定，下一攻击侧只提交一次。
4. 五段轻攻击、连续重攻击和重转轻全部按转换表运行。
5. 轻攻击和重攻击均能被 Block/Deflect 正确中断。
6. 所有中断路径均无残留 Hitbox、输入候选和旧动作回调。
7. C++ 源码中不存在具体攻击资产路径、连段名称和硬编码伤害。
