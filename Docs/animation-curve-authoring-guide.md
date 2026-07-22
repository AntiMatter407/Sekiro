# 动画曲线生成与使用手册

本文是项目语义动画曲线的唯一登记入口。所有新增、重命名、删除或改变语义的动画曲线，都必须同步更新本文的曲线登记表、生成来源、运行时消费者和验证方法。

本文回答四个问题：

1. 曲线表达什么语义。
2. 曲线由什么数据、工具和规则生成。
3. Lua、C++ 和 BlendSpace 如何读取它。
4. 曲线缺失或数据不可信时如何处理。

Locomotion 曲线的姿势采样、脚步检测和匹配评分算法详见 [Locomotion 曲线自动标注方案](design/locomotion-curve-auto-annotation.md)。本文只记录稳定的对外契约，不复制算法实现细节。

## 一、术语与边界

### 语义动画曲线

本文所说的动画曲线，是写在 `UAnimSequence` 上、由运行时代码按动画播放时间采样的 UE Animation Curve。它描述“当前帧允许做什么”或“当前帧处于什么阶段”，例如 `CanEnterLoop`。

骨骼动画轨道、Root 骨骼位移和旋转、RootMotion 提取结果不属于本文登记的语义曲线。它们负责姿势和运动数据，不能因为名字里出现 Root 或 Curve 就混为一类。

### 曲线存储类型

UE 底层仍以浮点曲线保存数据，项目在写入和使用层约定以下语义类型：

| 类型 | 值域 | 插值 | 适用场景 |
| --- | --- | --- | --- |
| `float` | 连续数值 | 线性 | 相位、权重、连续参数 |
| `int bool` | `0` 或 `1` | 阶梯 | 独立的允许/禁止窗口 |
| `int enum` | 离散整数 | 阶梯 | 同一时刻只能取一个枚举语义 |
| `int flags` | 位掩码 | 阶梯 | 多个标志可同时成立，且消费者明确使用按位判断 |

互相独立、未来可能重叠的条件必须拆成独立布尔曲线，不要压入同一枚举。只有确实需要同时携带多个标志时才使用位掩码。

## 二、曲线登记表

状态说明：`使用中` 表示已有运行时消费者；`已生成` 表示资产中有数据但当前只用于分析或后续功能；`保留` 表示有导入工具或旧接口，但当前业务链路不依赖它。

