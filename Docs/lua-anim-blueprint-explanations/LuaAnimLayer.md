# `LuaAnimLayer` 概念讲解

源文件：`Content/Script/Animation/Compiler/LuaAnimLayer.lua`

## 结论

当前 Lua 动画蓝图实现了一个名为 `Layer` 的 **IR Graph 所有权作用域**，但还没有实现 UE 原生 Animation Layer 功能。

现阶段：

- Lua 编译器具有 `LuaAnimLayer` 类和 `Layers` IR 数组；
- `LuaAnimBlueprint` 自动创建唯一的 `Main` Layer；
- `Main` 保存根 AnimGraph、状态机内部 Graph 和所有 StatePose Graph；
- C++ Factory 明确要求 `Layers.Num() == 1`；
- 第二个 Layer 会报 `Factory.UnsupportedLayerCount`；
- 没有生成 UE Animation Layer 函数、Animation Blueprint Interface 或 Linked Anim Layer 节点。

因此，阅读当前代码时应把 `Layer` 理解为“Graph 作用域”，不能理解成已经可以在 UE 中调用和覆盖的动画层。

## UE 原生 Animation Layer 是什么

UE Animation Layer 更接近一种专门处理 Pose 的动画蓝图函数：

- 可以具有 Pose 输入和 Pose 输出；
- 通常由 Animation Blueprint Interface 声明；
- AnimBlueprint 可以实现或覆盖 Layer；
- 可以通过 Linked Anim Layer 在运行时链接其他 AnimInstance 实现；
- 每个 Layer 在编辑器中拥有自己的动画 Graph；
- Layer 之间通过明确的函数接口交换 Pose，而不是共享任意内部 Graph。

例如可以抽象出：

```text
GroundLocomotion Layer
UpperBodyCombat Layer
AdditiveAim Layer
FinalPose Layer
```

这些 Layer 可以有各自输入输出和实现边界，再由调用节点组合。

## 当前 `LuaAnimLayer` 实际做什么

`LuaAnimLayer` 保存：

| 字段 | 当前用途 |
|---|---|
| `Id` / `Name` | Graph 作用域的稳定身份 |
| `Graphs` | 本作用域内全部 Graph 的扁平数组 |
| `GraphIds` | 按稳定 ID 查找 Graph |
| `GraphNames` | 按名称查找顶层 Pose Graph |
| `RootGraphId` | 指定该作用域最终输出的根 Pose Graph |

默认编译流程是：

```lua
local Layer = Blueprint:AnimationLayer("Main")
local Graph = Layer:PoseGraph("AnimGraph")
Blueprint:AnimGraph(Graph)
Layer:SetRootGraph(Graph)
```

业务动画蓝图不会直接写这段代码，`BuildDeclaredAnimGraph()` 已经自动完成。

## 为什么所有内部 Graph 都登记在 Layer 中

一个状态机不仅有外层 Pose 节点，还拥有多张内部 Graph：

```text
Main Layer
├── AnimGraph                       根 Pose Graph
├── GroundLocomotionGraph           StateMachine Graph
├── Idle StatePose Graph
├── Move StatePose Graph
└── 子状态机及其 StatePose Graph
```

这些 Graph 统一登记在同一个 Layer，Node 和 State 再通过稳定 ID 引用内部 Graph。这样 Validator 可以限定和检查：

- Graph ID 在作用域内唯一；
- Node 拥有的内部 Graph 位于同一作用域；
- StatePose Graph 恰好属于一个 State；
- Layer 根必须是普通 Pose Graph；
- Graph 所有权不能形成循环；
- Cached Pose 等引用不能无边界跨作用域扩散。

所以当前 `Layer` 的主要价值是所有权、查找、验证和未来扩展，不是运行时 Pose 分层混合。

## 当前只有一个 `Main`

`LuaAnimBlueprint:BuildDeclaredAnimGraph()` 固定创建：

