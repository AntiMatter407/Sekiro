-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，仅集中管理新系统使用的动画资产路径。
-- 本文件只登记已经通过当前素材准入门禁的资源；数据库分组和 SamplingRange 由配置文件负责。

local MotionMatchingAnimAssets = {}

MotionMatchingAnimAssets.PoseSearch = {
    Skeleton =
        "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
    LocomotionSchema =
        "/Game/Characters/SekiroMotionMatching/PoseSearch/PSS_SekiroFreeRun_Minimal.PSS_SekiroFreeRun_Minimal",
    AirborneSchema =
        "/Game/Characters/SekiroMotionMatching/PoseSearch/Schemas/PSS_Airborne.PSS_Airborne",
    DatabaseTemplate =
        "/Game/Characters/SekiroMotionMatching/PoseSearch/PSD_SekiroFreeRun_Minimal.PSD_SekiroFreeRun_Minimal",
    Databases = {
        Stationary =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Run/PSD_FreeRun_Stationary.PSD_FreeRun_Stationary",
        WalkStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Walk/PSD_FreeWalk_Starts.PSD_FreeWalk_Starts",
        WalkLoops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Walk/PSD_FreeWalk_Loops.PSD_FreeWalk_Loops",
        WalkStops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Walk/PSD_FreeWalk_Stops.PSD_FreeWalk_Stops",
        RunStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Run/PSD_FreeRun_Starts.PSD_FreeRun_Starts",
        RunLoops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Run/PSD_FreeRun_Loops.PSD_FreeRun_Loops",
        RunStops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Run/PSD_FreeRun_Stops.PSD_FreeRun_Stops",
        SprintStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Sprint/PSD_FreeSprint_Starts.PSD_FreeSprint_Starts",
        SprintLoops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Sprint/PSD_FreeSprint_Loops.PSD_FreeSprint_Loops",
        SprintStops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Sprint/PSD_FreeSprint_Stops.PSD_FreeSprint_Stops",
        SprintPivots =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Free/Sprint/PSD_FreeSprint_Pivots.PSD_FreeSprint_Pivots",
        CrouchStationary =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/PSD_Crouching_Stationary.PSD_Crouching_Stationary",
        CrouchWalkStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/Walk/PSD_CrouchWalk_Starts.PSD_CrouchWalk_Starts",
        CrouchWalkLoops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/Walk/PSD_CrouchWalk_Loops.PSD_CrouchWalk_Loops",
        CrouchWalkStops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/Walk/PSD_CrouchWalk_Stops.PSD_CrouchWalk_Stops",
        CrouchRunStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/PSD_Crouching_Starts.PSD_Crouching_Starts",
        CrouchRunLoops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/PSD_Crouching_Loops.PSD_Crouching_Loops",
        CrouchRunStops =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Crouching/PSD_Crouching_Stops.PSD_Crouching_Stops",
        JumpStarts =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Airborne/PSD_Airborne_JumpStarts.PSD_Airborne_JumpStarts",
        InAirPoseOnly =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Airborne/PSD_Airborne_InAirPoseOnly.PSD_Airborne_InAirPoseOnly",
        Landing =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/Databases/Airborne/PSD_Airborne_Landing.PSD_Airborne_Landing",
    },
    NormalizationSets = {
        Locomotion =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/NormalizationSets/PSN_FreeRun_Locomotion.PSN_FreeRun_Locomotion",
        Airborne =
            "/Game/Characters/SekiroMotionMatching/PoseSearch/NormalizationSets/PSN_Airborne.PSN_Airborne",
    },
}

MotionMatchingAnimAssets.Chooser = {
    LocomotionDatabases =
        "/Game/Characters/SekiroMotionMatching/Choosers/CHT_LocomotionDatabases.CHT_LocomotionDatabases",
}

