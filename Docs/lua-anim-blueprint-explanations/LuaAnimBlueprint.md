# LuaAnimBlueprint

## 一、LuaAnimBlueprint 是什么

`LuaAnimBlueprint` 是一份 Lua 动画蓝图源码的编译期基类。

它负责建立整份动画蓝图的顶层编译流程：

- 保存 IR 版本、Lua 模块名、父 AnimInstance 类和目标 Skeleton；
- 为每次编译创建干净实例；
- 管理动画层；
- 自动创建默认 `Main` Layer 和 `AnimGraph`；
- 调用子类实现的 `AnimGraph(graph)`；
- 展开状态机拓扑、State Pose Graph 和 Transition 规则；
- 汇总 Layer、Graph、Node、Link 和 StateMachine IR；
- 导出供 C++ `require` 的模块表；
- 把状态机运行时规则函数暴露给 UnLua 桥接。

它不是运行时 `UAnimInstance`，也不每帧执行 AnimGraph。它描述的是 C++ 应当生成哪一份 UE 动画蓝图资产。

## 二、标准子类写法

```lua
local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local MinimalLocomotion = require(
    "Animation.Examples.StateMachines.MinimalLocomotion")

---@class ABP_Minimal: LuaAnimBlueprint
local ABP_Minimal = LuaAnimBlueprint:Extend("ABP_Minimal", {
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
})

function ABP_Minimal:AnimGraph(graph)
    local locomotion = graph:StateMachine(
        "MainStateMachine",
        MinimalLocomotion)

    local inertialization = graph:Inertialization(
        "FinalInertialization")

    inertialization.Source:Connect(locomotion.Pose)
    graph.Result:Connect(inertialization.Pose)
end

return ABP_Minimal:Export()
```

业务动画蓝图只需要：

1. 继承 `LuaAnimBlueprint`；
2. 填写顶层配置；
3. 实现 `AnimGraph(graph)`；
4. 返回 `ABP_Class:Export()`。

## 三、四个不同对象

| 对象 | 示例 | 生命周期 | 作用 |
|---|---|---|---|
| 动画蓝图类 | `ABP_Minimal` | Lua 模块加载期间长期存在 | 保存配置和 override 方法 |
| 编译实例 | `class:New()` 的结果 | 单次 `CompileIR` | 保存本次 Layers、Graph 和运行时函数 |
| 导出模块 | `ABP_Minimal:Export()` | `require` 缓存期间存在 | 提供无参 `CompileIR` 和 Transition 函数 |
| 纯 IR | `CompileIR()` 返回值 | 交给 C++ 导入 | 只包含可序列化的动画蓝图描述 |

这四者不是同一个 table。

## 四、类默认配置

基类定义：

```lua
SchemaVersion = 2
SourceModule = ""
ParentAnimInstanceClass = "/Script/Engine.AnimInstance"
TargetSkeleton = ""
```

### SchemaVersion

Lua IR 协议版本。C++ Validator 只接受支持的版本，避免旧 Lua 数据被新编译器误读。

### SourceModule

当前动画蓝图的 `require` 模块名，例如：

```text
Animation.Examples.ABP_Minimal
```

它用于源码诊断和运行时 Transition 规则定位，必须与实际模块路径一致。

### ParentAnimInstanceClass

生成动画蓝图的父 `UAnimInstance` 类软路径，例如：

```text
/Script/Engine.AnimInstance
```

项目动画蓝图可以改为自己的 AnimInstance 派生类。它决定生成类可以使用哪些 C++ 属性和函数，最终合法性由 C++ 工厂与 Validator 检查。

### TargetSkeleton

生成普通 AnimBlueprint 时使用的 Skeleton 对象路径，例如：

```text
/Game/Characters/Sekiro/Mesh/SK_Sekiro_Skeleton.SK_Sekiro_Skeleton
```

它必须是 UE 对象路径，不能填写磁盘路径或 `.uasset` 文件名。

## 五、Initialize

每个编译实例初始化三份可变数据：

```lua
self.Layers = {}
self.LayerNames = {}
self.RuntimeFunctions = {}
```

- `Layers`：本次编译声明的动画层；
- `LayerNames`：检查 Layer 语义名称重复；
- `RuntimeFunctions`：本次状态机展开发现的 Transition Lua 函数。

同时捕获动画蓝图模块的源码位置。

这些表必须属于实例，不能放在类上共享，否则热重载或重复编译会残留旧 Graph、Layer 和函数。

当前 `LuaAnimBlueprintConfig` 没有实际配置字段，`Initialize(_config)` 也不读取传入配置。顶层配置来自动画蓝图子类的类字段。

## 六、AnimationLayer

