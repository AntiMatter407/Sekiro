#include "LuaTransitionRuntimeLibrary.h"

#include "Animation/AnimInstance.h"
#include "LuaAnimDebugRuntime.h"
#include "Containers/StringConv.h"
#include "LuaEnv.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UnLuaBase.h"
#include "UnLuaModule.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakFieldPtr.h"
#include "lua.hpp"

DEFINE_LOG_CATEGORY_STATIC(LogLuaTransitionRuntime, Log, All);

namespace LuaTransitionRuntimePrivate
{
    /** Inst 代理内保存真实 UObject userdata 的字段名；UnLua 方法调用也识别同一 Object 约定。 */
    constexpr const char* AnimInstanceObjectField = "Object";
    /** 所有单帧 Inst 代理共用的 Lua registry metatable 名。 */
    constexpr const char* AnimInstanceProxyMetatable = "Lua.AnimInstanceProxy";
    TMap<TWeakObjectPtr<UAnimInstance>, FString> FailedBlueprintUpdateModules; // 本次运行不再重试的更新模块
    TMap<TWeakObjectPtr<UClass>, TMap<FName, TWeakFieldPtr<FProperty>>> GeneratedPropertyCache; // 动态类属性弱缓存

    /**
     * 从 package.loaded 读取已加载模块，不触发 require、CompileIR 或搜索器。
     * 只能在持有当前 Lua 栈的游戏线程调用；成功时模块 table 留在栈顶，失败时栈由外层恢复。
     *
     * @param State 当前 UnLua 主状态。
     * @param LuaModuleName 点分 Lua 模块名，不能为空。
     * @return package.loaded 中存在 table 时返回 true，否则返回 false。
     */
    bool PushLoadedModule(lua_State* State, const FString& LuaModuleName)
    {
        lua_getglobal(State, "package");
        if (!lua_istable(State, -1)) return false;
        lua_getfield(State, -1, "loaded");
        if (!lua_istable(State, -1)) return false;
        const FTCHARToUTF8 ModuleUtf8(*LuaModuleName);
        lua_pushlstring(State, ModuleUtf8.Get(), ModuleUtf8.Length());
        lua_rawget(State, -2);
        return lua_istable(State, -1);
    }

    /**
     * 首次运行实例尚未加载主模块时调用一次 require；模块 Export 不再执行 CompileIR。
     * 本函数只能在游戏线程调用；成功时模块 table 位于栈顶，失败时错误对象位于栈顶。
     *
     * @param State 当前 UnLua 主状态。
     * @param LuaModuleName 点分 Lua 模块名，不能为空。
     * @return require 成功且返回 table 时返回 true，否则返回 false。
     */
    bool RequireRuntimeModule(lua_State* State, const FString& LuaModuleName)
    {
        const FTCHARToUTF8 ModuleUtf8(*LuaModuleName);
        lua_getglobal(State, "require");
        lua_pushlstring(State, ModuleUtf8.Get(), ModuleUtf8.Length());
        return lua_pcall(State, 1, 1, 0) == LUA_OK && lua_istable(State, -1);
    }

    /**
     * 优先复用 package.loaded，只在当前实例首次更新且模块尚未加载时执行一次 require。
     * 函数不调用 CompileIR；成功时模块 table 位于栈顶，失败时保留 require 错误供调用方记录。
     *
     * @param State 当前 UnLua 主状态。
     * @param LuaModuleName 点分 Lua 模块名，不能为空。
     * @return 找到或首次加载模块 table 时返回 true，否则返回 false。
     */
    bool PushOrRequireRuntimeModule(lua_State* State, const FString& LuaModuleName)
    {
        const int32 InitialTop = lua_gettop(State);
        if (PushLoadedModule(State, LuaModuleName)) return true;
        lua_settop(State, InitialTop);
        return RequireRuntimeModule(State, LuaModuleName);
    }

