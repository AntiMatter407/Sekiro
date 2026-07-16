# CompilerClass 源码讲解

源码位置：`Content/Script/Animation/Compiler/CompilerClass.lua`

## 一、职责

`CompilerClass` 是 Lua 动画蓝图编译器内部使用的轻量类系统，只负责两件事：

1. `Extend()` 创建子类；
2. `New()` 创建类实例。

它不是 UE 的 `UClass`，不会创建 UObject，也不负责 UnLua 运行时绑定。它只让编译期代码可以用继承、override 和构造函数的形式组织。

最终形成的部分继承关系如下：

```text
CompilerClass
├── LuaAnimBlueprint
├── LuaAnimGraph
├── LuaAnimPin
├── LuaAnimNode
│   ├── LuaSequencePlayerNode
│   ├── LuaStateMachineNode
│   ├── LuaInertializationNode
│   ├── LuaSaveCachedPoseNode
│   └── LuaUseCachedPoseNode
└── LuaAnimStateMachine
```

## 二、根类 table

```lua
local CompilerClass = {}

CompilerClass.__index = CompilerClass
CompilerClass.ClassName = "CompilerClass"
```

Lua 没有原生 `class` 关键字，因此这里使用普通 table 表示类。

`CompilerClass.__index = CompilerClass` 表示实例自身找不到字段或方法时，继续到 `CompilerClass` 中查找。

`ClassName` 只用于诊断和调试，不是 Unreal 的反射类名。

## 三、merge_definition

```lua
local function merge_definition(target, definition)
    for key, value in pairs(definition or {}) do
        target[key] = value
    end

    return target
end
```

这个函数把子类声明中的默认字段和方法复制到新类。

例如：

```lua
local ABP_Minimal = LuaAnimBlueprint:Extend("ABP_Minimal", {
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
})
```

第二个参数中的三个字段会成为 `ABP_Minimal` 类字段。

这里执行的是浅复制。table 类型的可变实例数据不能直接放在类定义中共享，因此 `Layers`、`Nodes` 等数组必须在各自的 `Initialize()` 中重新创建。

## 四、Extend 创建子类

```lua
function CompilerClass:Extend(class_name, definition)
    local child = {
        ClassName = class_name or "CompilerClass",
        Super = self,
    }
```

当代码执行：

```lua
local LuaAnimBlueprint = CompilerClass:Extend("LuaAnimBlueprint")
```

此时：

```text
self  = CompilerClass
child = 新创建的 LuaAnimBlueprint 类 table
```

`Super = self` 记录直接父类，但不会自动调用父类构造函数。

旧实现曾在 `child` 中先写入 `__index = false`，随后立刻被 `child.__index = child` 覆盖。该占位值没有被读取，现已删除。

### 4.1 实例到类的查找

```lua
child.__index = child
```

以后以 `child` 为元表创建实例时，实例找不到成员就会到 `child` 中查找。

### 4.2 子类到父类的查找

```lua
setmetatable(child, { __index = self })
```

子类找不到成员时，会继续到父类查找。完整的方法查找链类似：

```text
SequencePlayer 实例
    -> LuaSequencePlayerNode
    -> LuaAnimNode
    -> CompilerClass
```

因此 `LuaSequencePlayerNode` 即使没有重新实现 `SetProperty()`，也可以使用 `LuaAnimNode:SetProperty()`。

## 五、为什么复制 __newindex

```lua
local inherited_new_index = rawget(self, "__newindex")
if inherited_new_index ~= nil then
    child.__newindex = inherited_new_index
end
```

`LuaAnimNode` 使用 `__newindex` 拦截节点属性赋值：

```lua
player.Sequence = AnimAssets.Idle
player.PlayRate = 1.0
```

拦截后，这些普通 Lua 值会被转换成有明确类型的 IR Property。

Lua 查找 `__newindex` 等元方法时，不会像普通成员那样沿父类的 `__index` 链继续查找。因此 `Extend()` 必须把父类自身的 `__newindex` 复制到子类。

如果没有这段代码，`LuaSequencePlayerNode` 实例就无法继承 `LuaAnimNode` 的直接属性赋值规则。

`rawget(self, "__newindex")` 只读取当前父类 table 自身的字段，不触发其他元表查找，避免复制到来源不明确的值。

## 六、合并子类声明

