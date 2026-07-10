# Sekiro Locomotion 动画速度分析方案

## 背景

当前导入到 UE 的 Locomotion 动画资源是 Inplace。`Content/Script/Animation/Sekiro/AnimAssets.lua` 只保存动画资源路径，不包含 Walk、Run 等动画的真实速度元数据。

当前运行时速度主要来自手写调参：

- `Content/Script/Animation/Sekiro/Layer/GroundLocomotion/Library.lua`
  - `WalkSpeed = 150`
  - `RunSpeed = 520`
  - `CycleBlendMaxSpeed = 650`
- `Source/Sekiro/Movement/SKMovementComponent.h`
  - `WalkSpeed = 150.f`
  - `RunSpeed = 500.f`

本方案目标是：从动画本身反推出 Walk、Jog、Run、Sprint 的参考速度，用于后续修正 Lua 调参、BlendSpace 采样点和移动组件速度，减少脚步滑动。

## 输入数据

### AnimAssets.lua

`AnimAssets.lua` 用于确定要分析的动画清单：

- Walk 起步、停止、转向动画
- Run 起步、停止、转向动画
- Walk/Run BlendSpace 资源
- JogForward、RunFastForward 等当前 locomotion 相关资源

注意：这个文件不能直接算速度，只能作为动画资产索引。

### 动画 JSON

优先使用导入 UE 前的动画 JSON。已有样例结构如下：

```json
{
  "BoneNames": ["Master", "RootPos", "Pelvis"],
  "BoneParents": [-1, 0, 1],
  "BoneLocalTransforms": [],
  "Animations": [
    {
      "Name": "a000_299000",
      "Duration": 0.16666667,
      "FrameCount": 6,
      "SampleRate": 30,
      "Frames": [
        {
          "BoneTransforms": [
            { "P": [0, 0, 0], "R": [0, 0, 0, 1], "S": [1, 1, 1] }
          ]
        }
      ]
    }
  ]
}
```

速度分析需要字段：

- `BoneNames`
- `BoneParents`
- `Animations[0].Duration`
- `Animations[0].FrameCount`
- `Animations[0].SampleRate`
- `Animations[0].Frames[*].BoneTransforms[*].P/R/S`

## 核心算法

### 方案 A：Root 位移速度

如果 JSON 中仍保留 Root 位移，优先使用 Root 位移计算速度。

骨骼选择优先级：

1. `RootPos`
2. `Master`
3. `Pelvis`

Sekiro 当前 JSON 坐标看起来是 Y 轴向上，因此水平面使用 `X/Z`。

基础公式：

```text
start = root_position(frame_0).xz
end = root_position(frame_last).xz
distance_meter = length(end - start)
speed_cm_s = distance_meter * 100 / duration
```

更稳健的实现使用线性回归：

```text
move_dir = normalize(last_horizontal_position - first_horizontal_position)
s[i] = dot(horizontal_position[i] - first_horizontal_position, move_dir)
speed_meter_s = linear_regression_slope(time[i], s[i])
speed_cm_s = speed_meter_s * 100
```

线性回归比首尾帧差值更抗单帧抖动。

### 方案 B：脚锁定反推速度

如果 Root 位移接近 0，说明动画已经 Inplace，需要用脚锁定反推速度。

目标骨骼：

- `L_Foot`
- `R_Foot`
- `L_Toe0`
- `R_Toe0`

步骤：

1. 对每帧根据 `BoneParents` 做 FK，把脚骨骼转换到统一空间。
2. 找脚接触地面的区间。
3. 在接地区间内，Inplace 动画中的脚会相对身体向后滑动。
4. 这个脚部反向滑动距离，就是角色本应向前移动的距离。

接地区间判定：

```text
foot_height <= min_foot_height + height_threshold
vertical_speed <= vertical_speed_threshold
horizontal_speed is stable
segment_duration >= min_contact_duration
```

速度公式：

```text
foot_slide = foot_position_end - foot_position_start
body_move = -foot_slide
segment_speed_cm_s = length(body_move.xz) * 100 / segment_duration
```

同一个动画内通常会有多个左右脚接地区间，最终速度取中位数：

```text
foot_lock_speed = median(all_valid_segment_speed)
```

### 方案 C：混合置信度

每个动画输出两个速度候选：

- `RootSpeed`
- `FootLockSpeed`

最终选择规则：

