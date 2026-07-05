# Sekiro 常用动画导入参考

> **导入日期**: 2026-06-11  
> **源文件**: `Extracted/Sekiro_common_anims.json` (210.7 MB)  
> **导入结果**: 326/327 成功 (缺失1个: `102390` Attack_Dodge_Charged — 源文件中不存在)  
> **资源路径**: `/Game/Characters/Sekiro/Animations/`

---

## 分类统计

| 分类 | 数量 | 描述 |
|------|------|------|
| Idle | 10 | 待机（普通/战斗/收刀） |
| Walk | 16 | 步行（前行/后退/侧移） |
| Jog | 4 | 慢跑 |
| Sprint | 4 | 冲刺 |
| Run | 8 | 快速奔跑 |
| Sprint Transition | 2 | 冲刺过渡 |
| Turn | 12 | 转身（45°/90°/135°/180°） |
| Walk/Run Transition | 6 | 步伐切换过渡 |
| Dodge/Quickstep | 13 | 闪避/垫步 |
| Jump | 14 | 跳跃（中立/方向/落地/蹬墙） |
| Attack | 41 | 攻击（R1连段/突刺/蓄力/跳劈/蹲攻） |
| CombatArt | 30 | 流派招式（旋风斩/一文字/不死斩等） |
| Guard/Parry/Deflect/Mikiri | 47 | 防御/弹反/识破 |
| PostureBreak | 7 | 架势崩坏 |
| Sweep | 2 | 下段危—踩踏反击 |
| Grab | 4 | 擒拿—被擒/挣脱 |
| Deathblow | 13 | 忍杀（正面/背后/空中/落下/攀爬/暗杀） |
| Hit | 24 | 受击（轻/中/重/击退/硬直） |
| Death | 13 | 死亡（各方向/坠落/特殊/不死斩） |
| Resurrect | 2 | 回生 |
| Prosthetic | 39 | 忍义手（手里剑/斧/枪/火筒/伞/锈丸等） |
| Grapple | 15 | 钩绳（启动/飞行/落地/攻击） |

---

## 动画详细列表

### Idle (10)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000000` | `Sekiro_Idle_Default` | 默认待机 |
| `Sekiro_a000_000010` | `Sekiro_Idle_Combat` | 战斗待机 |
| `Sekiro_a000_000011` | `Sekiro_Idle_Combat_L` | 战斗待机-左 |
| `Sekiro_a000_000012` | `Sekiro_Idle_Combat_R` | 战斗待机-右 |
| `Sekiro_a000_000013` | `Sekiro_Idle_Combat_Shift` | 战斗待机-切换 |
| `Sekiro_a000_001151` | `Sekiro_Idle_WeaponOut` | 持刀待机 |
| `Sekiro_a000_001152` | `Sekiro_Idle_WeaponOut_L` | 持刀待机-左 |
| `Sekiro_a000_001153` | `Sekiro_Idle_WeaponOut_R` | 持刀待机-右 |
| `Sekiro_a000_001154` | `Sekiro_Idle_WeaponOut_Shift` | 持刀待机-切换 |
| `Sekiro_a000_001200` | `Sekiro_Idle_WeaponSheathe` | 收刀待机 |

### Walk (16)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000100` | `Sekiro_Walk_Fwd` | 前行 |
| `Sekiro_a000_000101` | `Sekiro_Walk_Fwd_L` | 前行-左斜 |
| `Sekiro_a000_000102` | `Sekiro_Walk_Fwd_R` | 前行-右斜 |
| `Sekiro_a000_000103` | `Sekiro_Walk_Fwd_Stop` | 前行-停止 |
| `Sekiro_a000_000110` | `Sekiro_Walk_Bwd` | 后退 |
| `Sekiro_a000_000111` | `Sekiro_Walk_Bwd_L` | 后退-左斜 |
| `Sekiro_a000_000112` | `Sekiro_Walk_Bwd_R` | 后退-右斜 |
| `Sekiro_a000_000113` | `Sekiro_Walk_Bwd_Stop` | 后退-停止 |
| `Sekiro_a000_000120` | `Sekiro_Walk_L` | 左移 |
| `Sekiro_a000_000121` | `Sekiro_Walk_L_Stop` | 左移-停止 |
| `Sekiro_a000_000122` | `Sekiro_Walk_R` | 右移 |
| `Sekiro_a000_000123` | `Sekiro_Walk_R_Stop` | 右移-停止 |
| `Sekiro_a000_000132` | `Sekiro_Walk_LSlow` | 慢速左移 |
| `Sekiro_a000_000133` | `Sekiro_Walk_RSlow` | 慢速右移 |
| `Sekiro_a000_020000` | `Sekiro_Walk_To_Idle` | 步行→待机 |
| `Sekiro_a000_020010` | `Sekiro_Walk_To_Jog` | 步行→慢跑 |

