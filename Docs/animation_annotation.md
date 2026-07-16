# 只狼动画注释对照表

> 来源：animation_annotation.csv
> 说明：前缀为 a000 代表基础移动；a050 代表受击反应；a070 代表忍义手；a100 代表战斗；a200+
>       为特殊互动/Boss 战。

## 一、基础移动（a000）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a000 | 0 | idle | `Anim_Sekiro_Idle_Default` |
|  | 10~13 | 似乎是走路停止 | `Anim_Sekiro_Walk_Stop_*` |
|  | 100-103 | idle 起步到走路的过渡 | `Anim_Sekiro_Idle_ToWalk_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 110-113 | 深蹲到前后左右的走路 | `Anim_Sekiro_CrouchWalk_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 120-123 | 浅蹲到前后左右的走路 | `Anim_Sekiro_ShortCrouchWalk_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 132-133 | 站立状态左右转向起步动画，不是 Walk 循环中的转向动画 | `Anim_Sekiro_Stand_TurnStart_L` / `_R` |
|  | 200-203 | 前后左右的走路动画,和100-103差不多 | `Anim_Sekiro_WalkAlt_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 300-303 | 前后左右的走路到停止站立 | `Anim_Sekiro_Walk_Fwd_Stop` / `_Bwd_Stop` / `_L_Stop` / `_R_Stop` |
|  | 400-403 | 站立到前后左右跑步的起步过渡，不是循环动画 | `Anim_Sekiro_Stand_ToRun_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 410-413 | 深蹲到前后左右跑步的起步过渡 | `Anim_Sekiro_Crouch_ToRun_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 420-423 | 浅蹲到前后左右跑步的起步过渡 | `Anim_Sekiro_ShortCrouch_ToRun_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 432-433 | 站立状态左右转向到前向 Run 的起步过渡，不是 Run 循环中的转向动画 | `Anim_Sekiro_Stand_TurnToRun_L` / `_R` |
|  | 442-443 | 从后向按左/右方向转到前向 Run 的起步过渡，不是 Run 循环中的转向动画 | `Anim_Sekiro_Back_TurnToRun_L` / `_R` |
|  | 500-503 | 前后左右跑步循环 | `Anim_Sekiro_Run_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 600-603 | 前后左右跑步到停止 | `Anim_Sekiro_Run_Fwd_Stop` / `_Bwd_Stop` / `_L_Stop` / `_R_Stop` |
|  | 1151-1154 | 不同方向开始朝前冲刺 | `Anim_Sekiro_Sprint_Fwd` / `_Fwd_L` / `_Fwd_R` / `_Fwd_Bwd` |
|  | 1200 | 冲刺动画 | `Anim_Sekiro_Sprint_Loop` |
|  | 1402-1403 | 左右急停 | `Anim_Sekiro_Sprint_Fwd_Stop` / `_Bwd_Stop` |
|  | 1410 | 冲刺动画（只有几帧） | `Anim_Sekiro_Sprint_Burst` |
|  | 1500 | 未知 | — |
|  | 1510-1512 | 冲刺降速/急停过渡 | `Anim_Sekiro_Sprint_To_Idle` / `_Transition_1511` / `_To_Run` |
|  | 5000 | 蹲 | `Anim_Sekiro_Crouch_Idle` |
|  | 5010-5013 | 不同方向的蹲到蹲走 | `Anim_Sekiro_Crouch_ToWalk_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5100-5103 | 不同方向的开始蹲走 | `Anim_Sekiro_Crouch_WalkStart_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5110-5113 | 不同方向开始蹲走 | `Anim_Sekiro_Crouch_WalkStart2_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5200-5203 | 不同方向蹲走 | `Anim_Sekiro_Crouch_WalkLoop_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5300-5303 | 不同反向蹲走到蹲 | `Anim_Sekiro_Crouch_WalkToIdle_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5400-5403 | 不同方向的蹲到蹲跑 | `Anim_Sekiro_Crouch_RunStart_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5410-5413 | 不同方向的蹲到蹲跑 | `Anim_Sekiro_Crouch_RunStart2_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5500-5503 | 不同方向蹲跑 | `Anim_Sekiro_Crouch_RunLoop_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 5600-5603 | 不同方向蹲跑到停止 | `Anim_Sekiro_Crouch_RunStop_Fwd` / `_Bwd` / `_L` / `_R` |
|  | 20000 | 腿放在的悬挂 | `Anim_Sekiro_Hang_Idle` |
|  | 20010 | 腿曲着悬挂 | `Anim_Sekiro_Hang_CrouchIdle` |
|  | 20102-20103/20112-20113 | 腿放在/曲着悬挂左右移动 | `Anim_Sekiro_Hang_Move_L` / `_R` / `_CrouchL` / `_CrouchR` |
|  | 20202-20203/20212-20213 | 腿放在/曲着悬挂左右移动速度更快 | `Anim_Sekiro_Hang_MoveFast_L` / `_R` / `_CrouchFastL` / `_CrouchFastR` |
|  | 20302-20303/20312-20313 | 腿放在/曲着悬挂左右移动到停止移动 | `Anim_Sekiro_Hang_Stop_L` / `_R` / `_CrouchStopL` / `_CrouchStopR` |
|  | 20402-20403/20412-20413 | 腿放在/曲着悬挂左右移动跨度更大 | `Anim_Sekiro_Hang_Reach_L` / `_R` / `_CrouchReachL` / `_CrouchReachR` |
|  | 20422-20423/20432-20433 | 和上面差不多 | `Anim_Sekiro_Hang_Reach2_L` / `_R` |
|  | 21000开头 | 贴墙动画 | `Anim_Sekiro_Wall_*` |
|  | 23000开头 | 杵着到的动作 | `Anim_Sekiro_Lean_*` |
|  | 24000开头 | 杵着到的动作，速度更快 | `Anim_Sekiro_LeanFast_*` |
|  | 30000开头 | 水下的动作 | `Anim_Sekiro_Swim_*` |
|  | 31000开头 | 水下的动作冲刺版 | `Anim_Sekiro_Swim_Sprint_*` |
|  | 40000开头 | 水下的动作缓慢版 | `Anim_Sekiro_Swim_Slow_*` |
|  | 41000开头 | 水下的动作蹬腿版 | `Anim_Sekiro_Swim_Kick_*` |
|  | 100000开头 | 各种受击动作 | `Anim_Sekiro_Hit_*` |
|  | 110000开头 | 死亡/回生动作 | `Anim_Sekiro_Death_*` / `_Resurrection_*` |
|  | 190000开头 | 各种状态的受击动作 | `Anim_Sekiro_Hit_State_*` |
|  | 200000开头 | 跳跃置空落地的动作 | `Anim_Sekiro_Jump_*` / `_Airborne_*` / `_Land_*` |
|  | 205000开头 | 贴墙偷听 | `Anim_Sekiro_Wall_Eavesdrop` |
|  | 206000开头 | 翻滚 | `Anim_Sekiro_Roll` |
|  | 210000开头 | 这种类型的都有，不好分类 | `Anim_Sekiro_Misc_*` |
|  | 213301-213304 | 前后左右垫步/闪避动画，不是下段跳跃 | `Anim_Sekiro_StepDodge_Alt_Fwd` / `_L` / `_R` / `_Bwd` |
|  | 220000开头 | 这种状态的窃听的动作 | `Anim_Sekiro_Eavesdrop_*` |
|  | 226000开头 | 杵刀的动作 | `Anim_Sekiro_StaffLean_*` |
|  | 250000开头 | 使用各种道具的动作 | `Anim_Sekiro_ItemUse_*` |
|  | 700000开头 | 各种交互动作 | `Anim_Sekiro_Interact_*` |

