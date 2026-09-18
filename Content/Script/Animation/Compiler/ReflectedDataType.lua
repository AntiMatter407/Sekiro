-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，只保存反射节点 Pin 使用的稳定类型标识。
-- 这些标识只负责 Lua 编译期的连线一致性检查；最终兼容性仍由 UE 原生 Graph Schema 判定。

local ReflectedDataType = {
    Bool = "Bool",
    Float = "Float",
    Pose = "Pose",
    MotionTrajectory = "MotionTrajectory",
    PoseSearchDatabase = "PoseSearchDatabase",
    GameplayTagContainer = "GameplayTagContainer",
    PoseSearchSearchable = "PoseSearchSearchable",
}

-- 类型表会被 ReflectedNodeSpec 长期引用；整体热重载保证编辑器无需重启即可读取新增类型。
ReflectedDataType.HOT_RELOAD = true

return ReflectedDataType
