# Locomotion 曲线自动标注方案

本文定义一套确定版离线标注方案，用于从动画本身生成 Lua 动画状态机需要的 Locomotion 曲线。目标是替代之前依赖残留 TAE 曲线或手写 normalized time 的方案，让 `Start -> Cycle -> Stop -> Idle` 的过渡由动画姿势、脚步相位和匹配质量驱动。

曲线名称、稳定语义、运行时读取方式和新增曲线登记规则统一以 [动画曲线生成与使用手册](../animation-curve-authoring-guide.md) 为准。本文只负责 Locomotion 曲线的离线生成算法和质量标准。

## 目标

1. 删除旧方案残留曲线后，不再给基础移动动画写 `FrameFlags`、`CancelActions`、`AttackHitbox`。
2. Locomotion 曲线只解决基础移动过渡，不承载动作取消、攻击、格挡等战斗语义。
3. Walk/Run/Sprint/Step 的自然过渡统一由自动标注出的最少曲线驱动。
4. 自动标注结果必须输出报告，低置信度动画不写曲线，交给人工复核。

## 曲线定义

曲线命名遵循两个原则：

1. 尽量少，避免每个动画堆大量布尔曲线。
2. 不使用 bitmask；过渡条件使用独立布尔曲线，`FootPlant` 才使用单值枚举。
3. 曲线名表达动画系统语义，而不是某个状态机实现细节。

最终核心保留 5 条曲线：

| 曲线名 | 类型 | 用途 |
| --- | --- | --- |
| `CanEnterLoop` | int bool | Start/Turn/SprintStart 当前帧能否进入 Loop |
| `CanEnterStop` | int bool | Start/Loop 当前帧能否进入匹配的 Stop |
| `CanEnterIdle` | int bool | Stop/SprintStop 当前帧能否进入 Idle |
| `FootPlant` | int enum | 当前稳定触地的脚 |
| `MovePhase` | float | 当前步态循环相位 |

### CanEnter 曲线

类型：三条独立整数布尔曲线，阶梯插值，值只使用 `0/1`。

用途：直接表达目标状态当前是否开放，不再把互斥语义压进同一条枚举曲线。

写入规则：

- Start/Turn/SprintStart：从自动匹配到 Loop 的帧开始写 `CanEnterLoop = 1`，直到动画结束；同时在适合取消到 Stop 的触地窗口写 `CanEnterStop = 1`。
- Loop：只在与目标 Stop 起始姿势和脚步相位匹配的窗口写 `CanEnterStop = 1`。
- BlendSpace 样本动画各自生成该曲线，运行时按样本权重混合或择优读取。
- Stop/SprintStop：从自动匹配到 Idle 的帧开始写 `CanEnterIdle = 1`，直到动画结束。
- 非移动过渡动画不生成该曲线。

### FootPlant

类型：整数曲线，阶梯插值。

用途：调试、相位校验、Loop -> Stop 匹配。

| 值 | 含义 |
| ---: | --- |
| `0` | 无稳定触地 |
| `1` | 左脚稳定触地 |
| `2` | 右脚稳定触地 |
| `3` | 双脚稳定触地，作为枚举值使用，不是 bitmask |

写入规则：

- 每帧检测左右脚是否触地。
- 左右脚都满足触地条件时可同时置位，但正常 Walk/Run 应尽量避免长时间双脚同时置位。

### MovePhase

类型：浮点曲线，线性插值。

用途：描述步态周期相位，供 Start -> Loop、Loop -> Stop 与 BlendSpace 方向切换同步使用。

值域：

- `0.0`：左脚主要触地相位。
- `0.5`：右脚主要触地相位。
- `1.0`：回到下一次左脚主要触地相位。

写入规则：

- Loop 动画必须生成。
- Start/Stop 动画可选生成，用最近匹配 Loop 姿势映射相位。
- 非周期动作例如 Step 可不生成。

## 输入配置

自动标注器不硬编码项目动画路径。项目侧提供配置文件，例如：

