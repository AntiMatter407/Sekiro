#include "SekiroLuaTransitionRuntimeLibrary.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "LuaEnv.h"
#include "Misc/AutomationTest.h"
#include "UnLuaModule.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SekiroLuaTransitionRuntimeTests
{
    /**
     * 激活 UnLua 并取得供内存模块测试使用的主环境。
     * 函数只能在游戏线程调用；它会保留模块激活状态，不负责在测试结束时关闭共享环境。
     *
     * @return 当前 UnLua 主环境；模块无法激活时返回 nullptr。
     */
    UnLua::FLuaEnv* GetOrActivateEnvironment()
    {
        IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
        if (!UnLuaModule.IsActive())
        {
            UnLuaModule.SetActive(true);
        }
        return UnLuaModule.GetEnv();
    }

    /**
     * 构造覆盖继承函数、false、非法返回和可变规则的 package.preload 内存模块。
     * 函数只生成 Lua 源码字符串，不访问 Lua VM 或文件系统，可在任意测试线程调用。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return 可交给 FLuaEnv::DoString 执行的完整 Lua chunk。
     */
    FString BuildRuntimeRuleModuleChunk(const FString& ModuleName)
    {
        FString Chunk = TEXT(R"LUA(
package.loaded["__MODULE__"] = nil
package.preload["__MODULE__"] = function()
    local Base = {}

    function Base.InheritedTrue(self)
        return self ~= nil
    end

    local Module = {
        FalseRule = function(self)
            return false
        end,
        InvalidRule = function(self)
            return 7
        end,
        MutableRule = function(self)
            return true
        end,
    }

    return setmetatable(Module, { __index = Base })
end
)LUA");
        Chunk.ReplaceInline(TEXT("__MODULE__"), *ModuleName, ESearchCase::CaseSensitive);
        return Chunk;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaTransitionRuntimeCacheTest,
    "Sekiro.AnimGraphIR.Runtime.TransitionRuleCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证运行时入口支持 metatable 继承、严格 boolean 降级，并按 AnimInstance 隔离发布缓存。
 * 测试只使用 package.preload 内存模块与 transient UAnimInstance；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroLuaTransitionRuntimeCacheTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.RuntimeTransitionCache"));
    UnLua::FLuaEnv* Environment = SekiroLuaTransitionRuntimeTests::GetOrActivateEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(TEXT("Runtime rule module is injected"), Environment->DoString(
        SekiroLuaTransitionRuntimeTests::BuildRuntimeRuleModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.RuntimeTransitionCache.Inject")));

    USkeletalMeshComponent* FirstComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage());
    USkeletalMeshComponent* SecondComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage());
    UAnimInstance* FirstInstance = FirstComponent != nullptr
        ? NewObject<UAnimInstance>(FirstComponent)
        : nullptr;
    UAnimInstance* SecondInstance = SecondComponent != nullptr
        ? NewObject<UAnimInstance>(SecondComponent)
        : nullptr;
    TestNotNull(TEXT("First transient AnimInstance is created"), FirstInstance);
    TestNotNull(TEXT("Second transient AnimInstance is created"), SecondInstance);
    if (FirstInstance == nullptr || SecondInstance == nullptr) return true;

    TestTrue(
        TEXT("Inherited true rule evaluates"),
        USekiroLuaTransitionRuntimeLibrary::EvaluateAndCacheTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("InheritedTrue")));
    TestTrue(
        TEXT("Inherited true rule is cached"),
        USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("InheritedTrue")));

    TestFalse(
        TEXT("False rule evaluates false"),
        USekiroLuaTransitionRuntimeLibrary::EvaluateAndCacheTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("FalseRule")));
    TestFalse(
        TEXT("False result is cached"),
        USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("FalseRule")));

    AddExpectedError(
        TEXT("must return boolean"),
        EAutomationExpectedErrorFlags::Contains,
        1);
    TestFalse(
        TEXT("Non-boolean rule degrades to false"),
        USekiroLuaTransitionRuntimeLibrary::EvaluateAndCacheTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("InvalidRule")));
    TestFalse(
        TEXT("Non-boolean failure publishes false"),
        USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("InvalidRule")));

    TestTrue(
        TEXT("First instance publishes mutable true"),
        USekiroLuaTransitionRuntimeLibrary::EvaluateAndCacheTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("MutableRule")));
    const FString MutationChunk = FString::Printf(
        TEXT("package.loaded[\"%s\"].MutableRule = function(self) return false end"),
        *ModuleName);
    TestTrue(
        TEXT("Loaded module rule is changed for the second instance"),
        Environment->DoString(MutationChunk, TEXT("SekiroAnimGraphIRTests.RuntimeTransitionCache.Mutate")));
    TestFalse(
        TEXT("Second instance publishes mutable false"),
        USekiroLuaTransitionRuntimeLibrary::EvaluateAndCacheTransitionRule(
            SecondInstance,
            ModuleName,
            TEXT("MutableRule")));
    TestTrue(
        TEXT("First instance cache remains isolated"),
        USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
            FirstInstance,
            ModuleName,
            TEXT("MutableRule")));
    TestFalse(
        TEXT("Second instance cache stores its own value"),
        USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
            SecondInstance,
            ModuleName,
            TEXT("MutableRule")));
    return true;
}

#endif
