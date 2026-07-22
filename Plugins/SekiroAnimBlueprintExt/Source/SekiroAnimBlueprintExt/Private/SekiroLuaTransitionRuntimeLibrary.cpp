#include "SekiroLuaTransitionRuntimeLibrary.h"

#include "Animation/AnimInstance.h"
#include "SekiroLuaAnimDebugRuntime.h"
#include "Containers/StringConv.h"
#include "LuaEnv.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UnLuaBase.h"
#include "UnLuaModule.h"
#include "UObject/UnrealType.h"
#include "lua.hpp"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroLuaTransitionRuntime, Log, All);

namespace SekiroLuaTransitionRuntimePrivate
{
    /** Inst 代理内保存真实 UObject userdata 的字段名；UnLua 方法调用也识别同一 Object 约定。 */
    constexpr const char* AnimInstanceObjectField = "Object";

    /**
     * 从 Inst 代理读取真实动画实例，并移除临时压栈的 UObject userdata。
     * 本函数只能在当前 UnLua 主状态中调用；代理缺少 Object 或对象已失效时返回 nullptr。
     *
     * @param State 当前 Lua 状态。
     * @param ProxyIndex Inst 代理 table 的栈索引。
     * @return 仍然有效的动画实例；代理无效时返回 nullptr。
     */
    UAnimInstance* GetProxyAnimInstance(lua_State* State, const int32 ProxyIndex)
    {
        lua_getfield(State, ProxyIndex, AnimInstanceObjectField);
        UAnimInstance* AnimInstance = Cast<UAnimInstance>(UnLua::GetUObject(State, -1));
        lua_pop(State, 1);
        return AnimInstance;
    }