## 二、不好区分类别（a010）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a010 |  | 不好区分类型 | `Anim_Sekiro_MiscUncategorized_*` |

## 三、受击反应（a050）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a050 | 0开头的 | 各种防御招架的姿势 | `Anim_Sekiro_Guard_*` |
|  | 1开头的 |  | `Anim_Sekiro_GuardMove_*` |
|  | 2开头的 | 各种弹刀 | `Anim_Sekiro_Deflect_*` |
|  | 3开头的 | 攻击的姿势 | `Anim_Sekiro_AttackPose_*` |

## 四、忍义手（a070-a079）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a070 | 4开头的 | 忍义手 | `Anim_Sekiro_Prosthetic_Shuriken_*` |
| a071 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Axe_*` |
| a072 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Fire_*` |
| a073 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Spear_*` |
| a074 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Fan_*` |
| a075 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Umbrella_*` |
| a076 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Sword_*` |
| a077 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Claw_*` |
| a078 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Finger_*` |
| a079 |  | 忍义手 | `Anim_Sekiro_Prosthetic_Mist_*` |

## 五、战斗（a100-a110）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a100 |  | 战技 | `Anim_Sekiro_CombatArt_1_*` |
| a101 |  | 战技 | `Anim_Sekiro_CombatArt_2_*` |
| a102 |  | 战技 | `Anim_Sekiro_CombatArt_3_*` |
| a103 |  | 战技 | `Anim_Sekiro_CombatArt_4_*` |
| a104 |  | 战技 | `Anim_Sekiro_CombatArt_5_*` |
| a105 |  | 战技 | `Anim_Sekiro_CombatArt_6_*` |
| a106 |  | 战技 | `Anim_Sekiro_CombatArt_7_*` |
| a107 |  | 战技 | `Anim_Sekiro_CombatArt_8_*` |
| a108 |  | 战技 | `Anim_Sekiro_CombatArt_9_*` |
| a109 |  | 战技 | `Anim_Sekiro_CombatArt_10_*` |
| a110 |  | 战技 | `Anim_Sekiro_CombatArt_11_*` |

## 六、特殊互动/Boss 战（a200-a250）

| 前缀 | ID | 行为描述 | 方案动画名（UE） |
|------|-----|---------|-----------------|
| a200-a250 |  | 和不同角色boss的互动动作，包括忍杀，识破等 | `Anim_Sekiro_Deathblow_*` / `_MikiriCounter_*` / `_BossSync_*` |
