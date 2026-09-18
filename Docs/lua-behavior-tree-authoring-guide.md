# Lua 行为树编写手册

`LuaEditorExtensions` 的 `LuaBehaviorTree` 模块将 Lua 声明编译为 UE5.2 原生 `UBlackboardData` 与
`UBehaviorTree`。Lua 是唯一源码，生成资产是可重复生成的编译产物。

## 核心边界

- 插件不为具体 `UBTTaskNode`、`UBTCompositeNode`、`UBTDecorator`、
  `UBTService` 或 `UBlackboardKeyType` 编写 Adapter。
- Lua 只提供 `ClassPath + Properties`。C++ 通过 `UClass::IsChildOf` 判断角色，
  通过 `FProperty` 写入配置。
- 新增引擎、项目或第三方插件节点后，只要它继承正确的 UE 基类，且待配置成员以
  `UPROPERTY(EditAnywhere/EditDefaultsOnly/EditInstanceOnly)` 暴露，就不需要修改本插件。
- 行为树结构始终生成原生 UE 节点；普通节点运行时不解释声明文件，只有显式
  `LuaTask` 节点通过唯一的通用宿主调用对应 Lua Task 模块。
- 插件不依赖 `Source/Sekiro/`，不包含角色、Boss、招式或固定资产路径。

## 行为树编辑器工作流

打开标准 `UBehaviorTree` 资产后，行为树编辑器工具栏提供当前资产专属的 Lua 配置：

- `Lua Module`：填写相对 `Content/Script` 的模块名，例如
  `AI.Sekiro.BT_SKAICharacter`，不填写磁盘路径和 `.lua` 后缀。
- `检查 Lua`：只执行模块导入、IR 校验和反射属性预检，不修改当前资产。
- `从 Lua 生成`：检查通过后原地重建当前 BehaviorTree 和关联 Blackboard。

Lua 模块名保存在当前行为树资产的 package metadata 中，不是项目全局设置。生成时优先
复用当前树已经关联的 Blackboard；尚未关联时，按当前目录将 `BT_` 前缀替换为 `BB_`
推导目标包名。工具栏与 Python/蓝图入口调用同一套编译器，不存在两套生成逻辑。

## 最小示例

```lua
local LuaBehaviorTree = require("AI.Compiler.LuaBehaviorTree")

local Example = LuaBehaviorTree.New({
    SourceModule = "AI.Examples.BT_ReflectionExample",
})

function Example:DeclareBlackboard(blackboard)
    blackboard:Key(
        "/Script/AIModule.BlackboardKeyType_Bool",
        "bExampleEnabled",
        nil,
        false)
end

function Example:BehaviorTree(tree)
    local root = tree:Composite(
        "/Script/AIModule.BTComposite_Selector",
        "Root")

    root:Task(
        "/Script/AIModule.BTTask_Wait",
        "Wait",
        {
            WaitTime = LuaBehaviorTree.Value.Float(0.25),
            RandomDeviation = LuaBehaviorTree.Value.Float(0.0),
        })
end

return Example
```

完整可运行示例位于
`Content/Script/AI/Examples/BT_ReflectionExample.lua`。

## DSL

根节点：

```lua
tree:Composite(class_path, name, properties)
```

主节点：

```lua
parent:Composite(class_path, name, properties)
parent:Task(class_path, name, properties)
parent:LuaTask(name, lua_module_name, configuration)
```

挂载节点：

```lua
node:Decorator(class_path, name, properties)
node:Service(class_path, name, properties)
```

Blackboard：

```lua
blackboard:Key(class_path, name, properties, instance_synced)
```

具体角色始终由 C++ 继承关系决定。Lua 中没有节点类型注册表，也不能通过伪造角色绕过校验。

## C++ Task 与 UnLua Task

任意原生 C++ Task 继续通过 `Task(ClassPath, Name, Properties)` 声明。只要类型继承
`UBTTaskNode` 且属性可编辑，新增引擎、项目或第三方 Task 都不需要修改插件：

```lua
patrol:Task(
    "/Script/AIModule.BTTask_MoveTo",
    "MoveToPatrolLocation",
    {
        BlackboardKey = LuaBehaviorTree.Value.Struct({
            SelectedKeyName = LuaBehaviorTree.Value.Name("PatrolLocation"),
        }),
        AcceptableRadius = LuaBehaviorTree.Value.Float(100.0),
    })
```

需要运行 Lua 逻辑时使用唯一的通用宿主，不为每个 Lua Task 新建 C++ 类：

```lua
patrol:LuaTask(
    "PatrolWait",
    "AI.Tasks.LuaWait",
    "2.0")
```

`LuaTask` 等价于声明
`/Script/LuaBehaviorTree.LuaBehaviorTreeTask`，并反射写入：

- `LuaModuleName`：运行时 `require` 的模块名。
- `Configuration`：原样传给 Lua 的配置字符串，可自行约定 JSON 或简单标量。

Lua Task 模块返回普通 table。统一生命周期如下：

```lua
local Task = {}

function Task.Execute(task, controller, pawn, blackboard, configuration)
    return "InProgress"
end

function Task.Tick(
    task,
    controller,
    pawn,
    blackboard,
    configuration,
    delta_seconds)
    return "Succeeded"
end

function Task.Abort(task, controller, pawn, blackboard, configuration)
    return "Succeeded"
end

return Task
```

- `Execute` 必须返回 `Succeeded`、`Failed` 或 `InProgress`。
- `Tick` 只在 `InProgress` 后调用；返回空值或 `InProgress` 时继续运行。
- `Abort` 可返回 `InProgress`，随后由 Lua 调用 `task:FinishLuaTask(...)` 完成异步中止；
  普通清理直接返回 `Succeeded`。
