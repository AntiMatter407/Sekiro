# 玩家输入 → TAE JT 事件映射解析

> 本文档解析原版《只狼》中玩家物理按键如何映射为 TAE（Time Action Event）的 JumpTable 事件，阐明这套以"帧区间 + 开关标签"为核心的动画驱动机制，作为 UE5 迁移项目的参考依据。

---

## 一、核心概念

### 1.1 JT ID 和 JT 事件

| 概念 | 定义 | 类比 |
|------|------|------|
| **JT 事件** | TAE JSON 中 `Type=0` 的一条原始记录 | 一句话 |
| **JT ID** | JT 事件参数中的 `JumpTableID` 字段值 | 这句话里提到的动作 |

一个 JT 事件包含三个核心信息：

```
[Type=0, TypeName=JumpTable]  ×  [StartFrame~EndFrame]  ×  [JumpTableID=N]
         ↑                                ↑                          ↑
   标记这是 JumpTable 事件          这个窗口多长（帧区间）       具体做什么
```

### 1.2 关键认识

> **JT ID 不是"按键→下一个动画"的映射。**
>
> JT ID 是**附着在一个帧区间上的开关标签**，它说的是：
> "在当前动画的第 N 到第 M 帧之间，某个操作是有效的"。

原版游戏的切换不是查找表式的 `按键→动画`，而是**反向查询**机制：

```python
# 伪代码：原版每 60 帧执行一次
current_anim = 201040  # 当前播放的攻击动画
current_frame = 5      # 当前在第 5 帧

# 当玩家按 R1:
if player.R1_pressed:
    # 查询 TAE: AnimID=201040 有没有 JT=115 覆盖第 5 帧？
    if has_jt(201040, 115, covers_frame=5):
        play_next_attack()    # 有窗口 → 播下一段
    else:
        discard_input()       # 无窗口 → 丢弃输入
```

---

## 二、物理输入 → JT ID 映射表

### 2.1 直接映射

| 玩家输入 | JT ID | 含义 | TAE 出现次数 |
|----------|-------|------|-------------|
| **R1 按下** | 115 | `AnimCancelEnd_R1` — R1取消窗口结束 | 1217 |
| **R1 按下** | 26 | `GenericCancelStart` — 通用取消窗口（进攻） | 1096 |
| **R1 按下** | 87 | `InvokeAttackAction` — 攻击行为激活 | 1333 |
| **L1 按下** | 117 | `AnimCancelEnd_L1` — L1取消窗口结束 | 1127 |
| **L1 按下（弹刀）** | 119 | `EnableParry` — 弹刀模式启用 | 1250 |
| **L2 按下** | 118 | `AnimCancelEnd_L2` — L2义手取消窗口结束 | 1129 |
| **◻ 按下** | 25 | `AnimCancelStart_Dodge` — 闪避取消窗口开始 | 64 |
| **◻ 按住+移动** | 111 | `AnimCancelStart_Emergency` — 紧急闪避 | 0（原版罕见，项目未用到）|
| **○ 按下** | 154 | `ItemUseWindow` — 道具使用窗口 | 1049 |
| **方向输入** | 32 | `EnterMovement` — 进入移动状态 | 1172 |
| **方向输入** | 31 | `ExitMovement` — 退出移动状态 | 1158 |
| **方向输入** | 28 | `SetMoveSpeedNormal` — 恢复移动速度 | 949 |
| **方向输入** | 3 | `SetTurnSpeed` — 设置转向速度 | 139 |

### 2.2 帧级行为标志（被动约束）

这些 JT ID 不直接对应玩家操作，而是约束当前帧上角色的行为状态：

| JT ID | 含义 | 出现次数 | 说明 |
|-------|------|---------|------|
| 7 | `DisableTurning` — 禁用转向 | 1378 | 攻击动画期间不可转向 |
| 133 | `DisableSpecial` — 禁用义手/战技 | 1420 | 攻击/受击动画期间禁用 |
| 134 | `DisableItem` — 禁用道具 | 1398 | 攻击/受击动画期间禁用 |
| 137 | `DisableParry` — 禁用弹刀 | 1091 | 与 119 互斥 |
| 51 | `InvincibilityFrame` — 无敌帧 | 641 | 闪避动画的无敌区间 |
| 27 | `SetNoGravity` — 无重力 | 113 | 跳跃/下落中 |
| 55 | `StaggerFlag` — 硬直标志 | 212 | 命中后的硬直区间 |
| 65 | `LookAtTarget` — 追踪目标 | 191 | 攻击动画中面向目标 |
| 90 | `LimitMoveSpeedWalk` — 限制移速步行 | 16 | 走刀时 |
| 50 | `ActionRestriction` — 动作限制 | 227 | 禁用移动+转向 |

