# 碰撞检测系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗、精确节奏

## 概述

碰撞检测系统负责战斗中的所有物理交互判定——攻击盒（Attack Hitbox）是否命中受击盒（Hurtbox）、弹刀窗口是否被触发。它不对伤害或反馈做出决策，只输出"谁在何时打中了什么部位"的原始数据。所有判定基于 UE 物理查询（Sweep/Overlap），每帧执行。

## 玩家幻想

碰撞检测是玩家看不到的底层——但当它出错时（"我明明弹到了！"），玩家立刻会愤怒。所以它的核心幻想是：**判定的结果必须与视觉匹配**。攻击盒的尺寸和位置应紧跟武器模型，弹刀窗口应与火花特效的时刻一致。

## 详细设计

### 核心规则

1. 每个角色有多个碰撞体：
   - **受击盒 (Hurtbox)**：胶囊体或盒体，贴在角色骨骼上，持续存在
   - **攻击盒 (Attack Hitbox)**：仅在攻击动作的特定帧生成，由动画通知（AnimNotify）控制生命周期
2. 攻击盒/受击盒的碰撞通道使用自定义 UE Collision Channel：`ECC_SKAttack` / `ECC_SKHurtbox`
3. 每帧通过 `Sweep` 或 `Overlap` 检测攻击盒与受击盒的重叠
4. 检测结果输出为 `FSKHitResult` 结构体，包含命中点、命中部位、攻击方、被击方、命中帧
5. 同一攻击盒在单次攻击生命周期内**只对每个目标命中一次**（防重复判定）
6. 弹刀判定：在弹刀窗口帧内，检查角色前方的球形/扇形区域是否与攻击盒相交

### 碰撞通道配置

| 通道 | 响应 | 说明 |
|------|------|------|
| ECC_SKAttack | Overlap | 攻击盒 |
| ECC_SKHurtbox | Overlap | 受击盒 |
| ECC_SKDeflect | Overlap | 弹刀判定区域 |

碰撞响应矩阵：
- Attack ⇄ Hurtbox = Overlap（产生命中事件）
- Attack ⇄ Deflect = Overlap（产生弹刀事件）
- Attack ⇄ Attack = Ignore
- Hurtbox ⇄ Deflect = Ignore

### 攻击盒生命周期

```
AnimNotify_AttackStart → 生成攻击盒 → 每帧 Sweep/Overlap
    → 命中检测 → 记录已命中目标（去重）
    → AnimNotify_AttackEnd → 销毁攻击盒
```

### 数据结构

#### FSKHitResult

| 字段 | 类型 | 说明 |
|------|------|------|
| Attacker | AActor* | 攻击方 |
| Target | AActor* | 被击方 |
| HitLocation | FVector | 世界坐标命中点 |
| HitBone | FName | 命中的骨骼名 |
| AttackID | FName | 触发的攻击标识 |
| FrameNumber | int | 命中发生的帧号 |
| bIsDeflected | bool | 是否被弹刀 |
| HitDirection | FVector | 攻击方向（用于击退计算） |

#### FSKHitboxDef

| 字段 | 类型 | 说明 |
|------|------|------|
| Shape | EHitboxShape | Box / Sphere / Capsule |
| Extent | FVector | 尺寸 |
| Offset | FTransform | 相对骨骼的偏移 |
| AttachBone | FName | 附加到的骨骼 |
| ActiveStartFrame | int | 在攻击动画的第几帧激活 |
| ActiveEndFrame | int | 在攻击动画的第几帧停用 |

### 与动画系统的接口

攻击盒生成/销毁由 AnimNotify 驱动：
- `ANSK_AttackHitboxEnable`：在指定帧启用攻击盒
- `ANSK_AttackHitboxDisable`：在指定帧禁用攻击盒
- `ANSK_DeflectWindow`：标记弹刀窗口的开始和结束帧

