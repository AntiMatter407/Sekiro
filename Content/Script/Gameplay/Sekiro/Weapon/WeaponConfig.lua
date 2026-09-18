-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 集中描述武器资产、角色挂点和原版 TAE 武器模型切换帧；C++ 只消费这些通用参数。

---@class SKWeaponTransitionConfig
---@field AnimationPath string 角色动作的 UE 对象路径。
---@field AdditiveAnimationPath string 以原动画首帧为基准生成的局部空间 Additive 实验资源路径。
---@field EventName string 动画序列中负责提交挂载状态的 WeaponEvent Notify 名称。
---@field SourceFrameRate number 原版 TAE 与导入动画共同使用的采样帧率。
---@field SwitchFrame number 原版 OverrideWeaponModelLocation 事件的挂载边界帧。
---@field SwitchTime number 资产写入和校验使用的秒数，由 SwitchFrame / SourceFrameRate 得到；运行时不轮询该值。
---@field Duration number 导入动画的播放时长，单位为秒。
---@field StartPresentation userdata|number ESKWeaponPresentation 动画开始前必须成立的原生武器展示状态。
---@field TargetPresentation userdata|number ESKWeaponPresentation 边界帧到达后提交给 C++ 的原生武器展示状态。
---@field SlotName string 动画蓝图中承载该动作的上半身 Slot 名称。
---@field BlendInTime number 上半身动作淡入时间，单位为秒。
---@field BlendOutTime number 上半身动作淡出时间，单位为秒。

---@class SKWeaponAttachmentConfig
---@field HandSocket string 刀身拔出时使用的角色右手挂点。
---@field HandBoneFallback string 右手挂点缺失时使用的回退骨骼。
---@field SheathSocket string 刀鞘与收纳刀身使用的角色腰部挂点。

---@class SKWeaponAnimationConfig
---@field Sheathe SKWeaponTransitionConfig 收刀动作与合并事件配置。
---@field Draw SKWeaponTransitionConfig 拔刀动作与分离事件配置。
---@field PrimaryAttack string 第一段基础攻击动画路径。
---@field SecondaryAttack string 第二段基础攻击动画路径。
---@field Guard string 持续招架动画路径。
---@field Deflect string 弹反反馈动画路径。

---@class SKWeaponDefinition
---@field ActorClassPath string 武器蓝图生成类的 UE 对象路径。
---@field InitialPresentation userdata|number ESKWeaponPresentation 武器生成后尚未播放动作时的原生展示状态。
---@field Attachments SKWeaponAttachmentConfig 角色骨架挂点配置。
---@field Animations SKWeaponAnimationConfig 武器相关角色动画与原版切换帧配置。

---@class SKWeaponConfigModule
---@field DefaultWeaponId string 玩家默认武器的稳定配置键。
---@field AutoPlaySheathePreview boolean 是否在 WeaponManager 启动后自动播放一次收刀预览。
---@field RestrictedTransitionMode string 收拔刀模式；Override 强制 Walk，LayeredOverride 保留下半身移动，Additive 使用差值实验资源。
---@field Weapons table<string, SKWeaponDefinition> 稳定武器键到完整配置的映射。

---@type SKWeaponConfigModule
local WeaponConfig = {
    DefaultWeaponId = "Kusabimaru",
    AutoPlaySheathePreview = false,
    -- 使用上半身差值资源，但只在角色落地、站立并收敛到 Walk/Idle 后启动收拔刀。
    RestrictedTransitionMode = "Additive",
    Weapons = {
        Kusabimaru = {
            ActorClassPath = "/Game/Gameplay/BP_Kusabimaru.BP_Kusabimaru_C",
            InitialPresentation = UE.ESKWeaponPresentation.Drawn,
            Attachments = {
                HandSocket = "R_WeaponSocket",
                HandBoneFallback = "R_Hand",
                SheathSocket = "SheathSocket",
            },
            Animations = {
                -- 原版 a00.tae 的 Type 715 从第 14 帧开始把主手模型覆盖到 Dummy 147。
                Sheathe = {
                    AnimationPath = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700500.Anim_Sekiro_a000_700500",
                    AdditiveAnimationPath = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700500_Additive.Anim_Sekiro_a000_700500_Additive",
                    EventName = "Weapon.MergeIntoSheath",
                    SourceFrameRate = 30.0,
                    SwitchFrame = 14,
                    SwitchTime = 14.0 / 30.0,
                    Duration = 2.0,
                    StartPresentation = UE.ESKWeaponPresentation.Drawn,
                    TargetPresentation = UE.ESKWeaponPresentation.Sheathed,
                    SlotName = "DefaultSlot",
                    BlendInTime = 0.12,
                    BlendOutTime = 0.12,
                },
                -- 原版 a00.tae 的 Type 715 在第 7 帧结束，随后主手模型恢复到右手 Dummy 20。
                Draw = {
                    AnimationPath = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700510.Anim_Sekiro_a000_700510",
                    AdditiveAnimationPath = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700510_Additive.Anim_Sekiro_a000_700510_Additive",
                    EventName = "Weapon.SeparateFromSheath",
                    SourceFrameRate = 30.0,
                    SwitchFrame = 7,
                    SwitchTime = 7.0 / 30.0,
                    Duration = 4.0 / 3.0,
                    StartPresentation = UE.ESKWeaponPresentation.Sheathed,
                    TargetPresentation = UE.ESKWeaponPresentation.Drawn,
                    SlotName = "DefaultSlot",
                    BlendInTime = 0.12,
                    BlendOutTime = 0.12,
                },
                PrimaryAttack = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300100.Anim_Sekiro_a050_300100",
                SecondaryAttack = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300110.Anim_Sekiro_a050_300110",
                Guard = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002000.Anim_Sekiro_a050_002000",
                Deflect = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130100.Anim_Sekiro_a050_130100",
            },
        },
    },
}

return WeaponConfig