### 2.3 当前 C++ 头文件未映射的 JT ID

TAE 中有 28 个 JT ID 在 `ESKJumpTableAction` 枚举中未映射。高频的如下：

| JT ID | 出现次数 | 推测含义 |
|-------|---------|---------|
| 16 | 82 | 特殊攻击变体标记 |
| 39 | 107 | 姿态相关 |
| 56 | 143 | 反击/格挡反击 |
| 72 | 133 | 特定武器动作 |
| 141 | 132 | 未知（与义手相关）|
| 145 | 119 | 镜头震动 |
| 148 | 121 | 慢动作/时间控制 |

---

## 三、完整映射链路实例

### 场景 1：R1 → R1 → R1 三连击

```
玩家连按 3 次 R1
        │
        ▼
  第一刀 (AnimID=201040, 24帧, 0.8s)
        │
  帧 0-3:  [JT=51] 无敌帧           ← 前摇阶段
            [JT=87] 攻击行为激活     ← 判定框开始
            [JT=133] 禁用义手
  帧 3-23: [JT=115] R1取消窗口      ← 允许 R1 打断 → 连段
            [JT=26]  通用取消窗口
            [JT=119] 启用弹刀模式    ← 敌人可以弹这刀
            [JT=137] 禁用弹刀        ← 但自己不能弹（攻击动画中）
            [JT=118] L2取消窗口
            [JT=7]   禁用转向
  帧 24:   动画结束
        │
  第 3 帧后 R1 按下? → CanCancelTo(201040, frame, "Attack")? → 命中窗口
        │  ✅ → 播放第二刀 AnimID=201045
        │  ❌ → 输入丢弃，等待动画播完
        ▼
  第二刀 (AnimID=201045, 24帧, 0.8s)
        │  帧结构同上
        ▼
  第三刀 (AnimID=201050, 34帧, 1.13s)
        │  帧 0-33: JT=117 L1取消窗口（L1可以打断第三刀）
        │           JT=115 R1取消窗口（R1不能再接连段，只有取消到别的动作）
        ▼
  动画播完 → 回到 Idle
```

### 场景 2：敌人攻击时按 L1 → Deflect

```
玩家按 L1 按住
        │
        ▼
  GuardActive = true
  播放 Guard_Idle 动画 (AnimID=301000)
        │
  敌人攻击判定帧触发
        │
  DeflectWindow = 当前帧在目标动画的 JT=119 (EnableParry) 区间内？
        │
  ┌─── 是（玩家 L1 在敌人攻击判定帧前 6 帧内按下）
  │      → 播放 Deflect 动画 (AnimID=310000)
  │      → 弹开敌人，开启反斩窗口
  │      → TAE 中 Deflect 动画的 JT=87 定义反斩攻击框
  │
  └─── 否（L1 按晚了或敌人无攻击判定）
         → 播放 GuardImpact 动画（普通格挡）
         → 架势条增加
```

### 场景 3：攻击中按 ◻ → Dodge 取消

```
玩家正在播放攻击动画 (AnimID=201040, 第 5 帧)
  帧 5 在 JT=25 (AnimCancelStart_Dodge) 的窗口内？
        │
  ┌─── 是（第 0-3 帧不能闪避，第 3 帧后可以）
  │      → CanCancelTo(201040, 5, "Dodge") = true
  │      → 播放 Dodge_Back/Fwd/L/R 动画
  │      → 闪避动画中 JT=51 定义无敌帧区间
  │
  └─── 否（前摇阶段，或者攻击的后半段窗口已关闭）
         → 输入被丢弃
```

---

## 四、原版 TAE 的运行机制

### 4.1 TAE 不是在定义"动画→下一个动画"

