# Sekiro Locomotion 方向速度计算报告

生成时间：2026-07-08 14:59:30

输入文件：

- 动画 JSON：`Output/Sekiro/Sekiro_anims_c0000_a000_lo.json`
- Lua 清单：`Content/Script/Animation/Sekiro/AnimAssets.lua`

本报告只计算并输出数据，不修改 UE 资源、Lua 或 C++ 运行时代码。

## 计算方法

- 先对每帧 `BoneTransforms` 做 FK，得到 `RootPos`、`Master`、`Pelvis`、左右脚和左右脚趾的世界位置。
- Root 水平位移接近 0 时，判定动画为 Inplace，改用脚锁定反推速度。
- 循环动画使用低误差投影算法：逐帧寻找低脚高、低垂直速度的接触 interval，将脚部反向速度投影到期望移动方向，再用中位数和 MAD 去异常值。
- Start/Stop/TurnStart/Turn 仍用传统脚锁定段估算，只作为过渡动画诊断，不参与运行时速度阈值。
- 速度单位统一输出为 UE `cm/s`。
- `身体速度向量(X,Z)` 是脚锁定反推出来的身体移动方向，当前数据中 Forward 大致对应负 Z。

## 主循环方向速度

这组是后续最适合用于 Walk/Run 方向 BlendSpace 的速度。方向映射采用 `Docs/design/sekiro-camera-locomotion.md` 中的 000200-203 / 000500-503 方案。

| AnimID | 动画 | 阶段 | 步态 | 方向 | 时长 | Root速度 | 脚锁定速度 | 最终速度 | 身体速度向量(X,Z) | 接触段 | 置信度 |
|---|---|---|---|---|---:|---:|---:|---:|---|---:|---|
| 000200 | Sekiro_a000_000200 | Cycle | Walk | Forward | 2.67 | 0.00 | 161.26 | 161.26 | (-0.52, -161.26) | 134/167 | High |
| 000201 | Sekiro_a000_000201 | Cycle | Walk | Back | 2.67 | 0.00 | 138.96 | 138.96 | (0.48, 138.96) | 138/158 | High |
| 000202 | Sekiro_a000_000202 | Cycle | Walk | Left | 2.67 | 0.00 | 128.53 | 128.53 | (128.53, 0.46) | 131/170 | High |
| 000203 | Sekiro_a000_000203 | Cycle | Walk | Right | 2.67 | 0.00 | 141.43 | 141.43 | (-141.43, -0.85) | 132/170 | High |
| 000500 | Sekiro_a000_000500 | Cycle | Run | Forward | 1.33 | 0.00 | 405.53 | 405.53 | (-2.98, -405.53) | 32/34 | Medium |
| 000501 | Sekiro_a000_000501 | Cycle | Run | Back | 1.53 | 0.02 | 423.56 | 423.56 | (-0.27, 423.56) | 46/56 | High |
| 000502 | Sekiro_a000_000502 | Cycle | Run | Left | 1.47 | 0.00 | 367.62 | 367.62 | (367.62, -2.44) | 56/70 | High |
| 000503 | Sekiro_a000_000503 | Cycle | Run | Right | 1.47 | 0.00 | 408.60 | 408.60 | (-408.60, -0.20) | 52/64 | High |

低误差测算中位数（写入运行时）：

- WalkSpeed：`140.19 cm/s`
- RunSpeed：`407.06 cm/s`
- SprintSpeed：`852.63 cm/s`

写入运行时的步态阈值：

- WalkSpeed：`140 cm/s`
- RunSpeed：`407 cm/s`
- SprintSpeed：`853 cm/s`
- CycleBlendMaxSpeed：`407 cm/s`（Walk/Run 插值上限，Sprint 单独处理）

方向 Loop 速度只用于诊断和动画 PlayRate 参考，不再写成角色移动阈值。运行时速度阈值只取每个步态的 Loop 中位数。Sprint 当前只使用 `001200` Forward Loop。

## Lua 当前引用动画

这组来自 `Content/Script/Animation/Sekiro/AnimAssets.lua` 当前引用，会随 Lua 清单自动更新。`Cycle` 可作为巡航速度参考；`Start`、`Stop`、`Turn`、`TurnStart` 更适合用于过渡调参，不建议直接作为巡航速度。

