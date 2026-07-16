# Lua 动画蓝图源码讲解

本目录逐个解释 `Content/Script/Animation/Compiler/` 下的 Lua 动画蓝图编译器源码。

目标不是只说明函数做了什么，而是说明：

- 为什么需要这个类型；
- 它在整个编译流程中的位置；
- 每段 Lua 元表代码实际产生了什么对象；
- 它与 UE AnimGraph、UnLua 和运行时 AnimInstance 的边界；
- 讲解过程中提出的问题及最终答案。

## 阅读顺序

| 顺序 | 文件 | 状态 | 讲解 |
|---|---|---|---|
| 1 | `CompilerClass.lua` | 已完成 | [CompilerClass](CompilerClass.md) |
| 2 | `IRSchema.lua` | 已完成 | [IRSchema](IRSchema.md) |
| 3 | `IRTypes.lua` 与 `IRValue.lua` | 已完成 | [IRTypes 与 IRValue](IRTypes-and-IRValue.md) |
| 4 | `NodeContracts.lua` | 已完成 | [NodeContracts](NodeContracts.md) |
| 5 | `LuaAnimPin.lua` | 已完成 | [LuaAnimPin](LuaAnimPin.md) |
| 6 | `LuaAnimNode.lua` | 已完成 | [LuaAnimNode](LuaAnimNode.md) |
| 7 | `LuaAnimGraph.lua` | 已完成 | [LuaAnimGraph](LuaAnimGraph.md) |
| 8 | `LuaAnimBlueprint.lua` | 已完成 | [LuaAnimBlueprint](LuaAnimBlueprint.md) |
| 9 | `LuaAnimStateMachine.lua` | 已完成 | [LuaAnimStateMachine](LuaAnimStateMachine.md) |
| 10 | `LuaStateMachineNode.lua` | 已完成 | [LuaStateMachineNode](LuaStateMachineNode.md) |
| 11 | `LuaAnimStateMachineGraph.lua` | 已完成 | [LuaAnimStateMachineGraph](LuaAnimStateMachineGraph.md) |
| 12 | `LuaAnimState.lua` | 已完成 | [LuaAnimState](LuaAnimState.md) |
| 13 | `LuaAnimStateGraph.lua` | 已完成 | [LuaAnimStateGraph](LuaAnimStateGraph.md) |
| 14 | `LuaAnimTransition.lua` | 已完成 | [LuaAnimTransition](LuaAnimTransition.md) |

阅读顺序会根据实际问答调整，不要求严格按照文件名字母顺序。

## 问答记录

讲解过程中产生的实际问题统一记录在 [问答记录](qa.md)。对应源码文档中也会保留与该文件直接相关的问答。

## 补充主题

- [LuaAnimLayer：当前 Graph 作用域与 UE Animation Layer 的区别](LuaAnimLayer.md)
- [Lua 到 UE 原生动画结构的编译与运行链](LuaToNativePipeline.md)