| 曲线名 | 状态 | 类型 | 语义 | 写入资产 | 当前消费者 |
| --- | --- | --- | --- | --- | --- |
| `CanEnterLoop` | 使用中 | `int bool` | Start 或锁定 Jump InAir 过渡段已进入可衔接循环姿势的窗口 | Locomotion Start、非锁定/原地 Jump Start、锁定 Jump InAir Sequence | Lua GroundLocomotion 的 Start 到 Cycle；Jump Start/InAir 到 Loop |
| `CanEnterStop` | 使用中 | `int bool` | 当前 Start/Loop 帧可进入匹配的 Stop | Start Sequence、BlendSpace 使用的 Loop 样本 Sequence | Lua GroundLocomotion 的 Start/Cycle 到 Stop |
| `CanEnterIdle` | 使用中 | `int bool` | Stop 已进入可回 Idle 或退出外层 Sprint 的窗口 | Walk/Run/Sprint Stop Sequence | Stop 到 Idle；外层 Sprint 到 Standing/Crouching |
| `CanExitStep` | 使用中 | `int bool` | Step 已进入可返回普通地面移动的尾部窗口 | 四方向 Step Sequence | 外层 Step 到 Standing/Crouching |
| `CanExitTurn` | 使用中 | `int bool` | 原地 Turn 已进入可返回 Idle 的尾部窗口 | 站立/蹲姿左右 Idle Turn Sequence | Lua GroundLocomotion 的 Turn 到 Idle |
| `CanEnterInAir` | 使用中 | `int bool` | 锁定八方向 Jump Start 已进入可衔接方向 InAir 过渡段的窗口 | 锁定八方向 Jump Start Sequence | Jump Start 到 DirectionalInAir |
| `CanResumeMovement` | 使用中 | `int bool` | Jump Land 已恢复到可被移动输入安全打断的姿势窗口 | 原地/非锁定共用及锁定八方向 Jump Land Sequence | 有输入时外层 InAir 提前返回 Grounded |
| `CanExitLand` | 使用中 | `int bool` | Jump Land 已进入可完整结束落地的尾部窗口 | 原地/非锁定共用及锁定八方向 Jump Land Sequence | 无输入时外层 InAir 返回 Grounded |
| `AttackSide` | 使用中 | `int enum` | 当前攻击动作提交下一攻击侧，`-1=Left`、`0=Keep`、`1=Right` | 首版地面攻击 Sequence | Lua 战斗动作状态机 |
| `CanAcceptLightAttack` | 使用中 | `int bool` | 当前攻击动作允许缓存下一段短按攻击 | 首版地面攻击 Sequence | Lua 战斗动作状态机 |
| `CanAcceptHeavyAttack` | 使用中 | `int bool` | 当前攻击动作允许缓存蓄力攻击 | 首版地面攻击 Sequence | Lua 战斗动作状态机 |
| `CanCancelToGuard` | 使用中 | `int bool` | 当前全身动作允许防御输入取消 | 支持防御取消的战斗 Sequence | Lua 战斗动作状态机 |
| `CanCancelToDodge` | 已登记 | `int bool` | 当前全身动作允许闪避输入取消 | 支持闪避取消的战斗 Sequence | 后续闪避动作接入时启用 |
| `FootPlant` | 已生成 | `int enum` | 当前稳定触地脚 | 需要脚步分析的 Locomotion Sequence | 自动标注、调试和相位校验；GroundLocomotion 暂未直接消费 |
| `MovePhase` | 已生成 | `float` | 一个步态周期中的循环相位 | Start 与周期 Loop | 自动标注、调试和后续精确相位匹配；当前 GroundLocomotion 未直接消费 |
| `FrameFlags` | 保留 | `int flags` | TAE 帧级行为标志 | 有对应 TAE 事件的动作动画 | 当前无正式运行时消费者 |
| `CancelActions` | 保留 | `int enum` | TAE 动作取消窗口类型 | 有对应 TAE 事件的动作动画 | 当前无正式运行时消费者 |
| `AttackHitbox` | 保留 | `int enum` | TAE 攻击框类型窗口 | 有对应 TAE 事件的攻击动画 | 当前武器碰撞走独立组件链路，不消费该曲线 |

基础 Locomotion 动画不得因为旧 TAE 管线存在，就批量写入 `FrameFlags`、`CancelActions` 或 `AttackHitbox`。没有可靠源数据、没有消费者的曲线不写入资产。

## 三、CanEnter 曲线

### 共同规则

所有 Transition 语义曲线都使用 `0/1` 阶梯值：

- `0`：当前帧不允许进入目标状态。
- `1`：当前帧允许进入目标状态。
- 曲线只写入语义适用的动画，不给无关动画添加全程为 `0` 的空曲线。
- 输入、速度、姿态和物理状态决定“是否想切换”；语义曲线决定动画何时允许切换。
- 输入打断、Dodge 升级 Sprint、起跳和落地检测等必须即时响应的 Transition 不强制增加曲线 Gate。
- 需要等待动画窗口的 Transition 禁止再用 `TimeRemainingLessEqual` 作为正常条件或兜底。
- 阶梯曲线最后一次状态变化后必须在动画末尾重复同值关键帧。UE 会把没有后续区间的孤立末键视为无持续时间语义，压缩后可能保留前一段值。

### CanEnterLoop

含义：当前起步动作已完成关键重心转移，可以混入目标循环动画。

