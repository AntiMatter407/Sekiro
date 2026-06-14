# TAE→UE 动画蓝图迁移方案

> **状态**: 草稿
> **最后更新**: 2026-06-13
> **支撑 GDD**: animation-system.md, attack-combo-system.md

## 1. 概述 (Overview)

### 1.1 核心问题

FS（FromSoftware）动画系统和 UE Animation Blueprint 是两种根本不同的架构范式：

| 维度 | FS 原始系统 | UE Animation Blueprint |
|---|---|---|
| 编辑器 | 时间轴+事件块（TimeActEditor） | 节点图（AnimGraph + State Machine） |
| 状态切换 | JumpTable 事件嵌入动画帧中 | State Machine Transition 在图中连线 |
| 条件判断 | HKS + JumpTable 参数 | Transition Rule 节点 |
| 动画混合 | TAE Type 16 Blend + HKS 层控制 | BlendSpace / Layered Blend Per Bone |
| 事件触发 | TAE Action（Type + 帧时间 + 参数） | AnimNotify / NotifyState |
| 状态变量 | 引擎内部 BitField (0x78/0x7C/0x80) | AnimInstance 成员变量 + GameplayTag |
| 数据格式 | 私有二进制 .tae + .hkx | .uasset（序列化的蓝图图） |
| 脚本绑定 | HKS (Havok Script) Lua | AnimInstance Native C++ + Blueprint |

**一句话总结**：FS 将"状态机转换条件"作为时间轴事件嵌入动画内部（动画知道自己在什么帧可以切换到哪个动作），而 UE 将转换条件放在状态机图的连线上（状态图知道什么条件触发什么动画）。FS 是"动画推送"模式，UE 是"状态机拉取"模式。

### 1.2 迁移策略

本方案通过三层架构桥接这两种范式：

```
Layer 1: AnimInstance C++ (USKAnimInstance)
         ├── 运行时状态变量（GameplayTag + Enum）
         ├── 输入意图缓存（Attack/Dodge/Guard/Item...）
         ├── SK_AnimLogicData 查询接口（CanCancelTo / GetAttackHitbox...）
         └── 优先级打断系统

Layer 2: Animation Blueprint (ABP_Sekiro)
         ├── Locomotion 状态机（BlendSpace 驱动）
         ├── Combat 状态机（Montage 槽位 + 取消规则）
         ├── Reaction 状态机（Hit/Death/Resurrection）
         └── Transition Rules（条件判断 + CanCancelTo 查询）

Layer 3: Data Assets (SK_AnimLogicData)
         └── TAE 帧级事件 → 运行时可查询的取消规则/攻击盒/SpEffect
```

### 1.3 与当前实现的关系

当前 `FSekiroAnimBlueprintBuilder::BuildStateMachine()` 生成的 ABP 是第一版原型（18 类别状态 + 64 静态过渡）。本方案是第二代设计，核心变化：

1. 从"类别状态"改为"逻辑状态"（Locomotion/Combat/Reaction 三层）
2. 过渡条件从"始终可取消"改为运行时通过 DataAsset 查询
3. 新增 AnimInstance C++ 类承载所有运行时逻辑
4. Locomotion 用 BlendSpace 替代离散的 SequencePlayer

---

*[后续章节待填充]*

## 2. 玩家幻想 (Player Fantasy)

- **零延迟响应**：按下攻击/闪避/防御键的瞬间，如果当前动画允许取消，角色应在 1 帧内开始过渡
- **流畅取消**：动作取消时通过 Crossfade 平滑过渡（≤ 0.1s），无跳帧/瞬移
- **有力反馈**：攻击命中时 AnimNotify 驱动 FreezeFrame + 火花特效 + 手柄震动，让玩家"感受"到刀刃碰撞
- **精确帧对齐**：攻击盒的生成/销毁与动画帧精确对齐，不应出现攻击盒残影或延迟

## 3. 详细规则

### 3.1 AnimInstance C++ 类：USKAnimInstance

AnimInstance 是 FS BitField 状态机在 UE 中的等价物。它持有所有运行时状态变量，供 ABP 状态机的 Transition Rule 查询。

