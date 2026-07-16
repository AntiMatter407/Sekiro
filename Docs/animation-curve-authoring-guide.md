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
| `CanEnterLoop` | 已生成 | `int bool` | Start 与循环动画的历史匹配窗口 | 对应 Start/SprintStart Sequence | 当前无正式运行时消费者；Start 改为完整播放后进入 Cycle |
| `CanEnterStop` | 使用中 | `int bool` | 当前 Start/Loop 帧可进入匹配的 Stop | Start Sequence、BlendSpace 使用的 Loop 样本 Sequence | Lua GroundLocomotion 的 Start/Cycle 到 Stop |
| `CanEnterIdle` | 已生成 | `int bool` | Stop 与 Idle 的历史匹配窗口 | 对应 Stop/SprintStop Sequence | 当前无正式运行时消费者；Stop 改为完整播放后进入 Idle |
| `FootPlant` | 已生成 | `int enum` | 当前稳定触地脚 | 需要脚步分析的 Locomotion Sequence | 自动标注、调试和相位校验；GroundLocomotion 暂未直接消费 |
| `MovePhase` | 使用中 | `float` | 一个步态周期中的循环相位 | Start 与周期 Loop | GroundLocomotion 的 Start→Cycle 和 Cycle 方向资产切换相位匹配 |
| `FrameFlags` | 保留 | `int flags` | TAE 帧级行为标志 | 有对应 TAE 事件的动作动画 | 当前无正式运行时消费者 |
| `CancelActions` | 保留 | `int enum` | TAE 动作取消窗口类型 | 有对应 TAE 事件的动作动画 | 当前无正式运行时消费者 |
| `AttackHitbox` | 保留 | `int enum` | TAE 攻击框类型窗口 | 有对应 TAE 事件的攻击动画 | 当前武器碰撞走独立组件链路，不消费该曲线 |

基础 Locomotion 动画不得因为旧 TAE 管线存在，就批量写入 `FrameFlags`、`CancelActions` 或 `AttackHitbox`。没有可靠源数据、没有消费者的曲线不写入资产。

## 三、CanEnter 曲线

### 共同规则

历史生成的三条曲线都使用 `0/1` 阶梯值；当前运行时只消费 `CanEnterStop`：

- `0`：当前帧不允许进入目标状态。
- `1`：当前帧允许进入目标状态。
- 曲线只写入语义适用的动画，不给无关动画添加全程为 `0` 的空曲线。
- 输入、速度和角色状态决定“是否想切换”；`CanEnterStop` 决定进入 Stop 的自然脚步窗口。
- `Start -> Cycle` 与 `Stop -> Idle` 不再读取 CanEnter 曲线；它们按剩余秒数在尾段开始交叉混合，旧一次性动画在淡出期间继续推进到末帧。
- 阶梯曲线最后一次状态变化后必须在动画末尾重复同值关键帧。UE 会把没有后续区间的孤立末键视为无持续时间语义，压缩后可能保留前一段值。

### CanEnterLoop

含义：当前起步动作已完成关键重心转移，可以混入目标循环动画。

生成方式：

1. 对 Start 与目标 Loop 采样 Root 速度、Pelvis、双脚和关键骨骼姿势。
2. 跳过动作前段不可切区域。
3. 搜索与 Loop 姿势、速度和脚步相位最匹配的候选帧。
4. 从匹配帧开始写 `1`，直到动画结束。
5. Sprint Start 在 TAE 存在可靠移动进入窗口时，可优先使用原始事件帧；否则使用离线匹配结果。

当前没有正式状态切换消费者。曲线保留在已标注资产中用于历史审计和姿势匹配分析；Walk/Run/Sprint Start 统一完整播放后进入 Cycle。

### CanEnterStop

含义：当前 Start 或循环动画所处的脚步相位和姿势适合进入目标 Stop，不会因起始脚或身体姿势错位产生明显顿挫。

生成方式：

1. 读取 Start/Loop 的 `MovePhase`、`FootPlant` 和姿势采样结果。
2. 将目标 Stop 的起始姿势与源动画可取消区间比较。
3. 在匹配相位附近生成一个或多个允许窗口。
4. 窗口内写 `1`，窗口外写 `0`。

