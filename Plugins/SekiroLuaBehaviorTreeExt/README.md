# Sekiro Lua Behavior Tree

该插件把 Lua 声明编译为原生 Unreal Engine Behavior Tree 和 Blackboard 资产。节点类型通过 UE
反射按 `ClassPath` 创建，属性通过反射写入，因此增加新的引擎类或其他插件类时不需要修改本插件，
也不需要为每个 C++ 节点编写 Lua 包装类。

## 编辑器工作流

打开一个 Behavior Tree 资产，在 Behavior Tree 模式工具栏中填写 `LuaModuleName`：

- **Check Lua**：加载并校验 Lua IR，不修改资产。
- **Lua → BehaviorTree**：仅在点击时用 Lua 原地重建并保存 Behavior Tree 与 Blackboard。
- **BehaviorTree → Lua**：仅在点击并确认后，把当前编辑器 Graph 和 Blackboard 导出为 Lua。

Behavior Tree 编辑器资产始终是可独立编辑、编译和运行的权威资产。插件不会在打开、保存、
BehaviorTree 编译、项目编译或 PIE/SIE 时自动导入或导出，也没有注册这些生命周期 Hook。
`Source: BehaviorTree` / `Source: Lua` 只保存最近选择或成功同步的来源；切换 Source Mode
只写资产元数据并标记 Dirty，不执行同步、编译或文件操作。PIE/SIE 期间模块输入和三个操作按钮禁用。

`LuaModuleName` 和最终使用的 Blackboard 长包路径保存在 Behavior Tree 所属 Package 的
`UMetaData` 中。生成时按以下顺序选择 Blackboard：

1. 当前 Behavior Tree 已绑定的 `BlackboardAsset`；
2. 资产元数据中保存的 Blackboard 路径；
3. 同目录命名推导，例如 `BT_Guard` 推导为 `BB_Guard`。

蓝图和 Python 也可以调用 `USekiroBehaviorTreeFactoryLibrary` 的
`GetLuaAssetConfiguration`、`SetLuaAssetConfiguration`、
`CheckConfiguredBehaviorTree` 和 `GenerateConfiguredBehaviorTree`。

`USekiroBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua` 提供 Blueprint/Python 可调用的反向导出，
`bOverwrite=false` 时绝不覆盖旧文件。模块名严格映射到项目 `Content/Script/<module>.lua`，拒绝
绝对路径、`..`、空段和非法标识符；写入使用同目录临时文件替换，编码为 UTF-8 无 BOM、CRLF。
导出后会清除目标模块的 `package.loaded` 精确缓存并立即重新编译；拓扑、真实 ClassPath、属性或
Blackboard round-trip 不等价时恢复旧文件，避免留下损坏的 Lua。

反向属性导出只包含 `CPF_Edit` 且相对 CDO 变化的值，支持 Bool、Integer、Float、String、Name、
Text、Enum、Object、SoftObject、Class、SoftClass、Struct 和 Array。非默认 Set/Map 或其他无法无损
表示的属性会稳定失败，不会静默丢弃。编辑器 Shell 只提供拓扑，真实类路径始终来自 NodeInstance，
因此 RunBehavior、通用 LuaTask 和第三方 Composite/Task 不需要类型注册表。

## 自动布局

生成器根据 IR 父子拓扑自动排列主节点：同一深度位于同一水平层，兄弟子树按
`DeclarationOrder` 从左到右排列，父节点居中于直接子树的整体轮廓。子树宽度会计入
Decorator 和 Service 的视觉占用，因此多层、不均匀行为树也不会退化成一行节点或让兄弟子树重叠。
虚拟 Root 始终位于 IR 根节点正上方；相同 IR 重复生成会得到完全相同的节点坐标。

布局只依赖 IR 拓扑、声明顺序和附属节点数量。`RunBehavior` 等叶节点与任意反射创建的 Task
采用同一套规则，不存在具体节点类型的布局注册表。

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
