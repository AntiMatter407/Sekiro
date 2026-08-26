# ALS V4 风格只狼动画系统改造 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [ALS V4 风格只狼动画系统改造](../plan/als-v4-sekiro-animation-system.md) | 🟢 已完成 | 2026-07-29 | 2026-07-30 |

## 设计目标

以经典 ALS V4 的动画职责划分为学习参照，用项目已有 C++、Lua 动画蓝图编译器和只狼 Root Motion 动画实现同类能力。

方案追求的是“概念可对照、职责可解释”，而不是让项目文件结构、蓝图节点或变量名称逐字复制 ALS V4。

## ALS V4 对照关系

| ALS V4 概念 | 项目承载位置 | 处理方式 |
|-------------|--------------|----------|
| `ALS_Character_BP` 基础移动状态 | `ASKCharacter`、`USKMovementComponent`、Movement Lua | 保留项目实现，不迁入 ALS 角色蓝图 |
| Essential Values | `USKAnimInstance` | 整理为稳定的数据采集层 |
| Movement State | `ESKAnimMovementState` 与 Root Locomotion | 保留 Grounded/InAir；特殊状态后续扩展 |
| Movement Action | 新增项目枚举或等价状态 | 表达 Step、Dodge 等互斥动作，不绑定具体资产 |
| Rotation Mode | `ESKAnimRotationMode` 与 Movement Lua | 保留 Velocity/Looking/Sprint Align 项目语义 |
| Gait | `ESKAnimGait` | 保留 Idle/Walk/Run/Sprint |
| Stance | `ESKAnimStance` | 保留 Standing/Crouching |
| Grounded State | Lua GroundLocomotion | 使用 Idle/TurnInPlace/Start/Cycle/Pivot/Stop/Step |
| In Air State | Lua Airborne Layer | 使用只狼 Jump/InAir/Land 动画 |
| Turn In Place | AnimInstance 数据 + Lua 状态机 | 采用 ALS V4 的触发思想，姿势由只狼动画提供 |
| Rotate In Place | Movement Lua + Orientation Warping | 只保留锁定/旋转所需的程序修正 |
| Dynamic Transition | Lua 状态机和曲线门控 | 优先被只狼显式 Start/Stop/Turn 状态替代 |
| Stride Blend / Play Rate | Root Motion 动画 | 不照搬 In-Place 方案，只提供有限倍率修正 |
| Lean | AnimInstance 参数 + 可选 Additive 层 | 先建立数据契约，再决定是否接入资产 |
| Land Prediction | AnimInstance 通用查询 + Lua 选择 | 保留程序预测，驱动 Light/Heavy Land |
| Foot IK | UE Foot Placement + Leg IK | 保留项目原生节点和动画曲线契约 |
| Overlay State | Lua Animation Layer、Slot、Montage | 映射为 Default/Sword/Guard/Combat |
| Mantle / Ragdoll | 无 | 首期排除 |

## 架构设计

### 总体分层

```text
Input Lua
    ↓
Movement Lua + USKMovementComponent
    ↓ 发布速度、输入、朝向和动作快照
USKAnimInstance
    ↓ 生成 ALS V4 风格动画数据
Lua AnimBlueprint + ALI_Sekiro
    ├─ BasePoses
    ├─ BaseLayer
    ├─ OverlayLayer
    ├─ LayerBlending
    └─ FootIK
        ↓
ABP_Sekiro Final Pose
```

生成后的动画蓝图使用原生 Animation Layer Interface 表达职责。主 AnimGraph 只保留层调用顺序，具体状态机、Overlay、Slot 和姿势修正分别进入侧栏可独立打开的 Animation Layer Function Graph：

```text
BasePoses
    → BaseLayer
    → OverlayLayer
    → LayerBlending
    → FootIK
    → FinalPoseInertialization
    → Output Pose
```

