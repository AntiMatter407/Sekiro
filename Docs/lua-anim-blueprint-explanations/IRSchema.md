# IRSchema 源码讲解

源码位置：`Content/Script/Animation/Compiler/IRSchema.lua`

## 一、IRSchema 是什么

IR 是 Intermediate Representation，即“中间表示”。Lua 动画蓝图代码不会直接创建 UE 节点，而是先生成一份与语言无关的 AnimGraph IR，再由 C++ NodeFactory 将 IR 转换成原生 `UAnimBlueprint`。

`IRSchema` 是 Lua 编译器前端的基础规则工具，负责：

1. 校验 Layer、Graph、Node、State 等语义名称；
2. 校验会成为 Lua 函数名一部分的标识符；
3. 校验 UE 顶层资产对象路径格式；
4. 生成可重复的层级稳定 ID；
5. 记录声明来自哪个 Lua 模块和源码行。

它不是完整 IR 数据结构，也不是最终 Validator。跨 Graph 引用、Pin 类型、连接数量、Pose 环等完整检查仍由 C++ Validator 完成。

## 二、为什么需要统一规则

Lua 文件中只写语义名称：

```lua
graph:StateMachine("GroundLocomotion", GroundLocomotion)
machine:State("Idle")
machine:Transition("Idle_Start", "Idle", "Start")
```

编译器需要把这些局部名称转换成全局唯一、重复编译不变化的身份：

```text
Layer/Main
Layer/Main/Graph/AnimGraph
Layer/Main/Graph/AnimGraph/Node/GroundLocomotion
```

如果每个类自行拼接名称，分隔符、空名称和函数命名规则很容易不一致。`IRSchema` 将这些规则集中到一个地方。

## 三、lua_reserved_words

```lua
local lua_reserved_words = {
    ["and"] = true,
    ["break"] = true,
    -- ...
}
```

这个集合记录 Lua 保留字，只用于检查未来会参与 Lua 函数名生成的名称。

例如 Transition Key 会生成：

```text
CanEnter_GroundLocomotion_Idle_Start
```

如果用户把 Key 写成不合法标识符，后续函数无法用普通 Lua 语法声明，因此应在 IR 生成阶段立即拒绝。

## 四、RequireSemanticName

```lua
function IRSchema.RequireSemanticName(name, kind)
    assert(type(name) == "string" and name ~= "", ...)
    assert(string.find(name, "/", 1, true) == nil, ...)
    return name
end
```

这是最宽松的名称检查，用于 Layer、Graph、Node 和 State。

它只保证：

- 值是非空字符串；
- 名称不包含 `/`。

`/` 被禁止，是因为稳定 ID 使用 `/` 表达父子层级。如果业务名称也含 `/`，ID 将无法可靠区分“名称内容”和“层级边界”。

`kind` 只用于生成更明确的错误信息：

```lua
IRSchema.RequireSemanticName(name, "State")
```

失败时会指出是 State 名称有问题。

## 五、RequireLuaIdentifier

```lua
function IRSchema.RequireLuaIdentifier(name, kind)
    local valid_name = IRSchema.RequireSemanticName(name, kind)
    assert(string.match(valid_name, "^[A-Za-z_][A-Za-z0-9_]*$") ~= nil, ...)
    assert(lua_reserved_words[valid_name] ~= true, ...)
    return valid_name
end
```

这是更严格的名称检查，用于：

- StateMachine Node 名；
- Transition Key；
- Transition Rule 函数名。

合法形式为：

```text
Idle
Idle_Start
GroundLocomotion2
_InternalState
```

不合法形式为：

```text
Idle-Start       包含减号
2Locomotion      数字开头
Idle Start       包含空格
function         Lua 保留字
```

它先调用 `RequireSemanticName()`，再补充 Lua 标识符和保留字检查。

## 六、RequireAssetObjectPath

```lua
function IRSchema.RequireAssetObjectPath(path, kind)
```

这个函数检查 UE 顶层资产对象路径格式，例如：

```text
/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton
/Engine/EngineMeshes/SkeletalCube_Anim.SkeletalCube_Anim
```

它拒绝：

```text
F:\ProjectAI\Sekiro\Content\...     磁盘路径
/Game/Characters/Sekiro/Animation     缺少 .对象名
/Game/Animation/Idle.uasset           uasset 文件路径
/Game/Asset.Asset:SubObject           子对象路径
```

