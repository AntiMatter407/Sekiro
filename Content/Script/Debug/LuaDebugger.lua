-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- 统一管理 Rider EmmyLua 调试端口的启动和停止。
-- 编辑器工具栏开关与 PIE Main.lua 共用本模块，避免重复监听同一端口。

local debugger_dll_pattern = "C:/Users/guoya/AppData/Roaming/JetBrains/Rider2026.1/plugins/IntelliJ-EmmyLua/debugger/emmy/windows/x64/?.dll"
if string.find(package.cpath, debugger_dll_pattern, 1, true) == nil then
    package.cpath = package.cpath .. ";" .. debugger_dll_pattern
end

local debugger = require("emmy_core")

---@class LuaDebuggerModule
---@field IsListening boolean 9966 端口是否已由本 Lua Env 启动监听。
local LuaDebugger = {
    IsListening = false,
}

---在当前 Lua Env 中启动 Rider EmmyLua TCP 监听。
---@return boolean started 首次启动或端口已由本模块监听时返回 true，底层调试器报错时返回 false。
function LuaDebugger.Start()
    if LuaDebugger.IsListening == true then
        return true
    end

    local succeeded = pcall(debugger.tcpListen, "localhost", 9966)
    if succeeded ~= true then
        return false
    end

    LuaDebugger.IsListening = true
    return true
end

---停止当前 Lua Env 中的 Rider EmmyLua 调试器并释放监听端口。
---@return boolean stopped 成功停止或本模块尚未监听时返回 true，底层调试器报错时返回 false。
function LuaDebugger.Stop()
    if LuaDebugger.IsListening ~= true then
        return true
    end

    local succeeded = pcall(debugger.stop)
    if succeeded ~= true then
        return false
    end

    LuaDebugger.IsListening = false
    return true
end

-- StartupModuleName 仍使用 require 约定：当工具栏开关开启时，模块加载即开始监听。
LuaDebugger.Start()

return LuaDebugger
