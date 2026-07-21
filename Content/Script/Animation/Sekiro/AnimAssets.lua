-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
local SekiroAnimations = {}

-- 里面动画Forward，Left，Right，Back是相对角色朝向

SekiroAnimations.Locomotion = {
    Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",

    Idle_Forward_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000010.Anim_Sekiro_a000_000010",
    Idle_Back_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000011.Anim_Sekiro_a000_000011",
    Idle_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000012.Anim_Sekiro_a000_000012",
    Idle_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000013.Anim_Sekiro_a000_000013",

    -- Walk Animations

    Walk_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000100.Anim_Sekiro_a000_000100",
    Walk_Back_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000101.Anim_Sekiro_a000_000101",
    Walk_Left_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000102.Anim_Sekiro_a000_000102",
    Walk_Right_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000103.Anim_Sekiro_a000_000103",

    -- Crouch to Walk
    Crouch_Forward_Walk = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000110.Anim_Sekiro_a000_000110",
    Crouch_Back_Walk = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000111.Anim_Sekiro_a000_000111",
    Crouch_Left_Walk = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000112.Anim_Sekiro_a000_000112",
    Crouch_Right_Walk = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000113.Anim_Sekiro_a000_000113",

    IdleToWalk_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000132.Anim_Sekiro_a000_000132",
    IdleToWalk_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000133.Anim_Sekiro_a000_000133",

    Walk_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000200.Anim_Sekiro_a000_000200",
    Walk_Back_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000201.Anim_Sekiro_a000_000201",
    Walk_Left_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000202.Anim_Sekiro_a000_000202",
    Walk_Right_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000203.Anim_Sekiro_a000_000203",

    Walk_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000300.Anim_Sekiro_a000_000300",
    Walk_Back_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000301.Anim_Sekiro_a000_000301",
    Walk_Left_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000302.Anim_Sekiro_a000_000302",
    Walk_Right_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000303.Anim_Sekiro_a000_000303",

    -- Run Animations

    Run_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000400.Anim_Sekiro_a000_000400",
    Run_Back_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000401.Anim_Sekiro_a000_000401",
    Run_Left_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000402.Anim_Sekiro_a000_000402",
    Run_Right_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000403.Anim_Sekiro_a000_000403",

    -- Crouch to Run
    Crouch_Forward_Run = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000410.Anim_Sekiro_a000_000410",
    Crouch_Back_Run = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000411.Anim_Sekiro_a000_000411",
    Crouch_Left_Run = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000412.Anim_Sekiro_a000_000412",
    Crouch_Right_Run = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000413.Anim_Sekiro_a000_000413",

    IdleToRun_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000432.Anim_Sekiro_a000_000432",
    IdleToRun_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000433.Anim_Sekiro_a000_000433",

    Run_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000442.Anim_Sekiro_a000_000442",
    Run_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000443.Anim_Sekiro_a000_000443",

    Run_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000500.Anim_Sekiro_a000_000500",
    Run_Back_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000501.Anim_Sekiro_a000_000501",
    Run_Left_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000502.Anim_Sekiro_a000_000502",
    Run_Right_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000503.Anim_Sekiro_a000_000503",

    Run_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000600.Anim_Sekiro_a000_000600",
    Run_Back_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000601.Anim_Sekiro_a000_000601",
    Run_Left_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000602.Anim_Sekiro_a000_000602",
    Run_Right_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000603.Anim_Sekiro_a000_000603",

    -- Crouch Locomotion

    Crouch_Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005000.Anim_Sekiro_a000_005000",

    -- Stand to Crouch
    Stand_Crouch_Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_216000.Anim_Sekiro_a000_216000",
    -- Crouch to Stand
    Crouch_Stand_Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_216100.Anim_Sekiro_a000_216100",

    Sprint_Left_Crouch = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_216020.Anim_Sekiro_a000_216020",
    Sprint_Right_Crouch = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_216021.Anim_Sekiro_a000_216021",

    Crouch_Idle_Forward_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005010.Anim_Sekiro_a000_005010",
    Crouch_Idle_Back_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005011.Anim_Sekiro_a000_005011",
    Crouch_Idle_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005012.Anim_Sekiro_a000_005012",
    Crouch_Idle_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005013.Anim_Sekiro_a000_005013",

    Crouch_Walk_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005100.Anim_Sekiro_a000_005100",
    Crouch_Walk_Back_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005101.Anim_Sekiro_a000_005101",
    Crouch_Walk_Left_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005102.Anim_Sekiro_a000_005102",
    Crouch_Walk_Right_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005103.Anim_Sekiro_a000_005103",

    Crouch_Walk_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005200.Anim_Sekiro_a000_005200",
    Crouch_Walk_Back_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005201.Anim_Sekiro_a000_005201",
    Crouch_Walk_Left_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005202.Anim_Sekiro_a000_005202",
    Crouch_Walk_Right_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005203.Anim_Sekiro_a000_005203",

    Crouch_Walk_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005300.Anim_Sekiro_a000_005300",
    Crouch_Walk_Back_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005301.Anim_Sekiro_a000_005301",
    Crouch_Walk_Left_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005302.Anim_Sekiro_a000_005302",
    Crouch_Walk_Right_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005303.Anim_Sekiro_a000_005303",

    Crouch_Run_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005400.Anim_Sekiro_a000_005400",
    Crouch_Run_Back_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005401.Anim_Sekiro_a000_005401",
    Crouch_Run_Left_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005402.Anim_Sekiro_a000_005402",
    Crouch_Run_Right_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005403.Anim_Sekiro_a000_005403",

    Crouch_Run_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005500.Anim_Sekiro_a000_005500",
    Crouch_Run_Back_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005501.Anim_Sekiro_a000_005501",
    Crouch_Run_Left_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005502.Anim_Sekiro_a000_005502",
    Crouch_Run_Right_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005503.Anim_Sekiro_a000_005503",

    Crouch_Run_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005600.Anim_Sekiro_a000_005600",
    Crouch_Run_Back_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005601.Anim_Sekiro_a000_005601",
    Crouch_Run_Left_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005602.Anim_Sekiro_a000_005602",
    Crouch_Run_Right_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005603.Anim_Sekiro_a000_005603",

    -- Step Dodge

    Step_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213301.Anim_Sekiro_a000_213301",
    Step_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213302.Anim_Sekiro_a000_213302",
    Step_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213303.Anim_Sekiro_a000_213303",
    Step_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213304.Anim_Sekiro_a000_213304",

    -- Sprint Start from Stand or Crouch

    Sprint_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001151.Anim_Sekiro_a000_001151",
    Sprint_Back_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001152.Anim_Sekiro_a000_001152",
    Sprint_Left_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001153.Anim_Sekiro_a000_001153",
    Sprint_Right_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001154.Anim_Sekiro_a000_001154",

    Sprint_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001200.Anim_Sekiro_a000_001200",

    Sprint_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001510.Anim_Sekiro_a000_001510",
    Sprint_Forward_Left_Turn_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001511.Anim_Sekiro_a000_001511",
    Sprint_Forward_Right_Turn_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001512.Anim_Sekiro_a000_001512",

}

