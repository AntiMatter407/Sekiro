#include "SekiroAnimGraphNodeRegistry.h"

namespace SekiroAnimGraphNodeRegistryPrivate
{
    /**
     * 构造插件内置 NodeType 的完整权威契约快照。
     * 本函数仅创建值类型和软类路径，不加载 UObject，可在任意线程调用。
     *
     * @return 按稳定注册顺序排列的节点契约数组，调用方取得独立值。
     */
    TArray<FSekiroAnimIRNodeContract> BuildContracts()
    {
        TArray<FSekiroAnimIRNodeContract> Contracts;
        Contracts.Reserve(13);

        FSekiroAnimIRNodeContract& OutputPose = Contracts.AddDefaulted_GetRef();
        OutputPose.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
        OutputPose.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Root"));
        OutputPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        OutputPose.RootRole = ESekiroAnimIRNodeRootRole::GraphRoot;
        FSekiroAnimIRPinContract& OutputResult = OutputPose.Pins.AddDefaulted_GetRef();
        OutputResult.Name = TEXT("Result");
        OutputResult.Direction = ESekiroAnimIRPinDirection::Input;
        OutputResult.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRNodeContract& StateResult = Contracts.AddDefaulted_GetRef();
        StateResult.NodeType = SekiroAnimGraphIRNames::StateResultNode;
        StateResult.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateResult"));
        StateResult.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        StateResult.RootRole = ESekiroAnimIRNodeRootRole::GraphRoot;
        FSekiroAnimIRPinContract& StateResultPin = StateResult.Pins.AddDefaulted_GetRef();
        StateResultPin.Name = TEXT("Result");
        StateResultPin.Direction = ESekiroAnimIRPinDirection::Input;
        StateResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRNodeContract& SequencePlayer = Contracts.AddDefaulted_GetRef();
        SequencePlayer.NodeType = SekiroAnimGraphIRNames::SequencePlayerNode;
        SequencePlayer.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
        SequencePlayer.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        SequencePlayer.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& SequencePose = SequencePlayer.Pins.AddDefaulted_GetRef();
        SequencePose.Name = TEXT("Pose");
        SequencePose.Direction = ESekiroAnimIRPinDirection::Output;
        SequencePose.DataType = SekiroAnimGraphIRNames::PoseData;
        SequencePose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& Sequence = SequencePlayer.Properties.AddDefaulted_GetRef();
        Sequence.Name = TEXT("Sequence");
        Sequence.ValueType = ESekiroAnimIRValueType::SoftObjectPath;
        Sequence.bRequired = true;
        FSekiroAnimIRPropertyContract& LoopAnimation = SequencePlayer.Properties.AddDefaulted_GetRef();
        LoopAnimation.Name = TEXT("bLoopAnimation");
        LoopAnimation.ValueType = ESekiroAnimIRValueType::Bool;
        FSekiroAnimIRPropertyContract& PlayRate = SequencePlayer.Properties.AddDefaulted_GetRef();
        PlayRate.Name = TEXT("PlayRate");
        PlayRate.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& StartPosition = SequencePlayer.Properties.AddDefaulted_GetRef();
        StartPosition.Name = TEXT("StartPosition");
        StartPosition.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& GroupName = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupName.Name = TEXT("GroupName");
        GroupName.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& GroupRole = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupRole.Name = TEXT("GroupRole");
        GroupRole.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& GroupMethod = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupMethod.Name = TEXT("GroupMethod");
        GroupMethod.ValueType = ESekiroAnimIRValueType::Name;

        FSekiroAnimIRNodeContract& StateMachine = Contracts.AddDefaulted_GetRef();
        StateMachine.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
        StateMachine.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateMachine"));
        StateMachine.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        StateMachine.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        StateMachine.OwnedGraphPolicy = ESekiroAnimIROwnedGraphPolicy::Required;
        StateMachine.OwnedGraphType = SekiroAnimGraphIRNames::StateMachineGraph;
        FSekiroAnimIRPinContract& StateMachinePose = StateMachine.Pins.AddDefaulted_GetRef();
        StateMachinePose.Name = TEXT("Pose");
        StateMachinePose.Direction = ESekiroAnimIRPinDirection::Output;
        StateMachinePose.DataType = SekiroAnimGraphIRNames::PoseData;
        StateMachinePose.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& Inertialization = Contracts.AddDefaulted_GetRef();
        Inertialization.NodeType = SekiroAnimGraphIRNames::InertializationNode;
        Inertialization.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Inertialization"));
        Inertialization.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        Inertialization.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& InertialSource = Inertialization.Pins.AddDefaulted_GetRef();
        InertialSource.Name = TEXT("Source");
        InertialSource.Direction = ESekiroAnimIRPinDirection::Input;
        InertialSource.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& InertialPose = Inertialization.Pins.AddDefaulted_GetRef();
        InertialPose.Name = TEXT("Pose");
        InertialPose.Direction = ESekiroAnimIRPinDirection::Output;
        InertialPose.DataType = SekiroAnimGraphIRNames::PoseData;
        InertialPose.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& SaveCachedPose = Contracts.AddDefaulted_GetRef();
        SaveCachedPose.NodeType = SekiroAnimGraphIRNames::SaveCachedPoseNode;
        SaveCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SaveCachedPose"));
        SaveCachedPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        FSekiroAnimIRPinContract& CachedPoseInput = SaveCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseInput.Name = TEXT("Pose");
        CachedPoseInput.Direction = ESekiroAnimIRPinDirection::Input;
        CachedPoseInput.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPropertyContract& SaveCacheName = SaveCachedPose.Properties.AddDefaulted_GetRef();
        SaveCacheName.Name = TEXT("CacheName");
        SaveCacheName.ValueType = ESekiroAnimIRValueType::String;
        SaveCacheName.bRequired = true;

        FSekiroAnimIRNodeContract& UseCachedPose = Contracts.AddDefaulted_GetRef();
        UseCachedPose.NodeType = SekiroAnimGraphIRNames::UseCachedPoseNode;
        UseCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_UseCachedPose"));
        UseCachedPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        FSekiroAnimIRPinContract& CachedPoseOutput = UseCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseOutput.Name = TEXT("Pose");
        CachedPoseOutput.Direction = ESekiroAnimIRPinDirection::Output;
        CachedPoseOutput.DataType = SekiroAnimGraphIRNames::PoseData;
        CachedPoseOutput.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& UseCacheName = UseCachedPose.Properties.AddDefaulted_GetRef();
        UseCacheName.Name = TEXT("CacheName");
        UseCacheName.ValueType = ESekiroAnimIRValueType::String;
        UseCacheName.bRequired = true;

        const FName GetterTypes[] = {
            SekiroAnimGraphIRNames::BoolPropertyGetterNode,
            SekiroAnimGraphIRNames::FloatPropertyGetterNode,
            SekiroAnimGraphIRNames::BytePropertyGetterNode,
            SekiroAnimGraphIRNames::EnumPropertyGetterNode,
        };
        const FName GetterDataTypes[] = {
            SekiroAnimGraphIRNames::BoolData,
            SekiroAnimGraphIRNames::FloatData,
            SekiroAnimGraphIRNames::ByteData,
            SekiroAnimGraphIRNames::EnumData,
        };
        for (int32 GetterIndex = 0; GetterIndex < UE_ARRAY_COUNT(GetterTypes); ++GetterIndex)
        {
            FSekiroAnimIRNodeContract& Getter = Contracts.AddDefaulted_GetRef();
            Getter.NodeType = GetterTypes[GetterIndex];
            Getter.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/BlueprintGraph.K2Node_VariableGet"));
            Getter.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
            Getter.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
            FSekiroAnimIRPinContract& ValuePin = Getter.Pins.AddDefaulted_GetRef();
            ValuePin.Name = TEXT("Value");
            ValuePin.Direction = ESekiroAnimIRPinDirection::Output;
            ValuePin.DataType = GetterDataTypes[GetterIndex];
            ValuePin.bAllowMultipleConnections = true;
            FSekiroAnimIRPropertyContract& PropertyName = Getter.Properties.AddDefaulted_GetRef();
            PropertyName.Name = TEXT("PropertyName");
            PropertyName.ValueType = ESekiroAnimIRValueType::Name;
            PropertyName.bRequired = true;
        }

        FSekiroAnimIRNodeContract& BlendByBool = Contracts.AddDefaulted_GetRef();
        BlendByBool.NodeType = SekiroAnimGraphIRNames::BlendListByBoolNode;
        BlendByBool.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"));
        BlendByBool.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        BlendByBool.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        const TCHAR* BoolPoseNames[] = { TEXT("TruePose"), TEXT("FalsePose") };
        for (const TCHAR* PoseName : BoolPoseNames)
        {
            FSekiroAnimIRPinContract& PosePin = BlendByBool.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ESekiroAnimIRPinDirection::Input;
            PosePin.DataType = SekiroAnimGraphIRNames::PoseData;
        }
        FSekiroAnimIRPinContract& BoolActive = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolActive.Name = TEXT("ActiveValue");
        BoolActive.Direction = ESekiroAnimIRPinDirection::Input;
        BoolActive.DataType = SekiroAnimGraphIRNames::BoolData;
        FSekiroAnimIRPinContract& BoolPose = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolPose.Name = TEXT("Pose");
        BoolPose.Direction = ESekiroAnimIRPinDirection::Output;
        BoolPose.DataType = SekiroAnimGraphIRNames::PoseData;
        BoolPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& BoolBlendTime = BlendByBool.Properties.AddDefaulted_GetRef();
        BoolBlendTime.Name = TEXT("BlendTime");
        BoolBlendTime.ValueType = ESekiroAnimIRValueType::Float;

        FSekiroAnimIRNodeContract& BlendByEnum = Contracts.AddDefaulted_GetRef();
        BlendByEnum.NodeType = SekiroAnimGraphIRNames::BlendListByEnumNode;
        BlendByEnum.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum"));
        BlendByEnum.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        BlendByEnum.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        const TCHAR* EnumPoseNames[] = {
            TEXT("DefaultPose"), TEXT("Pose0"), TEXT("Pose1"), TEXT("Pose2"), TEXT("Pose3"),
            TEXT("Pose4"), TEXT("Pose5"), TEXT("Pose6"), TEXT("Pose7")
        };
        for (const TCHAR* PoseName : EnumPoseNames)
        {
            FSekiroAnimIRPinContract& PosePin = BlendByEnum.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ESekiroAnimIRPinDirection::Input;
            PosePin.DataType = SekiroAnimGraphIRNames::PoseData;
        }
        FSekiroAnimIRPinContract& EnumActive = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumActive.Name = TEXT("ActiveValue");
        EnumActive.Direction = ESekiroAnimIRPinDirection::Input;
        EnumActive.DataType = SekiroAnimGraphIRNames::EnumData;
        FSekiroAnimIRPinContract& EnumPose = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumPose.Name = TEXT("Pose");
        EnumPose.Direction = ESekiroAnimIRPinDirection::Output;
        EnumPose.DataType = SekiroAnimGraphIRNames::PoseData;
        EnumPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& EnumType = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumType.Name = TEXT("EnumType");
        EnumType.ValueType = ESekiroAnimIRValueType::SoftObjectPath;
        EnumType.bRequired = true;
        FSekiroAnimIRPropertyContract& EnumEntries = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumEntries.Name = TEXT("EnumEntries");
        EnumEntries.ValueType = ESekiroAnimIRValueType::String;
        EnumEntries.bRequired = true;
        FSekiroAnimIRPropertyContract& EnumBlendTime = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumBlendTime.Name = TEXT("BlendTime");
        EnumBlendTime.ValueType = ESekiroAnimIRValueType::Float;

        return Contracts;
    }