```text
Layer: Main
Root Graph: AnimGraph
```

C++ Preflight 随后检查：

```cpp
if (Blueprint.Layers.Num() != 1)
{
    // Factory.UnsupportedLayerCount
    return false;
}
```

Factory 只读取 `Layers[0]`，并把它的 `RootGraphId` 物化到普通 AnimBlueprint 的主 `UAnimationGraph`。

虽然 Lua API 目前允许再次调用 `AnimationLayer("UpperBody")`，生成的多 Layer IR 仍无法创建 AnimBlueprint。这只是 IR 预留，不是可用功能。

## 它与状态机“层级”的区别

子状态机不是 Layer。

```text
Main Layer
└── AnimGraph
    └── GroundLocomotion StateMachine
        └── Grounded StatePose Graph
            └── StandingLocomotion 子状态机
```

这里无论嵌套多少状态机，都仍在同一个 `Main` Layer。状态机层级表达状态选择和 Pose 组合；Layer 表达更上层的 Graph 所有权或动画函数边界。

把状态机拆到不同 Lua 文件也不会自动变成 Animation Layer。文件拆分只影响代码组织，不改变生成的 UE Graph 类型。

## Lua 动画蓝图要实现真正 Animation Layer 还缺什么

至少需要补充以下能力：

1. 在 IR 中区分主 AnimGraph Layer 与真正的 Animation Layer Function。
2. 为 Layer 声明输入 Pose、输出 Pose以及其他参数 Pin。
3. 支持 Animation Blueprint Interface 或等价的 Layer 签名描述。
4. Factory 为每个 Layer 创建对应的原生 Animation Layer Graph，而不是只寻找主 `UAnimationGraph`。
5. 注册并生成 Linked Anim Layer、Linked Anim Graph 等调用节点。
6. 支持 Layer Override、目标 AnimInstance Class 和运行时链接生命周期。
7. 建立跨 Layer Pose 调用规则，不能直接使用普通 Graph Link 或任意共享 Cached Pose。
8. 增加多 Layer 的验证、规范化、生成和运行时自动化测试。

完成这些之后，Lua 才能写出真正对应 UE Animation Layer 的结构，例如：

```lua
function ABP_Sekiro.AnimationLayer_GroundLocomotion(Graph, InputPose)
    -- 声明该 Layer 独立的节点网络并输出 Pose。
end
```

上面只是目标语义示例，当前 API 尚不存在。

## 是否应该继续叫 `Layer`

从未来扩展角度，这个名称可以保留；从当前功能准确性看，它更接近：

```text
LuaAnimGraphScope
LuaAnimGraphProgram
LuaAnimRootScope
```

如果下一阶段准备立即实现 UE Animation Layer，可以保留 `LuaAnimLayer` 并扩展语义。如果短期只支持主 AnimGraph，重命名为 `LuaAnimGraphScope` 会更不容易误解，但会涉及 IR Schema、C++ 结构和文档的整体迁移。

## 相关问答

### 当前能不能创建 `GroundLocomotion` 和 `UpperBody` 两个 Layer？

Lua 前端能构造两条 Layer IR，但 C++ Factory 会拒绝生成，因此当前不能作为可运行功能使用。

### 状态机拆成单独 Lua 文件是不是动画层？

不是。它只是独立状态机定义，最终仍生成到 `Main` Layer 中的 StateMachine Node 及内部 Graph。

### Cached Pose 能否跨 Layer 使用？

当前只有一个 `Main`，不存在真正的跨 Layer调用。未来实现原生 Animation Layer 后，应通过 Layer Pose 输入输出或 Linked Layer 接口传递，不应把 Cached Pose 当作跨 Layer 全局变量。

### 当前 Lua 动画蓝图有没有动画层概念？

有“Layer”这一层 IR 抽象和所有权容器，但没有 UE 原生 Animation Layer 的生成与运行时功能。准确说法是：**概念预留了一部分，功能尚未实现。**