```lua
return merge_definition(child, definition)
```

继承关系建立后，再把调用者提供的字段和 override 方法写入子类。

如果 `definition` 中存在和父类同名的方法，实例查找时会先找到子类版本，从而形成 Lua 的 override 效果。

## 七、New 创建实例

```lua
function CompilerClass:New(config)
    local instance = setmetatable({}, self)
```

例如：

```lua
local player = LuaSequencePlayerNode:New({
    Graph = graph,
    Name = "IdlePlayer",
})
```

此时：

```text
self     = LuaSequencePlayerNode 类
instance = 新的空 table
```

把 `self` 设为实例元表后，实例便可以沿类继承链查找方法。

### 7.1 调用 Initialize

```lua
if type(instance.Initialize) == "function" then
    instance:Initialize(config or {})
end
```

`Initialize()` 相当于这个轻量类系统的构造函数。

它通过继承链查找，因此子类可以实现自己的 `Initialize()`。父类构造不会自动执行，子类需要明确调用：

```lua
function LuaSequencePlayerNode:Initialize(config)
    config.NodeType = "SequencePlayer"
    LuaAnimNode.Initialize(self, config)
end
```

这里直接调用 `LuaAnimNode.Initialize(self, config)`，相当于显式执行父类构造。

`config` 现在只承担构造参数职责。`New()` 不会把任意字段隐式复制到实例；每个具体类必须在 `Initialize(config)` 中校验并显式保存自己拥有的字段。

例如 `LuaAnimNode.Initialize()` 会通过 `IRSchema.RequireSemanticName(config.Name)` 校验名称，再写入 `self.Name`。这样从源码可以直接看出实例最终拥有哪些字段，也不会出现同一参数先隐式写入、再校验写回的重复过程。

## 八、完整对象关系

以下代码：

```lua
local LuaAnimNode = CompilerClass:Extend("LuaAnimNode")
local LuaSequencePlayerNode = LuaAnimNode:Extend("LuaSequencePlayerNode")
local player = LuaSequencePlayerNode:New({
    Graph = graph,
    Name = "IdlePlayer",
})
```

最终关系为：

```text
player 实例
  元表 = LuaSequencePlayerNode

LuaSequencePlayerNode
  __index = LuaSequencePlayerNode
  父类 = LuaAnimNode

LuaAnimNode
  __index = LuaAnimNode
  父类 = CompilerClass
```

## 九、能力边界

`CompilerClass` 支持：

- 类继承；
- 方法 override；
- 独立实例；
- `Initialize()` 构造；
- 父类方法查找；
- 特殊 `__newindex` 行为继承。

它不支持：

- 自动父类构造；
- C++ 或 UObject 继承；
- UE 反射；
- 访问修饰符；
- 编译期强类型检查；
- 自动内存或生命周期管理。

一句话概括：`CompilerClass` 是为 Lua 动画蓝图编译器定制的最小类系统，不是对 Unreal 类系统的重新实现。

## 十、相关问答

### 问：`CompilerClass` 是不是 UnLua 类？

不是。它不绑定 UObject，也不依赖 UnLua 运行时。UnLua 只是后续负责加载 Lua 模块和调用运行时 Transition 函数。

### 问：`Super` 会不会自动执行父类函数？

不会。`Super` 只记录父类。需要执行父类构造时，子类会明确调用 `LuaAnimNode.Initialize(self, config)`。

### 问：为什么不用一个成熟的 Lua class 库？

编译器只需要继承、实例化和一个可控的属性拦截链。当前实现很小，没有业务依赖，也可以在编辑器、Commandlet 和独立 Lua 测试中保持一致。

### 问：这个类会参与每帧动画更新吗？

不会。它只在 Lua 动画蓝图编译成 IR 时使用。生成的 AnimBlueprint 在运行时由 UE 原生 AnimNode 更新和求值。

### 问：`child` 创建时写 `__index = false` 有什么意义？

没有实际意义，因为它会马上被 `child.__index = child` 覆盖，期间也没有被读取。该占位字段已经删除。

### 问：为什么 `config` 先复制到实例，又传给 `Initialize()`？

旧实现同时把 `config` 当作实例覆盖表和构造参数，造成重复。现在已经删除预复制循环，`config` 只传给 `Initialize()`，所有实例字段由具体初始化函数明确写入。
