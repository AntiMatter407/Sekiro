# Lua 组合输入编写手册

## 一、职责边界

`Gameplay.Base.InputChordResolver` 统一处理按钮与方向轴的组合输入：

- 记录按钮和连续轴快照及其时间戳。
- 消除同一帧 Enhanced Input 回调顺序差异。
- 处理短暂组合窗口、优先级和共享触发键消费。
- 在窗口结束、稳定输入或提前释放时只结算一次。

解析器不调用 UE 接口，也不理解 Step、攻击或防御等玩法含义。具体结果由 Gameplay Lua 组件注册的 `Handler` 执行。

## 二、基础用法

在组件 `Construct` 中创建解析器并注册规则：

```lua
self.ChordResolver = InputChordResolver:New(self)
self.ChordResolver:RegisterChord("GuardAttack", {
    Trigger = "Attack",
    Window = 0.08,
    Priority = 100,
    RequiredButtons = { "Guard" },
    ConsumeTrigger = true,
    ResolveImmediately = true,
    Handler = function(owner, result)
        owner:RequestGuardAttack(result)
    end,
})
```

需要方向轴的真正组合必须记录原始输入，而不是只记录经过释放缓冲后的 MoveIntent：

```lua
self.ChordResolver:RecordAxis("Move", normalized_x, normalized_y, event_time)
```

按钮按下时创建候选，按钮释放时结算尚未到期的候选：

```lua
self.ChordResolver:RecordButtonStarted("Attack", started_time)
self.ChordResolver:BeginChord("GuardAttack", started_time)

self.ChordResolver:RecordButtonCompleted("Attack", completed_time)
self.ChordResolver:ResolveByTrigger("Attack", completed_time, "TriggerReleased")
```

组件 Tick 必须推进待决窗口：

```lua
self.ChordResolver:Update(current_time)
```

## 三、结算规则

1. 方向已经稳定按住超过 `Window` 时，组合立即结算，不增加延迟。
2. 按钮和方向近乎同时按下时，等待 `Window` 后使用最新轴快照。
3. 按钮在窗口结束前释放时立即结算，极短点击不会被吞掉。
4. 同一 Trigger 的规则按 `Priority` 从高到低结算。
5. `ConsumeTrigger=true` 的规则成功后取消同一 Trigger 的其他候选。
6. 结算后的 `AxisX/AxisY` 是冻结结果，动作播放期间不应重新读取输入并更换资源。
7. `WaitForAxisWhileTriggerHeld=true` 时，无方向且触发键仍按住的候选不会因窗口到期而结算；方向出现或触发键释放时才结算。

## 四、Dodge、Step 与 Sprint 约定

Sekiro 的 Dodge、Step 与 Sprint 不是组合输入，不使用 `InputChordResolver`，也没有人为时间窗口。输入组件按帧保存按键边沿、Held 状态和当前方向快照：

- Dodge 按下时立即请求一次 Step；当前没有方向则使用 Forward。
- Dodge 保持期间，移动输入每次从零重新进入有效区时再次请求 Step。
- 同一帧同时按下 Dodge 和方向只请求一次 Step，并使用该帧最终方向快照。
- Step 播放期间的连续方向变化只更新 MoveIntent，不重新选择或打断 Step 动画。
- Dodge 与有效移动输入同时持续超过 `0.50s` 后请求 Sprint。
- 只按住 Dodge 而没有移动时不累计 Sprint 时间，之后开始移动会从零计时。
- 锁定与非锁定共用输入规则；具体 Step 动画和 Sprint 移动规则由动画与 Movement 层决定。

极短移动输入的开始边沿会保留到当前 Tick 结束，即使 Enhanced Input 的 Completed 在同帧到达，也不会吞掉轻推转向或 Held+Move Step。

## 五、调试日志

输入模块输出三个关键阶段：

```text
phase=Pressed moveActive=false direction=(0.00, 0.00)
phase=StepRequested reason=DodgePressed direction=(1.00, 0.00) moveActive=false
phase=SprintRequested sprintInputHoldTime=0.504 physicalHoldTime=0.504 moveAmount=1.00
```

`DodgePressed` 表示 Shift 按下产生 Step，`HeldMoveStarted` 表示 Shift 保持期间出现新的移动开始边沿。Sprint 日志只在请求状态从 false 变为 true 时输出一次。