-- Standing 与 Crouching 候选仍按连续 Trajectory/Pose 特征排序，方向名仅用于素材追踪。
-- Run_Forward_Loop 已通过此前最小组运行验证；本轮自动植脚指标的复核不会撤销该既有基线。
MotionMatchingAnimAssets.Locomotion = {
    Idle =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",
    Crouch_Idle =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005000.Anim_Sekiro_a000_005000",

    Walk_Forward_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000100.Anim_Sekiro_a000_000100",
    Walk_Back_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000101.Anim_Sekiro_a000_000101",
    Walk_Left_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000102.Anim_Sekiro_a000_000102",
    Walk_Right_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000103.Anim_Sekiro_a000_000103",
    Walk_Forward_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000200.Anim_Sekiro_a000_000200",
    Walk_Back_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000201.Anim_Sekiro_a000_000201",
    Walk_Left_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000202.Anim_Sekiro_a000_000202",
    Walk_Right_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000203.Anim_Sekiro_a000_000203",
    Walk_Forward_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000300.Anim_Sekiro_a000_000300",
    Walk_Back_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000301.Anim_Sekiro_a000_000301",
    Walk_Left_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000302.Anim_Sekiro_a000_000302",
    Walk_Right_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000303.Anim_Sekiro_a000_000303",

    Run_Forward_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000400.Anim_Sekiro_a000_000400",
    Run_Back_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000401.Anim_Sekiro_a000_000401",
    Run_Left_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000402.Anim_Sekiro_a000_000402",
    Run_Right_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000403.Anim_Sekiro_a000_000403",
    Run_Forward_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000500.Anim_Sekiro_a000_000500",
    Run_Back_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000501.Anim_Sekiro_a000_000501",
    Run_Left_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000502.Anim_Sekiro_a000_000502",
    Run_Right_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000503.Anim_Sekiro_a000_000503",
    Run_Forward_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000600.Anim_Sekiro_a000_000600",
    Run_Back_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000601.Anim_Sekiro_a000_000601",
    Run_Left_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000602.Anim_Sekiro_a000_000602",
    Run_Right_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000603.Anim_Sekiro_a000_000603",

    Crouch_Walk_Forward_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005100.Anim_Sekiro_a000_005100",
    Crouch_Walk_Back_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005101.Anim_Sekiro_a000_005101",
    Crouch_Walk_Left_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005102.Anim_Sekiro_a000_005102",
    Crouch_Walk_Right_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005103.Anim_Sekiro_a000_005103",
    Crouch_Walk_Forward_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005200.Anim_Sekiro_a000_005200",
    Crouch_Walk_Back_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005201.Anim_Sekiro_a000_005201",
    Crouch_Walk_Left_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005202.Anim_Sekiro_a000_005202",
    Crouch_Walk_Right_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005203.Anim_Sekiro_a000_005203",
    Crouch_Walk_Forward_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005300.Anim_Sekiro_a000_005300",
    Crouch_Walk_Back_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005301.Anim_Sekiro_a000_005301",
    Crouch_Walk_Left_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005302.Anim_Sekiro_a000_005302",
    Crouch_Walk_Right_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005303.Anim_Sekiro_a000_005303",

    Crouch_Run_Forward_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005400.Anim_Sekiro_a000_005400",
    Crouch_Run_Back_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005401.Anim_Sekiro_a000_005401",
    Crouch_Run_Left_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005402.Anim_Sekiro_a000_005402",
    Crouch_Run_Right_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005403.Anim_Sekiro_a000_005403",
    Crouch_Run_Forward_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005500.Anim_Sekiro_a000_005500",
    Crouch_Run_Back_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005501.Anim_Sekiro_a000_005501",
    Crouch_Run_Left_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005502.Anim_Sekiro_a000_005502",
    Crouch_Run_Right_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005503.Anim_Sekiro_a000_005503",
    Crouch_Run_Forward_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005600.Anim_Sekiro_a000_005600",
    Crouch_Run_Back_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005601.Anim_Sekiro_a000_005601",
    Crouch_Run_Left_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005602.Anim_Sekiro_a000_005602",
    Crouch_Run_Right_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_005603.Anim_Sekiro_a000_005603",

    Sprint_Forward_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001151.Anim_Sekiro_a000_001151",
    Sprint_Back_Turn_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001152.Anim_Sekiro_a000_001152",
    Sprint_Left_Turn_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001153.Anim_Sekiro_a000_001153",
    Sprint_Right_Turn_Start =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001154.Anim_Sekiro_a000_001154",
    Sprint_Forward_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001200.Anim_Sekiro_a000_001200",
    Sprint_Forward_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001510.Anim_Sekiro_a000_001510",
    Sprint_Forward_Left_Turn_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001511.Anim_Sekiro_a000_001511",
    Sprint_Forward_Right_Turn_Stop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001512.Anim_Sekiro_a000_001512",
    Sprint_Left_180_Pivot =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001402.Anim_Sekiro_a000_001402",
    Sprint_Right_180_Pivot =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001403.Anim_Sekiro_a000_001403",
}

-- JumpStart/Landing 负责各自 RootMotion；InAirPoseOnly 只提供空中姿势，水平位移继承 CMC 实际惯性。
MotionMatchingAnimAssets.Jump = {
    Start_Forward =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201100.Anim_Sekiro_a000_201100",
    Start_Back =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201101.Anim_Sekiro_a000_201101",
    Start_Left =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201102.Anim_Sekiro_a000_201102",
    Start_Right =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201103.Anim_Sekiro_a000_201103",
    Start_ForwardLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201104.Anim_Sekiro_a000_201104",
    Start_ForwardRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201105.Anim_Sekiro_a000_201105",
    Start_BackLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201106.Anim_Sekiro_a000_201106",
    Start_BackRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201107.Anim_Sekiro_a000_201107",

    InAir_Forward =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201110.Anim_Sekiro_a000_201110",
    InAir_Back =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201111.Anim_Sekiro_a000_201111",
    InAir_Left =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201112.Anim_Sekiro_a000_201112",
    InAir_Right =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201113.Anim_Sekiro_a000_201113",
    InAir_ForwardLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201114.Anim_Sekiro_a000_201114",
    InAir_ForwardRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201115.Anim_Sekiro_a000_201115",
    InAir_BackLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201116.Anim_Sekiro_a000_201116",
    InAir_BackRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201117.Anim_Sekiro_a000_201117",
    InAir_Loop =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201030.Anim_Sekiro_a000_201030",

    Land_Forward =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201140.Anim_Sekiro_a000_201140",
    Land_Back =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201141.Anim_Sekiro_a000_201141",
    Land_Left =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201142.Anim_Sekiro_a000_201142",
    Land_Right =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201143.Anim_Sekiro_a000_201143",
    Land_ForwardLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201144.Anim_Sekiro_a000_201144",
    Land_ForwardRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201145.Anim_Sekiro_a000_201145",
    Land_BackLeft =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201146.Anim_Sekiro_a000_201146",
    Land_BackRight =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_201147.Anim_Sekiro_a000_201147",
}

-- 查询尚未建立时仍输出可辨识的有效动画，避免 Reference Pose 掩盖 AnimGraph 是否正在求值。
MotionMatchingAnimAssets.Fallback = {
    Idle =
        "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",
}

return MotionMatchingAnimAssets
