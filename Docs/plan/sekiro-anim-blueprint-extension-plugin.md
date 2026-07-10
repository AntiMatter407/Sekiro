# Sekiro AnimBlueprint Extension 插件 - 计划

> 状态：Lua AnimBlueprint Host MVP 已接入 `ABP_Sekiro` | 创建：2026-07-06 | 更新：2026-07-08  
> 关联：[技术方案](../design/sekiro-anim-blueprint-extension-plugin.md) / [Lua 动画蓝图编写手册](../lua-anim-blueprint-authoring-guide.md)

## 当前方向

当前需求已从“包装每条 Transition Rule”调整为“AnimBlueprint 只作为 UnLua 绑定宿主，状态机和动画流程由 Lua 编写，并通过 C++ 接口提交当前 Pose”：

1. 不再为每条 Transition 添加 `Use Lua Rule` 勾选项。
2. 不再优先推进批量包装 Transition Rule Graph。
3. 动画蓝图中只放置运行时宿主节点 `Sekiro Lua Anim Blueprint Host` 并连接到 Output Pose。
4. Lua 主模块绑定到 AnimBlueprint，例如 `Animation.Sekiro.ABP_Sekiro`。
5. Lua 主模块继承 `Animation.Base.LuaAnimBlueprint`，负责创建状态机、注册动画层并声明 `AnimGraph()` 根输出。
6. Lua 每帧在 `UpdateAnimation_<State>` 中调用 `PlaySequence` / `SampleBlendSpace1D`，基类分别转发到 C++ `SetLuaAnimSequencePoseByPath` / `SetLuaAnimBlendSpacePoseByPath`。
7. C++ 插件负责把 Lua 提交的 Pose 转换成线程安全快照；动画线程只读取快照并评估动画，不直接调用 Lua。
8. Lua 返回动画决策表仍作为兼容层保留，但新代码不再推荐直接返回字符串、表驱动映射或拼接动画资源名。

## 当前目录约定

```text
Content/Script/Animation/
  Base/
    LuaAnimBlueprint.lua
    LuaAnimStateMachine.lua
  Sekiro/
    ABP_Sekiro.lua
    AnimAssets.lua
    GroundLocomotion.lua
    Layer/
      GroundLocomotion/
        Library.lua
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
| 7 | Lua Pose 提交 | 支持 Lua 通过 `SetLuaAnimPoseByPath` 提交 `StateName`、`AnimationPath`、`BlendTime`、`PlayRate`、`Loop`、`ResetTime`、`BlendInputX/Y/Z`，并兼容旧决策表返回 |
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
| 19 | UnLua 风格 Pose 接口 | `UpdateAnimation_<State>` 改为调用 `PlaySequence` / `SampleBlendSpace1D`，内部转发 C++ Sequence / BlendSpace Pose 接口；`SekiroEditor Win64 Development` 编译通过 |
| 20 | Lua AnimBlueprint Host | 新增 `Animation.Base.LuaAnimBlueprint`，`ABP_Sekiro` 继承它并在 `AnimGraph()` 输出 `GroundLocomotion`；AnimBlueprint 不再需要手动画 UE StateMachine |
| 21 | 删除空基类 | 删除 `USekiroAnimBlueprintInstance`，`USekiroLuaAnimInstance` 直接继承 `UAnimInstance` |

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
bool SetLuaAnimPose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);
bool SetLuaAnimPoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);
bool SetLuaAnimSequencePoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);
bool SetLuaAnimBlendSpacePoseByPath(FName LayerName, FName StateName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);
FSekiroLuaAnimSnapshot GetLuaAnimLayerSnapshot(FName LayerName) const;
```

### Lua 主模块接口

```lua
function M.Configure(context)
end

function M.UpdateLayer(context, layer_name, delta_seconds, runtime_context)
    -- 新写法通常返回 nil：具体 Pose 已由状态机调用 C++ 接口提交。
    -- 旧决策表返回仍兼容。
end

function M.Update(context, delta_seconds, runtime_context)
    -- 同上。
end
```

### Lua Pose 提交示例

```lua
function GroundLocomotion:UpdateAnimation_Cycle()
    return self:SampleBlendSpace1D(Anim.Walk_Run_Blend_Forward_1D, self.Speed, {
        PlayRate = self:Clamp(self.Speed / 520.0, 0.75, 1.35),
    })
end
```

## 下一步任务

| # | 任务 | 说明 | 状态 |
|:-:|------|------|:----:|
| 1 | 完善 GroundLocomotion | 已先实现 Walk/Run 的 Idle / Start / Cycle / Stop，支持锁定四方向与非锁定前向逻辑；Sprint、Turn 暂缓 | 进行中 |
| 2 | 动画层扩展 | 增加 UpperBody / Combat / Additive Aim 等 Lua 层，并在 AnimGraph 中用 UE 节点混合 | 待开始 |
| 3 | Slot / Montage 接口 | Lua 发起 Montage 或 Slot 播放命令，C++ 提供安全封装 | 待设计 |
| 4 | Curve 输出 | Lua 决策支持输出曲线值，由 C++ 或 AnimGraph 参数消费 | 待设计 |
| 5 | Inertialization 支持 | AnimGraph 预置 Inertialization 节点，Lua 只返回惯性化意图或混合策略 | 待设计 |
| 6 | 调试工具 | 增加当前 Lua 层、状态、动画资源、BlendInput 的日志或调试面板 | 待开始 |
| 7 | 编辑器自动化 | 提供更稳定的一键绑定 ABP、插入 Lua AnimBlueprint Host 节点、设置默认 LayerName 的工具 | 待完善 |
| 8 | 热重载与错误处理 | 梳理 UnLua 热重载缓存、模块路径错误、Lua 异常时的 fallback 策略 | 待完善 |
| 9 | 设计文档同步 | 将旧 Transition 包装方案文档改写为当前 Lua 动画层方案 | 待开始 |
| 10 | Python 动画脚本层评估 | 评估用 Python 替代 Lua 编写现阶段动画蓝图脚本，复用“脚本提交 Pose 或返回兼容决策、C++ 生成快照、动画线程只读快照”的架构 | 待评估 |

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
| 2026-07-07 | 修订：Transition 命名统一为 `CanEnter_<From>_<To>`，`UpdateState_<State>` 保留兼容但不再作为子类必写样板 |
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