| 条件 | FinalSpeed | Confidence |
|------|------------|------------|
| Root 位移明显，且速度稳定 | RootSpeed | High |
| Root 位移为 0，但脚锁定区间稳定 | FootLockSpeed | Medium |
| Root 与脚锁定结果接近 | 二者加权平均 | High |
| 无 Root 位移，脚锁定区间不足 | 空值或人工标记 | Low |

## 动画分类

速度分析不能把所有 Locomotion 动画混在一起处理。

| 类型 | 用途 | 速度处理 |
|------|------|----------|
| Cycle | Walk/Run/Jog/Sprint 循环 | 计算巡航速度，作为 BlendSpace 采样速度 |
| Start | Idle 到移动的起步 | 记录平均速度、总位移、起步时间，不作为巡航速度 |
| Stop | 移动到 Idle 的停止 | 记录平均速度、总位移、刹车时间，不作为巡航速度 |
| Turn | 原地或移动转身 | 主要分析角速度，不参与 Walk/Run 线速度 |

当前文档中部分 AnimID 命名存在冲突，速度分析结果可以反过来帮助确认分类：

- 循环动画通常速度稳定、脚锁定区间规律。
- Start/Stop 动画速度变化明显，不应当直接作为巡航速度。
- Turn 动画水平位移小，但朝向变化明显。

## 输出报告

离线脚本建议先输出报告，不直接改运行时数据。

目标文件：

- `Script/temp/locomotion_speed_report.json`
- `Script/temp/locomotion_speed_report.csv`

报告字段：

| 字段 | 说明 |
|------|------|
| `AnimID` | 动画 ID，例如 `000100` |
| `Name` | 动画名 |
| `AssetPath` | UE 资产路径 |
| `Type` | `Cycle` / `Start` / `Stop` / `Turn` |
| `Direction` | `Forward` / `Back` / `Left` / `Right` |
| `Duration` | 动画时长 |
| `FrameCount` | 帧数 |
| `RootSpeed` | Root 位移速度 |
| `FootLockSpeed` | 脚锁定反推速度 |
| `FinalSpeed` | 最终采用速度 |
| `Confidence` | `High` / `Medium` / `Low` |
| `Notes` | 异常或人工备注 |

示例：

```csv
AnimID,Name,Type,Direction,Duration,RootSpeed,FootLockSpeed,FinalSpeed,Confidence
000100,Walk_Fwd,Cycle,Forward,2.667,0.0,152.4,152.4,Medium
000200,Jog_Fwd,Cycle,Forward,2.667,0.0,301.8,301.8,Medium
000400,Run_Fast_Fwd,Cycle,Forward,1.667,0.0,506.2,506.2,Medium
```

## 后续接入方式

确认报告结果后，再生成运行时速度表：

```lua
local AnimSpeedTable = {
    WalkSpeed = 152.4,
    JogSpeed = 301.8,
    RunSpeed = 506.2,
    CycleBlendMaxSpeed = 650.0,

    AnimSpeeds = {
        WalkForward = 152.4,
        JogForward = 301.8,
        RunFastForward = 506.2,
    },
}

return AnimSpeedTable
```

接入点：

1. `GroundLocomotion/Library.lua` 从速度表读取 `WalkSpeed`、`RunSpeed`。
2. `GroundLocomotion.lua` 的 `GetCyclePlayRate()` 使用当前动画对应的实测速度做参考。
3. BlendSpace 采样点使用实测速度，而不是固定写死 `150/300/500/600`。
4. 如果需要同步 C++ 移动速度，再按 C++ 工作流更新 `SKMovementComponent`。

## 推荐落地顺序

1. 从 `AnimAssets.lua` 提取 Locomotion 动画资产清单。
2. 批量导出对应 AnimID 的原始 JSON。
3. 编写 `Script/temp/analyze_locomotion_speed.py`。
4. 先输出 `locomotion_speed_report.json/csv`。
5. 人工检查 Walk、Jog、Run、Sprint 的速度结果。
6. 再生成 `AnimSpeedTable.lua`。
7. 最后接入 Lua locomotion 和 BlendSpace。

## 当前结论

速度不能从 `AnimAssets.lua` 本身直接读取，只能从动画 JSON 的骨骼帧数据反推。

最可靠路径：

```text
Root 位移存在：
    Root 水平位移 / Duration

Root 位移不存在：
    脚锁定区间反推身体位移 / 接触时间
```

最终目标是用动画实测速度替换当前手写的 `WalkSpeed=150`、`RunSpeed=520`，并让 BlendSpace 采样点与动画真实步频一致。
