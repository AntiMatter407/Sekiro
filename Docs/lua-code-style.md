# Lua 代码规范

本文是项目 Lua 脚本的统一代码规范。以后新增或修改 `Content/Script/**/*.lua` 时必须遵守本文；历史 Lua 文件可以在业务改动时逐步补齐注释和结构，不要求只为格式化做大范围无意义改动。

本规范基于当前项目已有 Lua 代码整理，主要参考：

- `Content/Script/Animation/Compiler/*.lua`
- `Content/Script/Animation/Examples/*.lua`
- `Content/Script/Gameplay/**/*.lua`
- `Docs/lua-anim-blueprint-authoring-guide.md`

`Plugins/UnLua*`、`Plugins/UnLuaExtensions*` 下的 Lua 多为插件或第三方代码，不套用本项目风格，除非明确是在项目侧扩展。

## 一、核心原则

Lua 是脚本编排层，负责把 C++ 暴露的通用接口组合成玩法、输入、UI、动画状态机等具体工作流。

- C++ 提供稳定、通用、可复用的 `UFUNCTION` / 属性 / 组件接口。
- Lua 负责业务决策、状态切换、输入意图、UI 显隐、动画资源选择。
- Lua 不硬编码本机磁盘路径，不直接依赖编辑器临时状态。
- Lua 模块只暴露当前模块需要给 C++ / UnLua 调用的函数，其余逻辑保持 `local` 或类内方法。
- 每个新增业务模块必须有中文说明注释，复杂函数必须解释意图、输入来源和边界条件。

## 二、文件与编码

- 项目 Lua 放在 `Content/Script/` 下，模块名从该目录开始计算。
- 文件编码使用 UTF-8，无 BOM。
- 缩进统一 4 个空格，不使用 Tab。
- 行尾遵循项目 `.editorconfig`，保持 CRLF。
- 文件名使用模块或类名，例如 `SKInputManager.lua`、`GroundLocomotion.lua`、`Library.lua`。
- 临时 Lua 调试代码不要提交到业务模块；确实需要临时脚本时放到约定的临时目录，并在完成后清理。

模块路径示例：

```text
Content/Script/Gameplay/Sekiro/Input/SKInputManager.lua
=> require("Gameplay.Sekiro.Input.SKInputManager")
```

## 三、模块结构

每个 Lua 文件按以下顺序组织：

1. Lua 类型说明和模块职责注释。
2. `require` 依赖。
3. 本模块类表 / 数据表 / 常量表。
4. `local` helper 函数。
5. 类方法或模块方法。
6. C++ / UnLua 调用的 override 方法，例如 `Tick`、`OnMove`、`BeginPlay`。
7. `return`。

每个文件第一条有效注释必须明确所属类型，禁止省略：

```lua
-- Lua 类型：UnLua UObject 运行时类。self 是真实的 C++/蓝图实例，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua；运行时规则通过显式 Inst 访问真实 AnimInstance。
```

同一个文件只选择一条最符合实际职责的类型说明。不得把纯 Lua 编译对象写成 UObject，也不得为已有 UObject 的运行时类再创建代理实例。

UnLua UObject 运行时类推荐写法：

```lua
-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKInputManager，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- 管理玩家输入到角色意图的转换。
-- 本模块只编排 C++ 暴露的输入、移动和缓冲接口，不直接操作 Enhanced Input 资产。

---@class SKInputManager: USKInputManager
---@field Debug boolean 是否输出输入调试信息。
local SKInputManager = UnLua.Class()
SKInputManager.Debug = false

local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

function SKInputManager:Initialize(_initializer)
    self.SprintHoldTime = 0.0
end

function SKInputManager:Tick(delta_seconds)
    local delta = delta_seconds or 0
    self:SetDodgeHoldTime(self:GetDodgeHoldTime() + delta)
    return true
end

return SKInputManager
```

纯数据模块推荐写法：

