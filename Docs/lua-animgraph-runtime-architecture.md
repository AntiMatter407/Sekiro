# Lua AnimGraph 运行时架构

本文依据 UE5.2 动画运行时源码和 `Saved/Animation原理.txt`，说明 C++ 动画蓝图的底层求值方式、当前 Lua 实现的对应关系，以及本项目采用的复刻边界。

## 1. UE 运行时模型

UE 动画图由三类对象组成：

| 层级 | UE 类型 | 职责 |
|------|---------|------|
| 编辑器 | `UAnimGraphNode_*` | 显示节点、Pin、菜单、编译校验，不参与打包后 Pose 求值 |
| 运行时节点 | `FAnimNode_*` | 执行 `Initialize / CacheBones / Update / Evaluate`，负责播放、混合、IK、状态机等算法 |
| Pose 连接 | `FPoseLinkBase / FPoseLink` | 保存并调用上游运行时节点，是输入连接，不是动画节点 |

动画图采用 pull model。最终输出节点调用自己的输入 `FPoseLink.Evaluate()`，连接再递归请求上游节点生成 Pose。执行方向与编辑器中从播放器连向输出的视觉方向相反：

```text
FAnimNode_Root.Result
    -> FAnimNode_StateMachine
        -> StatePoseLinks[CurrentState]
            -> FAnimNode_StateResult.Result
                -> FAnimNode_SequencePlayer
```

`Update` 负责时间、权重、通知和 Root Motion 相关性；`Evaluate` 负责生成 Pose、Curve 和 Custom Attributes。两者不能合并成单纯的“选择动画资源”。

## 2. 状态机的真实结构

`FAnimNode_StateMachine` 是 AnimNode，但 State 不是 AnimNode。编辑器编译后，`FBakedAnimationStateMachine` 保存状态数组，`FBakedAnimationState` 保存状态名、出边转换、通知、State Root 索引和重入策略；运行时节点只保存当前状态、活动过渡和连接。

每个 State 内部是一棵独立 AnimGraph，固定以 `FAnimNode_StateResult` 结束：

```text
FAnimNode_StateMachine
  StatePoseLinks[i] -> FAnimNode_StateResult
                         Result -> 状态内部 AnimNode
```

标准过渡由状态机保存活动过渡数组。发生 `A -> B -> C` 打断时，旧过渡不会冻结：

```text
OutputAB  = Blend(Evaluate(A), Evaluate(B), AlphaAB)
OutputABC = Blend(OutputAB, Evaluate(C), AlphaBC)
```

旧状态节点继续 Update；最终叶节点权重由每层 `(1 - Alpha)` 递乘得到。新过渡完成后，它和更旧过渡一起移除。UE 还使用 `StatesUpdated` 避免同一状态在一帧内被重复更新，并在 `Update` 阶段处理状态通知、转换通知、同步组和 Root Motion 权重；`Evaluate` 只按已经确定的活动过渡生成结果。

原生状态机的完整职责可以概括为：

1. `Initialize` 根据烘焙状态数据建立固定 `StatePoseLinks`，进入 Entry 指向的初始状态。
2. `Update` 按转换优先级调用 `FindValidTransition`，维护 `ActiveTransitionArray`，再更新所有仍有权重的状态图。
3. `Evaluate` 求值当前状态或按活动过渡栈逐层混合前后状态的 Pose、Curve 和 Attributes。
4. 状态切换时处理重入、通知、BlendProfile、Custom Transition Graph 和 Inertialization 等策略。

## 3. Lua 类映射

项目 Lua 类按 UE 职责拆分：

```text
LuaAnimNodeBase                         LuaPoseLinkBase
├─ LuaAssetPlayerBase                  └─ LuaPoseLink
│  └─ LuaSequencePlayer
├─ LuaRoot
│  └─ LuaStateResult
└─ LuaAnimStateMachine

LuaAnimationState       -- State 数据，不是 AnimNode
LuaAnimationTransition  -- 转换描述数据，不是 AnimNode
```

对应关系：

| Lua | UE | 说明 |
|-----|----|------|
| `LuaAnimNodeBase` | `FAnimNode_Base` | 所有计算节点的共同类型 |
| `LuaPoseLinkBase` | `FPoseLinkBase` | 只保存 `LinkedNode` 并转发生命周期 |
| `LuaPoseLink` | `FPoseLink` | 本地空间 Pose 连接 |
| `LuaRoot` | `FAnimNode_Root` | 只有一个 `Result` 输入 |
| `LuaStateResult` | `FAnimNode_StateResult` | 每个状态图固定的输出边界；C++ 对应节点直接继承原生类型 |
| `LuaSequencePlayer` | `FAnimNode_SequencePlayer_Standalone` | 持久资产播放器节点 |
| `LuaAnimStateMachine` | `FAnimNode_StateMachine` | 持有状态数据、StatePoseLinks 和转换描述 |
| `LuaAnimationState` | `FBakedAnimationState` | 状态数据，持有 StateResult 和出边转换 |
| `LuaAnimationTransition` | 烘焙转换数据 | 保存源、目标、规则名、混合时长和优先级 |