- `BasePoses`：承载 Locomotion、InAir、Land 和 Guard 基础状态机。
- `BaseLayer`：保留角色子类覆盖基础姿势的扩展边界，当前透传 `SourcePose`。
- `OverlayLayer`：选择 Default、Sword、Guard、Combat 持续姿势。
- `LayerBlending`：组合武器上半身 Slot 与战斗全身 Slot，明确 Montage 优先级。
- `FootIK`：集中 Weapon Hand IK、Foot Placement 和 Leg IK，确保骨骼修正只执行一次。

`CombatBasePose` 暂时保留为主图中未连接的 UE5.2 旧资产兼容节点，实际输出由 `BasePoses` 内的新状态机提供。最终 Inertialization 位于五层之后。主角与 AI 动画蓝图都实现同一个 `ALI_Sekiro`，并由同一份 Lua 层声明生成。

### 权威边界

1. `USKMovementComponent` 负责物理、碰撞、Root Motion 应用和网络移动基础。
2. Movement Lua 负责自由、锁定、冲刺时的 ActorYaw 策略。
3. `USKAnimInstance` 只采集数据并计算通用派生量，不直接选择动画资产。
4. Lua 动画蓝图负责状态、过渡、资源选择和姿势分层。
5. 动画资产提供 Root Motion、骨骼姿势、曲线和同步语义。
6. Orientation Warping、Foot Placement、Leg IK 等节点只修正动画无法稳定表达的部分。

## 数据模型

### 保留并整理

- `ESKAnimMovementState`
- `ESKAnimRotationMode`
- `ESKAnimGait`
- `ESKAnimStance`
- `ESKAnimGroundedEntryState`
- `ESKAnimTurnDirection`
- `Velocity`
- `Acceleration`
- `MovementInputAmount`
- `AimYawDelta`
- `DirectionDelta`
- `DesiredMoveYaw`
- `bHasMovementInput`
- `bIsMoving`
- `bIsInAir`
- `bIsCrouching`

### 计划新增或实装

```cpp
UENUM(BlueprintType)
enum class ESKAnimMovementAction : uint8
{
    None,
    Step,
    Dodge
};

UENUM(BlueprintType)
enum class ESKAnimOverlayState : uint8
{
    Default,
    Sword,
    Guard,
    Combat
};
```

计划补充的 AnimInstance 数据：

| 数据 | 说明 |
|------|------|
| `SmoothedVelocity` | 供方向和 Lean 稳定计算，不替代真实 Velocity |
| `LocalAcceleration` | Actor 局部空间加速度 |
| `GroundedLeanAmount` | 地面加减速和转向产生的倾斜参数 |
| `InAirLeanAmount` | 空中水平速度产生的倾斜参数 |
| `FallSpeed` | 垂直下落速度 |
| `LandPredictionAmount` | 预测落地强度或混合权重 |
| `RootYawOffset` | Idle/Turn In Place 的根骨偏移，不再固定为零 |
| `MovementAction` | Step/Dodge 等互斥动作 |
| `OverlayState` | 当前姿势层语义 |

新增 C++ 函数时，完整职责、参数、返回值和线程约束注释写在 `.cpp` 实现处；`.h` 只保留必要接口摘要。

## 地面 Locomotion 设计

### 状态职责

| 状态 | 职责 | 只狼动画 |
|------|------|----------|
| Idle | 静止姿势、等待 Turn In Place | `a000_000000`、`a000_005000` |
| TurnInPlace | 锁定待机时的原地转向 | Standing/Crouching Idle Turn 资源 |
| Start | 从无输入进入移动 | Walk/Run/Sprint Start |
| Cycle | 稳定持续移动 | Walk/Run/Sprint Cycle |
| Stop | 输入释放后的制动动作 | Walk/Run/Sprint Stop |
| Pivot | 输入方向大幅反转 | 优先专用资源；缺少时降级到 Turn Start/Stop |
| Step | 一次性四方向垫步 | `a000_213301~213304` |

`StopTurn` 不属于最终拓扑。锁定 Stop 的方向残差由 Graph Orientation Warping 在制动姿势内保持，Stop 完成后直接回 Idle；旧 StopTurn 曲线只保留在资产中供历史审计。