```lua
-- GroundLocomotion 动画层的数据定义。
-- 状态、调参和曲线名集中放在这里，状态机逻辑文件只消费这些语义名。
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "GroundLocomotion"

M.State = {
    Idle = "Idle",
    Cycle = "Cycle",
    Stop = "Stop",
}

M.Tuning = {
    MovingSpeedThreshold = 3, -- 低于该速度认为角色静止，避免动画在微小速度下抖动。
}

M.Assets = AnimAssets.Locomotion

return M
```

## 四、命名规范

- 类表 / 模块表使用 `PascalCase`：`SKInputManager`、`LuaAnimNode`、`GroundLocomotion`。
- 项目 Gameplay Lua 类沿用 `SK` 前缀：`SKHUD`、`SKUIManager`、`SKCameraManager`。
- 动画蓝图入口使用 `ABP_角色名`：`ABP_Sekiro`。
- 局部变量和局部 helper 函数使用 `snake_case`：`read_context_index`、`move_x`、`delta_seconds`。
- 类方法使用 `PascalCase`，并用冒号声明：`function SKCameraManager:RefreshLockOnTarget(...)`。
- C++ / UnLua 调用方法直接使用 C++ 事件名：`Tick`、`OnMove`、`OnDodgeCompleted`、`BeginPlay`。
- 布尔字段沿用 UE 风格时允许 `b` 前缀：`bIsMoving`、`bHasSmoothedScreenPosition`。
- 枚举和配置表字段使用 `PascalCase`：`State.Idle`、`Tuning.RunSpeed`、`CameraMode.LockOn`。
- 动画资源名按语义分段，使用下划线连接：`Walk_Forward_Start`、`Sprint_Forward_Loop`。
- 禁止新增单字母下划线前缀变量，例如 `S_Value`、`R_Data`。

`local M = {}` 只用于纯数据模块或小型库模块。业务类模块必须使用清晰类名。

## 五、注释规范

Lua 新代码要求详细注释，但注释要解释“为什么”和“边界”，不要重复代码字面意思。项目中的每一个函数都必须具有可被 EmmyLua / LuaLS 识别的完整函数文档，不再只要求“复杂函数”补注释。

### 5.1 函数文档强制格式

每个命名函数、`local function`、元方法、赋值形式函数和匿名回调都必须满足以下要求：

- 函数前使用至少一行 `---` 中文说明，写清职责、调用时机、数据来源或边界条件。
- 每个显式参数必须各有一条 `---@param 参数名 类型 详细含义`，参数顺序必须和签名一致。
- 每个函数至少有一条 `---@return 类型 名称 详细含义`。
- 没有 Lua 返回值的函数统一写 `---@return nil`，明确调用方不得依赖返回结果。
- 多返回值函数为每个返回位置分别写一条 `---@return`，顺序必须和 `return` 一致。
- 可变参数使用 `---@param ... any 透传给目标函数的参数列表`，并在说明中写明最终消费者。
- 冒号方法隐含的 `self` 不写 `---@param self`；所属类已经由函数名和 `---@class` 表达。
- Transition 必须使用 `Rule.BoolProperty/CurveGreaterEqual/TimeRemainingLessEqual/All/Any/Not` 声明完整强类型原生规则，不再新增或导出运行时 `CanEnter_*` 函数。
- `_context`、`_delta_seconds` 等当前未使用参数也必须标注，说明它们为何保留以及由谁传入。
- 类型不确定时优先写准确联合类型，例如 `number|nil`、`table|string`；只有真正无法约束的跨语言值才使用 `any`。
- 注释块必须紧邻对应函数，中间不得插入空行或其他语句。

完整示例：

```lua
---处理移动输入，并把输入强度映射为角色移动档位。
---该函数由 C++ Enhanced Input 事件调用；返回 true 表示 Lua 已消费本次输入。
---@param _context userdata|nil UnLua 传入的调用上下文；当前逻辑直接使用实例字段，因此仅保留签名兼容。
---@param input_x number|nil 屏幕空间横向输入，范围通常为 -1..1，负值表示左移。
---@param input_y number|nil 屏幕空间纵向输入，范围通常为 -1..1，负值表示后退。
---@return boolean handled 是否已成功处理并提交移动意图。
function SKInputManager:OnMove(_context, input_x, input_y)
    -- ...
    return true
end
```

