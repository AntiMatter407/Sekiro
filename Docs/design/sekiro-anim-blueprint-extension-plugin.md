# Sekiro AnimBlueprint Extension 插件 - 技术方案

> 更新日期：2026-07-08  
> 状态：历史方案，记录早期 Lua Transition 包装以及随后采用的 Lua AnimBlueprint Host 演进；两者都已被当前原生 AnimBlueprint 编译方案替代。
> 当前主线请以 `Docs/plan/lua-anim-blueprint-compiler.md`、`Docs/design/lua-anim-blueprint-compiler.md` 和 `Docs/lua-anim-blueprint-authoring-guide.md` 为准。

## 1. 当前目标

插件名为 `SekiroAnimBlueprintExt`，不是单一 Transition 工具，而是 AnimBlueprint 扩展插件。第一阶段只实现 Lua Transition：

- 游戏动画实例 `USKAnimInstance` 继承插件基类 `USekiroAnimBlueprintInstance`。
- 插件编辑器工具批量包装 AnimBlueprint 中的 Transition Rule Graph。
- 每条 Transition 保留原有蓝图规则，外层追加 `ResolveLuaTransitionOverride(...)`。
- Lua 有对应逻辑时覆盖原规则；Lua 无对应逻辑时回退原蓝图规则。
- 不再做每条 Transition 的 Lua 规则勾选项。

## 2. 运行时接口

插件基类：

```cpp
UCLASS(Blueprintable, BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroAnimBlueprintInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Transition")
    bool ResolveLuaTransitionOverride(FName StateMachinePath, FName FromState, FName ToState, bool bOriginalCanEnter);

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sekiro|Lua Transition")
    bool TryEvaluateLuaTransitionOverride(FName StateMachinePath, FName FromState, FName ToState, bool& bOutCanEnter);
};
```

契约：

- `ResolveLuaTransitionOverride` 由 Transition Rule Graph 调用。
- `TryEvaluateLuaTransitionOverride` 由项目侧或 Lua 桥接层实现。
- `TryEvaluateLuaTransitionOverride` 返回 `true` 表示 Lua 命中，并把最终结果写入 `bOutCanEnter`。
- 返回 `false` 表示没有 Lua 规则，继续使用 `bOriginalCanEnter`。

## 3. 状态机 Lua 文件粒度

Lua 文件不按 AnimBlueprint 粒度绑定，而是按状态机绑定。状态机可以是顶层状态机，也可以是子状态机。

编辑器包装 Transition 时会生成 `StateMachinePath`：

```text
AnimGraph/RootSM
AnimGraph/RootSM/LocomotionState/GroundedSM
AnimGraph/RootSM/CombatState/AttackSubSM
AnimLayer/UpperBodyLayer/UpperBodySM
AnimLayer/UpperBodyLayer/AimState/AimSubSM
```

路径规则：

- 主 AnimGraph 状态机：`AnimGraph/状态机名`
- 主 AnimGraph 子状态机：`AnimGraph/父状态机/所属父状态/子状态机`
- 动画层状态机：`AnimLayer/动画层图名/状态机名`
- 动画层子状态机：`AnimLayer/动画层图名/父状态机/所属父状态/子状态机`
- 继续嵌套时按同样规则向外递归

这样每个子状态机都能对应独立 Lua 文件，例如：

```text
Script/AnimBlueprint/ABP_Sekiro/AnimGraph/RootSM.lua
Script/AnimBlueprint/ABP_Sekiro/AnimGraph/RootSM/LocomotionState/GroundedSM.lua
Script/AnimBlueprint/ABP_Sekiro/AnimLayer/UpperBodyLayer/UpperBodySM.lua
```

实际路径或 Lua module 命名由项目侧 `TryEvaluateLuaTransitionOverride` 统一映射。

## 4. 动画蓝图中的 Lua 配置

Lua 文件配置放在 AnimBlueprint 的 Class Defaults 里，也就是 `USekiroAnimBlueprintInstance` 或 `USKAnimInstance` 暴露出来的默认属性：

