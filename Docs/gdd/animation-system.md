# 动画系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗、精确节奏、垂直机动

## 概述

动画系统负责狼和 Boss 的所有动作表现。基于 UE Animation Blueprint + AnimMontage，管理移动动画、战斗动作、受击反馈和忍杀演出。动画系统通过状态机和 Montage 控制动作切换、取消链（Cancel Chain）和根运动（Root Motion）。动画通知（AnimNotify）驱动攻击盒、音效、特效等外部事件。

## 玩家幻想

每一刀都应该有力反馈感——动画的加速度曲线、停顿帧（Freeze Frame）和火花特效同步，让玩家"感受到"刀刃碰撞。动作取消应该流畅而不跳帧，玩家永远不会看到角色"瞬移"到另一个动作。

## 详细设计

### 动画资产组织

```
Content/Animations/
├── Wolf/
│   ├── Locomotion/       # 移动动画（Idle/Walk/Run/Sprint/Jump/Crouch）
│   ├── Combat/
│   │   ├── Attack/       # 普攻连段1-4、蓄力突刺
│   │   ├── Deflect/      # 弹刀/格挡
│   │   ├── Dodge/        # 侧闪/后闪/识破/踩踏
│   │   └── Hit/          # 受击反馈（轻/重/击飞）
│   ├── Items/            # 喝药
│   ├── Deathblow/        # 忍杀处决
│   └── Misc/             # 回生、死亡
└── Boss/
    ├── Idle/
    ├── Attack/
    ├── Hit/
    ├── Deathblow/
    └── PhaseTransition/
```

### 动画状态机 (AnimBP State Machine)

```
Entry → Idle/Run (BlendSpace)
    │
    ├──→ Attack (Montage)       ← IA_Attack
    ├──→ Deflect (Montage)      ← IA_Deflect + 攻击命中判定
    ├──→ Block (State)          ← IA_Deflect Hold
    ├──→ Dodge (Montage)        ← IA_Dodge
    ├──→ HitStun (Montage)      ← 被击中
    ├──→ Knockback (Montage)    ← 被击退/击飞
    ├──→ ItemUse (Montage)      ← IA_UseItem
    ├──→ Death (Montage)        ← HP=0 (非忍杀)
    ├──→ Resurrection (Montage) ← 回生触发
    └──→ Deathblow (Montage)    ← 忍杀演出
```

### BlendSpace 定义

移动动画使用 2D BlendSpace：
- X 轴：方向（-180° 到 180°，以角色朝向为 0°）
- Y 轴：速度（0 到 RunSpeed）
- 采样点：Idle(0,0), Walk_Fwd(0,0.4), Run_Fwd(0,1.0), Walk_Left(-90,0.4), Run_Left(-90,1.0) ……

锁定状态下使用单独 BlendSpace（以目标方向为参考）。

### 动作取消规则 (Cancel Rules)

| 当前动作 | 可取消到 | 条件 |
|----------|---------|------|
| Attack_N（第N段普攻） | Attack_N+1 | 在攻击的 CancelWindow 帧范围内 |
| Attack_N | Deflect | 弹刀窗口内按下防御 |
| Attack_N | Dodge | 前段部分帧允许取消（原版设定） |
| Deflect | Attack_1 | 弹刀成功后立刻可反击 |
| Dodge | Attack_1 | 闪避后可追击 |
| ItemUse | Dodge | 原版喝药可被闪避取消 |
| HitStun | — | 不可取消（受击硬直必须播放完） |
| Deathblow | — | 不可取消（演出必须播放完） |

### AnimNotify 事件

| Notify | 触发内容 |
|--------|---------|
| ANSK_AttackHitboxEnable | 在攻击动画指定帧生成攻击盒 |
| ANSK_AttackHitboxDisable | 销毁攻击盒 |
| ANSK_DeflectWindowStart | 弹刀窗口开始 |
| ANSK_DeflectWindowEnd | 弹刀窗口结束 |
| ANSK_CancelWindowStart | 允许取消到下一动作 |
| ANSK_CancelWindowEnd | 关闭取消窗口 |
| ANSK_AttackSFX | 触发挥刀音效 |
| ANSK_Footstep | 触发行走脚步声 |
| ANSK_RootMotionLock | 在此帧锁定根运动位移 |
| ANSK_RootMotionUnlock | 释放根运动锁定 |

### 根运动策略

| 动作类型 | 根运动 | 说明 |
|----------|--------|------|
| 普攻连段 | 启用 | 攻击位移由动画控制 |
| 弹刀 | 禁用 | 弹刀不应移动角色 |
| 闪避 | 启用 | 闪避距离由动画控制 |
| 识破 | 启用 | 识破时踩住敌人武器→位移固定 |
| 受击 | 部分 | 击退位移由物理+动画混合 |
| 忍杀 | 锁定 | 双方移动到演出起始位置 |

