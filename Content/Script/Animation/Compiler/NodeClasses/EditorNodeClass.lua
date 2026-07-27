-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，只集中保存动画图编辑器节点类路径。
-- 业务动画图通过语义字段引用原生类，避免 UE 类路径散落在状态机和 Pose 构建脚本中。

---@class AnimGraphEditorNodeClass
---@field VariableGet string 原生蓝图变量 Getter 节点。
---@field SequencePlayer string 原生动画序列播放器节点。
---@field BlendListByBool string 原生 Bool 姿势选择节点。
---@field BlendListByEnum string 原生 Enum 姿势选择节点。
---@field Inertialization string 原生惯性化节点。
---@field Slot string 原生 Montage Slot 节点。
---@field LayeredBlendPerBone string 原生按骨骼分层混合节点。
---@field LocalToComponentSpace string 原生局部空间转组件空间节点。
---@field ComponentToLocalSpace string 原生组件空间转局部空间节点。
---@field OrientationWarping string 原生方向扭曲节点。
---@field FootPlacement string 原生脚部放置节点。
---@field LegIK string 原生多腿 IK 节点。
---@field TwoBoneIK string 原生双骨骼 IK 节点。
---@field SaveCachedPose string 原生保存缓存姿势节点。
---@field UseCachedPose string 原生读取缓存姿势节点。
local EditorNodeClass = {
    VariableGet = "/Script/BlueprintGraph.K2Node_VariableGet",
    SequencePlayer = "/Script/AnimGraph.AnimGraphNode_SequencePlayer",
    BlendListByBool = "/Script/AnimGraph.AnimGraphNode_BlendListByBool",
    BlendListByEnum = "/Script/AnimGraph.AnimGraphNode_BlendListByEnum",
    Inertialization = "/Script/AnimGraph.AnimGraphNode_Inertialization",
    Slot = "/Script/AnimGraph.AnimGraphNode_Slot",
    LayeredBlendPerBone = "/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend",
    LinkedInputPose = "/Script/AnimGraph.AnimGraphNode_LinkedInputPose",
    LinkedAnimLayer = "/Script/AnimGraph.AnimGraphNode_LinkedAnimLayer",
    LinkedAnimGraph = "/Script/AnimGraph.AnimGraphNode_LinkedAnimGraph",
    LocalToComponentSpace = "/Script/AnimGraph.AnimGraphNode_LocalToComponentSpace",
    ComponentToLocalSpace = "/Script/AnimGraph.AnimGraphNode_ComponentToLocalSpace",
    OrientationWarping = "/Script/AnimationWarpingEditor.AnimGraphNode_OrientationWarping",
    FootPlacement = "/Script/AnimationWarpingEditor.AnimGraphNode_FootPlacement",
    LegIK = "/Script/AnimGraph.AnimGraphNode_LegIK",
    TwoBoneIK = "/Script/AnimGraph.AnimGraphNode_TwoBoneIK",
    SaveCachedPose = "/Script/AnimGraph.AnimGraphNode_SaveCachedPose",
    UseCachedPose = "/Script/AnimGraph.AnimGraphNode_UseCachedPose",
}

return EditorNodeClass
