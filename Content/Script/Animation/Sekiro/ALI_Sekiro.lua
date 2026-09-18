-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua，不绑定运行时 AnimInstance。
-- 声明 Sekiro 动画蓝图的 ALS V4 风格 Animation Layer Interface；实现逻辑由 ABP_Sekiro 提供。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")

local PoseLayerParameters = {
    {
        Name = "SourcePose",
        DataType = "Pose",
        bIsPose = true,
    },
}

---@class ALI_Sekiro: LuaAnimBlueprint
local ALI_Sekiro = LuaAnimBlueprint:Extend("ALI_Sekiro", {
    BlueprintKind = UE.ELuaAnimIRBlueprintKind.AnimationLayerInterface,
    SourceModule = "Animation.Sekiro.ALI_Sekiro",
    ParentAnimInstanceClass = "",
    TargetSkeleton = "",
})

---声明项目动画蓝图可覆写的职责层签名；接口不包含任何姿势实现。
---@return nil result 各签名会生成到原生 Animation Layer Interface 资产。
function ALI_Sekiro:DeclareAnimationLayers()
    self:AnimLayer("BasePoses", {})
    self:AnimLayer("BaseLayer", {
        Parameters = PoseLayerParameters,
    })
    self:AnimLayer("OverlayLayer", {
        Parameters = PoseLayerParameters,
    })
    self:AnimLayer("LayerBlending", {
        Parameters = PoseLayerParameters,
    })
    self:AnimLayer("FootIK", {
        Parameters = PoseLayerParameters,
    })
end

return ALI_Sekiro:Export()