原版 TAE 不是在定义一张 "R1 → Attack_Combo01" 的死映射表。TAE 定义的是：

> **在当前动画 `AnimID` 的帧 `[N, M]` 范围内，输入 `X` 是有效的**

每个动画的 JT 事件集合定义了该动画的"可操作区间"，而游戏引擎的工作就是**每帧检查当前动画的 JT 事件，决定是否响应玩家输入**。

### 4.2 CancelWindow 的本质

原版 TAE 中的取消窗口（CancelWindow）由两部分组成：

| 部分 | JT ID | 含义 | 是否必须成对？|
|------|-------|------|-------------|
| CancelStart | 1(R1), 9(L1), 21(Guard), 25(Dodge), 26(Generic), 105(L2), 111(Emergency) | 允许取消到目标动作 | **非必须** — 原版实际只用到了 25/26 |
| CancelEnd | 115(R1), 117(L1), 118(L2), 34/121(General), 107(Item), 112(Emergency) | 禁止取消到目标动作 | **非必须** — 但自身定义了一个完整的窗口区间 |

**关键洞察**：在原版 TAE JSON 的实际数据中：

- JT=1 (R1 CancelStart) **出现 0 次**
- JT=115 (R1 CancelEnd) **出现 1217 次**
- 这 1217 个 CancelEnd 事件本身就是完整的窗口定义——每个事件自己的 `StartFrame~EndFrame` 就是有效的输入窗口

**不要被 "Start/End" 的名字迷惑。** 原版 FromSoftware 的设计是：在 JumpTable 树的这个节点上，`115` 号动作对应"在这个帧区间内 R1 取消有效"。这个区间由事件自身的帧范围决定。

### 4.3 输入缓冲

原版使用**6帧输入缓冲**（60fps 下约 100ms）：

```
玩家按 R1 → [缓冲队列: 最多6帧]
               ↓ 当前动画下一帧到达
            当前帧在 JT=115 窗口内？
              ├── 是 → 消费缓冲，播放下一段动画
              └── 否 → 继续缓存，最多等6帧
                       超时 → 丢弃
```

---

## 五、UE5 项目中的映射实现

### 5.1 数据管线

```
TAE JSON (28MB, 2209动画, 21148个JT事件)
        │
        ▼  FSATAEImporter::ImportFromFile()
        │
  FSAAnimLogicImportResult (IR 中间表示)
        │  - CancelWindows 从 JT=25/26/115/117/118 提取
        │  - AttackHitboxes 从 Type=1 提取
        │  - FrameFlags 从 JT=7/51/119/133/... 提取
        │
        ▼  FSATAELogicBuilder::BuildDataAsset()
        │
  USKAnimationLogicData (DataAsset, .uasset)
        │  - CancelRules: AnimID → [FSKCancelRule]
        │  - AttackHitboxConfigs
        │  - AnimFrameFlags
        │  - CategoryAnimMap
        │
        ▼  运行时
  USKAnimationController::TryPlayAction()
        → CanCancelTo(CurrentAnimID, CurrentTime, TargetAction)
        → 查 CancelRules 中该动画的窗口是否覆盖当前帧
```

### 5.2 运行时核心逻辑

```
每帧 Tick:
  1. UpdateFrameState()     — 更新当前动画帧/时间
  2. ApplyFrameFlags()      — 从 DataAsset 读取帧级标志（禁用转向/弹刀等）
  3. UpdateAttackHitbox()   — 从 DataAsset 读取攻击框帧数据，激活/关闭武器碰撞体
  4. ProcessIntents()       — 按 Priority 降序遍历，用 CanCancelTo 逐项判定
  5. ProcessLocomotion()    — 移动层处理

ProcessIntents() 核心:
  Priority降序 [Deathblow=10 > Hit=8 > Dodge=7 > Deflect=6 > Guard/Jump=5
                > Prosthetic=4 > Item=3 > Attack=2 > Quickstep=1]
  
  for each input in priority order:
    if TryPlayAction("Attack", Priority=2):
      → CanCancelTo(CurrentAnimID, CurrentFrame, "Attack")
      → 查 CancelRules[CurrentAnimID] 是否有 TargetAction="Attack" 且覆盖 CurrentFrame
      → 有 → PlayMontage(AnimID), 更新 CurrentAnimID/CurrentPriority
      → 无 → 跳过

ProcessLocomotion() 核心:
  if CurrentPriority > Locomotion(0): 跳过（战斗动作覆盖移动）
  读取 Speed/Angle → 计算目标 LocomotionState
  Tier升降 → 过渡动画 → 新循环
  方向变化 → Crossfade
  原地转身 → Turn 动画
```