### Jog (4)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000200` | `Sekiro_Jog_Fwd` | 慢跑前进 |
| `Sekiro_a000_000201` | `Sekiro_Jog_Fwd_L` | 慢跑-左斜 |
| `Sekiro_a000_000202` | `Sekiro_Jog_Fwd_R` | 慢跑-右斜 |
| `Sekiro_a000_000203` | `Sekiro_Jog_Fwd_Stop` | 慢跑-停止 |

### Sprint (4)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000300` | `Sekiro_Sprint_Fwd` | 冲刺前进 |
| `Sekiro_a000_000301` | `Sekiro_Sprint_Fwd_L` | 冲刺-左斜 |
| `Sekiro_a000_000302` | `Sekiro_Sprint_Fwd_R` | 冲刺-右斜 |
| `Sekiro_a000_000303` | `Sekiro_Sprint_Fwd_Stop` | 冲刺-停止 |

### Run (8)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000400` | `Sekiro_Run_Fast_Fwd` | 快跑前进 |
| `Sekiro_a000_000401` | `Sekiro_Run_Fast_Fwd_L` | 快跑-左斜 |
| `Sekiro_a000_000402` | `Sekiro_Run_Fast_Fwd_R` | 快跑-右斜 |
| `Sekiro_a000_000403` | `Sekiro_Run_Fast_Fwd_Stop` | 快跑-停止 |
| `Sekiro_a000_000420` | `Sekiro_Run_Fast_L` | 快跑-左 |
| `Sekiro_a000_000421` | `Sekiro_Run_Fast_L_Stop` | 快跑-左停止 |
| `Sekiro_a000_000422` | `Sekiro_Run_Fast_R` | 快跑-右 |
| `Sekiro_a000_000423` | `Sekiro_Run_Fast_R_Stop` | 快跑-右停止 |

### Sprint Transition (2)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_000500` | `Sekiro_Sprint_To_Idle` | 冲刺→待机 |
| `Sekiro_a000_000600` | `Sekiro_Sprint_To_Idle_Fast` | 快跑→待机 |

### Turn (12)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_005000` | `Sekiro_Turn_L45` | 左转45° |
| `Sekiro_a000_005010` | `Sekiro_Turn_L90` | 左转90° |
| `Sekiro_a000_005011` | `Sekiro_Turn_L90_Fast` | 左转90°-快速 |
| `Sekiro_a000_005100` | `Sekiro_Turn_R45` | 右转45° |
| `Sekiro_a000_005110` | `Sekiro_Turn_R90` | 右转90° |
| `Sekiro_a000_005111` | `Sekiro_Turn_R90_Fast` | 右转90°-快速 |
| `Sekiro_a000_005200` | `Sekiro_Turn_L135` | 左转135° |
| `Sekiro_a000_005300` | `Sekiro_Turn_R135` | 右转135° |
| `Sekiro_a000_005400` | `Sekiro_Turn_L180` | 左转180° |
| `Sekiro_a000_005410` | `Sekiro_Turn_L180_Fast` | 左转180°-快速 |
| `Sekiro_a000_005500` | `Sekiro_Turn_R180` | 右转180° |
| `Sekiro_a000_005510` | `Sekiro_Turn_R180_Fast` | 右转180°-快速 |

### Walk/Run Transition (16)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_021000` | `Sekiro_Walk_Stop_Turn` | 步行停止转身 |
| `Sekiro_a000_023000` | `Sekiro_Run_To_Idle` | 奔跑→待机 |
| `Sekiro_a000_023200` | `Sekiro_Run_To_Walk` | 奔跑→步行 |
| `Sekiro_a000_023300` | `Sekiro_Run_To_Jog` | 奔跑→慢跑 |
| `Sekiro_a000_024200` | `Sekiro_Sprint_To_Run` | 冲刺→奔跑 |
| `Sekiro_a000_024300` | `Sekiro_Sprint_To_Jog` | 冲刺→慢跑 |

### Dodge/Quickstep (13)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_030000` | `Sekiro_StepDodge_Fwd` | 闪避-前 |
| `Sekiro_a000_030100` | `Sekiro_StepDodge_Bwd` | 闪避-后 |
| `Sekiro_a000_030110` | `Sekiro_StepDodge_L` | 闪避-左 |
| `Sekiro_a000_030200` | `Sekiro_StepDodge_R` | 闪避-右 |
| `Sekiro_a000_030300` | `Sekiro_StepDodge_Dash` | 闪避-冲刺 |
| `Sekiro_a000_031400` | `Sekiro_Quickstep_Fwd` | 垫步-前 |
| `Sekiro_a000_031410` | `Sekiro_Quickstep_L` | 垫步-左 |
| `Sekiro_a000_031420` | `Sekiro_Quickstep_R` | 垫步-右 |
| `Sekiro_a000_031430` | `Sekiro_Quickstep_Bwd` | 垫步-后 |
| `Sekiro_a000_213301` | `Sekiro_StepDodge_Alt_Fwd` | 垫步/闪避-前 |
| `Sekiro_a000_213302` | `Sekiro_StepDodge_Alt_L` | 垫步/闪避-左 |
| `Sekiro_a000_213303` | `Sekiro_StepDodge_Alt_R` | 垫步/闪避-右 |
| `Sekiro_a000_213304` | `Sekiro_StepDodge_Alt_Bwd` | 垫步/闪避-后 |