生成方式：

1. 对 Start 与目标 Loop 采样 Root 速度、Pelvis、双脚和关键骨骼姿势。
2. 跳过动作前段不可切区域。
3. 搜索与 Loop 姿势、速度和脚步相位最匹配的候选帧。
4. 从匹配帧开始写 `1`，直到动画结束。
5. Sprint Start 在 TAE 存在可靠移动进入窗口时，可优先使用原始事件帧；否则使用离线匹配结果。

当前由统一 Grounded 阶段状态机的 `Start -> Cycle` Gate，以及 Jump 状态机的非锁定/原地 `Start -> Loop`、锁定 `DirectionalInAir -> Loop` Gate 消费。

### CanEnterStop

含义：当前 Start 或循环动画所处的脚步相位和姿势适合进入目标 Stop，不会因起始脚或身体姿势错位产生明显顿挫。

生成方式：

1. 读取 Start/Loop 的 `MovePhase`、`FootPlant` 和姿势采样结果。
2. 将目标 Stop 的起始姿势与源动画可取消区间比较。
3. 在匹配相位附近生成一个或多个允许窗口。
4. 窗口内写 `1`，窗口外写 `0`。
5. Start 资产从 `CanEnterLoop` 开启点起保持 `CanEnterStop = 1` 到结尾，保证保持输入和释放输入分别能进入 Cycle 与 Stop，不会错过最后窗口。

适用动画：Walk/Run/Sprint Start、被 Walk/Run BlendSpace 使用的循环样本，以及独立 Sprint Loop。Start 的多个触地窗口允许短输入在最近安全帧进入 Stop；Loop 曲线仍写在样本 `UAnimSequence` 上，不写在 BlendSpace 资产本身。

### CanEnterIdle

含义：当前停止动作已回到稳定站立姿势，可以进入 Idle。

生成方式：

1. 比较 Stop 后段与 Idle 的姿势差。
2. 同时检查 Root、Pelvis 速度已降低到稳定范围。
3. 从最佳匹配帧开始写 `1`，直到动画结束。

适用动画是 Walk/Run Stop、Sprint Stop，以及确实以 Idle 为目标的 TurnStop。当前由统一 Grounded 阶段状态机的 `Stop -> Idle` Gate 消费。

### Step、Turn 与 Jump 曲线

- `CanExitStep`：写入四方向 Step；从动画尾部提前 `0.06` 秒开启，使 `Step -> Cycle/Idle` 保留交叉混合区间。
- `CanExitTurn`：写入站立和蹲姿的左右 Idle Turn；从动画尾部提前约 `0.12` 秒开启，使 `Turn -> Idle` 保留交叉混合区间。当前资产由 AIBridge `anim_blueprint add_curve` 按实际时长写入并保存。
- `CanEnterInAir`：只写入锁定八方向 Jump Start；从动画尾部提前 `0.08` 秒开启，使 Start 完成后衔接非循环的 DirectionalInAir。
- `CanEnterLoop`：写入非锁定前向/原地 Jump Start 和锁定八方向 Jump InAir；从动画尾部提前 `0.08` 秒开启，仍未落地时进入通用 Jump Loop。
- `CanResumeMovement`：写入原地/非锁定共用及锁定八方向 Jump Land。`Jump_Light_Stand` 在归一化 35% 开启；锁定八方向 Land 因与地面 Cycle 姿势差异更大，在归一化 50% 开启。有移动输入时可以较早恢复 Cycle，但不会从 Land 首帧硬切。
- `CanExitLand`：同样写入全部 Jump Land；保持原有窗口，`Jump_Light_Stand` 在归一化 90%（`0.600s`）开启，锁定八方向 Land 在归一化 92%（`0.920s`）开启。没有移动输入时继续完整播放 Land，不受提前移动窗口影响。
- 三者均由 `generate_locomotion_move_curve_payload.py` 根据实际 Sequence 时长生成 `0 -> 1` 阶梯键，并在动画末尾重复允许值。

