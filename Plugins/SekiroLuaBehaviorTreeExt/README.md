# Sekiro Lua Behavior Tree

该插件把 Lua 声明编译为原生 Unreal Engine Behavior Tree 和 Blackboard 资产。节点类型通过 UE
反射按 `ClassPath` 创建，属性通过反射写入，因此增加新的引擎类或其他插件类时不需要修改本插件，
也不需要为每个 C++ 节点编写 Lua 包装类。

## 编辑器工作流

打开一个 Behavior Tree 资产，在 Behavior Tree 模式工具栏中填写 `LuaModuleName`：

- **Check Lua**：加载并校验 Lua IR，不修改资产。
- **Generate**：原地重建当前 Behavior Tree，并保存生成结果。

`LuaModuleName` 和最终使用的 Blackboard 长包路径保存在 Behavior Tree 所属 Package 的
`UMetaData` 中。生成时按以下顺序选择 Blackboard：

1. 当前 Behavior Tree 已绑定的 `BlackboardAsset`；
2. 资产元数据中保存的 Blackboard 路径；
3. 同目录命名推导，例如 `BT_Guard` 推导为 `BB_Guard`。

蓝图和 Python 也可以调用 `USekiroBehaviorTreeFactoryLibrary` 的
`GetLuaAssetConfiguration`、`SetLuaAssetConfiguration`、
`CheckConfiguredBehaviorTree` 和 `GenerateConfiguredBehaviorTree`。

## 通用 Lua Task

`USekiroLuaBehaviorTreeTask` 是唯一的通用运行时 Lua Task。每个节点实例都可以设置不同的
`LuaModuleName` 和 `Configuration`。它不使用 UnLua 的类模块绑定，而是在每次生命周期回调中
显式 `require` 对应模块并调用模块表函数，因此不存在同一个 C++ 类只能绑定一个 Lua 模块的问题。

Lua 模块可以实现：

```lua
local Task = {}

function Task.Execute(task, controller, pawn, blackboard, configuration)
    return "InProgress"
end

function Task.Tick(task, controller, pawn, blackboard, configuration, deltaSeconds)
    return "InProgress"
end

function Task.Abort(task, controller, pawn, blackboard, configuration)
    return "Succeeded"
end

return Task
```

`Execute` 和 `Tick` 的返回值为 `"Succeeded"`、`"Failed"` 或 `"InProgress"`。返回
`"InProgress"` 后，Lua 可以调用节点实例的 `FinishLuaTask` 显式完成。`Abort` 返回
`"InProgress"` 时同样等待显式完成；其他合法结果均按同步中止处理。

运行时 Task 只传递通用的 Controller、Pawn、Blackboard 和配置数据，不包含项目玩法逻辑。
