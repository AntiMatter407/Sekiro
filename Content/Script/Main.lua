-- Lua 类型：UnLua UObject 运行时类；self 表示真实 UObject，可直接访问其反射属性与函数。
--
-- DESCRIPTION
--
-- @COMPANY **
-- @AUTHOR **
-- @DATE ${date} ${time}
--
-- Main.lua 只在游戏世界启动后加载；编辑器调试开关关闭时，这里才开放 9966 端口。
-- 开关开启时模块已被 require，Start 会安全保持现有监听，不重复绑定端口。
---@type LuaDebuggerModule
local LuaDebugger = require("Debug.LuaDebugger")
LuaDebugger.Start()

---@type BP_SKGameState_C
local M = UnLua.Class()

-- function M:Initialize(Initializer)
-- end

-- function M:UserConstructionScript()
-- end

-- function M:ReceiveBeginPlay()
-- end

-- function M:ReceiveEndPlay()
-- end

-- function M:ReceiveTick(DeltaSeconds)
-- end

-- function M:ReceiveAnyDamage(Damage, DamageType, InstigatedBy, DamageCauser)
-- end

-- function M:ReceiveActorBeginOverlap(OtherActor)
-- end

-- function M:ReceiveActorEndOverlap(OtherActor)
-- end

return M