    /**
     * 取得进程内唯一的不可变内置节点契约数组。
     * C++11 静态局部初始化保证首次并发查询安全；函数不加载 UObject。
     *
     * @return 注册表内部数组的常量引用，其生命周期持续到进程结束。
     */
    const TArray<FSekiroAnimIRNodeContract>& GetRegisteredContracts()
    {
        static const TArray<FSekiroAnimIRNodeContract> Contracts = BuildContracts();
        return Contracts;
    }
}

/**
 * 返回全部内置 NodeType 契约的只读连续视图。
 * 本函数可在任意线程调用，不加载 UObject；返回视图不可修改注册表且在进程生命周期内有效。
 *
 * @return 按稳定注册顺序排列的常量契约视图。
 */
TConstArrayView<FSekiroAnimIRNodeContract> FSekiroAnimGraphNodeRegistry::GetContracts()
{
    return SekiroAnimGraphNodeRegistryPrivate::GetRegisteredContracts();
}

/**
 * 按稳定注册名查询单个 NodeType 的权威契约。
 * 本函数执行大小写不敏感的 FName 比较，不加载 UObject，可在任意线程调用。
 *
 * @param NodeType Lua IR 声明的节点类型注册名；None 不匹配任何契约。
 * @return 找到时返回注册表内部常量指针，否则返回 nullptr；指针在进程生命周期内有效。
 */
const FSekiroAnimIRNodeContract* FSekiroAnimGraphNodeRegistry::Find(const FName NodeType)
{
    for (const FSekiroAnimIRNodeContract& Contract : SekiroAnimGraphNodeRegistryPrivate::GetRegisteredContracts())
    {
        if (Contract.NodeType == NodeType) return &Contract;
    }

    return nullptr;
}