正则：

```lua
"^(/.+)%.([^%./]+)$"
```

要求路径由以下两部分组成：

```text
/Package/Asset . ObjectName
```

这个函数只检查字符串格式，不加载资产，也不确认：

- 资产是否存在；
- 资产是不是 AnimSequence 或 Skeleton；
- 动画和 Skeleton 是否兼容。

这些需要 UObject 的检查由 C++ NodeFactory 预检完成。

## 七、MakeStableId

```lua
function IRSchema.MakeStableId(parent_id, category, name)
```

它在父 ID 后追加“实体类别”和“语义名称”：

```lua
IRSchema.MakeStableId("", "Layer", "Main")
-- Layer/Main

IRSchema.MakeStableId("Layer/Main", "Graph", "AnimGraph")
-- Layer/Main/Graph/AnimGraph

IRSchema.MakeStableId(
    "Layer/Main/Graph/AnimGraph",
    "Node",
    "GroundLocomotion")
-- Layer/Main/Graph/AnimGraph/Node/GroundLocomotion
```

稳定 ID 的“稳定”表示：只要父级、类别和语义名称不变，多次编译得到的字符串就相同。

它用于：

- IR 中的 Node、Graph、State 和 Transition 引用；
- C++ Validator 定位对象；
- NodeFactory 生成确定性的 GraphGuid 和 NodeGuid；
- 增量重建和错误诊断；
- 避免用 Lua table 地址或 UObject 临时指针表达所有权。

稳定 ID 不是 UE 资产路径，也不是运行时对象指针。

## 八、CaptureSourceLocation

```lua
function IRSchema.CaptureSourceLocation(module_name, stack_level)
```

它记录某个 Graph、Node、Link 或 State 是从哪一行 Lua 声明产生的。

```lua
local source_info = debug.getinfo(stack_level or 2, "l")
```

`debug.getinfo()` 从 Lua 调用栈读取行号。`stack_level` 表示向上查找几层调用者，因为真实业务调用与 `CaptureSourceLocation()` 中间通常隔着 Graph 或 Node 构造函数。

返回结构为：

```lua
{
    LuaModule = module_name or "",
    Line = line,
    Column = 0,
}
```

如果运行环境关闭了 `debug` 库，仍返回完整结构，只是 `Line = 0`。Lua 通常不能直接获得精确列号，因此当前固定为 `Column = 0`。

这些信息最终进入 C++ Diagnostic，使错误可以指向 Lua 模块和声明行，而不是只显示生成后的 UE 节点。

## 九、在编译流程中的位置

```text
业务 Lua
    -> Graph/Node/State 构造函数
    -> IRSchema 校验名称、路径并生成稳定 ID
    -> 导出 AnimGraph IR
    -> C++ Validator 做完整拓扑检查
    -> C++ NodeFactory 生成原生 AnimBlueprint
```

`IRSchema` 的失败发生在编辑器编译期，不会进入每帧动画运行时。

## 十、当前限制

- `RequireSemanticName()` 目前只检查空值和 `/`，不会主动限制其他特殊字符；
- Lua 标识符只允许 ASCII 字母、数字和下划线；
- `CaptureSourceLocation()` 依赖手工传入正确的调用栈层级；
- Column 固定为 0；
- 独立状态机文件当前主要沿用动画蓝图的 `SourceModule`，跨文件诊断模块名仍可进一步精确化；
- 资产存在性和类型检查不属于本模块。

## 十一、相关问答

### 问：为什么不能直接用 Node 名作为 ID？

不同 Graph 可以都有 `IdlePlayer`。层级稳定 ID 把 Layer、Graph、类别和名称一起编码，避免跨作用域重名。

### 问：为什么资产路径一定要写两遍资产名？

`/Game/Folder/Asset.Asset` 是 UE 顶层 UObject 的规范对象路径，点号前是 Package，点号后是 Package 中的对象名。IR 保存的是对象路径，不是磁盘文件路径。

### 问：IRSchema 会检查动画资源真的存在吗？

不会。Lua 层只检查路径形状；C++ NodeFactory 才加载并检查 UObject 类型、Skeleton 和兼容性。

