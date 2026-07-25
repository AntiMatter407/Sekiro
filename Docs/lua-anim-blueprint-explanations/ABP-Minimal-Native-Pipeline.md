# ABP_Minimal 新版 Lua 动画蓝图生成流程

本文以 `Content/Script/Animation/Examples/ABP_Minimal.lua` 和
`Content/Script/Animation/Examples/StateMachines/MinimalLocomotion.lua` 为例，
说明新版 Lua 动画蓝图如何完成：

```text
Lua 更新变量
→ CompileIR 生成 Table
→ C++ 读取 Table
→ 创建原生 AnimBlueprint
→ 创建原生 Transition Rule
→ UE 编译 GeneratedClass
```

## 一、新旧版本的核心区别

旧式 Transition Rule：

```text
Transition 检查
→ C++ 回调 Lua
→ CanEnter_Idle_Move(Inst)
→ Lua 返回 true/false
```

新版 Transition Rule：

```text
Lua BlueprintUpdateAnimation 更新 bShouldMove
→ Transition Rule 直接读取 bShouldMove
→ UE 原生蓝图节点返回 true/false
```

新版代码：

```lua
Machine:Transition("Idle_Move", "Idle", "Move", {
    Rule = Rule.BoolProperty("bShouldMove", true),
})
```

不再需要：

```lua
function MinimalLocomotion.CanEnter_Idle_Move(Inst)
    return Inst.bShouldMove
end
```

## 二、最小示例行为

`ABP_Minimal` 声明两个真正生成到动画蓝图类中的变量：

```lua
self:Variable("DemoElapsedSeconds", "Float", 0.0)
self:Variable("bShouldMove", "Bool", false)
```

最终会成为生成类成员：

```text
SekiroGeneratedAnimBlueprint_C
├── DemoElapsedSeconds : Float
└── bShouldMove         : Bool
```

Lua 每帧更新变量：

```lua
function ABP_Minimal.BlueprintUpdateAnimation(Inst, delta_seconds)
    local elapsed_seconds =
        (Inst.DemoElapsedSeconds or 0.0)
        + math.max(delta_seconds or 0.0, 0.0)

    Inst.DemoElapsedSeconds = elapsed_seconds
    Inst.bShouldMove =
        math.floor(elapsed_seconds / 2.0) % 2 == 1
end
```

运行效果：

```text
0～2 秒：bShouldMove = false → Idle
2～4 秒：bShouldMove = true  → Move
4～6 秒：bShouldMove = false → Idle
```

Idle 使用待机动画，Move 使用前向跑步循环。

## 三、Lua 模块导出

C++ 通过 UnLua 加载：

```lua
require("Animation.Examples.ABP_Minimal")
```

Lua 文件最后返回：

```lua
return ABP_Minimal:Export()
```

返回值不是 AnimBlueprint 或 UObject，而是一个 Lua 模块 Table：

```lua
{
    CompileIR = function() ... end,
    BlueprintUpdateAnimation = function(Inst, DeltaSeconds) ... end,
}
```

- `CompileIR()` 只在编辑器编译期执行，生成动画蓝图 IR Table。
- `BlueprintUpdateAnimation()` 在游戏线程逐帧执行，更新生成变量。

## 四、CompileIR 执行过程

C++ 调用：

```lua
module.CompileIR()
```

`Export()` 内部为每次编译创建干净实例：

```lua
local instance = ABP_Minimal:New()
local blueprint_ir = instance:CompileIR()
```

随后执行：

```lua
ABP_Minimal:AnimGraph(graph)
```

依次声明：

```text
GeneratedClass 变量
MainStateMachine
Save Cached Pose
Use Cached Pose
Inertialization
Output Pose
```

状态机模块声明：

```text
Entry → Idle
Idle → Move
Move → Idle
```

以及两个状态内部的 Sequence Player。

## 五、CompileIR 返回的 Table

下面省略自动生成的稳定 ID、源码行号、完整 Pin 契约和布局数据，但字段层次与真实 IR 一致：