```json
{
  "skeleton": {
    "root": "Root",
    "pelvis": "Pelvis",
    "left_foot": "L_Foot",
    "right_foot": "R_Foot"
  },
  "groups": [
    {
      "name": "Run_Forward",
      "start": "Run_Forward_Start",
      "loop": "Run_Forward_Loop",
      "stop": "Run_Forward_Stop",
      "idle": "Idle"
    },
    {
      "name": "Walk_Forward",
      "start": "Walk_Forward_Start",
      "loop": "Walk_Forward_Loop",
      "stop": "Walk_Forward_Stop",
      "idle": "Idle"
    }
  ]
}
```

动画别名由 `Content/Script/Animation/Sekiro/AnimAssets.lua` 提供，生成脚本负责把别名解析成 UE 资产路径。

## 自动标注流程

### 1. 采样动画姿势

对每个 AnimSequence 按固定帧率采样，默认 30 FPS。

采样数据：

- root 位置、朝向和速度。
- pelvis 位置和速度。
- 左右脚位置、速度和高度。
- 用于姿势匹配的一组关键骨骼局部姿势。

实现上优先使用 C++ Editor 工具采样动画，避免 UE Python 对骨骼姿势 API 暴露不完整。

### 2. 检测脚步触地

单脚触地条件：

1. 脚高度接近该动画内局部最低高度。
2. 脚水平速度低于阈值。
3. 脚垂直速度低于阈值。
4. 连续满足至少 2 帧，避免单帧噪声。

推荐初始阈值：

| 参数 | 默认值 |
| --- | ---: |
| `FootHeightToleranceCm` | `4.0` |
| `FootHorizontalSpeedMaxCmS` | `12.0` |
| `FootVerticalSpeedMaxCmS` | `8.0` |
| `MinPlantFrames` | `2` |

输出：

- `FootPlant`
- Loop 动画的基础步态相位锚点。

### 3. 生成 Loop 相位

对 Loop 动画：

1. 找左脚主触地点序列。
2. 找右脚主触地点序列。
3. 选择最稳定的一组左脚触地到下一次左脚触地作为一个周期。
4. 左脚触地写相位 `0.0`，右脚触地附近写相位 `0.5`，下一次左脚触地写相位 `1.0`。
5. 中间线性插值。

如果检测不到稳定左右脚相位，则该 Loop 不写 `MovePhase`，报告低置信度。

### 4. Start 到 Loop 匹配

对每组 `start -> loop`：

1. 跳过 Start 前段不可切帧，默认前 35% 不参与搜索。
2. 遍历 Start 候选帧和 Loop 全周期帧。
3. 计算匹配分数。

分数组成：

| 项 | 权重 | 说明 |
| --- | ---: | --- |
| 脚位置差 | `0.35` | 左右脚 root-space 位置差 |
| pelvis 差 | `0.20` | pelvis 高度和水平位置差 |
| root 速度差 | `0.20` | Start 当前速度接近 Loop 速度 |
| 姿势差 | `0.15` | 关键骨骼局部姿势差 |
| 相位一致性 | `0.10` | 脚步相位接近 |

选择最低分帧作为 `CanEnterLoop = 1` 的开启帧。若置信度低于阈值，不写曲线。

推荐阈值：

| 参数 | 默认值 |
| --- | ---: |
| `StartSearchMinNormalizedTime` | `0.35` |
| `EnterLoopMinConfidence` | `0.70` |

### 5. Start/Loop 到 Stop 匹配

对每组 `start/loop -> stop`：

1. 提取 Stop 前 3 到 5 帧作为 Stop 入口姿势。
2. 遍历 Start 可取消区间或 Loop 全周期帧，找与 Stop 入口姿势最接近的相位。
3. 只在匹配相位附近写 `CanEnterStop = 1` 窗口。

窗口规则：

- 优先使用 `MovePhase`，默认相位窗口半径 `0.08`。
- 如果没有相位曲线，则退化为同侧脚触地窗口。
- 如果两种方式都失败，不写 `CanEnterStop`，报告低置信度。

推荐阈值：