```cpp
UCLASS()
class SEKIROIMPORT_API USKAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    // ── BlendSpace 参数（移动系统写入） ─────────────────
    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Speed = 0.f;           // 0 ~ SprintSpeed (cm/s)

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Direction = 0.f;       // -180° ~ 180°, 0 = 前方

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    bool bIsLockedOn = false;

    // ── 动作状态（当前播放的动画身份） ─────────────────
    UPROPERTY(BlueprintReadOnly, Category = "State")
    FGameplayTag CurrentActionTag;   // 如 "Action.Attack.R1.Combo01"

    UPROPERTY(BlueprintReadOnly, Category = "State")
    int32 CurrentAnimID = 0;         // TAE AnimID, 用于查询 SK_AnimLogicData

    UPROPERTY(BlueprintReadOnly, Category = "State")
    float CurrentAnimTime = 0.f;     // 当前动画播放时间(s)
    
    UPROPERTY(BlueprintReadOnly, Category = "State")
    float CurrentAnimLength = 0.f;

    // ── JumpTable 等价标志（TAE 事件驱动） ────────────
    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    bool bCanDeflect = false;        // JumpTableID 119: EnableParry

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    bool bIsDodging = false;         // JumpTableID 8: FlagAsDodging

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    bool bDisableTurning = false;    // JumpTableID 7

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    bool bDisableMovement = false;   // JumpTableID 89

    // ── 输入意图（由 Character/InputSystem 每帧写入） ──
    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FGameplayTag InputIntent;  // "Input.Attack", "Input.Dodge", "Input.Guard"...

    // ── 数据资产引用 ─────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
    TObjectPtr<USKAnimationLogicData> AnimLogicData;

    // ── 查询接口 ───────────────────────────────────
    /// 查询当前动画是否可在当前帧取消到目标动作
    UFUNCTION(BlueprintCallable, Category = "Cancel")
    bool CanCancelTo(FGameplayTag TargetAction, float& OutCrossfade) const;

    /// 获取当前帧的攻击盒配置
    UFUNCTION(BlueprintCallable, Category = "Attack")
    bool GetCurrentHitbox(FSKAttackHitboxConfig& OutConfig) const;

    // ── 生命周期 ───────────────────────────────────
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativeInitializeAnimation() override;
};
```

### 3.2 状态机层次结构

ABP_Sekiro 的状态机按三层划分，每一层有独立的职责和优先级：

```
Root State Machine
├── [Layer 1] Locomotion State Machine (BlendSpace)
│   ├── Idle      ← Speed = 0
│   ├── Walk      ← Speed ∈ (0, 200]
│   ├── Jog       ← Speed ∈ (200, 400]
│   └── Sprint    ← Speed > 400
│
├── [Layer 2] Combat State Machine (蒙太奇槽位)
│   │  ┌─ DefaultSlot ──────────────────────┐
│   │  │  Attack_R1 (Combo01→Combo02→...)   │
│   │  │  Attack_Charged                     │
│   │  │  Attack_Jump                        │
│   │  │  Attack_Crouch                      │
│   │  │  Guard / Deflect                    │
│   │  │  Dodge (Fwd/Back/Left/Right)        │
│   │  │  Quickstep                          │
│   │  │  Prosthetic (L2 义手)               │
│   │  │  ItemUse                            │
│   │  └─────────────────────────────────────┘
│   │  ┌─ UpperBodySlot ─────────────────────┐
│   │  │  (移动中攻击/防御，上半身与下半身分层)│
│   │  └─────────────────────────────────────┘
│   │
│   │  Transition Rules: InputIntent + CanCancelTo(AnimID, Time, TargetAction)
│   │  优先级: 闪避 > 防御 > 义手 > 攻击 (低优先级可被高优先级打断)
│
└── [Layer 3] Reaction State Machine (最高优先级)
    ├── Hit_Light      ← 受击-轻
    ├── Hit_Heavy      ← 受击-重
    ├── Knockback      ← 击退/击飞
    ├── Death          ← HP=0 (非忍杀)
    ├── Resurrection   ← 回生
    └── Deathblow      ← 忍杀演出
        Priority: Always > Combat & Locomotion
```