```lua
{
    SchemaVersion = 2,
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton =
        "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",

    Variables = {
        {
            Name = "DemoElapsedSeconds",
            DataType = "Float",
            TypeObjectPath = "",
            DefaultValue = {
                Type = "Float",
                FloatValue = 0.0,
            },
            bTransient = true,
        },
        {
            Name = "bShouldMove",
            DataType = "Bool",
            TypeObjectPath = "",
            DefaultValue = {
                Type = "Bool",
                BoolValue = false,
            },
            bTransient = true,
        },
    },

    Layers = {
        {
            Name = "Main",
            RootGraphId = "<AnimGraph稳定ID>",

            Graphs = {
                {
                    Name = "AnimGraph",
                    GraphType = "Pose",
                    RootNodeId = "<OutputPose节点ID>",

                    Nodes = {
                        {
                            NodeType = "OutputPose",
                            DisplayName = "Output Pose",
                        },
                        {
                            NodeType = "StateMachine",
                            DisplayName = "MainStateMachine",
                            OwnedGraphId =
                                "<MainStateMachine内部Graph ID>",
                        },
                        {
                            NodeType = "SaveCachedPose",
                            DisplayName = "SavedLocomotion",
                        },
                        {
                            NodeType = "UseCachedPose",
                            DisplayName = "UseLocomotion",
                        },
                        {
                            NodeType = "Inertialization",
                            DisplayName = "FinalInertialization",
                        },
                    },

                    Links = {
                        {
                            Source = {
                                NodeId = "<MainStateMachine ID>",
                                PinName = "Pose",
                            },
                            Target = {
                                NodeId = "<SavedLocomotion ID>",
                                PinName = "Pose",
                            },
                        },
                        {
                            Source = {
                                NodeId = "<UseLocomotion ID>",
                                PinName = "Pose",
                            },
                            Target = {
                                NodeId = "<Inertialization ID>",
                                PinName = "Source",
                            },
                        },
                        {
                            Source = {
                                NodeId = "<Inertialization ID>",
                                PinName = "Pose",
                            },
                            Target = {
                                NodeId = "<OutputPose ID>",
                                PinName = "Result",
                            },
                        },
                    },
                },

                {
                    Name = "MainStateMachine",
                    GraphType = "StateMachine",

                    StateMachine = {
                        EntryStateId = "<Idle ID>",

                        States = {
                            {
                                Id = "<Idle ID>",
                                Name = "Idle",
                                GraphId = "<Idle StateGraph ID>",
                            },
                            {
                                Id = "<Move ID>",
                                Name = "Move",
                                GraphId = "<Move StateGraph ID>",
                            },
                        },

                        Transitions = {
                            {
                                Key = "Idle_Move",
                                SourceStateId = "<Idle ID>",
                                TargetStateId = "<Move ID>",
                                RuleFunctionName = "",

                                Settings = {
                                    BlendDuration = 0.15,
                                    PriorityOrder = 0,
                                    BlendMode = "Linear",
                                },

                                Gate = {
                                    RootIndex = 0,
                                    Nodes = {
                                        {
                                            Type = "BoolProperty",
                                            Name = "bShouldMove",
                                            ExpectedBool = true,
                                            Threshold = 0.0,
                                            Children = {},
                                        },
                                    },
                                },
                            },
                            {
                                Key = "Move_Idle",
                                SourceStateId = "<Move ID>",
                                TargetStateId = "<Idle ID>",
                                RuleFunctionName = "",

                                Settings = {
                                    BlendDuration = 0.15,
                                    PriorityOrder = 0,
                                    BlendMode = "Linear",
                                },

                                Gate = {
                                    RootIndex = 0,
                                    Nodes = {
                                        {
                                            Type = "BoolProperty",
                                            Name = "bShouldMove",
                                            ExpectedBool = false,
                                            Threshold = 0.0,
                                            Children = {},
                                        },
                                    },
                                },
                            },
                        },
                    },
                },

                {
                    Name = "<Idle StateGraph>",
                    GraphType = "StatePose",

                    Nodes = {
                        {
                            NodeType = "SequencePlayer",
                            DisplayName = "IdlePlayer",
                            Properties = {
                                {
                                    Name = "Sequence",
                                    Value = {
                                        Type = "SoftObjectPath",
                                        SoftObjectPathValue =
                                            "/Game/.../Anim_Sekiro_a000_000000",
                                    },
                                },
                                {
                                    Name = "bLoopAnimation",
                                    Value = {
                                        Type = "Bool",
                                        BoolValue = true,
                                    },
                                },
                                {
                                    Name = "PlayRate",
                                    Value = {
                                        Type = "Float",
                                        FloatValue = 1.0,
                                    },
                                },
                            },
                        },
                        {
                            NodeType = "StateResult",
                        },
                    },
                },

                {
                    Name = "<Move StateGraph>",
                    GraphType = "StatePose",

                    Nodes = {
                        {
                            NodeType = "SequencePlayer",
                            DisplayName = "MovePlayer",
                            Properties = {
                                {
                                    Name = "Sequence",
                                    Value = {
                                        Type = "SoftObjectPath",
                                        SoftObjectPathValue =
                                            "/Game/.../Anim_Sekiro_a000_000500",
                                    },
                                },
                                {
                                    Name = "bLoopAnimation",
                                    Value = {
                                        Type = "Bool",
                                        BoolValue = true,
                                    },
                                },
                                {
                                    Name = "PlayRate",
                                    Value = {
                                        Type = "Float",
                                        FloatValue = 1.0,
                                    },
                                },
                            },
                        },
                        {
                            NodeType = "StateResult",
                        },
                    },
                },
            },
        },
    },
}
```

这个 Table 是 Lua 和 C++ 的交接边界。Lua 到这里没有创建 UE 节点。

## 六、C++ 导入 Table

入口：

