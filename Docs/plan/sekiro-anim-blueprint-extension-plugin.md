# Sekiro AnimBlueprint Extension 插件 - 计划

> 状态：第一阶段 Lua Pose Graph 已接入 `ABP_Sekiro` | 创建：2026-07-06 | 更新：2026-07-13
> 关联：[技术方案](../design/sekiro-anim-blueprint-extension-plugin.md) / [Lua 动画蓝图编写手册](../lua-anim-blueprint-authoring-guide.md) / [Lua AnimGraph 运行时架构](../lua-animgraph-runtime-architecture.md)

## 当前方向

当前需求已从“包装每条 Transition Rule”调整为“AnimBlueprint 只作为 UnLua 绑定宿主，状态机和动画流程由 Lua 编写，并通过 C++ 接口提交当前 Pose”：

1. 不再为每条 Transition 添加 `Use Lua Rule` 勾选项。
2. 不再优先推进批量包装 Transition Rule Graph。
3. 动画蓝图中只放置运行时宿主节点 `Sekiro Lua Anim Blueprint Host` 并连接到 Output Pose。
4. Lua 主模块绑定到 AnimBlueprint，例如 `Animation.Sekiro.ABP_Sekiro`。
5. Lua 主模块继承 `Animation.Base.LuaAnimBlueprint`，负责创建状态机、注册动画层并声明 `AnimGraph()` 根输出。
6. Lua 节点统一继承 `LuaAnimNodeBase`：`LuaSequencePlayer` 经 `LuaAssetPlayerBase` 派生，`LuaAnimStateMachine` 也是节点；`LuaPoseLink` 只保存 `LinkedNode`。
7. 状态机 `Enter/Keep` 自动调用 `PublishLuaOutputPose`；C++ 生成线程安全 Pose Graph 快照，动画线程不直接调用 Lua。
8. 第一阶段运行时按 `NodeId` 将 Lua 句柄解析到真正的 `FAnimNode_SequencePlayer_Standalone` 派生节点，由状态机内部真实 `FPoseLink` 转发生命周期，并对当前/上一 Pose 做过渡和 Root Motion 混合；状态函数不再返回旧动画决策表。
9. 状态机拓扑按 UE 结构组织为 `StatePoseLink -> StateResult.Result -> 状态内部 AnimNode`；State 和 Transition 是数据对象，不继承 AnimNode。

## 当前目录约定

```text
Content/Script/Animation/
  Base/
    LuaAnimBlueprint.lua
    LuaAnimStateMachine.lua
    AnimGraph/
      LuaAnimNodeBase.lua
      LuaAssetPlayerBase.lua
      LuaRoot.lua
      LuaStateResult.lua
      LuaAnimationState.lua
      LuaAnimationTransition.lua
      LuaPoseLinkBase.lua
      LuaPoseLink.lua
      LuaSequencePlayer.lua
  Sekiro/
    ABP_Sekiro.lua
    AnimAssets.lua
    GroundLocomotion.lua
    Layer/
      GroundLocomotion/
        Library.lua
        WalkRunStateMachine.lua
        StandingLocomotion.lua
        CrouchLocomotion.lua
```

## 已完成