无返回值示例：

```lua
---清除本次动作缓存，防止一次性动画计划污染下一个状态。
---@return nil
function GroundLocomotion:ClearTransientMotionPlan()
    -- ...
end
```

匿名回调示例：

```lua
---按输入时间排序候选按键；时间相同时保持配置优先级稳定。
---@param left table 左侧候选输入记录。
---@param right table 右侧候选输入记录。
---@return boolean less_than 左侧记录是否应排在右侧之前。
table.sort(candidates, function(left, right)
    return left.Time < right.Time
end)
```

必须写注释的位置：

- 文件顶部：说明模块职责、和 C++ / 蓝图 / 其他 Lua 模块的边界。
- 所有函数：说明调用时机、输入来源、参数类型和返回值含义。
- 状态机转换：说明进入条件的设计意图，尤其是曲线门控、最短时间、取消窗口。
- 配置表和阈值：说明单位、来源或调参影响。
- C++ 桥接：说明调用的 C++ 接口承担什么职责，Lua 是否需要兜底。
- `pcall`、`nil` fallback、兼容旧接口等防御代码：说明为什么需要防御。

推荐使用 EmmyLua 注释描述函数签名：

```lua
---处理移动输入，并把输入强度直接映射成 C++ MovementTier。
---这里是 C++ / UnLua 调用入口，业务规则直接写在入口内，避免 OnMove 再转发到 HandleMove / ResolveMovementTier。
---@param input_x number|nil 屏幕空间横向输入，来自 Enhanced Input。
---@param input_y number|nil 屏幕空间纵向输入，来自 Enhanced Input。
---@return boolean handled 返回 true 表示 Lua 已处理本次输入事件。
function SKInputManager:OnMove(_context, input_x, input_y)
    local x = input_x or 0
    local y = input_y or 0
    local amount = math.min(math.sqrt(x * x + y * y), 1)

    if self:IsOwnerCrouched() then
        self:SetMovementTierByName("Crouch")
    elseif self:IsWalkHeld() or amount <= self.AnalogWalkEnterThreshold then
        self:SetMovementTierByName("Walk")
    else
        self:SetMovementTierByName("Run")
    end

    return true
end
```

配置项注释示例：

```lua
M.Tuning = {
    StartToCycleMinTime = 0.48, -- Start 至少播放到该秒数后才允许进入 Cycle，避免起步动画被过早截断。
    CurveGateMaxWaitTime = 0.20, -- 曲线缺失或未触发时的最大等待时间，防止状态机卡死。
}
```

无效注释示例：

```lua
-- 获取速度
local speed = self:GetSpeed()
```

这种注释只重复代码，不提供设计信息。更好的写法是：

```lua
-- 输入释放后的短暂残留速度不应立即触发 Stop，先交给曲线门控决定退出时机。
local speed = self:GetSpeed()
```

### 5.2 类型声明与 IDE 导航

Lua 类型注释必须让 Rider/LuaLS 能从参数、返回值、字段和父类跳转到对应声明，不能只把类型名称写在自然语言描述里。

- 每个类模块必须在类变量前声明 `---@class 子类: 父类`；类字段使用 `---@field` 描述类型和用途。
- 作为构造参数、设置项或稳定数据契约使用的表，必须声明具名 `---@class XxxConfig`、`---@class XxxSettings` 或业务数据类型。
- 函数参数和返回值必须直接使用已声明类型，例如 `---@param graph LuaAnimGraph`，禁止写成 `---@param graph table LuaAnimGraph 实例`。
- 回调参数必须写出具体函数签名，例如 `fun(graph: LuaAnimGraph):nil`，不得退化为 `fun(graph: table):nil`。
- 集合分别使用 `Type[]` 和 `table<KeyType, ValueType>`；只有真正无固定结构的动态表才允许使用裸 `table` 或 `table<string, any>`。
- 联合类型使用 `TypeA|TypeB|nil`；类型较长或会重复使用时通过 `---@alias` 起稳定名称。
- 从基类工厂、反射接口或 `setmetatable` 返回具体子类时，在局部变量前补 `---@type ConcreteType`，避免 IDE 将类型退化成基类。
- 注解类型名应全局唯一，并与模块、类或数据契约名称一致；不要为了规避类型冲突添加含义不明的缩写。
- 为兼容 Rider 的 EmmyLua 类型解析器，整数仍标注为 `number`，并在字段或参数说明中注明“整数”；项目 Lua 注解不得使用会被 Rider 报为 `Unresolved type` 的 `integer`。
- 静态 IR、配置结构等不需要运行时行为的类型可以集中放在专用类型文件中，但类型文件不得引入业务副作用。