    /**
     * 把当前类上的基础标量属性直接压入 Lua，避免动态 Blueprint 类重建后复用 UnLua 旧字段描述。
     * 仅处理 Lua 动画编译器当前可生成的 Bool、Float、Byte、Enum；其他属性交回 UnLua。
     *
     * @param State 当前 Lua 状态。
     * @param AnimInstance 属性所属的真实动画实例。
     * @param Property 从实例当前 UClass 查到的属性。
     * @return 已压入一个 Lua 值时返回 true；属性类型不属于支持集合时返回 false。
     */
    bool PushGeneratedScalarProperty(
        lua_State* State,
        UAnimInstance* AnimInstance,
        FProperty* Property)
    {
        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            lua_pushboolean(State, BoolProperty->GetPropertyValue_InContainer(AnimInstance));
            return true;
        }
        if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            const void* Value = EnumProperty->ContainerPtrToValuePtr<void>(AnimInstance);
            lua_pushinteger(State, EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
            return true;
        }
        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            const void* Value = NumericProperty->ContainerPtrToValuePtr<void>(AnimInstance);
            if (NumericProperty->IsFloatingPoint())
            {
                lua_pushnumber(State, NumericProperty->GetFloatingPointPropertyValue(Value));
            }
            else
            {
                lua_pushinteger(State, static_cast<lua_Integer>(NumericProperty->GetSignedIntPropertyValue(Value)));
            }
            return true;
        }
        return false;
    }

    /**
     * 将 Lua 标量写入实例当前 UClass 上的基础属性。
     * 类型不匹配时产生可由外层 pcall 捕获的 Lua 错误；其他属性返回 false 交回 UnLua。
     *
     * @param State 当前 Lua 状态。
     * @param AnimInstance 属性所属的真实动画实例。
     * @param Property 从实例当前 UClass 查到的属性。
     * @param ValueIndex 待写 Lua 值的栈索引。
     * @return 已处理并完成写入时返回 true；类型不属于支持集合时返回 false。
     */
    bool WriteGeneratedScalarProperty(
        lua_State* State,
        UAnimInstance* AnimInstance,
        FProperty* Property,
        const int32 ValueIndex)
    {
        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            if (!lua_isboolean(State, ValueIndex))
            {
                luaL_error(State, "property '%s' requires boolean", TCHAR_TO_UTF8(*Property->GetName()));
                return true;
            }
            BoolProperty->SetPropertyValue_InContainer(AnimInstance, lua_toboolean(State, ValueIndex) != 0);
            return true;
        }
        if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            if (!lua_isinteger(State, ValueIndex))
            {
                luaL_error(State, "property '%s' requires integer enum value", TCHAR_TO_UTF8(*Property->GetName()));
                return true;
            }
            void* Value = EnumProperty->ContainerPtrToValuePtr<void>(AnimInstance);
            EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Value, lua_tointeger(State, ValueIndex));
            return true;
        }
        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            if (!lua_isnumber(State, ValueIndex)
                || (!NumericProperty->IsFloatingPoint() && !lua_isinteger(State, ValueIndex)))
            {
                luaL_error(State, "property '%s' requires %s", TCHAR_TO_UTF8(*Property->GetName()),
                    NumericProperty->IsFloatingPoint() ? "number" : "integer");
                return true;
            }
            void* Value = NumericProperty->ContainerPtrToValuePtr<void>(AnimInstance);
            if (NumericProperty->IsFloatingPoint())
            {
                NumericProperty->SetFloatingPointPropertyValue(Value, lua_tonumber(State, ValueIndex));
            }
            else
            {
                NumericProperty->SetIntPropertyValue(Value, lua_tointeger(State, ValueIndex));
            }
            return true;
        }
        return false;
    }

    /**
     * 实现 Inst.Property 读取：基础标量按当前动态类反射读取，其余字段和函数委托给原始 UnLua UObject。
     * 函数由 Lua VM 在游戏线程调用。
     *
     * @param State 栈 1 为 Inst 代理、栈 2 为字段名。
     * @return 始终返回一个字段值。
     */
    int32 AnimInstanceProxyIndex(lua_State* State)
    {
        UAnimInstance* AnimInstance = GetProxyAnimInstance(State, 1);
        const char* FieldName = lua_tostring(State, 2);
        if (AnimInstance == nullptr || FieldName == nullptr) return luaL_error(State, "invalid animation instance field read");

        FProperty* Property = AnimInstance->GetClass()->FindPropertyByName(UTF8_TO_TCHAR(FieldName));
        if (Property != nullptr && PushGeneratedScalarProperty(State, AnimInstance, Property)) return 1;

        lua_getfield(State, 1, AnimInstanceObjectField);
        lua_pushvalue(State, 2);
        lua_gettable(State, -2);
        lua_remove(State, -2);
        return 1;
    }

    /**
     * 实现 Inst.Property = Value：基础标量按当前动态类反射写入，其余字段委托给原始 UnLua UObject。
     * 因而业务 Lua 保持直接成员赋值语义，不需要任何 SetLuaXxx 包装。
     *
     * @param State 栈 1 为 Inst 代理、栈 2 为字段名、栈 3 为待写值。
     * @return 不向 Lua 返回值。
     */
    int32 AnimInstanceProxyNewIndex(lua_State* State)
    {
        UAnimInstance* AnimInstance = GetProxyAnimInstance(State, 1);
        const char* FieldName = lua_tostring(State, 2);
        if (AnimInstance == nullptr || FieldName == nullptr) return luaL_error(State, "invalid animation instance field write");

        FProperty* Property = AnimInstance->GetClass()->FindPropertyByName(UTF8_TO_TCHAR(FieldName));
        if (Property != nullptr && WriteGeneratedScalarProperty(State, AnimInstance, Property, 3)) return 0;

        lua_getfield(State, 1, AnimInstanceObjectField);
        lua_pushvalue(State, 2);
        lua_pushvalue(State, 3);
        lua_settable(State, -3);
        lua_pop(State, 1);
        return 0;
    }

    /**
     * 创建保持 UnLua Object 约定的 Inst 代理。
     * 代理只存在于单次 Lua 调用栈；Object 字段确保原生 UFunction closure 仍能把代理解析为真实 UObject。
     *
     * @param State 当前 UnLua 主状态。
     * @param AnimInstance 要暴露给 Lua 的真实动画实例。
     * @return 无返回值；代理 table 留在 Lua 栈顶。
     */
    void PushAnimInstanceProxy(lua_State* State, UAnimInstance* AnimInstance)
    {
        lua_newtable(State);
        lua_pushstring(State, AnimInstanceObjectField);
        UnLua::PushUObject(State, AnimInstance);
        lua_rawset(State, -3);

        lua_newtable(State);
        lua_pushstring(State, "__index");
        lua_pushcfunction(State, &AnimInstanceProxyIndex);
        lua_rawset(State, -3);
        lua_pushstring(State, "__newindex");
        lua_pushcfunction(State, &AnimInstanceProxyNewIndex);
        lua_rawset(State, -3);
        lua_setmetatable(State, -2);
    }

    /**
     * 从已 require 的模块解析函数；若函数尚未导出，则先调用无参 CompileIR 完成 fresh require 初始化再重试。
     * 函数只能在游戏线程、持有当前 UnLua 主栈时调用；成功后目标函数位于栈顶，失败时栈内容由外层作用域统一恢复。
     *
     * @param State 当前 UnLua 主状态。
     * @param ModuleTableIndex require 返回模块 table 的绝对栈索引。
     * @param FunctionName 要解析的导出函数 UTF-8 名称。
     * @return 栈顶为目标函数时返回 true；CompileIR 不存在、执行失败或重试仍不是函数时返回 false。
     */
    bool PushRuntimeFunction(lua_State* State, const int32 ModuleTableIndex, const FString& FunctionName)
    {
        const FTCHARToUTF8 FunctionUtf8(*FunctionName);
        if (lua_getfield(State, ModuleTableIndex, FunctionUtf8.Get()) == LUA_TFUNCTION) return true;
        lua_pop(State, 1);

        if (lua_getfield(State, ModuleTableIndex, "CompileIR") != LUA_TFUNCTION)
        {
            lua_pop(State, 1);
            return false;
        }
        if (lua_pcall(State, 0, 1, 0) != LUA_OK) return false;
        lua_pop(State, 1);
        return lua_getfield(State, ModuleTableIndex, FunctionUtf8.Get()) == LUA_TFUNCTION;
    }

    /**
     * 将旧式 Lua Transition 的失败尝试写入统一快照接口。
     * 调试未启用时底层立即返回；ErrorText 保持原始错误文本，供 UI/JSONL 精确显示。
     */
    void RecordFailedTransition(
        UAnimInstance* AnimInstance,
        const FString& RuleFunctionName,
        const FString& Phase,
        const FString& ErrorText)
    {
        FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, RuleFunctionName, TEXT("LuaTransitionRule"), Phase, TEXT("Error"), ErrorText,
            FString(), FString(), false, false, true);
    }
}