### Jump (13)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_040000` | `Sekiro_Jump_Neutral` | 原地跳跃 |
| `Sekiro_a000_040012` | `Sekiro_Jump_Fwd` | 前跳 |
| `Sekiro_a000_040100` | `Sekiro_Jump_Bwd` | 后跳 |
| `Sekiro_a000_040110` | `Sekiro_Jump_L` | 左跳 |
| `Sekiro_a000_040120` | `Sekiro_Jump_R` | 右跳 |
| `Sekiro_a000_040200` | `Sekiro_Jump_Land` | 跳跃落地 |
| `Sekiro_a000_040210` | `Sekiro_Jump_Fwd_Land` | 前跳落地 |
| `Sekiro_a000_040300` | `Sekiro_Jump_Fall` | 坠落 |
| `Sekiro_a000_040310` | `Sekiro_Jump_Fall_Long` | 长坠落 |
| `Sekiro_a000_040320` | `Sekiro_Jump_Ledge_Grab` | 攀抓边缘 |
| `Sekiro_a000_041400` | `Sekiro_Jump_WallKick_Fwd` | 蹬墙跳-前 |
| `Sekiro_a000_041410` | `Sekiro_Jump_WallKick_L` | 蹬墙跳-左 |
| `Sekiro_a000_041420` | `Sekiro_Jump_WallKick_R` | 蹬墙跳-右 |
| `Sekiro_a000_041430` | `Sekiro_Jump_WallKick_Bwd` | 蹬墙跳-后 |

### Attack (40)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_100000` | `Sekiro_Attack_R1_Combo01` | R1连段1 |
| `Sekiro_a000_100001` | `Sekiro_Attack_R1_Combo02` | R1连段2 |
| `Sekiro_a000_100002` | `Sekiro_Attack_R1_Combo03` | R1连段3 |
| `Sekiro_a000_100003` | `Sekiro_Attack_R1_Combo04` | R1连段4 |
| `Sekiro_a000_100100` | `Sekiro_Attack_R1_Step01` | R1踏步攻击1 |
| `Sekiro_a000_100101` | `Sekiro_Attack_R1_Step02` | R1踏步攻击2 |
| `Sekiro_a000_100102` | `Sekiro_Attack_R1_Step03` | R1踏步攻击3 |
| `Sekiro_a000_100200` | `Sekiro_Attack_R1_Dash01` | R1冲刺攻击1 |
| `Sekiro_a000_100201` | `Sekiro_Attack_R1_Dash02` | R1冲刺攻击2 |
| `Sekiro_a000_100300` | `Sekiro_Attack_R1_L_Combo01` | R1左连段1 |
| `Sekiro_a000_100301` | `Sekiro_Attack_R1_L_Combo02` | R1左连段2 |
| `Sekiro_a000_100310` | `Sekiro_Attack_R1_L_Step01` | R1左踏步1 |
| `Sekiro_a000_100311` | `Sekiro_Attack_R1_L_Step02` | R1左踏步2 |
| `Sekiro_a000_100320` | `Sekiro_Attack_R1_L_Dash01` | R1左冲刺攻击 |
| `Sekiro_a000_100400` | `Sekiro_Attack_Charged` | 蓄力攻击 |
| `Sekiro_a000_100401` | `Sekiro_Attack_Charged_Step` | 蓄力踏步 |
| `Sekiro_a000_100410` | `Sekiro_Attack_Charged_L` | 蓄力左 |
| `Sekiro_a000_100420` | `Sekiro_Attack_Charged_Dash` | 蓄力冲刺 |
| `Sekiro_a000_100500` | `Sekiro_Attack_Thrust` | 突刺 |
| `Sekiro_a000_100600` | `Sekiro_Attack_Thrust_Charged` | 蓄力突刺 |
| `Sekiro_a000_100610` | `Sekiro_Attack_Thrust_Charged_L` | 蓄力突刺-左 |
| `Sekiro_a000_100800` | `Sekiro_Attack_GuardBreak` | 破防攻击 |
| `Sekiro_a000_101100` | `Sekiro_Attack_Sprint_R1` | 冲刺R1 |
| `Sekiro_a000_101300` | `Sekiro_Attack_Sprint_Thrust` | 冲刺突刺 |
| `Sekiro_a000_102000` | `Sekiro_Attack_Dodge_Fwd` | 闪避攻击-前 |
| `Sekiro_a000_102100` | `Sekiro_Attack_Dodge_L` | 闪避攻击-左 |
| `Sekiro_a000_102110` | `Sekiro_Attack_Dodge_R` | 闪避攻击-右 |
| `Sekiro_a000_102300` | `Sekiro_Attack_Dodge_Dash` | 闪避冲刺攻击 |
| `Sekiro_a000_102310` | `Sekiro_Attack_Dodge_Back` | 闪避回身攻击 |
| `Sekiro_a000_102500` | `Sekiro_Attack_Slide_Fwd` | 滑步攻击-前 |
| `Sekiro_a000_102510` | `Sekiro_Attack_Slide_L` | 滑步攻击-左 |
| `Sekiro_a000_102520` | `Sekiro_Attack_Slide_R` | 滑步攻击-右 |
| `Sekiro_a000_102900` | `Sekiro_Attack_Quickstep_Fwd` | 垫步攻击-前 |
| `Sekiro_a000_102910` | `Sekiro_Attack_Quickstep_L` | 垫步攻击-左 |
| `Sekiro_a000_102930` | `Sekiro_Attack_Quickstep_R` | 垫步攻击-右 |
| `Sekiro_a000_102940` | `Sekiro_Attack_Quickstep_Bwd` | 垫步攻击-后 |
| `Sekiro_a000_103000` | `Sekiro_Attack_Jump` | 跳劈 |
| `Sekiro_a000_103100` | `Sekiro_Attack_Jump_Fwd` | 前跳劈 |
| `Sekiro_a000_103300` | `Sekiro_Attack_Jump_Charged` | 蓄力跳劈 |
| `Sekiro_a000_104100` | `Sekiro_Attack_Crouch` | 蹲伏攻击 |
| `Sekiro_a000_104300` | `Sekiro_Attack_Crouch_Charged` | 蹲伏蓄力攻击 |