正确示例：

```lua
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")

---创建一个循环播放的原生 Sequence Player。
---@param graph LuaAnimGraph 当前 Pose Graph。
---@param name string Graph 内的节点语义名。
---@param sequence string 动画序列资产软路径。
---@return LuaAnimNode node 由通用反射入口创建的节点。
local function create_looping_sequence(graph, name, sequence)
    return graph:Node(
        name,
        EditorNodeClass.SequencePlayer,
        {
            Sequence = sequence,
            bLoopAnimation = true,
            PlayRate = 1.0,
        },
        "SequencePlayer")
end
```

错误示例：

```lua
---@param graph table LuaAnimGraph 编译期实例。
---@param properties table 节点属性。
---@return table node LuaAnimNode 实例。
```

上面的 `LuaAnimGraph` 和 `LuaAnimNode` 只是描述文本，IDE 仍会把参数和返回值识别为普通 `table`，无法可靠补全或跳转。

## 六、格式细则

- 每个函数之间空一行。
- `require` 分组：基础库、项目基类、同域模块依次排列，中间可空一行。
- 表项多行书写并保留尾随逗号。
- 多行函数调用的参数每行一个语义块，右括号单独收尾或跟随最后一行，以现有上下文为准。
- 条件表达式跨行时，续行缩进 8 个空格，并把 `and` / `or` 放在行首。
- 简单卫语句允许早返回。复杂嵌套可以拆成真正复用的语义函数；禁止只为了转发入口而创建 `OnMove -> HandleMove -> Helper` 这类空壳链路。
- 字符串默认使用双引号。只有和已有外部示例或调试器代码保持一致时才使用单引号。

条件换行示例：

```lua
if self:IsDodgeHeld()
    and self:GetDodgeHoldTime() >= self:GetSprintHoldThreshold()
    and amount > 0.1
    and not self:IsOwnerFalling() then
    return "Sprint"
end
```

多行调用示例：

```lua
local created = self:CreateWidgetByPath(
    name,
    widget_class_path or "",
    layer_name or LayerName.HUD,
    z_order_offset or 0)
```

## 七、逻辑写法

- 优先使用 `local`，禁止无意创建全局变量。
- 读取 C++ / UObject 字段时，可能失败的地方使用 `pcall` 或基类封装函数。
- 判断 `nil` 时使用显式比较：`value == nil`、`value ~= nil`。
- 判断布尔语义时使用 `== true` / `~= true`，避免 `nil`、`false`、`0` 混淆。
- 对数值输入提供 fallback：`local delta = delta_seconds or 0`。
- 高频 `Tick` 中避免无意义创建大表；确实为了表达配置创建小表时要保持局部、短生命周期。
- Debug 输出走 `LogDebug` / `LogDebugFlag`，不要在正式逻辑里散落裸 `print`。
- 返回值要和 C++ 调用方约定一致：事件函数通常返回 `true` 表示 Lua 已处理，创建失败或无效输入才返回 `false`。

## 八、UnLua 与 C++ 桥接

已有 C++ 或蓝图实例的 Gameplay 类统一返回 `UnLua.Class()`。UnLua 将 Lua 类表绑定到真实 UObject，因此方法内的 `self` 就是该 UObject，不再创建 `ContextObject`、代理表或弱引用包装实例。