### 3.3 JumpTable → Transition Rule 映射表

将 FS 的 JumpTable Action 映射为 UE ABP Transition Rule 的 GetVariable + CanCancelTo 表达式：

| JumpTable | ID | Target GameplayTag | Transition Rule 伪代码 |
|---|---|---|---|
| AnimCancelStart_R1 | 1 | `Action.Attack` | `InputIntent == Attack && CanCancelTo(Attack, Crossfade)` |
| AnimCancelStart_L1 | 9 | `Action.Deflect` | `InputIntent == Deflect && CanCancelTo(Deflect, Crossfade)` |
| AnimCancelStart_Guard | 21 | `Action.Guard` | `InputIntent == Guard && CanCancelTo(Guard, Crossfade)` |
| AnimCancelStart_Dodge | 25 | `Action.Dodge` | `InputIntent == Dodge && CanCancelTo(Dodge, Crossfade)` |
| AnimCancelStart_Item | 30 | `Action.ItemUse` | `InputIntent == Item && CanCancelTo(Item, Crossfade)` |
| AnimCancelStart_L2 | 105 | `Action.Prosthetic` | `InputIntent == Prosthetic && CanCancelTo(Prosthetic, Crossfade)` |
| AnimCancelStart_Emergency | 111 | `Action.Dodge` | `InputIntent == Dodge && CanCancelTo(Dodge, Crossfade)` |
| AnimCancelEnd_General | 34 | — | 关闭所有取消窗口 |
| AnimCancelEnd_R1 | 115 | — | 关闭 R1 取消窗口 |
| AnimCancelEnd_L1 | 117 | — | 关闭 L1 取消窗口 |
| AnimCancelEnd_L2 | 118 | — | 关闭 L2 取消窗口 |
| AnimCancelEnd_Item | 107 | — | 关闭道具取消窗口 |
| AnimCancelEnd_Emergency | 112 | — | 关闭紧急闪避窗口 |
| EnableParry | 119 | — | `Set bCanDeflect = true` |
| FlagAsDodging | 8 | — | `Set bIsDodging = true` |
| DisableTurning | 7 | — | `Set bDisableTurning = true` |
| DisableAllMovement | 89 | — | `Set bDisableMovement = true` |
| LimitMoveSpeedWalk | 90 | — | `Set MaxSpeed = WalkSpeed` |
| LimitMoveSpeedDash | 91 | — | `Set MaxSpeed = DashSpeed` |
| InvokeAttackAction | 87 | — | 触发特定攻击行为 |
| SetHeightCorrection | 113 | — | 调整角色高度 |

### 3.4 运行时 Tick 流程

```
NativeUpdateAnimation(DeltaTime):
  1. 从 Character 获取 Speed/Direction/bIsLockedOn
  2. 从当前 Montage/Sequence 获取 CurrentAnimID, CurrentAnimTime
  3. 读取 InputIntent（由 Character::SetupPlayerInputComponent 写入）
  4. 如果 InputIntent 不为 None:
     a. 调用 AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, InputIntent, OutCrossfade)
     b. 如果返回 true → 在 AnimInstance 中设置 TransitionRequest 标志
     c. ABP Transition Rule 检测到 TransitionRequest → 触发状态切换
  5. 更新 JumpTable 等价标志（bCanDeflect, bIsDodging...）
     — 由 Notify 在特定帧设置/复位
```

### 3.5 CanCancelTo 查询实现

```cpp
bool USKAnimInstance::CanCancelTo(FGameplayTag TargetAction, float& OutCrossfade) const
{
    if (!AnimLogicData) return false;
    
    const FSKCancelRule* Rule = AnimLogicData->CancelRules.Find(CurrentAnimID);
    if (!Rule) return false;
    
    int32 CurrentFrame = FMath::RoundToInt(CurrentAnimTime * 30.f); // 假设 30fps
    
    if (CurrentFrame >= Rule->StartFrame && CurrentFrame <= Rule->EndFrame)
    {
        if (Rule->TargetAction == TargetAction)
        {
            OutCrossfade = Rule->CrossfadeDuration;
            return true;
        }
    }
    return false;
}
```