适用动画：Walk/Run/Sprint Start、被 Walk/Run BlendSpace 使用的循环样本，以及独立 Sprint Loop。Start 的多个触地窗口允许短输入在最近安全帧进入 Stop；Loop 曲线仍写在样本 `UAnimSequence` 上，不写在 BlendSpace 资产本身。

### CanEnterIdle

含义：当前停止动作已回到稳定站立姿势，可以进入 Idle。

生成方式：

1. 比较 Stop 后段与 Idle 的姿势差。
2. 同时检查 Root、Pelvis 速度已降低到稳定范围。
3. 从最佳匹配帧开始写 `1`，直到动画结束。

历史适用动画是 Walk/Run Stop、Sprint Stop，以及确实以 Idle 为目标的 TurnStop。当前没有正式状态切换消费者，Stop 统一完整播放后进入 Idle。

### MoveTransition 拆分记录

旧曲线 `MoveTransition` 把三个互斥值压在同一枚举中：

| 旧值 | 新曲线 |
| ---: | --- |
| `1` | `CanEnterLoop = 1` |
| `2` | `CanEnterStop = 1` |
| `3` | `CanEnterIdle = 1` |

迁移时保留原关键帧时间，将每种值展开成目标曲线的 `0/1` 阶梯关键帧，并删除连续重复值。只有实际出现过 `1` 的目标曲线才写入该动画；写入成功后删除旧 `MoveTransition`。

旧迁移数据保留在 `Script/temp/split_move_transition_curves.json` 供审计。当前生成器直接写独立曲线，不再先生成 `MoveTransition` 再拆分。截至 2026-07-12，当前 Locomotion 引用资产的生成结果为：`CanEnterLoop` 16 个、`CanEnterStop` 21 个、`CanEnterIdle` 9 个、`MovePhase` 18 个、`FootPlant` 9 个；其中 `CanEnterStop` 包含 12 个 Start/TurnStart 与 9 个 Loop。

蹲姿 Locomotion 使用 `005000~005603` 资源组，与站姿 Idle/Turn、Walk/Run Start/Loop/Stop 语义对应。`Script/generate_crouch_locomotion_curves.py` 从站姿规范 payload 读取关键帧，按源/目标动画时长转换归一化位置后写入蹲姿资源；第二组 Start（`005110~005113`、`005410~005413`）复用同方向第一组 Start 模板。当前结果覆盖 37 个资产，其中 Idle 和 4 个 Idle Turn 不写无消费者曲线，其余 32 个资产共写入 80 条曲线：`CanEnterLoop` 16 条、`CanEnterStop` 24 条、`CanEnterIdle` 8 条、`MovePhase` 24 条、`FootPlant` 8 条。

### 当前接入状态

截至 2026-07-12，曲线资产、Lua 曲线名和 GroundLocomotion 的主要移动过渡均已完成接入：

| 过渡 | 当前实现 | 目标实现 |
| --- | --- | --- |
| `Start -> Stop` | 输入释放后等待 `CanEnterStop >= 0.5` | `0.55` 秒异常等待上限 |
| `Start -> Cycle` | 剩余时间进入 `CycleBlendTime` 时捕获源 `MovePhase`、反查目标起播位置并交叉混合；Start 在淡出期间继续到末帧 | `2.10` 秒动画时间异常上限 |
| `Cycle -> Stop` | 停止意图确认后等待 `CanEnterStop >= 0.5` | `0.65` 秒异常等待上限 |
| `Stop -> Idle` | 剩余时间进入 `IdleBlendTime` 时开始 Pose 交叉混合；Stop 在淡出期间继续到末帧 | `1.50` 秒动画时间异常上限 |

这些上限都不是正常过渡条件。Start/Stop 只有在剩余时间查询或动画时间推进异常时才使用时间上限，并输出 `AnimationFallback` 调试日志；`CanEnterStop` 缺失或样本无法形成有效门控时仍使用 `CurveFallback`。

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

## 五、运行时读取

### Lua 接口

