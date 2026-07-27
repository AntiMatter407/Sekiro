# Lua 行为树编译插件设计

## 目标

`SekiroLuaBehaviorTreeExt` 提供一条与具体节点类解耦的编辑器编译链：

```text
Lua DSL
  → 显式标签 Lua table
  → FSekiroBehaviorTreeIR
  → 结构/继承/环校验
  → transient 反射属性预检
  → BehaviorTreeEditor Graph
  → UBlackboardData + UBehaviorTree
```

新增任意合法 UE 行为树节点或 Blackboard KeyType 后，使用方只增加 Lua
`ClassPath` 与属性声明；插件无需新增类、Adapter、注册表或类型 switch。

## 模块边界

### SekiroLuaBehaviorTreeExt

Runtime 模块保持最小，仅提供模块生命周期。生成资产运行时只依赖 UE `AIModule`，
不依赖 Lua 执行桥。

### SekiroLuaBehaviorTreeExtEditor

`UncookedOnly` 模块负责：

- UnLua 模块导入；
- 强类型 IR 与诊断；
- 继承、拓扑和稳定 ID 校验；
- 通用 `FProperty` writer；
- Blackboard 与 BehaviorTree Graph 生成；
- 保存入口与 Automation Tests。

该模块只依赖引擎和 UnLua，不引用 `Source/Sekiro/`。

## IR

IR 主节点只保存：

- `Id`；
- `ParentId`；
- `ClassPath`；
- `DisplayName`；
- `Properties`；
- `DeclarationOrder`；
- `SourceLocation`。

Decorator、Service 使用相同结构，位于独立数组。角色不由 Lua 字符串决定，C++
加载 `UClass` 后使用 `IsChildOf` 判断。

属性值使用扁平池。每个值有显式 `ESekiroBehaviorTreeValueType`；Struct 字段和
Array 元素只保存子值索引。这样避免递归 USTRUCT，同时保留严格类型与扩展容器的空间。

## 反射安全

顶层属性必须具有 `CPF_Edit`，并拒绝：

- `CPF_Transient`；
- `CPF_Deprecated`；
- 单播或多播 Delegate；
- 不存在的属性。

Struct 内部字段同样拒绝 transient、deprecated 和 delegate。对象与类硬引用同步加载并
检查目标类型，软引用只保存路径。未知类型、Set 和 Map 返回明确诊断。

`FBlackboardKeySelector` 不需要专用 Adapter：Lua 使用 `Value.Struct` 写入
`SelectedKeyName`，Graph 更新阶段由 UE 原生逻辑解析 Key。

## Graph 生成

UE5.2 的 BehaviorTreeEditor 具体 Graph 类未完整导出 C++ API。插件因此按类路径动态
加载 Graph、Schema 和 Graph shell，再通过已导出的 `UEdGraph`、`UAIGraph`、
`UAIGraphNode` 虚接口完成创建、连接和 `UpdateAsset`。

这组类路径属于 UE5.2 编辑器基础设施，不是具体运行时节点注册表。真实
`NodeInstance` 始终按 IR ClassPath 直接创建。Simple Parallel 与 Run Behavior
分别选择 UE 要求的特殊编辑器 shell，但其任意派生类仍无需插件注册。

## 失败保护

流程先完成：

1. Lua 严格导入；
2. 结构与类继承校验；
3. 所有节点及 KeyType 的 transient 实例化；
4. 全部反射属性写入预检。

只有这些步骤全部成功后才查找或创建目标资产。生成失败时不会保存资产，并返回可定位诊断。
现有资产更新使用 `Modify()` 支持编辑器 Undo；调用方也可以先执行 Check。

## 测试

Automation Tests 覆盖：

- 重复 ID；
- 父链环；
- 非法节点继承；
- Float 反射属性成功；
- 显式错误类型被拒绝；
- 引擎 `UBTTask_Wait` 不经注册直接生成；
- Lua DSL 示例通过 UnLua 导入。

测试不启动 PIE。资产工厂测试只使用不保存的内存包。