`AnimationLayer(name)` 创建并登记一个 `LuaAnimLayer`：

```lua
local layer = self:AnimationLayer("Main")
```

它会：

1. 校验 Layer 名称；
2. 拒绝重复 Layer；
3. 创建 `LuaAnimLayer`；
4. 分配 Layer 声明顺序；
5. 写入 `Layers` 和 `LayerNames`。

`build_function` 参数是旧式即时回调入口。当前动画蓝图标准流程不传回调，而是由固定编译流程创建 Graph 后调用具名函数。

## 七、BuildDeclaredAnimGraph

这是默认主图构建流程：

```lua
local layer = self:AnimationLayer("Main")
local graph = layer:PoseGraph("AnimGraph")
self:AnimGraph(graph)
layer:SetRootGraph(graph)
```

它固定完成四件事：

1. 要求子类实现 `AnimGraph`；
2. 创建默认 `Main` Layer；
3. 创建默认 `AnimGraph` Pose Graph；
4. 把该 Graph 设置为 Layer 根 Graph。

业务子类只实现：

```lua
function ABP_Class:AnimGraph(graph)
    -- 声明节点、属性和连接
end
```

不需要自己处理 Layer、根 Graph 或 OutputPose。按照项目规范，业务类不得 override `BuildDeclaredAnimGraph()`。

## 八、ConfigureStateMachine

当 `graph:StateMachine(name, definition)` 创建状态机节点后，Blueprint 负责展开状态机定义。

### 独立状态机文件

传入 `definition` 时，约定函数名为：

```text
StateMachine(Machine)
StateGraph_Idle(Graph)
StateGraph_Move(Graph)
CanEnter_Idle_Move(Inst)
```

流程为：

1. 调用 `StateMachine(Machine)` 声明 Entry、State 和 Transition；
2. 遍历已经声明的 State；
3. 根据 State 名调用 `StateGraph_<State>`；
4. 遍历 Transition；
5. 查找 `CanEnter_<TransitionKey>`；
6. 按完整运行时函数名登记到 Blueprint。

### 主文件内联状态机

没有传入独立定义时，函数位于动画蓝图类自身，并增加状态机节点名前缀：

```text
StateMachine_GroundLocomotion(Machine)
StateGraph_GroundLocomotion_Idle(Graph)
CanEnter_GroundLocomotion_Idle_Move(Inst)
```

独立文件更适合大型状态机，主动画蓝图只保留外层 Graph 结构。

## 九、RegisterRuntimeFunction

Transition IR 保存完整规则名，例如：

```text
CanEnter_MainStateMachine_Idle_Move
```

独立状态机文件中的本地函数可能叫：

```text
CanEnter_Idle_Move
```

`ConfigureStateMachine()` 找到该函数后，使用完整规则名登记：

```lua
self.RuntimeFunctions[full_name] = runtime_function
```

如果两个状态机试图用同一个完整名字登记不同函数，会立即报错。

这里登记的是函数引用，不执行 Transition 规则。运行时桥接会把真实 AnimInstance 的 UnLua 代理作为显式 `Inst` 参数调用规则，并把结果发布给原生 Transition 查询路径。

## 十、CompileIR

单次编译过程是：

```lua
function LuaAnimBlueprint:CompileIR()
    local target_skeleton = RequireAssetObjectPath(...)
    self:BuildDeclaredAnimGraph()

    for _, layer in ipairs(self.Layers) do
        layers[#layers + 1] = layer:ToIR()
    end

    return {
        SchemaVersion = self.SchemaVersion,
        SourceModule = self.SourceModule,
        ParentAnimInstanceClass = self.ParentAnimInstanceClass,
        TargetSkeleton = target_skeleton,
        Layers = layers,
        SourceLocation = self.SourceLocation,
    }
end
```

它先检查 Skeleton 路径，再构建所有声明，最后逐层调用 `ToIR()`。

返回值只包含 C++ 数据协议，不包含 Lua 类、编译实例、状态机 definition 或 Graph 对象引用。

## 十一、Export 为什么不是直接返回类

动画蓝图文件最后写：

```lua
return ABP_Minimal:Export()
```

`Export()` 创建一个模块表：

```lua
local exported_module = setmetatable({}, {
    __index = class,
})
```

这个模块可以通过元表访问动画蓝图类的方法，但拥有自己的原始字段，用来保存：

- 无参 `CompileIR` 入口；
- 当前编译有效的完整 `CanEnter_*` 函数。

C++ `require` 模块后只需要调用：

```lua
module.CompileIR()
```

不需要理解 Lua 类系统，也不需要手工创建实例。

## 十二、Exported CompileIR 的完整流程

导出模块中的 `CompileIR()` 每次执行：

