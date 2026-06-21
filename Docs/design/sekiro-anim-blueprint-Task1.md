# 只狼动画逻辑→UE动画蓝图 / Task 1 — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/sekiro-anim-blueprint.md) | 1. Locomotion+Input 集成 | 🔄 进行中 | 2026-06-14 |

## 架构设计

### 原版 FS → UE 映射

| 原版机制 | UE 等价 |
|---------|---------|
| HKS Speed→动画查表 | BlendSpace1D (Speed 轴 0→600) |
| 离散方向动画 + HKS 选择 | 后续 Phase 2 扩展 2D BlendSpace |
| Type 16 Blend 事件 | BlendSpace 内置混合 |
| BlendToIdleOrMovementAnim 事件 | ABP Transition Rule: `Speed < StopThreshold` |
| Combat Idle JumpTable 窗口 | Transition Rule + InputIntent + CanCancelTo() |

### 数据流

```
EnhancedInput(WASD) → ASKCharacter::Move() → AddMovementInput
  → USKMovementComponent::GetMaxSpeed() → 速度计算
    → USKAnimInstance::NativeUpdateAnimation()
      → Speed = Velocity.Size2D()           [UPROPERTY(BlueprintReadOnly)]
      → Angle = CalculateDirection()        [UPROPERTY(BlueprintReadOnly)]
      → MovementTier                        [UPROPERTY(BlueprintReadOnly)]
        → ABP AnimGraph: BlendSpacePlayer.X = Speed (反射写入)
          → BlendSpace1D: Idle(0)↔Walk(150)↔Jog(350)↔Run(500)↔Sprint(600)
```

### 三层状态机

```
Entry → [Locomotion] (BlendSpace1D, priority=0)
          ↑ Transition: Speed + MovementTier 驱动
          │
          ├── 始终运行，Speed=0 时输出 Idle
          │
          └── [Combat/Reaction] 层覆盖 (后续 Task)
```

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Plugins/SekiroImport/.../SekiroTAEImporter.cpp` | ✅ 已修改 | `InferCategoryFromAnimID`: 100-999 细分 Walk/Jog/Sprint/Run/Locomotion |
| `Plugins/SekiroImport/.../SekiroAnimBlueprintBuilder.cpp` | 检查 | `BuildLocomotionBlendSpace` 应能取到正确动画 |
| `Source/Sekiro/Animation/SKAnimInstance.h` | 不改 | 已有 Speed/Angle/MovementTier/bIsInAir/bIsCrouching |
| `Source/Sekiro/Animation/SKAnimInstance.cpp` | 检查 | 反射写 BlendSpace.X 逻辑 |
| `Source/Sekiro/Character/SKCharacter.h/cpp` | 不改 | Move() + Sprint/Crouch 已实现 |
| `Source/Sekiro/Movement/SKMovementComponent.h/cpp` | 不改 | GetMaxSpeed() 已按 Tier 返回速度 |

## 关键决策

1. **Phase 1: 1D BlendSpace** — X 轴仅 Speed (0→600)，先打通前向移动
2. **不把过渡动画放入 BlendSpace** — Walk_Stop/Jog_Stop/Sprint_To_Idle 等后续通过 Transition Rule BlendTime 处理
3. **BlendSpace 采样点**修复后应取到: Idle_Default(0), Walk_Fwd(150), Jog_Fwd(350), Run_Fast_Fwd(500), Sprint_Fwd(600)
4. **Crouch 独立处理** — Crouch 状态应使用独立的 BlendSpace 或 SequencePlayer，不在主 Locomotion BlendSpace 中

## 子任务→Agent 映射

| 子任务 | 描述 | Agent | 状态 |
|--------|------|-------|------|
| 1.1 | 确认 Locomotion 动画资产 | — | ✅ |
| 1.2 | 重新运行 BuildAnimBlueprint | — | ⬜ (需 UE Editor) |
| 1.3 | 验证 ABP 父类为 USKAnimInstance | — | ⬜ (需 UE Editor) |
| 1.4 | 验证 Speed→BlendSpace 数据链路 | gameplay-programmer | ✅ |
| 1.5 | 验证 MovementTier 切换逻辑 | gameplay-programmer | ✅ |
| 1.6 | PIE 测试按键移动动画 | — | ⬜ (需 UE Editor) |

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-14 | 创建方案，InferCategoryFromAnimID 修复完成 |