-- Jump 动画按“原地”和“方向”两组组织。
-- 方向后缀相对角色朝向：锁定模式直接使用八方向资源，非锁定模式转向输入方向并使用 Forward 资源。
SekiroAnimations.Jump = {
    -- 非锁定
    Stand_Jump_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_200000.Anim_Sekiro_a000_200000",
    Crouch_Jump_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_200100.Anim_Sekiro_a000_200100",
    Jump_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201030.Anim_Sekiro_a000_201030",
    Jump_Light_Stand = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201040.Anim_Sekiro_a000_201040",
    Jump_Heavy_Stand = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_200021.Anim_Sekiro_a000_200021",
    Jump_Heavy_Crouch = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_200121.Anim_Sekiro_a000_200121",
    Jump_Unlock_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201010.Anim_Sekiro_a000_201010",

    -- 方向 Start/Land 原始资产包含 RootMotion；InAir 原始资产没有 RootMotion，空中轨迹由 CharacterMovement 物理计算。
    Jump_Start_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201100.Anim_Sekiro_a000_201100",
    Jump_Start_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201101.Anim_Sekiro_a000_201101",
    Jump_Start_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201102.Anim_Sekiro_a000_201102",
    Jump_Start_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201103.Anim_Sekiro_a000_201103",
    Jump_Start_ForwardLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201104.Anim_Sekiro_a000_201104",
    Jump_Start_ForwardRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201105.Anim_Sekiro_a000_201105",
    Jump_Start_BackLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201106.Anim_Sekiro_a000_201106",
    Jump_Start_BackRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201107.Anim_Sekiro_a000_201107",

    Jump_InAir_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201110.Anim_Sekiro_a000_201110",
    Jump_InAir_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201111.Anim_Sekiro_a000_201111",
    Jump_InAir_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201112.Anim_Sekiro_a000_201112",
    Jump_InAir_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201113.Anim_Sekiro_a000_201113",
    Jump_InAir_ForwardLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201114.Anim_Sekiro_a000_201114",
    Jump_InAir_ForwardRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201115.Anim_Sekiro_a000_201115",
    Jump_InAir_BackLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201116.Anim_Sekiro_a000_201116",
    Jump_InAir_BackRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201117.Anim_Sekiro_a000_201117",

    Jump_Land_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201140.Anim_Sekiro_a000_201140",
    Jump_Land_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201141.Anim_Sekiro_a000_201141",
    Jump_Land_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201142.Anim_Sekiro_a000_201142",
    Jump_Land_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201143.Anim_Sekiro_a000_201143",
    Jump_Land_ForwardLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201144.Anim_Sekiro_a000_201144",
    Jump_Land_ForwardRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201145.Anim_Sekiro_a000_201145",
    Jump_Land_BackLeft = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201146.Anim_Sekiro_a000_201146",
    Jump_Land_BackRight = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201147.Anim_Sekiro_a000_201147",

}