### 默认资产策略

- Walk、Run 使用四方向 Start/Cycle/Stop。
- Sprint 使用前向 Cycle；方向起步和方向停止使用现有只狼资源。
- 锁定移动直接选择四方向主资产，Orientation Warping 修正离散方向之外的角度残差。
- 非锁定移动由 Movement Lua 先旋转 Actor，再使用前向或转向起步动画。
- 不使用 ALS V4 的通用 Stride Blend 驱动只狼 Root Motion。
- 播放倍率只处理小范围速度误差，不承担跨 Walk/Run/Sprint 的速度匹配。

### Turn In Place

Turn In Place 分成“触发逻辑”和“姿势表现”：

1. 未平滑的 `AimYawDelta` 超过进入阈值时产生 Turn 请求，并用较小退出阈值形成迟滞。
2. 进入状态时根据 `AimYawDelta` 锁存左右方向，确保 Movement 开始旋转前保留完整目标偏角。
3. 选择最合适的只狼 Standing/Crouching Turn 动画。
4. `RootYawOffset` 吸收视角与身体之间的静止偏差，并随 ActorYaw 更新逐步归零。
5. 只狼 Turn 资产的根骨起止变换相同，只负责脚步与身体姿势；Movement Lua 是该状态唯一的 ActorYaw 权威。
6. 曲线允许退出后回到 Idle。

这里复用 ALS V4 的瞄准偏角触发、方向锁存和迟滞职责，不照搬面向 In-Place 动画的 `Rotate Root Bone` 图实现。`RootYawOffset` 保留为平滑姿势数据，但不参与进入门槛，避免其尚未追上目标角时 Movement 已经开始消耗偏差。项目 Turn 资产没有旋转 Root Motion，因此首帧先锁存完整偏差，后续由 Movement 平滑旋转胶囊体。

只狼 Turn 动画替代 ALS V4 的 Turn Pose，但不会替代阈值、锁存、打断和偏差收敛逻辑。

### Pivot 降级

当前没有已确认的完整 Pivot 资产集，因此首期采用以下顺序：

1. Walk/Run 中新输入相对当前速度达到 `135°` 时产生 Pivot 请求，回落到 `45°` 后解除，形成迟滞。
2. Cycle 直接进入独立 Pivot 状态，并复用锁存新方向的 Start 资产作为降级姿势。
3. Pivot 保持输入时等待 `CanEnterLoop` 后回 Cycle；释放输入时等待 `CanEnterStop` 后进入 Stop。
4. Sprint 不进入 Pivot，继续使用自身方向 Start/Stop 资产；状态接口保留，方便后续替换专用 Pivot 资源。

降级路径必须保证角色不会卡在 Pivot，也不会同时由 Movement 与 Root Motion 双重旋转。

## InAir 设计

```text
Jump Start
    ├─ 原地 Standing/Crouching
    └─ 锁定八方向 / 非锁定前向
            ↓
Directional InAir
            ↓
Fall Loop
            ↓
Light Land / Heavy Land
```

- Jump Start 资产虽然包含 Root Motion 数据，但离地期间统一忽略；起跳与 InAir 轨迹由 CharacterMovement 物理负责。
- 接地后方向 Light Land 恢复使用只狼 Root Motion；Heavy Land 是原地受力姿势，不接管水平位移。
- 每次离地重置 `PeakFallSpeed`；`LandPredictionAmount >= 0.25` 且峰值下落速度达到 `850 cm/s` 时提前锁存 Heavy Land。
- 预测未命中表面时，实际接地边沿仍按 `PeakFallSpeed` 兜底；预测和动画选择都不改变物理落点。
- Light Land 保留原地/非锁定前向和锁定八方向分支；Heavy Land 根据起跳时 Standing/Crouching 姿态选择资源。
- Land 动画通过 `CanResumeMovement` 与 `CanExitLand` 曲线控制可打断窗口。
- Heavy Land 阈值集中到调参表，不散落在状态转换中。

## Lean 设计