```text
USekiroAnimGraphIRLibrary::CompileLuaModule
```

处理流程：

```text
UnLua require Lua 模块
→ 取得模块 Table
→ 查找 CompileIR
→ 调用 CompileIR()
→ 取得返回 Table
→ 逐字段转换为 C++ 结构体
```

最终转换成：

```cpp
FSekiroAnimBlueprintIR
```

结构关系：

```text
FSekiroAnimBlueprintIR
├── Variables
└── Layers
    └── FSekiroAnimIRLayer
        └── Graphs
            ├── Nodes
            ├── Links
            └── StateMachine
                ├── States
                └── Transitions
```

Importer 依赖显式类型标签读取数据。例如：

```lua
{
    Type = "Float",
    FloatValue = 1.0,
}
```

会转换为 Float；而：

```lua
{
    Type = "SoftObjectPath",
    SoftObjectPathValue = "/Game/...",
}
```

会转换为 UE 软对象路径。

## 七、C++ 生成 AnimBlueprint

### 1. 预检查

C++ 首先检查：

- Parent Class 是否为 AnimInstance；
- Skeleton 是否存在；
- 动画资源是否存在并匹配目标 Skeleton；
- NodeType 是否注册；
- 属性类型是否合法；
- Pin 名称和方向是否合法；
- State、Graph 和 Transition 引用是否完整；
- Pose Graph 是否正确连接 Result。

### 2. 创建 GeneratedClass 变量

根据 IR 的 `Variables` 创建：

```text
DemoElapsedSeconds
bShouldMove
```

Blueprint 编译后，它们进入 `UAnimBlueprintGeneratedClass`。

### 3. 创建原生 AnimGraph 节点

节点映射示例：

```text
StateMachine
→ UAnimGraphNode_StateMachine

SequencePlayer
→ UAnimGraphNode_SequencePlayer

SaveCachedPose
→ UAnimGraphNode_SaveCachedPose

UseCachedPose
→ UAnimGraphNode_UseCachedPose

Inertialization
→ UAnimGraphNode_Inertialization
```

Factory 根据节点 `Properties` 设置动画资源、循环和播放倍率等属性。

### 4. 建立 Pin 连线

C++ 根据：

```lua
Source = {
    NodeId = "...",
    PinName = "Pose",
}

Target = {
    NodeId = "...",
    PinName = "Result",
}
```

找到原生节点和 Pin，再通过 UE Graph Schema 创建连接。

### 5. 创建状态机

根据 StateMachine IR 创建：

```text
Entry
Idle
Move
Idle → Move
Move → Idle
```

每个 State 的 `GraphId` 指向独占的 StatePose Graph。

## 八、生成原生 Transition Rule

Lua：

```lua
Rule.BoolProperty("bShouldMove", true)
```

转换后的 Gate：

```lua
Gate = {
    RootIndex = 0,
    Nodes = {
        {
            Type = "BoolProperty",
            Name = "bShouldMove",
            ExpectedBool = true,
        },
    },
}
```

C++ 在 Transition Rule Graph 中生成：

```text
Get bShouldMove
→ Transition Result
```

反向规则相当于：

```text
Get bShouldMove
→ NOT
→ Transition Result
```

`RuleFunctionName = ""` 表示过渡完全由原生 Gate 决定，不调用 Lua `CanEnter_*`。

## 九、生成 EventGraph 更新桥接

Factory 自动生成：

```text
Event BlueprintUpdateAnimation
├── Self
├── Delta Seconds
└── LuaModuleName = "Animation.Examples.ABP_Minimal"
        ↓
EvaluateBlueprintUpdateAnimation
```

运行时调用：

```lua
ABP_Minimal.BlueprintUpdateAnimation(Inst, delta_seconds)
```

Lua 更新 `DemoElapsedSeconds` 和 `bShouldMove`，原生状态机读取 `bShouldMove`。

## 十、UE 编译与运行

Factory 创建所有变量、节点、状态和连线后，调用 UE Blueprint 编译：

```text
UAnimGraphNode_StateMachine
→ FAnimNode_StateMachine

UAnimGraphNode_SequencePlayer
→ FAnimNode_SequencePlayer
```

最终生成：

```text
UAnimBlueprintGeneratedClass
```

运行时完整流程：

```text
BlueprintUpdateAnimation Event
→ Lua 更新 bShouldMove
→ 原生 StateMachine 检查 Transition Rule
→ Idle 或 Move
→ SequencePlayer 采样动画
→ Cached Pose
→ Inertialization
→ 最终 Pose
```

## 十一、验证结果

本示例已经完成以下验证：

- Lua IR 导入无诊断；
- IR Schema 为 2；
- `DemoElapsedSeconds` 与 `bShouldMove` 正确导出；
- C++ Factory 创建临时 AnimBlueprint 无诊断；
- 成功生成 `UAnimBlueprintGeneratedClass`；
- 验证未启动 PIE，也未在 Content 中留下测试资产。
