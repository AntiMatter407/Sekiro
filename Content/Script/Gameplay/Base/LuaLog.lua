-- Lua 类型：纯 Lua 工具模块。本文件不绑定 UObject，所有参数都由调用方显式传入。
-- 为 Gameplay 运行时脚本提供统一的可开关调试日志格式。

local LuaLog = {}

---在调试开关启用时输出带模块名和功能区域的日志。
---@param enabled boolean 是否允许输出本条调试日志。
---@param module_name string 产生日志的 Lua 模块或 UObject 语义名称。
---@param area string 日志所属的功能区域。
---@param message string 需要输出的调试内容。
---@return nil 该函数只写入日志。
function LuaLog.Debug(enabled, module_name, area, message)
    if enabled ~= true then
        return
    end

    print(string.format(
        "[%s][%s] %s",
        tostring(module_name),
        tostring(area),
        tostring(message)))
end

return LuaLog