    /**
     * 只解析模块已导出的函数，不触发兼容用 CompileIR。
     * 调用方持有模块 table；成功时函数留在栈顶，失败时弹出非函数值。
     *
     * @param State 当前 UnLua 主状态。
     * @param ModuleTableIndex 模块 table 的绝对栈索引。
     * @param FunctionName 要解析的导出函数名。
     * @return 已找到函数时返回 true，否则返回 false。
     */
    bool PushExportedFunction(
        lua_State* State,
        const int32 ModuleTableIndex,
        const FString& FunctionName)
    {
        const FTCHARToUTF8 FunctionUtf8(*FunctionName);
        if (lua_getfield(State, ModuleTableIndex, FunctionUtf8.Get()) == LUA_TFUNCTION) return true;
        lua_pop(State, 1);
        return false;
    }

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
     * 按当前动态 AnimInstance 类缓存属性描述，避免 Lua 高频字段访问重复遍历生成类继承链。
     * 只能在游戏线程使用；弱字段引用会在蓝图重编译后重新解析，编辑器编译边界也会主动清空缓存。
     *
     * @param AnimInstance 属性所属的真实动画实例，不能为空。
     * @param FieldNameUtf8 Lua VM 提供的 UTF-8 字段名，不能为空。
     * @return 当前动态类上有效的反射属性；字段已删除或未知时返回 nullptr，未命中结果不缓存。
     */
    FProperty* FindCachedAnimInstanceProperty(
        UAnimInstance* AnimInstance,
        const char* FieldNameUtf8)
    {
        UClass* InstanceClass = AnimInstance->GetClass();
        const FName FieldName(UTF8_TO_TCHAR(FieldNameUtf8));
        TMap<FName, TWeakFieldPtr<FProperty>>& ClassProperties =
            GeneratedPropertyCache.FindOrAdd(TWeakObjectPtr<UClass>(InstanceClass));
        TWeakFieldPtr<FProperty>* CachedProperty = ClassProperties.Find(FieldName);
        if (CachedProperty != nullptr)
        {
            if (FProperty* Property = CachedProperty->Get()) return Property;
            ClassProperties.Remove(FieldName);
        }

        FProperty* Property = InstanceClass->FindPropertyByName(FieldName);
        if (Property != nullptr) ClassProperties.Add(FieldName, TWeakFieldPtr<FProperty>(Property));
        return Property;
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

        FProperty* Property = FindCachedAnimInstanceProperty(AnimInstance, FieldName);
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

        FProperty* Property = FindCachedAnimInstanceProperty(AnimInstance, FieldName);
        if (Property != nullptr && WriteGeneratedScalarProperty(State, AnimInstance, Property, 3)) return 0;

        lua_getfield(State, 1, AnimInstanceObjectField);
        lua_pushvalue(State, 2);
        lua_pushvalue(State, 3);
        lua_settable(State, -3);
        lua_pop(State, 1);
        return 0;
    }