### 3.6 Locomotion BlendSpace 定义

**BS_Sekiro_Locomotion** (1D BlendSpace, X 轴 = Speed):

```
轴范围: X=0 (Speed=0) to X=600 (Speed=SprintMax)
采样点:
  Speed    0: Anim_Sekiro_Idle_Default
  Speed  150: Anim_Sekiro_Walk_Fwd
  Speed  300: Anim_Sekiro_Jog_Fwd  
  Speed  450: Anim_Sekiro_Run_Fwd
  Speed  600: Anim_Sekiro_Sprint_Fwd
```

锁定状态下使用独立 BlendSpace **BS_Sekiro_Locomotion_Locked**，加入方向维度。

### 3.7 新增 Notify 类型

| Notify 类 | 触发帧 | 作用 |
|---|---|---|
| `ANSK_CancelWindowNotify` | TAE CancelWindow StartFrame | 设置 AnimInstance 的取消窗口激活标志 |
| `ANSK_CancelWindowNotifyState` | TAE CancelWindow StartFrame→EndFrame | 区间内 CanCancelTo 返回 true |
| `ANSK_SetFlagNotify` | JumpTable 事件的 StartFrame | 设置 bCanDeflect / bIsDodging 等标志 |
| `ANSK_ClearFlagNotify` | JumpTable 事件的 EndFrame | 复位标志 |
| `ANSK_AttackHitboxNotify` | AttackHitbox StartFrame | 通过 GameplayAbility 生成攻击盒 |
| `ANSK_SEventNotify` | SoundEvent Frame | 触发音效 |
| `ANSK_FFXNotify` | FFXEvent StartFrame | 触发射弹/视觉特效 |
| `ANSK_FootstepNotify` | FootStep Frame | 触发脚步声 |

## 4. 公式 (Formulas)

### 4.1 CancelWindow 判定

```
变量:
  CurrentFrame = RoundToInt(CurrentAnimTime × 30)  // 换算为 30fps 帧号
  WindowStart  = CancelRule.StartFrame
  WindowEnd    = CancelRule.EndFrame

判定:
  CanCancel = (CurrentFrame ≥ WindowStart) AND (CurrentFrame ≤ WindowEnd)
              AND (InputIntent == CancelRule.TargetAction)
              AND (InputIntent.Priority > CurrentAction.Priority)  // 优先级打断

示例:
  AnimID=100000 (R1_Combo01), CurrentTime=0.5s → CurrentFrame=15
  CancelRule: StartFrame=10, EndFrame=25, Target=Attack, Priority=5
  玩家在第0.5秒按下攻击 → CanCancel=true, Crossfade=0.1s
```

### 4.2 优先级打断公式

```
优先级表（数字越大优先级越高）:
  10 = Deathblow / Resurrection
  9  = Death
  8  = Hit (Light/Heavy/Knockback)
  7  = Dodge (包含 Emergency Dodge)
  6  = Deflect (弹刀)
  5  = Guard
  4  = Prosthetic (义手 L2)
  3  = ItemUse (喝药)
  2  = Attack (R1 连段/蓄力/跳斩/蹲斩)
  1  = Quickstep

TransitionRule 通过 CanInterrupt(CurrentPriority, RequestedPriority):
  return RequestedPriority > CurrentPriority
```

### 4.3 BlendSpace 参数映射

```
Speed    = VSize(Velocity × Vector(1,1,0))     // cm/s, 0~600
Direction = AngleBetween(CharacterForward, VelocityDirection)  // -180°~180°
            LockedOn 时以目标方向为参考
            bIsInCombat 时使用不同的最大速度阈值
```

### 4.4 Crossfade 持续时间