| AnimID | 动画 | 阶段 | 步态 | 方向 | 时长 | Root速度 | 脚锁定速度 | 最终速度 | 身体速度向量(X,Z) | 接触段 | 置信度 |
|---|---|---|---|---|---:|---:|---:|---:|---|---:|---|
| 000000 | Sekiro_a000_000000 | Idle | Idle | None | 3.33 | 0.00 | 0.00 | 0.00 | (0.00, 0.00) | 0/4 | Low |
| 000100 | Sekiro_a000_000100 | Start | Walk | Forward | 2.67 | 1.50 | 165.95 | 165.95 | (-0.03, -165.95) | 4/15 | Medium |
| 000101 | Sekiro_a000_000101 | Start | Walk | Back | 2.67 | 1.56 | 54.66 | 54.66 | (3.42, 54.52) | 2/14 | Medium |
| 000102 | Sekiro_a000_000102 | Start | Walk | Left | 2.67 | 2.84 | 127.27 | 127.27 | (127.26, 0.17) | 4/24 | Medium |
| 000103 | Sekiro_a000_000103 | Start | Walk | Right | 2.67 | 0.33 | 144.18 | 144.18 | (-144.18, -0.06) | 4/14 | Medium |
| 000132 | Sekiro_a000_000132 | Turn | Walk | Left | 2.67 | 1.50 | 162.20 | 162.20 | (-1.51, -152.81) | 6/11 | Medium |
| 000133 | Sekiro_a000_000133 | Turn | Walk | Right | 2.67 | 1.50 | 156.74 | 156.74 | (-2.52, -152.48) | 6/15 | Medium |
| 000200 | Sekiro_a000_000200 | Cycle | Walk | Forward | 2.67 | 0.00 | 161.26 | 161.26 | (-0.52, -161.26) | 134/167 | High |
| 000201 | Sekiro_a000_000201 | Cycle | Walk | Back | 2.67 | 0.00 | 138.96 | 138.96 | (0.48, 138.96) | 138/158 | High |
| 000202 | Sekiro_a000_000202 | Cycle | Walk | Left | 2.67 | 0.00 | 128.53 | 128.53 | (128.53, 0.46) | 131/170 | High |
| 000203 | Sekiro_a000_000203 | Cycle | Walk | Right | 2.67 | 0.00 | 141.43 | 141.43 | (-141.43, -0.85) | 132/170 | High |
| 000300 | Sekiro_a000_000300 | Stop | Walk | Forward | 1.33 | 0.00 | 42.82 | 42.82 | (4.79, -42.55) | 1/6 | Low |
| 000301 | Sekiro_a000_000301 | Stop | Walk | Back | 1.17 | 0.00 | 89.65 | 89.65 | (-25.82, 85.85) | 1/7 | Low |
| 000302 | Sekiro_a000_000302 | Stop | Walk | Left | 1.07 | 10.17 | 20.53 | 20.53 | (20.52, 0.41) | 1/6 | Low |
| 000303 | Sekiro_a000_000303 | Stop | Walk | Right | 1.07 | 10.22 | 29.33 | 29.33 | (-28.25, 7.58) | 2/7 | Medium |
| 000400 | Sekiro_a000_000400 | Start | Run | Forward | 1.67 | 1.80 | 590.31 | 590.31 | (0.16, -590.31) | 3/7 | Medium |
| 000401 | Sekiro_a000_000401 | Start | Run | Back | 1.97 | 1.82 | 347.75 | 347.75 | (1.08, 347.53) | 3/11 | Medium |
| 000402 | Sekiro_a000_000402 | Start | Run | Left | 1.80 | 1.96 | 418.86 | 418.86 | (418.86, 0.52) | 3/13 | Medium |
| 000403 | Sekiro_a000_000403 | Start | Run | Right | 1.87 | 2.72 | 420.12 | 420.12 | (-420.12, -1.23) | 3/14 | Medium |
| 000432 | Sekiro_a000_000432 | Turn | Run | Left | 1.53 | 4.40 | 554.45 | 554.45 | (1.62, -541.55) | 3/6 | Medium |
| 000433 | Sekiro_a000_000433 | Turn | Run | Right | 1.23 | 5.21 | 531.59 | 531.59 | (30.17, -530.03) | 2/6 | Medium |
| 000500 | Sekiro_a000_000500 | Cycle | Run | Forward | 1.33 | 0.00 | 405.53 | 405.53 | (-2.98, -405.53) | 32/34 | Medium |
| 000501 | Sekiro_a000_000501 | Cycle | Run | Back | 1.53 | 0.02 | 423.56 | 423.56 | (-0.27, 423.56) | 46/56 | High |
| 000502 | Sekiro_a000_000502 | Cycle | Run | Left | 1.47 | 0.00 | 367.62 | 367.62 | (367.62, -2.44) | 56/70 | High |
| 000503 | Sekiro_a000_000503 | Cycle | Run | Right | 1.47 | 0.00 | 408.60 | 408.60 | (-408.60, -0.20) | 52/64 | High |
| 000600 | Sekiro_a000_000600 | Stop | Run | Forward | 1.50 | 4.62 | 37.01 | 37.01 | (4.95, -36.40) | 3/8 | Medium |
| 000601 | Sekiro_a000_000601 | Stop | Run | Back | 1.17 | 0.00 | 20.92 | 20.92 | (-6.42, -19.91) | 1/7 | Low |
| 000602 | Sekiro_a000_000602 | Stop | Run | Left | 1.07 | 9.79 | 28.92 | 28.92 | (8.87, 2.19) | 2/8 | Medium |
| 000603 | Sekiro_a000_000603 | Stop | Run | Right | 1.07 | 10.22 | 29.14 | 29.14 | (-28.23, 6.59) | 2/7 | Medium |
| 001151 | Sekiro_a000_001151 | Start | Sprint | Forward | 0.67 | 7.47 | 695.01 | 695.01 | (-1.08, -695.01) | 3/5 | Medium |
| 001152 | Sekiro_a000_001152 | TurnStart | Sprint | Back | 0.67 | 3.90 | 442.76 | 442.76 | (-384.57, 219.40) | 1/5 | Low |
| 001153 | Sekiro_a000_001153 | TurnStart | Sprint | Left | 0.67 | 25.41 | 888.95 | 888.95 | (612.14, -624.33) | 2/8 | Medium |
| 001154 | Sekiro_a000_001154 | TurnStart | Sprint | Right | 0.67 | 21.71 | 765.07 | 765.07 | (-564.55, -446.21) | 2/5 | Medium |
| 001200 | Sekiro_a000_001200 | Cycle | Sprint | Forward | 0.43 | 0.00 | 852.63 | 852.63 | (-3.53, -852.63) | 6/8 | Low |
| 001510 | Sekiro_a000_001510 | Stop | Sprint | Forward | 1.17 | 0.00 | 65.59 | 65.59 | (-3.23, -65.43) | 2/7 | Medium |