状态机基类提供三个读取入口：

```lua
local value = self:GetCurrentCurveValue(Curve.CanEnterStop, 0)
local enum_value = self:GetCurrentCurveIntValue(Curve.FootPlant, 0)
```

接口选择必须匹配语义：

- 连续值和可被 BlendSpace 加权的布尔门控使用 `GetCurrentCurveValue`。
- 单个 Sequence 上的离散枚举可使用 `GetCurrentCurveIntValue`。
- 真正的位掩码使用 `HasCurrentCurveFlag`。
- 当前运行时的 `CanEnterStop` 必须使用浮点接口并比较 `>= 0.5`。

状态机写法示例：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Start 进入 Cycle。
function GroundLocomotion.CanEnter_Start_Cycle(Inst)
    return Inst:WantsMove()
        and Inst:IsTailBlendReady(Tuning.CycleBlendTime)
end

---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Cycle 进入 Stop。
function GroundLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst:WantsStop()
        and Inst:GetCurrentCurveValue(Curve.CanEnterStop, 0) >= 0.5
end

---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Stop 进入 Idle。
function GroundLocomotion.CanEnter_Stop_Idle(Inst)
    return not Inst:WantsMove()
        and Inst:IsTailBlendReady(Tuning.IdleBlendTime)
end
```

曲线名集中声明在动画层 `Library.lua`，状态机不得散落裸字符串。

### Sequence 采样

C++ 根据当前 `UAnimSequenceBase`、当前播放时间和 Skeleton SmartName 查找浮点曲线并求值。动画、Skeleton 或曲线不存在时返回调用方给出的 fallback。

### BlendSpace 采样

浮点接口对 BlendSpace 的处理流程是：

1. 获取当前输入下所有有效样本及权重。
2. 将 BlendSpace 当前时间归一化后映射到每个样本自己的动画长度。
3. 分别采样各样本曲线。
4. 按样本权重计算加权平均值。

因此 `CanEnterStop >= 0.5` 表示当前主要混合结果已经进入允许停止的窗口。所有可能参与该 BlendSpace 区域的样本都应带有该曲线；缺失样本按 `0` 参与，会降低最终门控值。

整数接口对 BlendSpace 样本执行按位 OR，适合 `FrameFlags` 一类位掩码，不适合 `CanEnter*`。否则只要一个低权重样本为 `1` 就可能提前开放过渡。

`MovePhase` 不走上述普通浮点加权。插件将每个样本的相位映射到单位圆，对正弦/余弦分量按权重混合后再还原相位，因此 `0.98` 与 `0.02` 会得到接近 `0/1` 的结果，而不是错误的 `0.5`。

状态切换时，Lua 先从源动画读取圆周混合后的 `MovePhase`，再让插件在目标 Sequence/BlendSpace 上扫描同名曲线，选择相位误差最小的归一化位置。相同相位在目标动画中出现多次时，才使用源归一化时间作为次级排序。目标 Pose 通过 `NormalizedStartPosition` 从该位置开始，普通 Blend 只负责消除剩余姿势差，不再承担脚步同步职责。

## 六、缺失曲线处理

默认原则是失败关闭：曲线缺失时返回 `0`，不允许正常过渡路径静默放行。

允许的兜底只用于保证游戏不会永久卡住：

- 输出包含动画别名、资产路径、状态名和曲线名的错误日志。
- 达到明确的最大安全等待时间后才放行。
- 兜底触发必须能在调试界面或日志中识别。
- 不允许用新的 normalized time 常量悄悄替代缺失曲线。

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
- Sekiro 曲线名声明：`Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Library.lua`
- GroundLocomotion 消费逻辑：`Content/Script/Animation/Sekiro/GroundLocomotion.lua`
- Sequence/BlendSpace 采样实现：`Plugins/SekiroAnimBlueprintExt/Source/SekiroAnimBlueprintExt/Private/SekiroLuaAnimInstance.cpp`
- Lua 动画蓝图写法：[Lua 动画蓝图编写手册](lua-anim-blueprint-authoring-guide.md)
- Lua 编码规范：[Lua 代码规范](lua-code-style.md)