| # | 任务 | 结果 |
|:-:|------|------|
| 1 | 插件骨架 | `Plugins/SekiroAnimBlueprintExt/`，Runtime + Editor 双模块 |
| 2 | AnimInstance 基类 | `USekiroLuaAnimInstance` 直接继承 `UAnimInstance`，提供 Lua 动画更新、层注册、资源加载和快照接口 |
| 3 | 游戏接入 | `USKAnimInstance` 继承 `USekiroLuaAnimInstance`，并在更新末尾调用 Lua 动画更新 |
| 4 | Lua 模块绑定 | 优先使用 UnLua `GetModuleName`，`ABP_Sekiro` 绑定到 `Animation.Sekiro.ABP_Sekiro` |
| 5 | Lua 运行时上下文 | C++ 将 AnimInstance 简单 UPROPERTY 和 Snapshot 打包为 Lua table；Lua 基类同步为 `self.Speed`、`self.StateTime` 等实例字段 |
| 6 | Lua 动画层注册 | Lua 主模块 `Configure` 调用 `ClearLuaAnimLayers`、`SetDefaultLuaAnimLayerName`、`RegisterLuaAnimLayer` |
| 7 | 旧 Lua Pose 提交 MVP | 曾通过 `SetLuaAnimPoseByPath` 提交单一动画决策；当前已由第一阶段 Pose Graph 取代 |
| 8 | 快照应用 | C++ 根据 Lua 决策加载动画资源，维护当前/上一状态、时间、混合权重和 BlendSpace 输入 |
| 9 | AnimGraph 宿主节点 | `Sekiro Lua Anim Blueprint Host` 节点读取快照，支持 Sequence 和 BlendSpace 姿势评估 |
| 10 | 编辑器辅助接口 | 提供绑定 Lua 模块、连接 Lua AnimBlueprint Host 节点到动画图的 Editor Library |
| 11 | Lua 基类 | `Animation.Base.LuaAnimStateMachine` 支持继承、状态更新、Transition 函数命名、通用读取函数 |
| 12 | Sekiro 示例 | 已实现 `ABP_Sekiro.lua`、`GroundLocomotion.lua`、`AnimAssets.lua`、`Layer/GroundLocomotion/Library.lua` |
| 13 | 编写手册 | 新增 `Docs/lua-anim-blueprint-authoring-guide.md` |
| 14 | 编译验证 | `SekiroEditor Win64 Development` 和 `DebugGame` 编译通过 |
| 15 | PIE 验证 | 已验证 Lua 可驱动 Idle / Start / Cycle / Stop 等 Locomotion 状态决策 |
| 16 | Lua OO 编写风格 | 新增 `Animation.Base.Class`，支持 `class("X", Base)`、类函数实例化、`X.super.Method(self)`、`__init`；`ABP_Sekiro` 和 `GroundLocomotion` 已改为新风格示例 |
| 17 | 自动状态机基类 | `LuaAnimStateMachine` 支持 `StateList`、`EntryState`、自动发现 `CanEnter_<From>_<To>`，子类不再需要写 `UpdateState_<State>` |
| 18 | 类内变量/函数访问 | `LuaAnimStateMachine` 绑定原始 AnimInstance 与运行时上下文，子类可用 `self.Speed`、`self.StateTime` 和 `self:WantsCycle()` 这类类内函数，并可转发调用 AnimInstance 函数 |
| 19 | 旧逻辑节点接口 MVP | 曾用 `PlaySequence` / `SampleBlendSpace1D` 包装单一动画决策；当前业务 Lua 已迁移到 `SequencePlayer` |
| 20 | Lua AnimBlueprint Host | 新增 `Animation.Base.LuaAnimBlueprint`，`ABP_Sekiro` 继承它并在 `AnimGraph()` 输出 `GroundLocomotion`；AnimBlueprint 不再需要手动画 UE StateMachine |
| 21 | 删除空基类 | 删除 `USekiroAnimBlueprintInstance`，`USekiroLuaAnimInstance` 直接继承 `UAnimInstance` |
| 22 | 层级 GroundLocomotion | 单个 C++ Pose 快照使用 `Standing.*`、`Crouch.*`、`Sprint.*` 复合状态；Standing/Crouch 复用 Idle/Turn/Start/Cycle/Stop，Step/Sprint 由顶层调度 |
| 23 | 蹲姿步态解耦 | `MovementTier=Crouch` 只表达姿态；`USKAnimInstance` 根据 Walk 修饰键和摇杆迟滞阈值生成 Walk/Run，蹲姿不产生 Sprint |
| 24 | 第一阶段 Lua Pose Graph | 新增稳定句柄 `FSekiroLuaPoseLink`、`FAnimNode_SequencePlayer_Standalone` 派生播放器、Pose Graph 快照、`PublishLuaOutputPose` 和可打断活动过渡链；Lua 侧明确区分 AnimNode 与 PoseLink |
| 25 | UE 状态图结构复刻 | 新增 `LuaRoot`、`LuaStateResult`、`LuaAnimationState`、`LuaAnimationTransition`；Lua/C++ 的 `StatePoseLinks` 固定连接独立 StateResult，状态实际节点连接 `StateResult.Result`；原生 StateResult 直接继承 `FAnimNode_StateResult` |

