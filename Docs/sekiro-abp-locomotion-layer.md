# ABP Locomotion 层设计

> 父方案：[sekiro-asset-pipeline.md](sekiro-asset-pipeline.md) — Sekiro 资产管线总览
> 关联方案：[implementation-plan.md](implementation-plan.md) — 分阶段实施计划

## 概述

打通角色移动输入到 ABP 动画输出的 Locomotion 层。角色移动系统（`SekiroAnimInstance`）已每帧计算 Speed/Angle/MovementTier，但 ABP 状态机的 Locomotion 层（Idle/Walk/Jog/Run/Sprint）各自只播放单个静态动画。本方案用 **BlendSpace1D** 替换 5 个离散状态，实现 Speed 驱动的连续移动动画混合。

## 架构

### 状态机三层结构

```
┌─────────────────────────────────────────────────────────┐
│ [Locomotion] ← BlendSpace1D player (Speed 0→600)        │  priority=0
│     BlendSpace 自动在 Idle/Walk/Jog/Run/Sprint 间混合    │
├─────────────────────────────────────────────────────────┤
│ [Combat] Attack / Guard / Deflect / ...                  │  priority=2
├─────────────────────────────────────────────────────────┤
│ [Reaction] Hit / Death / Knockback / ...                 │  priority=8
└─────────────────────────────────────────────────────────┘
```

- **Locomotion (priority=0)**：始终运行的后台层，Speed 驱动 BlendSpace 混合
- **Combat (priority=2)**：覆盖全身的战斗动画（攻击/防御/ prosthetic等）
- **Reaction (priority=8)**：最高优先级的受击/死亡动画

### 数据流

```
SekiroCharacter::Move() → AddMovementInput
    ↓
SekiroMovementComponent::GetMaxSpeed() → MovementTier
    ↓
SekiroAnimInstance::NativeUpdateAnimation()
    Speed = Velocity.Size2D()
    ↓ (BlueprintReadOnly)
ABP AnimGraph: BlendSpacePlayer.X = Speed
    ↓
BlendSpace1D: Speed(0→600) → Idle/Walk/Jog/Run/Sprint 混合
```

### BlendSpace 参数

| 参数 | 值 |
|------|-----|
| 类型 | BlendSpace1D |
| 轴名 | Speed |
| X 范围 | 0 → 600 |
| 网格分段 | 4（5个采样点） |
| 动画速率缩放 | BSA_X（动画播放速率跟随 Speed 缩放，减少滑步） |

### 采样点

| Speed | 动画类别 | 说明 |
|-------|---------|------|
| 0 | Idle | 站立待机 |
| 150 | Walk | 步行 (Walk=150) |
| 350 | Jog | 慢跑 (Jog=350) |
| 500 | Run | 奔跑 (Run=500) |
| 600 | Sprint | 冲刺 (Sprint=600) |

每个采样点取该类别在 `AnimSequences` 映射中的第一个匹配动画（通常是正向移动变体）。

## 实现细节

### 修改文件

| 文件 | 改动 |
|------|------|
| `SekiroAnimBlueprintBuilder.h` | 新增 `BuildLocomotionBlendSpace` 方法声明，`UBlendSpace1D` 前向声明 |
| `SekiroAnimBlueprintBuilder.cpp` | 新增 `BuildLocomotionBlendSpace` 和 `CreateBlendSpaceStateNode`；修改 `BuildStateMachine` 合并 Locomotion 状态 |
| `SekiroAnimInstance.h` | 新增 `FAnimNode_BlendSpacePlayer* CachedBlendSpacePlayer` 成员 |
| `SekiroAnimInstance.cpp` | 新增 BlendSpacePlayer 节点缓存 + 每帧写入 X 参数 |

### Step 1: BuildLocomotionBlendSpace

- `NewObject<UBlendSpace1D>(AnimBP, NAME, RF_Public|RF_Standalone)` 创建资产
- `SetSkeleton(Skeleton)` 绑定骨架
- `BlendParameters[3]` 是 `UPROPERTY(EditAnywhere)` (BlendSpace.h:857)，通过 `FindPropertyByName` + `ContainerPtrToValuePtr` 反射设置：DisplayName="Speed", Min=0, Max=600, GridNum=4
- `AddSample(Sequence, FVector(Speed, 0, 0))` 添加 5 个采样点
- `ResampleData()` 构建运行时网格
- `AxisToScaleAnimation` 是 **protected** (BlendSpace.h:865)，通过 `FByteProperty::SetPropertyValue_InContainer` 反射设置为 `BSA_X`

### Step 2: Locomotion 状态改造

新增 `CreateBlendSpaceStateNode` 函数（镜像 `CreateStateNode`，但用 `UAnimGraphNode_BlendSpacePlayer` 替代 `UAnimGraphNode_SequencePlayer`）。

