-- Lua 类型：纯 Lua 数据模块。本文件集中保存弦一郎 Type 2 弹射物的项目侧资产与调参。
-- 通用 C++ 弹射物不引用这些值；BehaviorTree 动作执行器在动画步骤的弹射物时间点读取本模块。

local GenichiroProjectileProfile = {
    ProjectileClassPath = "/Game/Characters/Genichiro/AI/BP_GenichiroArrowProjectile.BP_GenichiroArrowProjectile_C",
    Speed = 2600.0,
    GravityScale = 0.15,
    Damage = 100.0,
    LifeSeconds = 5.0,
    SpawnForwardOffsetCm = 90.0,
    SpawnHeightOffsetCm = 125.0,
    DefaultCueTimeSeconds = 0.35,
    EventTag = "GenichiroType2",
}

return GenichiroProjectileProfile