## 当前接口

### C++ AnimInstance 接口

```cpp
bool UpdateLuaDrivenAnimation(float DeltaSeconds);
bool UpdateLuaDrivenAnimationLayer(FName LayerName, float DeltaSeconds);
void SetLuaAnimModuleName(const FString& ModuleName);
void SetDefaultLuaAnimLayerName(FName LayerName);
void RegisterLuaAnimLayer(FName LayerName);
void ClearLuaAnimLayers();
TArray<FName> GetLuaAnimLayerNames() const;
UAnimationAsset* LoadLuaAnimationAsset(const FString& AssetPath);
FSekiroLuaPoseLink CreateLuaSequencePlayer(FName LayerName, FName NodeName);
FSekiroLuaPoseLink CreateLuaStateResult(FName LayerName, FName NodeName, FName StateName);
bool SetLuaStateResultInput(FSekiroLuaPoseLink StateResultPoseLink, FSekiroLuaPoseLink InputPoseLink);
bool IsLuaPoseLinkValid(FSekiroLuaPoseLink PoseLink) const;
bool SetLuaSequencePlayerAssetByPath(FSekiroLuaPoseLink PoseLink, const FString& AnimationPath, FName AnimationName, bool bResetTime, float StartPosition);
bool SetLuaSequencePlayerParameters(FSekiroLuaPoseLink PoseLink, float PlayRate, bool bLoop);
bool PublishLuaOutputPose(FName LayerName, FSekiroLuaPoseLink PoseLink, float TransitionTime);
FSekiroLuaAnimSnapshot GetLuaAnimLayerSnapshot(FName LayerName) const;
```

### Lua 主模块接口

```lua
function M.Configure(context)
end

function M.UpdateLayer(context, layer_name, delta_seconds, runtime_context)
    -- 返回 nil：状态机已把根 PoseLink 发布给 C++。
end

function M.Update(context, delta_seconds, runtime_context)
    -- 同上。
end
```

### Lua Pose 提交示例

```lua
function GroundLocomotion:UpdateAnimation_Cycle()
    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = Anim.Run_Forward_Loop
    player.PlayRate = 1.0
    player.bLoop = true
    return player
end
```

## 下一步任务

| # | 任务 | 说明 | 状态 |
|:-:|------|------|:----:|
| 1 | 完善 GroundLocomotion | 已实现 Standing/Crouch 子状态机、锁定四向素材+连续方向扭转、斜向 Stop 收束、Step/Sprint/八向 Jump；待 PIE 手感验证 | 进行中 |
| 2 | 动画层扩展 | 增加 UpperBody / Combat / Additive Aim 等 Lua 层，并在 AnimGraph 中用 UE 节点混合 | 待开始 |
| 3 | Slot / Montage 接口 | Lua 发起 Montage 或 Slot 播放命令，C++ 提供安全封装 | 待设计 |
| 4 | Curve 输出 | Lua 决策支持输出曲线值，由 C++ 或 AnimGraph 参数消费 | 待设计 |
| 5 | Pose Graph 常用节点 | 第二阶段实现 Blend、BlendSpacePlayer、Inertialization 请求、Slot 和曲线节点 | 待开始 |
| 6 | 调试工具 | 增加当前 Lua 层、状态、动画资源、BlendInput 的日志或调试面板 | 待开始 |
| 7 | 原生子状态机节点 | 为每个 Lua 子状态机创建独立 C++ StateMachine AnimNode 和活动过渡数组，避免发布时展开成叶节点 | 待开始 |
| 8 | 编辑器自动化 | 已支持自动构建 LuaHost→Lua Orientation Warping→Inertialization→Root，并提供 Spine/IK 骨骼配置接口 | 已完成 |
| 9 | 热重载与错误处理 | 梳理 UnLua 热重载缓存、模块路径错误、Lua 异常时的 fallback 策略 | 待完善 |
| 10 | 设计文档同步 | 将旧 Transition 包装方案文档改写为当前 Lua 动画层方案 | 待开始 |
| 11 | Python 动画脚本层评估 | 评估用 Python 替代 Lua 编写现阶段动画蓝图脚本，复用“Lua 生成图描述、C++ 生成快照、动画线程只读快照”的架构 | 待评估 |