### CombatArt (29)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_110000` | `Sekiro_CombatArt_Whirlwind` | 旋风斩 |
| `Sekiro_a000_110001` | `Sekiro_CombatArt_Whirlwind_L` | 旋风斩-左 |
| `Sekiro_a000_110010` | `Sekiro_CombatArt_Whirlwind_Alt` | 旋风斩-变体 |
| `Sekiro_a000_110030` | `Sekiro_CombatArt_Nightjar` | 寄鹰斩 |
| `Sekiro_a000_110031` | `Sekiro_CombatArt_Nightjar_Reversal` | 寄鹰斩-反 |
| `Sekiro_a000_111000` | `Sekiro_CombatArt_Ichimonji` | 一文字 |
| `Sekiro_a000_111001` | `Sekiro_CombatArt_Ichimonji_Double` | 一文字二连 |
| `Sekiro_a000_111010` | `Sekiro_CombatArt_Ichimonji_Alt` | 一文字-变体 |
| `Sekiro_a000_111011` | `Sekiro_CombatArt_Ichimonji_Double_Alt` | 一文字二连-变体 |
| `Sekiro_a000_111030` | `Sekiro_CombatArt_Ichimonji_Jump` | 一文字-跳 |
| `Sekiro_a000_111031` | `Sekiro_CombatArt_Ichimonji_Double_Jump` | 一文字二连-跳 |
| `Sekiro_a000_112020` | `Sekiro_CombatArt_PrayingStrikes` | 轻舟渡 |
| `Sekiro_a000_112021` | `Sekiro_CombatArt_PrayingStrikes_Alt` | 轻舟渡-变体 |
| `Sekiro_a000_113000` | `Sekiro_CombatArt_AshinaCross` | 苇名十字斩 |
| `Sekiro_a000_113010` | `Sekiro_CombatArt_AshinaCross_Alt` | 苇名十字斩-变体 |
| `Sekiro_a000_113030` | `Sekiro_CombatArt_AshinaCross_Dash` | 苇名十字斩-冲刺 |
| `Sekiro_a000_114000` | `Sekiro_CombatArt_Shadowrush` | 暗影突刺 |
| `Sekiro_a000_114010` | `Sekiro_CombatArt_Shadowrush_Alt` | 暗影突刺-变体 |
| `Sekiro_a000_114030` | `Sekiro_CombatArt_Shadowrush_Dash` | 暗影突刺-冲刺 |
| `Sekiro_a000_190000` | `Sekiro_CombatArt_MortalDraw` | 不死斩 |
| `Sekiro_a000_190001` | `Sekiro_CombatArt_MortalDraw_Empowered` | 不死斩-强化 |
| `Sekiro_a000_190010` | `Sekiro_CombatArt_MortalDraw_Jump` | 不死斩-跳跃 |
| `Sekiro_a000_190011` | `Sekiro_CombatArt_MortalDraw_Jump_Empowered` | 不死斩-跳跃强化 |
| `Sekiro_a000_190030` | `Sekiro_CombatArt_OneMind` | 一心 |
| `Sekiro_a000_191000` | `Sekiro_CombatArt_SakuraDance` | 樱舞 |
| `Sekiro_a000_191200` | `Sekiro_CombatArt_Lightning` | 雷电奉还 |
| `Sekiro_a000_191400` | `Sekiro_CombatArt_HighMonk` | 仙峰脚 |
| `Sekiro_a000_191500` | `Sekiro_CombatArt_HighMonk_Leap` | 仙峰脚-跃起 |
| `Sekiro_a000_192400` | `Sekiro_CombatArt_SenThrow` | 金钱镖 |
| `Sekiro_a000_192500` | `Sekiro_CombatArt_PhantomKunai` | 幻影苦无 |

