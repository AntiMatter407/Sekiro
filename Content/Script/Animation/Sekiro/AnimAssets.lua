local SekiroAnimations = {}

SekiroAnimations.Locomotion = {
    Idle = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000",

    -- Walk Animations

    Walk_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000100.Anim_Sekiro_a000_000100",
    Walk_Back_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000101.Anim_Sekiro_a000_000101",
    Walk_Left_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000102.Anim_Sekiro_a000_000102",
    Walk_Right_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000103.Anim_Sekiro_a000_000103",

    Walk_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000132.Anim_Sekiro_a000_000132",
    Walk_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000133.Anim_Sekiro_a000_000133",

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

    Run_Left_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000432.Anim_Sekiro_a000_000432",
    Run_Right_Turn = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000433.Anim_Sekiro_a000_000433",

    Run_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000500.Anim_Sekiro_a000_000500",
    Run_Back_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000501.Anim_Sekiro_a000_000501",
    Run_Left_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000502.Anim_Sekiro_a000_000502",
    Run_Right_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000503.Anim_Sekiro_a000_000503",

    Run_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000600.Anim_Sekiro_a000_000600",
    Run_Back_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000601.Anim_Sekiro_a000_000601",
    Run_Left_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000602.Anim_Sekiro_a000_000602",
    Run_Right_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000603.Anim_Sekiro_a000_000603",

    -- Step Dodge

    Step_Forward = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213301.Anim_Sekiro_a000_213301",
    Step_Left = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213302.Anim_Sekiro_a000_213302",
    Step_Right = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213303.Anim_Sekiro_a000_213303",
    Step_Back = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_213304.Anim_Sekiro_a000_213304",

    -- Walk/Run Blend

    Walk_Run_Blend_Forward_1D = "/Game/Characters/Sekiro/Anim/Sekiro_CycleForward1D.Sekiro_CycleForward1D",
    Walk_Run_Blend_Back_1D = "/Game/Characters/Sekiro/Anim/Sekiro_CycleBack1D.Sekiro_CycleBack1D",
    Walk_Run_Blend_Left_1D = "/Game/Characters/Sekiro/Anim/Sekiro_CycleLeft1D.Sekiro_CycleLeft1D",
    Walk_Run_Blend_Right_1D = "/Game/Characters/Sekiro/Anim/Sekiro_CycleRight1D.Sekiro_CycleRight1D",

    -- Sprint

    Sprint_Forward_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001151.Anim_Sekiro_a000_001151",
    Sprint_Back_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001152.Anim_Sekiro_a000_001152",
    Sprint_Left_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001153.Anim_Sekiro_a000_001153",
    Sprint_Right_Turn_Start = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001154.Anim_Sekiro_a000_001154",

    Sprint_Forward_Loop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001200.Anim_Sekiro_a000_001200",

    Sprint_Forward_Stop = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001510.Anim_Sekiro_a000_001510",
    Sprint_Forward_To_Run = "/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_001512.Anim_Sekiro_a000_001512",
}

return SekiroAnimations
