#include "SekiroAnimGraphIRLibrary.h"

#include "LuaEnv.h"
#include "Misc/AutomationTest.h"
#include "UnLuaModule.h"
#include "lua.hpp"

#if WITH_DEV_AUTOMATION_TESTS

namespace SekiroAnimGraphIRLuaImporterTests
{
    /**
     * 为只测试 Lua table 注入准备一个活动 Env。
     * 非激活状态会在不存在任何 Lua wrapper 的测试入口安全启动，并保持激活到进程结束。
     *
     * @return 当前 UnLua 主环境；模块启动失败时返回 nullptr。
     */
    UnLua::FLuaEnv* GetOrActivateTestEnvironment()
    {
        IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
        if (!UnLuaModule.IsActive())
        {
            UnLuaModule.SetActive(true);
        }
        return UnLuaModule.GetEnv();
    }

    /**
     * 创建一个 package.preload 内存模块，其 CompileIR 返回覆盖完整第一阶段结构的合法 IR。
     * 函数只构造 Lua 源码字符串，不访问文件系统；模块名会同时写入 SourceModule 和所有 SourceLocation。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return 可交给 FLuaEnv::DoString 执行的 Lua chunk。
     */
    FString BuildValidModuleChunk(const FString& ModuleName)
    {
        FString Chunk = TEXT(R"LUA(
package.loaded["__MODULE__"] = nil
package.preload["__MODULE__"] = function()
    local Base = {}
    function Base.CanEnter_IdleSelf(self)
        return true
    end

    local Module = setmetatable({}, { __index = Base })

    function Module.CompileIR()
        local Location = {
            LuaModule = "__MODULE__",
            Line = 1,
            Column = 1,
        }

        return {
            SchemaVersion = 2,
            SourceModule = "__MODULE__",
            ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
            TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
            Layers = {
                {
                    Id = "Layer.Main",
                    Name = "Main",
                    RootGraphId = "Graph.Main",
                    Graphs = {
                        {
                            Id = "Graph.Main",
                            Name = "Main",
                            GraphType = "Pose",
                            RootNodeId = "Node.Main.Output",
                            Nodes = {
                                {
                                    Id = "Node.Main.Output",
                                    NodeType = "OutputPose",
                                    DisplayName = "Output Pose",
                                    OwnedGraphId = "",
                                    Pins = {
                                        {
                                            Name = "Result",
                                            Direction = "Input",
                                            DataType = "Pose",
                                            bAllowMultipleConnections = false,
                                            DeclarationOrder = 1,
                                        },
                                    },
                                    Properties = {},
                                    DeclarationOrder = 1,
                                    SourceLocation = Location,
                                },
                                {
                                    Id = "Node.Main.StateMachine",
                                    NodeType = "StateMachine",
                                    DisplayName = "Locomotion",
                                    OwnedGraphId = "Graph.Locomotion",
                                    Pins = {
                                        {
                                            Name = "Pose",
                                            Direction = "Output",
                                            DataType = "Pose",
                                            bAllowMultipleConnections = true,
                                            DeclarationOrder = 1,
                                        },
                                    },
                                    Properties = {},
                                    DeclarationOrder = 2,
                                    SourceLocation = Location,
                                },
                            },
                            Links = {
                                {
                                    Id = "Link.Main.StateMachineToOutput",
                                    Source = { NodeId = "Node.Main.StateMachine", PinName = "Pose" },
                                    Target = { NodeId = "Node.Main.Output", PinName = "Result" },
                                    DeclarationOrder = 1,
                                    SourceLocation = Location,
                                },
                            },
                            StateMachine = {
                                EntryStateId = "",
                                States = {},
                                Transitions = {},
                            },
                            DeclarationOrder = 1,
                            SourceLocation = Location,
                        },

)LUA")
            TEXT(R"LUA(
                        {
                            Id = "Graph.Idle",
                            Name = "Idle",
                            GraphType = "StatePose",
                            RootNodeId = "Node.Output",
                            Nodes = {
                                {
                                    Id = "Node.Output",
                                    NodeType = "StateResult",
                                    DisplayName = "State Result",
                                    OwnedGraphId = "",
                                    Pins = {
                                        {
                                            Name = "Result",
                                            Direction = "Input",
                                            DataType = "Pose",
                                            bAllowMultipleConnections = false,
                                            DeclarationOrder = 1,
                                        },
                                    },
                                    Properties = {},
                                    DeclarationOrder = 1,
                                    SourceLocation = Location,
                                },
                                {
                                    Id = "Node.Idle.Sequence",
                                    NodeType = "SequencePlayer",
                                    DisplayName = "Idle Sequence",
                                    OwnedGraphId = "",
                                    Pins = {
                                        {
                                            Name = "Pose",
                                            Direction = "Output",
                                            DataType = "Pose",
                                            bAllowMultipleConnections = true,
                                            DeclarationOrder = 1,
                                        },
                                    },
                                    Properties = {
                                        { Name = "Sequence", Value = { Type = "SoftObjectPath", SoftObjectPathValue = "/Game/Test/Fake.Fake" }, DeclarationOrder = 1 },
                                        { Name = "bLoopAnimation", Value = { Type = "Bool", BoolValue = true }, DeclarationOrder = 2 },
                                        { Name = "PlayRate", Value = { Type = "Float", FloatValue = 1.25 }, DeclarationOrder = 3 },
                                        { Name = "StartPosition", Value = { Type = "Float", FloatValue = 0.1 }, DeclarationOrder = 4 },
                                    },
                                    DeclarationOrder = 2,
                                    SourceLocation = Location,
                                },
                            },
                            Links = {
                                {
                                    Id = "Link.Idle.SequenceToOutput",
                                    Source = { NodeId = "Node.Idle.Sequence", PinName = "Pose" },
                                    Target = { NodeId = "Node.Output", PinName = "Result" },
                                    DeclarationOrder = 1,
                                    SourceLocation = Location,
                                },
                            },
                            StateMachine = {
                                EntryStateId = "",
                                States = {},
                                Transitions = {},
                            },
                            DeclarationOrder = 3,
                            SourceLocation = Location,
                        },
                        {
                            Id = "Graph.Locomotion",
                            Name = "Locomotion",
                            GraphType = "StateMachine",
                            RootNodeId = "",
                            Nodes = {},
                            Links = {},
                            StateMachine = {
                                EntryStateId = "State.Idle",
                                States = {
                                    {
                                        Id = "State.Idle",
                                        Name = "Idle",
                                        GraphId = "Graph.Idle",
                                        bAlwaysResetOnEntry = false,
                                        DeclarationOrder = 1,
                                        SourceLocation = Location,
                                    },
                                },
                                Transitions = {
                                    {
                                        Id = "Transition.IdleSelf",
                                        Key = "IdleSelf",
                                        SourceStateId = "State.Idle",
                                        TargetStateId = "State.Idle",
                                        RuleFunctionName = "CanEnter_IdleSelf",
                                        Settings = {
                                            BlendDuration = 0.2,
                                            PriorityOrder = 0,
                                            BlendMode = "Linear",
                                        },
                                        DeclarationOrder = 1,
                                        SourceLocation = Location,
                                    },
                                },
                            },
                            DeclarationOrder = 2,
                            SourceLocation = Location,
                        },
                    },
                    DeclarationOrder = 1,
                    SourceLocation = Location,
                },
            },
            SourceLocation = Location,
        }
    end

    return Module
end
)LUA");
        Chunk.ReplaceInline(TEXT("__MODULE__"), *ModuleName, ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个缺少 Transition.Key 的内存模块，用于验证 Key 是 Lua 导入契约的必填字段。
     * 函数基于合法模块替换字段名，不访问文件系统或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return Transition 使用未知 MissingKey 字段、但缺少必填 Key 字段的 Lua chunk。
     */
    FString BuildMissingTransitionKeyModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("Key = \"IdleSelf\","),
            TEXT("MissingKey = \"IdleSelf\","),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个缺少顶层 TargetSkeleton 的内存模块，用于验证具体 AnimBlueprint 的必填资产契约。
     * 函数基于合法模块替换字段名，不访问文件系统、Asset Registry 或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return 使用未知 MissingTargetSkeleton 字段、但缺少必填 TargetSkeleton 字段的 Lua chunk。
     */
    FString BuildMissingTargetSkeletonModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("TargetSkeleton = \"/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton\","),
            TEXT("MissingTargetSkeleton = \"/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton\","),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建 IR 引用了未导出 Transition Rule 的内存模块。
     * 函数仅替换基类函数名，不访问文件系统或 Lua VM；CompileIR 仍声明原始 RuleFunctionName。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return RuleFunctionName 无法从模块及 metatable 继承链解析的 Lua chunk。
     */
    FString BuildMissingRuleFunctionModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("function Base.CanEnter_IdleSelf(self)"),
            TEXT("function Base.UnrelatedRule(self)"),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个字段类型错误的内存模块，用于验证解析错误早于 Validator 且代码稳定。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return SchemaVersion 错写为 string 的 Lua chunk。
     */
    FString BuildInvalidTypeModuleChunk(const FString& ModuleName)
    {
        FString Chunk = TEXT(R"LUA(
package.loaded["__MODULE__"] = nil
package.preload["__MODULE__"] = function()
    return {
        CompileIR = function()
            return {
                SchemaVersion = "2",
                SourceModule = "__MODULE__",
                ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
                Layers = {},
                SourceLocation = {
                    LuaModule = "__MODULE__",
                    Line = 7,
                    Column = 9,
                },
            }
        end,
    }
end
)LUA");
        Chunk.ReplaceInline(TEXT("__MODULE__"), *ModuleName, ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 查询诊断数组是否包含指定稳定代码。
     *
     * @param Diagnostics CompileLuaModule 返回的诊断数组。
     * @param Code 目标 IR.Lua* 代码。
     * @return 至少存在一条匹配诊断时返回 true，否则返回 false。
     */
    bool HasLuaDiagnosticCode(const TArray<FSekiroAnimIRDiagnostic>& Diagnostics, const FName Code)
    {
        for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Code == Code) return true;
        }

        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaValidImportTest,
    "Sekiro.AnimGraphIR.Lua.ValidImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 package.preload 模块可经 UnLua 导入完整状态机结构及七种属性值。
 * 测试只修改当前 Lua Env 的内存 module cache，不依赖 Content 或项目资源。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaValidImportTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.ValidImport"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Memory Lua module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildValidModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.ValidImport.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(TEXT("Valid Lua IR imports"), USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestEqual(TEXT("Valid import has no diagnostics"), Diagnostics.Num(), 0);
    TestEqual(TEXT("Layer count is imported"), Blueprint.Layers.Num(), 1);
    TestEqual(TEXT("Schema version 2 is imported"), Blueprint.SchemaVersion, 2);
    TestEqual(
        TEXT("TargetSkeleton path is preserved exactly"),
        Blueprint.TargetSkeleton.ToString(),
        FString(TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton")));
    TestEqual(TEXT("Native topology imports three Graphs"), Blueprint.Layers[0].Graphs.Num(), 3);
    TestEqual(TEXT("Layer root remains the Main Pose Graph"), Blueprint.Layers[0].RootGraphId, FString(TEXT("Graph.Main")));
    TestEqual(TEXT("Graphs are canonicalized"), Blueprint.Layers[0].Graphs[0].Id, FString(TEXT("Graph.Idle")));
    TestEqual(TEXT("State machine is imported"), Blueprint.Layers[0].Graphs[1].StateMachine.States.Num(), 1);
    TestEqual(
        TEXT("Transition semantic Key is imported"),
        Blueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Key,
        FString(TEXT("IdleSelf")));
    TestEqual(
        TEXT("StateMachine Node imports its owned Graph"),
        Blueprint.Layers[0].Graphs[2].Nodes[1].OwnedGraphId,
        FString(TEXT("Graph.Locomotion")));
    TestEqual(TEXT("Registered Sequence properties are imported"), Blueprint.Layers[0].Graphs[0].Nodes[0].Properties.Num(), 4);
    TestEqual(
        TEXT("Soft object path remains unresolved value data"),
        Blueprint.Layers[0].Graphs[0].Nodes[0].Properties[0].Value.SoftObjectPathValue.ToString(),
        FString(TEXT("/Game/Test/Fake.Fake")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaMissingTransitionKeyTest,
    "Sekiro.AnimGraphIR.Lua.MissingTransitionKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua Transition 缺少 Key 时在导入阶段返回稳定字段诊断。
 * 测试只修改当前 Lua Env 的内存 module cache，不依赖 Content 或项目资源。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaMissingTransitionKeyTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.MissingTransitionKey"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Missing-Key memory module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildMissingTransitionKeyModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.MissingTransitionKey.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing Transition Key is rejected"), USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing Transition Key emits importer code"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(Diagnostics, TEXT("IR.LuaMissingField")));
    if (!Diagnostics.IsEmpty())
    {
        TestTrue(TEXT("Diagnostic identifies Transition.Key"), Diagnostics[0].SubjectId.EndsWith(TEXT(".Key")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaMissingTargetSkeletonTest,
    "Sekiro.AnimGraphIR.Lua.MissingTargetSkeleton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua Blueprint 根 table 缺少 TargetSkeleton 时在导入阶段返回精确字段诊断。
 * 测试只修改当前 Lua Env 的内存 module cache，不依赖 Content 或项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaMissingTargetSkeletonTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.MissingTargetSkeleton"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Missing-TargetSkeleton memory module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildMissingTargetSkeletonModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.MissingTargetSkeleton.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("Missing Blueprint.TargetSkeleton is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing Blueprint.TargetSkeleton emits importer code"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(Diagnostics, TEXT("IR.LuaMissingField")));
    if (!Diagnostics.IsEmpty())
    {
        TestEqual(
            TEXT("Diagnostic identifies Blueprint.TargetSkeleton"),
            Diagnostics[0].SubjectId,
            FString(TEXT("Blueprint.TargetSkeleton")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaMissingRuleFunctionTest,
    "Sekiro.AnimGraphIR.Lua.MissingRuleFunction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 IR 声明的 Transition Rule 必须能从模块 table 或其 metatable 继承链解析为函数。
 * 测试只修改当前 Lua Env 的内存 module cache，不读取 Content，也不加载项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaMissingRuleFunctionTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.MissingRuleFunction"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(TEXT("Missing-rule memory module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildMissingRuleFunctionModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.MissingRuleFunction.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("Missing Transition Rule function is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing Transition Rule emits stable importer code"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(
            Diagnostics,
            TEXT("IR.LuaMissingRuleFunction")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaAnimGraphFunctionModuleTest,
    "Sekiro.AnimGraphIR.Lua.AnimGraphFunctionModule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证项目真实 Lua 模块可通过 AnimGraph Function、独立状态机文件和直接 Pin API 导出完整 IR。
 * 测试只读取 Content/Script 中的示例模块，不创建或保存 UObject 资产；CompileLuaModule 负责按需激活 UnLua。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaAnimGraphFunctionModuleTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("Animation.Examples.ABP_Minimal"));
    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bCompiled = USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics);

    TestTrue(TEXT("AnimGraph Function example imports through UnLua"), bCompiled);
    TestEqual(TEXT("AnimGraph Function example has no diagnostics"), Diagnostics.Num(), 0);
    if (!bCompiled || Blueprint.Layers.IsEmpty()) return true;

    const FSekiroAnimIRLayer& MainLayer = Blueprint.Layers[0];
    TestEqual(TEXT("Example emits four owned Graphs"), MainLayer.Graphs.Num(), 4);

    const FSekiroAnimIRGraph* MainGraph = nullptr;
    for (const FSekiroAnimIRGraph& Graph : MainLayer.Graphs)
    {
        if (Graph.Name == TEXT("AnimGraph"))
        {
            MainGraph = &Graph;
            break;
        }
    }
    TestNotNull(TEXT("Example emits the base-owned AnimGraph"), MainGraph);
    if (MainGraph == nullptr) return true;

    bool bFoundStateMachine = false;
    bool bFoundSaveCachedPose = false;
    bool bFoundUseCachedPose = false;
    for (const FSekiroAnimIRNode& Node : MainGraph->Nodes)
    {
        bFoundStateMachine |= Node.NodeType == SekiroAnimGraphIRNames::StateMachineNode;
        bFoundSaveCachedPose |= Node.NodeType == SekiroAnimGraphIRNames::SaveCachedPoseNode;
        bFoundUseCachedPose |= Node.NodeType == SekiroAnimGraphIRNames::UseCachedPoseNode;
    }

    TestTrue(TEXT("External state machine expands into the main Graph"), bFoundStateMachine);
    TestTrue(TEXT("Direct Graph API emits Save Cached Pose"), bFoundSaveCachedPose);
    TestTrue(TEXT("Direct Graph API emits Use Cached Pose"), bFoundUseCachedPose);
    TestEqual(TEXT("Typed Pin API emits three Pose Links"), MainGraph->Links.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaInvalidTypeTest,
    "Sekiro.AnimGraphIR.Lua.InvalidType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua Schema 字段不允许隐式类型转换，并返回稳定字段路径与源码位置。
 * 测试只使用 package.preload 内存模块。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaInvalidTypeTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.InvalidType"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Invalid memory module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildInvalidTypeModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.InvalidType.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid field type is rejected"), USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestTrue(
        TEXT("Stable invalid type code is returned"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(Diagnostics, TEXT("IR.LuaInvalidFieldType")));
    if (!Diagnostics.IsEmpty())
    {
        TestEqual(TEXT("Diagnostic identifies exact field"), Diagnostics[0].SubjectId, FString(TEXT("Blueprint.SchemaVersion")));
        TestEqual(TEXT("Diagnostic preserves Lua line"), Diagnostics[0].SourceLocation.Line, 7);
        TestEqual(TEXT("Diagnostic preserves Lua column"), Diagnostics[0].SourceLocation.Column, 9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaDeterminismTest,
    "Sekiro.AnimGraphIR.Lua.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同一 Lua 模块重复 CompileIR 得到相同规范 IR，且 FLuaRetValues 生命周期保持主 Lua 栈平衡。
 * 测试不热重载模块、不读取文件系统。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaDeterminismTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.Determinism"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Determinism memory module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildValidModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.Determinism.Inject")));

    lua_State* State = Environment->GetMainState();
    const int32 InitialStackTop = lua_gettop(State);
    FSekiroAnimBlueprintIR FirstBlueprint;
    FSekiroAnimBlueprintIR SecondBlueprint;
    TArray<FSekiroAnimIRDiagnostic> FirstDiagnostics;
    TArray<FSekiroAnimIRDiagnostic> SecondDiagnostics;

    TestTrue(TEXT("First compile succeeds"), USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, FirstBlueprint, FirstDiagnostics));
    TestEqual(TEXT("First compile restores Lua stack"), lua_gettop(State), InitialStackTop);
    TestTrue(TEXT("Second compile succeeds"), USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, SecondBlueprint, SecondDiagnostics));
    TestEqual(TEXT("Second compile restores Lua stack"), lua_gettop(State), InitialStackTop);
    TestTrue(
        TEXT("Repeated compile produces identical reflected IR"),
        FSekiroAnimBlueprintIR::StaticStruct()->CompareScriptStruct(&FirstBlueprint, &SecondBlueprint, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaCommandletActivationTest,
    "Sekiro.AnimGraphIR.Lua.CommandletActivation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证非 PIE 命令行环境中 CompileLuaModule 会从 inactive UnLua 状态自行启动 Env。
 * 停用发生在任何 Lua wrapper 创建之前；导入器保持重启后的模块激活，再注入并编译合法内存模块。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaCommandletActivationTest::RunTest(const FString& Parameters)
{
    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLuaModule.SetActive(false);
    TestFalse(TEXT("Test begins with inactive UnLua"), UnLuaModule.IsActive());

    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.CommandletActivation"));
    const FString ModuleChunk = SekiroAnimGraphIRLuaImporterTests::BuildValidModuleChunk(ModuleName);
    bool bModuleInjected = false;
    const FDelegateHandle OnCreatedHandle = UnLua::FLuaEnv::OnCreated.AddLambda(
        [&ModuleChunk, &bModuleInjected](UnLua::FLuaEnv& CreatedEnvironment)
        {
            bModuleInjected = CreatedEnvironment.DoString(
                ModuleChunk,
                TEXT("SekiroAnimGraphIRTests.CommandletActivation.Inject"));
        });

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bCompiled = USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics);
    UnLua::FLuaEnv::OnCreated.Remove(OnCreatedHandle);

    TestTrue(TEXT("CompileLuaModule activates UnLua"), UnLuaModule.IsActive());
    TestTrue(TEXT("New commandlet environment receives memory module"), bModuleInjected);
    TestTrue(TEXT("Inactive commandlet environment compiles valid IR"), bCompiled);
    TestEqual(TEXT("Commandlet activation import has no diagnostics"), Diagnostics.Num(), 0);
    return true;
}

#endif