### Guard/Parry/Deflect/Mikiri (49)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_200000` | `Sekiro_Guard_Idle` | 防御待机 |
| `Sekiro_a000_200100` | `Sekiro_Guard_Raise` | 举刀防御 |
| `Sekiro_a000_200120` | `Sekiro_Guard_Lower` | 收刀防御 |
| `Sekiro_a000_200121` | `Sekiro_Guard_Lower_Fast` | 快速收刀 |
| `Sekiro_a000_201000` | `Sekiro_Parry_Idle` | 弹反待机 |
| `Sekiro_a000_201001` | `Sekiro_Parry_Deflect` | 弹反 |
| `Sekiro_a000_201010` | `Sekiro_Parry_Raise` | 举刀弹反 |
| `Sekiro_a000_201011` | `Sekiro_Parry_Lower` | 收刀弹反 |
| `Sekiro_a000_201030` | `Sekiro_Parry_Deflect_Success` | 弹反成功 |
| `Sekiro_a000_201040` | `Sekiro_Parry_Deflect_L` | 弹反-左 |
| `Sekiro_a000_201045` | `Sekiro_Parry_Deflect_R` | 弹反-右 |
| `Sekiro_a000_201050` | `Sekiro_Parry_Deflect_Fwd` | 弹反-前 |
| `Sekiro_a000_201055` | `Sekiro_Parry_Deflect_Bwd` | 弹反-后 |
| `Sekiro_a000_201110` | `Sekiro_Deflect_Counter_R1` | 弹反反击R1 |
| `Sekiro_a000_201140` | `Sekiro_Deflect_Counter_Step` | 弹反反击-踏步 |
| `Sekiro_a000_201141` | `Sekiro_Deflect_Counter_Step_L` | 弹反反击-左踏步 |
| `Sekiro_a000_201142` | `Sekiro_Deflect_Counter_Step_R` | 弹反反击-右踏步 |
| `Sekiro_a000_201200` | `Sekiro_Deflect_Receive_01` | 被弹反01 |
| `Sekiro_a000_201210` | `Sekiro_Deflect_Receive_02` | 被弹反02 |
| `Sekiro_a000_201300` | `Sekiro_Guard_Heavy_Hit` | 重击防御 |
| `Sekiro_a000_201301` | `Sekiro_Guard_Heavy_Hit_L` | 重击防御-左 |
| `Sekiro_a000_201302` | `Sekiro_Guard_Heavy_Hit_R` | 重击防御-右 |
| `Sekiro_a000_201303` | `Sekiro_Guard_Heavy_Hit_Fwd` | 重击防御-前 |
| `Sekiro_a000_201304` | `Sekiro_Guard_Heavy_Hit_Back` | 重击防御-后 |
| `Sekiro_a000_201305` | `Sekiro_Guard_Heavy_Hit_KnockBack` | 重击防御-击退 |
| `Sekiro_a000_201320` | `Sekiro_Guard_Break_Recover` | 破防恢复 |
| `Sekiro_a000_201321` | `Sekiro_Guard_Break_Recover_Fast` | 破防恢复-快速 |
| `Sekiro_a000_201500` | `Sekiro_Guard_Counter` | 防御反击 |
| `Sekiro_a000_201501` | `Sekiro_Guard_Counter_Alt` | 防御反击-变体 |
| `Sekiro_a000_201600` | `Sekiro_Deflect_Jump` | 弹反跳 |
| `Sekiro_a000_201610` | `Sekiro_Deflect_Jump_Receive` | 被弹反跳 |
| `Sekiro_a000_202000` | `Sekiro_Parry_Recover` | 弹反恢复 |
| `Sekiro_a000_202010` | `Sekiro_Parry_Recover_L` | 弹反恢复-左 |
| `Sekiro_a000_202100` | `Sekiro_Parry_Into_Guard` | 弹反→防御 |
| `Sekiro_a000_202300` | `Sekiro_Guard_Into_Parry` | 防御→弹反 |
| `Sekiro_a000_202400` | `Sekiro_Guard_Into_Attack` | 防御→攻击 |
| `Sekiro_a000_202600` | `Sekiro_Guard_Break` | 破防 |
| `Sekiro_a000_202610` | `Sekiro_Guard_Break_L` | 破防-左 |
| `Sekiro_a000_202700` | `Sekiro_Guard_Break_Attack` | 破防后攻击 |
| `Sekiro_a000_202710` | `Sekiro_Guard_Break_Attack_L` | 破防后攻击-左 |
| `Sekiro_a000_205010` | `Sekiro_Mikiri_Counter` | 识破反击 |
| `Sekiro_a000_205011` | `Sekiro_Mikiri_Counter_Alt` | 识破反击-变体 |
| `Sekiro_a000_205020` | `Sekiro_Mikiri_StepIn` | 识破-踏步 |
| `Sekiro_a000_205030` | `Sekiro_Mikiri_Stab` | 识破-刺击 |
| `Sekiro_a000_206100` | `Sekiro_Mikiri_Receive` | 被识破 |
| `Sekiro_a000_206101` | `Sekiro_Mikiri_Receive_Alt` | 被识破-变体 |
| `Sekiro_a000_206120` | `Sekiro_Mikiri_Recover` | 识破恢复 |