### MoveTransition 拆分记录

旧曲线 `MoveTransition` 把三个互斥值压在同一枚举中：

| 旧值 | 新曲线 |
| ---: | --- |
| `1` | `CanEnterLoop = 1` |
| `2` | `CanEnterStop = 1` |
| `3` | `CanEnterIdle = 1` |

迁移时保留原关键帧时间，将每种值展开成目标曲线的 `0/1` 阶梯关键帧，并删除连续重复值。只有实际出现过 `1` 的目标曲线才写入该动画；写入成功后删除旧 `MoveTransition`。

旧迁移数据保留在 `Script/temp/split_move_transition_curves.json` 供审计。当前生成器直接写独立曲线，不再先生成 `MoveTransition` 再拆分。截至 2026-07-19，Lua 动画蓝图引用资产的语义 Gate 验证结果为：`CanEnterLoop` 35 个、`CanEnterStop` 41 个、`CanEnterIdle` 19 个、`CanExitStep` 4 个、`CanExitTurn` 4 个、`CanEnterInAir` 8 个、`CanResumeMovement` 9 个、`CanExitLand` 9 个，96 个资产零错误。Jump 的非锁定/原地 Start 与锁定 DirectionalInAir 已补齐 `CanEnterLoop`；锁定八方向 Start 保留 `CanEnterInAir`。

蹲姿 Locomotion 使用 `005000~005603` 资源组，与站姿 Idle/Turn、Walk/Run Start/Loop/Stop 语义对应。`Script/generate_crouch_locomotion_curves.py` 从站姿规范 payload 读取关键帧，按源/目标动画时长转换归一化位置后写入蹲姿资源；第二组 Start（`005110~005113`、`005410~005413`）复用同方向第一组 Start 模板。当前结果覆盖 37 个资产，其中 Idle 和 4 个 Idle Turn 不写无消费者曲线，其余 32 个资产共写入 80 条曲线：`CanEnterLoop` 16 条、`CanEnterStop` 24 条、`CanEnterIdle` 8 条、`MovePhase` 24 条、`FootPlant` 8 条。

### 当前接入状态

截至 2026-07-18，需要动画时机的 Locomotion 跳转全部改为曲线 Gate：

| 过渡 | 当前实现 |
| --- | --- |
| `Start -> Stop` | 输入释放后等待 `CanEnterStop >= 0.5` |
| `Start -> Cycle` | 保持移动时等待 `CanEnterLoop >= 0.5` |
| `Cycle -> Stop` | 停止意图确认后等待 `CanEnterStop >= 0.5` |
| `Stop -> Idle` | 无新输入时等待 `CanEnterIdle >= 0.5` |
| `Step -> Standing/Crouching` | Dodge 结束后等待 `CanExitStep >= 0.5` |
| `Turn -> Idle` | 没有移动或闪避输入时等待 `CanExitTurn >= 0.5` |
| `Sprint -> Standing/Crouching` | Sprint 意图结束后等待嵌套 Stop 输出的 `CanEnterIdle >= 0.5` |
| `Jump Start -> DirectionalInAir` | 锁定有向跳仍在空中且 `CanEnterInAir >= 0.5` |
| `Jump Start -> Loop` | 非锁定或原地跳仍在空中且 `CanEnterLoop >= 0.5` |
| `Jump DirectionalInAir -> Loop` | 锁定有向跳仍在空中且 `CanEnterLoop >= 0.5` |
| `InAir -> Grounded`（有移动输入） | 已落地、有移动输入且嵌套 Land 输出的 `CanResumeMovement >= 0.5` |
| `InAir -> Grounded`（无移动输入） | 已落地、无移动输入且嵌套 Land 输出的 `CanExitLand >= 0.5` |