这种方式让动画师可以直接在动画编辑器中调整判定窗口，无需修改代码。

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 动画系统 | 输入 AnimNotify | 动画驱动攻击盒的生成/销毁 |
| 伤害计算 | 输出 FSKHitResult | 命中数据作为伤害计算的输入 |
| 弹刀系统 | 输出命中+弹刀判定 | 弹刀系统消费弹刀碰撞结果 |
| 普攻连段 | 输出命中结果 | 命中后可能触发连段窗口 |
| 闪避/识破 | 输出（回避判定） | 闪避帧内 Hurtbox 通道禁用 |

## 公式

### 攻击盒放置（World Space）

```
WorldTransform = BoneWorldTransform * HitboxOffset * HitboxRotation
```

由动画骨骼的运行时 Transform 驱动，攻击盒跟随武器/肢体运动。

### 攻击盒 Sweep 查询

```
OverlapMultiByChannel(AttackChannel, WorldTransform, HitboxExtent)
→ TArray<FOverlapResult>
→ 过滤排除 Attacker 自身和已命中目标
→ 构造 FSKHitResult 数组
```

### 弹刀检测

```
// 在弹刀窗口帧内，在角色前方做扇形检测
SphereOverlapByChannel(DefenderLocation + ForwardVector * CheckOffset, CheckRadius, ECC_SKAttack)
→ 检测到攻击盒 → 弹刀成功
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| 同一帧命中多个目标 | 全部记录——单次攻击可以命中多个敌人 | 横扫攻击应命中范围内的所有人 |
| 同一攻击对同一目标多次命中 | 仅第一次命中有效（去重表记录 Target） | 防止一刀触发多次伤害 |
| 两个攻击盒同时命中一个 Hurtbox | 两个攻击分别记录，伤害累加 | 同时发生即为同时命中 |
| 攻击盒在生成时已与 Hurtbox 重叠 | 首帧不判定——需要 Sweep（上一帧位置→当前帧位置） | 防止穿透高速运动的角色 |
| 角色死亡后的碰撞 | Hurtbox 立即禁用（SetCollisionEnabled(NoCollision)） | 尸体不应被继续攻击判定 |
| 弹刀帧内同时按攻击和防御 | 防御优先——先检测弹刀判定，再检测攻击命中 | 只狼战斗哲学 |
| 斜面和高度差 | 攻击盒 Z 轴范围适当放大（1.2× 武器长度），Sweep 而非 Overlap | 防止视觉上打中了但检测不到 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 动画系统 | 碰撞依赖动画 | AnimNotify 驱动攻击盒生命周期 |
| 数据配置 | 碰撞依赖数据配置 | 攻击盒的形状/帧数据来自 FSKAttackData |
| 伤害计算 | 依赖碰撞 | 需要 FSKHitResult 作为输入 |
| 弹刀系统 | 依赖碰撞 | 需要弹刀检测结果 |
| 闪避/识破 | 依赖碰撞 | 闪避帧内需要禁用 Hurtbox 响应 |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| AttackHitboxScale | 1.0× | 0.8-1.5× | 攻击范围更大 → 更容易命中 | 范围更小 → 更精确 |
| HurtboxScale | 1.0× | 0.9-1.2× | 受击范围更大 → 更难闪避 | 受击范围更小 → 闪避更宽容 |
| DeflectCheckRadius | 100 UE单位 | 80-150 | 弹刀更宽松 | 弹刀更严格 |
| SweepSubSteps | 2 | 1-4 | 更精确的快速移动检测 | 性能更好 |

## 验收标准

- [ ] 攻击盒在 AnimNotify 后正确生成，在对应结束通知后正确销毁
- [ ] 同一攻击盒对同一目标只命中一次（去重验证）
- [ ] 弹刀窗口内防御输入被正确检测为弹刀（而非格挡）
- [ ] Sweep 查询防止高速穿透——60fps 下角色移动速度 1000 UE单位/s 时不漏检
- [ ] 两个攻击盒同时命中同一目标时，产生两条 FSKHitResult
- [ ] 角色死亡后 Hurtbox 在 1 帧内禁用
- [ ] 攻击盒在 AnimMontage 被中断时正确清理（不残留浮空攻击盒）
- [ ] 碰撞检测每帧开销 < 0.5ms（在 2 角色 + 攻击盒激活时）