- `task` 是节点运行时实例，可使用 `GetTaskAIController()`、`GetTaskPawn()`、
  `GetTaskBlackboard()`、`IsTaskActive()` 和 `FinishLuaTask()`。
- 模块 table 会被 Lua `require` 共享。逐实例状态应以 `task` 为键保存，推荐使用弱键 table。

完整示例位于 `Content/Script/AI/Tasks/LuaWait.lua`。

## 强类型属性

属性值必须使用 `LuaBehaviorTree.Value` 构造器。导入器不会把 Lua number 猜成
`int32` 或 `float`，也不会把普通 string 猜成 `FName`、枚举或对象路径。

| Lua 构造器 | 目标类型 |
|---|---|
| `Value.Bool` | `FBoolProperty` |
| `Value.Integer` | 整数 `FNumericProperty` |
| `Value.Float` | 浮点 `FNumericProperty` |
| `Value.String` | `FString` |
| `Value.Name` | `FName` |
| `Value.Text` | `FText` |
| `Value.Enum` | `FEnumProperty`、带 Enum 的 `FByteProperty` |
| `Value.Object` | 硬对象引用；编译时同步加载并检查类型 |
| `Value.SoftObject` | 软对象路径 |
| `Value.Class` | 硬类引用；编译时同步加载并检查 `MetaClass` |
| `Value.SoftClass` | 软类路径 |
| `Value.Struct` | 递归结构体字段 |
| `Value.Array` | 递归数组元素 |

Blackboard Selector 也是普通结构体属性：

```lua
BlackboardKey = LuaBehaviorTree.Value.Struct({
    SelectedKeyName = LuaBehaviorTree.Value.Name("TargetActor"),
})
```

`Set` 与 `Map` 在当前版本中会返回
`BT.Reflection.UnsupportedContainer`，不会静默跳过。架构使用扁平值池和子索引，
后续可以在不改变节点 IR 的情况下增加容器 writer。

## 编译入口

编辑器蓝图/Python/UnLua 均可调用
`USekiroBehaviorTreeFactoryLibrary`：

- `CheckLuaBehaviorTree(LuaModuleName, Diagnostics)`：只导入和校验。
- `GenerateFromLua(LuaModuleName, BlackboardPackagePath,
  BehaviorTreePackagePath, bSaveAssets, ...)`：生成并按需保存资产。
- `GenerateFromIR(...)`：供其他严格类型前端和自动化测试复用。

示例参数：

```text
LuaModuleName             = AI.Examples.BT_ReflectionExample
BlackboardPackagePath     = /Game/AI/Examples/BB_ReflectionExample
BehaviorTreePackagePath   = /Game/AI/Examples/BT_ReflectionExample
bSaveAssets               = true
```

包路径和所有类路径都由调用方传入。插件没有默认 `/Game` 目录或项目类名。

## 校验与失败语义

编译器会拒绝：

- 空或重复稳定 ID；
- 根节点不是 Composite；
- 主节点不是 Composite/Task；
- Decorator、Service 或 Blackboard KeyType 继承错误；
- 父节点不存在、父节点不是 Composite、父链形成环；
- 属性不存在、不可编辑、transient、deprecated 或 delegate；
- 强类型标签和 `FProperty` 不一致；
- 无效枚举成员、对象路径、类路径或值池索引；
- Schema 不接受的行为树 Graph 连接。

诊断包含稳定 `Code`、中文 `Message`、IR `Path` 和 Lua
`SourceLocation`。错误从不按 warning 静默降级。

生成前，插件会在 transient 对象上完成所有节点和 KeyType 属性预检，因此常见的
类路径或属性错误不会触碰旧资产。目标资产开始重建后若发生罕见的 Graph Schema
连接失败，资产会保持 dirty 且不会由本次调用保存；此时应修复诊断后重新生成。

## Graph 与运行时资产

插件动态加载 UE5.2 `BehaviorTreeEditor` 的 Graph shell。选择只由节点基类类别决定：

- Composite；
- Task；
- Decorator；
- Service；
- Simple Parallel 特殊 Composite shell；
- Run Behavior 特殊 Subtree Task shell。

真实节点对象始终是 Lua `ClassPath` 指向的运行时类实例。Graph 连接完成后调用
`UAIGraph::UpdateAsset()` 生成 `RootNode`、Children、Decorator 与 Service 拓扑，
所以生成资产可以在 Behavior Tree Editor 中打开，也可以由
`AAIController::RunBehaviorTree` 使用。

局部父链环由 IR 校验器拒绝。`UBTTask_RunBehavior` 的硬对象引用必须在编译时已存在；
预检会递归遍历已加载 Subtree 依赖，发现依赖链回到当前目标行为树时返回
`BT.Factory.SubtreeCycle`。

## 确定性

- 节点、Key 和挂载节点使用“角色 + 语义名”的稳定 ID。
- 同一作用域重复语义名在 Lua DSL 中立即报错。
- 属性名排序后进入 IR。
- 节点按 `DeclarationOrder`，再按稳定 ID 生成。
- `SourceLocation` 只参与诊断，不参与 ID 或排序。

因此相同 Lua 源码会得到相同节点顺序和连接拓扑。

## 当前限制

- 不支持 `Set`、`Map` 属性写入。
- 不把 Lua 回调转换为原生 C++ 行为树节点。
- 不自动启动 PIE，也不执行玩家输入或运行时场景测试。
- `DisplayName` 保存在 IR 中，但当前原生节点标题仍以 UE 节点自身描述为准。