### 5.3 USKInputHandler → USKAnimationController 的映射

| 玩家按键 | 消费型/持续型 | 调用的 TryPlayAction | 查的 JT ID |
|----------|-------------|---------------------|-----------|
| R1 按下 | `ConsumeAttackPressed()` | `TryPlayAction("Attack", 2)` | 115, 26 |
| R1 按住 (>0.3s) | `IsAttackHeld()` | `TryPlayAction("Attack_Charged", 2)` | — |
| L1 按住 | `IsGuardHeld()` | `TryPlayAction("Guard", 5)` | 117, 119 |
| L1 (弹刀帧) | — | `TryPlayAction("Deflect", 6)` | 119 |
| ◻ 按下 | `ConsumeDodgePressed()` | `TryPlayAction("Dodge", 7)` | 25 |
| ◻ 按住 | `IsDodgeHeld()` + `GetMoveIntent()` | 设置 Sprint | — |
| ✗ 按下 | `ConsumeJumpPressed()` | `TryPlayAction("Jump", 5)` | — |
| L2 按下 | `ConsumeProstheticPressed()` | `TryPlayAction("Prosthetic", 4)` | 118 |
| ○ 按下 | `ConsumeInteractPressed()` | 交互/忍杀 | 154 |
| ○ (架势满)→忍杀 | — | `TryPlayAction("Deathblow", 10)` | — |

---

## 六、当前管线的断裂点

| 断裂 | 位置 | 现象 | 影响 |
|------|------|------|------|
| **CancelEnd 丢失** | `SATAEImporter::ExtractCancelWindows` | JT=115/117/118 的 CancelEnd 被识别后完全丢弃 | R1/L1/L2 取消窗口失效 |
| **Generic 代替 R1** | 同上 | JT=26 (通用取消) 被映射到 "Attack"，替代了本该由 JT=115 提供的 R1 取消 | 窗口帧与原版不一致 |
| **JT 未映射** | `MapJumpTableToAction()` | 28 个 JT ID 未在枚举中定义 | 帧级行为丢失 |
| **Priority 顺序错误** | `SKAnimationController::ProcessIntents()` | 顺序 if-return 链代替 Priority 降序遍历 | 多输入同时按下时优先级错误 |
| **连段窗口硬编码** | `USKInputHandler::Tick()` | 硬编码 0.5s 超时，不查 DataAsset | 连段窗口与 TAE 数据不匹配 |

---

## 附录：TAE JSON 数据结构参考

```json
{
  "SourceDirectory": "D:/Extract/tae",
  "TotalTaeFiles": 65,
  "TAE_Files": [
    {
      "FileName": "a00.tae",
      "TAE_ID": 4000,
      "AnimationCount": 30,
      "Animations": [
        {
          "AnimID": 201040,
          "Events": [
            {
              "Type": 0,
              "TypeName": "JumpTable",
              "StartTime": 0.0,
              "EndTime": 0.1,
              "StartFrame": 0,
              "EndFrame": 3,
              "Parameters": {
                "JumpTableID": 51,
                "ArgA": 0,
                "ArgB": -1,
                "ArgC": 6,
                "ArgD": 0,
                "StateInfo": 0
              }
            },
            {
              "Type": 1,
              "TypeName": "InvokeAttackBehavior",
              "StartTime": 0.0,
              "EndTime": 0.76666665,
              "StartFrame": 0,
              "EndFrame": 23,
              "Parameters": {
                "AttackType": 0,
                "Unk04": 0,
                "BehaviorJudgeID": 994,
                "DirectionType": 0,
                "Source": 0,
                "StateInfo": 0
              }
            }
          ]
        }
      ]
    }
  ]
}
```

统计数据：
- 总动画数: 2209
- 总事件数: 70410
- 其中 JT 事件 (Type=0): 21148
- JT ID 种类: 56 种
- TAE 文件数: 65