```text
Sekiro | Lua Transition
├─ LuaTransitionModuleRoot
└─ StateMachineLuaBindings
   ├─ StateMachinePath
   └─ LuaModuleName
```

推荐配置方式：

```text
LuaTransitionModuleRoot = AnimBlueprint.ABP_Sekiro
```

这样状态机会自动推导 Lua module：

```text
AnimGraph/RootSM                                -> AnimBlueprint.ABP_Sekiro.AnimGraph.RootSM
AnimGraph/RootSM/LocomotionState/GroundedSM     -> AnimBlueprint.ABP_Sekiro.AnimGraph.RootSM.LocomotionState.GroundedSM
AnimLayer/UpperBodyLayer/UpperBodySM            -> AnimBlueprint.ABP_Sekiro.AnimLayer.UpperBodyLayer.UpperBodySM
```

如果某个状态机需要特殊 Lua 文件，则在 `StateMachineLuaBindings` 添加精确映射：

```text
StateMachinePath = AnimGraph/RootSM/LocomotionState/GroundedSM
LuaModuleName    = AnimBlueprint.ABP_Sekiro.Locomotion.Grounded
```

动画层里的状态机同理：

```text
StateMachinePath = AnimLayer/UpperBodyLayer/UpperBodySM
LuaModuleName    = AnimBlueprint.ABP_Sekiro.UpperBody.Main
```

精确映射优先级高于 `LuaTransitionModuleRoot` 自动推导。

## 5. Transition 包装方式

原始 Transition Rule Graph：

```text
OriginalRule -> Result.bCanEnterTransition
```

插件包装后：

```text
OriginalRule
    -> ResolveLuaTransitionOverride(StateMachinePath, FromState, ToState, bOriginalCanEnter)
    -> Result.bCanEnterTransition
```

如果原规则没有连线，插件会把 `Result.bCanEnterTransition` 原默认值写入 `bOriginalCanEnter`。

## 6. Lua 侧建议

建议每个状态机 Lua 文件暴露按 Transition 命名的函数：

```lua
local M = {}

function M.Idle_To_Walk(ctx)
    local anim = ctx.AnimInstance
    return anim and anim.Speed >= 10
end

return M
```

项目侧 `TryEvaluateLuaTransitionOverride` 可以按以下方式分发：

```text
StateMachinePath -> Lua module
FromState + ToState -> Lua function
```

伪流程：

```text
TryEvaluateLuaTransitionOverride(StateMachinePath, FromState, ToState)
    module = LoadLuaModule(StateMachinePath)
    functionName = FromState .. "_To_" .. ToState
    if module/function 不存在:
        return false
    bOutCanEnter = module[functionName](ctx)
    return true
```

## 7. 编辑器 API

`USekiroAnimBlueprintEditorLibrary` 提供：

- `InstallLuaTransitionOverrides(UAnimBlueprint* AnimBlueprint)`：批量包装整张 AnimBlueprint 的 Transition。
- `WrapTransitionWithLuaOverride(UAnimStateTransitionNode* TransitionNode)`：包装单条 Transition。
- `IsTransitionWrappedWithLuaOverride(const UAnimStateTransitionNode* TransitionNode)`：判断是否已经包装。

当前实现没有注册 Transition Details 面板，也没有逐条勾选字段。

## 8. 已知后续工作

- 实现项目侧 UnLua 调用。
- 增加编辑器菜单或 AIBridge 命令，触发 `InstallLuaTransitionOverrides`。
- 对已包装节点做刷新/重装工具。
- 增加 Lua 热重载、错误日志、耗时统计。
- 评估并行动画更新时的缓存模式，避免 worker thread 直接访问 Lua state。

## 9. 验证

当前 C++ 验证命令：

```powershell
& 'F:/UnrealEngine-5.2/Engine/Build/BatchFiles/Build.bat' SekiroEditor Win64 Development -Project='F:/ProjectAI/Sekiro/Sekiro.uproject' -WaitMutex
```

2026-07-06 验证结果：编译通过；仅 `Plugins/UnLua` 中存在 UE5.2 弃用警告。