- 模块直接 `return ClassName`，不写导出函数列表，也不调用 `:Export()`。
- `self.Property = Value` 直接写入同名 `UPROPERTY`；`self:Function(...)` 直接调用同名 `UFUNCTION`。字段不存在于反射系统时，赋值才作为该 UObject 对应 Lua 实例的私有状态。
- `Initialize(_initializer)` 可能在 UObject 仍处于 `PendingConstruction` 时执行。这里仅初始化 Lua 私有字段，不得覆盖 `UPROPERTY`，也不得调用依赖完整对象状态的 `UFUNCTION`。
- C++ 或蓝图默认值的 Lua 覆盖统一写在 `ReceiveBeginPlay()`；Actor/HUD 使用引擎实际派发的 `BeginPlay()`。例如 `self.MaxLockOnRange = 4000.0` 应在 `ReceiveBeginPlay()` 中执行，避免随后被 C++ 成员初始化或蓝图模板复制覆盖。
- 不要在 Lua 类表上声明与 `UPROPERTY` 同名的字段，否则类字段可能遮蔽反射属性。
- C++ 手动调用模块函数时已经把 UObject 作为第一个 Lua 参数传入，冒号方法会自动把它接收为 `self`。Lua 签名只声明业务参数，例如 C++ `Call(this, DeltaTime)` 对应 `Tick(delta_seconds)`，不得再增加 `_context`。
- 不写旧前缀方法。输入、摄像机、UI 等事件逻辑应直接写在 `Tick`、`OnMove`、`OnDodgeCompleted`、`BeginPlay` 这类方法里。
- 禁止为了“适配签名”额外套一层 `OnMove -> HandleMove -> Helper` 的业务转发链。只有确实被多个入口复用、并且名字能表达独立语义的代码，才允许抽成 `local` 函数或类方法。
- 不提供 `GetContextObject()`、`CallCpp()` 一类转发接口。类内直接使用真实 `self`；确需动态函数名时应优先改成有明确语义的 C++ 接口。
- 只有动画编译器、IR、数据表和工具库继续使用纯 Lua 类系统；它们的 `self` 不是 UObject。

override 示例：

```lua
function SKCameraManager:Tick(delta_seconds)
    self:RefreshCachedCameraComponents()

    if self:IsLockOnEnabled() then
        self:UpdateLockOnCamera(delta_seconds or 0)
    end

    return true
end
```

错误示例：

```lua
function SKCameraManager:Tick(_context, delta_seconds)
    return self:UpdateCamera(delta_seconds)
end
```

上面的写法只是在函数之间转发，业务入口被拆散到空壳方法里，后续调试时需要顺着调用链跳转，不符合项目 Lua 风格。

## 九、动画 Lua 约定

动画 Lua 继续遵守 `Docs/lua-anim-blueprint-authoring-guide.md`，并补充以下风格要求：