```
CrossfadeDuration = Min(TransitionRule.CrossfadeDuration, BlendOutLimit)
BlendOutLimit = 0.1s  // 硬上限，确保响应速度

特殊情况:
  弹刀成功 → Crossfade = 0.05s (比普通更快，强调即时反馈)
  受击 → Crossfade = 0.05s (被击中立即切受击动画)
```

## 5. 边界情况 (Edge Cases)

| 场景 | 预期行为 | 理由 |
|---|---|---|
| 两个输入在同一帧到达 | 按优先级选择最高的执行，忽略低优先级 | 不能同时执行两个动作 |
| CancelWindow 在 BlendOut 期间 | Notify 在 BlendOut 期间不触发，CanCancelTo 返回 false | 防止过渡期间误触发攻击盒 |
| 当前动画无 CancelWindow 数据 | CanCancelTo 返回 false，只能等动画播放完毕 | 保守策略，防止非法取消 |
| AnimLogicData 为 null | CanCancelTo 返回 false，日志警告一次 | 缺失配置不应崩溃 |
| 帧率低于 30fps | CanCancelTo 使用 DeltaTime 累积补偿，窗口判定使用浮点时间而非整数帧 | 低帧率下取消窗口不应缩窄 |
| Montage 被打断时 Notify 触发 | AnimNotifyState 的 End 分支在 BlendOut 时跳过 | 攻击盒不应在取消时残影 |
| 同一动画有多个 CancelWindow | 取覆盖当前帧的第一个窗口（按 StartFrame 排序） | 确定性行为 |
| ComboNextAnimID 指向不存在的动画 | CanCancelTo 返回 false，播放完当前动画后返回 Idle | 缺失连段不应崩溃 |
| JumpTable 标志帧与 CancelWindow 重叠 | 标志设置优先——先设置 bCanDeflect，再检查 CanCancelTo | 弹刀帧必须在取消判定之前生效 |
| SK_AnimLogicData 中 AnimID 不存在 | Find 返回 nullptr，CanCancelTo 返回 false | 安静失败 |

## 6. 依赖关系

### 6.1 本系统依赖的外部系统

| 系统 | 依赖内容 | 说明 |
|---|---|---|
| **SekiroImport 资产管线** | SK_AnimLogicData（DataAsset）、AnimSequence、BlendSpace、ABP_Sekiro | 所有运行时数据由 TAE 导入构建管线生成 |
| **输入系统** | InputIntent GameplayTag、移动输入（Speed/Direction） | 驱动状态机切换和 BlendSpace |
| **移动系统** | 角色 Speed/Direction/bIsLockedOn 每帧写入 AnimInstance | BlendSpace 参数源 |
| **弹刀系统** | 写入弹刀结果到 AnimInstance，触发 Deflect 状态切换 | 弹刀成功→立即切换到反击动画 |
| **碰撞检测** | 读取 AnimNotify 的 FrameInterval → 攻击盒生命周期 | 攻击盒由 Notify 帧驱动，非 Tick 驱动 |
| **GameplayTag 管理器** | Action.* 和 Input.* 标签注册 | 所有状态和输入意图用 GameplayTag 表示 |

### 6.2 依赖本系统的外部系统

| 系统 | 依赖内容 | 说明 |
|---|---|---|
| **碰撞检测** | AnimNotify 在指定帧触发攻击盒 | 攻击盒的开始/结束帧由 TAE AttackHitbox 事件确定 |
| **弹刀系统** | bCanDeflect 标志、弹刀窗口 Notify | 弹刀的判定帧窗口由 TAE EnableParry 事件确定 |
| **伤害计算** | 攻击盒命中时通过 Notify 传递 BehaviorJudgeID | 伤害类型/数值由 BehaviorJudgeID 映射 |
| **音效系统** | ANSK_SEventNotify 在指定帧触发 SoundID | SoundID 由 TAE PlaySound 事件确定 |
| **特效系统** | ANSK_FFXNotify 在指定帧触发 FFXID | 视觉特效由 TAE SpawnFFX 事件确定 |
| **忍杀系统** | Deathblow 状态请求和 Montage 播放 | 忍杀演出由 GameplayAbility 触发 |

### 6.3 双向依赖协议