ALS V4 的 Lean 思路保留为数据能力，但不强制使用 ALS Additive 资产：

- Grounded Lean 来源于局部加速度、角速度和当前 Gait。
- InAir Lean 来源于水平速度、朝向差和空中阶段。
- `USKAnimInstance` 已发布 `GroundedLeanAmount` 和 `InAirLeanAmount`，Movement 与状态机不依赖具体 Lean 资产。
- 当前 Lua 编译器没有 `Apply Additive` 节点契约，也没有确认可用的只狼 Lean Additive 资产，因此不生成无消费者变量或伪 Lean 节点。
- `LayeredActionPose` 到 `Pose Correction` 之间是可选 Additive 接口；找到合适资产并补齐节点契约后，只在该边界接入，不修改 Movement 或 Locomotion 状态机。

## Foot IK 设计

- 继续使用当前 UE Foot Placement 和 Leg IK。
- `FootIKAlpha` 负责 Grounded/InAir 混合。
- `FootIKAlpha` 在空中和全身动作期间淡出，落地后平滑淡入；Foot Placement 与 Leg IK 必须消费同一权重。
- 当前 `PlantLockType = Unlocked`，不伪造 ALS V4 的左右脚锁定曲线；`FootPlant` 和 `MovePhase` 只保留为资产标注、调试和后续相位匹配数据。
- `CanEnter...`、`CanExit...`、`CanResumeMovement` 等曲线只控制状态转换窗口，不兼任 Foot IK 权重。
- Start、Stop、Turn、Jump、Dodge 等 Root Motion 动作必须逐类检查脚锁定需求。
- 当前骨架脚目标不适合时，不强行复制 ALS V4 的 Foot Lock 实现。
- Foot Lock 是否启用由骨架和动画实测决定，不作为首期编译完成的阻塞条件。

## 姿势修正与同步契约

- Orientation Warping 只用于锁定地面 Start/Cycle/Stop/Step 的真实轨迹对齐，以及方向 Jump 的量化残差；Sprint 和非锁定地面移动不重复扭曲。
- Standing/Crouching Walk、Run、Sprint Cycle 的 Sequence Player 使用 `SekiroLocomotion` Sync Group，并显式设置 `CanBeLeader` 与 `SyncGroup`。
- `MovePhase` 暂不作为运行时同步输入；如果普通同步组仍出现明显错脚，再单独补充相位匹配节点，不恢复旧动态播放器。
- Grounded Cycle 内保留方向、步态和姿态切换的 Inertialization；主图末端再提供统一的 `FinalPoseInertialization`，供后续 Overlay 和 Slot 层的惯性化请求使用。
- Transition 曲线门控继续由原生 Gate 节点采样；Lua 更新函数只发布业务意图，不直接读取当前动画时间。

## Overlay 设计

| Overlay | 基础姿势 | 动作覆盖 |
|---------|----------|----------|
| Default | 普通 Locomotion | 无 |
| Sword | 当前阶段复用普通 Locomotion，由武器展示状态选中 | Draw/Sheathe 使用上半身窗口；后续可直接替换 Sword 分支姿势 |
| Guard | Guard Idle/Move | Raise/Lower、受击中断 |
| Combat | 当前阶段复用普通 Locomotion，作为战斗动作底座 | Attack、Deflect、Posture Break 全身 Montage |

`USKAnimInstance` 根据当前武器展示状态发布 Overlay：收刀为 Default、拔刀为 Sword；持续防御和全身战斗动作分别提升为 Guard、Combat。攻击、弹反和架势崩溃属于全身动作，不为追求 ALS Overlay 形式而拆成上半身动画。

这些分支位于 `OverlayLayer` Function Graph；`LayerBlending` 只处理 Slot 与分骨骼混合，不再同时承担 Overlay 选择。

## 动画数据修改

默认需要检查或补齐：