- `ABP_xxx.lua` 继承 `Animation.Compiler.LuaAnimBlueprint`；主图 override `AnimGraph(graph)`，Animation Layer 统一在 `DeclareAnimationLayers()` 中通过 `self:AnimLayer()` 声明。禁止业务类 override `BuildDeclaredAnimGraph()` 或重新创建默认 Main Layer。
- Animation Layer Interface 同样继承 `LuaAnimBlueprint`，但必须设置 `BlueprintKind = "AnimationLayerInterface"`，且不得声明主 `AnimGraph` 或 `TargetSkeleton`。
- 实现接口的 AnimBlueprint 必须在 `ImplementedInterfaces` 中登记 GeneratedClass 软路径；接口实现和父 Layer 覆写都显式设置 `bOverride = true`。
- 子 AnimBlueprint 只声明需要覆写的 Layer；未覆写的父 Layer 交给 UE 原生继承，不在 Lua 中复制。
- `LinkedAnimLayer`、`LinkedAnimGraph` 和 Layer 函数参数使用结构型专用入口；业务脚本不得手写动态 Pin、`LinkedInputPose` 或底层 IR。
- 普通 `ABP_xxx.lua` 必须在类定义中显式填写 `TargetSkeleton = "/Package/Asset.Asset"`；禁止从动画资源、父类或磁盘路径隐式推导。
- `AnimAssets.lua` 只放语义资源名与 UE 对象路径，不写状态逻辑。
- 编译器通用类放在 `Animation/Compiler/`，其中禁止出现项目角色名、状态名和资源路径。
- 普通原生节点统一使用 `graph:Node(Name, EditorNodeClass, Properties, NodeType)` 创建；业务脚本不得为每种 UE 节点新增一个 Lua 子类或专用构造函数。
- 原生类路径统一登记在 `Animation.Compiler.NodeClasses.EditorNodeClass`；业务脚本使用 `EditorNodeClass.SequencePlayer` 这类语义引用，禁止散落 `"/Script/..."` 字符串。
- 节点初始属性集中写在 `Properties` 表中。业务代码禁止调用 `SetProperty()`、构造 `IRValue` 或直接修改内部属性表。
- 已有特殊生成逻辑或强类型 Pin 契约的节点必须传入稳定 `NodeType`，例如 `SequencePlayer`、`BlendListByEnum`、`SaveCachedPose`；新增普通反射节点可以省略，最终由 UE Schema 校验 Pin 和连接。
- `graph:StateMachine()` 与 `graph:Property()` 是结构型专用入口，继续负责内部 Graph 所有权和 AnimInstance 变量类型解析。
- Pin 连接统一写成 `目标输入Pin:Connect(来源输出Pin)`，例如 `graph.Result:Connect(player.Pose)`；业务代码禁止手写节点名和 Pin 名字符串调用底层 `Link()`。
- StateMachine 必须由 `PoseGraph:StateMachine()` 创建为 `LuaAnimNode` 子类，并通过 `OwnedGraphId` 持有内部 StateMachine Graph；禁止把内部状态机 Graph 直接作为 Layer 根。
- 大型状态机继承 `Animation.Compiler.LuaAnimStateMachine` 并拆分到独立文件；主图通过 `graph:StateMachine("Name", Definition)` 引用。
- 状态机拓扑和状态图回调统一使用点号声明：`StateMachine(Machine)`、`StateGraph_<State>(Graph)`；编译器只传入显式 `Machine` 或 `Graph`，禁止冒号声明和隐式 `self`。
- Transition Rule 统一在 `Machine:Transition(..., { Rule = ... })` 中声明强类型 AST，由插件生成原生属性、曲线、时间和布尔组合节点。
- 很小的内联状态机使用 `StateMachine_<Machine>` 和 `StateGraph_<Machine>_<State>` 约定。
- State 使用 `machine:State(Name)` 声明；每个 State 必须有独立 `StateGraph_*`，禁止多个 State 共享一个 StatePose Graph。
- Transition 使用 `machine:Transition(Key, From, To, { Rule = ... })` 创建对象，并直接配置 `BlendDuration`、`PriorityOrder` 和 `BlendMode`。
- 主 AnimGraph 使用 `Pose + OutputPose`，State 独占 Graph 使用 `StatePose + StateResult`；根节点和 Layer 根由基类自动管理。
- 稳定 ID、Link 和 SourceLocation 由基类生成，业务模块不得自己拼接 ID。
- 已注册 NodeType 的 Pin、Property、允许放置的 GraphType 和内部 Graph 所有权以 C++ 注册表为权威；`NodeContracts.lua` 只做 Lua 前端镜像和快速报错。未注册的普通节点由 UE 反射与 Graph Schema 校验。
- 业务 Lua 禁止调用或新增 `AddPin` 一类任意 Pin 接口，也不得直接修改节点的 `Pins`、`Properties` 或注册契约表；Pin 断言由 `LuaAnimNode` 自动复制。
- `SaveCachedPose` 和引用它的 `UseCachedPose` 必须位于同一个主 Pose Graph。Lua 不保存运行时 Pose。
- Lua 编译期类不实现 `Initialize_AnyThread`、`Update_AnyThread`、`Evaluate_AnyThread`，这些生命周期属于 C++ 原生 AnimNode。
- 动画 Lua 不直接创建 UObject、不修改动画蓝图资产、不发布运行时 Pose；资产生成统一由后续编辑器 NodeFactory 完成。
- 新增、重命名、删除或改变任何语义动画曲线时，必须同步更新 `Docs/animation-curve-authoring-guide.md` 的登记表、生成来源、消费者和验证方法。