- **AnimInstance ↔ 碰撞检测**：AnimInstance 通过 Notify 通知碰撞系统"攻击盒现在激活"，碰撞系统反馈"命中目标 X"通过 AnimInstance 的 HitResult 变量传递给 ABP Transition Rule
- **AnimInstance ↔ 弹刀系统**：AnimInstance 通过 Notify 设置 bCanDeflect 窗口，弹刀系统判断成功后写回 DeflectSuccess 标志到 AnimInstance
- **AnimInstance ↔ 输入系统**：输入系统每帧写入 InputIntent，AnimInstance 在下一帧的 NativeUpdateAnimation 中消费

## 7. 调节旋钮 (Tuning Knobs)

| 参数 | 存储位置 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|---|---|---|---|---|---|
| CrossfadeBlendOutMax | AnimInstance CDO | 0.1s | 0.05-0.2 | 过渡更平滑但响应延迟 | 响应更快但可能跳帧 |
| DeflectCrossfadeBoost | AnimInstance CDO | 0.05s | 0.03-0.08 | 弹刀后过渡更平滑 | 弹刀反击更快但不自然 |
| CancelWindowTolerance | DataAsset | 1帧 | 0-3 | 取消更宽容（提前/延后） | 取消更严格（精确到帧） |
| PriorityTable | AnimInstance CDO | 见4.2节 | — | 高优先级动作更难被打断 | 低优先级动作更容易被打断 |
| BlendSpaceInterpolationSpeed | ABP BlendSpace 节点 | 5.0 | 2-10 | 移动切换更平滑 | 移动切换更即时 |
| FreezeFrameDuration | AnimNotify CDO | 2帧 | 0-6 | 受击停顿感更明显 | 更流畅但力道感减弱 |

## 8. 验收标准 (Acceptance Criteria)

### 8.1 构建管线验收

- [ ] `SekiroImport.Run` 能完整导入 Skeleton + SkeletalMesh + 1434 AnimSequence + 46 Material
- [ ] `SekiroImport.BuildAnimBlueprint` 能生成 ABP_Sekiro + SK_AnimLogicData
- [ ] ABP_Sekiro 编译零错误（通过 FKismetEditorUtilities::CompileBlueprint）
- [ ] SK_AnimLogicData 包含完整的 CancelRules / AttackHitboxConfigs / SpEffectConfigs / CategoryAnimMap
- [ ] 所有 AnimNotify 正确注入到 AnimSequence 的 NotifyTrack 中

### 8.2 运行时验收

- [ ] **BlendSpace 移动**：Speed 0→600 变化时角色在 Idle/Walk/Jog/Run/Sprint 之间无跳变过渡
- [ ] **CanCancelTo 准确**：在 CancelWindow 内按下对应按键 → CanCancelTo 返回 true（通过单元测试验证）
- [ ] **CancelWindow 外拒绝**：在 CancelWindow 外按下按键 → CanCancelTo 返回 false，不触发状态切换
- [ ] **优先级打断**：高优先级动作（受击）能打断低优先级动作（攻击），反之不能
- [ ] **AnimNotify 触发**：攻击动画播放到命中帧 → Notify 触发攻击盒生成（PIE 中通过 Debug Draw 验证）
- [ ] **Notify 不残留**：Montage BlendOut 期间 Notify 不触发，攻击盒不残影
- [ ] **弹刀即时响应**：弹刀判定成功后 1 帧内切换到反击动画（视觉上即时）
- [ ] **低帧率补偿**：30fps 和 60fps 下 CanCancelTo 判定结果一致（使用浮点时间）
- [ ] **异常安全**：AnimLogicData 为 null 时不崩溃，CanCancelTo 返回 false

### 8.3 性能验收

- [ ] AnimInstance::NativeUpdateAnimation(DeltaTime) 耗时 < 0.1ms（不含 Montage 加载）
- [ ] CanCancelTo 查询 < 0.01ms（TMap Find + 整数比较）
- [ ] ABP 状态机节点数 ≤ 50（状态 + 过渡 + 规则节点）