| 参数 | 默认值 |
| --- | ---: |
| `StopPhaseWindow` | `0.08` |
| `StopMatchMinConfidence` | `0.68` |

### 6. Stop 到 Idle 匹配

对每个 Stop：

1. 只搜索 Stop 后半段。
2. 找最接近 Idle 姿势且 root/pelvis 速度接近 0 的帧。
3. 从该帧开始写 `CanEnterIdle = 1` 到动画结束。

推荐阈值：

| 参数 | 默认值 |
| --- | ---: |
| `StopToIdleSearchMinNormalizedTime` | `0.45` |
| `IdleVelocityMaxCmS` | `8.0` |
| `StopToIdleMinConfidence` | `0.70` |

## 生成报告

每次自动标注输出报告：

```json
{
  "Run_Forward_Start": {
    "enter_loop_frame": 23,
    "matched_loop_frame": 8,
    "confidence": 0.84
  },
  "Run_Forward_Loop": {
    "enter_stop_windows": [
      {
        "phase": 0.52,
        "start_frame": 14,
        "end_frame": 17,
        "confidence": 0.79
      }
    ]
  },
  "Run_Forward_Stop": {
    "enter_idle_frame": 31,
    "confidence": 0.81
  }
}
```

低置信度项目必须进入 `needs_review`：

```json
{
  "needs_review": [
    {
      "animation": "Walk_Left_Turn",
      "reason": "No stable foot plant detected",
      "best_confidence": 0.51
    }
  ]
}
```

## Lua 使用规则

Lua 状态机只读曲线，不再猜字符串或硬拼动画名。

规则：

1. `Start -> Stop`：输入停止后，当前 Start 的 `CanEnterStop >= 0.5` 才允许切。
2. `Start -> Cycle`：当前动画 `CanEnterLoop >= 0.5` 才允许切，并把源 `MovePhase` 映射为目标 Cycle 的起播位置。
3. `Cycle -> Stop`：输入停止后，当前 Loop/BlendSpace 样本 `CanEnterStop >= 0.5` 才允许切。
4. `Cycle` 更换方向 BlendSpace：读取源圆周相位，在目标资产中反查匹配位置；曲线缺失才退回归一化时间保持。
5. `Stop -> Idle`：当前 Stop `CanEnterIdle >= 0.5` 才允许切。
6. 如果某动画没有曲线，才走临时 fallback；fallback 必须在 debug 中明确标红，方便后续补标注。

## 实现阶段

### Phase 1：Forward Walk/Run

覆盖：

- `Walk_Forward_Start`
- `Walk_Forward_Loop`
- `Walk_Forward_Stop`
- `Run_Forward_Start`
- `Run_Forward_Loop`
- `Run_Forward_Stop`

目标：

- 验证脚步触地检测。
- 验证 Start->Loop 和 Loop->Stop 匹配是否能改善过渡。

### Phase 2：四方向 Walk/Run

覆盖 Back/Left/Right，并接入 1D BlendSpace 当前样本曲线读取。

### Phase 3：Turn/Sprint

覆盖：

- `Walk_Left_Turn`
- `Walk_Right_Turn`
- `Run_Left_Turn`
- `Run_Right_Turn`
- Sprint Start/Loop/Stop/ToRun

Sprint 只有前向动画，因此 Stop/ToRun 重点使用角色朝向修正和相位窗口，不能直接按屏幕方向乱切。

### Phase 4：Step

Step 暂不纳入 Phase 1；后续根据目标状态写已有 `CanEnterLoop` / `CanEnterIdle`，并按需要补 `FootPlant` / `MovePhase`，不新增 Step 专用曲线。

## 成功标准

1. Walk/Run Start 不再过早切 Cycle。
2. Cycle 进入 Stop 时脚步相位匹配，不出现明显跨脚硬切。
3. Stop 进入 Idle 时重心已收住，不出现半途切断。
4. `ShowDebug Animation` 或 Lua debug 能看到当前状态、当前动画、当前曲线值和曲线来源。
5. 自动标注报告中低置信度动画不会静默写入错误曲线。