### PostureBreak (6)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_210000` | `Sekiro_PostureBreak_Receive` | 架势崩坏-被破 |
| `Sekiro_a000_210001` | `Sekiro_PostureBreak_Receive_Heavy` | 架势崩坏-重击被破 |
| `Sekiro_a000_210010` | `Sekiro_PostureBreak_Stagger` | 架势崩坏-踉跄 |
| `Sekiro_a000_210018` | `Sekiro_PostureBreak_Fall` | 架势崩坏-倒地 |
| `Sekiro_a000_210050` | `Sekiro_PostureBreak_Recover` | 架势崩坏-恢复 |
| `Sekiro_a000_210100` | `Sekiro_PostureBreak_KnockDown` | 架势崩坏-击倒 |
| `Sekiro_a000_210150` | `Sekiro_PostureBreak_KnockDown_Recover` | 架势崩坏-击倒恢复 |

### Sweep (2)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_213100` | `Sekiro_Sweep_JumpKick` | 下段踩踏 |
| `Sekiro_a000_213110` | `Sekiro_Sweep_JumpKick_L` | 下段踩踏-左 |

### Grab (4)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_216000` | `Sekiro_Grab_Receive` | 被擒拿 |
| `Sekiro_a000_216010` | `Sekiro_Grab_Receive_Alt` | 被擒拿-变体 |
| `Sekiro_a000_216020` | `Sekiro_Grab_Escape` | 擒拿挣脱 |
| `Sekiro_a000_216100` | `Sekiro_Grab_Throw_Receive` | 被投技 |

### Deathblow (15)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_220000` | `Sekiro_Deathblow_Front` | 忍杀-正面 |
| `Sekiro_a000_220001` | `Sekiro_Deathblow_Back` | 忍杀-背后 |
| `Sekiro_a000_220002` | `Sekiro_Deathblow_Air` | 忍杀-空中 |
| `Sekiro_a000_220003` | `Sekiro_Deathblow_Plunge` | 忍杀-落下 |
| `Sekiro_a000_220004` | `Sekiro_Deathblow_Ledge` | 忍杀-边缘 |
| `Sekiro_a000_220005` | `Sekiro_Deathblow_Climb` | 忍杀-攀爬 |
| `Sekiro_a000_220006` | `Sekiro_Deathblow_Sneak` | 忍杀-暗杀 |
| `Sekiro_a000_220010` | `Sekiro_Deathblow_Front_Receive` | 被忍杀-正面 |
| `Sekiro_a000_220011` | `Sekiro_Deathblow_Back_Receive` | 被忍杀-背后 |
| `Sekiro_a000_220012` | `Sekiro_Deathblow_Air_Receive` | 被忍杀-空中 |
| `Sekiro_a000_220020` | `Sekiro_Deathblow_Front_Alt` | 忍杀-正面变体 |
| `Sekiro_a000_220021` | `Sekiro_Deathblow_Back_Alt` | 忍杀-背后变体 |
| `Sekiro_a000_220022` | `Sekiro_Deathblow_Air_Alt` | 忍杀-空中变体 |

### Hit (22)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_250000` | `Sekiro_Hit_Light_Front` | 轻受击-前 |
| `Sekiro_a000_250001` | `Sekiro_Hit_Light_Back` | 轻受击-后 |
| `Sekiro_a000_250005` | `Sekiro_Hit_Light_L` | 轻受击-左 |
| `Sekiro_a000_250006` | `Sekiro_Hit_Light_R` | 轻受击-右 |
| `Sekiro_a000_250010` | `Sekiro_Hit_Medium_Front` | 中受击-前 |
| `Sekiro_a000_250011` | `Sekiro_Hit_Medium_Back` | 中受击-后 |
| `Sekiro_a000_250015` | `Sekiro_Hit_Medium_L` | 中受击-左 |
| `Sekiro_a000_250016` | `Sekiro_Hit_Medium_R` | 中受击-右 |
| `Sekiro_a000_250030` | `Sekiro_Hit_Heavy_Front` | 重受击-前 |
| `Sekiro_a000_250031` | `Sekiro_Hit_Heavy_Back` | 重受击-后 |
| `Sekiro_a000_250035` | `Sekiro_Hit_Heavy_L` | 重受击-左 |
| `Sekiro_a000_250036` | `Sekiro_Hit_Heavy_R` | 重受击-右 |
| `Sekiro_a000_250040` | `Sekiro_Hit_KnockBack` | 受击-击退 |
| `Sekiro_a000_250041` | `Sekiro_Hit_KnockBack_Heavy` | 受击-重击退 |
| `Sekiro_a000_250045` | `Sekiro_Hit_KnockBack_L` | 受击-左击退 |
| `Sekiro_a000_250046` | `Sekiro_Hit_KnockBack_R` | 受击-右击退 |
| `Sekiro_a000_250100` | `Sekiro_Hit_Stagger` | 受击-硬直 |
| `Sekiro_a000_250110` | `Sekiro_Hit_Stagger_Heavy` | 受击-重硬直 |
| `Sekiro_a000_250200` | `Sekiro_Hit_Ground_Recover` | 受击-地面恢复 |
| `Sekiro_a000_250300` | `Sekiro_Hit_Air_Recover` | 受击-空中恢复 |
| `Sekiro_a000_250400` | `Sekiro_Hit_HeadShot` | 受击-爆头 |
| `Sekiro_a000_250500` | `Sekiro_Hit_Poison` | 受击-中毒 |
| `Sekiro_a000_250600` | `Sekiro_Hit_Terror` | 受击-恐怖 |
| `Sekiro_a000_250610` | `Sekiro_Hit_Terror_Death` | 受击-恐怖死亡 |