| 数据 | 作用 |
|------|------|
| Root Motion 根轨迹 | 与 Movement 速度和旋转权威匹配 |
| `CanEnterCycle` | Start 进入 Cycle |
| `CanEnterStop` | Cycle 允许进入 Stop |
| `CanExitStop` | Stop 返回 Idle/Start |
| `CanExitTurn` | Turn 返回 Idle/Start |
| `CanEnterInAir` | Jump Start 进入 InAir |
| `CanEnterLoop` | 方向 InAir 进入 Fall Loop |
| `CanResumeMovement` | Land 可被移动打断 |
| `CanExitLand` | Land 完整退出 |
| Sync Marker | Walk/Run/Crouch Cycle 脚步相位同步 |
| Foot IK 曲线 | 动作阶段允许或禁用脚部修正 |

语义动画曲线发生新增、删除或重命名时，必须同步更新 `Docs/animation-curve-authoring-guide.md`。

## 数据流

### 每帧更新

```text
Input Lua
  → 发布移动输入与动作意图
Movement Lua
  → 决定 MovementTier、ActorYaw 和锁定方向快照
USKAnimInstance::NativeUpdateAnimation
  → 采集速度、加速度、空中状态、相机和战斗数据
  → 计算 Essential Values、Lean、RootYawOffset、Land Prediction
ABP_Sekiro Lua Update
  → 锁存一次性动作方向与步态
  → 选择状态、动画资产和姿势修正参数
UE AnimGraph
  → 求值状态机、Overlay、Montage、Warping 和 IK
```

### ActorYaw 所有权