动画蓝图源码建议按以下顺序组织：

1. `require` 编译器基类与 `AnimAssets`。
2. 动画蓝图类和 Skeleton/ParentClass 配置。
3. `AnimGraph(graph)` 主图函数。
4. 可选的 `DeclareAnimationLayers()` 接口实现或子类 Layer 覆写。
5. 点号声明的内联状态机 `StateMachine_*(Machine)` 和 `StateGraph_*(Graph)`；大型状态机移到独立文件。
6. `return ABP_Class:Export()`。

Transition 声明必须写明影响过渡质量的设置：

```lua
local start_to_cycle = machine:Transition(
    "Start_Cycle",
    "Start",
    "Cycle",
    {
        Rule = Rule.BoolProperty("bHasMovementInput", true),
    })
start_to_cycle.BlendDuration = 0.12
start_to_cycle.PriorityOrder = 0
start_to_cycle.BlendMode = "Linear"
```

## 十、UI / 输入 / Movement / 摄像机 Lua 约定

- 存在按键先后顺序歧义的组合输入统一使用 `Gameplay.Base.InputChordResolver`，具体规则见 [Lua 组合输入编写手册](input-chord-authoring-guide.md)。
- Dodge、Step、Sprint 这类“按下边沿触发动作、Held 升级状态、方向持续驱动 Movement”的输入不属于组合键，不得加入人为时间窗口。
- UI 层名、ZOrder、颜色、距离缩放等配置集中在文件顶部的表里。
- 输入事件只转换为“意图”和“缓冲”，不要在输入模块里直接写复杂战斗结算。
- `Source/Sekiro` 中凡是可以安全脚本化的项目策略，优先迁入 Lua；C++ 只保留 UE 生命周期承载、物理/碰撞/复制、线程边界和语义化桥接接口。
- Movement Lua 负责速度档位映射、ActorYaw 所有权、自由/冲刺/锁定朝向选择和转向前方向快照；`UCharacterMovementComponent` 继续负责物理、碰撞、Root Motion 应用和网络预测。
- 摄像机模块只处理视角模式、ControllerYaw、视角输入和锁定目标，不直接写 ActorYaw 或角色移动规则。
- 同一帧固定为 `Input Lua -> Movement Lua -> Camera Lua -> AnimInstance/Lua AnimBP`；动画需要转身前角度时读取 Movement 发布的快照，不得在角色旋转后重新推导。
- UI、输入、摄像机调用 C++ 的函数应保持语义化，例如 `SetMovementTierByName`、`ApplyControllerYawForScript`、`SetLockOnIndicatorVisible`。
- 每个跨系统调用点要通过函数名或注释说明影响范围，避免 Lua 文件之间隐式耦合。

## 十一、提交前检查

修改 Lua 后至少检查：

- 文件在 `Content/Script/` 下，模块名和 `require` 路径一致。
- 文件首条有效注释已经声明 Lua 类型，并且与实际 UObject/纯 Lua/动画编译职责一致。
- 没有新增全局变量。
- 每个函数和匿名回调都有中文职责说明、完整 `---@param` 和至少一条 `---@return`；无返回值函数显式标注 `---@return nil`。
- 类、配置和稳定数据表具有可跳转的 `---@class` / `---@alias` 声明，函数签名不使用“`table` + 描述中的类型名”代替真实类型。
- 运行 `python Script/check_lua_function_docs.py`，确保函数文档覆盖率为 100%。
- C++ 调用的事件方法使用原名 override，参数中没有额外 `_context`，文件中没有包装实例、导出函数列表或旧前缀方法。
- 动画状态名、资源名、曲线名来自集中数据表。
- 新增或修改的语义动画曲线已经登记到 `Docs/animation-curve-authoring-guide.md`。
- Debug `print` 已移入统一日志工具并受模块调试开关控制。
- 触碰动画 Lua 时，同时确认 `Docs/lua-anim-blueprint-authoring-guide.md` 的约定没有被破坏。