### Death (14)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_251000` | `Sekiro_Death_Front` | 死亡-前 |
| `Sekiro_a000_251100` | `Sekiro_Death_Back` | 死亡-后 |
| `Sekiro_a000_251200` | `Sekiro_Death_L` | 死亡-左 |
| `Sekiro_a000_251300` | `Sekiro_Death_R` | 死亡-右 |
| `Sekiro_a000_251400` | `Sekiro_Death_Fall` | 死亡-坠落 |
| `Sekiro_a000_251410` | `Sekiro_Death_Fall_Long` | 死亡-长坠落 |
| `Sekiro_a000_251500` | `Sekiro_Death_Special` | 死亡-特殊 |
| `Sekiro_a000_251530` | `Sekiro_Death_Immortal` | 死亡-不死 |
| `Sekiro_a000_251540` | `Sekiro_Death_Immortal_Fall` | 死亡-不死坠落 |
| `Sekiro_a000_251550` | `Sekiro_Death_Immortal_Recover` | 死亡-不死恢复 |
| `Sekiro_a000_251600` | `Sekiro_Death_Plunge` | 死亡-落下 |
| `Sekiro_a000_251800` | `Sekiro_Death_Grab` | 死亡-擒拿 |
| `Sekiro_a000_252000` | `Sekiro_Death_Snake` | 死亡-蛇 |

### Resurrect (2)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_259000` | `Sekiro_Resurrect_01` | 回生01 |
| `Sekiro_a000_259010` | `Sekiro_Resurrect_02` | 回生02 |