- 自由移动和冲刺：Movement Lua 控制 ActorYaw。
- 锁定移动：Movement Lua 朝锁定目标旋转 Actor。
- Turn In Place：Movement Lua 控制 ActorYaw；只狼 Turn 动画只提供脚步和身体姿势，不启用 Root Motion。
- 动画图不得在 Movement 完成旋转后重新推导“旋转前方向”，必须读取 Movement 快照。

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Source/Sekiro/Animation/SKAnimDataTypes.h` | 修改 | 增加或整理 Movement Action、Overlay State 等通用动画类型 |
| `Source/Sekiro/Animation/SKAnimInstance.h` | 修改 | 暴露 ALS V4 对照数据 |
| `Source/Sekiro/Animation/SKAnimInstance.cpp` | 修改 | 采集和计算 Essential Values、Lean、RootYawOffset、Land Prediction |
| `Content/Script/Animation/Sekiro/ABP_Sekiro.lua` | 修改 | 主图更新、锁存和 Overlay 编排 |
| `Content/Script/Animation/Sekiro/ALI_Sekiro.lua` | 新增 | 声明五个原生 Animation Layer 接口签名 |
| `Content/Script/Animation/Sekiro/ABP_SKAICharacter.lua` | 修改 | 使用相同接口与五层图，保留 AI 专用运行时更新 |
| `Content/Script/Animation/Sekiro/AnimAssets.lua` | 修改 | 增加稳定的语义资产分组 |
| `Content/Script/Animation/Sekiro/Shared/Tuning.lua` | 修改 | 集中阈值、混合和降级参数 |
| `Content/Script/Animation/Sekiro/Shared/CurveNames.lua` | 修改 | 登记新增动画曲线语义 |
| `Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Root.lua` | 修改 | Grounded/InAir 根层保持清晰边界 |
| `Content/Script/Animation/Sekiro/Layer/GroundLocomotion/GroundedMode.lua` | 修改 | 重构 Idle/Start/Cycle/Stop/Turn/Pivot/Step |
| `Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Standing.lua` | 修改 | Standing 资产与姿势构建 |
| `Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Crouching.lua` | 修改 | Crouching 资产与姿势构建 |
| `Content/Script/Animation/Sekiro/Layer/Airborne/Jump.lua` | 修改 | Jump、Fall、Land 和预测选择 |
| `Content/Script/Animation/Sekiro/Layer/Combat/*` | 修改 | Overlay 与全身 Montage 优先级 |
| `Docs/animation-curve-authoring-guide.md` | 修改 | 曲线生产者、消费者和验证方式 |
| `Content/Characters/Sekiro/ABP_Sekiro.uasset` | 生成 | Lua 描述生成的动画蓝图 |
| `Content/Characters/Sekiro/ALI_Sekiro.uasset` | 生成 | Animation Layer Interface 资产 |
| `Content/Gameplay/AI/ABP_SKAICharacter.uasset` | 生成 | 与主角同架构的 AI 动画蓝图 |
| `Content/Characters/Sekiro/Animations/*.uasset` | 修改 | Root Motion、曲线和同步标记 |

实际实施前应重新检查工作区已有的动画资产修改，避免覆盖用户尚未提交的曲线和 Root Motion 调整。

## 验证方案

### 默认允许执行

1. Lua 函数文档检查。
2. Lua 动画蓝图描述生成。
3. 动画蓝图编译。
4. UBT 编译对应项目目标。
5. 资产和曲线静态检查。

### 需要用户明确授权

- 启动 PIE。
- 模拟移动、锁定、跳跃或战斗输入。
- 执行运行时动画观感测试。

### 后续 PIE 验收场景

- 自由模式 Walk/Run/Sprint 起停和转向。
- 锁定模式四方向移动与方向切换。
- Standing/Crouching Turn In Place。
- 180 度输入反转的 Pivot 降级。
- 原地、移动和锁定八方向跳跃。
- Light/Heavy Land 与落地移动恢复。
- 斜坡、台阶和地面边缘 Foot IK。
- Guard 移动、攻击和弹反对 Locomotion 的覆盖优先级。

## 依赖与风险

| 风险 | 影响 | 处理 |
|------|------|------|
| ALS V4 主要按 In-Place 思路组织，项目动画大量使用 Root Motion | 直接复制图逻辑会双重驱动位移或旋转 | 只复制职责和状态概念，不复制位移实现 |
| Turn 动画实际旋转角和命名语义不一致 | Turn In Place 结束后残留角度 | 实测 Root Motion，锁存目标角并限制残差 |
| 缺少完整 Pivot 动画 | 大角度反向观感不完整 | 使用 Stop → Start 稳定降级 |
| Foot Target 骨骼与 ALS Mannequin 不同 | ALS Foot Lock 不能直接复用 | 使用项目骨架和 UE Foot Placement |
| 动画曲线缺失或时间点不准确 | 状态卡死、提前切换或脚滑 | 曲线门控提供超时降级并集中登记 |
| 当前工作区已有未提交动画资产修改 | 生成或批处理可能覆盖人工调整 | 实施前逐文件核对，禁止批量覆盖未知修改 |

## Agent 派发

| 子任务 ID | 任务描述 | 负责 Agent | 状态 |
|-----------|----------|------------|------|
| 2 | `Source/Sekiro` 动画数据层 | gameplay-programmer | ✅ |
| 3 | Lua 地面 Locomotion 与资产编排 | 主任务 Agent | ✅ |
| 4 | Lua InAir/Land 与预测接入 | 主任务 Agent | ✅ |
| 5 | Warping、IK、曲线和同步 | 主任务 Agent | ✅ |
| 6 | Overlay 与战斗分层 | 主任务 Agent | ✅ |
| 7 | 编译和静态验证 | 主任务 Agent | ✅ |
| 8 | Animation Layer IR、接口资产与磁盘迁移 | plugin-programmer + 主任务 Agent | ✅ |

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-07-29 | 创建；以 ALS V4 为学习基线，记录默认动画取舍与实施边界 |
| 2026-07-29 | 主 AnimGraph 展开 Default/Sword/Guard/Combat Overlay；攻击与弹反继续由 CombatFullBodySlot 保持最高覆盖优先级 |
| 2026-07-30 | 按用户期望改为 `ALI_Sekiro` 驱动的五层 Animation Layer 架构；主图仅保留层调用链，磁盘重载验证进行中 |
| 2026-07-30 | 修复 UE5.2 Animation Layer Interface 的磁盘签名持久化；重启编辑器后 `ABP_Sekiro` 与 `ABP_SKAICharacter` 均可从磁盘恢复五层调用链并编译为 UpToDate |