### 动画性能

1. 所有移动动画使用 In-Place（原地）动画 + 代码驱动的位移
2. 攻击动画使用 Root Motion 以确保精确的位移和碰撞对齐
3. Montage 分段：将长 Montage 拆分为多段以避免一次加载整个序列
4. LOD：远距离时使用低精度骨骼（减少骨骼数）

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 输入系统 | 输入动作意图 | 驱动状态机切换 |
| 移动系统 | 输入移动状态（速度/方向/是否锁定） | BlendSpace 参数 |
| 碰撞检测 | 输出 AnimNotify → 攻击盒 | 动画帧驱动碰撞生命周期 |
| 弹刀系统 | 输出弹刀窗口/输入弹刀结果 | 弹刀结果影响后续动画 |
| 伤害计算 | 输出命中帧/输入受击类型 | 受击动画由伤害类型决定 |
| 忍杀 | 输入忍杀触发 | 播放忍杀演出 Montage |

## 公式

### BlendSpace 参数映射

```
Speed = VSize(MovementComponent.Velocity * Vector(1,1,0)) / RunSpeed  // 归一化 0-1
Direction = AngleBetween(CharacterForward, MovementDirection)          // -180 到 180
```

### 动作取消判定

```
CanCancel = CurrentMontageTime >= CancelWindowStart && CurrentMontageTime <= CancelWindowEnd
           && (TargetAction == CancelAllowedList[CurrentAction])
```

### 根运动提取

```
每帧位移 = 动画骨骼的 Root Bone Transform 差异
世界位移 = 每帧位移 × CharacterRotation
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| Montage 被打断 | 通过 `Montage_Stop(BlendOut)` 平滑淡出，淡出时间 ≤ 0.1s | 不应出现跳帧 |
| 两个 Montage 同时请求 | 优先级：受击 > 忍杀 > 弹刀 > 闪避 > 攻击——高优先级打断低优先级 | 受击反馈必须即时 |
| 攻击中切换到防御 | 仅当处于 CancelWindow 内且方向为防御时才允许 | 不允许任意帧取消 |
| AnimNotify 在淡出帧触发 | Notify 在 BlendOut 期间不触发 | 防止攻击盒在动作取消时残影 |
| 帧率低于 30fps | Root Motion 使用 DeltaTime 补偿，确保位移量不变 | 防止低帧率下动作距离变短 |
| 网络延迟（多人预留） | Montage 在 Server 上播放并通过 RPC 同步到 Client | — |
| 不同 LOD 下的动画切换 | LOD1(近距离) = 完整动画；LOD2(中距离) = 降低骨骼数；LOD3(远距离) = 禁用动画更新 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 输入系统 | 动画依赖输入 | 动作意图驱动状态机 |
| 移动系统 | 动画依赖移动 | BlendSpace 参数 |
| 数据配置 | 动画依赖数据配置 | 移动速度等参数 |
| 碰撞检测 | 依赖动画（AnimNotify） | 攻击盒生命周期 |
| 弹刀系统 | 依赖动画（弹刀窗口） | 弹刀判定帧窗口 |
| 普攻连段 | 依赖动画（取消窗口） | 连段取消规则 |
| 忍杀 | 依赖动画（演出） | 忍杀 Montage |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| BlendOutTime | 0.1s | 0.05-0.2 | 过渡更平滑但响应延迟 | 响应更快但可能跳帧 |
| CancelWindowSize | 4帧 | 2-8 | 取消更宽容 | 取消更严格 |
| RootMotionScale | 1.0 | 0.8-1.2 | 攻击位移更远 | 攻击位移更近 |
| HitFreezeFrames | 2帧 | 0-6 | 受击停顿更明显 | 更流畅但力道感减弱 |
| MontagePlayRate | 1.0 | 0.8-1.2 | 动作更快 | 动作更慢 |

## 验收标准

- [ ] 移动 BlendSpace 在全部方向（前/后/左/右/45°）平滑过渡无跳变
- [ ] 攻击连段 1→2→3→4 在 CancelWindow 内按下攻击键时连续播放
- [ ] 弹刀动画在弹刀判定成功后的 1 帧内触发（视觉上即时响应）
- [ ] 受击动画优先级高于攻击——任何时候被击中立即切受击
- [ ] Montage 被打断时 BlendOut 时间 ≤ 0.1s 且无不自然跳帧
- [ ] AnimNotify 在 BlendOut 期间不触发
- [ ] 根运动位移量与动画一致（PIE 中通过 Debug Draw 验证）
- [ ] 30fps 和 60fps 下根运动总位移量相同（DeltaTime 补偿）
- [ ] Montage 加载不阻塞主线程（异步加载）
