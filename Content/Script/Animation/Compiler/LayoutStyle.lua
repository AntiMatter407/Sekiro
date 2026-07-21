-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Lua 动画蓝图 Graph 的编辑器排版风格枚举。
-- 值会原样进入 IR 并由 C++ Importer 严格转换；本模块不参与运行时动画求值。

local LayoutStyle = {
    Auto = "Auto",
    LeftToRight = "LeftToRight",
    RightToLeft = "RightToLeft",
    TopToBottom = "TopToBottom",
    BottomToTop = "BottomToTop",
    CompactGrid = "CompactGrid",
    Radial = "Radial",
    HierarchicalBlocks = "HierarchicalBlocks",
}

return LayoutStyle