曲线缺失时 Gate 失败关闭，避免在错误姿势窗口静默跳转。资产生成和 `Check Lua` 前的审计必须保证所有被消费曲线存在且至少包含一个允许区间。

## 四、相位与触地曲线

### FootPlant

`FootPlant` 是单值枚举，不是业务标志位集合：

| 值 | 含义 |
| ---: | --- |
| `0` | 无稳定触地 |
| `1` | 左脚稳定触地 |
| `2` | 右脚稳定触地 |
| `3` | 双脚稳定触地 |

生成器按脚部高度、水平速度、垂直速度和连续稳定帧数检测触地。它主要用于验证自动标注、辅助 Loop 到 Stop 匹配和调试步态；在 GroundLocomotion 正式读取它之前，不要在 Lua 中假定它一定存在。

### MovePhase

`MovePhase` 是 `0.0~1.0` 的连续周期相位：

- `0.0`：左脚主要触地。
- `0.5`：右脚主要触地。
- `1.0`：下一次左脚主要触地。

它不是“动画开始为 0、结尾为 1”的普通时间归一化直线。生成器必须从实际左右脚接触锚点建立相位；检测不到可靠锚点时不写曲线，并把动画列入低置信度报告。

## 五、当前运行时消费

业务 `CanEnter_*` 只回答输入和角色状态等业务意图，不直接采样动画曲线。曲线比较由编译期 Gate DSL 声明，并由 NodeFactory 生成原生 Transition Rule 节点：

```lua
Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
    BlendDuration = Tuning.StopBlendDuration,
    Gate = Rule.CurveGreaterEqual(
        CurveNames.CanEnterStop,
        Tuning.CurveThreshold),
})

---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 输入是否已经释放。
function GroundLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst.bHasMovementInput ~= true
end
```

曲线名集中声明在 `Animation.Sekiro.Shared.CurveNames`。`CanEnterLoop/Stop/Idle`、`CanExitStep`、`CanExitTurn`、`CanEnterInAir`、`CanResumeMovement` 和 `CanExitLand` 已有正式 Transition 消费者；`FootPlant` 和 `MovePhase` 继续用于标注、调试和后续相位匹配。

### 历史相位匹配实现

旧动态 Pose Graph 曾把 `MovePhase` 映射到单位圆进行混合，并反查目标 Sequence/BlendSpace 的 `NormalizedStartPosition`。该运行时消费者已经随旧 Host/快照架构删除。

如果后续重新接入精确相位匹配，应通过新的原生 AnimNode 或 Transition 能力实现，并在完成运行时消费者和验证后，把登记表中的 `MovePhase` 状态改回“使用中”。

## 六、缺失曲线处理

默认原则是失败关闭：曲线缺失时返回 `0`，不允许正常过渡路径静默放行。

不再使用剩余时间或 normalized time 常量作为运行时兜底。缺失必须在生成/验证阶段报出动画别名、资产路径、状态名和曲线名，并修复资产后重新生成动画蓝图。

## 七、生成与更新流程

新增或重算曲线按以下顺序执行：

1. 在本文登记名称、语义类型、值域、适用动画、生成来源、消费者和缺失行为。
2. 明确源数据：动画姿势、RootMotion、脚步接触、TAE 事件或人工标注。没有可靠来源则不生成。
3. 先生成 JSON 报告和写入 payload，检查资产范围、关键帧时间、低置信度项和曲线数量。
4. 使用 AIBridge/Editor 工具小批量写入并人工验证，确认后再批量执行。
5. 只修改语义相关的动画，不给所有动画创建空曲线。
6. 写入后审计曲线名称、资产数量、关键帧和值域，并检查被替代的旧曲线为 0 个残留。
7. 在 Sequence、BlendSpace 和实际状态机中分别验证读取结果与过渡表现。

当前 Locomotion 生成入口和中间产物：

