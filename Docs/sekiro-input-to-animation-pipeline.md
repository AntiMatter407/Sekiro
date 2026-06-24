# 只狼 输入→动画 完整 ID 链路

| 创建 | 更新 |
|------|------|
| 2026-06-24 | 2026-06-24 |

## 概述

本文档梳理只狼原版从玩家按下按键到骨骼动画播放的完整链路中涉及的所有 ID、数据结构及其来源，为 AnimID 分类方案提供数据基础。

## 数据源总览

| 数据层 | 原始格式 | 位置 | 提取工具 | 当前状态 |
|--------|---------|------|---------|---------|
| TAE 事件 | `.tae` 二进制 | `Extracted/.../tae/a00.tae` 等 65 个文件 | `SekiroTAEExtractor` (C#) | ✅ 已导出为 `Extracted/Sekiro_TAE_Logic.json` (28MB) |
| JumpTable 参数 | TAE Type=0 事件内 | 同上 | 同上 | ✅ 已提取 (JumpTableID, ArgA-D, StateInfo) |
| InvokeBehavior 事件 | TAE Type=1/2/5 事件内 | 同上 | 同上 | ✅ 已提取 (BehaviorJudgeID, AttackType 等) |
| BehaviorParam_PC | `.param` 二进制 | 游戏 `GameParam.parambnd.dcx` 内 | Yabber → XML | ❌ 未提取 |
| AtkParam_PC | `.param` 二进制 | 同上 | Yabber → XML | ❌ 未提取 |
| 状态机 (ezState/ChrAsm) | `.behbnd` | 游戏动画容器内 | 无现成工具 | ❌ 未提取 |
| HKX 动画 | `.hkx` 二进制 | `Extracted/.../chr/c0000/` | Havok SDK / DSAnimStudio | ⚠️ 部分提取 |

---

## 链路全景图

```
                     +--------------------------------------------------------------+
                     |           Sekiro 动画链路 (四阶段)            |
                     +--------------------------------------------------------------+

[阶段1: 输入决策]          [阶段2: 行为调度]         [阶段3: 事件触发]           [阶段4: 动画播放]
 
 按键 -> ezState        ->   BehaviorParam_PC    ->    TAE 逐帧事件       ->    HKX 骨骼动画
                             |                       |
                         behaviorVariationID     BehaviorJudgeID
                         + BehaviorJudgeID       (在 Type=1/2/5 中)
                             |                       |
                             v                       v
                         RefType (权威分类)      BehaviorParam 查表
                         RefID  -> AtkParam       RefType -> 分类

关键认知:
- 84% 的动画(1348/1610)没有 Type=1/2/5 事件 -> 纯 Locomotion/Idle/Transition
- 16% 的动画(262/1610)有行为触发事件 -> 可通过 BehaviorParam 精确分类
```

---

## 数据层详解

### 1. TAE 文件 (.tae -> JSON)

**来源**: FromSoftware 专有二进制格式，位于 `Extracted/c0000-anibnd-dcx/chr/c0000/tae/`，
共 65 个文件 (`a00.tae` ~ `a99.tae`)。

**提取**: `Script/sekiro_asset_manager/ext_tools/SekiroTAEExtractor` (C#)，使用 SoulsFormats 库 +
SDT 模板 (`TAE.Template.SDT.xml`)。

**TAE 二进制内部结构**:
```
TAE Header:
    ID: int            <- TAE 文件 ID (等于文件名数字部分, a00.tae -> ID=0)
    Format: enum        <- SDT / ACE 等
    EventBank: int      <- 事件银行编号 (Sekiro 角色动画 = 14)
    AnimationCount: int

Animation:
    AnimID: int         <- 全局唯一动画ID (如 201010)
    EventCount: int
    Events[]:
        Type: ushort    <- 事件类型 (0=JumpTable, 1=InvokeAttack, 2=InvokeBullet, 5=InvokeCommon, ...)
        StartTime: float <- 开始时间 (秒)
        EndTime: float   <- 结束时间 (秒, float.MaxValue=动画结束)
        Parameters: byte[] <- 事件参数 (12~N 字节, 由 SDT 模板定义结构)
```

**JSON 输出结构** (`Sekiro_TAE_Logic.json`):
```json
{
    "SourceDirectory": "...",
    "TemplateFile": "TAE.Template.SDT.xml",
    "TotalTaeFiles": 65,
    "TAE_Files": [
        {
            "FileName": "a00.tae",
            "TAE_ID": 0,
            "AnimationCount": 939,
            "Animations": [
                {
                    "AnimID": 201010,
                    "Events": [
                        {
                            "Type": 0,
                            "TypeName": "JumpTable",
                            "StartFrame": 3,
                            "EndFrame": 38,
                            "Parameters": {
                                "JumpTableID": 115,
                                "ArgA": 0,
                                "ArgB": -1,
                                "ArgC": 6,
                                "ArgD": 0,
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

**事件类型统计** (84 种):

| Type | TypeName | 语义 | 数量 |
|------|----------|------|------|
| 0 | JumpTable | 帧级行为标志/取消窗口 | 21,148 |
| 1 | InvokeAttackBehavior | 触发攻击行为 (含 BehaviorJudgeID) | 1,514 |
| 2 | InvokeBulletBehavior | 触发子弹行为 (含 BehaviorJudgeID) | 607 |
| 5 | InvokeCommonBehavior | 触发通用行为 (含 BehaviorJudgeID) | 423 |
| 16 | Blend | 动画混合过渡 | — |
| 67 | AddSpEffect | 附加特殊效果 | — |
| 96-120 | SpawnFFX_* | 粒子特效系列 | — |
| 128-132 | PlaySound_* | 音效系列 | — |
| 224 | SetTurnSpeed | 设置转向速度 | — |
| 607 | Facial Expression | 面部表情 | — |
---

### 2. JumpTable 事件 (Type=0)

**结构**: 每个 Type=0 事件 12 字节参数
```
Offset  Size  Type    Field
0x00    4     int32   JumpTableID   <- 标志/行为的 ID
0x04    4     float   ArgA          <- (当前解析可能不准确)
0x08    4     float   ArgB
0x0C    4     float   ArgC
0x10    4     float   ArgD
0x14    4     int32   StateInfo     <- 状态信息
```

**当前已知 JT ID 映射** (来自 `write_tae_curves.py` 和 `SATAEImporter.cpp`):

| JT ID | 名称 | 含义 | 分类价值 |
|-------|------|------|---------|
| 5 | — | 未知 | — |
| 7 | DisableTurning | 禁止转向 | 攻击/受击特征 |
| 8 | FlagAsDodging | 标记为闪避中 | **Dodge 核心特征** |
| 11 | — | 未知 | — |
| 12 | InvokeDeath | 触发死亡 | Death 特征 |
| 19 | DisableMapHit | 禁止地图碰撞 | — |
| 25 | DodgeCancelStart | 垫步取消窗口开始 | **Dodge 核心特征** |
| 26 | GenericCancelStart | 通用取消窗口开始 | Attack 辅助特征 |
| 27 | SetNoGravity | 设置无重力 | Jump 特征 |
| 28 | — | 未知 (跨动画长标志) | — |
| 31 | ExitMovement | 退出移动态 | Locomotion 过渡特征 |
| 32 | EnterMovement | 进入移动态 | Locomotion 过渡特征 |
| 51 | Invincible | 无敌 | Dodge/特殊 特征 |
| 55 | Staggered | 硬直 | Hit 核心特征 |
| 63 | SpecialAction | 特殊行为 | Deathblow/事件动画 特征 |
| 72 | — | 未知 | — |
| 87 | — | 未知 (高频出现) | — |
| 89 | DisableMovement | 禁止移动 | 攻击/受击特征 |
| 90 | LimitMoveSpeedWalk | 限制移速->步行 | Locomotion 影响 |
| 91 | LimitMoveSpeedDash | 限制移速->冲刺 | Locomotion 影响 |
| 111 | EmergencyCancelStart | 紧急取消窗口 | — |
| 115 | AttackCancelEnd (R1) | 攻击取消窗口 | **Attack 核心特征** |
| 117 | GuardCancelEnd (L1) | 防御取消窗口 | **Guard 核心特征** |
| 118 | ProstheticCancelEnd (L2) | 义手取消窗口 | **Prosthetic 核心特征** |
| 119 | EnableParry | 允许弹刀 | Guard/Attack 辅助特征 |
| 133 | DisableSpecial | 禁用特殊 | — |
| 134 | DisableItem | 禁用道具 | — |
| 137 | DisableParry | 禁用弹刀 | — |
| 150 | — | 未知 (仅在 300000 出现) | — |
| 151 | — | 未知 (仅在 300000 出现) | — |
| 154 | ItemUseWindow | 道具使用窗口 | **Item 核心特征** |

---

### 3. InvokeBehavior 事件 (Type=1/2/5)

**Type=1 (InvokeAttackBehavior)**:
```
Parameters:
    AttackType: int          <- 0=轻击, 2=前刺, 62=落下攻击
    Unk04: int
    BehaviorJudgeID: int     <- 行为判定ID, 关联 BehaviorParam
    DirectionType: int
    Source: int
    StateInfo: int
```

**Type=2 (InvokeBulletBehavior)**:
```
Parameters:
    DummyPolyID: int         <- 子弹发射位置
    Unk04: int
    BehaviorJudgeID: int     <- 行为判定ID, 关联 BehaviorParam
    AttachmentType: int
    Enable: bool
    StateInfo: int
    Offset: int
```

**Type=5 (InvokeCommonBehavior)**:
```
Parameters:
    AttackIndex (0-8): int
    BehaviorJudgeID: int     <- 行为判定ID, 关联 BehaviorParam
```

**BehaviorJudgeID 分布** (按 100 分桶):

| 来源 | 范围 | 数量 | 关联行为 |
|------|------|------|---------|
| Type=1 (Attack) | 0-99 | 61 | 基本攻击判定 |
| Type=1 (Attack) | 100-199 | 844 | 普通攻击变体 |
| Type=1 (Attack) | 200-299 | 166 | 特殊攻击 |
| Type=1 (Attack) | 900-999 | 443 | 战技/特殊攻击 |
| Type=2 (Bullet) | 100-199 | 220 | 子弹/投射物 |
| Type=2 (Bullet) | 200-299 | 350 | 特殊子弹 |
| Type=2 (Bullet) | 900-999 | 27 | 特殊投射物 |
| Type=5 (Common) | 600-799 | 416 | 通用特殊行为 |

---

### 4. BehaviorParam_PC (游戏行为参数数据库)

**来源**: 游戏 `GameParam.parambnd.dcx` 内的 `BehaviorParam_PC.param`。
使用 Yabber 可导出为 XML。

**内部结构** (来自 DSAnimStudio 逆向):
```
BehaviorParam entry (每行 0x20 字节):
    Offset  Size  Type    Field
    0x00    4     int32   VariationID         <- 行为大类ID (与状态机的behaviorVariationID对应)
    0x04    4     int32   BehaviorJudgeID     <- 行为判定ID (与TAE事件中的BehaviorJudgeID对应)
    0x08    1     byte    EzStateBehaviorType_Old
    0x09    1     enum    RefType             <- 行为类型权威定义!
        0 = Attack (攻击)
        1 = Bullet (子弹/投射物)
        2 = SpEffect (特殊效果)
        3 = UnkAC6_Type3
    0x0A    2     byte[]  pad0[2]
    0x0C    4     int32   RefID               <- 引用 ID
        RefType=0 -> AtkParam_PC[RefID]
        RefType=1 -> BulletParam[RefID]
        RefType=2 -> SpEffectParam[RefID]
    0x10    4     int32   SFXVariationID      <- 音效变体ID
    0x14    4     int32   Stamina             <- 消耗精力
    0x18    4     int32   MP                  <- 消耗纸人/魔力
    0x1C    1     byte    Category            <- 行为子类别
    0x1D    1     byte    HeroPoint           <- 英雄点数
```

**查表 Key 构造** (来自 DSAnimStudio):
- NPC: `behaviorParamID = 2_00000_000 + (behaviorVariationID * 1_000) + behaviorJudgeID`
- PC: `behaviorParamID = behaviorVariationID * 1_000 + behaviorJudgeID` (推测)

**BehaviorVariationID 范围编码** (FromSoftware 约定):

| VariationID 范围 | 行为大类 |
|-----------------|---------|
| 0-99 | Idle/Locomotion |
| 200-299 | Attack |
| 300-399 | Guard/Deflect/Dodge |
| 500-599 | Hit/HitReaction |
| 600-699 | Death/Knockback |
| 700-799 | Deathblow |
| 800-899 | Item/Goods |
| 900-999 | CombatArt/Special |
| 1000+ | Prosthetic |
---

### 5. AtkParam_PC (攻击参数数据库)

**来源**: 同上 `GameParam.parambnd.dcx` 内的 `AtkParam_PC.param`。

**内部结构** (来自 DSAnimStudio 逆向):
```
AtkParam entry (部分字段):
    AttackType: int         <- 0=轻击, 2=前刺, 62=落下攻击
    HitType: enum           <- 0=尖端, 1=中部, 2=根部
    HitSourceType: enum     <- 0=武器, 1=身体, 2+=部件
    Damage: int
    StaminaDamage: int
    GuardLevel: int         <- 格挡等级
    HitboxRadius: float
    DummyPoly: int          <- 判定框关联的 DummyPoly ID
    SpEffectOnHit: int      <- 命中附加特效ID
    KnockbackDistance: float
```

---

### 6. 状态机 (ezState / ChrAsm)

**来源**: 游戏的 `.behbnd` 文件。定义了状态之间的转移规则。

**概念结构** (来自 DSAnimStudio 逆向):
```
ezState Transition:
    SourceState: uint16     <- 来源状态
    TargetState: uint16     <- 目标状态 (或 behaviorVariationID)
    ConditionFlags: uint32  <- 触发条件 (按键/落地/受击等)
    TargetAnimID: int32     <- 目标动画ID
    BlendDuration: float    <- 混合时间
```

**状态机 -> BehaviorVariationID 映射**: 状态机的转移目标 `TargetState` 就是 `behaviorVariationID`，
由它去 BehaviorParam_PC 查表获取行为定义。

---

## 完整案例追踪

### 案例 A: 201010 — 标准 R1 第一刀

```
+-- 阶段1: 状态机决策 ---------------------------------------------------

   玩家按 R1 (Idle 状态)
       |
   ezState: Idle_Standing + Input_R1 + OnGround
       -> behaviorVariationID = 200
       -> Play AnimID = 201010

+-- 阶段2: BehaviorParam 查表 ------------------------------------------

   BehaviorParam_PC[200000]:
       VariationID     = 200
       BehaviorJudgeID = 0
       RefType         = 0  <- Attack!
       RefID           = 3000
       Stamina         = 30
       Category        = ?
       |
   AtkParam_PC[3000]:
       AttackType      = 0 (Standard)
       Damage          = 120
       GuardLevel      = 1
       HitboxRadius    = ?
       SpEffectOnHit   = 5000

+-- 阶段3: TAE 逐帧事件 ------------------------------------------------

   AnimID=201010 (38帧, ~1.27秒, 来自 a00.tae)

   帧 0:  JT=87  (unknown, 持续到帧38)
   帧 0:  JT=133 (DisableSpecial, 帧0-38)
   帧 0:  JT=134 (DisableItem, 帧0-38)
   帧 0:  Blend (混合过渡, 帧0-5)
   帧 0:  SpEffect 100330
   帧 0:  CameraModule4(11) 晃动
   帧 0:  JT=28  (跨动画长标志, 帧0-100)
   帧 1:  Sound 6502, 2001
   帧 3:  JT=115 (AttackCancelEnd <- R1取消窗口开启!)
   帧 3:  JT=26  (GenericCancelStart)
   帧 3:  JT=117 (GuardCancelEnd <- L1取消窗口开启!)
   帧 3:  JT=118 (ProstheticCancelEnd <- L2取消窗口开启!)
   帧 3:  JT=119 (EnableParry <- 允许弹刀!)
   帧 3:  JT=154 (ItemUseWindow)
   帧 3:  JT=137 (DisableParry <- 禁止弹刀! 注意与JT=119矛盾)
   帧 3:  SpEffect 100367, 100368, 100387
   帧 3:  SpEffect 100332, 100328
   帧 6:  JT=31  (ExitMovement)
   帧 6:  JT=32  (EnterMovement)
   帧 17: Sound 1100

   注意: 没有任何 Type=1/2/5 事件!
   攻击判定由状态机一次性派发, 不由TAE逐帧触发。

+-- 阶段4: 动画播放 ----------------------------------------------------

   AnimID=201010 -> HKX 动画文件 -> 骨骼变换 + RootMotion
   BehaviorParam 的攻击判定在第N帧生效 (由游戏引擎驱动, 不在TAE中)

+-- 分类结论 -----------------------------------------------------------

   范围规则: 201010 in [200000, 299999] -> "Attack" 正确
   BehaviorParam: behaviorVariationID=200 -> RefType=0 -> "Attack" 正确
   一致!
```

---

### 案例 B: 300000 — 垫步攻击 (典型的争议动画)

```
+-- 阶段1: 状态机决策 ---------------------------------------------------

   玩家按 B(Dodge) 后按 R1
       |
   ezState: Dodge -> DodgeAttack transition
       -> behaviorVariationID = 310
       -> Play AnimID = 300000

+-- 阶段2: TAE 逐帧事件 (有 Type=1/2!) ---------------------------------

   AnimID=300000 (68帧, ~2.27秒, 来自 a50.tae)

   帧 0:  JT=133 (DisableSpecial, 帧0-68)
   帧 0:  JT=134 (DisableItem, 帧0-68)
   帧 0:  Blend (帧0-6)
   帧 0:  JT=51  (Invincible <- 帧0-15 无敌!)               <- Dodge特征!
   帧 0:  JT=7   (DisableTurning, 帧0-6)
   帧 3:  JT=25  (DodgeCancelStart <- 帧3-15)               <- Dodge核心!
   帧 3:  JT=26  (GenericCancelStart)
   帧 3:  JT=117 (GuardCancelEnd)
   帧 3:  JT=119 (EnableParry)
   帧 3:  JT=150, 151 (unknown)
   帧 3:  SpEffect 100338

   // --- 帧18: 行为触发开始 ---
   帧18: JT=63  (SpecialAction <- 帧18-19)                  <- 特殊行为标记!
   帧18: > Type=2 InvokeBulletBehavior (帧18-19)
             BehaviorJudgeID = 290
             DummyPolyID = 603
   帧18: > Type=2 InvokeBulletBehavior (帧18-19)
             BehaviorJudgeID = 295
             DummyPolyID = 603

   // --- 帧23: 攻击判定三连发 ---
   帧21: JT=7   (DisableTurning <- 帧21-68)
   帧21: JT=87  (unknown)
   帧23: > Type=1 InvokeAttackBehavior (帧23-25)
             AttackType = 0
             BehaviorJudgeID = 0      <- 攻击判定1
             StateInfo = 0
   帧23: > Type=1 InvokeAttackBehavior (帧23-25)
             AttackType = 0
             BehaviorJudgeID = 185    <- 攻击判定2
             StateInfo = 358
   帧23: > Type=1 InvokeAttackBehavior (帧23-25)
             AttackType = 0
             BehaviorJudgeID = 186    <- 攻击判定3
             StateInfo = 940
   帧23: > Type=2 InvokeBulletBehavior (帧23-24)
             BehaviorJudgeID = 190    <- 子弹判定1
   帧23: > Type=2 InvokeBulletBehavior (帧23-24)
             BehaviorJudgeID = 191    <- 子弹判定2
   帧23: > Type=2 InvokeBulletBehavior (帧23-24)
             BehaviorJudgeID = 192    <- 子弹判定3
   帧23: JT=56, 72, 5 (unknown)

   // --- 帧33: 第二段取消窗口 ---
   帧33: JT=115 (AttackCancelEnd <- 帧33-68)
   帧33: JT=26  (GenericCancelStart)
   帧33: JT=117 (GuardCancelEnd)
   帧33: JT=154 (ItemUseWindow)
   帧33: JT=119 (EnableParry)

   // --- 帧39: 恢复移动 ---
   帧39: JT=11  (unknown)
   帧39: JT=31  (ExitMovement)
   帧39: JT=32  (EnterMovement)
   帧39: SpEffect 100367, 100368

   // --- 其他 ---
   帧18: Type=700 (Event700) 行为移动参数
   帧23: SpEffect 106085, 106086
   帧23: SetKnockbackPercent(0)
   帧18: SetMovementMultiplier (1.58/0.8/2.4 倍速)

+-- 阶段3: BehaviorParam 查表链 -----------------------------------------

   帧23 触发 Type=1, BehaviorJudgeID=0:
       查 BehaviorParam_PC[310000]:
           VariationID     = 310
           BehaviorJudgeID = 0
           RefType         = 0  <- Attack!
           RefID           = ?  -> AtkParam_PC[?]

   帧23 触发 Type=1, BehaviorJudgeID=185:
       查 BehaviorParam_PC[310185]:
           RefType = 0  <- Attack!

   帧23 触发 Type=1, BehaviorJudgeID=186:
       查 BehaviorParam_PC[310186]:
           RefType = 0  <- Attack!

   帧18 触发 Type=2, BehaviorJudgeID=290:
       查 BehaviorParam_PC[310290]:
           RefType = 1  <- Bullet!
           RefID   = ?  -> BulletParam[?]

   帧18 触发 Type=2, BehaviorJudgeID=295:
       查 BehaviorParam_PC[310295]:
           RefType = 1  <- Bullet!

+-- 分类结论 -----------------------------------------------------------

   范围规则: 300000 in [300000, 300999] -> MidPart=00<10 -> "Guard"  错误!
   实际行为: 垫步 (帧0-15无敌+取消) + 攻击 (帧23三次攻击判定)
   BehaviorParam: RefType=0 (Attack) + RefType=1 (Bullet)
   -> 正确的分类: 这是一个带攻击判定的垫步动画, 不是 Guard
   -> 建议分类: "Dodge" (主行为) 或 "Dodge_Attack" (精确)
```
---

## 两种分类路径对比

```
                     +-- 动画有 Type=1/2/5 事件? --+
                     |                            |
                    是 (262个, 16%)              否 (1348个, 84%)
                     |                            |
                     v                            v
            从 TAE 事件提取                纯 Locomotion/Idle/Transition
            BehaviorJudgeID                无行为触发, 只有帧级标志
                     |                            |
                     v                            v
            查 BehaviorParam_PC            使用 AnimID 数字范围大类:
            RefType:                       0-699  -> Locomotion (Walk/Jog/Run/Sprint/Jump)
               0 -> Attack                 20xxxx -> Attack
               1 -> Bullet/Prosthetic      30xxxx -> Guard/Deflect/Dodge/...
               2 -> SpEffect/Special       40xxxx -> Hit/Knockback/Death
                                           50xxxx -> Deathblow
         结合 VariationID 范围:            60xxxx -> Resurrection
             0-99   -> Idle/Locomotion     70xxxx -> Prosthetic
             200-299 -> Attack             80xxxx -> Grapple
             300-399 -> Guard/Dodge         90xxxx -> Item
             500-599 -> Hit                1xxxxx -> CombatArt
             600-699 -> Death
             700-799 -> Deathblow
             800-899 -> Item
             900-999 -> CombatArt
             1000+  -> Prosthetic

关键区别:
   路径A (有Type=1/2/5): BehaviorParam 的 RefType 是游戏自己的权威定义
   路径B (无Type=1/2/5): 数字范围是约定俗成, 大部分对, 但有边缘错误
```

---

## BehaviorParam 驱动分类方案

### 方案概述

1. **提取 BehaviorParam_PC** — Yabber 导出 `.param` -> XML
2. **解析 XML** -> 构建 `{VariationID*1000 + BehaviorJudgeID -> {RefType, Category, RefID}}` 查表
3. **对 262 个有 Type=1/2/5 事件的动画**:
   - 从 TAE JSON 提取 BehaviorJudgeID
   - 从 AnimID 范围推断 VariationID (BehaviorJudgeID 的高位 + AnimID 大类)
   - 查 BehaviorParam 表 -> 拿到 RefType
4. **对 1348 个无行为事件的动画**: 保持数字范围分类
5. **合并** -> 生成最终的 CategoryAnimMap

### 预期修正

| 动画范围 | 当前分类 | BehaviorParam 分类 | 修正原因 |
|---------|---------|-------------------|---------|
| 300000-300040 | Guard | Dodge (RefType=0 + JT=25+51) | 含攻击判定的垫步, 不是Guard |
| 300100-300111 | Guard | Dodge (RefType=0) | 同上 |
| 301000, 301050 | Guard | Dodge (RefType=0) | 同上 |
| 311xxx (空事件) | Dodge | Locomotion_Idle | 无任何行为触发 |
| 313xxx (空事件) | Dodge | Locomotion_Idle | 同上 |

---

## 关联文档

- 进度文档: `Docs/plan/sekiro-anim-input-replica.md`
- 技术方案: `Docs/design/sekiro-anim-input-replica.md`
- TAE 分析脚本: `Script/temp/analyze_tae_categories.py`
- TAE 曲线写入: `Script/write_tae_curves.py`

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-24 | 创建文档 |