### Prosthetic (34)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_700010` | `Sekiro_Prosthetic_Shuriken` | 手里剑 |
| `Sekiro_a000_700200` | `Sekiro_Prosthetic_Shuriken_Charged` | 手里剑-蓄力 |
| `Sekiro_a000_700240` | `Sekiro_Prosthetic_Shuriken_Jump` | 手里剑-跳跃 |
| `Sekiro_a000_700280` | `Sekiro_Prosthetic_Shuriken_Sprint` | 手里剑-冲刺 |
| `Sekiro_a000_700300` | `Sekiro_Prosthetic_Shuriken_Slide` | 手里剑-滑步 |
| `Sekiro_a000_700500` | `Sekiro_Prosthetic_Shuriken_Dash` | 手里剑-闪避 |
| `Sekiro_a000_710000` | `Sekiro_Prosthetic_Axe` | 忍斧 |
| `Sekiro_a000_710100` | `Sekiro_Prosthetic_Axe_Charged` | 忍斧-蓄力 |
| `Sekiro_a000_710200` | `Sekiro_Prosthetic_Axe_Jump` | 忍斧-跳跃 |
| `Sekiro_a000_710310` | `Sekiro_Prosthetic_Axe_Dash` | 忍斧-闪避 |
| `Sekiro_a000_710400` | `Sekiro_Prosthetic_Spear` | 忍枪 |
| `Sekiro_a000_710410` | `Sekiro_Prosthetic_Spear_Charged` | 忍枪-蓄力 |
| `Sekiro_a000_710420` | `Sekiro_Prosthetic_Spear_Jump` | 忍枪-跳跃 |
| `Sekiro_a000_710430` | `Sekiro_Prosthetic_Spear_Dash` | 忍枪-闪避 |
| `Sekiro_a000_710500` | `Sekiro_Prosthetic_FlameVent` | 火筒 |
| `Sekiro_a000_710510` | `Sekiro_Prosthetic_FlameVent_Charged` | 火筒-蓄力 |
| `Sekiro_a000_710520` | `Sekiro_Prosthetic_FlameVent_Jump` | 火筒-跳跃 |
| `Sekiro_a000_710530` | `Sekiro_Prosthetic_FlameVent_Dash` | 火筒-闪避 |
| `Sekiro_a000_710600` | `Sekiro_Prosthetic_Umbrella` | 机关伞 |
| `Sekiro_a000_710700` | `Sekiro_Prosthetic_Umbrella_Open` | 机关伞-展开 |
| `Sekiro_a000_710800` | `Sekiro_Prosthetic_Umbrella_Spin` | 机关伞-旋转 |
| `Sekiro_a000_710900` | `Sekiro_Prosthetic_Umbrella_Attack` | 机关伞-攻击 |
| `Sekiro_a000_711000` | `Sekiro_Prosthetic_Sabimaru` | 锈丸 |
| `Sekiro_a000_711010` | `Sekiro_Prosthetic_Sabimaru_Combo` | 锈丸-连段 |
| `Sekiro_a000_711020` | `Sekiro_Prosthetic_Sabimaru_Dash` | 锈丸-闪避 |
| `Sekiro_a000_711100` | `Sekiro_Prosthetic_Whistle` | 口哨 |
| `Sekiro_a000_711110` | `Sekiro_Prosthetic_Whistle_Charged` | 口哨-蓄力 |
| `Sekiro_a000_711200` | `Sekiro_Prosthetic_Firecracker` | 爆竹 |
| `Sekiro_a000_711201` | `Sekiro_Prosthetic_Firecracker_Alt` | 爆竹-变体 |
| `Sekiro_a000_711210` | `Sekiro_Prosthetic_Firecracker_Dash` | 爆竹-闪避 |
| `Sekiro_a000_711211` | `Sekiro_Prosthetic_Firecracker_Dash_Alt` | 爆竹-闪避变体 |
| `Sekiro_a000_711300` | `Sekiro_Prosthetic_MistRaven` | 雾鸦 |
| `Sekiro_a000_711310` | `Sekiro_Prosthetic_MistRaven_Fwd` | 雾鸦-前 |
| `Sekiro_a000_711311` | `Sekiro_Prosthetic_MistRaven_L` | 雾鸦-左 |
| `Sekiro_a000_711312` | `Sekiro_Prosthetic_MistRaven_R` | 雾鸦-右 |
| `Sekiro_a000_711315` | `Sekiro_Prosthetic_MistRaven_Bwd` | 雾鸦-后 |
| `Sekiro_a000_711400` | `Sekiro_Prosthetic_FingerWhistle` | 指啸 |
| `Sekiro_a000_711500` | `Sekiro_Prosthetic_DivineAbduction` | 神隐 |
| `Sekiro_a000_711510` | `Sekiro_Prosthetic_DivineAbduction_Charged` | 神隐-蓄力 |

### Grapple (15)

| 动画ID | UE资产名 | 描述 |
|--------|----------|------|
| `Sekiro_a000_790000` | `Sekiro_Grapple_Start` | 钩绳-启动 |
| `Sekiro_a000_790010` | `Sekiro_Grapple_Fly` | 钩绳-飞行 |
| `Sekiro_a000_790020` | `Sekiro_Grapple_Land` | 钩绳-落地 |
| `Sekiro_a000_790030` | `Sekiro_Grapple_Ledge` | 钩绳-攀边 |
| `Sekiro_a000_790040` | `Sekiro_Grapple_Vault` | 钩绳-翻越 |
| `Sekiro_a000_790050` | `Sekiro_Grapple_Swing` | 钩绳-摆荡 |
| `Sekiro_a000_790060` | `Sekiro_Grapple_Swing_L` | 钩绳-摆荡左 |
| `Sekiro_a000_790070` | `Sekiro_Grapple_Swing_R` | 钩绳-摆荡右 |
| `Sekiro_a000_790080` | `Sekiro_Grapple_Swing_End` | 钩绳-摆荡结束 |
| `Sekiro_a000_790090` | `Sekiro_Grapple_Attack` | 钩绳-攻击 |
| `Sekiro_a000_790100` | `Sekiro_Grapple_Attack_L` | 钩绳-攻击左 |
| `Sekiro_a000_790110` | `Sekiro_Grapple_Attack_R` | 钩绳-攻击右 |
| `Sekiro_a000_790120` | `Sekiro_Grapple_Attack_Fwd` | 钩绳-攻击前 |
| `Sekiro_a000_790130` | `Sekiro_Grapple_Attack_Bwd` | 钩绳-攻击后 |
| `Sekiro_a000_790500` | `Sekiro_Grapple_To_Ledge` | 钩绳-到边缘 |

---

## 备注

- **缺失的动画**: `Sekiro_a000_102390` (Attack_Dodge_Charged) — 源JSON中不存在对应数据，可能游戏中为根运动驱动的派生动作
- **命名规则**: `Sekiro_<Category>_<Variant>` → UE资产名 `Sekiro_<Category>_<Variant>`
- **资源路径**: `/Game/Characters/Sekiro/Animations/Sekiro_<Category>_<Variant>.uasset`
- **骨架**: `/Game/Characters/Sekiro/Sekiro_Skeleton` (146骨骼，含IK骨骼)
- **网格体**: `/Game/Characters/Sekiro/Sekiro_SkeletalMesh`