## Python 替代方案子任务

目标：验证是否可以把当前 Lua 动画蓝图脚本层迁移为 Python，让状态机、动画资源表、动画层库、动画决策都使用 Python 编写。

第一阶段只做可行性和最小 PoC，不直接推翻当前 Lua MVP：

- 确认运行时 Python 嵌入方式：使用 UE Python、第三方嵌入 Python，还是插件内自管 Python 解释器。
- 设计 Python 模块接口，对齐当前 Lua 的 `Configure`、`UpdateLayer`、`Update`。
- 设计 Python 决策返回结构，对齐当前 `FSekiroLuaAnimDecision` 字段。
- 验证 `ABP_Sekiro` 可绑定到 Python 主模块，例如 `Animation.Sekiro.ABP_Sekiro` 的 Python 等价路径。
- 验证 Python 状态机类可以实现继承、override、子状态机拆分和动画层库拆分。
- 保持动画线程安全边界：Python 只在游戏线程计算决策，动画线程只读取 C++ 快照。
- 评估热重载、断点调试、异常日志、性能和打包发布风险。

初步验收：

- Python 能驱动 `GroundLocomotion` 的 Idle / Start / Cycle / Stop 至少四个状态。
- Python 版本可以读取 `USKAnimInstance` 的 `Speed`、`MovementInputAmount`、`GroundedEntryState` 等上下文字段。
- Python 返回的动画决策能复用现有 `Sekiro Lua Anim Blueprint Host` 快照评估链路，或抽象出语言无关的 `Sekiro Script Anim Blueprint Host` 链路。
- 若 Python 运行时无法安全用于游戏运行时，需要在计划中明确限制和回退方案。

## 验收标准

- 插件不修改 UE5.2 引擎源码。
- `USKAnimInstance` 可以作为 `ABP_Sekiro` 的 Parent Class 使用。
- `ABP_Sekiro` 通过 UnLua `GetModuleName` 绑定到 `Animation.Sekiro.ABP_Sekiro`。
- AnimGraph 中的 `Sekiro Lua Anim Blueprint Host` 节点可以按 `LayerName` 读取对应 Lua 层快照；留空时读取 Lua 主模块声明的默认输出。
- Lua 可以独立编写状态机切换逻辑和动画更新逻辑。
- Lua 可以按角色和动画层拆文件，例如主模块、子状态机、动画层库、资源表。
- 动画线程不直接调用 Lua，只读取 C++ 预计算快照。
- Sequence 和 BlendSpace 资源都能由 Lua 决策驱动播放。
- `SekiroEditor Win64 Development` 编译通过。

## 已停止推进的旧方向

以下内容属于早期 Lua Transition override 方案，当前不再作为主线推进：

- 每条 Transition 勾选 `Use Lua Rule`。
- 为每条 Transition 自动生成单独的 Lua 包装函数。
- 批量包装 Transition Rule Graph 并调用 `ResolveLuaTransitionOverride(...)`。
- 在 AnimBlueprint Class Defaults 中配置 `LuaTransitionModuleRoot` 和 `StateMachineLuaBindings`。

保留这部分历史记录的原因是：后续如果需要“Lua 只覆盖局部 Transition，其他仍走蓝图连线”，可以从旧方案恢复，但当前主线是 Lua 动画层状态机。

## 变更记录