/**
 * 在游戏线程调用 Lua 主模块的 BlueprintUpdateAnimation(Inst, DeltaSeconds)，允许脚本直接写入生成成员变量。
 * fresh require 时会通过 CompileIR 初始化动态导出；函数不参与动画线程 Pose 求值，失败只记录日志并返回 false。
 *
 * @param AnimInstance 当前生成 AnimBlueprint 的真实实例，不能为空。
 * @param LuaModuleName require 模块名，不是文件路径。
 * @param DeltaSeconds 当前 BlueprintUpdateAnimation 帧间隔，单位秒。
 * @return Lua 调用成功时返回 true；线程、模块、函数或执行失败时返回 false。
 */
bool USekiroLuaTransitionRuntimeLibrary::EvaluateBlueprintUpdateAnimation(
    UAnimInstance* AnimInstance,
    const FString& LuaModuleName,
    const float DeltaSeconds)
{
    using namespace SekiroLuaTransitionRuntimePrivate;

    if (!IsInGameThread()
        || !IsValid(AnimInstance)
        || AnimInstance->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        || LuaModuleName.IsEmpty())
    {
        return false;
    }
    FSekiroLuaAnimDebugRuntime::RecordAnimInstanceModule(AnimInstance, LuaModuleName);
    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (UnLuaModule == nullptr) return false;
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(AnimInstance);
    if (Environment == nullptr) return false;

    lua_State* State = Environment->GetMainState();
    const int32 InitialTop = lua_gettop(State);
    ON_SCOPE_EXIT { lua_settop(State, InitialTop); };
    const FTCHARToUTF8 ModuleUtf8(*LuaModuleName);
    lua_getglobal(State, "require");
    lua_pushlstring(State, ModuleUtf8.Get(), ModuleUtf8.Length());
    if (lua_pcall(State, 1, 1, 0) != LUA_OK || !lua_istable(State, -1)) return false;
    const int32 ModuleTableIndex = lua_absindex(State, -1);
    if (!PushRuntimeFunction(State, ModuleTableIndex, TEXT("BlueprintUpdateAnimation"))) return false;

    PushAnimInstanceProxy(State, AnimInstance);
    lua_pushnumber(State, static_cast<lua_Number>(DeltaSeconds));
    if (lua_pcall(State, 2, 0, 0) == LUA_OK) return true;
    UE_LOG(LogSekiroLuaTransitionRuntime, Error, TEXT("Lua update '%s.BlueprintUpdateAnimation' failed: %s"),
        *LuaModuleName, UTF8_TO_TCHAR(lua_tostring(State, -1)));
    return false;
}

