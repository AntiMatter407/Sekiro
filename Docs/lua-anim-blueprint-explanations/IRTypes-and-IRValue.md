# IRTypes 与 IRValue

## 一、它们解决什么问题

Lua 的 table 没有固定结构，Lua 的字符串也无法表达自己最终应当成为 `FName`、普通字符串、软对象路径还是软类路径。Lua 动画蓝图在把 Graph 交给 C++ 以前，需要同时解决两个问题：

1. 告诉开发者和 IDE，完整 IR 中有哪些对象及字段；
2. 告诉 C++ 导入器，某个节点属性应当按照哪一种 UE/C++ 类型读取。

`IRTypes.lua` 解决第一个问题，`IRValue.lua` 解决第二个问题。

## 二、IRTypes 是静态结构说明

`IRTypes.lua` 集中声明整个 Lua AnimGraph IR 的 EmmyLua 类型，包括：

- `SekiroAnimBlueprintIR`：一份动画蓝图 IR 的根对象；
- `SekiroAnimIRLayer`：动画层；
- `SekiroAnimIRGraph`：Pose Graph、State Pose Graph 或 State Machine Graph；
- `SekiroAnimIRNode`：动画节点；
- `SekiroAnimIRPin` 与 `SekiroAnimIRLink`：Pin 和连接；
- `SekiroAnimIRStateMachine`、`SekiroAnimIRState`、`SekiroAnimIRTransition`：状态机拓扑；
- `SekiroAnimIRProperty` 与 `SekiroAnimIRValue`：节点属性及其类型化值；
- `SekiroAnimIRSourceLocation`：Lua 源码定位信息。

它的核心内容都是 `---@class`、`---@field` 和 `---@alias`。这些注解让 Rider 能够补全字段、检查明显的类型错误并跳转到定义。

文件最后虽然写了：

```lua
local IRTypes = {}
return IRTypes
```

但这个返回表没有运行时功能。它只是让文件保持合法 Lua 模块形式。Rider 会索引项目中的类型声明，因此其他文件可以直接在注解里使用 `SekiroAnimIRNode` 等类型。

换句话说，`IRTypes` 类似于 TypeScript 的接口声明文件或 C++ 数据结构头文件，但它不会创建 Graph、Node，也不会把数据发送给 C++。

## 三、IRValue 是运行时构造器

`IRValue.lua` 会在 Lua 编译动画蓝图时真实执行。它把普通 Lua 值包装成带有明确类型标签的 `SekiroAnimIRValue`。

例如：

```lua
IRValue.Bool(true)
```

生成：

```lua
{
    Type = "Bool",
    BoolValue = true,
}
```

而：

```lua
IRValue.SoftObjectPath("/Game/Characters/Sekiro/Anim/Idle.Idle")
```

生成：

```lua
{
    Type = "SoftObjectPath",
    SoftObjectPathValue = "/Game/Characters/Sekiro/Anim/Idle.Idle",
}
```

支持的显式类型是：

| IR 类型 | Lua 值 | C++ 目标语义 |
|---|---|---|
| `Bool` | `boolean` | `bool` |
| `Integer` | 整数 `number` | `int64` |
| `Float` | `number` | 浮点数 |
| `Name` | `string` | `FName` |
| `String` | `string` | `FString` |
| `SoftObjectPath` | `string` | `FSoftObjectPath` |
| `SoftClassPath` | `string` | `FSoftClassPath` |

各构造函数会先用 `assert` 检查 Lua 值的基础类型。`Integer()` 还会检查 `value % 1 == 0`，防止把小数写入整数属性。

## 四、为什么不能直接传普通 Lua 值

下面三个值在 Lua 中全都是字符串：

```lua
"Idle"
"/Game/Characters/Sekiro/Anim/Idle.Idle"
"/Script/Engine.AnimInstance"
```

但 C++ 需要分别将它们理解为普通文本、动画资产路径和类路径。只传字符串会迫使 C++ 根据字段名或字符串内容猜类型，容易产生歧义。

显式 IR Value 把类型和内容一起传递：

```lua
{ Type = "Name", NameValue = "Idle" }
{ Type = "SoftObjectPath", SoftObjectPathValue = "/Game/.../Idle.Idle" }
{ Type = "SoftClassPath", SoftClassPathValue = "/Script/Engine.AnimInstance" }
```

C++ 导入器先读取 `Type`，然后只读取该类型对应的值字段，不需要猜测。

## 五、业务代码为什么通常看不到 IRValue

业务 Graph 按照编辑动画蓝图的体感直接赋值：

```lua
local player = graph:SequencePlayer("IdlePlayer")
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true
player.PlayRate = 1.0
```

`LuaAnimNode` 的属性代理会查询 `NodeContracts` 中的属性契约，并在内部执行类似逻辑：

```lua
IRValue.From(registered_property.ValueType, value)
```

因此实际数据流是：

```text
player.Sequence = 动画路径
    -> NodeContracts 判断 Sequence 是 SoftObjectPath
    -> IRValue.From("SoftObjectPath", 动画路径)
    -> SekiroAnimIRProperty.Value
    -> C++ Lua Importer
    -> FSekiroAnimIRValue::SoftObjectPathValue
    -> NodeFactory 写入原生 SequencePlayer 节点
```

`IRValue` 属于编译器内部机制。普通动画蓝图 Lua 不应手工构造它，也不应直接拼装 `SekiroAnimIRProperty`。

## 六、两者与 IRSchema 的区别

| 文件 | 作用 | 是否真实执行编译逻辑 |
|---|---|---|
| `IRSchema.lua` | 校验名称和路径、生成稳定 ID、记录源码位置 | 是 |
| `IRTypes.lua` | 描述完整 IR table 的字段和类型，服务 IDE | 否 |
| `IRValue.lua` | 构造带明确类型标签的节点属性值 | 是 |

简单记忆：

- `IRSchema` 定规则；
- `IRTypes` 写说明书；
- `IRValue` 装箱具体属性值。

## 七、相关问答

### IRTypes 为什么不直接创建这些对象？

因为它只承担静态类型声明。Graph、Node、State 等对象由各自的 `LuaAnimGraph`、`LuaAnimNode`、`LuaAnimState` 类创建，职责不会混在类型说明文件中。

### `Integer` 为什么在注解里是 `number`？

Lua 的数值注解为兼容当前 Rider/EmmyLua 统一写成 `number`。`IRValue.Integer()` 在运行时额外检查它没有小数部分，C++ 端最终读取为 `int64`。

### 为什么一个 Value 有很多可选字段？

这是带类型标签的联合结构。`Type` 决定当前有效字段，例如 `Type = "Float"` 时只有 `FloatValue` 有意义，其余字段保持 `nil`。
