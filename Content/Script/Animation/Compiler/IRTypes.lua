-- DSL(Domain-Specific Language),领域特定语言
-- AnimGraph IR 的 EmmyLua 静态类型声明。
-- 本模块只供 Rider/LuaLS 建立字段补全与类型跳转，不参与 Lua 编译器运行时流程。

---@alias SekiroAnimIRValueType
---| '"None"'
---| '"Bool"'
---| '"Integer"'
---| '"Float"'
---| '"Name"'
---| '"String"'
---| '"SoftObjectPath"'
---| '"SoftClassPath"'

---@alias SekiroAnimIRPinDirection '"Input"'|'"Output"'

---@alias SekiroAnimIRGraphType '"Pose"'|'"StatePose"'|'"StateMachine"'

---@class SekiroAnimIRSourceLocation
---@field LuaModule string 声明来源的 Lua 模块名。
---@field Line number 声明所在行整数；无法定位时为 0。
---@field Column number 声明所在列整数；当前固定为 0。

---@class SekiroAnimIRValue
---@field Type SekiroAnimIRValueType 显式属性类型标签。
---@field BoolValue boolean|nil Bool 类型使用的值。
---@field IntegerValue number|nil Integer 类型使用的整数值。
---@field FloatValue number|nil Float 类型使用的值。
---@field NameValue string|nil Name 类型使用的值。
---@field StringValue string|nil String 类型使用的值。
---@field SoftObjectPathValue string|nil SoftObjectPath 类型使用的值。
---@field SoftClassPathValue string|nil SoftClassPath 类型使用的值。

---@class SekiroAnimIRPin
---@field Name string C++ NodeType 注册表中的稳定 Pin 名称。
---@field Direction SekiroAnimIRPinDirection Pin 方向。
---@field DataType string C++ NodeType 注册表中的稳定数据类型名。
---@field bAllowMultipleConnections boolean 是否允许该 Pin 连接多个 Link；Pose 输出可据此扇出。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。

---@class SekiroAnimIRProperty
---@field Name string C++ NodeType 注册表允许写入的属性名。
---@field Value SekiroAnimIRValue 显式类型化属性值。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。

---@class SekiroAnimIRNode
---@field Id string 节点稳定 ID。
---@field NodeType string NodeFactory 注册的节点类型名。
---@field DisplayName string 编辑器显示名称。
---@field OwnedGraphId string 节点独占的内部 Graph ID；无内部 Graph 时为空。
---@field Pins SekiroAnimIRPin[] 对 C++ 注册 Pin 的完整一致性断言，不定义真实 Pin。
---@field Properties SekiroAnimIRProperty[] 请求 NodeFactory 写入的已注册类型化属性值。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation 节点源码位置。

---@class SekiroAnimIRPinEndpoint
---@field NodeId string 端点所属节点的稳定 ID。
---@field PinName string 端点 Pin 名称。

---@class SekiroAnimIRLink
---@field Id string Link 稳定 ID。
---@field Source SekiroAnimIRPinEndpoint 输出端点。
---@field Target SekiroAnimIRPinEndpoint 输入端点。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Link 源码位置。

---@class SekiroAnimIRState
---@field Id string State 稳定 ID。
---@field Name string State 语义名称。
---@field GraphId string 该 State 独占的 StatePose Graph ID。
---@field bAlwaysResetOnEntry boolean 重新进入时是否重置 State Pose Graph。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation State 源码位置。

---@class SekiroAnimIRTransitionSettings
---@field BlendDuration number 过渡混合时长，单位为秒。
---@field PriorityOrder number 同一源状态下的过渡优先级整数。
---@field BlendMode string UE 过渡混合模式注册名。

---@class SekiroAnimIRTransition
---@field Id string Transition 稳定 ID。
---@field Key string 所属状态机内唯一且可读的 Transition Key。
---@field SourceStateId string 起始 State 稳定 ID。
---@field TargetStateId string 目标 State 稳定 ID。
---@field RuleFunctionName string Lua 规则函数名。
---@field Settings SekiroAnimIRTransitionSettings 过渡混合设置。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Transition 源码位置。

---@class SekiroAnimIRStateMachine
---@field EntryStateId string Entry 指向的 State 稳定 ID。
---@field States SekiroAnimIRState[] 状态集合。
---@field Transitions SekiroAnimIRTransition[] 有向过渡集合。

---@class SekiroAnimIRGraph
---@field Id string Graph 稳定 ID。
---@field Name string Graph 语义名称。
---@field GraphType SekiroAnimIRGraphType Graph 注册类型名。
---@field RootNodeId string Pose Graph 的根节点 ID；非 Pose Graph 可为空。
---@field Nodes SekiroAnimIRNode[] Graph 内节点声明。
---@field Links SekiroAnimIRLink[] Graph 内 Pose Link 声明。
---@field StateMachine SekiroAnimIRStateMachine 状态机拓扑；Pose Graph 中为空拓扑。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Graph 源码位置。

---@class SekiroAnimIRLayer
---@field Id string Layer 稳定 ID。
---@field Name string Layer 语义名称。
---@field RootGraphId string 最终输出 Pose 的根 Graph ID。
---@field Graphs SekiroAnimIRGraph[] Layer 拥有的 Graph 集合。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Layer 源码位置。

---@class SekiroAnimBlueprintIR
---@field SchemaVersion number IR Schema 版本整数。
---@field SourceModule string 动画蓝图 Lua 源模块名。
---@field ParentAnimInstanceClass string 父 AnimInstance 类软路径。
---@field TargetSkeleton string 普通 AnimBlueprint 的目标 Skeleton 资产软路径。
---@field Layers SekiroAnimIRLayer[] Graph 所有权作用域；当前原生 Factory 仅支持一个 Main Layer，并非 UE Animation Layer 函数。
---@field SourceLocation SekiroAnimIRSourceLocation 动画蓝图源码位置。

local IRTypes = {}

return IRTypes