- 生成脚本：`Script/temp/generate_locomotion_move_curve_payload.py`
- Lua Transition 曲线验证器：`Script/verify_lua_anim_transition_curves.py`
- 初始写入数据：`Script/temp/set_locomotion_move_curves.json`
- 拆分迁移数据：`Script/temp/split_move_transition_curves.json`
- 蹲姿复制生成器：`Script/generate_crouch_locomotion_curves.py`
- 蹲姿逐键验证器：`Script/verify_crouch_locomotion_curves.py`
- 蹲姿写入数据：`Script/temp/set_crouch_locomotion_curves.json`
- 蹲姿审计报告：`Script/temp/crouch_locomotion_curve_report.json`
- 算法设计：[Locomotion 曲线自动标注方案](design/locomotion-curve-auto-annotation.md)

TAE 曲线入口：

- 源数据：`Extracted/Sekiro_TAE_Logic.json`
- 写入脚本：`Script/write_tae_curves.py`
- 当前只应对有明确 TAE 事件且业务需要的动作动画运行。

## 八、新曲线登记模板

以后增加曲线时，先在“曲线登记表”添加一行，再补充以下内容：

```markdown
### CurveName

- 状态：使用中 / 已生成 / 保留
- 类型：float / int bool / int enum / int flags
- 值域与插值：
- 精确定义：
- 适用动画：
- 禁止写入的动画：
- 源数据：
- 生成器与报告路径：
- Lua/C++ 消费者：
- Sequence 读取方式：
- BlendSpace 读取方式：
- 缺失时行为：
- 验证方法：
- 替代或废弃的旧曲线：
```

命名应表达稳定业务语义。允许窗口优先使用 `CanEnter...`，连续权重使用能说明对象的名称，相位使用 `...Phase`。禁止使用只对应某个临时状态机实现、资产编号或模糊缩写的名字。

## 九、实现索引

- Lua 曲线读取封装：`Content/Script/Animation/Base/LuaAnimStateMachine.lua`
- Sekiro 曲线名声明：`Content/Script/Animation/Sekiro/Shared/CurveNames.lua`
- GroundLocomotion 消费逻辑：`Content/Script/Animation/Sekiro/Layer/GroundLocomotion/` 与 `Content/Script/Animation/Sekiro/Layer/Airborne/Jump.lua`
- 曲线 Gate 生成实现：`Plugins/SekiroAnimBlueprintExt/Source/SekiroAnimBlueprintExtEditor/Private/SekiroAnimBlueprintFactoryLibrary.cpp`
- Lua 动画蓝图写法：[Lua 动画蓝图编写手册](lua-anim-blueprint-authoring-guide.md)
- Lua 编码规范：[Lua 代码规范](lua-code-style.md)
## 十、战斗动画曲线

首版攻击/防御动作原型从活动 `UAnimSequence` 直接采样以下曲线，不读取最终混合 Pose 的曲线值。这样可避免全身 Slot 淡入淡出时把枚举值混合成中间浮点数。

| 曲线 | 值域 | 作者规则 | 缺失行为 |
|---|---:|---|---|
| `AttackSide` | `-1 / 0 / 1` | 命中动作结束后、输入窗口开始前提交一次下一攻击侧；`-1=Left`、`0=Keep`、`1=Right`，动作末尾回零 | 保持当前侧别 |
| `CanAcceptLightAttack` | `0 / 1` | 可接受下一次短按攻击的时间范围 | 不接受轻攻击续段 |
| `CanAcceptHeavyAttack` | `0 / 1` | 可接受并等待本次按键释放判定长按的时间范围 | 不接受重攻击续段 |
| `CanCancelToGuard` | `0 / 1` | 当前动作允许防御键取消的时间范围 | 不允许防御取消 |
| `CanCancelToDodge` | `0 / 1` | 当前动作允许闪避取消的时间范围 | 不允许闪避取消 |

这些曲线只控制动作衔接。当前阶段不读取 `AttackHitbox`，也不启用碰撞、伤害、生命或躯干值逻辑。