```text
清除上次导出的 Transition 函数
    -> class:New() 创建干净编译实例
    -> instance:CompileIR() 构建整份 IR
    -> 把 instance.RuntimeFunctions 复制到导出模块
    -> 返回 blueprint_ir
```

### 为什么清理旧 Transition 函数

假设热重载前存在：

```text
CanEnter_MainStateMachine_Idle_Move
```

修改状态机后该 Transition 被删除。如果不清理，模块表仍会保留旧函数，C++ 或运行时可能误以为它仍然有效。

`exported_runtime_function_names` 精确记录上次由编译过程加入的函数名，下次编译前只清除这些函数，不破坏类本身的方法。

### 为什么每次创建新实例

Graph 构建会向 `Layers`、`Nodes`、`Links` 和 `RuntimeFunctions` 添加数据。同一个实例重复编译会产生重复声明。

导出入口每次执行：

```lua
local instance = class:New()
```

从而保证重新编译和热重载不会继承上一次的可变编译状态。

## 十三、编辑器期与运行时边界

### 编辑器编译期

执行：

- `CompileIR()`；
- `BuildDeclaredAnimGraph()`；
- `AnimGraph(graph)`；
- StateMachine 和 StateGraph 声明；
- 所有 `ToIR()`。

这些函数用于生成动画蓝图资产，不参与每帧 Pose 求值。

### 游戏运行时

生成后的 AnimBlueprint 使用 UE 原生 AnimNode 和 `FPoseLink` 更新、混合和输出 Pose。

Lua 模块中只有导出的 `CanEnter_*` 规则函数进入运行时桥接。Lua 来源 AnimBlueprint 关闭多线程 Update，对应原生 Transition Rule Graph 被当前状态机检查时，在游戏线程按需直接求值该规则。

## 十四、完整调用链

```text
C++ CompileLuaModule("Animation.Examples.ABP_Minimal")
    -> require 模块
    -> 得到 ABP_Minimal:Export() 返回的模块表
    -> 调用 module.CompileIR()
        -> 清理旧运行时规则
        -> ABP_Minimal:New()
        -> instance:CompileIR()
            -> BuildDeclaredAnimGraph()
            -> Main Layer
            -> AnimGraph Pose Graph
            -> instance:AnimGraph(graph)
            -> 展开状态机和 StateGraph
            -> Layer:ToIR()
        -> 复制 RuntimeFunctions 到模块表
        -> 返回 SekiroAnimBlueprintIR
    -> C++ Lua Importer
    -> C++ Validator
    -> NodeFactory 生成 UE AnimBlueprint
```

## 十五、当前实现边界

- `Initialize(_config)` 当前忽略 config，顶层设置只能通过子类类字段提供；
- `BuildDeclaredAnimGraph()` 按规范不可 override，但 Lua 语言本身没有阻止子类覆盖；
- 直接对同一个编译实例调用两次 `instance:CompileIR()` 会重复创建 `Main` Layer，标准 `Export()` 入口通过每次创建新实例规避该问题；
- Lua 只预检 `TargetSkeleton` 的对象路径格式，父类是否存在、是否继承 AnimInstance、Skeleton 是否存在等由 C++ 检查；
- `RuntimeFunctions` 当前专门服务 Transition 规则，不承担运行时 Pose Graph Update/Evaluate；
- `AnimationLayer()` 能表达多个 Layer IR，但具体动画层接口生成、覆写和调用能力仍取决于 C++ Factory 当前支持范围；
- 导出模块会在编译时更新自身的 Transition 函数字段，因此同一 Lua 环境中的编译调用应由现有编辑器编译入口串行管理。

## 十六、相关问答

### 为什么业务类只需要 override AnimGraph？

Layer、根 Graph、OutputPose、状态子图展开和 IR 导出都有固定生命周期。业务类只描述节点和连接，可以让代码结构接近手动编辑 AnimGraph，同时避免每个动画蓝图重复编写构建样板。

### `AnimGraph(graph)` 会在游戏每帧调用吗？

不会。它只在编辑器生成或重新编译动画蓝图时执行。运行时执行的是生成后的原生 AnimNode。

### `Export()` 返回的是 ABP 类实例吗？

不是。它返回一个以 ABP 类为 `__index` 后备的模块表。模块表提供 C++ 需要的无参 `CompileIR()`，并保存运行时桥接需要的 Transition 函数。

### 为什么 Transition 函数要复制到主模块？

状态机可以拆分到独立文件，但生成的 Transition IR 使用主动画蓝图模块名和完整规则函数名进行定位。把函数复制到主导出模块后，运行时只需要一个稳定模块入口，不必理解状态机文件的拆分结构。