/**
 * 从当前 Transition Rule Graph 按需 require Lua 模块，经 table/metatable 查找规则函数，
 * 并以 AnimInstance 代理作为 Inst 参数直接执行。规则约定为只读，本函数不缓存也不预计算其他 Transition。
 * 函数只接受严格 Lua boolean 返回；任意加载、查找、调用或类型错误均记录日志并返回 false。
 * 必须在游戏线程调用；Lua 来源 AnimBlueprint 由编辑器工厂强制关闭多线程动画更新。
 * Lua 栈在所有路径恢复，函数不保留 Lua wrapper 或 table 引用。
 *
 * @param AnimInstance 本次规则所属的实际动画实例；不能为空，函数不保留引用。
 * @param LuaModuleName 交给 require 的模块名，不是文件系统路径，不能为空。
 * @param RuleFunctionName 从模块导出 table（含 __index 继承链）查找的函数名，不能为空。
 * @return Lua 严格返回 true 时返回 true；其他返回、错误或线程不符时返回 false。
 */
bool USekiroLuaTransitionRuntimeLibrary::EvaluateLuaTransitionRule(
    UAnimInstance* AnimInstance,
    const FString& LuaModuleName,
    const FString& RuleFunctionName)
{
    using namespace SekiroLuaTransitionRuntimePrivate;

    if (!IsInGameThread())
    {
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("EvaluateLuaTransitionRule must run on the game thread."));
        return false;
    }
    if (!IsValid(AnimInstance)
        || AnimInstance->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        || LuaModuleName.IsEmpty()
        || RuleFunctionName.IsEmpty())
    {
        return false;
    }
    FSekiroLuaAnimDebugRuntime::RecordAnimInstanceModule(AnimInstance, LuaModuleName);

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (UnLuaModule == nullptr)
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("Module"), TEXT("UnLua module is unavailable."));
        UE_LOG(LogSekiroLuaTransitionRuntime, Error, TEXT("UnLua module is unavailable."));
        return false;
    }
    if (!UnLuaModule->IsActive())
    {
        UnLuaModule->SetActive(true);
    }

    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(AnimInstance);
    if (Environment == nullptr)
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("Environment"), TEXT("UnLua environment is unavailable."));
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("UnLua environment is unavailable for AnimInstance '%s'."),
            *AnimInstance->GetPathName());
        return false;
    }

    lua_State* State = Environment->GetMainState();
    const int32 InitialTop = lua_gettop(State);
    ON_SCOPE_EXIT { lua_settop(State, InitialTop); };

    const FTCHARToUTF8 ModuleUtf8(*LuaModuleName);
    lua_getglobal(State, "require");
    lua_pushlstring(State, ModuleUtf8.Get(), ModuleUtf8.Length());
    if (lua_pcall(State, 1, 1, 0) != LUA_OK)
    {
        const FString Error = UTF8_TO_TCHAR(lua_tostring(State, -1));
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("Require"), Error);
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("require('%s') failed: %s"), *LuaModuleName, *Error);
        return false;
    }
    if (!lua_istable(State, -1))
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("RequireType"), TEXT("Lua module did not return a table."));
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("Lua module '%s' did not return a table."), *LuaModuleName);
        return false;
    }

    const int32 ModuleTableIndex = lua_absindex(State, -1);
    if (!PushRuntimeFunction(State, ModuleTableIndex, RuleFunctionName))
    {
        const FString Error = lua_isstring(State, -1)
            ? UTF8_TO_TCHAR(lua_tostring(State, -1))
            : FString::Printf(TEXT("Function '%s' is not exported."), *RuleFunctionName);
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("Function"), Error);
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("Lua module '%s' does not export function '%s'."),
            *LuaModuleName,
            *RuleFunctionName);
        return false;
    }

    PushAnimInstanceProxy(State, AnimInstance);
    if (lua_pcall(State, 1, 1, 0) != LUA_OK)
    {
        const FString Error = UTF8_TO_TCHAR(lua_tostring(State, -1));
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("PCall"), Error);
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("Lua transition rule '%s.%s' failed: %s"),
            *LuaModuleName,
            *RuleFunctionName,
            *Error);
        return false;
    }
    if (!lua_isboolean(State, -1))
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("ReturnType"), TEXT("Lua Transition must return strict boolean."));
        UE_LOG(LogSekiroLuaTransitionRuntime, Error,
            TEXT("Lua transition rule '%s.%s' must return boolean."),
            *LuaModuleName,
            *RuleFunctionName);
        return false;
    }

    const bool bResult = lua_toboolean(State, -1) != 0;
    FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
        AnimInstance,
        RuleFunctionName,
        TEXT("LuaTransitionRule"),
        TEXT("ReturnValue"),
        TEXT("Bool"),
        bResult ? TEXT("true") : TEXT("false"),
        FString(),
        FString(),
        bResult,
        bResult,
        true);
    return bResult;
}