    /**
     * 创建保持 UnLua Object 约定的单帧 Inst 代理，并复用 registry 内唯一 metatable。
     * 代理 table 只存在于单次调用栈；Object 字段确保原生 UFunction closure 仍能解析真实 UObject。
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

        if (luaL_newmetatable(State, AnimInstanceProxyMetatable) != 0)
        {
            lua_pushstring(State, "__index");
            lua_pushcfunction(State, &AnimInstanceProxyIndex);
            lua_rawset(State, -3);
            lua_pushstring(State, "__newindex");
            lua_pushcfunction(State, &AnimInstanceProxyNewIndex);
            lua_rawset(State, -3);
        }
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
        FLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, RuleFunctionName, TEXT("LuaTransitionRule"), Phase, TEXT("Error"), ErrorText,
            FString(), FString(), false, false, true);
    }
}

/**
 * 在游戏线程调用 Lua 主模块的 BlueprintUpdateAnimation(Inst, DeltaSeconds)，允许脚本直接写入生成成员变量。
 * 首帧只在 package.loaded 缺失时加载一次模块，不执行 CompileIR；任一失败在本次 PIE 内停止重试，避免逐帧异常。
 *
 * @param AnimInstance 当前生成 AnimBlueprint 的真实实例，不能为空。
 * @param LuaModuleName require 模块名，不是文件路径。
 * @param DeltaSeconds 当前 BlueprintUpdateAnimation 帧间隔，单位秒。
 * @return Lua 调用成功时返回 true；线程、模块、函数或执行失败时返回 false。
 */
bool ULuaTransitionRuntimeLibrary::EvaluateBlueprintUpdateAnimation(
    UAnimInstance* AnimInstance,
    const FString& LuaModuleName,
    const float DeltaSeconds)
{
    using namespace LuaTransitionRuntimePrivate;

    if (!IsInGameThread()
        || !IsValid(AnimInstance)
        || AnimInstance->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        || LuaModuleName.IsEmpty())
    {
        return false;
    }
    const TWeakObjectPtr<UAnimInstance> WeakAnimInstance(AnimInstance);
    const FString* FailedModule = FailedBlueprintUpdateModules.Find(WeakAnimInstance);
    if (FailedModule != nullptr && *FailedModule == LuaModuleName) return false;

    FLuaAnimDebugRuntime::RecordAnimInstanceModule(AnimInstance, LuaModuleName);
    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (UnLuaModule == nullptr)
    {
        FailedBlueprintUpdateModules.Add(WeakAnimInstance, LuaModuleName);
        UE_LOG(LogLuaTransitionRuntime, Error, TEXT("UnLua module is unavailable; Lua animation update will not retry during this PIE."));
        return false;
    }
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(AnimInstance);
    if (Environment == nullptr)
    {
        FailedBlueprintUpdateModules.Add(WeakAnimInstance, LuaModuleName);
        UE_LOG(
            LogLuaTransitionRuntime,
            Error,
            TEXT("UnLua environment is unavailable for '%s'; Lua animation update will not retry during this PIE."),
            *AnimInstance->GetPathName());
        return false;
    }

    lua_State* State = Environment->GetMainState();
    const int32 InitialTop = lua_gettop(State);
    ON_SCOPE_EXIT { lua_settop(State, InitialTop); };
    if (!PushOrRequireRuntimeModule(State, LuaModuleName))
    {
        const FString Error = lua_isstring(State, -1)
            ? UTF8_TO_TCHAR(lua_tostring(State, -1))
            : TEXT("module did not return a table");
        FailedBlueprintUpdateModules.Add(WeakAnimInstance, LuaModuleName);
        UE_LOG(
            LogLuaTransitionRuntime,
            Error,
            TEXT("Lua update module '%s' failed to load once and will not retry during this PIE: %s"),
            *LuaModuleName,
            *Error);
        return false;
    }
    const int32 ModuleTableIndex = lua_absindex(State, -1);
    if (!PushExportedFunction(State, ModuleTableIndex, TEXT("BlueprintUpdateAnimation")))
    {
        FailedBlueprintUpdateModules.Add(WeakAnimInstance, LuaModuleName);
        UE_LOG(
            LogLuaTransitionRuntime,
            Error,
            TEXT("Lua update module '%s' does not export BlueprintUpdateAnimation and will not retry during this PIE."),
            *LuaModuleName);
        return false;
    }

    PushAnimInstanceProxy(State, AnimInstance);
    lua_pushnumber(State, static_cast<lua_Number>(DeltaSeconds));
    if (lua_pcall(State, 2, 0, 0) == LUA_OK)
    {
        FailedBlueprintUpdateModules.Remove(WeakAnimInstance);
        return true;
    }
    FailedBlueprintUpdateModules.Add(WeakAnimInstance, LuaModuleName);
    UE_LOG(LogLuaTransitionRuntime, Error, TEXT("Lua update '%s.BlueprintUpdateAnimation' failed: %s"),
        *LuaModuleName, UTF8_TO_TCHAR(lua_tostring(State, -1)));
    return false;
}

/**
 * 从当前 Transition Rule Graph 按需 require Lua 模块，经 table/metatable 查找规则函数，
 * 并以 AnimInstance 代理作为 Inst 参数直接执行。规则约定为只读，本函数不缓存也不预计算其他 Transition。
 * 函数只接受严格 Lua boolean 返回；任意加载、查找、调用或类型错误均记录日志并返回 false。
 * 必须在游戏线程调用；包含该兼容节点的 AnimBlueprint 由编辑器工厂自动关闭多线程动画更新。
 * Lua 栈在所有路径恢复，函数不保留 Lua wrapper 或 table 引用。
 *
 * @param AnimInstance 本次规则所属的实际动画实例；不能为空，函数不保留引用。
 * @param LuaModuleName 交给 require 的模块名，不是文件系统路径，不能为空。
 * @param RuleFunctionName 从模块导出 table（含 __index 继承链）查找的函数名，不能为空。
 * @return Lua 严格返回 true 时返回 true；其他返回、错误或线程不符时返回 false。
 */
bool ULuaTransitionRuntimeLibrary::EvaluateLuaTransitionRule(
    UAnimInstance* AnimInstance,
    const FString& LuaModuleName,
    const FString& RuleFunctionName)
{
    using namespace LuaTransitionRuntimePrivate;

    if (!IsInGameThread())
    {
        UE_LOG(LogLuaTransitionRuntime, Error,
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
    FLuaAnimDebugRuntime::RecordAnimInstanceModule(AnimInstance, LuaModuleName);

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (UnLuaModule == nullptr)
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("Module"), TEXT("UnLua module is unavailable."));
        UE_LOG(LogLuaTransitionRuntime, Error, TEXT("UnLua module is unavailable."));
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
        UE_LOG(LogLuaTransitionRuntime, Error,
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
        UE_LOG(LogLuaTransitionRuntime, Error,
            TEXT("require('%s') failed: %s"), *LuaModuleName, *Error);
        return false;
    }
    if (!lua_istable(State, -1))
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("RequireType"), TEXT("Lua module did not return a table."));
        UE_LOG(LogLuaTransitionRuntime, Error,
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
        UE_LOG(LogLuaTransitionRuntime, Error,
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
        UE_LOG(LogLuaTransitionRuntime, Error,
            TEXT("Lua transition rule '%s.%s' failed: %s"),
            *LuaModuleName,
            *RuleFunctionName,
            *Error);
        return false;
    }
    if (!lua_isboolean(State, -1))
    {
        RecordFailedTransition(AnimInstance, RuleFunctionName, TEXT("ReturnType"), TEXT("Lua Transition must return strict boolean."));
        UE_LOG(LogLuaTransitionRuntime, Error,
            TEXT("Lua transition rule '%s.%s' must return boolean."),
            *LuaModuleName,
            *RuleFunctionName);
        return false;
    }

    const bool bResult = lua_toboolean(State, -1) != 0;
    FLuaAnimDebugRuntime::RecordTransitionValue(
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
 * 可由并行动画线程调用；启用采样时提交线程安全队列，禁用时只执行一次原子读取且不改变图语义。
 */
bool ULuaTransitionRuntimeLibrary::RecordBoolTransitionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const FString& ParameterName,
    const bool ActualValue,
    const bool ExpectedValue,
    const bool Result,
    const bool bIsFinal)
{
    if (FLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, ParameterName, TEXT("Bool"),
            ActualValue ? TEXT("true") : TEXT("false"), ExpectedValue ? TEXT("true") : TEXT("false"), FString(),
            Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}

/**
 * 记录 Float 叶的实际值、阈值与结果并原样透传 Result，供生成图插桩。
 * 可由并行动画线程调用；所有文本使用稳定数值格式，禁用采样时不分配调试快照。
 */
bool ULuaTransitionRuntimeLibrary::RecordFloatTransitionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const FString& ParameterName,
    const float ActualValue,
    const float Threshold,
    const bool Result,
    const bool bIsFinal)
{
    if (FLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, ParameterName, TEXT("Float"),
            FString::SanitizeFloat(ActualValue), FString(), FString::SanitizeFloat(Threshold),
            Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}

/**
 * 记录无标量参数的逻辑/最终表达式并原样透传 Result。
 * 可由并行动画线程调用；bIsFinal 为 true 时 RuleResult 才具有权威含义，函数不触发 Lua 或日志。
 */
bool ULuaTransitionRuntimeLibrary::RecordTransitionExpressionDebugValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const bool Result,
    const bool bIsFinal)
{
    if (FLuaAnimDebugRuntime::IsSamplingEnabled())
    {
        FLuaAnimDebugRuntime::RecordTransitionValue(
            AnimInstance, TransitionId, ExpressionLabel, FString(), TEXT("Expression"),
            Result ? TEXT("true") : TEXT("false"), FString(), FString(), Result, bIsFinal ? Result : false, bIsFinal);
    }
    return Result;
}

/**
 * 清除当前运行的 Lua Update 失败抑制与动态类属性缓存，隔离蓝图 Reinstance 前后的反射描述。
 * 只能由编辑器蓝图编译或 PIE 起止边界在游戏线程调用；不卸载 package.loaded，也不触发 Lua 编译或热重载。
 */
void ULuaTransitionRuntimeLibrary::ResetRuntimeCachesForPIESession()
{
    using namespace LuaTransitionRuntimePrivate;
    check(IsInGameThread());
    FailedBlueprintUpdateModules.Reset();
    GeneratedPropertyCache.Reset();
}