## 旧参考变体

这组是脚本内保留的旧 Walk/Run 参考变体，只用于横向排查，不参与 Walk/Run/Sprint 速度结论。

| AnimID | 动画 | 阶段 | 步态 | 方向 | 时长 | Root速度 | 脚锁定速度 | 最终速度 | 身体速度向量(X,Z) | 接触段 | 置信度 |
|---|---|---|---|---|---:|---:|---:|---:|---|---:|---|
| 000110 | Sekiro_a000_000110 | Variant | Walk | Back | 2.67 | 1.50 | 153.90 | 153.90 | (-0.25, -153.64) | 6/15 | Medium |
| 000111 | Sekiro_a000_000111 | Variant | Walk | BackLeft | 2.67 | 1.74 | 152.66 | 152.66 | (-1.31, 152.64) | 3/15 | Medium |
| 000112 | Sekiro_a000_000112 | Variant | Walk | BackRight | 2.67 | 3.25 | 184.73 | 184.73 | (-173.08, 63.06) | 2/23 | Medium |
| 000120 | Sekiro_a000_000120 | Variant | Walk | Left | 2.67 | 1.50 | 156.67 | 156.67 | (-1.44, -156.66) | 5/14 | Medium |
| 000122 | Sekiro_a000_000122 | Variant | Walk | Right | 2.67 | 3.23 | 193.87 | 193.87 | (-188.07, 35.02) | 4/25 | Medium |
| 000420 | Sekiro_a000_000420 | Variant | Run | Left | 1.33 | 4.21 | 0.00 | 0.00 | (0.00, 0.00) | 0/0 | Low |
| 000422 | Sekiro_a000_000422 | Variant | Run | Right | 1.47 | 1.98 | 395.08 | 395.08 | (395.08, -0.45) | 5/12 | Medium |

## 注意事项

- `RootSpeed` 基本接近 0，说明低号 locomotion JSON 已经是 Inplace 或近似 Inplace。
- `FinalSpeed` 当前主要来自脚锁定估算，置信度取决于接触段数量和速度聚类稳定性。
- Start/Stop/TurnStart 动画速度是平均/局部接触段参考值，不等同于角色巡航速度。
- 本报告以 `Content/Script/Animation/Sekiro/AnimAssets.lua` 当前人工确认的引用为准，不使用旧版 animations-reference 文档修正资源含义。

## 输出文件

- `Script/temp/locomotion_direction_speed_report.json`
- `Script/temp/locomotion_direction_speed_report.csv`
- `Docs/design/sekiro-locomotion-speed-report.md`
