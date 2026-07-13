-- 历史 Turn 资源映射，仅保留为资产语义和后续动作研究参考。
-- 当前 GroundLocomotion 不导入本文件；角色转向由 Movement 完成，Sprint 大角度使用 Forward Stop/Start。
--
-- 方案摘要：
-- 1. Turn 不是 Walk、Run 或 Sprint 的子类型，而是一次独立的“开始状态 -> 结束状态”动作。
-- 2. 资源只负责表现转身时的身体动作和原始 RootMotion 平移，不负责改变角色最终朝向。
-- 3. GroundLocomotion 会在进入 Turn 时锁定起止状态、转向角和动画，插件再按动画进度合成旋转。
-- 4. 表中没有配置的状态组合表示项目当前没有可信的专用动画，调用方必须走明确回退，禁止按名字拼接资源。
-- 5. RotationDuration 是角色完成真实朝向的秒数；MaximumPlayTime 是曲线缺失时动作最晚退出的秒数，两者均独立于动画总长度。
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local Anim = AnimAssets.Locomotion
local TurnProfiles = {}

TurnProfiles.Mode = {
    Idle = "Idle",
    Walk = "Walk",
    Run = "Run",
    Sprint = "Sprint",
}

TurnProfiles.Direction = {
    Forward = "Forward",
    Back = "Back",
    Left = "Left",
    Right = "Right",
}

local Mode = TurnProfiles.Mode
local Direction = TurnProfiles.Direction

-- 第一层键是动作开始前的 Locomotion 状态，第二层键是动作完成后要进入的状态。
-- 方向键表示目标朝向相对当前角色朝向的角度区间；只有左右资源时，Back 会由规划器按转向侧回退到 Left/Right。
TurnProfiles.Profiles = {
    [Mode.Idle] = {
        [Mode.Idle] = {
            RotationDuration = 0.22,
            MaximumPlayTime = 0.34,
            [Direction.Forward] = Anim.Idle_Forward_Turn,
            [Direction.Back] = Anim.Idle_Back_Turn,
            [Direction.Left] = Anim.Idle_Left_Turn,
            [Direction.Right] = Anim.Idle_Right_Turn,
        },
        [Mode.Walk] = {
            RotationDuration = 0.30,
            MaximumPlayTime = 0.55,
            [Direction.Left] = Anim.IdleToWalk_Left_Turn,
            [Direction.Right] = Anim.IdleToWalk_Right_Turn,
        },
        [Mode.Run] = {
            RotationDuration = 0.24,
            MaximumPlayTime = 0.44,
            [Direction.Left] = Anim.IdleToRun_Left_Turn,
            [Direction.Right] = Anim.IdleToRun_Right_Turn,
        },
        [Mode.Sprint] = {
            RotationDuration = 0.20,
            MaximumPlayTime = 0.36,
            [Direction.Forward] = Anim.Sprint_Forward_Start,
            [Direction.Back] = Anim.Sprint_Back_Turn_Start,
            [Direction.Left] = Anim.Sprint_Left_Turn_Start,
            [Direction.Right] = Anim.Sprint_Right_Turn_Start,
        },
    },
    [Mode.Walk] = {
        -- Walk 没有 Walk -> Walk 专用 Turn。小角度由 SteerToTarget 修正，大角度后续可扩展 Stop -> IdleToWalk。
        [Mode.Sprint] = {
            RotationDuration = 0.20,
            MaximumPlayTime = 0.36,
            [Direction.Forward] = Anim.Sprint_Forward_Start,
            [Direction.Back] = Anim.Sprint_Back_Turn_Start,
            [Direction.Left] = Anim.Sprint_Left_Turn_Start,
            [Direction.Right] = Anim.Sprint_Right_Turn_Start,
        },
    },
    [Mode.Run] = {
        [Mode.Run] = {
            RotationDuration = 0.22,
            MaximumPlayTime = 0.40,
            [Direction.Left] = Anim.Run_Left_Turn,
            [Direction.Right] = Anim.Run_Right_Turn,
        },
        [Mode.Sprint] = {
            RotationDuration = 0.18,
            MaximumPlayTime = 0.34,
            [Direction.Forward] = Anim.Sprint_Forward_Start,
            [Direction.Back] = Anim.Sprint_Back_Turn_Start,
            [Direction.Left] = Anim.Sprint_Left_Turn_Start,
            [Direction.Right] = Anim.Sprint_Right_Turn_Start,
        },
    },
    [Mode.Sprint] = {
        -- Sprint 只有前向循环。退出 Sprint 时先播放 Stop/TurnStop，再进入 Idle、Walk 或 Run。
        [Mode.Idle] = {
            RotationDuration = 0.22,
            MaximumPlayTime = 0.42,
            [Direction.Forward] = Anim.Sprint_Forward_Stop,
            [Direction.Left] = Anim.Sprint_Forward_Left_Turn_Stop,
            [Direction.Right] = Anim.Sprint_Forward_Right_Turn_Stop,
        },
        [Mode.Walk] = {
            RotationDuration = 0.22,
            MaximumPlayTime = 0.42,
            [Direction.Forward] = Anim.Sprint_Forward_Stop,
            [Direction.Left] = Anim.Sprint_Forward_Left_Turn_Stop,
            [Direction.Right] = Anim.Sprint_Forward_Right_Turn_Stop,
        },
        [Mode.Run] = {
            RotationDuration = 0.22,
            MaximumPlayTime = 0.42,
            [Direction.Forward] = Anim.Sprint_Forward_Stop,
            [Direction.Left] = Anim.Sprint_Forward_Left_Turn_Stop,
            [Direction.Right] = Anim.Sprint_Forward_Right_Turn_Stop,
        },
    },
}

return TurnProfiles
