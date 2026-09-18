-- Lua 类型：纯 Lua类/数据/工具模块。本文件不绑定 UObject，只集中描述尚未进入 C++ 注册表的原生节点。
-- 业务动画图引用语义化 Spec；原生类与 Pin 名发生变化时，只需要在这里复核一次。

local ReflectedDataType = require(
    "Animation.Compiler.ReflectedDataType")

---@class LuaReflectedNodePropertySpec
---@field ValueType SekiroAnimIRValueType 反射属性写入所需的显式 IR 值类型。

---@class LuaReflectedNodeFunctionSpec
---@field PropertyName string 编辑器节点上保存 FMemberReference 的反射属性名。
---@field PrototypeFunction string UE 线程安全 Anim Node Function 原型路径。

---@class LuaReflectedNodeSpec
---@field NodeType string 仅用于 Lua IR 诊断的稳定语义类型；不要求 C++ 注册同名节点。
---@field EditorNodeClass string 原生编辑器节点类软路径。
---@field Pins SekiroAnimIRPin[] 供 Lua 具名连接与早期类型检查使用的 Pin 断言。
---@field Properties table<string, LuaReflectedNodePropertySpec> 允许业务脚本写入的反射属性及值类型。
---@field Functions table<string, LuaReflectedNodeFunctionSpec>|nil 允许业务脚本绑定的 Anim Node Function 契约。

local ReflectedNodeSpec = {}
local PinDirection = UE.ELuaAnimIRPinDirection
local ValueType = UE.ELuaAnimIRValueType
-- 节点契约是嵌套纯数据表；整体替换才能让已加载动画蓝图立即读取修改后的字段白名单。
ReflectedNodeSpec.HOT_RELOAD = true

ReflectedNodeSpec.PoseSearchHistoryCollector = {
    NodeType = "ReflectedPoseSearchHistoryCollector",
    EditorNodeClass =
        "/Script/PoseSearchEditor.AnimGraphNode_PoseSearchHistoryCollector",
    Pins = {
        {
            Name = "Source",
            Direction = PinDirection.Input,
            DataType = ReflectedDataType.Pose,
            bAllowMultipleConnections = false,
            DeclarationOrder = 0,
        },
        {
            -- UE5.8 将未来轨迹交给 History Collector，而非直接传入 Motion Matching。
            Name = "TransformTrajectory",
            Direction = PinDirection.Input,
            DataType = ReflectedDataType.MotionTrajectory,
            bAllowMultipleConnections = false,
            DeclarationOrder = 1,
        },
        {
            Name = "Pose",
            Direction = PinDirection.Output,
            DataType = ReflectedDataType.Pose,
            bAllowMultipleConnections = true,
            DeclarationOrder = 2,
        },
    },
    Properties = {
        bGenerateTrajectory = {
            ValueType = ValueType.Bool,
        },
    },
}

ReflectedNodeSpec.LocalReferencePose = {
    NodeType = "ReflectedLocalReferencePose",
    EditorNodeClass = "/Script/AnimGraph.AnimGraphNode_LocalRefPose",
    Pins = {
        {
            Name = "Pose",
            Direction = PinDirection.Output,
            DataType = ReflectedDataType.Pose,
            bAllowMultipleConnections = true,
            DeclarationOrder = 0,
        },
    },
    Properties = {},
}

ReflectedNodeSpec.MotionMatching = {
    NodeType = "ReflectedMotionMatching",
    EditorNodeClass =
        "/Script/PoseSearchEditor.AnimGraphNode_MotionMatching",
    Pins = {
        {
            -- 数据库仍写入默认对象；声明输入 Pin 使 UE5.8 节点在图中显示并可被动态覆盖。
            Name = "Database",
            Direction = PinDirection.Input,
            DataType = ReflectedDataType.PoseSearchDatabase,
            bAllowMultipleConnections = false,
            DeclarationOrder = 0,
        },
        {
            Name = "Pose",
            Direction = PinDirection.Output,
            DataType = ReflectedDataType.Pose,
            bAllowMultipleConnections = true,
            DeclarationOrder = 1,
        },
    },
    Properties = {
        Database = {
            ValueType = ValueType.SoftObjectPath,
        },
        BlendTime = {
            ValueType = ValueType.Float,
        },
        MaxActiveBlends = {
            ValueType = ValueType.Integer,
        },
        BlendOption = {
            ValueType = ValueType.Enum,
        },
        PoseJumpThresholdTime = {
            ValueType = ValueType.Struct,
        },
        PoseReselectHistory = {
            ValueType = ValueType.Float,
        },
        SearchThrottleTime = {
            ValueType = ValueType.Float,
        },
        PlayRate = {
            ValueType = ValueType.Struct,
        },
        PlayRateMultiplier = {
            ValueType = ValueType.Float,
        },
        bUseInertialBlend = {
            ValueType = ValueType.Bool,
        },
    },
    Functions = {
        Update = {
            -- 基础 AnimGraphNode 的 UpdateFunction 在节点 Update_AnyThread 前调用，负责应用持久重搜请求。
            PropertyName = "UpdateFunction",
            PrototypeFunction =
                "/Script/AnimGraphRuntime.AnimExecutionContextLibrary.Prototype_ThreadSafeAnimUpdateCall",
        },
        StateUpdated = {
            -- UE5.8 在搜索状态更新后、BlendTo 前调用；它是方案中的 PostSelection 采集点。
            PropertyName = "OnMotionMatchingStateUpdatedFunction",
            PrototypeFunction =
                "/Script/AnimGraphRuntime.AnimExecutionContextLibrary.Prototype_ThreadSafeAnimUpdateCall",
        },
    },
}

return ReflectedNodeSpec