-- 人工核验的武器切换动画。
-- 资源语义以 Saved/AnimationTemp-Attack.txt 为准，不使用旧自动分类脚本中的推测名称。
SekiroAnimations.Weapon = {
    Sheathe = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700500.Anim_Sekiro_a000_700500",
    Draw = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_700510.Anim_Sekiro_a000_700510",
}

-- 人工核验的招架姿势、移动、受击中断和空中招架动画。
SekiroAnimations.Guard = {
    Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002000.Anim_Sekiro_a050_002000",
    Move_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002200.Anim_Sekiro_a050_002200",
    Move_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002201.Anim_Sekiro_a050_002201",
    Move_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002202.Anim_Sekiro_a050_002202",
    Move_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_002203.Anim_Sekiro_a050_002203",
    Interrupted = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_120100.Anim_Sekiro_a050_120100",
    Interrupted_Heavy = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_120200.Anim_Sekiro_a050_120200",

    Raise = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203000.Anim_Sekiro_a050_203000",
    Shake = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203005.Anim_Sekiro_a050_203005",
    Lower = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203010.Anim_Sekiro_a050_203010",

    Air_Raise = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203300.Anim_Sekiro_a050_203300",
    Air_Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203310.Anim_Sekiro_a050_203310",
    Air_Lower = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_203320.Anim_Sekiro_a050_203320",
}

-- 人工核验的弹反分型动画；Stage 表示同一弹反类型内的连续阶段。
SekiroAnimations.Deflect = {
    Type_01_Stage_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130100.Anim_Sekiro_a050_130100",
    Type_01_Stage_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130101.Anim_Sekiro_a050_130101",
    Type_01_Stage_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130102.Anim_Sekiro_a050_130102",
    Type_02_Stage_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130200.Anim_Sekiro_a050_130200",
    Type_02_Stage_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130201.Anim_Sekiro_a050_130201",
    Type_02_Stage_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130202.Anim_Sekiro_a050_130202",
    Type_03_Stage_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130300.Anim_Sekiro_a050_130300",
    Type_03_Stage_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130301.Anim_Sekiro_a050_130301",
    Type_03_Stage_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130302.Anim_Sekiro_a050_130302",
    Type_04_Stage_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_130700.Anim_Sekiro_a050_130700",
}

-- 人工核验的基础攻击动画。
-- Combo_01~03 是短按连段；Charged_Thrust_* 只用于长按攻击，不能打断已经开始的短按连段。
SekiroAnimations.Attack = {
    Charged_Thrust_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300000.Anim_Sekiro_a050_300000",
    Charged_Thrust_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300001.Anim_Sekiro_a050_300001",
    Combo_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300020.Anim_Sekiro_a050_300020",
    Combo_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300030.Anim_Sekiro_a050_300030",
    Combo_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300040.Anim_Sekiro_a050_300040",

    Air_Combo_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308000.Anim_Sekiro_a050_308000",
    Air_Combo_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308010.Anim_Sekiro_a050_308010",
    Air_Combo_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308020.Anim_Sekiro_a050_308020",

    Land_Combo_01 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308050.Anim_Sekiro_a050_308050",
    Land_Combo_02 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308060.Anim_Sekiro_a050_308060",
    Land_Combo_03 = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_308070.Anim_Sekiro_a050_308070",

    Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300100.Anim_Sekiro_a050_300100",
    Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a050_300110.Anim_Sekiro_a050_300110",
}

-- ShowDebug Animation 使用 UObject 短名输出当前 Sequence Player 资产。
-- 这里反向建立索引，使 C++ 调试宿主无需硬编码项目的 Lua 表结构。
local lua_asset_name_by_native_name = {}

---将一组 Lua 动画资产注册到 UObject 短名反向索引。
---@param group_name string Lua 资产分组名，例如 Locomotion。
---@param group_assets table<string, string> 资产键到 UE 对象路径的映射表。
---@return nil 无返回值，仅更新模块内部的反向索引。
local function register_lua_asset_names(group_name, group_assets)
    for asset_name, asset_path in pairs(group_assets) do
        local native_asset_name = asset_path:match("%.([^%.]+)$")
        if native_asset_name ~= nil and lua_asset_name_by_native_name[native_asset_name] == nil then
            lua_asset_name_by_native_name[native_asset_name] = string.format(
                "AnimAssets.%s.%s",
                group_name,
                asset_name
            )
        end
    end
end

register_lua_asset_names("Locomotion", SekiroAnimations.Locomotion)
register_lua_asset_names("Jump", SekiroAnimations.Jump)
register_lua_asset_names("Weapon", SekiroAnimations.Weapon)
register_lua_asset_names("Guard", SekiroAnimations.Guard)
register_lua_asset_names("Deflect", SekiroAnimations.Deflect)
register_lua_asset_names("Attack", SekiroAnimations.Attack)

---查询 ShowDebug Animation 中原生动画资产对应的 Lua 语义名。
---@param native_asset_name string Sequence Player 输出的 UObject 短名。
---@return string|nil lua_asset_name 已注册时返回 AnimAssets.<Group>.<Key>，否则返回 nil。
function SekiroAnimations.GetLuaAssetName(native_asset_name)
    return lua_asset_name_by_native_name[native_asset_name]
end

return SekiroAnimations