状态拓扑必须是：

```text
LuaAnimStateMachine.StatePoseLinks[i]
    -> LuaAnimationState.StateResult
        -> LuaStateResult.Result
            -> LuaSequencePlayer / Blend / 子状态机
```

禁止让 `StatePoseLinks[i]` 直接改连到本帧选择的 SequencePlayer。状态输出边界必须稳定，具体状态图输出只修改 `StateResult.Result`。第一阶段 `Result` 的原生输入只支持 `SequencePlayer`；Lua 类已经允许 Blend 或子状态机作为上游，但在对应 C++ 节点完成前不会伪装成已支持。

## 4. Lua 与 C++ 的线程边界

Lua 不在动画工作线程执行。两侧分工如下：

```text
游戏线程 Lua
  读取 C++ 属性
  执行 CanEnter 规则
  设置节点成员
  建立逻辑 PoseLink
  发布只读快照
        |
        v
动画线程 C++
  原生 AnimNode Initialize/CacheBones/Update/Evaluate
  Sequence 时间推进
  StateResult Pose 转发
  ActiveTransition 链
  Pose/Curve/Attributes 混合
  Root Motion 提取
```

Lua 类是编写层和控制层；真正的 Pose 算法仍由 C++ `FAnimNode_*` 执行。这样既保留 Lua 的类、继承和 override 风格，也不破坏 UE 动画线程模型。

## 5. 当前匹配度

| 能力 | 当前状态 | 结论 |
|------|----------|------|
| Node 与 Link 类型分离 | 已实现 | 匹配 |
| SequencePlayer 为真实原生 AnimNode | 已实现 | 匹配 |
| State 不是 AnimNode | 已用 `LuaAnimationState` 明确 | 匹配 |
| 每个 State 拥有 StateResult | Lua/C++ 均已实现，C++ 直接继承 `FAnimNode_StateResult` | 匹配 |
| 状态机持有稳定 StatePoseLinks | Lua/C++ 均已实现 | 匹配 |
| 被打断过渡继续 Update/Evaluate | C++ 使用活动过渡链 | 匹配标准 Blend 主路径 |
| Pose、Curve、Attributes 同时混合 | C++ 使用 `FAnimationPoseData` | 匹配 |
| 状态机直接继承原生 `FAnimNode_StateMachine` | 未实现；动态 Lua 图使用自定义 `FAnimNode_Base` 节点复刻核心字段和流程 | 结构匹配，类型不匹配 |
| 烘焙状态机、Conduit、状态/转换通知 | 尚未实现 | 不匹配 |
| 状态重入与一帧去重 | SequencePlayer 支持显式重置；尚未完整复刻 `bAlwaysResetOnEntry` 与 `StatesUpdated` | 部分匹配 |
| 子状态机拥有独立原生状态机节点 | 尚未实现；当前仍会在发布时展开到叶节点 | 不匹配 |
| BlendSpace、TwoWayBlend、Additive、Slot、IK | 尚未抽象为通用 Lua 节点 | 不匹配 |
| SyncGroup、Marker Sync、BlendProfile、Custom Transition | 尚未完整复刻 | 不匹配 |

“Lua 类存在”不等于“原生运行时节点存在”。只有拥有独立 NodeId、快照描述和对应 `FAnimNode_*` 实例的类型，才算完成 C++ 运行时复刻。当前方案不是把 Lua 放进动画工作线程，而是让 Lua 在游戏线程生成与更新图描述，再由原生节点执行 AnyThread 生命周期。

## 6. 业务状态机写法

业务代码仍只实现转换和状态动画：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许从 Idle 进入 Start。
function Locomotion.CanEnter_Idle_Start(Inst)
    return Inst.bIsMoving == true
end

function Locomotion:UpdateAnimation_Start()
    local player = self:GetSequencePlayer("Start")
    player.Sequence = self.Assets.Run_Forward_Start
    player.PlayRate = 1.0
    player.bLoop = false
    return player
end
```

基类自动完成：

1. 为 StateList 建立 `LuaAnimationState`。
2. 为每个 State 创建固定 `LuaStateResult`。
3. 将 `UpdateAnimation_<State>` 返回节点接到 `StateResult.Result`。
4. 自动发现并按声明顺序烘焙 `CanEnter_<From>_<To>`。
5. 选择 Entry、执行转换并向 C++ 发布状态输出。

业务类不得直接调用 `PublishLuaOutputPose`，也不得用动画名字符串拼接模拟 AnimGraph。

## 7. 后续复刻顺序

1. 每个子状态机独立的原生 StateMachine 节点和活动过渡数组。
2. `BlendSpacePlayer`、`TwoWayBlend`、`ApplyAdditive`、`LayeredBoneBlend`。
3. `StatesUpdated`、完整重入策略、状态/转换通知和 Conduit。
4. Inertialization、Slot、曲线修改节点。
5. SyncGroup、Marker Sync、BlendProfile 和自定义过渡图。

每新增一种 Lua 节点，都必须同时具备 Lua 类、线程安全快照、C++ 原生节点和 PoseLink 连接；不能只在 Lua 里新增表结构。
