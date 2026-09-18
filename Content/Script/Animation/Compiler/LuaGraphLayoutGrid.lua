-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- 单个 Graph 内的编辑器布局分区。
-- Grid 只记录节点或状态的逻辑单元格，不保存 UE 画布像素坐标，也不影响运行时语义。

local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")

---@class LuaGraphLayoutGridConfig
---@field Graph LuaAnimGraph|LuaAnimStateMachineGraph 所属 Graph。
---@field Name string Graph 内唯一的布局分区名。
---@field Settings LuaGraphLayoutGridSettings|nil 分区位置、间距和可选风格覆盖。

---@class LuaGraphLayoutGridSettings
---@field RegionColumn number|nil 分区在画布区域网格中的横向索引，必须为非负整数。
---@field RegionRow number|nil 分区在画布区域网格中的纵向索引，必须为非负整数。
---@field CellWidth number|nil 分区内部单元格横向间距，单位为 UE Graph 像素。
---@field CellHeight number|nil 分区内部单元格纵向间距，单位为 UE Graph 像素。
---@field LayoutStyle SekiroAnimIRLayoutStyle|nil 分区风格覆盖；当前显式 Grid 不消费该值。

---@class LuaGraphLayoutGrid: CompilerClass
---@field Graph LuaAnimGraph|LuaAnimStateMachineGraph 所属 Graph。
---@field Name string Graph 内唯一分区名。
---@field RegionColumn number 区域横向索引。
---@field RegionRow number 区域纵向索引。
---@field CellWidth number 单元格横向间距。
---@field CellHeight number 单元格纵向间距。
---@field LayoutStyle SekiroAnimIRLayoutStyle 分区风格覆盖。
---@field Items SekiroAnimIRLayoutItem[] 显式元素位置。
local LuaGraphLayoutGrid = CompilerClass:Extend("LuaGraphLayoutGrid")

---初始化布局分区并规范默认间距；数值合法性会在 Place 和 C++ Validator 中再次校验。
---@param config LuaGraphLayoutGridConfig 所属 Graph、分区名和可选设置。
---@return nil result 只初始化编译期布局数据。
function LuaGraphLayoutGrid:Initialize(config)
    local settings = config.Settings or {}
    self.Graph = assert(config.Graph, "LuaGraphLayoutGrid requires Graph")
    self.Name = IRSchema.RequireSemanticName(config.Name, "Layout Grid")
    self.RegionColumn = settings.RegionColumn or 0
    self.RegionRow = settings.RegionRow or 0
    self.CellWidth = settings.CellWidth or 360
    self.CellHeight = settings.CellHeight or 220
    self.LayoutStyle = settings.LayoutStyle or LayoutStyle.Auto
    self.Items = {}
    self.ElementIds = {}
    self.Cells = {}
end

---把当前 Graph 的节点或状态固定到分区单元格；未调用本函数的元素由 LayoutStyle 自动推导。
---@param element LuaAnimNode|LuaAnimState 当前 Graph 拥有的节点或状态。
---@param column number 分区内部非负整数列索引。
---@param row number 分区内部非负整数行索引。
---@return LuaAnimNode|LuaAnimState element 原样返回元素，便于继续声明。
function LuaGraphLayoutGrid:Place(element, column, row)
    assert(element ~= nil and type(element.Id) == "string", "Grid:Place requires Graph element")
    assert(column >= 0 and column % 1 == 0, "Grid column must be a non-negative integer")
    assert(row >= 0 and row % 1 == 0, "Grid row must be a non-negative integer")
    local belongs_to_graph = element.Graph == self.Graph or element.MachineGraph == self.Graph
    assert(belongs_to_graph, "Grid element must belong to the same Graph")
    assert(self.Graph.LayoutElementIds[element.Id] == nil, "Graph element may only be placed once")
    local cell_key = string.format("%d:%d", column, row)
    assert(self.Cells[cell_key] == nil, "Layout Grid cell is already occupied")

    self.Graph.LayoutElementIds[element.Id] = self.Name
    self.ElementIds[element.Id] = true
    self.Cells[cell_key] = element.Id
    table.insert(self.Items, {
        ElementId = element.Id,
        Column = column,
        Row = row,
        ColumnSpan = 1,
        RowSpan = 1,
        DeclarationOrder = #self.Items,
    })
    return element
end

---导出可由 C++ Importer 解析的单个布局分区。
---@return SekiroAnimIRLayoutGrid ir_grid 不含 UObject 和运行时引用的纯 Lua 表。
function LuaGraphLayoutGrid:ToIR()
    return {
        Name = self.Name,
        RegionColumn = self.RegionColumn,
        RegionRow = self.RegionRow,
        CellWidth = self.CellWidth,
        CellHeight = self.CellHeight,
        LayoutStyle = self.LayoutStyle,
        Items = self.Items,
    }
end

return LuaGraphLayoutGrid
