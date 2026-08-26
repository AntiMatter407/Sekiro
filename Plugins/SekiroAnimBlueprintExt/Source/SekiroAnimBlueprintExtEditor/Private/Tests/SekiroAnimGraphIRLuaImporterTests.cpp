#include "SekiroAnimGraphIRLibrary.h"

#include "SekiroAnimGraphIRLuaWriter.h"

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
            BlueprintKind = "AnimBlueprint",
            SourceModule = "__MODULE__",
            ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
            TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
            ImplementedInterfaces = {
                "/Script/Engine.AnimInstance",
                "/Script/Engine.AnimSingleNodeInstance",
            },
            Layers = {
                {
                    Id = "Layer.Main",
                    Name = "Main",
                    FunctionName = "AnimGraph",
                    InterfaceClass = "",
                    bOverride = false,
                    Parameters = {},
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
                            Layout = {
                                Style = "LeftToRight",
                                Grids = {},
                                Positions = {
                                    { ElementId = "Node.Main.Output", X = 720, Y = -80 },
                                    { ElementId = "Node.Main.StateMachine", X = -320, Y = 140 },
                                },
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
     * 创建一个精确布局 X 坐标为无穷大的内存模块，验证 Importer 在写入 int32 IR 前拒绝非有限数值。
     * 函数只替换合法模块中唯一的测试坐标，不访问 Lua VM 或文件系统。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return Positions[2].X 为正无穷大的 Lua chunk。
     */
    FString BuildNonFiniteLayoutPositionModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("X = -320"),
            TEXT("X = 1 / 0"),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个通过真实 LuaAnimGraph/LuaAnimStateMachineGraph SetPosition API 声明精确布局的内存模块。
     * 模块同时覆盖 Pose Graph、StateMachine Graph 和 StatePose Graph，不引用项目业务 ABP 或动画资产。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return 可注入 package.preload 并由 CompileLuaModule 调用的 Lua chunk。
     */
    FString BuildCompilerSetPositionModuleChunk(const FString& ModuleName)
    {
        FString Chunk = TEXT(R"LUA(
package.loaded["__MODULE__"] = nil
package.preload["__MODULE__"] = function()
    local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")

    local PositionBlueprint = LuaAnimBlueprint:Extend("PositionBlueprint", {
        SourceModule = "__MODULE__",
        ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
        TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
    })

    function PositionBlueprint:AnimGraph(graph)
        local machine = graph:StateMachine("Machine")
        graph.Result:Connect(machine.Pose)
        graph:SetPosition(machine, -320.0, 140)
        graph:SetPosition(graph.OutputNode, 720, -80)
    end

    function PositionBlueprint.StateMachine_Machine(machine)
        local idle = machine:State("Idle")
        machine:Entry("Idle")
        machine:SetPosition(idle, 111, 222)
    end

    function PositionBlueprint.StateGraph_Machine_Idle(graph)
        graph:SetPosition(graph.OutputNode, 333, 444)
    end

    return PositionBlueprint:Export()
end
)LUA");
        Chunk.ReplaceInline(TEXT("__MODULE__"), *ModuleName, ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个合法 Animation Layer Interface 内存模块，用于验证资产种类分支及空父类、空骨架契约。
     * 函数基于完整合法模块替换 Blueprint 根字段，不访问文件系统、Asset Registry 或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return BlueprintKind 为 AnimationLayerInterface 且不声明具体父类和骨架的 Lua chunk。
     */
    FString BuildValidAnimationLayerInterfaceModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("BlueprintKind = \"AnimBlueprint\","),
            TEXT("BlueprintKind = \"AnimationLayerInterface\","),
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT("ParentAnimInstanceClass = \"/Script/Engine.AnimInstance\","),
            TEXT("ParentAnimInstanceClass = \"\","),
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT("TargetSkeleton = \"/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton\","),
            TEXT("TargetSkeleton = \"\","),
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT(R"LUA(ImplementedInterfaces = {
                "/Script/Engine.AnimInstance",
                "/Script/Engine.AnimSingleNodeInstance",
            },)LUA"),
            TEXT("ImplementedInterfaces = {},"),
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT(R"LUA(                    FunctionName = "AnimGraph",
                    InterfaceClass = "",
                    bOverride = false,
                    Parameters = {},)LUA"),
            TEXT(R"LUA(                    FunctionName = "Main",
                    InterfaceClass = "",
                    bOverride = false,
                    Parameters = {
                        {
                            Name = "SourcePose",
                            DataType = "Pose",
                            TypeObjectPath = "",
                            bIsPose = true,
                            DeclarationOrder = 1,
                            SourceLocation = Location,
                        },
                    },)LUA"),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建一个动画层 Pose 参数标记类型非法的内存模块，用于验证函数签名字段的严格导入。
     * 函数基于合法 Animation Layer Interface 模块替换字段值，不访问文件系统、类加载器或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return SourcePose.bIsPose 为字符串而非 boolean 的 Lua chunk。
     */
    FString BuildInvalidLayerParameterTypeModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidAnimationLayerInterfaceModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("bIsPose = true,"),
            TEXT("bIsPose = \"true\","),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建 BlueprintKind 含未知字面量的内存模块，用于验证枚举值诊断。
     * 函数只替换合法模块的根字段，不访问文件系统或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return BlueprintKind 为不受支持值的 Lua chunk。
     */
    FString BuildInvalidBlueprintKindModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("BlueprintKind = \"AnimBlueprint\","),
            TEXT("BlueprintKind = \"LinkedAnimGraph\","),
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 创建 ImplementedInterfaces 含非字符串元素的内存模块，用于验证数组元素类型诊断。
     * 函数只替换合法模块的接口数组，不访问文件系统、类加载器或 Lua VM。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return 第二个接口元素为 number 的 Lua chunk。
     */
    FString BuildInvalidImplementedInterfaceElementModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("\"/Script/Engine.AnimSingleNodeInstance\","),
            TEXT("17,"),
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
     * 创建包含 ExpectedBool 正反值的纯原生 BoolProperty Rule 内存模块。
     * 函数基于合法模块替换单条 Transition，不访问文件系统、Lua VM 或 UObject。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @return RuleFunctionName 为空且 Gate 根为 All(true 属性, false 属性) 的 Lua chunk。
     */
    FString BuildNativeBoolRuleModuleChunk(const FString& ModuleName)
    {
        FString Chunk = BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("RuleFunctionName = \"CanEnter_IdleSelf\","),
            TEXT(R"LUA(RuleFunctionName = "",
                                        Gate = {
                                            RootIndex = 2,
                                            Nodes = {
                                                {
                                                    Type = "BoolProperty",
                                                    Name = "bExpectedTrue",
                                                    Threshold = 0.0,
                                                    ExpectedBool = true,
                                                    Children = {},
                                                },
                                                {
                                                    Type = "BoolProperty",
                                                    Name = "bExpectedFalse",
                                                    Threshold = 0.0,
                                                    ExpectedBool = false,
                                                    Children = {},
                                                },
                                                {
                                                    Type = "All",
                                                    Name = "",
                                                    Threshold = 0.0,
                                                    ExpectedBool = false,
                                                    Children = { 0, 1 },
                                                },
                                            },
                                        },)LUA"),
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
                BlueprintKind = "AnimBlueprint",
                SourceModule = "__MODULE__",
                ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
                TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
                ImplementedInterfaces = {},
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
    TestTrue(
        TEXT("AnimBlueprint kind is imported"),
        Blueprint.BlueprintKind == ESekiroAnimIRBlueprintKind::AnimBlueprint);
    TestEqual(
        TEXT("Implemented interface count is imported"),
        Blueprint.ImplementedInterfaces.Num(),
        2);
    TestTrue(
        TEXT("Implemented interface class paths are preserved"),
        Blueprint.ImplementedInterfaces.Contains(
            FSoftClassPath(TEXT("/Script/Engine.AnimSingleNodeInstance"))));
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
    const FSekiroAnimIRGraph* ImportedMainGraph = nullptr;
    for (const FSekiroAnimIRGraph& Graph : Blueprint.Layers[0].Graphs)
    {
        if (Graph.Id == TEXT("Graph.Main"))
        {
            ImportedMainGraph = &Graph;
            break;
        }
    }
    TestNotNull(TEXT("Main Graph with exact positions is imported"), ImportedMainGraph);
    if (ImportedMainGraph != nullptr)
    {
        TestEqual(
            TEXT("Exact positions are imported and canonicalized"),
            ImportedMainGraph->Layout.Positions.Num(),
            2);
        TestEqual(
            TEXT("Canonical first exact position uses stable ElementId order"),
            ImportedMainGraph->Layout.Positions[0].ElementId,
            FString(TEXT("Node.Main.Output")));
        TestEqual(
            TEXT("Imported exact X coordinate is preserved"),
            ImportedMainGraph->Layout.Positions[0].X,
            720);
        TestEqual(
            TEXT("Imported exact Y coordinate is preserved"),
            ImportedMainGraph->Layout.Positions[0].Y,
            -80);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaNonFiniteLayoutPositionTest,
    "Sekiro.AnimGraphIR.Lua.NonFiniteLayoutPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Importer 在精确布局坐标写入 int32 IR 前拒绝 Lua 无穷大。
 * 测试只修改 package.preload 内存模块，不读取 Content 资产或创建 UObject。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaNonFiniteLayoutPositionTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.NonFiniteLayoutPosition"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Non-finite layout module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildNonFiniteLayoutPositionModuleChunk(ModuleName),
            TEXT("SekiroAnimGraphIRTests.NonFiniteLayoutPosition.Inject")));
    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("Non-finite exact layout coordinate is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestTrue(
        TEXT("Non-finite exact layout coordinate emits numeric diagnostic"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(
            Diagnostics,
            TEXT("IR.LuaInvalidNumber")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaCompilerSetPositionTest,
    "Sekiro.AnimGraphIR.Lua.CompilerSetPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证编译器的统一 SetPosition API 会从 Pose、StateMachine 和 StatePose Graph 完整导出精确坐标。
 * 测试只使用 package.preload 内存模块和编译器 Lua，不读取或生成业务 ABP 资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaCompilerSetPositionTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.CompilerSetPosition"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Compiler SetPosition module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildCompilerSetPositionModuleChunk(ModuleName),
            TEXT("SekiroAnimGraphIRTests.CompilerSetPosition.Inject")));
    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Compiler SetPosition module imports"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestEqual(TEXT("Compiler SetPosition import has no diagnostics"), Diagnostics.Num(), 0);

    const FSekiroAnimIRGraph* MainGraph = nullptr;
    const FSekiroAnimIRGraph* StateMachineGraph = nullptr;
    const FSekiroAnimIRGraph* IdleGraph = nullptr;
    if (!Blueprint.Layers.IsEmpty())
    {
        for (const FSekiroAnimIRGraph& Graph : Blueprint.Layers[0].Graphs)
        {
            if (Graph.GraphType == SekiroAnimGraphIRNames::PoseGraph) MainGraph = &Graph;
            else if (Graph.GraphType == SekiroAnimGraphIRNames::StateMachineGraph)
                StateMachineGraph = &Graph;
            else if (Graph.GraphType == SekiroAnimGraphIRNames::StatePoseGraph)
                IdleGraph = &Graph;
        }
    }
    TestNotNull(TEXT("SetPosition test exports Pose Graph"), MainGraph);
    TestNotNull(TEXT("SetPosition test exports StateMachine Graph"), StateMachineGraph);
    TestNotNull(TEXT("SetPosition test exports StatePose Graph"), IdleGraph);
    if (MainGraph != nullptr)
    {
        TestEqual(TEXT("Pose Graph exports two exact positions"), MainGraph->Layout.Positions.Num(), 2);
    }
    if (StateMachineGraph != nullptr)
    {
        TestEqual(
            TEXT("StateMachine defaults to CompactGrid"),
            StateMachineGraph->Layout.Style,
            ESekiroAnimIRLayoutStyle::CompactGrid);
        TestEqual(
            TEXT("StateMachine exports one exact State position"),
            StateMachineGraph->Layout.Positions.Num(),
            1);
        if (!StateMachineGraph->Layout.Positions.IsEmpty())
        {
            TestEqual(
                TEXT("State exact X survives compiler and importer"),
                StateMachineGraph->Layout.Positions[0].X,
                111);
            TestEqual(
                TEXT("State exact Y survives compiler and importer"),
                StateMachineGraph->Layout.Positions[0].Y,
                222);
        }
    }
    if (IdleGraph != nullptr)
    {
        TestEqual(TEXT("StatePose exports one exact position"), IdleGraph->Layout.Positions.Num(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaAnimationLayerInterfaceImportTest,
    "Sekiro.AnimGraphIR.Lua.AnimationLayerInterfaceImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua 根字段可选择 Animation Layer Interface，且空父类和空骨架不会误走普通 AnimBlueprint 校验。
 * 测试只修改当前 Lua Env 的内存 module cache，不读取或生成项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaAnimationLayerInterfaceImportTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(
        TEXT("SekiroAnimGraphIRTests.AnimationLayerInterfaceImport"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Animation Layer Interface memory module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildValidAnimationLayerInterfaceModuleChunk(ModuleName),
            TEXT("SekiroAnimGraphIRTests.AnimationLayerInterfaceImport.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Animation Layer Interface Lua IR imports"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestEqual(
        TEXT("Animation Layer Interface import has no diagnostics"),
        Diagnostics.Num(),
        0);
    TestTrue(
        TEXT("Animation Layer Interface kind is preserved"),
        Blueprint.BlueprintKind
            == ESekiroAnimIRBlueprintKind::AnimationLayerInterface);
    TestTrue(
        TEXT("Animation Layer Interface parent class remains empty"),
        Blueprint.ParentAnimInstanceClass.IsNull());
    TestTrue(
        TEXT("Animation Layer Interface skeleton remains empty"),
        Blueprint.TargetSkeleton.IsNull());
    TestEqual(
        TEXT("Animation Layer Interface implements no interfaces"),
        Blueprint.ImplementedInterfaces.Num(),
        0);
    TestEqual(
        TEXT("Animation Layer Interface function name is imported"),
        Blueprint.Layers[0].FunctionName,
        FName(TEXT("Main")));
    TestTrue(
        TEXT("Animation Layer Interface class remains empty"),
        Blueprint.Layers[0].InterfaceClass.IsNull());
    TestFalse(
        TEXT("Animation Layer Interface declaration is not an override"),
        Blueprint.Layers[0].bOverride);
    TestEqual(
        TEXT("Animation Layer Interface imports one function parameter"),
        Blueprint.Layers[0].Parameters.Num(),
        1);
    if (Blueprint.Layers[0].Parameters.Num() == 1)
    {
        const FSekiroAnimIRFunctionParameter& Parameter =
            Blueprint.Layers[0].Parameters[0];
        TestEqual(
            TEXT("Animation Layer Interface parameter name is imported"),
            Parameter.Name,
            FName(TEXT("SourcePose")));
        TestEqual(
            TEXT("Animation Layer Interface parameter data type is imported"),
            Parameter.DataType,
            FName(TEXT("Pose")));
        TestTrue(
            TEXT("Animation Layer Interface parameter pose flag is imported"),
            Parameter.bIsPose);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaInvalidLayerParameterTypeTest,
    "Sekiro.AnimGraphIR.Lua.InvalidLayerParameterType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证动画层函数参数只接受严格字段类型，并为非法 bIsPose 返回精确路径诊断。
 * 测试只使用 package.preload 内存模块，不读取或生成项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaInvalidLayerParameterTypeTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(
        TEXT("SekiroAnimGraphIRTests.InvalidLayerParameterType"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Invalid Layer parameter module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildInvalidLayerParameterTypeModuleChunk(ModuleName),
            TEXT("SekiroAnimGraphIRTests.InvalidLayerParameterType.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("String Layer parameter bIsPose is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestTrue(
        TEXT("Invalid Layer parameter emits stable type diagnostic"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(
            Diagnostics,
            TEXT("IR.LuaInvalidFieldType")));
    if (!Diagnostics.IsEmpty())
    {
        TestEqual(
            TEXT("Layer parameter diagnostic identifies exact field"),
            Diagnostics[0].SubjectId,
            FString(TEXT("Blueprint.Layers[1].Parameters[1].bIsPose")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaInvalidBlueprintKindTest,
    "Sekiro.AnimGraphIR.Lua.InvalidBlueprintKind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证未知 BlueprintKind 在导入阶段被拒绝，并返回精确字段路径和稳定枚举诊断。
 * 测试只使用 package.preload 内存模块，不加载任何类或项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaInvalidBlueprintKindTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.InvalidBlueprintKind"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Invalid BlueprintKind memory module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildInvalidBlueprintKindModuleChunk(ModuleName),
            TEXT("SekiroAnimGraphIRTests.InvalidBlueprintKind.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("Unknown BlueprintKind is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestTrue(
        TEXT("Unknown BlueprintKind emits stable enum diagnostic"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(
            Diagnostics,
            TEXT("IR.LuaInvalidEnumValue")));
    if (!Diagnostics.IsEmpty())
    {
        TestEqual(
            TEXT("BlueprintKind diagnostic identifies exact field"),
            Diagnostics[0].SubjectId,
            FString(TEXT("Blueprint.BlueprintKind")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaInvalidImplementedInterfaceElementTest,
    "Sekiro.AnimGraphIR.Lua.InvalidImplementedInterfaceElement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 ImplementedInterfaces 只接受连续字符串数组，非字符串元素返回精确元素路径诊断。
 * 测试只使用 package.preload 内存模块，不解析或加载接口类。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaInvalidImplementedInterfaceElementTest::RunTest(
    const FString& Parameters)
{
    const FString ModuleName(
        TEXT("SekiroAnimGraphIRTests.InvalidImplementedInterfaceElement"));
    UnLua::FLuaEnv* Environment =
        SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Invalid interface element memory module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::
                BuildInvalidImplementedInterfaceElementModuleChunk(ModuleName),
            TEXT(
                "SekiroAnimGraphIRTests.InvalidImplementedInterfaceElement.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(
        TEXT("Non-string implemented interface is rejected"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            ModuleName,
            Blueprint,
            Diagnostics));
    TestTrue(
        TEXT("Non-string implemented interface emits stable type diagnostic"),
        SekiroAnimGraphIRLuaImporterTests::HasLuaDiagnosticCode(
            Diagnostics,
            TEXT("IR.LuaInvalidFieldType")));
    if (!Diagnostics.IsEmpty())
    {
        TestEqual(
            TEXT("Implemented interface diagnostic identifies exact element"),
            Diagnostics[0].SubjectId,
            FString(TEXT("Blueprint.ImplementedInterfaces[2]")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLuaNativeBoolRuleImportTest,
    "Sekiro.AnimGraphIR.Lua.NativeBoolRuleImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Importer 读取纯原生 BoolProperty Rule，并保持 ExpectedBool 的 true/false 值且不查找空 Lua 函数。
 * 测试只修改当前 Lua Env 的内存 module cache，不依赖 Content 文件或项目资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaNativeBoolRuleImportTest::RunTest(const FString& Parameters)
{
    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.NativeBoolRuleImport"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("UnLua environment is available"), Environment);
    if (!Environment) return true;

    TestTrue(TEXT("Native Bool Rule module is injected"), Environment->DoString(
        SekiroAnimGraphIRLuaImporterTests::BuildNativeBoolRuleModuleChunk(ModuleName),
        TEXT("SekiroAnimGraphIRTests.NativeBoolRuleImport.Inject")));

    FSekiroAnimBlueprintIR Blueprint;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Pure native Bool Rule imports without Lua function lookup"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(ModuleName, Blueprint, Diagnostics));
    TestEqual(TEXT("Pure native Bool Rule import has no diagnostics"), Diagnostics.Num(), 0);

    const FSekiroAnimIRTransition* ImportedTransition = nullptr;
    for (const FSekiroAnimIRLayer& Layer : Blueprint.Layers)
    {
        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            if (Graph.StateMachine.Transitions.IsEmpty()) continue;
            ImportedTransition = &Graph.StateMachine.Transitions[0];
            break;
        }
        if (ImportedTransition != nullptr) break;
    }
    TestNotNull(TEXT("Imported native Transition exists"), ImportedTransition);
    if (ImportedTransition != nullptr)
    {
        TestTrue(TEXT("Pure native Transition has no Lua RuleFunctionName"),
            ImportedTransition->RuleFunctionName.IsNone());
        TestEqual(TEXT("Native Rule imports three AST nodes"), ImportedTransition->Gate.Nodes.Num(), 3);
        if (ImportedTransition->Gate.Nodes.Num() == 3)
        {
            TestTrue(TEXT("ExpectedBool true is preserved"), ImportedTransition->Gate.Nodes[0].bExpectedBool);
            TestFalse(TEXT("ExpectedBool false is preserved"), ImportedTransition->Gate.Nodes[1].bExpectedBool);
            TestEqual(TEXT("All remains the imported Gate root"), ImportedTransition->Gate.RootIndex, 2);
        }
    }
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
    FSekiroAnimGraphIRLuaWriterRoundTripTest,
    "Sekiro.AnimGraphIR.Lua.WriterRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Writer 对同一 Canonical IR 产生字节稳定文本，正确转义字符串并保留精确布局坐标。
 * 生成模块通过 package.preload 注入当前 UnLua Env，再由现有 Importer 回读；测试不写文件、
 * 不创建 UObject，并确认 RuleFunctionName 仍从 IR.SourceModule 的运行时模块解析。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLuaWriterRoundTripTest::RunTest(const FString& Parameters)
{
    const FString SourceModule(TEXT("SekiroAnimGraphIRTests.WriterSource"));
    const FString GeneratedModule(TEXT("SekiroAnimGraphIRTests.WriterSource.generated"));
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("Writer test UnLua environment is available"), Environment);
    if (Environment == nullptr) return true;

    TestTrue(
        TEXT("Writer source module is injected"),
        Environment->DoString(
            SekiroAnimGraphIRLuaImporterTests::BuildValidModuleChunk(SourceModule),
            TEXT("SekiroAnimGraphIRTests.WriterSource.Inject")));

    FSekiroAnimBlueprintIR SourceIR;
    TArray<FSekiroAnimIRDiagnostic> SourceDiagnostics;
    TestTrue(
        TEXT("Writer source module imports"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            SourceModule,
            SourceIR,
            SourceDiagnostics));
    if (!SourceIR.Layers.IsEmpty() && !SourceIR.Layers[0].Graphs.IsEmpty())
    {
        SourceIR.Layers[0].Graphs[0].Name = TEXT("Main \"line\"\n\t\\中文");
    }
    if (SourceIR.Layers.IsEmpty()) return true;
    for (FSekiroAnimIRGraph& Graph : SourceIR.Layers[0].Graphs)
    {
        if (Graph.StateMachine.Transitions.IsEmpty()) continue;
        FSekiroAnimIRTransition& Transition = Graph.StateMachine.Transitions[0];
        Transition.Gate.RootIndex = 0;
        FSekiroAnimIRTransitionGateNode& GateNode =
            Transition.Gate.Nodes.AddDefaulted_GetRef();
        GateNode.Type = TEXT("LuaBool");
        break;
    }

    FString FirstText;
    FString SecondText;
    TArray<FSekiroAnimIRDiagnostic> FirstDiagnostics;
    TArray<FSekiroAnimIRDiagnostic> SecondDiagnostics;
    TestTrue(
        TEXT("First deterministic write succeeds"),
        FSekiroAnimGraphIRLuaWriter::WriteModule(
            SourceIR,
            FirstText,
            FirstDiagnostics));
    TestTrue(
        TEXT("Second deterministic write succeeds"),
        FSekiroAnimGraphIRLuaWriter::WriteModule(
            SourceIR,
            SecondText,
            SecondDiagnostics));
    TestEqual(TEXT("Repeated writes are byte-stable"), FirstText, SecondText);
    TestTrue(TEXT("Writer escapes quote"), FirstText.Contains(TEXT("\\\"line\\\"")));
    TestTrue(TEXT("Writer escapes newline"), FirstText.Contains(TEXT("\\n")));
    TestTrue(TEXT("Writer escapes tab"), FirstText.Contains(TEXT("\\t")));
    TestTrue(TEXT("Writer preserves negative layout Y"), FirstText.Contains(TEXT("Y = -80")));
    TestTrue(TEXT("Writer preserves Rule AST root"), FirstText.Contains(TEXT("RootIndex = 0")));
    TestTrue(TEXT("Writer preserves Rule AST node type"), FirstText.Contains(TEXT("Type = \"LuaBool\"")));
    TestTrue(TEXT("Writer emits Integer IR value storage"), FirstText.Contains(TEXT("IntegerValue = 0")));
    TestTrue(TEXT("Writer emits Name IR value storage"), FirstText.Contains(TEXT("NameValue = \"\"")));
    TestTrue(TEXT("Writer emits String IR value storage"), FirstText.Contains(TEXT("StringValue = \"\"")));
    TestTrue(TEXT("Writer emits SoftClass IR value storage"), FirstText.Contains(TEXT("SoftClassPathValue = \"\"")));
    TestTrue(
        TEXT("Writer delegates runtime rules to SourceModule"),
        FirstText.Contains(TEXT("local RuntimeModule = require(IR.SourceModule)")));

    const FString GeneratedChunk = TEXT("package.loaded[\"") + GeneratedModule
        + TEXT("\"] = nil\npackage.preload[\"") + GeneratedModule
        + TEXT("\"] = function()\n") + FirstText + TEXT("\nend\n");
    TestTrue(
        TEXT("Generated writer module is injected"),
        Environment->DoString(
            GeneratedChunk,
            TEXT("SekiroAnimGraphIRTests.WriterGenerated.Inject")));

    FSekiroAnimBlueprintIR RoundTripIR;
    TArray<FSekiroAnimIRDiagnostic> RoundTripDiagnostics;
    TestTrue(
        TEXT("Generated module imports through existing CompileLuaModule"),
        USekiroAnimGraphIRLibrary::CompileLuaModule(
            GeneratedModule,
            RoundTripIR,
            RoundTripDiagnostics));
    USekiroAnimGraphIRLibrary::Canonicalize(SourceIR);
    TestTrue(
        TEXT("Writer and Importer preserve complete Canonical IR"),
        FSekiroAnimBlueprintIR::StaticStruct()->CompareScriptStruct(
            &SourceIR,
            &RoundTripIR,
            0));
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