/**
 * 记录 Bool 叶或最终表达式并原样透传 Result，供生成的 Transition Graph 插桩。
 * 仅游戏线程且调试/快照开启时记录；禁用时无日志且不改变图语义。
 */
bool USekiroLuaTransitionRuntimeLibrary::RecordBoolTransitionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const FString& ParameterName,
    const bool ActualValue,
    const bool ExpectedValue,
    const bool Result,
    const bool bIsFinal)
{
    if (FSekiroLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, ParameterName, TEXT("Bool"),
            ActualValue ? TEXT("true") : TEXT("false"), ExpectedValue ? TEXT("true") : TEXT("false"), FString(),
            Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}

/**
 * 记录 Float 叶的实际值、阈值与结果并原样透传 Result，供生成图插桩。
 * 所有文本使用稳定数值格式；禁用采样时不分配调试快照。
 */
bool USekiroLuaTransitionRuntimeLibrary::RecordFloatTransitionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const FString& ParameterName,
    const float ActualValue,
    const float Threshold,
    const bool Result,
    const bool bIsFinal)
{
    if (FSekiroLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, ParameterName, TEXT("Float"),
            FString::SanitizeFloat(ActualValue), FString(), FString::SanitizeFloat(Threshold),
            Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}

/**
 * 记录无标量参数的逻辑/最终表达式并原样透传 Result。
 * bIsFinal 为 true 时 RuleResult 才具有权威含义；函数不触发 Lua 或日志。
 */
bool USekiroLuaTransitionRuntimeLibrary::RecordTransitionExpressionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const bool Result,
    const bool bIsFinal)
{
    if (FSekiroLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, FString(), TEXT("Expression"),
            Result ? TEXT("true") : TEXT("false"), FString(), FString(), Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}