`BuildStateMachine` 中：
1. 调用 `BuildLocomotionBlendSpace` 创建资产
2. ValidCategories 循环中跳过 LayerName=="Locomotion" 的分类（Idle/Walk/Jog/Run/Sprint）
3. 循环后将 "Locomotion" BlendSpace 状态加入 `CategoryStateMap`
4. Entry→Locomotion 连接（优先查找 "Locomotion"，回退 "Idle"）
5. Combat/Reaction 层过渡逻辑不变

### Step 3: Speed → BlendSpace X 绑定

采用**备用方案**（主方案在主 AnimGraph 中程序化创建属性绑定过于复杂）：

在 `SekiroAnimInstance` 中：
- `NativeInitializeAnimation`：通过 `TFieldIterator<FStructProperty>` 遍历编译后的 ABP 类，找到第一个 `FAnimNode_BlendSpacePlayer` 并缓存指针
- `NativeUpdateAnimation`：计算 Speed 后，通过 `FFloatProperty::SetPropertyValue_InContainer` 反射写入 X 值（X 为 **private**，不可直接访问，AnimNode_BlendSpacePlayer.h:151）

> UE5.2 注意：`FAnimNode_BlendSpacePlayer::X` 声明在 `private:` 段（line 130），仅 `UAnimGraphNode_BlendSpacePlayer` 等 5 个 friend class 可直接访问。外部代码必须通过 UPROPERTY 反射。

## 实现状态 (2026-06-13)

### 已完成

- [x] `BuildLocomotionBlendSpace`：通过 UPROPERTY 反射创建 BlendSpace1D（Speed 0→600, GridNum=4）
- [x] `CreateBlendSpaceStateNode`：手动创建 BoundGraph 绕过 `PostPlacedNewNode` 的重命名冲突
- [x] `BuildStateMachine` 改造：跳过 Locomotion 层类目（Idle/Walk/Jog/Run/Sprint/Locomotion），创建单一 BlendSpace 状态
- [x] Speed → BlendSpace X 绑定：`SekiroAnimInstance::NativeUpdateAnimation` 通过反射写入 FAnimNode_BlendSpacePlayer::X
- [x] Entry → Locomotion 连接优先

### 已验证

- 编译通过（UBT, Development 配置）
- ABP 构建成功（无重命名冲突）
- SavePackage 成功（不再文件锁定）
- 5 个 BlendSpace 采样点创建成功（从 IR "Locomotion" 类目动态采样）

### 实际采样结果

| Speed | AnimID | 动画名 |
|-------|--------|--------|
| 0 | 1151 | Anim_Sekiro_Idle_WeaponOut |
| 150 | 5000 | Anim_Sekiro_Turn_L45 |
| 350 | 5113 | Anim_Sekiro_WeaponPose_a000_005113 |
| 500 | 5403 | Anim_Sekiro_WeaponPose_a000_005403 |
| 600 | 5603 | Anim_Sekiro_WeaponPose_a000_005603 |

### 与设计的差异

- **类目命名**：Sekiro TAE 使用 `InferCategoryFromAnimID` 派生的扁类目名（Common/Locomotion/Attack/...），不是 Idle/Walk/Jog/Run/Sprint 五类。采样选择改为从 Locomotion 类目的已匹配 AnimID 中按序均匀选取 5 个。
- **BlendParameters 反射**：`BlendParameters[3]` 是固定 C 数组（`FStructProperty`, ArrayDim=3），不是 TArray，因此使用 `ContainerPtrToValuePtr(BlendSpace, 0)` 直接访问元素 0，而非 FScriptArrayHelper。
- **BlendSpace 独立资产**：BlendSpace 必须创建为独立 Package（`/Game/Characters/Sekiro/SK_Locomotion_BS`），而非 AnimBlueprint 的子对象。因为编辑器 `FBlendSampleDetails::CustomizeDetails` 期望 BlendSpace 的 Outer 是 `UBlendSpaceGraph`，子对象形式会导致 `check(BlendSpaceNode)` 崩溃。

## 已知限制

- **仅前向移动**：1D BlendSpace 只用 Speed 轴，未处理方向（Angle）。转弯、后退等使用 Walk_Fwd/Jog_Fwd 动画。
- **无 Start/Stop 过渡**：缺少 Walk_Start、Run_Stop 等过渡动画采样。
- **动画数量**：当前每个类别只用第一个匹配动画，未区分 Fwd/Bwd/L/R 方向变体。

## 后续扩展

- **Phase 2**：2D BlendSpace (Speed × Angle) 支持方向混合
- **Phase 3**：Start/Stop/Turn 过渡动画
- **Phase 4**：Crouch/InAir 独立 BlendSpace
- **Phase 5**：Layered Blend Per Bone（下半身 locomotion + 上半身 combat）