| 日期 | 变更 |
|:----:|------|
| 2026-07-06 | 初版：Lua Transition 插件方案 |
| 2026-07-06 | 修订：升级为 AnimBlueprint Extension 插件，接入 AnimInstance 基类 |
| 2026-07-06 | 修订：去掉每条 Transition 勾选项，改为状态机路径 Lua 覆盖 |
| 2026-07-07 | 重大修订：旧 Transition 包装方案停止作为主线，改为 Lua 驱动动画层状态机 |
| 2026-07-07 | 完成 `USekiroLuaAnimInstance`、Lua 快照、`Sekiro Lua Anim Blueprint Host` 节点、`ABP_Sekiro` Lua 接入 |
| 2026-07-07 | 新增 Lua 动画蓝图编写手册 |
| 2026-07-07 | 补充子任务：评估用 Python 替代 Lua 编写现阶段动画蓝图脚本 |
| 2026-07-07 | 修订：优先保留 Lua 运行时，封装成接近项目 Lua class / UnLua 的类继承写法 |
| 2026-07-07 | 修订：Transition 命名统一为 `CanEnter_<From>_<To>`，状态遍历封装进基类，子类不再编写 `UpdateState_<State>` |
| 2026-07-13 | 实现：Lua/C++ 增加独立 StateResult，拓扑改为 `StatePoseLink -> StateResult.Result -> SequencePlayer`；标准过渡改为活动过渡链并补充 UE 原生架构对照文档 |
| 2026-07-07 | 修订：动画更新命名统一为 `UpdateAnimation_<State>` |
| 2026-07-07 | 修订：状态更新遍历下沉到基类，子状态机只需声明状态列表、Entry、Transition 和动画更新函数 |
| 2026-07-07 | 修订：`UpdateAnimation_<State>` 不再直接调用 `MakeDecision`，只返回动画 key 或动画参数表 |
| 2026-07-07 | 修订：Lua 状态机子类改为类内变量/函数风格，Transition 与动画更新函数直接读取 `self` 字段，不再显式传 `facts` |
| 2026-07-07 | 实现：`GroundLocomotion` 使用新动画资源名完成 Walk/Run 状态机，非锁定使用 Forward，锁定按 MoveDirectionAngle/Angle 选择 Forward/Back/Left/Right |
| 2026-07-07 | 修订：`GroundLocomotion` 移除子类 `BuildFacts`、`facts` 中介和 enum 模糊匹配，业务逻辑直接使用 C++ 同名字段 |
| 2026-07-07 | 修订：动画更新改为逻辑节点风格，使用 `PlaySequence` / `SampleBlendSpace1D` 显式播放序列或采样 BlendSpace，不再拼接动画资源名 |
| 2026-07-08 | 修订：Lua 像 UnLua 类一样嵌入动画蓝图脚本侧，`PlaySequence` / `SampleBlendSpace1D` 内部通过 C++ Sequence / BlendSpace Pose 接口提交 Pose，Lua 不再需要返回决策表 |
| 2026-07-08 | 实现：新增 `LuaAnimBlueprint` 宿主基类，`ABP_Sekiro` 改为 `AnimGraph()` 输出 Lua 状态机；蓝图只需 Host 节点，不再手动画 UE StateMachine |
| 2026-07-08 | 清理：删除空壳 `USekiroAnimBlueprintInstance`，`USekiroLuaAnimInstance` 直接继承 `UAnimInstance` |
| 2026-07-11 | 重构：Dodge 输入改为按键边沿与 Held 模型；Shift 按下和 Held 期间新的移动开始边沿触发 Step，持续方向输入升级 Sprint |
| 2026-07-11 | 重构：Locomotion Turn 动画退出运行路径，Walk/Run/Step 与 90 度内 Sprint 由 Movement 实时转向；Sprint 大角度改为 Forward Stop→程序转向→Forward Start |
| 2026-07-12 | 重构：GroundLocomotion 改为 Standing/Crouch 两个 WalkRun 子状态机与顶层 Sprint/Step 分支，使用复合状态名保持单 Pose 快照和原生动画调试 |
| 2026-07-12 | 实现：Crouch Idle/Turn/Start/Cycle/Stop 接入 005000~005603 资源，Cycle Sequence 在步态、方向和姿态切换时使用 MovePhase 匹配 |
| 2026-07-12 | 修复：蹲姿姿态与 Walk/Run 步态解耦，Walk modifier 与摇杆阈值在蹲姿下继续生效，且禁止蹲姿直接产生 Sprint |
| 2026-07-12 | 实现：锁定 Standing/Crouch 使用四向 Sequence 加连续方向残差，Movement 修正 Root Motion 平移，Orientation Warping 修正下半身并反向补偿脊柱；斜向 Stop 冻结轨迹并在尾段对齐目标 |
| 2026-07-12 | 实现：锁定 Jump 使用八向原始 Sequence，Start/Land 修正 Root Motion 方向，InAir 仅做低权重姿态补偿；非锁定 Jump 保持 Forward+Movement 物理方案 |
