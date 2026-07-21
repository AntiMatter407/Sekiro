#include "SekiroAnimBlueprintFactoryLibrary.h"

#include "AlphaBlend.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LegIK.h"
#include "AnimGraph/AnimGraphNode_FootPlacement.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNodes/AnimNode_BlendListBase.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_CallFunction.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "SekiroAnimGraphIRLibrary.h"
#include "SekiroAnimGraphNodeRegistry.h"
#include "SekiroLuaAnimBlueprintExtension.h"
#include "SekiroLuaTransitionRuntimeLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UnLuaFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroLuaAnimBlueprintCompiler, Log, All);

namespace SekiroAnimBlueprintFactoryPrivate
{
    const FName WrongThread = TEXT("Factory.WrongThread");
    const FName UnsupportedLayerCount = TEXT("Factory.UnsupportedLayerCount");
    const FName ParentClassLoadFailed = TEXT("Factory.ParentClassLoadFailed");
    const FName ParentClassTypeMismatch = TEXT("Factory.ParentClassTypeMismatch");
    const FName UnsupportedParentBlueprintClass = TEXT("Factory.UnsupportedParentBlueprintClass");
    const FName TargetSkeletonLoadFailed = TEXT("Factory.TargetSkeletonLoadFailed");
    const FName TargetSkeletonTypeMismatch = TEXT("Factory.TargetSkeletonTypeMismatch");
    const FName NodeClassLoadFailed = TEXT("Factory.NodeClassLoadFailed");
    const FName UnsupportedRegisteredNodeClass = TEXT("Factory.UnsupportedRegisteredNodeClass");
    const FName AnimationAssetLoadFailed = TEXT("Factory.AnimationAssetLoadFailed");
    const FName AnimationAssetTypeMismatch = TEXT("Factory.AnimationAssetTypeMismatch");
    const FName AnimationSkeletonMismatch = TEXT("Factory.AnimationSkeletonMismatch");
    const FName UnsupportedTransitionBlendMode = TEXT("Factory.UnsupportedTransitionBlendMode");
    const FName InvalidPackagePath = TEXT("Factory.InvalidPackagePath");
    const FName AssetAlreadyExists = TEXT("Factory.AssetAlreadyExists");
    const FName BlueprintCreationFailed = TEXT("Factory.BlueprintCreationFailed");
    const FName MainAnimGraphNotFound = TEXT("Factory.MainAnimGraphNotFound");
    const FName DefaultRootNodeNotFound = TEXT("Factory.DefaultRootNodeNotFound");
    const FName NativeNodeCreationFailed = TEXT("Factory.NativeNodeCreationFailed");
    const FName InvalidFootPlacementLegDefinitions = TEXT("Factory.InvalidFootPlacementLegDefinitions");
    const FName InvalidLegIKDefinitions = TEXT("Factory.InvalidLegIKDefinitions");
    const FName InvalidLayeredBlendBranchFilters = TEXT("Factory.InvalidLayeredBlendBranchFilters");
    const FName InvalidLayeredBlendCurveOption = TEXT("Factory.InvalidLayeredBlendCurveOption");
    const FName InvalidFootPlacementPlantSpeedMode = TEXT("Factory.InvalidFootPlacementPlantSpeedMode");
    const FName InvalidFootPlacementPlantLockType = TEXT("Factory.InvalidFootPlacementPlantLockType");
    const FName NativePinNotFound = TEXT("Factory.NativePinNotFound");
    const FName ConnectionFailed = TEXT("Factory.ConnectionFailed");
    const FName EmptyCachedPoseName = TEXT("Factory.EmptyCachedPoseName");
    const FName DuplicateCachedPoseName = TEXT("Factory.DuplicateCachedPoseName");
    const FName CachedPoseSaveNotFound = TEXT("Factory.CachedPoseSaveNotFound");
    const FName NativeGraphMissing = TEXT("Factory.NativeGraphMissing");
    const FName EventGraphNotFound = TEXT("Factory.EventGraphNotFound");
    const FName K2FunctionNotFound = TEXT("Factory.K2FunctionNotFound");
    const FName K2PinNotFound = TEXT("Factory.K2PinNotFound");
    const FName K2ConnectionFailed = TEXT("Factory.K2ConnectionFailed");
    const FName TransitionResultNotFound = TEXT("Factory.TransitionResultNotFound");
    const FName BlueprintCompileFailed = TEXT("Factory.BlueprintCompileFailed");
    const FName SavePackageFailed = TEXT("Factory.SavePackageFailed");
    const FName EnumTypeLoadFailed = TEXT("Factory.EnumTypeLoadFailed");
    const FName MemberVariableCreationFailed = TEXT("Factory.MemberVariableCreationFailed");
    const FName MissingLuaBlueprintExtension = TEXT("Factory.MissingLuaBlueprintExtension");
    const FName EmptyLuaModuleName = TEXT("Factory.EmptyLuaModuleName");
    const FName AssetConfigurationMismatch = TEXT("Factory.AssetConfigurationMismatch");
    const FName InPlaceCommitFailed = TEXT("Factory.InPlaceCommitFailed");
    const FName TransactionUnavailable = TEXT("Factory.TransactionUnavailable");
    const FName PIECompileForbidden = TEXT("Factory.PIECompileForbidden");
    const FName CompileAllReentry = TEXT("Factory.CompileAllReentry");
    const FName CompileAssetReentry = TEXT("Factory.CompileAssetReentry");

    bool GIsCompilingDirtyLuaAnimBlueprints = false;
    bool GIsCompilingLuaAnimBlueprintInPlace = false;
    bool GIsPreparingLuaAnimBlueprintGraph = false;
    TSet<UAnimBlueprint*> GPreparingLuaAnimBlueprints;

    /** 保存创建前解析出的 UObject 与注册表类，确保物化阶段不再遇到可预见的加载失败。 */
    struct FPreflightData
    {
        FSekiroAnimBlueprintIR Blueprint;
        UClass* ParentClass = nullptr;
        USkeleton* TargetSkeleton = nullptr;
        TMap<FName, UClass*> NodeClasses;
        TMap<FString, UAnimSequenceBase*> SequenceAssets;
        TMap<FName, UEnum*> VariableEnums;
        TMap<FString, UEnum*> NodeEnums;
        TMap<FString, TArray<FFootPlacemenLegDefinition>> FootPlacementLegDefinitions;
        TMap<FString, TArray<FAnimLegIKDefinition>> LegIKDefinitions;
        TMap<FString, TArray<FBranchFilter>> LayeredBlendBranchFilters;
        TMap<FString, EFootPlacementLockType> FootPlacementLockTypes;
    };

    /**
     * 向工厂诊断数组追加一条稳定 Error，并保留 IR 源位置。
     * 本函数只修改调用方独占的数组，不加载对象，可在任意线程调用。
     *
     * @param Diagnostics 接收新诊断的可变数组。
     * @param Code 稳定诊断码。
     * @param Message 面向使用者的错误说明。
     * @param SubjectId 发生错误的 IR 实体 ID 或字段名。
     * @param SourceLocation 对应 Lua 声明位置。
     * @return 无返回值。
     */
    void AddError(
        TArray<FSekiroAnimIRDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& SubjectId,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ESekiroAnimIRDiagnosticSeverity::Error;
        Diagnostic.Message = Message;
        Diagnostic.SubjectId = SubjectId;
        Diagnostic.SourceLocation = SourceLocation;
    }

    /**
     * 查询诊断数组中是否已存在 Error，用于阻止任何 UObject 创建副作用。
     * 本函数只读遍历值类型数组，可在任意线程调用。
     *
     * @param Diagnostics 待检查的结构化诊断。
     * @return 存在至少一个 Error 时返回 true，否则返回 false。
     */
    bool HasErrors(const TArray<FSekiroAnimIRDiagnostic>& Diagnostics)
    {
        for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Severity == ESekiroAnimIRDiagnosticSeverity::Error) return true;
        }

        return false;
    }

    /**
     * 在节点属性数组中按注册名查找只读属性。
     * 本函数不加载 UObject，可在任意线程调用；返回指针只在节点属性数组不扩容时有效。
     *
     * @param Node 待查询的 IR 节点。
     * @param PropertyName 注册表定义的属性名。
     * @return 找到时返回数组元素指针，否则返回 nullptr。
     */
    const FSekiroAnimIRProperty* FindProperty(const FSekiroAnimIRNode& Node, const FName PropertyName)
    {
        for (const FSekiroAnimIRProperty& Property : Node.Properties)
        {
            if (Property.Name == PropertyName) return &Property;
        }

        return nullptr;
    }

    /**
     * 将 FootPlacement 的紧凑双腿字符串解析为引擎原生腿定义。
     * 本函数仅处理值类型，可在任意线程调用；失败时清空输出且不访问 Skeleton 或 UObject。
     *
     * @param DefinitionText `FKFootBone,IKFootBone,BallBone,NumBonesInLimb|...` 格式字符串。
     * @param OutDefinitions 接收已去除字段首尾空白的原生腿定义；失败时为空。
     * @param OutError 接收首个格式错误的人类可读说明；成功时为空。
     * @return 所有分组均包含四个有效字段且 NumBonesInLimb 大于等于 1 时返回 true。
     */
    bool ParseFootPlacementLegDefinitions(
        const FString& DefinitionText,
        TArray<FFootPlacemenLegDefinition>& OutDefinitions,
        FString& OutError)
    {
        OutDefinitions.Reset();
        OutError.Reset();
        TArray<FString> LegStrings;
        DefinitionText.ParseIntoArray(LegStrings, TEXT("|"), false);
        if (LegStrings.IsEmpty())
        {
            OutError = TEXT("LegDefinitions must contain at least one leg.");
            return false;
        }

        for (int32 LegIndex = 0; LegIndex < LegStrings.Num(); ++LegIndex)
        {
            TArray<FString> Fields;
            LegStrings[LegIndex].ParseIntoArray(Fields, TEXT(","), false);
            if (Fields.Num() != 4)
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d must contain exactly four comma-separated fields."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }

            for (FString& Field : Fields) Field = Field.TrimStartAndEnd();
            const FName FKFootBone(*Fields[0]);
            const FName IKFootBone(*Fields[1]);
            const FName BallBone(*Fields[2]);
            int32 NumBonesInLimb = 0;
            if (FKFootBone.IsNone() || IKFootBone.IsNone() || BallBone.IsNone())
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d contains an empty or None bone name."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }
            if (!LexTryParseString(NumBonesInLimb, *Fields[3]) || NumBonesInLimb < 1)
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d requires NumBonesInLimb >= 1."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }

            FFootPlacemenLegDefinition& Definition = OutDefinitions.AddDefaulted_GetRef();
            Definition.FKFootBone = FBoneReference(FKFootBone);
            Definition.IKFootBone = FBoneReference(IKFootBone);
            Definition.BallBone = FBoneReference(BallBone);
            Definition.NumBonesInLimb = NumBonesInLimb;
        }

        return true;
    }

    /**
     * 将 LegIK 的紧凑双腿字符串解析为引擎原生腿定义。
     * 本函数仅处理值类型，可在任意线程调用；失败时清空输出且不访问 Skeleton 或 UObject。
     *
     * @param DefinitionText `IKFootBone,FKFootBone,NumBonesInLimb|...` 格式字符串。
     * @param OutDefinitions 接收已去除字段首尾空白的原生腿定义；失败时为空。
     * @param OutError 接收首个格式错误的人类可读说明；成功时为空。
     * @return 所有分组均包含三个有效字段且 NumBonesInLimb 大于等于 1 时返回 true。
     */
    bool ParseLegIKDefinitions(
        const FString& DefinitionText,
        TArray<FAnimLegIKDefinition>& OutDefinitions,
        FString& OutError)
    {
        OutDefinitions.Reset();
        OutError.Reset();
        TArray<FString> LegStrings;
        DefinitionText.ParseIntoArray(LegStrings, TEXT("|"), false);
        if (LegStrings.IsEmpty())
        {
            OutError = TEXT("LegDefinitions must contain at least one leg.");
            return false;
        }

        for (int32 LegIndex = 0; LegIndex < LegStrings.Num(); ++LegIndex)
        {
            TArray<FString> Fields;
            LegStrings[LegIndex].ParseIntoArray(Fields, TEXT(","), false);
            if (Fields.Num() != 3)
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d must contain exactly three comma-separated fields."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }

            for (FString& Field : Fields) Field = Field.TrimStartAndEnd();
            const FName IKFootBone(*Fields[0]);
            const FName FKFootBone(*Fields[1]);
            int32 NumBonesInLimb = 0;
            if (IKFootBone.IsNone() || FKFootBone.IsNone())
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d contains an empty or None bone name."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }
            if (!LexTryParseString(NumBonesInLimb, *Fields[2]) || NumBonesInLimb < 1)
            {
                OutError = FString::Printf(
                    TEXT("LegDefinitions entry %d requires NumBonesInLimb >= 1."),
                    LegIndex);
                OutDefinitions.Reset();
                return false;
            }

            FAnimLegIKDefinition& Definition = OutDefinitions.AddDefaulted_GetRef();
            Definition.IKFootBone = FBoneReference(IKFootBone);
            Definition.FKFootBone = FBoneReference(FKFootBone);
            Definition.NumBonesInLimb = NumBonesInLimb;
        }

        return true;
    }

    /**
     * 将 Layered Blend Per Bone 的紧凑分支过滤字符串解析为引擎原生过滤器。
     * 本函数仅处理值类型，可在任意线程调用；失败时清空输出，不校验骨骼是否存在于具体 Skeleton。
     *
     * @param DefinitionText `BoneName,BlendDepth|...` 格式字符串，至少包含一个过滤分支。
     * @param OutFilters 接收保持声明顺序的原生分支过滤器；失败时为空。
     * @param OutError 接收首个格式错误的人类可读说明；成功时为空。
     * @return 所有分组均包含有效骨骼名和整数 BlendDepth 时返回 true。
     */
    bool ParseLayeredBlendBranchFilters(
        const FString& DefinitionText,
        TArray<FBranchFilter>& OutFilters,
        FString& OutError)
    {
        OutFilters.Reset();
        OutError.Reset();
        TArray<FString> FilterStrings;
        DefinitionText.ParseIntoArray(FilterStrings, TEXT("|"), false);
        if (FilterStrings.IsEmpty())
        {
            OutError = TEXT("BranchFilters must contain at least one bone filter.");
            return false;
        }

        for (int32 FilterIndex = 0; FilterIndex < FilterStrings.Num(); ++FilterIndex)
        {
            TArray<FString> Fields;
            FilterStrings[FilterIndex].ParseIntoArray(Fields, TEXT(","), false);
            if (Fields.Num() != 2)
            {
                OutError = FString::Printf(
                    TEXT("BranchFilters entry %d must contain BoneName and BlendDepth."),
                    FilterIndex);
                OutFilters.Reset();
                return false;
            }

            for (FString& Field : Fields) Field = Field.TrimStartAndEnd();
            const FName BoneName(*Fields[0]);
            int32 BlendDepth = 0;
            if (BoneName.IsNone())
            {
                OutError = FString::Printf(
                    TEXT("BranchFilters entry %d contains an empty or None bone name."),
                    FilterIndex);
                OutFilters.Reset();
                return false;
            }
            if (!LexTryParseString(BlendDepth, *Fields[1]))
            {
                OutError = FString::Printf(
                    TEXT("BranchFilters entry %d requires an integer BlendDepth."),
                    FilterIndex);
                OutFilters.Reset();
                return false;
            }

            FBranchFilter& Filter = OutFilters.AddDefaulted_GetRef();
            Filter.BoneName = BoneName;
            Filter.BlendDepth = BlendDepth;
        }

        return true;
    }

    /**
     * 将 Lua 使用的稳定曲线混合名称解析为 UE5.2 的 Layered Blend 曲线策略。
     * 本函数仅比较名称并写入值类型输出，可在任意线程调用。
     *
     * @param OptionName CurveBlendOption 属性值；允许 UE5.2 ECurveBlendOption 的七个公开名称。
     * @param OutOption 接收匹配的引擎枚举；失败时保持调用前的值。
     * @return 名称精确匹配一个受支持枚举值时返回 true，否则返回 false。
     */
    bool ParseLayeredBlendCurveOption(
        const FName OptionName,
        ECurveBlendOption::Type& OutOption)
    {
        if (OptionName == TEXT("Override")) OutOption = ECurveBlendOption::Override;
        else if (OptionName == TEXT("DoNotOverride")) OutOption = ECurveBlendOption::DoNotOverride;
        else if (OptionName == TEXT("NormalizeByWeight")) OutOption = ECurveBlendOption::NormalizeByWeight;
        else if (OptionName == TEXT("BlendByWeight")) OutOption = ECurveBlendOption::BlendByWeight;
        else if (OptionName == TEXT("UseBasePose")) OutOption = ECurveBlendOption::UseBasePose;
        else if (OptionName == TEXT("UseMaxValue")) OutOption = ECurveBlendOption::UseMaxValue;
        else if (OptionName == TEXT("UseMinValue")) OutOption = ECurveBlendOption::UseMinValue;
        else return false;
        return true;
    }

    /**
     * 将 Lua IR 的 FootPlacement 植脚锁定名称解析为引擎枚举。
     * 本函数仅比较 FName 并写入值类型输出，可在任意线程调用，不访问 UObject。
     *
     * @param LockTypeName PlantLockType 属性值；允许 Unlocked、PivotAroundBall、PivotAroundAnkle、LockRotation。
     * @param OutLockType 接收匹配的引擎锁定枚举；失败时保持调用前的值。
     * @return 名称精确匹配一个受支持枚举值时返回 true，否则返回 false。
     */
    bool ParseFootPlacementLockType(
        const FName LockTypeName,
        EFootPlacementLockType& OutLockType)
    {
        if (LockTypeName == TEXT("Unlocked"))
        {
            OutLockType = EFootPlacementLockType::Unlocked;
            return true;
        }
        if (LockTypeName == TEXT("PivotAroundBall"))
        {
            OutLockType = EFootPlacementLockType::PivotAroundBall;
            return true;
        }
        if (LockTypeName == TEXT("PivotAroundAnkle"))
        {
            OutLockType = EFootPlacementLockType::PivotAroundAnkle;
            return true;
        }
        if (LockTypeName == TEXT("LockRotation"))
        {
            OutLockType = EFootPlacementLockType::LockRotation;
            return true;
        }

        return false;
    }

    /**
     * 覆盖 BlendList 每个 Pose 的统一 BlendTime；仅操作尚未编译的编辑器节点结构体。
     * 必须在游戏线程调用，Node 由当前 Factory 独占。
     *
     * @param Node 原生 BlendList 运行时结构。
     * @param BlendTime 所有输入 Pose 使用的混合时长，单位秒。
     */
    void SetBlendListTimes(FAnimNode_BlendListBase& Node, const float BlendTime)
    {
        FArrayProperty* BlendTimeProperty = FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendTime"));
        if (BlendTimeProperty == nullptr) return;
        FScriptArrayHelper ArrayHelper(BlendTimeProperty, BlendTimeProperty->ContainerPtrToValuePtr<void>(&Node));
        FFloatProperty* FloatProperty = CastField<FFloatProperty>(BlendTimeProperty->Inner);
        if (FloatProperty == nullptr) return;
        for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
        {
            FloatProperty->SetPropertyValue(ArrayHelper.GetRawPtr(Index), BlendTime);
        }
    }

    /**
     * 判断注册表解析出的编辑器类是否属于本阶段可物化的原生节点。
     * 本函数只比较已加载 UClass，必须在游戏线程调用以遵守 UObject 访问约束。
     *
     * @param NodeClass 注册表 EditorNodeClassPath 解析出的类。
     * @return 类精确匹配当前受支持原生节点之一时返回 true。
     */
    bool IsSupportedNodeClass(const UClass* NodeClass)
    {
        return NodeClass == UAnimGraphNode_Root::StaticClass()
            || NodeClass == UAnimGraphNode_StateResult::StaticClass()
            || NodeClass == UAnimGraphNode_SequencePlayer::StaticClass()
            || NodeClass == UAnimGraphNode_StateMachine::StaticClass()
            || NodeClass == UAnimGraphNode_Inertialization::StaticClass()
            || NodeClass == UAnimGraphNode_LocalToComponentSpace::StaticClass()
            || NodeClass == UAnimGraphNode_ComponentToLocalSpace::StaticClass()
            || NodeClass == UAnimGraphNode_OrientationWarping::StaticClass()
            || NodeClass == UAnimGraphNode_FootPlacement::StaticClass()
            || NodeClass == UAnimGraphNode_LegIK::StaticClass()
            || NodeClass == UAnimGraphNode_SaveCachedPose::StaticClass()
            || NodeClass == UAnimGraphNode_UseCachedPose::StaticClass()
            || NodeClass == UK2Node_VariableGet::StaticClass()
            || NodeClass == UAnimGraphNode_BlendListByBool::StaticClass()
            || NodeClass == UAnimGraphNode_BlendListByEnum::StaticClass()
            || NodeClass == UAnimGraphNode_Slot::StaticClass()
            || NodeClass == UAnimGraphNode_LayeredBoneBlend::StaticClass();
    }

    /**
     * 将稳定 IR ID 哈希为确定性 FGuid，供 GraphGuid 与 NodeGuid 重建复用。
     * 本函数只处理字符串和栈内 MD5 状态，可在任意线程调用。
     *
     * @param Domain 实体类别前缀，用于隔离 Graph、Node、State 与 Transition 命名域。
     * @param StableId IR 提供的全局稳定 ID。
     * @return 对相同 Domain 与 StableId 始终一致的非随机 Guid。
     */
    FGuid MakeStableGuid(const TCHAR* Domain, const FString& StableId)
    {
        const FString Source = FString(Domain) + TEXT("|") + StableId;
        const FTCHARToUTF8 Utf8(*Source);
        FMD5 Md5;
        Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        uint8 Digest[16];
        Md5.Final(Digest);

        FGuid Result;
        FMemory::Memcpy(&Result, Digest, sizeof(FGuid));
        return Result;
    }

    /**
     * 解析并验证工厂阶段所需的父类、Skeleton、注册节点类、动画资源和混合模式。
     * 函数首先调用权威 IR Validator，再规范化私有副本；只有游戏线程可调用，因为软路径解析会加载 UObject。
     *
     * @param Blueprint 调用方提供的只读 IR。
     * @param OutData 接收规范化 IR 与已加载对象，失败时内容不可用于物化。
     * @param OutDiagnostics 接收 Validator 与工厂预检诊断；函数调用期间必须由当前线程独占。
     * @return 所有检查通过且没有 Error 时返回 true，否则返回 false。
     */
    bool PrepareBuild(
        const FSekiroAnimBlueprintIR& Blueprint,
        FPreflightData& OutData,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
    {
        OutDiagnostics.Reset();
        if (!IsInGameThread())
        {
            AddError(
                OutDiagnostics,
                WrongThread,
                TEXT("AnimBlueprint factory must run on the game thread."),
                Blueprint.SourceModule,
                Blueprint.SourceLocation);
            return false;
        }

        if (!USekiroAnimGraphIRLibrary::Validate(Blueprint, OutDiagnostics)) return false;

        OutData.Blueprint = Blueprint;
        USekiroAnimGraphIRLibrary::Canonicalize(OutData.Blueprint);

        if (OutData.Blueprint.Layers.Num() != 1)
        {
            AddError(
                OutDiagnostics,
                UnsupportedLayerCount,
                FString::Printf(
                    TEXT("Native NodeFactory currently supports exactly one main AnimGraph layer; received %d."),
                    OutData.Blueprint.Layers.Num()),
                OutData.Blueprint.SourceModule,
                OutData.Blueprint.SourceLocation);
            return false;
        }

        OutData.ParentClass = OutData.Blueprint.ParentAnimInstanceClass.TryLoadClass<UAnimInstance>();
        if (OutData.ParentClass == nullptr)
        {
            AddError(
                OutDiagnostics,
                ParentClassLoadFailed,
                FString::Printf(
                    TEXT("Failed to load ParentAnimInstanceClass '%s'."),
                    *OutData.Blueprint.ParentAnimInstanceClass.ToString()),
                TEXT("Blueprint.ParentAnimInstanceClass"),
                OutData.Blueprint.SourceLocation);
        }
        else if (!OutData.ParentClass->IsChildOf(UAnimInstance::StaticClass()))
        {
            AddError(
                OutDiagnostics,
                ParentClassTypeMismatch,
                FString::Printf(
                    TEXT("Parent class '%s' is not derived from UAnimInstance."),
                    *OutData.ParentClass->GetPathName()),
                TEXT("Blueprint.ParentAnimInstanceClass"),
                OutData.Blueprint.SourceLocation);
        }
        else if (Cast<UAnimBlueprintGeneratedClass>(OutData.ParentClass) != nullptr)
        {
            AddError(
                OutDiagnostics,
                UnsupportedParentBlueprintClass,
                TEXT("This stage only supports native UAnimInstance parent classes with one owned main AnimGraph."),
                TEXT("Blueprint.ParentAnimInstanceClass"),
                OutData.Blueprint.SourceLocation);
        }

        UObject* SkeletonObject = OutData.Blueprint.TargetSkeleton.ResolveObject();
        if (SkeletonObject == nullptr) SkeletonObject = OutData.Blueprint.TargetSkeleton.TryLoad();
        if (SkeletonObject == nullptr)
        {
            AddError(
                OutDiagnostics,
                TargetSkeletonLoadFailed,
                FString::Printf(
                    TEXT("Failed to load TargetSkeleton '%s'."),
                    *OutData.Blueprint.TargetSkeleton.ToString()),
                TEXT("Blueprint.TargetSkeleton"),
                OutData.Blueprint.SourceLocation);
        }
        else
        {
            OutData.TargetSkeleton = Cast<USkeleton>(SkeletonObject);
            if (OutData.TargetSkeleton == nullptr)
            {
                AddError(
                    OutDiagnostics,
                    TargetSkeletonTypeMismatch,
                    FString::Printf(
                        TEXT("TargetSkeleton path resolved to '%s', not USkeleton."),
                        *SkeletonObject->GetClass()->GetPathName()),
                    TEXT("Blueprint.TargetSkeleton"),
                    OutData.Blueprint.SourceLocation);
            }
        }

        for (const FSekiroAnimIRVariable& Variable : OutData.Blueprint.Variables)
        {
            if (Variable.DataType != TEXT("Enum")) continue;
            UEnum* EnumType = Cast<UEnum>(Variable.TypeObjectPath.TryLoad());
            if (EnumType == nullptr)
            {
                AddError(OutDiagnostics, EnumTypeLoadFailed,
                    FString::Printf(TEXT("Failed to load Enum variable type '%s'."), *Variable.TypeObjectPath.ToString()),
                    Variable.Name.ToString(), Variable.SourceLocation);
                continue;
            }
            OutData.VariableEnums.Add(Variable.Name, EnumType);
        }

        const FSekiroAnimIRLayer& Layer = OutData.Blueprint.Layers[0];
        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                if (!OutData.NodeClasses.Contains(Node.NodeType))
                {
                    const FSekiroAnimIRNodeContract* Contract = FSekiroAnimGraphNodeRegistry::Find(Node.NodeType);
                    UClass* NodeClass = Contract != nullptr
                        ? Contract->EditorNodeClassPath.TryLoadClass<UEdGraphNode>()
                        : nullptr;
                    if (NodeClass == nullptr)
                    {
                        AddError(
                            OutDiagnostics,
                            NodeClassLoadFailed,
                            FString::Printf(
                                TEXT("Failed to load editor node class for registered NodeType '%s'."),
                                *Node.NodeType.ToString()),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else if (!IsSupportedNodeClass(NodeClass))
                    {
                        AddError(
                            OutDiagnostics,
                            UnsupportedRegisteredNodeClass,
                            FString::Printf(
                                TEXT("Registered editor node class '%s' is not supported by this NodeFactory stage."),
                                *NodeClass->GetPathName()),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        OutData.NodeClasses.Add(Node.NodeType, NodeClass);
                    }
                }

                UClass* const* RegisteredClass = OutData.NodeClasses.Find(Node.NodeType);
                if (RegisteredClass != nullptr && *RegisteredClass == UAnimGraphNode_BlendListByEnum::StaticClass())
                {
                    const FSekiroAnimIRProperty* EnumTypeProperty = FindProperty(Node, TEXT("EnumType"));
                    UEnum* EnumType = EnumTypeProperty != nullptr
                        ? Cast<UEnum>(EnumTypeProperty->Value.SoftObjectPathValue.TryLoad())
                        : nullptr;
                    if (EnumType == nullptr)
                    {
                        AddError(OutDiagnostics, EnumTypeLoadFailed,
                            TEXT("BlendListByEnum failed to load its EnumType."), Node.Id, Node.SourceLocation);
                    }
                    else
                    {
                        OutData.NodeEnums.Add(Node.Id, EnumType);
                    }
                }
                if (RegisteredClass != nullptr && *RegisteredClass == UAnimGraphNode_FootPlacement::StaticClass())
                {
                    const FSekiroAnimIRProperty* LegDefinitionsProperty =
                        FindProperty(Node, TEXT("LegDefinitions"));
                    TArray<FFootPlacemenLegDefinition> LegDefinitions;
                    FString ParseError;
                    if (LegDefinitionsProperty == nullptr
                        || !ParseFootPlacementLegDefinitions(
                            LegDefinitionsProperty->Value.StringValue,
                            LegDefinitions,
                            ParseError))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidFootPlacementLegDefinitions,
                            ParseError.IsEmpty()
                                ? TEXT("FootPlacement requires valid LegDefinitions.")
                                : ParseError,
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        OutData.FootPlacementLegDefinitions.Add(Node.Id, MoveTemp(LegDefinitions));
                    }

                    const FSekiroAnimIRProperty* PlantSpeedModeProperty =
                        FindProperty(Node, TEXT("PlantSpeedMode"));
                    if (PlantSpeedModeProperty != nullptr
                        && PlantSpeedModeProperty->Value.NameValue != TEXT("Graph")
                        && PlantSpeedModeProperty->Value.NameValue != TEXT("Manual"))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidFootPlacementPlantSpeedMode,
                            FString::Printf(
                                TEXT("FootPlacement PlantSpeedMode '%s' must be Graph or Manual."),
                                *PlantSpeedModeProperty->Value.NameValue.ToString()),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FSekiroAnimIRProperty* PlantLockTypeProperty =
                        FindProperty(Node, TEXT("PlantLockType"));
                    if (PlantLockTypeProperty != nullptr)
                    {
                        EFootPlacementLockType PlantLockType = EFootPlacementLockType::PivotAroundBall;
                        if (!ParseFootPlacementLockType(
                            PlantLockTypeProperty->Value.NameValue,
                            PlantLockType))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidFootPlacementPlantLockType,
                                FString::Printf(
                                    TEXT("FootPlacement PlantLockType '%s' must be Unlocked, PivotAroundBall, PivotAroundAnkle, or LockRotation."),
                                    *PlantLockTypeProperty->Value.NameValue.ToString()),
                                Node.Id,
                                Node.SourceLocation);
                        }
                        else
                        {
                            OutData.FootPlacementLockTypes.Add(Node.Id, PlantLockType);
                        }
                    }
                }
                if (RegisteredClass != nullptr && *RegisteredClass == UAnimGraphNode_LegIK::StaticClass())
                {
                    const FSekiroAnimIRProperty* LegDefinitionsProperty =
                        FindProperty(Node, TEXT("LegDefinitions"));
                    TArray<FAnimLegIKDefinition> LegDefinitions;
                    FString ParseError;
                    if (LegDefinitionsProperty == nullptr
                        || !ParseLegIKDefinitions(
                            LegDefinitionsProperty->Value.StringValue,
                            LegDefinitions,
                            ParseError))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidLegIKDefinitions,
                            ParseError.IsEmpty() ? TEXT("LegIK requires valid LegDefinitions.") : ParseError,
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        OutData.LegIKDefinitions.Add(Node.Id, MoveTemp(LegDefinitions));
                    }
                }
                if (RegisteredClass != nullptr
                    && *RegisteredClass == UAnimGraphNode_LayeredBoneBlend::StaticClass())
                {
                    const FSekiroAnimIRProperty* BranchFiltersProperty =
                        FindProperty(Node, TEXT("BranchFilters"));
                    TArray<FBranchFilter> BranchFilters;
                    FString ParseError;
                    if (BranchFiltersProperty == nullptr
                        || !ParseLayeredBlendBranchFilters(
                            BranchFiltersProperty->Value.StringValue,
                            BranchFilters,
                            ParseError))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidLayeredBlendBranchFilters,
                            ParseError.IsEmpty()
                                ? TEXT("LayeredBlendPerBone requires valid BranchFilters.")
                                : ParseError,
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        OutData.LayeredBlendBranchFilters.Add(Node.Id, MoveTemp(BranchFilters));
                    }

                    const FSekiroAnimIRProperty* CurveOptionProperty =
                        FindProperty(Node, TEXT("CurveBlendOption"));
                    if (CurveOptionProperty != nullptr)
                    {
                        ECurveBlendOption::Type CurveOption = ECurveBlendOption::Override;
                        if (!ParseLayeredBlendCurveOption(
                            CurveOptionProperty->Value.NameValue,
                            CurveOption))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidLayeredBlendCurveOption,
                                FString::Printf(
                                    TEXT("LayeredBlendPerBone CurveBlendOption '%s' is not supported."),
                                    *CurveOptionProperty->Value.NameValue.ToString()),
                                Node.Id,
                                Node.SourceLocation);
                        }
                    }
                }
                if (RegisteredClass == nullptr
                    || *RegisteredClass != UAnimGraphNode_SequencePlayer::StaticClass())
                {
                    continue;
                }

                const FSekiroAnimIRProperty* SequenceProperty = FindProperty(Node, TEXT("Sequence"));
                if (SequenceProperty == nullptr) continue;

                const FSoftObjectPath& SequencePath = SequenceProperty->Value.SoftObjectPathValue;
                UObject* SequenceObject = SequencePath.ResolveObject();
                if (SequenceObject == nullptr) SequenceObject = SequencePath.TryLoad();
                if (SequenceObject == nullptr)
                {
                    AddError(
                        OutDiagnostics,
                        AnimationAssetLoadFailed,
                        FString::Printf(TEXT("Failed to load Sequence '%s'."), *SequencePath.ToString()),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(SequenceObject);
                if (Sequence == nullptr)
                {
                    AddError(
                        OutDiagnostics,
                        AnimationAssetTypeMismatch,
                        FString::Printf(
                            TEXT("Sequence path resolved to '%s', not UAnimSequenceBase."),
                            *SequenceObject->GetClass()->GetPathName()),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                USkeleton* SequenceSkeleton = Sequence->GetSkeleton();
                if (OutData.TargetSkeleton != nullptr
                    && (SequenceSkeleton == nullptr
                        || !OutData.TargetSkeleton->IsCompatibleForEditor(SequenceSkeleton)))
                {
                    AddError(
                        OutDiagnostics,
                        AnimationSkeletonMismatch,
                        FString::Printf(
                            TEXT("Sequence '%s' is not compatible with TargetSkeleton '%s'."),
                            *Sequence->GetPathName(),
                            *OutData.TargetSkeleton->GetPathName()),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                OutData.SequenceAssets.Add(Node.Id, Sequence);
            }

            if (Graph.GraphType != SekiroAnimGraphIRNames::StateMachineGraph) continue;
            for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                if (Transition.Settings.BlendMode != TEXT("Linear"))
                {
                    AddError(
                        OutDiagnostics,
                        UnsupportedTransitionBlendMode,
                        FString::Printf(
                            TEXT("Transition blend mode '%s' is not supported; this stage supports Linear only."),
                            *Transition.Settings.BlendMode.ToString()),
                        Transition.Id,
                        Transition.SourceLocation);
                }
            }
        }

        return !HasErrors(OutDiagnostics);
    }

    /**
     * 通过 FGraphNodeCreator 完成 UE 节点标准构造，并在 Finalize 后覆盖确定性 NodeGuid。
     * 本模板只能在游戏线程调用，Graph 必须属于正在构建且尚未并发访问的 AnimBlueprint。
     *
     * @param Graph 新节点所属的原生 Graph。
     * @param StableId 节点的 IR 稳定 ID。
     * @param PositionX 编辑器布局横坐标。
     * @param PositionY 编辑器布局纵坐标。
     * @return 已执行 PostPlacedNewNode 与 AllocateDefaultPins 的节点；UE 创建失败时返回 nullptr。
     */
    template <typename NodeType>
    NodeType* CreateNativeNode(
        UEdGraph& Graph,
        const FString& StableId,
        const int32 PositionX,
        const int32 PositionY)
    {
        FGraphNodeCreator<NodeType> NodeCreator(Graph);
        NodeType* Node = NodeCreator.CreateNode(false);
        NodeCreator.Finalize();
        if (Node != nullptr)
        {
            Node->NodeGuid = MakeStableGuid(TEXT("Node"), StableId);
            Node->NodePosX = PositionX;
            Node->NodePosY = PositionY;
        }
        return Node;
    }

    /**
     * 通过 FGraphNodeCreator 创建真正的 Blueprint override Event，并在分配 Pin 前绑定原生函数签名。
     * 函数只能在游戏线程调用；Function 必须来自目标 AnimBlueprint 的父类继承链。
     *
     * @param Graph 新 Event 所属的 EventGraph。
     * @param StableId 用于生成确定性 NodeGuid 的稳定字符串。
     * @param Function Event 要 override 的原生 Blueprint event 函数，不能为空。
     * @param PositionX 编辑器布局横坐标。
     * @param PositionY 编辑器布局纵坐标。
     * @return 已完成 PostPlacedNewNode 与 Pin 分配的 override Event；参数非法或创建失败时返回 nullptr。
     */
    UK2Node_Event* CreateOverrideEventNode(
        UEdGraph& Graph,
        const FString& StableId,
        UFunction* Function,
        const int32 PositionX,
        const int32 PositionY)
    {
        if (Function == nullptr) return nullptr;

        FGraphNodeCreator<UK2Node_Event> NodeCreator(Graph);
        UK2Node_Event* Node = NodeCreator.CreateNode(false);
        if (Node == nullptr) return nullptr;
        Node->EventReference.SetExternalMember(Function->GetFName(), Function->GetOwnerClass());
        Node->bOverrideFunction = true;
        NodeCreator.Finalize();
        Node->NodeGuid = MakeStableGuid(TEXT("K2Event"), StableId);
        Node->NodePosX = PositionX;
        Node->NodePosY = PositionY;
        return Node;
    }

    /**
     * 通过 FGraphNodeCreator 创建原生 UFunction 调用，并在分配 Pin 前写入函数引用。
     * 函数只能在游戏线程调用；不会连接 Pin，也不会设置参数默认值。
     *
     * @param Graph 新调用节点所属的 K2 Graph。
     * @param StableId 用于生成确定性 NodeGuid 的稳定字符串。
     * @param Function 调用节点绑定的原生 UFunction，不能为空。
     * @param PositionX 编辑器布局横坐标。
     * @param PositionY 编辑器布局纵坐标。
     * @return 已完成 PostPlacedNewNode 与 Pin 分配的调用节点；参数非法或创建失败时返回 nullptr。
     */
    UK2Node_CallFunction* CreateCallFunctionNode(
        UEdGraph& Graph,
        const FString& StableId,
        UFunction* Function,
        const int32 PositionX,
        const int32 PositionY)
    {
        if (Function == nullptr) return nullptr;

        FGraphNodeCreator<UK2Node_CallFunction> NodeCreator(Graph);
        UK2Node_CallFunction* Node = NodeCreator.CreateNode(false);
        if (Node == nullptr) return nullptr;
        Node->SetFromFunction(Function);
        NodeCreator.Finalize();
        Node->NodeGuid = MakeStableGuid(TEXT("K2Call"), StableId);
        Node->NodePosX = PositionX;
        Node->NodePosY = PositionY;
        return Node;
    }

    /** 递归将规范 IR 物化为原生动画 Graph；实例仅在游戏线程、单次构建中使用。 */
    class FNativeAnimBlueprintBuilder
    {
    public:
        /**
         * 建立只读 IR 索引并绑定目标 AnimBlueprint；构造函数不创建节点。
         * 必须在游戏线程调用，且 Blueprint 与 Preflight 的生命周期需覆盖 Build。
         *
         * @param InPreflight 已完成软路径加载与规范化的预检数据。
         * @param InBlueprint 由 UAnimBlueprintFactory 新建的目标蓝图。
         * @param InDiagnostics 接收物化阶段错误的调用方数组。
         */
        FNativeAnimBlueprintBuilder(
            const FPreflightData& InPreflight,
            UAnimBlueprint& InBlueprint,
            TArray<FSekiroAnimIRDiagnostic>& InDiagnostics)
            : Preflight(InPreflight)
            , Blueprint(InBlueprint)
            , Diagnostics(InDiagnostics)
            , Layer(InPreflight.Blueprint.Layers[0])
        {
            for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
            {
                GraphsById.Add(Graph.Id, &Graph);
                for (const FSekiroAnimIRNode& Node : Graph.Nodes) NodesById.Add(Node.Id, &Node);
            }
        }

        /**
         * 定位工厂创建的主 AnimGraph，并从 Layer.RootGraphId 开始递归物化全部所有权树。
         * 必须在游戏线程调用；失败会保留诊断并停止后续连接。
         *
         * @return 所有 Graph、Node 与 Link 成功创建时返回 true。
         */
        bool Build()
        {
            UAnimationGraph* MainGraph = nullptr;
            for (UEdGraph* FunctionGraph : Blueprint.FunctionGraphs)
            {
                UAnimationGraph* Candidate = Cast<UAnimationGraph>(FunctionGraph);
                if (Candidate != nullptr && !Candidate->IsA<UAnimationStateGraph>())
                {
                    MainGraph = Candidate;
                    break;
                }
            }

            if (MainGraph == nullptr)
            {
                AddError(
                    Diagnostics,
                    MainAnimGraphNotFound,
                    TEXT("UAnimBlueprintFactory did not create an owned main AnimGraph."),
                    Layer.RootGraphId,
                    Layer.SourceLocation);
                return false;
            }

            const FSekiroAnimIRGraph* RootGraph = FindGraph(Layer.RootGraphId);
            if (RootGraph == nullptr)
            {
                AddError(
                    Diagnostics,
                    NativeGraphMissing,
                    TEXT("Layer root Graph could not be found after validation."),
                    Layer.RootGraphId,
                    Layer.SourceLocation);
                return false;
            }

            if (!BuildMemberVariables()) return false;
            if (!BuildPoseGraph(*RootGraph, *MainGraph)) return false;
            return BuildEventGraph();
        }

    private:
        /**
         * 将 IR Variable 生成成 AnimBlueprint Member Variable，并写入类型化默认值与 Transient 标记。
         * 必须在游戏线程、任何 Graph 编译前调用；Enum 对象来自 Preflight，不在此函数加载资产。
         *
         * @return 全部变量成功加入 Blueprint 时返回 true；名称冲突或类型物化失败时追加诊断并返回 false。
         */
        bool BuildMemberVariables()
        {
            for (const FSekiroAnimIRVariable& Variable : Preflight.Blueprint.Variables)
            {
                FEdGraphPinType PinType;
                FString DefaultValue;
                if (Variable.DataType == TEXT("Bool"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
                    DefaultValue = Variable.DefaultValue.BoolValue ? TEXT("true") : TEXT("false");
                }
                else if (Variable.DataType == TEXT("Float"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
                    PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
                    DefaultValue = FString::SanitizeFloat(Variable.DefaultValue.FloatValue);
                }
                else if (Variable.DataType == TEXT("Byte"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
                    DefaultValue = FString::FromInt(static_cast<int32>(Variable.DefaultValue.IntegerValue));
                }
                else
                {
                    UEnum* const* EnumType = Preflight.VariableEnums.Find(Variable.Name);
                    if (EnumType == nullptr) return false;
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
                    PinType.PinSubCategoryObject = *EnumType;
                    DefaultValue = (*EnumType)->GetNameStringByValue(Variable.DefaultValue.IntegerValue);
                }

                if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Variable.Name, PinType, DefaultValue))
                {
                    AddError(Diagnostics, MemberVariableCreationFailed,
                        FString::Printf(TEXT("Failed to create AnimBlueprint variable '%s'."), *Variable.Name.ToString()),
                        Variable.Name.ToString(), Variable.SourceLocation);
                    return false;
                }
                for (FBPVariableDescription& Description : Blueprint.NewVariables)
                {
                    if (Description.VarName != Variable.Name) continue;
                    Description.PropertyFlags |= CPF_BlueprintVisible;
                    Description.PropertyFlags &= ~(CPF_BlueprintReadOnly | CPF_DisableEditOnInstance);
                    if (Variable.bTransient) Description.PropertyFlags |= CPF_Transient;
                    break;
                }
            }
            return true;
        }

        /**
         * 按稳定 ID 查询当前 Layer 的 IR Graph。
         * 本函数只读访问构造期索引，可在当前构建线程调用。
         *
         * @param GraphId 待查找的全局稳定 Graph ID。
         * @return 找到时返回 IR 数组元素指针，否则返回 nullptr。
         */
        const FSekiroAnimIRGraph* FindGraph(const FString& GraphId) const
        {
            const FSekiroAnimIRGraph* const* Graph = GraphsById.Find(GraphId);
            return Graph != nullptr ? *Graph : nullptr;
        }

        /**
         * 在工厂创建的 EventGraph 中生成 BlueprintUpdateAnimation override 及唯一 Lua 更新桥接。
         * Transition Rule 由各自的 Rule Graph 按需执行，本函数不枚举或预计算任何规则。
         * 必须在游戏线程调用，目标蓝图是本次 Builder 独占的新资产。
         *
         * @return override Event、Self、Lua 更新节点、默认参数和连接创建成功时返回 true。
         */
        bool BuildEventGraph()
        {
            UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
            if (EventGraph == nullptr)
            {
                AddError(
                    Diagnostics,
                    EventGraphNotFound,
                    TEXT("UAnimBlueprintFactory did not create an EventGraph."),
                    Preflight.Blueprint.SourceModule,
                    Preflight.Blueprint.SourceLocation);
                return false;
            }

            UFunction* UpdateFunction = UAnimInstance::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(UAnimInstance, BlueprintUpdateAnimation));
            UFunction* LuaUpdateFunction = USekiroLuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(USekiroLuaTransitionRuntimeLibrary, EvaluateBlueprintUpdateAnimation));
            if (UpdateFunction == nullptr || LuaUpdateFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2FunctionNotFound,
                    TEXT("Required BlueprintUpdateAnimation function or Lua update bridge was not found."),
                    Preflight.Blueprint.SourceModule,
                    Preflight.Blueprint.SourceLocation);
                return false;
            }

            UK2Node_Event* EventNode = nullptr;
            for (UEdGraphNode* ExistingNode : EventGraph->Nodes)
            {
                UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode);
                if (ExistingEvent != nullptr
                    && ExistingEvent->bOverrideFunction
                    && ExistingEvent->GetFunctionName() == UpdateFunction->GetFName())
                {
                    EventNode = ExistingEvent;
                    break;
                }
            }
            if (EventNode == nullptr)
            {
                EventNode = CreateOverrideEventNode(
                    *EventGraph,
                    TEXT("BlueprintUpdateAnimation"),
                    UpdateFunction,
                    0,
                    0);
            }
            else
            {
                EventNode->NodeGuid = MakeStableGuid(TEXT("K2Event"), TEXT("BlueprintUpdateAnimation"));
                EventNode->NodePosX = 0;
                EventNode->NodePosY = 0;
            }
            UK2Node_Self* SelfNode = CreateNativeNode<UK2Node_Self>(
                *EventGraph,
                TEXT("EventGraph.Self"),
                0,
                180);
            UEdGraphPin* SelfPin = SelfNode != nullptr
                ? SelfNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Output)
                : nullptr;
            UEdGraphPin* EventExecPin = EventNode != nullptr
                ? EventNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)
                : nullptr;
            if (EventNode == nullptr || SelfPin == nullptr || EventExecPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("BlueprintUpdateAnimation Event or Self node did not expose required K2 Pins."),
                    Preflight.Blueprint.SourceModule,
                    Preflight.Blueprint.SourceLocation);
                return false;
            }

            const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
            UK2Node_CallFunction* LuaUpdateNode = CreateCallFunctionNode(
                *EventGraph, TEXT("EvaluateBlueprintUpdateAnimation"), LuaUpdateFunction, 320, 0);
            UEdGraphPin* LuaUpdateExec = LuaUpdateNode != nullptr ? LuaUpdateNode->GetExecPin() : nullptr;
            UEdGraphPin* LuaUpdateInstance = LuaUpdateNode != nullptr ? LuaUpdateNode->FindPin(TEXT("AnimInstance"), EGPD_Input) : nullptr;
            UEdGraphPin* LuaUpdateModule = LuaUpdateNode != nullptr ? LuaUpdateNode->FindPin(TEXT("LuaModuleName"), EGPD_Input) : nullptr;
            UEdGraphPin* LuaUpdateDelta = LuaUpdateNode != nullptr ? LuaUpdateNode->FindPin(TEXT("DeltaSeconds"), EGPD_Input) : nullptr;
            UEdGraphPin* EventDelta = EventNode->FindPin(TEXT("DeltaTimeX"), EGPD_Output);
            if (EventDelta == nullptr) EventDelta = EventNode->FindPin(TEXT("DeltaSeconds"), EGPD_Output);
            if (LuaUpdateExec == nullptr || LuaUpdateInstance == nullptr
                || LuaUpdateModule == nullptr || LuaUpdateDelta == nullptr || EventDelta == nullptr)
            {
                AddError(Diagnostics, K2PinNotFound,
                    TEXT("Lua BlueprintUpdateAnimation bridge did not expose required Pins."),
                    Preflight.Blueprint.SourceModule, Preflight.Blueprint.SourceLocation);
                return false;
            }
            K2Schema->TrySetDefaultValue(*LuaUpdateModule, Preflight.Blueprint.SourceModule, false);
            if (!K2Schema->TryCreateConnection(EventExecPin, LuaUpdateExec)
                || !K2Schema->TryCreateConnection(SelfPin, LuaUpdateInstance)
                || !K2Schema->TryCreateConnection(EventDelta, LuaUpdateDelta))
            {
                AddError(Diagnostics, K2ConnectionFailed,
                    TEXT("Failed to connect Lua BlueprintUpdateAnimation bridge."),
                    Preflight.Blueprint.SourceModule, Preflight.Blueprint.SourceLocation);
                return false;
            }
            return true;
        }

        /** 把 Graph.Layout 中的逻辑分区单元格转换为确定性编辑器像素坐标。 */
        TMap<FString, FIntPoint> BuildExplicitLayoutPositions(const FSekiroAnimIRGraph& Graph) const
        {
            constexpr int32 RegionWidth = 2400;
            constexpr int32 RegionHeight = 1600;
            TMap<FString, FIntPoint> Positions;
            for (const FSekiroAnimIRLayoutGrid& Grid : Graph.Layout.Grids)
            {
                const int32 OriginX = Grid.RegionColumn * RegionWidth;
                const int32 OriginY = Grid.RegionRow * RegionHeight;
                for (const FSekiroAnimIRLayoutItem& Item : Grid.Items)
                {
                    Positions.Add(Item.ElementId, FIntPoint(
                        OriginX + Item.Column * Grid.CellWidth,
                        OriginY + Item.Row * Grid.CellHeight));
                }
            }
            return Positions;
        }

        /** 根据风格把拓扑主轴层级和同层序号转换为画布坐标。 */
        FIntPoint MakeFlowPosition(
            const ESekiroAnimIRLayoutStyle Style,
            const int32 Depth,
            const int32 SecondaryIndex,
            const int32 ElementIndex) const
        {
            constexpr int32 PrimarySpacing = 400;
            constexpr int32 SecondarySpacing = 240;
            if (Style == ESekiroAnimIRLayoutStyle::CompactGrid)
            {
                return FIntPoint((ElementIndex % 4) * PrimarySpacing, (ElementIndex / 4) * SecondarySpacing);
            }
            if (Style == ESekiroAnimIRLayoutStyle::RightToLeft)
            {
                return FIntPoint(Depth * PrimarySpacing, SecondaryIndex * SecondarySpacing);
            }
            if (Style == ESekiroAnimIRLayoutStyle::TopToBottom)
            {
                return FIntPoint(SecondaryIndex * PrimarySpacing, Depth * SecondarySpacing);
            }
            if (Style == ESekiroAnimIRLayoutStyle::BottomToTop)
            {
                return FIntPoint(SecondaryIndex * PrimarySpacing, -Depth * SecondarySpacing);
            }
            return FIntPoint(-Depth * PrimarySpacing, SecondaryIndex * SecondarySpacing);
        }

        /**
         * 从 Result 反向领取每个有效输入节点，并递归计算“节点 + 直接输入子块”的层次布局。
         * 每组直接输入的根节点位于父节点左侧同一列，子块按高度纵向堆叠；共享输入只归首个稳定父块所有。
         * 未连接节点不计入有效树尺寸，而是在主树下方单独紧凑排列；显式 Grid 坐标最终覆盖自动结果。
         *
         * @param Graph 待排版的 Pose 或 StatePose IR Graph。
         * @param ExplicitPositions Lua Grid 提供的固定画布坐标。
         * @return 无返回值；函数直接修改 NativeNodes 中当前 Graph 对应节点的位置。
         */
        void ApplyHierarchicalPoseGraphLayout(
            const FSekiroAnimIRGraph& Graph,
            const TMap<FString, FIntPoint>& ExplicitPositions)
        {
            constexpr int32 HorizontalSpacing = 460;
            constexpr int32 NodeBlockHeight = 140;
            constexpr int32 SiblingSpacing = 40;
            constexpr int32 DisconnectedHorizontalSpacing = 360;
            constexpr int32 DisconnectedVerticalSpacing = 200;
            constexpr int32 DisconnectedSectionSpacing = 320;

            TArray<const FSekiroAnimIRNode*> OrderedNodes;
            TSet<FString> GraphNodeIds;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                OrderedNodes.Add(&Node);
                GraphNodeIds.Add(Node.Id);
            }
            OrderedNodes.Sort([](const FSekiroAnimIRNode& Left, const FSekiroAnimIRNode& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            TMap<FString, TArray<const FSekiroAnimIRLink*>> IncomingLinks;
            for (const FSekiroAnimIRLink& Link : Graph.Links)
            {
                if (!GraphNodeIds.Contains(Link.Source.NodeId)
                    || !GraphNodeIds.Contains(Link.Target.NodeId)) continue;
                IncomingLinks.FindOrAdd(Link.Target.NodeId).Add(&Link);
            }
            for (TPair<FString, TArray<const FSekiroAnimIRLink*>>& Pair : IncomingLinks)
            {
                Pair.Value.Sort([](const FSekiroAnimIRLink& Left, const FSekiroAnimIRLink& Right)
                {
                    return Left.DeclarationOrder < Right.DeclarationOrder;
                });
            }

            TMap<FString, TArray<FString>> OwnedInputs;
            TSet<FString> EffectiveNodeIds;
            EffectiveNodeIds.Add(Graph.RootNodeId);
            TFunction<void(const FString&)> ClaimInputBlocks;
            ClaimInputBlocks = [&](const FString& TargetNodeId)
            {
                const TArray<const FSekiroAnimIRLink*>* Links = IncomingLinks.Find(TargetNodeId);
                if (Links == nullptr) return;
                TSet<FString> DirectInputs;
                for (const FSekiroAnimIRLink* Link : *Links)
                {
                    if (Link == nullptr
                        || Link->Source.NodeId == TargetNodeId
                        || DirectInputs.Contains(Link->Source.NodeId)) continue;
                    DirectInputs.Add(Link->Source.NodeId);
                    if (EffectiveNodeIds.Contains(Link->Source.NodeId)) continue;
                    EffectiveNodeIds.Add(Link->Source.NodeId);
                    OwnedInputs.FindOrAdd(TargetNodeId).Add(Link->Source.NodeId);
                    ClaimInputBlocks(Link->Source.NodeId);
                }
            };
            ClaimInputBlocks(Graph.RootNodeId);

            TMap<FString, int32> BlockHeights;
            TFunction<int32(const FString&)> MeasureBlock;
            MeasureBlock = [&](const FString& NodeId)
            {
                if (const int32* CachedHeight = BlockHeights.Find(NodeId)) return *CachedHeight;
                const TArray<FString>* Inputs = OwnedInputs.Find(NodeId);
                int32 Height = NodeBlockHeight;
                if (Inputs != nullptr && Inputs->Num() > 0)
                {
                    Height = 0;
                    for (const FString& InputId : *Inputs) Height += MeasureBlock(InputId);
                    Height += (Inputs->Num() - 1) * SiblingSpacing;
                    Height = FMath::Max(Height, NodeBlockHeight);
                }
                BlockHeights.Add(NodeId, Height);
                return Height;
            };

            TMap<FString, FIntPoint> AutomaticPositions;
            TFunction<void(const FString&, int32, int32)> PlaceBlock;
            PlaceBlock = [&](const FString& NodeId, const int32 NodeX, const int32 BlockCenterY)
            {
                AutomaticPositions.Add(NodeId, FIntPoint(NodeX, BlockCenterY - NodeBlockHeight / 2));
                const TArray<FString>* Inputs = OwnedInputs.Find(NodeId);
                if (Inputs == nullptr || Inputs->Num() == 0) return;

                const int32 TotalHeight = MeasureBlock(NodeId);
                int32 CursorY = BlockCenterY - TotalHeight / 2;
                for (const FString& InputId : *Inputs)
                {
                    const int32 InputHeight = MeasureBlock(InputId);
                    PlaceBlock(InputId, NodeX - HorizontalSpacing, CursorY + InputHeight / 2);
                    CursorY += InputHeight + SiblingSpacing;
                }
            };

            const FIntPoint* ExplicitRoot = ExplicitPositions.Find(Graph.RootNodeId);
            const int32 RootX = ExplicitRoot != nullptr ? ExplicitRoot->X : 0;
            const int32 RootCenterY = ExplicitRoot != nullptr
                ? ExplicitRoot->Y + NodeBlockHeight / 2
                : 0;
            MeasureBlock(Graph.RootNodeId);
            PlaceBlock(Graph.RootNodeId, RootX, RootCenterY);

            int32 MinimumTreeX = 0;
            int32 MaximumTreeY = 0;
            bool bHasTreePosition = false;
            for (const TPair<FString, FIntPoint>& Pair : AutomaticPositions)
            {
                MinimumTreeX = bHasTreePosition ? FMath::Min(MinimumTreeX, Pair.Value.X) : Pair.Value.X;
                MaximumTreeY = bHasTreePosition ? FMath::Max(MaximumTreeY, Pair.Value.Y) : Pair.Value.Y;
                bHasTreePosition = true;
            }

            TArray<const FSekiroAnimIRNode*> DisconnectedNodes;
            for (const FSekiroAnimIRNode* Node : OrderedNodes)
            {
                if (!EffectiveNodeIds.Contains(Node->Id) && !ExplicitPositions.Contains(Node->Id))
                {
                    DisconnectedNodes.Add(Node);
                }
            }
            const int32 DisconnectedColumns = FMath::Max(
                1,
                FMath::CeilToInt(FMath::Sqrt(static_cast<float>(DisconnectedNodes.Num()))));
            const int32 DisconnectedOriginY = MaximumTreeY + DisconnectedSectionSpacing;
            for (int32 Index = 0; Index < DisconnectedNodes.Num(); ++Index)
            {
                AutomaticPositions.Add(
                    DisconnectedNodes[Index]->Id,
                    FIntPoint(
                        MinimumTreeX + (Index % DisconnectedColumns) * DisconnectedHorizontalSpacing,
                        DisconnectedOriginY + (Index / DisconnectedColumns) * DisconnectedVerticalSpacing));
            }

            for (const FSekiroAnimIRNode* Node : OrderedNodes)
            {
                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node->Id);
                if (NativeNode == nullptr || *NativeNode == nullptr) continue;
                const FIntPoint* Position = ExplicitPositions.Find(Node->Id);
                if (Position == nullptr) Position = AutomaticPositions.Find(Node->Id);
                if (Position == nullptr) continue;
                (*NativeNode)->NodePosX = Position->X;
                (*NativeNode)->NodePosY = Position->Y;
            }
        }

        /**
         * 将显式 Grid 位置应用为固定锚点，并按 Pose Link 到 Result 的反向深度排列其余节点。
         * Radial 使用反向深度作为半径分层；未连接节点按声明顺序落到额外深度。
         */
        void ApplyPoseGraphLayout(const FSekiroAnimIRGraph& Graph)
        {
            ESekiroAnimIRLayoutStyle Style = Graph.Layout.Style;
            const TMap<FString, FIntPoint> ExplicitPositions = BuildExplicitLayoutPositions(Graph);
            if (Style == ESekiroAnimIRLayoutStyle::Auto
                || Style == ESekiroAnimIRLayoutStyle::HierarchicalBlocks)
            {
                ApplyHierarchicalPoseGraphLayout(Graph, ExplicitPositions);
                return;
            }

            TMap<FString, int32> Depths;
            Depths.Add(Graph.RootNodeId, 0);
            for (int32 Pass = 0; Pass < Graph.Nodes.Num(); ++Pass)
            {
                bool bChanged = false;
                for (const FSekiroAnimIRLink& Link : Graph.Links)
                {
                    const int32* TargetDepth = Depths.Find(Link.Target.NodeId);
                    if (TargetDepth == nullptr) continue;
                    int32& SourceDepth = Depths.FindOrAdd(Link.Source.NodeId, *TargetDepth + 1);
                    const int32 CandidateDepth = *TargetDepth + 1;
                    if (CandidateDepth > SourceDepth)
                    {
                        SourceDepth = CandidateDepth;
                        bChanged = true;
                    }
                }
                if (!bChanged) break;
            }

            TArray<const FSekiroAnimIRNode*> OrderedNodes;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes) OrderedNodes.Add(&Node);
            OrderedNodes.Sort([](const FSekiroAnimIRNode& Left, const FSekiroAnimIRNode& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });
            int32 MaximumDepth = 0;
            for (const TPair<FString, int32>& Pair : Depths) MaximumDepth = FMath::Max(MaximumDepth, Pair.Value);
            for (const FSekiroAnimIRNode* Node : OrderedNodes)
            {
                if (!Depths.Contains(Node->Id)) Depths.Add(Node->Id, ++MaximumDepth);
            }
            TMap<int32, int32> LayerSizes;
            for (const FSekiroAnimIRNode* Node : OrderedNodes)
            {
                if (!ExplicitPositions.Contains(Node->Id)) ++LayerSizes.FindOrAdd(Depths[Node->Id]);
            }
            TMap<int32, int32> LayerCounts;
            TSet<FIntPoint> Occupied;
            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions) Occupied.Add(Pair.Value);

            for (int32 ElementIndex = 0; ElementIndex < OrderedNodes.Num(); ++ElementIndex)
            {
                const FSekiroAnimIRNode& Node = *OrderedNodes[ElementIndex];
                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                if (NativeNode == nullptr || *NativeNode == nullptr) continue;
                const FIntPoint* Explicit = ExplicitPositions.Find(Node.Id);
                if (Explicit != nullptr)
                {
                    (*NativeNode)->NodePosX = Explicit->X;
                    (*NativeNode)->NodePosY = Explicit->Y;
                    continue;
                }

                const int32 Depth = Depths[Node.Id];
                const int32 SecondaryIndex = LayerCounts.FindOrAdd(Depth)++;
                FIntPoint Position;
                if (Style == ESekiroAnimIRLayoutStyle::Radial)
                {
                    const int32 LayerSize = FMath::Max(1, LayerSizes.FindRef(Depth));
                    const double Angle = 2.0 * PI * static_cast<double>(SecondaryIndex)
                        / static_cast<double>(LayerSize) - PI / 2.0;
                    const double Radius = static_cast<double>(Depth * 520);
                    Position = FIntPoint(
                        FMath::RoundToInt(FMath::Cos(Angle) * Radius),
                        FMath::RoundToInt(FMath::Sin(Angle) * Radius));
                }
                else
                {
                    Position = MakeFlowPosition(Style, Depth, SecondaryIndex, ElementIndex);
                }
                while (Occupied.Contains(Position))
                {
                    if (Style == ESekiroAnimIRLayoutStyle::TopToBottom
                        || Style == ESekiroAnimIRLayoutStyle::BottomToTop)
                    {
                        Position.X += 400;
                    }
                    else if (Style != ESekiroAnimIRLayoutStyle::Radial)
                    {
                        Position.Y += 240;
                    }
                    else
                    {
                        Position.X += 160;
                        Position.Y += 160;
                    }
                }
                Occupied.Add(Position);
                (*NativeNode)->NodePosX = Position.X;
                (*NativeNode)->NodePosY = Position.Y;
            }
        }

        /**
         * 将 Pose 或 StatePose IR Graph 映射到已有原生 AnimationGraph，复用其默认 Result 节点。
         * 函数递归处理 StateMachine 拥有的内部 Graph，并通过原生 Schema 连接所有 Pose Link。
         * 必须在游戏线程调用，NativeGraph 在调用期间由本 Builder 独占修改。
         *
         * @param Graph 待物化的 Pose/StatePose IR Graph。
         * @param NativeGraph 对应的原生主 Graph 或 State Graph。
         * @return 节点、子图及连接全部成功时返回 true。
         */
        bool BuildPoseGraph(const FSekiroAnimIRGraph& Graph, UAnimationGraph& NativeGraph)
        {
            NativeGraph.GraphGuid = MakeStableGuid(TEXT("Graph"), Graph.Id);
            NativeGraphs.Add(Graph.Id, &NativeGraph);

            const FSekiroAnimIRNode* RootNode = nullptr;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                if (Node.Id == Graph.RootNodeId)
                {
                    RootNode = &Node;
                    break;
                }
            }

            if (RootNode == nullptr || !BindDefaultRootNode(*RootNode, NativeGraph)) return false;

            int32 NodeIndex = 0;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                if (Node.Id == Graph.RootNodeId) continue;
                if (!CreateRegisteredNode(Node, NativeGraph, NodeIndex)) return false;
                ++NodeIndex;
            }

            if (!BindCachedPoseNodes(Graph)) return false;

            for (const FSekiroAnimIRLink& Link : Graph.Links)
            {
                if (!ConnectPoseLink(Link, NativeGraph)) return false;
            }

            ApplyPoseGraphLayout(Graph);

            return true;
        }

        /**
         * 将 IR 根节点绑定到 Graph Schema 已创建的 OutputPose 或 StateResult，不重复创建根节点。
         * 必须在游戏线程调用；函数只修改默认根节点的确定性 Guid 与布局。
         *
         * @param RootNode IR 根节点声明。
         * @param NativeGraph 已由 UE 原生生命周期初始化的 Graph。
         * @return 找到与注册表类一致的唯一默认根节点时返回 true。
         */
        bool BindDefaultRootNode(const FSekiroAnimIRNode& RootNode, UAnimationGraph& NativeGraph)
        {
            UClass* const* RootClass = Preflight.NodeClasses.Find(RootNode.NodeType);
            if (RootClass == nullptr)
            {
                AddError(
                    Diagnostics,
                    NodeClassLoadFailed,
                    TEXT("Root NodeType has no preloaded editor class."),
                    RootNode.Id,
                    RootNode.SourceLocation);
                return false;
            }

            UEdGraphNode* NativeRoot = nullptr;
            for (UEdGraphNode* Candidate : NativeGraph.Nodes)
            {
                if (Candidate != nullptr && Candidate->GetClass() == *RootClass)
                {
                    NativeRoot = Candidate;
                    break;
                }
            }

            if (NativeRoot == nullptr)
            {
                AddError(
                    Diagnostics,
                    DefaultRootNodeNotFound,
                    FString::Printf(
                        TEXT("Native Graph '%s' has no default root node of class '%s'."),
                        *NativeGraph.GetPathName(),
                        *(*RootClass)->GetPathName()),
                    RootNode.Id,
                    RootNode.SourceLocation);
                return false;
            }

            NativeRoot->NodeGuid = MakeStableGuid(TEXT("Node"), RootNode.Id);
            NativeRoot->NodePosX = 500;
            NativeRoot->NodePosY = 0;
            NativeNodes.Add(RootNode.Id, NativeRoot);
            return true;
        }

        /**
         * 根据注册表解析出的 UClass 分派首批原生节点构造，并应用节点专属属性。
         * 分派不读取 NodeType 字符串，因此注册表仍是 NodeType 到编辑器类的唯一映射。
         * 必须在游戏线程调用，NativeGraph 由当前 Builder 独占。
         *
         * @param Node 待创建的非根 IR 节点。
         * @param NativeGraph 节点所属的原生 Pose Graph。
         * @param NodeIndex 当前 Graph 内的稳定布局序号。
         * @return 节点及其 OwnedGraph 成功物化时返回 true。
         */
        bool CreateRegisteredNode(
            const FSekiroAnimIRNode& Node,
            UAnimationGraph& NativeGraph,
            const int32 NodeIndex)
        {
            UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.NodeType);
            if (NodeClass == nullptr)
            {
                AddError(
                    Diagnostics,
                    NodeClassLoadFailed,
                    TEXT("NodeType has no preloaded editor class."),
                    Node.Id,
                    Node.SourceLocation);
                return false;
            }

            const int32 PositionX = 100;
            const int32 PositionY = NodeIndex * 180;
            if (*NodeClass == UAnimGraphNode_SequencePlayer::StaticClass())
            {
                UAnimGraphNode_SequencePlayer* SequenceNode =
                    CreateNativeNode<UAnimGraphNode_SequencePlayer>(NativeGraph, Node.Id, PositionX, PositionY);
                if (SequenceNode == nullptr) return ReportNodeCreationFailure(Node);

                UAnimSequenceBase* const* Sequence = Preflight.SequenceAssets.Find(Node.Id);
                if (Sequence == nullptr) return ReportNodeCreationFailure(Node);
                SequenceNode->Node.SetSequence(*Sequence);

                const FSekiroAnimIRProperty* LoopProperty = FindProperty(Node, TEXT("bLoopAnimation"));
                if (LoopProperty != nullptr)
                {
                    SequenceNode->Node.SetLoopAnimation(LoopProperty->Value.BoolValue);
                }

                const FSekiroAnimIRProperty* PlayRateProperty = FindProperty(Node, TEXT("PlayRate"));
                if (PlayRateProperty != nullptr)
                {
                    SequenceNode->Node.SetPlayRate(static_cast<float>(PlayRateProperty->Value.FloatValue));
                }

                const FSekiroAnimIRProperty* StartPositionProperty = FindProperty(Node, TEXT("StartPosition"));
                if (StartPositionProperty != nullptr)
                {
                    SequenceNode->Node.SetStartPosition(
                        static_cast<float>(StartPositionProperty->Value.FloatValue));
                }

                const FSekiroAnimIRProperty* GroupNameProperty = FindProperty(Node, TEXT("GroupName"));
                if (GroupNameProperty != nullptr)
                {
                    SequenceNode->Node.SetGroupName(GroupNameProperty->Value.NameValue);
                    SequenceNode->Node.SetGroupMethod(EAnimSyncMethod::SyncGroup);
                }
                const FSekiroAnimIRProperty* GroupRoleProperty = FindProperty(Node, TEXT("GroupRole"));
                if (GroupRoleProperty != nullptr)
                {
                    const FName Role = GroupRoleProperty->Value.NameValue;
                    EAnimGroupRole::Type NativeRole = EAnimGroupRole::CanBeLeader;
                    if (Role == TEXT("AlwaysFollower")) NativeRole = EAnimGroupRole::AlwaysFollower;
                    else if (Role == TEXT("AlwaysLeader")) NativeRole = EAnimGroupRole::AlwaysLeader;
                    else if (Role == TEXT("TransitionLeader")) NativeRole = EAnimGroupRole::TransitionLeader;
                    else if (Role == TEXT("TransitionFollower")) NativeRole = EAnimGroupRole::TransitionFollower;
                    SequenceNode->Node.SetGroupRole(NativeRole);
                }
                const FSekiroAnimIRProperty* GroupMethodProperty = FindProperty(Node, TEXT("GroupMethod"));
                if (GroupMethodProperty != nullptr)
                {
                    const FName Method = GroupMethodProperty->Value.NameValue;
                    SequenceNode->Node.SetGroupMethod(Method == TEXT("Graph")
                        ? EAnimSyncMethod::Graph
                        : Method == TEXT("DoNotSync") ? EAnimSyncMethod::DoNotSync : EAnimSyncMethod::SyncGroup);
                }

                NativeNodes.Add(Node.Id, SequenceNode);
                return true;
            }

            if (*NodeClass == UK2Node_VariableGet::StaticClass())
            {
                const FSekiroAnimIRProperty* PropertyName = FindProperty(Node, TEXT("PropertyName"));
                if (PropertyName == nullptr) return ReportNodeCreationFailure(Node);
                FGraphNodeCreator<UK2Node_VariableGet> NodeCreator(NativeGraph);
                UK2Node_VariableGet* GetterNode = NodeCreator.CreateNode(false);
                if (GetterNode == nullptr) return ReportNodeCreationFailure(Node);
                GetterNode->VariableReference.SetSelfMember(PropertyName->Value.NameValue);
                NodeCreator.Finalize();
                GetterNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                GetterNode->NodePosX = PositionX;
                GetterNode->NodePosY = PositionY;
                NativeNodes.Add(Node.Id, GetterNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_BlendListByBool::StaticClass())
            {
                UAnimGraphNode_BlendListByBool* BlendNode =
                    CreateNativeNode<UAnimGraphNode_BlendListByBool>(NativeGraph, Node.Id, PositionX, PositionY);
                if (BlendNode == nullptr) return ReportNodeCreationFailure(Node);
                const FSekiroAnimIRProperty* BlendTime = FindProperty(Node, TEXT("BlendTime"));
                if (BlendTime != nullptr) SetBlendListTimes(BlendNode->Node, static_cast<float>(BlendTime->Value.FloatValue));
                NativeNodes.Add(Node.Id, BlendNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_BlendListByEnum::StaticClass())
            {
                UAnimGraphNode_BlendListByEnum* BlendNode =
                    CreateNativeNode<UAnimGraphNode_BlendListByEnum>(NativeGraph, Node.Id, PositionX, PositionY);
                UEnum* const* EnumType = Preflight.NodeEnums.Find(Node.Id);
                const FSekiroAnimIRProperty* EntriesProperty = FindProperty(Node, TEXT("EnumEntries"));
                if (BlendNode == nullptr || EnumType == nullptr || EntriesProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }
                TArray<FString> EntryStrings;
                EntriesProperty->Value.StringValue.ParseIntoArray(EntryStrings, TEXT("|"), true);
                BlendNode->ReloadEnum(*EnumType);
                FArrayProperty* EntriesArray = FindFProperty<FArrayProperty>(
                    UAnimGraphNode_BlendListByEnum::StaticClass(), TEXT("VisibleEnumEntries"));
                FNameProperty* NameProperty = EntriesArray != nullptr
                    ? CastField<FNameProperty>(EntriesArray->Inner)
                    : nullptr;
                if (EntriesArray == nullptr || NameProperty == nullptr) return ReportNodeCreationFailure(Node);
                FScriptArrayHelper EntriesHelper(EntriesArray, EntriesArray->ContainerPtrToValuePtr<void>(BlendNode));
                EntriesHelper.EmptyValues();
                for (const FString& EntryString : EntryStrings)
                {
                    const int32 AddedIndex = EntriesHelper.AddValue();
                    NameProperty->SetPropertyValue(EntriesHelper.GetRawPtr(AddedIndex), FName(*EntryString));
                    BlendNode->Node.AddPose();
                }
                BlendNode->ReconstructNode();
                const FSekiroAnimIRProperty* BlendTime = FindProperty(Node, TEXT("BlendTime"));
                if (BlendTime != nullptr) SetBlendListTimes(BlendNode->Node, static_cast<float>(BlendTime->Value.FloatValue));
                NativeNodes.Add(Node.Id, BlendNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_Slot::StaticClass())
            {
                UAnimGraphNode_Slot* SlotNode =
                    CreateNativeNode<UAnimGraphNode_Slot>(NativeGraph, Node.Id, PositionX, PositionY);
                const FSekiroAnimIRProperty* SlotNameProperty = FindProperty(Node, TEXT("SlotName"));
                if (SlotNode == nullptr || SlotNameProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                SlotNode->Node.SlotName = SlotNameProperty->Value.NameValue;
                const FSekiroAnimIRProperty* AlwaysUpdateProperty =
                    FindProperty(Node, TEXT("bAlwaysUpdateSourcePose"));
                if (AlwaysUpdateProperty != nullptr)
                {
                    SlotNode->Node.bAlwaysUpdateSourcePose = AlwaysUpdateProperty->Value.BoolValue;
                }
                SlotNode->ReconstructNode();
                NativeNodes.Add(Node.Id, SlotNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LayeredBoneBlend::StaticClass())
            {
                UAnimGraphNode_LayeredBoneBlend* LayeredNode =
                    CreateNativeNode<UAnimGraphNode_LayeredBoneBlend>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                const TArray<FBranchFilter>* BranchFilters =
                    Preflight.LayeredBlendBranchFilters.Find(Node.Id);
                if (LayeredNode == nullptr || BranchFilters == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                if (LayeredNode->Node.BlendPoses.IsEmpty()) LayeredNode->Node.AddPose();
                while (LayeredNode->Node.BlendPoses.Num() > 1)
                {
                    LayeredNode->Node.RemovePose(LayeredNode->Node.BlendPoses.Num() - 1);
                }
                LayeredNode->Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
                LayeredNode->Node.LayerSetup.SetNum(1);
                LayeredNode->Node.LayerSetup[0].BranchFilters = *BranchFilters;
                const FSekiroAnimIRProperty* RotationBlend =
                    FindProperty(Node, TEXT("bMeshSpaceRotationBlend"));
                if (RotationBlend != nullptr)
                {
                    LayeredNode->Node.bMeshSpaceRotationBlend = RotationBlend->Value.BoolValue;
                }
                const FSekiroAnimIRProperty* ScaleBlend =
                    FindProperty(Node, TEXT("bMeshSpaceScaleBlend"));
                if (ScaleBlend != nullptr)
                {
                    LayeredNode->Node.bMeshSpaceScaleBlend = ScaleBlend->Value.BoolValue;
                }
                const FSekiroAnimIRProperty* CurveOption =
                    FindProperty(Node, TEXT("CurveBlendOption"));
                if (CurveOption != nullptr)
                {
                    ECurveBlendOption::Type ParsedCurveOption = ECurveBlendOption::Override;
                    if (!ParseLayeredBlendCurveOption(CurveOption->Value.NameValue, ParsedCurveOption))
                    {
                        return ReportNodeCreationFailure(Node);
                    }
                    LayeredNode->Node.CurveBlendOption = ParsedCurveOption;
                }
                const FSekiroAnimIRProperty* BlendRootMotion =
                    FindProperty(Node, TEXT("bBlendRootMotionBasedOnRootBone"));
                if (BlendRootMotion != nullptr)
                {
                    LayeredNode->Node.bBlendRootMotionBasedOnRootBone =
                        BlendRootMotion->Value.BoolValue;
                }

                LayeredNode->ReconstructNode();
                NativeNodes.Add(Node.Id, LayeredNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_Inertialization::StaticClass())
            {
                UAnimGraphNode_Inertialization* InertializationNode =
                    CreateNativeNode<UAnimGraphNode_Inertialization>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (InertializationNode == nullptr) return ReportNodeCreationFailure(Node);
                NativeNodes.Add(Node.Id, InertializationNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LocalToComponentSpace::StaticClass())
            {
                UAnimGraphNode_LocalToComponentSpace* ConversionNode =
                    CreateNativeNode<UAnimGraphNode_LocalToComponentSpace>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (ConversionNode == nullptr) return ReportNodeCreationFailure(Node);
                NativeNodes.Add(Node.Id, ConversionNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_ComponentToLocalSpace::StaticClass())
            {
                UAnimGraphNode_ComponentToLocalSpace* ConversionNode =
                    CreateNativeNode<UAnimGraphNode_ComponentToLocalSpace>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (ConversionNode == nullptr) return ReportNodeCreationFailure(Node);
                NativeNodes.Add(Node.Id, ConversionNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_OrientationWarping::StaticClass())
            {
                UAnimGraphNode_OrientationWarping* OrientationNode =
                    CreateNativeNode<UAnimGraphNode_OrientationWarping>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                const FSekiroAnimIRProperty* SpineBonesProperty = FindProperty(Node, TEXT("SpineBones"));
                const FSekiroAnimIRProperty* FootRootProperty = FindProperty(Node, TEXT("IKFootRootBone"));
                const FSekiroAnimIRProperty* FootBonesProperty = FindProperty(Node, TEXT("IKFootBones"));
                if (OrientationNode == nullptr
                    || SpineBonesProperty == nullptr
                    || FootRootProperty == nullptr
                    || FootBonesProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                OrientationNode->Node.Mode = EWarpingEvaluationMode::Manual;
                OrientationNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                OrientationNode->Node.SpineBones.Reset();
                TArray<FString> SpineBoneNames;
                SpineBonesProperty->Value.StringValue.ParseIntoArray(SpineBoneNames, TEXT("|"), true);
                for (const FString& SpineBoneName : SpineBoneNames)
                {
                    OrientationNode->Node.SpineBones.Add(
                        FBoneReference(FName(*SpineBoneName.TrimStartAndEnd())));
                }

                OrientationNode->Node.IKFootRootBone = FBoneReference(FootRootProperty->Value.NameValue);
                OrientationNode->Node.IKFootBones.Reset();
                TArray<FString> FootBoneNames;
                FootBonesProperty->Value.StringValue.ParseIntoArray(FootBoneNames, TEXT("|"), true);
                for (const FString& FootBoneName : FootBoneNames)
                {
                    OrientationNode->Node.IKFootBones.Add(
                        FBoneReference(FName(*FootBoneName.TrimStartAndEnd())));
                }

                const FSekiroAnimIRProperty* RotationAxisProperty = FindProperty(Node, TEXT("RotationAxis"));
                if (RotationAxisProperty != nullptr)
                {
                    const FName RotationAxis = RotationAxisProperty->Value.NameValue;
                    OrientationNode->Node.RotationAxis = RotationAxis == TEXT("X")
                        ? EAxis::X
                        : RotationAxis == TEXT("Y") ? EAxis::Y : EAxis::Z;
                }

                const FSekiroAnimIRProperty* DistributionProperty =
                    FindProperty(Node, TEXT("DistributedBoneOrientationAlpha"));
                if (DistributionProperty != nullptr)
                {
                    OrientationNode->Node.DistributedBoneOrientationAlpha = FMath::Clamp(
                        static_cast<float>(DistributionProperty->Value.FloatValue),
                        0.0f,
                        1.0f);
                }

                const FSekiroAnimIRProperty* InterpSpeedProperty =
                    FindProperty(Node, TEXT("RotationInterpSpeed"));
                if (InterpSpeedProperty != nullptr)
                {
                    OrientationNode->Node.RotationInterpSpeed = FMath::Max(
                        0.0f,
                        static_cast<float>(InterpSpeedProperty->Value.FloatValue));
                }

                OrientationNode->ReconstructNode();
                NativeNodes.Add(Node.Id, OrientationNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_FootPlacement::StaticClass())
            {
                UAnimGraphNode_FootPlacement* FootPlacementNode =
                    CreateNativeNode<UAnimGraphNode_FootPlacement>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                const FSekiroAnimIRProperty* FootRootProperty = FindProperty(Node, TEXT("IKFootRootBone"));
                const FSekiroAnimIRProperty* PelvisProperty = FindProperty(Node, TEXT("PelvisBone"));
                const TArray<FFootPlacemenLegDefinition>* LegDefinitions =
                    Preflight.FootPlacementLegDefinitions.Find(Node.Id);
                if (FootPlacementNode == nullptr
                    || FootRootProperty == nullptr
                    || PelvisProperty == nullptr
                    || LegDefinitions == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                FootPlacementNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                FootPlacementNode->Node.IKFootRootBone = FBoneReference(FootRootProperty->Value.NameValue);
                FootPlacementNode->Node.PelvisBone = FBoneReference(PelvisProperty->Value.NameValue);
                FootPlacementNode->Node.LegDefinitions = *LegDefinitions;
                const FSekiroAnimIRProperty* PlantSpeedModeProperty =
                    FindProperty(Node, TEXT("PlantSpeedMode"));
                FootPlacementNode->Node.PlantSpeedMode = PlantSpeedModeProperty != nullptr
                    && PlantSpeedModeProperty->Value.NameValue == TEXT("Manual")
                    ? EWarpingEvaluationMode::Manual
                    : EWarpingEvaluationMode::Graph;
                const EFootPlacementLockType* PlantLockType =
                    Preflight.FootPlacementLockTypes.Find(Node.Id);
                if (PlantLockType != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.LockType = *PlantLockType;
                }

                const FSekiroAnimIRProperty* PelvisMaxOffset = FindProperty(Node, TEXT("PelvisMaxOffset"));
                if (PelvisMaxOffset != nullptr)
                {
                    FootPlacementNode->Node.PelvisSettings.MaxOffset =
                        static_cast<float>(PelvisMaxOffset->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* PelvisRebalancing =
                    FindProperty(Node, TEXT("PelvisHorizontalRebalancingWeight"));
                if (PelvisRebalancing != nullptr)
                {
                    FootPlacementNode->Node.PelvisSettings.HorizontalRebalancingWeight =
                        static_cast<float>(PelvisRebalancing->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* PlantSpeedThreshold =
                    FindProperty(Node, TEXT("PlantSpeedThreshold"));
                if (PlantSpeedThreshold != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.SpeedThreshold =
                        static_cast<float>(PlantSpeedThreshold->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* PlantDistanceToGround =
                    FindProperty(Node, TEXT("PlantDistanceToGround"));
                if (PlantDistanceToGround != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.DistanceToGround =
                        static_cast<float>(PlantDistanceToGround->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* TraceStartOffset = FindProperty(Node, TEXT("TraceStartOffset"));
                if (TraceStartOffset != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.StartOffset =
                        static_cast<float>(TraceStartOffset->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* TraceEndOffset = FindProperty(Node, TEXT("TraceEndOffset"));
                if (TraceEndOffset != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.EndOffset =
                        static_cast<float>(TraceEndOffset->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* TraceSweepRadius = FindProperty(Node, TEXT("TraceSweepRadius"));
                if (TraceSweepRadius != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.SweepRadius =
                        static_cast<float>(TraceSweepRadius->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* TraceMaxGroundPenetration =
                    FindProperty(Node, TEXT("TraceMaxGroundPenetration"));
                if (TraceMaxGroundPenetration != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.MaxGroundPenetration =
                        static_cast<float>(TraceMaxGroundPenetration->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* TraceEnabled = FindProperty(Node, TEXT("bTraceEnabled"));
                if (TraceEnabled != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.bEnabled = TraceEnabled->Value.BoolValue;
                }

                FootPlacementNode->ReconstructNode();
                NativeNodes.Add(Node.Id, FootPlacementNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LegIK::StaticClass())
            {
                UAnimGraphNode_LegIK* LegIKNode = CreateNativeNode<UAnimGraphNode_LegIK>(
                    NativeGraph,
                    Node.Id,
                    PositionX,
                    PositionY);
                const TArray<FAnimLegIKDefinition>* LegDefinitions =
                    Preflight.LegIKDefinitions.Find(Node.Id);
                if (LegIKNode == nullptr || LegDefinitions == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                LegIKNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                LegIKNode->Node.LegsDefinition = *LegDefinitions;
                const FSekiroAnimIRProperty* ReachPrecision = FindProperty(Node, TEXT("ReachPrecision"));
                if (ReachPrecision != nullptr)
                {
                    LegIKNode->Node.ReachPrecision = static_cast<float>(ReachPrecision->Value.FloatValue);
                }
                const FSekiroAnimIRProperty* MaxIterations = FindProperty(Node, TEXT("MaxIterations"));
                if (MaxIterations != nullptr)
                {
                    LegIKNode->Node.MaxIterations = static_cast<int32>(MaxIterations->Value.IntegerValue);
                }

                LegIKNode->ReconstructNode();
                NativeNodes.Add(Node.Id, LegIKNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_SaveCachedPose::StaticClass())
            {
                UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode =
                    CreateNativeNode<UAnimGraphNode_SaveCachedPose>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                const FSekiroAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
                if (SaveCachedPoseNode == nullptr || CacheNameProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                SaveCachedPoseNode->CacheName = CacheNameProperty->Value.StringValue;
                SaveCachedPoseNode->Node.CachePoseName = FName(*SaveCachedPoseNode->CacheName);
                NativeNodes.Add(Node.Id, SaveCachedPoseNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_UseCachedPose::StaticClass())
            {
                UAnimGraphNode_UseCachedPose* UseCachedPoseNode =
                    CreateNativeNode<UAnimGraphNode_UseCachedPose>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (UseCachedPoseNode == nullptr) return ReportNodeCreationFailure(Node);
                NativeNodes.Add(Node.Id, UseCachedPoseNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_StateMachine::StaticClass())
            {
                UAnimGraphNode_StateMachine* StateMachineNode =
                    CreateNativeNode<UAnimGraphNode_StateMachine>(NativeGraph, Node.Id, PositionX, PositionY);
                if (StateMachineNode == nullptr || StateMachineNode->EditorStateMachineGraph == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                NativeNodes.Add(Node.Id, StateMachineNode);
                const FSekiroAnimIRGraph* OwnedGraph = FindGraph(Node.OwnedGraphId);
                if (OwnedGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeGraphMissing,
                        TEXT("StateMachine OwnedGraph could not be found after validation."),
                        Node.OwnedGraphId,
                        Node.SourceLocation);
                    return false;
                }

                StateMachineNode->EditorStateMachineGraph->GraphGuid =
                    MakeStableGuid(TEXT("Graph"), OwnedGraph->Id);
                FEdGraphUtilities::RenameGraphToNameOrCloseToName(
                    StateMachineNode->EditorStateMachineGraph,
                    OwnedGraph->Name);
                return BuildStateMachineGraph(
                    *OwnedGraph,
                    *StateMachineNode->EditorStateMachineGraph);
            }

            AddError(
                Diagnostics,
                UnsupportedRegisteredNodeClass,
                FString::Printf(
                    TEXT("Registered editor node class '%s' has no construction handler."),
                    *(*NodeClass)->GetPathName()),
                Node.Id,
                Node.SourceLocation);
            return false;
        }

        /**
         * 将当前 Pose Graph 中的 Use Cached Pose 节点按 CacheName 绑定到同图 Save Cached Pose 节点，并同步运行时调试名。
         * 函数在本图全部节点创建后、Pose Link 连接前执行，因此不依赖 IR 声明顺序；它不跨 Graph 搜索，也不通过普通反射字段模拟原生关联。
         * 必须在游戏线程调用，NativeNodes 与新建编辑器节点由当前 Builder 独占修改。
         *
         * @param Graph 待绑定的规范 Pose Graph IR；节点 ID 必须已存在于 NativeNodes。
         * @return 所有缓存名非空且唯一、每个 Use 都找到同图 Save 时返回 true；否则追加稳定诊断并返回 false。
         */
        bool BindCachedPoseNodes(const FSekiroAnimIRGraph& Graph)
        {
            TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveNodesByCacheName;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.NodeType);
                if (NodeClass == nullptr || *NodeClass != UAnimGraphNode_SaveCachedPose::StaticClass()) continue;

                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                UAnimGraphNode_SaveCachedPose* SaveNode = NativeNode != nullptr
                    ? Cast<UAnimGraphNode_SaveCachedPose>(*NativeNode)
                    : nullptr;
                const FSekiroAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
                const FString CacheName = CacheNameProperty != nullptr
                    ? CacheNameProperty->Value.StringValue
                    : FString();
                if (SaveNode == nullptr || CacheName.IsEmpty())
                {
                    AddError(
                        Diagnostics,
                        EmptyCachedPoseName,
                        TEXT("SaveCachedPose requires a non-empty CacheName."),
                        Node.Id,
                        Node.SourceLocation);
                    return false;
                }

                if (SaveNodesByCacheName.Contains(CacheName))
                {
                    AddError(
                        Diagnostics,
                        DuplicateCachedPoseName,
                        FString::Printf(
                            TEXT("Pose Graph '%s' contains duplicate SaveCachedPose CacheName '%s'."),
                            *Graph.Id,
                            *CacheName),
                        Node.Id,
                        Node.SourceLocation);
                    return false;
                }

                SaveNodesByCacheName.Add(CacheName, SaveNode);
            }

            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.NodeType);
                if (NodeClass == nullptr || *NodeClass != UAnimGraphNode_UseCachedPose::StaticClass()) continue;

                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                UAnimGraphNode_UseCachedPose* UseNode = NativeNode != nullptr
                    ? Cast<UAnimGraphNode_UseCachedPose>(*NativeNode)
                    : nullptr;
                const FSekiroAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
                const FString CacheName = CacheNameProperty != nullptr
                    ? CacheNameProperty->Value.StringValue
                    : FString();
                UAnimGraphNode_SaveCachedPose* const* SaveNode = SaveNodesByCacheName.Find(CacheName);
                if (UseNode == nullptr || SaveNode == nullptr)
                {
                    AddError(
                        Diagnostics,
                        CachedPoseSaveNotFound,
                        FString::Printf(
                            TEXT("UseCachedPose CacheName '%s' has no SaveCachedPose in Pose Graph '%s'."),
                            *CacheName,
                            *Graph.Id),
                        Node.Id,
                        Node.SourceLocation);
                    return false;
                }

                UseNode->SaveCachedPoseNode = *SaveNode;
                UseNode->Node.CachePoseName = FName(*CacheName);
            }

            return true;
        }

        /**
         * 记录原生节点创建失败的稳定诊断，供各类型分支统一返回。
         * 本函数只追加诊断，必须由当前 Builder 所在线程调用。
         *
         * @param Node 创建失败的 IR 节点。
         * @return 始终返回 false，便于调用方直接结束构建。
         */
        bool ReportNodeCreationFailure(const FSekiroAnimIRNode& Node)
        {
            AddError(
                Diagnostics,
                NativeNodeCreationFailed,
                FString::Printf(TEXT("Failed to create native node '%s'."), *Node.Id),
                Node.Id,
                Node.SourceLocation);
            return false;
        }

        /**
         * 在 Transition 自动创建的 UAnimationTransitionGraph 中搭建 Lua Rule 直接调用，并连接默认 Result。
         * 此图由原生状态机仅在检查当前 State 出边时求值；Lua 规则约定只读 AnimInstance。
         * 必须在游戏线程调用，TransitionNode 及其 BoundGraph 由当前 Builder 独占。
         *
         * @param Transition 提供模块规则名、稳定 ID 和源码位置的 IR Transition。
         * @param TransitionNode 已经完成 PostPlacedNewNode、拥有默认 Result 的原生 Transition 节点。
         * @return Lua Rule 调用、Self、默认参数和 Result 连接全部创建成功时返回 true。
         */
        bool BuildTransitionRuleGraph(
            const FSekiroAnimIRTransition& Transition,
            UAnimStateTransitionNode& TransitionNode,
            UAnimStateNode& SourceState)
        {
            UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(TransitionNode.BoundGraph);
            UAnimGraphNode_TransitionResult* ResultNode = TransitionGraph != nullptr
                ? TransitionGraph->GetResultNode()
                : nullptr;
            if (TransitionGraph == nullptr || ResultNode == nullptr)
            {
                AddError(
                    Diagnostics,
                    TransitionResultNotFound,
                    TEXT("Native Transition Graph did not create its default Transition Result node."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }

            UFunction* EvaluateFunction = USekiroLuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(
                    USekiroLuaTransitionRuntimeLibrary,
                    EvaluateLuaTransitionRule));
            if (EvaluateFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2FunctionNotFound,
                    TEXT("EvaluateLuaTransitionRule function was not found."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }

            ResultNode->NodeGuid = MakeStableGuid(TEXT("TransitionResult"), Transition.Id);
            ResultNode->NodePosX = 600;
            ResultNode->NodePosY = 0;
            UK2Node_Self* SelfNode = CreateNativeNode<UK2Node_Self>(
                *TransitionGraph,
                TEXT("TransitionRule.Self.") + Transition.Id,
                0,
                160);
            UK2Node_CallFunction* EvaluateNode = CreateCallFunctionNode(
                *TransitionGraph,
                TEXT("EvaluateTransitionRule.") + Transition.Id,
                EvaluateFunction,
                260,
                0);

            UEdGraphPin* SelfPin = SelfNode != nullptr
                ? SelfNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Output)
                : nullptr;
            UEdGraphPin* AnimInstancePin = EvaluateNode != nullptr
                ? EvaluateNode->FindPin(TEXT("AnimInstance"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ModulePin = EvaluateNode != nullptr
                ? EvaluateNode->FindPin(TEXT("LuaModuleName"), EGPD_Input)
                : nullptr;
            UEdGraphPin* RulePin = EvaluateNode != nullptr
                ? EvaluateNode->FindPin(TEXT("RuleFunctionName"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ReturnPin = EvaluateNode != nullptr ? EvaluateNode->GetReturnValuePin() : nullptr;
            UEdGraphPin* ResultPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
            if (SelfPin == nullptr
                || AnimInstancePin == nullptr
                || ModulePin == nullptr
                || RulePin == nullptr
                || ReturnPin == nullptr
                || ResultPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("Lua Transition Rule call or Result did not expose required K2 Pins."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }

            const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
            K2Schema->TrySetDefaultValue(*ModulePin, Preflight.Blueprint.SourceModule, false);
            K2Schema->TrySetDefaultValue(*RulePin, Transition.RuleFunctionName.ToString(), false);
            if (!K2Schema->TryCreateConnection(SelfPin, AnimInstancePin))
            {
                AddError(
                    Diagnostics,
                    K2ConnectionFailed,
                    TEXT("Failed to connect AnimInstance Self to Lua Transition Rule call."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }
            UEdGraphPin* FinalRulePin = ReturnPin;
            if (Transition.Gate.RootIndex != INDEX_NONE)
            {
                UEdGraphPin* NativeGatePin = BuildTransitionGateNode(
                    Transition, Transition.Gate.RootIndex, *TransitionGraph, SourceState, *SelfPin, *ReturnPin);
                UFunction* AndFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanAND));
                UK2Node_CallFunction* AndNode = CreateCallFunctionNode(
                    *TransitionGraph, TEXT("Gate.AndLua.") + Transition.Id, AndFunction, 480, 0);
                UEdGraphPin* AndA = AndNode != nullptr ? AndNode->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                UEdGraphPin* AndB = AndNode != nullptr ? AndNode->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                UEdGraphPin* AndResult = AndNode != nullptr ? AndNode->GetReturnValuePin() : nullptr;
                if (NativeGatePin == nullptr || AndA == nullptr || AndB == nullptr || AndResult == nullptr)
                {
                    AddError(Diagnostics, K2ConnectionFailed,
                        FString::Printf(
                            TEXT("Failed to create Lua/Gate AND Pins (Gate=%s, A=%s, B=%s, Result=%s)."),
                            NativeGatePin != nullptr ? TEXT("valid") : TEXT("missing"),
                            AndA != nullptr ? TEXT("valid") : TEXT("missing"),
                            AndB != nullptr ? TEXT("valid") : TEXT("missing"),
                            AndResult != nullptr ? TEXT("valid") : TEXT("missing")),
                        Transition.Id, Transition.SourceLocation);
                    return false;
                }
                const bool bLuaBoolConnected = K2Schema->TryCreateConnection(ReturnPin, AndA);
                const bool bNativeGateConnected = K2Schema->TryCreateConnection(NativeGatePin, AndB);
                if (!bLuaBoolConnected || !bNativeGateConnected)
                {
                    AddError(Diagnostics, K2ConnectionFailed,
                        FString::Printf(
                            TEXT("Failed to combine Lua bool with native Transition Gate (Lua=%s, Gate=%s)."),
                            bLuaBoolConnected ? TEXT("connected") : TEXT("rejected"),
                            bNativeGateConnected ? TEXT("connected") : TEXT("rejected")),
                        Transition.Id, Transition.SourceLocation);
                    return false;
                }
                FinalRulePin = AndResult;
            }
            if (!K2Schema->TryCreateConnection(FinalRulePin, ResultPin)) return false;
            return true;
        }

        /**
         * 递归物化一棵已验证的扁平 Gate AST，并返回该子树的 Bool 输出 Pin。
         * 除 LuaBool 叶子复用当前 Rule 的直接返回外，其余节点均为原生 Getter 或纯数学函数。
         * 本函数只能在游戏线程构建资产时调用，不执行实际动画规则。
         *
         * @param Transition 提供 Gate 节点数组、稳定 ID 和诊断位置。
         * @param GateIndex 当前子树根索引。
         * @param Graph 目标原生 Transition Rule Graph。
         * @param SourceState Transition 源 State，用于绑定最相关 SequencePlayer 时间 Getter。
         * @param SelfPin 当前 AnimInstance 的 Self 输出。
         * @param LuaBoolPin 当前 Transition Graph 内 Lua Rule 的直接 Bool 输出。
         * @return 成功时返回子树 Bool 输出 Pin；节点或连接创建失败时返回 nullptr。
         */
        UEdGraphPin* BuildTransitionGateNode(
            const FSekiroAnimIRTransition& Transition,
            const int32 GateIndex,
            UAnimationTransitionGraph& Graph,
            UAnimStateNode& SourceState,
            UEdGraphPin& SelfPin,
            UEdGraphPin& LuaBoolPin)
        {
            const FSekiroAnimIRTransitionGateNode& GateNode = Transition.Gate.Nodes[GateIndex];
            if (GateNode.Type == TEXT("LuaBool")) return &LuaBoolPin;
            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            if (GateNode.Type == TEXT("TimeRemainingLessEqual"))
            {
                UFunction* RelevantTimeFunction = UAnimInstance::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UAnimInstance, GetRelevantAnimTimeRemaining));
                if (RelevantTimeFunction == nullptr)
                {
                    AddError(Diagnostics, K2FunctionNotFound,
                        TEXT("GetRelevantAnimTimeRemaining function was not found."),
                        Transition.Id, Transition.SourceLocation);
                    return nullptr;
                }

                FGraphNodeCreator<UK2Node_AnimGetter> GetterCreator(Graph);
                UK2Node_AnimGetter* Getter = GetterCreator.CreateNode(false);
                if (Getter == nullptr) return nullptr;
                Getter->SourceStateNode = &SourceState;
                Getter->SourceNode = Cast<UAnimGraphNode_StateMachine>(SourceState.GetGraph()->GetOuter());
                Getter->GetterClass = Preflight.ParentClass;
                Getter->SourceAnimBlueprint = &Blueprint;
                Getter->Contexts.Add(TEXT("Transition"));
                Getter->SetFromFunction(RelevantTimeFunction);
                GetterCreator.Finalize();
                Getter->NodeGuid = MakeStableGuid(TEXT("GateTime"), Transition.Id + FString::FromInt(GateIndex));
                UFunction* CompareFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, LessEqual_DoubleDouble));
                UK2Node_CallFunction* Compare = CreateCallFunctionNode(
                    Graph, TEXT("Gate.TimeCompare.") + Transition.Id + FString::FromInt(GateIndex), CompareFunction, 360, GateIndex * 120);
                UEdGraphPin* A = Compare != nullptr ? Compare->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                UEdGraphPin* B = Compare != nullptr ? Compare->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                UEdGraphPin* GetterOutput = Getter->GetReturnValuePin();
                if (A == nullptr || B == nullptr || GetterOutput == nullptr)
                {
                    AddError(Diagnostics, K2PinNotFound,
                        FString::Printf(
                            TEXT("TimeRemaining Gate Pins are incomplete (Getter=%s, A=%s, B=%s)."),
                            GetterOutput != nullptr ? TEXT("valid") : TEXT("missing"),
                            A != nullptr ? TEXT("valid") : TEXT("missing"),
                            B != nullptr ? TEXT("valid") : TEXT("missing")),
                        Transition.Id, Transition.SourceLocation);
                    return nullptr;
                }
                if (!Schema->TryCreateConnection(GetterOutput, A))
                {
                    AddError(Diagnostics, K2ConnectionFailed,
                        FString::Printf(
                            TEXT("TimeRemaining Gate rejected Getter-to-compare connection (%s/%s -> %s/%s)."),
                            *GetterOutput->PinType.PinCategory.ToString(),
                            *GetterOutput->PinType.PinSubCategory.ToString(),
                            *A->PinType.PinCategory.ToString(),
                            *A->PinType.PinSubCategory.ToString()),
                        Transition.Id, Transition.SourceLocation);
                    return nullptr;
                }
                Schema->TrySetDefaultValue(*B, FString::SanitizeFloat(GateNode.Threshold), false);
                UEdGraphPin* CompareResult = Compare->GetReturnValuePin();
                if (CompareResult == nullptr)
                {
                    AddError(Diagnostics, K2PinNotFound,
                        TEXT("TimeRemaining Gate comparison has no return Pin."),
                        Transition.Id, Transition.SourceLocation);
                }
                return CompareResult;
            }
            if (GateNode.Type == TEXT("CurveGreaterEqual"))
            {
                UFunction* CurveFunction = UAnimInstance::StaticClass()->FindFunctionByName(
                    TEXT("GetCurveValue"));
                UK2Node_CallFunction* Curve = CreateCallFunctionNode(
                    Graph, TEXT("Gate.Curve.") + Transition.Id + FString::FromInt(GateIndex), CurveFunction, 120, GateIndex * 120);
                UEdGraphPin* CurveSelf = Curve != nullptr ? Curve->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input) : nullptr;
                UEdGraphPin* CurveName = Curve != nullptr ? Curve->FindPin(TEXT("CurveName"), EGPD_Input) : nullptr;
                if (CurveSelf == nullptr || CurveName == nullptr || !Schema->TryCreateConnection(&SelfPin, CurveSelf)) return nullptr;
                Schema->TrySetDefaultValue(*CurveName, GateNode.Name.ToString(), false);
                UFunction* CompareFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, GreaterEqual_DoubleDouble));
                UK2Node_CallFunction* Compare = CreateCallFunctionNode(
                    Graph, TEXT("Gate.CurveCompare.") + Transition.Id + FString::FromInt(GateIndex), CompareFunction, 360, GateIndex * 120);
                UEdGraphPin* A = Compare != nullptr ? Compare->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                UEdGraphPin* B = Compare != nullptr ? Compare->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                if (A == nullptr || B == nullptr || !Schema->TryCreateConnection(Curve->GetReturnValuePin(), A)) return nullptr;
                Schema->TrySetDefaultValue(*B, FString::SanitizeFloat(GateNode.Threshold), false);
                return Compare->GetReturnValuePin();
            }

            UEdGraphPin* Accumulator = BuildTransitionGateNode(
                Transition, GateNode.Children[0], Graph, SourceState, SelfPin, LuaBoolPin);
            if (Accumulator == nullptr) return nullptr;
            if (GateNode.Type == TEXT("Not"))
            {
                UFunction* NotFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool));
                UK2Node_CallFunction* NotNode = CreateCallFunctionNode(
                    Graph, TEXT("Gate.Not.") + Transition.Id + FString::FromInt(GateIndex), NotFunction, 360, GateIndex * 120);
                UEdGraphPin* Input = NotNode != nullptr ? NotNode->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                if (Input == nullptr || !Schema->TryCreateConnection(Accumulator, Input)) return nullptr;
                return NotNode->GetReturnValuePin();
            }

            UFunction* CombineFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                GateNode.Type == TEXT("All")
                    ? GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanAND)
                    : GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanOR));
            for (int32 ChildOffset = 1; ChildOffset < GateNode.Children.Num(); ++ChildOffset)
            {
                UEdGraphPin* Child = BuildTransitionGateNode(
                    Transition, GateNode.Children[ChildOffset], Graph, SourceState, SelfPin, LuaBoolPin);
                UK2Node_CallFunction* Combine = CreateCallFunctionNode(
                    Graph, TEXT("Gate.Combine.") + Transition.Id + FString::FromInt(GateIndex) + TEXT(".") + FString::FromInt(ChildOffset),
                    CombineFunction, 480, (GateIndex + ChildOffset) * 120);
                UEdGraphPin* A = Combine != nullptr ? Combine->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                UEdGraphPin* B = Combine != nullptr ? Combine->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                if (Child == nullptr || A == nullptr || B == nullptr
                    || !Schema->TryCreateConnection(Accumulator, A)
                    || !Schema->TryCreateConnection(Child, B)) return nullptr;
                Accumulator = Combine->GetReturnValuePin();
            }
            return Accumulator;
        }

        /**
         * 将有 Entry 或 Transition 连接的有效状态排成紧凑近方形网格，列数取有效自动状态数平方根的上取整。
         * 没有任何连接的状态不参与主网格尺寸计算，而是在主网格下方单独成组；显式 Grid 状态保持固定坐标。
         *
         * @param Graph 待排版的 StateMachine IR Graph。
         * @param NativeGraph 持有 Entry 和 State 节点的原生状态机 Graph。
         * @param StateNodes 状态稳定 ID 到原生 State 节点的映射。
         * @param ExplicitPositions Lua Grid 提供的固定画布坐标。
         * @return 无返回值；函数直接修改 State 和 Entry 节点的位置。
         */
        void ApplyHierarchicalStateMachineLayout(
            const FSekiroAnimIRGraph& Graph,
            UAnimationStateMachineGraph& NativeGraph,
            const TMap<FString, UAnimStateNode*>& StateNodes,
            const TMap<FString, FIntPoint>& ExplicitPositions)
        {
            constexpr int32 StateHorizontalSpacing = 300;
            constexpr int32 StateVerticalSpacing = 170;
            constexpr int32 EntryHorizontalOffset = 240;
            constexpr int32 DisconnectedSectionSpacing = 260;

            TSet<FString> EffectiveStateIds;
            if (!Graph.StateMachine.EntryStateId.IsEmpty())
            {
                EffectiveStateIds.Add(Graph.StateMachine.EntryStateId);
            }
            for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                EffectiveStateIds.Add(Transition.SourceStateId);
                EffectiveStateIds.Add(Transition.TargetStateId);
            }

            TArray<const FSekiroAnimIRState*> OrderedStates;
            for (const FSekiroAnimIRState& State : Graph.StateMachine.States) OrderedStates.Add(&State);
            OrderedStates.Sort([](const FSekiroAnimIRState& Left, const FSekiroAnimIRState& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            TArray<const FSekiroAnimIRState*> AutomaticEffectiveStates;
            TArray<const FSekiroAnimIRState*> DisconnectedStates;
            for (const FSekiroAnimIRState* State : OrderedStates)
            {
                if (ExplicitPositions.Contains(State->Id)) continue;
                if (EffectiveStateIds.Contains(State->Id)) AutomaticEffectiveStates.Add(State);
                else DisconnectedStates.Add(State);
            }

            TSet<FIntPoint> Occupied;
            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions) Occupied.Add(Pair.Value);
            const int32 EffectiveColumns = FMath::Max(
                1,
                FMath::CeilToInt(FMath::Sqrt(static_cast<float>(AutomaticEffectiveStates.Num()))));
            for (int32 Index = 0; Index < AutomaticEffectiveStates.Num(); ++Index)
            {
                FIntPoint Position(
                    (Index % EffectiveColumns) * StateHorizontalSpacing,
                    (Index / EffectiveColumns) * StateVerticalSpacing);
                while (Occupied.Contains(Position)) Position.Y += StateVerticalSpacing;
                Occupied.Add(Position);
                UAnimStateNode* const* StateNode = StateNodes.Find(AutomaticEffectiveStates[Index]->Id);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                (*StateNode)->NodePosX = Position.X;
                (*StateNode)->NodePosY = Position.Y;
            }

            const int32 EffectiveRows = AutomaticEffectiveStates.Num() > 0
                ? FMath::DivideAndRoundUp(AutomaticEffectiveStates.Num(), EffectiveColumns)
                : 0;
            const int32 DisconnectedColumns = FMath::Max(
                1,
                FMath::CeilToInt(FMath::Sqrt(static_cast<float>(DisconnectedStates.Num()))));
            const int32 DisconnectedOriginY = EffectiveRows * StateVerticalSpacing + DisconnectedSectionSpacing;
            for (int32 Index = 0; Index < DisconnectedStates.Num(); ++Index)
            {
                FIntPoint Position(
                    (Index % DisconnectedColumns) * StateHorizontalSpacing,
                    DisconnectedOriginY + (Index / DisconnectedColumns) * StateVerticalSpacing);
                while (Occupied.Contains(Position)) Position.Y += StateVerticalSpacing;
                Occupied.Add(Position);
                UAnimStateNode* const* StateNode = StateNodes.Find(DisconnectedStates[Index]->Id);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                (*StateNode)->NodePosX = Position.X;
                (*StateNode)->NodePosY = Position.Y;
            }

            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions)
            {
                UAnimStateNode* const* StateNode = StateNodes.Find(Pair.Key);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                (*StateNode)->NodePosX = Pair.Value.X;
                (*StateNode)->NodePosY = Pair.Value.Y;
            }

            UAnimStateNode* const* EntryStateNode = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode != nullptr && EntryStateNode != nullptr && *EntryStateNode != nullptr)
            {
                NativeGraph.EntryNode->NodePosX = (*EntryStateNode)->NodePosX - EntryHorizontalOffset;
                NativeGraph.EntryNode->NodePosY = (*EntryStateNode)->NodePosY;
            }
        }

        /**
         * 将 StateMachine 显式 Grid 作为固定锚点，并按选定风格排列其余 State。
         * HierarchicalBlocks 使用紧凑近方形网格；传统流式风格仍从 Entry 做 BFS 分层。
         */
        void ApplyStateMachineLayout(
            const FSekiroAnimIRGraph& Graph,
            UAnimationStateMachineGraph& NativeGraph,
            const TMap<FString, UAnimStateNode*>& StateNodes)
        {
            ESekiroAnimIRLayoutStyle Style = Graph.Layout.Style;
            const TMap<FString, FIntPoint> ExplicitPositions = BuildExplicitLayoutPositions(Graph);
            if (Style == ESekiroAnimIRLayoutStyle::Auto
                || Style == ESekiroAnimIRLayoutStyle::HierarchicalBlocks)
            {
                ApplyHierarchicalStateMachineLayout(Graph, NativeGraph, StateNodes, ExplicitPositions);
                return;
            }

            TMap<FString, int32> Depths;
            TArray<FString> Queue;
            if (!Graph.StateMachine.EntryStateId.IsEmpty())
            {
                Depths.Add(Graph.StateMachine.EntryStateId, 0);
                Queue.Add(Graph.StateMachine.EntryStateId);
            }
            for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
            {
                const FString& Current = Queue[QueueIndex];
                const int32 CurrentDepth = Depths[Current];
                for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    if (Transition.SourceStateId != Current || Depths.Contains(Transition.TargetStateId)) continue;
                    Depths.Add(Transition.TargetStateId, CurrentDepth + 1);
                    Queue.Add(Transition.TargetStateId);
                }
            }

            TArray<const FSekiroAnimIRState*> OrderedStates;
            for (const FSekiroAnimIRState& State : Graph.StateMachine.States) OrderedStates.Add(&State);
            OrderedStates.Sort([](const FSekiroAnimIRState& Left, const FSekiroAnimIRState& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });
            TMap<int32, int32> LayerCounts;
            int32 FallbackDepth = Depths.Num();
            TSet<FIntPoint> Occupied;
            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions) Occupied.Add(Pair.Value);
            for (int32 StateIndex = 0; StateIndex < OrderedStates.Num(); ++StateIndex)
            {
                const FSekiroAnimIRState& State = *OrderedStates[StateIndex];
                UAnimStateNode* const* StateNode = StateNodes.Find(State.Id);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                const FIntPoint* Explicit = ExplicitPositions.Find(State.Id);
                if (Explicit != nullptr)
                {
                    (*StateNode)->NodePosX = Explicit->X;
                    (*StateNode)->NodePosY = Explicit->Y;
                    continue;
                }

                const int32 Depth = Depths.Contains(State.Id) ? Depths[State.Id] : FallbackDepth++;
                const int32 SecondaryIndex = LayerCounts.FindOrAdd(Depth)++;
                FIntPoint Position;
                if (Style == ESekiroAnimIRLayoutStyle::Radial)
                {
                    const float Angle = static_cast<float>(SecondaryIndex) * 1.57079632679f;
                    const float Radius = static_cast<float>(FMath::Max(1, Depth)) * 420.0f;
                    Position = FIntPoint(
                        FMath::RoundToInt(FMath::Cos(Angle) * Radius),
                        FMath::RoundToInt(FMath::Sin(Angle) * Radius));
                    if (Depth == 0) Position = FIntPoint::ZeroValue;
                }
                else
                {
                    Position = MakeFlowPosition(Style, Depth, SecondaryIndex, StateIndex);
                    if (Style == ESekiroAnimIRLayoutStyle::LeftToRight
                        || Style == ESekiroAnimIRLayoutStyle::RightToLeft)
                    {
                        Position.X = -Position.X;
                    }
                }
                while (Occupied.Contains(Position)) Position.Y += 260;
                Occupied.Add(Position);
                (*StateNode)->NodePosX = Position.X;
                (*StateNode)->NodePosY = Position.Y;
            }

            UAnimStateNode* const* EntryStateNode = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode != nullptr && EntryStateNode != nullptr && *EntryStateNode != nullptr)
            {
                NativeGraph.EntryNode->NodePosX = (*EntryStateNode)->NodePosX - 280;
                NativeGraph.EntryNode->NodePosY = (*EntryStateNode)->NodePosY;
            }
        }

        /**
         * 物化一个原生 StateMachine Graph，包括 Entry、State、Transition 及各 StatePose 子图。
         * State/Transition 均经 FGraphNodeCreator::Finalize 触发 UE 默认 BoundGraph 生命周期；Transition Rule 按需直接调用 Lua。
         * 必须在游戏线程调用，NativeGraph 及其 SubGraphs 在调用期间由本 Builder 独占。
         *
         * @param Graph StateMachine 类型 IR Graph。
         * @param NativeGraph StateMachine 节点自动创建的原生 Graph。
         * @return 拓扑与全部嵌套 StatePose 成功物化时返回 true。
         */
        bool BuildStateMachineGraph(
            const FSekiroAnimIRGraph& Graph,
            UAnimationStateMachineGraph& NativeGraph)
        {
            NativeGraphs.Add(Graph.Id, &NativeGraph);
            TMap<FString, UAnimStateNode*> StateNodes;
            int32 StateIndex = 0;
            for (const FSekiroAnimIRState& State : Graph.StateMachine.States)
            {
                UAnimStateNode* StateNode = CreateNativeNode<UAnimStateNode>(
                    NativeGraph,
                    State.Id,
                    StateIndex * 300,
                    100);
                if (StateNode == nullptr || StateNode->BoundGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeNodeCreationFailed,
                        TEXT("Failed to create native animation state and its BoundGraph."),
                        State.Id,
                        State.SourceLocation);
                    return false;
                }

                StateNode->NodeGuid = MakeStableGuid(TEXT("State"), State.Id);
                StateNode->bAlwaysResetOnEntry = State.bAlwaysResetOnEntry;
                const FSekiroAnimIRGraph* StateGraph = FindGraph(State.GraphId);
                if (StateGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeGraphMissing,
                        TEXT("StatePose Graph could not be found after validation."),
                        State.GraphId,
                        State.SourceLocation);
                    return false;
                }

                UAnimationStateGraph* NativeStateGraph = Cast<UAnimationStateGraph>(StateNode->BoundGraph);
                if (NativeStateGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeGraphMissing,
                        TEXT("UAnimStateNode did not create UAnimationStateGraph."),
                        State.GraphId,
                        State.SourceLocation);
                    return false;
                }

                NativeStateGraph->GraphGuid = MakeStableGuid(TEXT("Graph"), StateGraph->Id);
                const FString DesiredStateName = !State.Name.IsEmpty() ? State.Name : StateGraph->Name;
                FEdGraphUtilities::RenameGraphToNameOrCloseToName(NativeStateGraph, DesiredStateName);
                NativeGraphs.Add(StateGraph->Id, NativeStateGraph);
                StateNodes.Add(State.Id, StateNode);
                ++StateIndex;
            }

            for (const FSekiroAnimIRState& State : Graph.StateMachine.States)
            {
                UAnimStateNode* const* StateNode = StateNodes.Find(State.Id);
                const FSekiroAnimIRGraph* StateGraph = FindGraph(State.GraphId);
                UAnimationStateGraph* NativeStateGraph = StateNode != nullptr
                    ? Cast<UAnimationStateGraph>((*StateNode)->BoundGraph)
                    : nullptr;
                if (StateGraph == nullptr || NativeStateGraph == nullptr)
                {
                    AddError(Diagnostics, NativeGraphMissing,
                        TEXT("StatePose Graph mapping disappeared during materialization."),
                        State.GraphId, State.SourceLocation);
                    return false;
                }
                if (!BuildPoseGraph(*StateGraph, *NativeStateGraph)) return false;
            }

            ApplyStateMachineLayout(Graph, NativeGraph, StateNodes);

            UAnimStateNode* const* EntryState = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode == nullptr
                || NativeGraph.EntryNode->Pins.Num() == 0
                || EntryState == nullptr
                || (*EntryState)->GetInputPin() == nullptr
                || !NativeGraph.GetSchema()->TryCreateConnection(
                    NativeGraph.EntryNode->Pins[0],
                    (*EntryState)->GetInputPin()))
            {
                AddError(
                    Diagnostics,
                    ConnectionFailed,
                    TEXT("Failed to connect StateMachine Entry to EntryState."),
                    Graph.StateMachine.EntryStateId,
                    Graph.SourceLocation);
                return false;
            }

            int32 TransitionIndex = 0;
            for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                UAnimStateNode* const* SourceState = StateNodes.Find(Transition.SourceStateId);
                UAnimStateNode* const* TargetState = StateNodes.Find(Transition.TargetStateId);
                if (SourceState == nullptr || TargetState == nullptr)
                {
                    AddError(
                        Diagnostics,
                        ConnectionFailed,
                        TEXT("Transition source or target State is missing after validation."),
                        Transition.Id,
                        Transition.SourceLocation);
                    return false;
                }

                UAnimStateTransitionNode* TransitionNode = CreateNativeNode<UAnimStateTransitionNode>(
                    NativeGraph,
                    Transition.Id,
                    ((*SourceState)->NodePosX + (*TargetState)->NodePosX) / 2,
                    ((*SourceState)->NodePosY + (*TargetState)->NodePosY) / 2
                        + ((TransitionIndex % 2 == 0) ? -60 : 60));
                if (TransitionNode == nullptr || TransitionNode->BoundGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeNodeCreationFailed,
                        TEXT("Failed to create native Transition and its BoundGraph."),
                        Transition.Id,
                        Transition.SourceLocation);
                    return false;
                }

                TransitionNode->NodeGuid = MakeStableGuid(TEXT("Transition"), Transition.Id);
                TransitionNode->CrossfadeDuration = Transition.Settings.BlendDuration;
                TransitionNode->PriorityOrder = Transition.Settings.PriorityOrder;
                TransitionNode->BlendMode = EAlphaBlendOption::Linear;
                TransitionNode->CreateConnections(*SourceState, *TargetState);
                TransitionNode->BoundGraph->GraphGuid =
                    MakeStableGuid(TEXT("TransitionGraph"), Transition.Id);
                FEdGraphUtilities::RenameGraphToNameOrCloseToName(
                    TransitionNode->BoundGraph,
                    Transition.Key);
                if (!BuildTransitionRuleGraph(Transition, *TransitionNode, **SourceState)) return false;
                ++TransitionIndex;
            }

            return true;
        }

        /**
         * 将 IR Link 的注册 Pin 名解析为原生 Pin，并交由当前 AnimationGraph Schema 建立连接。
         * 函数不直接调用 MakeLinkTo，确保 UE 类型、方向和单连接规则保持权威。
         * 必须在游戏线程调用，NativeGraph 在调用期间由当前 Builder 独占。
         *
         * @param Link 待建立的有向 Pose Link。
         * @param NativeGraph Link 所属原生 Graph。
         * @return 两端 Pin 存在且 Schema 接受连接时返回 true。
         */
        bool ConnectPoseLink(const FSekiroAnimIRLink& Link, UAnimationGraph& NativeGraph)
        {
            UEdGraphNode* const* SourceNode = NativeNodes.Find(Link.Source.NodeId);
            UEdGraphNode* const* TargetNode = NativeNodes.Find(Link.Target.NodeId);
            UEdGraphPin* SourcePin = SourceNode != nullptr
                ? FindNativePin(Link.Source.NodeId, **SourceNode, Link.Source.PinName, EGPD_Output)
                : nullptr;
            UEdGraphPin* TargetPin = TargetNode != nullptr
                ? FindNativePin(Link.Target.NodeId, **TargetNode, Link.Target.PinName, EGPD_Input)
                : nullptr;
            if (SourcePin == nullptr || TargetPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    NativePinNotFound,
                    FString::Printf(
                        TEXT("Native Pin lookup failed for Link '%s' (%s.%s -> %s.%s)."),
                        *Link.Id,
                        *Link.Source.NodeId,
                        *Link.Source.PinName,
                        *Link.Target.NodeId,
                        *Link.Target.PinName),
                    Link.Id,
                    Link.SourceLocation);
                return false;
            }

            if (!NativeGraph.GetSchema()->TryCreateConnection(SourcePin, TargetPin))
            {
                AddError(
                    Diagnostics,
                    ConnectionFailed,
                    FString::Printf(TEXT("AnimationGraph Schema rejected Link '%s'."), *Link.Id),
                    Link.Id,
                    Link.SourceLocation);
                return false;
            }

            return true;
        }

        /**
         * 将稳定 IR Pin 别名解析为 UE5.2 节点实际 Pin 名，覆盖 VariableGet 与动态 BlendList。
         * 只能在游戏线程、节点完成 Reconstruct 后调用；不会创建或修改 Pin。
         *
         * @param NodeId 节点稳定 ID，用于查询 IR NodeType 和 PropertyName。
         * @param NativeNode 已物化的原生节点。
         * @param IRPinName Lua DSL 使用的稳定 Pin 名。
         * @param Direction 期望的原生 Pin 方向。
         * @return 匹配的原生 Pin；不存在时返回 nullptr。
         */
        UEdGraphPin* FindNativePin(
            const FString& NodeId,
            UEdGraphNode& NativeNode,
            const FString& IRPinName,
            const EEdGraphPinDirection Direction) const
        {
            const FSekiroAnimIRNode* const* IRNode = NodesById.Find(NodeId);
            if (IRNode == nullptr) return nullptr;
            FString NativePinName = IRPinName;
            if ((*IRNode)->NodeType == SekiroAnimGraphIRNames::BoolPropertyGetterNode
                || (*IRNode)->NodeType == SekiroAnimGraphIRNames::FloatPropertyGetterNode
                || (*IRNode)->NodeType == SekiroAnimGraphIRNames::BytePropertyGetterNode
                || (*IRNode)->NodeType == SekiroAnimGraphIRNames::EnumPropertyGetterNode)
            {
                const FSekiroAnimIRProperty* PropertyName = FindProperty(**IRNode, TEXT("PropertyName"));
                if (PropertyName != nullptr) NativePinName = PropertyName->Value.NameValue.ToString();
            }
            else if ((*IRNode)->NodeType == SekiroAnimGraphIRNames::BlendListByBoolNode)
            {
                if (IRPinName == TEXT("TruePose")) NativePinName = TEXT("BlendPose_0");
                else if (IRPinName == TEXT("FalsePose")) NativePinName = TEXT("BlendPose_1");
                else if (IRPinName == TEXT("ActiveValue")) NativePinName = TEXT("bActiveValue");
            }
            else if ((*IRNode)->NodeType == SekiroAnimGraphIRNames::BlendListByEnumNode)
            {
                if (IRPinName == TEXT("DefaultPose")) NativePinName = TEXT("BlendPose_0");
                else if (IRPinName.StartsWith(TEXT("Pose")) && IRPinName.Len() > 4)
                {
                    NativePinName = FString::Printf(TEXT("BlendPose_%d"), FCString::Atoi(*IRPinName.Mid(4)) + 1);
                }
                else if (IRPinName == TEXT("ActiveValue")) NativePinName = TEXT("ActiveEnumValue");
            }
            else if ((*IRNode)->NodeType == SekiroAnimGraphIRNames::LayeredBlendPerBoneNode)
            {
                if (IRPinName == TEXT("BlendPose")) NativePinName = TEXT("BlendPoses_0");
                else if (IRPinName == TEXT("BlendWeight")) NativePinName = TEXT("BlendWeights_0");
            }
            UEdGraphPin* Pin = NativeNode.FindPin(NativePinName, Direction);
            if (Pin == nullptr && IRPinName == TEXT("ActiveValue"))
            {
                Pin = NativeNode.FindPin(IRPinName, Direction);
            }
            return Pin;
        }

        const FPreflightData& Preflight;
        UAnimBlueprint& Blueprint;
        TArray<FSekiroAnimIRDiagnostic>& Diagnostics;
        const FSekiroAnimIRLayer& Layer;
        TMap<FString, const FSekiroAnimIRGraph*> GraphsById;
        TMap<FString, const FSekiroAnimIRNode*> NodesById;
        TMap<FString, UEdGraph*> NativeGraphs;
        TMap<FString, UEdGraphNode*> NativeNodes;
    };

    /**
     * 将创建失败或编译失败的新蓝图从资产可见集合中移除并标记回收。
     * 函数仅处理本次调用刚创建且尚未通知 AssetRegistry 的对象，不删除磁盘文件或既有资产。
     * 必须在游戏线程调用。
     *
     * @param Blueprint 待丢弃的新建蓝图，可为空。
     * @return 无返回值。
     */
    void DiscardCreatedBlueprint(UAnimBlueprint* Blueprint)
    {
        if (Blueprint == nullptr) return;
        Blueprint->ClearFlags(RF_Public | RF_Standalone);
        Blueprint->SetFlags(RF_Transient);
        Blueprint->MarkAsGarbage();
    }

    /**
     * 返回 Blueprint 中由原生 AnimBlueprintFactory 创建的主 AnimGraph，不返回 State BoundGraph。
     * 只能在游戏线程调用；函数只读对象数组，不改变 Graph 所有权。
     *
     * @param Blueprint 待查询的标准动画蓝图。
     * @return 找到时返回主 UAnimationGraph，否则返回 nullptr。
     */
    UAnimationGraph* FindMainAnimationGraph(UAnimBlueprint& Blueprint)
    {
        for (UEdGraph* FunctionGraph : Blueprint.FunctionGraphs)
        {
            UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(FunctionGraph);
            if (AnimationGraph != nullptr && !AnimationGraph->IsA<UAnimationStateGraph>())
            {
                return AnimationGraph;
            }
        }

        return nullptr;
    }

    /**
     * 确保 Lua 物化器所需的 UE 原生主 AnimGraph、EventGraph 和默认 Root 节点存在。
     * 缺失图按 FKismetEditorUtilities 创建新动画蓝图时使用的同一 API 重建；已有图保持对象身份不变。
     * 必须在游戏线程和编辑器事务内调用。本函数不生成 Lua 节点，也不执行 Blueprint 编译。
     *
     * @param Blueprint 待修复外壳的标准动画蓝图。
     * @return 主 AnimGraph 与 EventGraph 均存在且主图包含默认 Root 时返回 true。
     */
    bool EnsureLuaBlueprintShell(UAnimBlueprint& Blueprint)
    {
        UAnimationGraph* MainGraph = FindMainAnimationGraph(Blueprint);
        if (MainGraph == nullptr)
        {
            MainGraph = Cast<UAnimationGraph>(FBlueprintEditorUtils::CreateNewGraph(
                &Blueprint,
                UEdGraphSchema_K2::GN_AnimGraph,
                UAnimationGraph::StaticClass(),
                UAnimationGraphSchema::StaticClass()));
            if (MainGraph == nullptr) return false;

            FBlueprintEditorUtils::AddDomainSpecificGraph(&Blueprint, MainGraph);
            Blueprint.LastEditedDocuments.AddUnique(MainGraph);
            MainGraph->bAllowDeletion = false;
        }

        bool bHasRootNode = false;
        for (UEdGraphNode* Node : MainGraph->Nodes)
        {
            if (Node != nullptr && Node->IsA<UAnimGraphNode_Root>())
            {
                bHasRootNode = true;
                break;
            }
        }
        if (!bHasRootNode)
        {
            const UEdGraphSchema* Schema = MainGraph->GetSchema();
            if (Schema == nullptr) return false;
            Schema->CreateDefaultNodesForGraph(*MainGraph);
        }

        if (FBlueprintEditorUtils::FindEventGraph(&Blueprint) == nullptr)
        {
            FKismetEditorUtilities::CreateDefaultEventGraphs(&Blueprint);
        }
        return FBlueprintEditorUtils::FindEventGraph(&Blueprint) != nullptr;
    }

    /**
     * 清理一个完全由 Lua 拥有的 AnimBlueprint 的变量、主 AnimGraph 非 Root 节点和 EventGraph 节点。
     * 主图与 EventGraph UObject 本身尽量保留；缺失时先按 UE 原生创建流程恢复图外壳和默认 Root。
     * 必须在游戏线程且编辑器事务已开启时调用；函数不编译 Blueprint。
     *
     * @param Blueprint 待原地重建的标准动画蓝图，调用方保证其全部业务 Graph 由 Lua 管理。
     * @param OutDiagnostics 接收缺失原生基础图的稳定错误。
     * @param SourceLocation Lua 模块根声明位置，用于定位诊断。
     * @return 基础图已恢复并清理时返回 true，否则不继续物化并返回 false。
     */
    bool ResetLuaOwnedBlueprint(
        UAnimBlueprint& Blueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        Blueprint.Modify();
        if (!EnsureLuaBlueprintShell(Blueprint))
        {
            AddError(
                OutDiagnostics,
                InPlaceCommitFailed,
                TEXT("The target AnimBlueprint native AnimGraph/EventGraph shell could not be restored."),
                Blueprint.GetPathName(),
                SourceLocation);
            return false;
        }

        UAnimationGraph* MainGraph = FindMainAnimationGraph(Blueprint);
        UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
        if (MainGraph == nullptr || EventGraph == nullptr) return false;
        MainGraph->Modify();
        EventGraph->Modify();

        TArray<FName> VariableNames;
        VariableNames.Reserve(Blueprint.NewVariables.Num());
        for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
        {
            VariableNames.Add(Variable.VarName);
        }
        FBlueprintEditorUtils::BulkRemoveMemberVariables(&Blueprint, VariableNames);

        const TArray<UEdGraphNode*> MainNodes = MainGraph->Nodes;
        for (UEdGraphNode* Node : MainNodes)
        {
            if (Node == nullptr || Node->IsA<UAnimGraphNode_Root>()) continue;
            Node->Modify();
            FBlueprintEditorUtils::RemoveNode(&Blueprint, Node, true);
        }

        const TArray<UEdGraphNode*> EventNodes = EventGraph->Nodes;
        for (UEdGraphNode* Node : EventNodes)
        {
            if (Node == nullptr) continue;
            Node->Modify();
            FBlueprintEditorUtils::RemoveNode(&Blueprint, Node, true);
        }

        return true;
    }

    /**
     * 编译已完成 Graph 物化的标准 AnimBlueprint，并把原生编译错误转换为稳定 Factory 诊断。
     * 必须在游戏线程调用；成功会替换 GeneratedClass，失败可能留下待事务回滚的编辑器中间状态。
     *
     * @param Blueprint 待编译的标准动画蓝图。
     * @param Preflight 当前 Lua IR 的规范化预检数据。
     * @param OutDiagnostics 接收编译错误。
     * @return 原生 AnimBlueprint 编译无错误且 GeneratedClass 有效时返回 true。
     */
    bool CompileNativeBlueprint(
        UAnimBlueprint& Blueprint,
        const FPreflightData& Preflight,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
        FCompilerResultsLog CompileResults;
        CompileResults.bSilentMode = true;
        FKismetEditorUtilities::CompileBlueprint(
            &Blueprint,
            EBlueprintCompileOptions::SkipGarbageCollection,
            &CompileResults);
        if (CompileResults.NumErrors == 0
            && Blueprint.Status != BS_Error
            && Blueprint.GeneratedClass != nullptr)
        {
            return true;
        }

        AddError(
            OutDiagnostics,
            BlueprintCompileFailed,
            FString::Printf(
                TEXT("Generated AnimBlueprint failed native compilation with %d error(s)."),
                CompileResults.NumErrors),
            Preflight.Blueprint.SourceModule,
            Preflight.Blueprint.SourceLocation);
        return false;
    }

    /**
     * 结束当前重建事务并立即撤销，以恢复准备阶段修改前的 Graph。
     * 只能在游戏线程调用；本函数不触发 Blueprint 编译，避免在原生编译前钩子内递归。
     *
     * @param Blueprint 需要恢复旧 Graph 的目标动画蓝图。
     * @return 事务撤销成功时返回 true；GEditor 不可用或撤销失败时返回 false。
     */
    bool RollbackInPlaceTransaction(UAnimBlueprint& Blueprint)
    {
        if (GEditor == nullptr) return false;
        GEditor->EndTransaction();
        return GEditor->UndoTransaction(false);
    }

    /**
     * 在预检完成后调用 UAnimBlueprintFactory，物化原生 Graph 并执行完整 AnimBlueprint 编译。
     * 必须在游戏线程调用；新蓝图会关闭多线程动画更新，使 Rule Graph 可安全直接进入 UnLua。
     * 失败时清理新建蓝图且不通知 AssetRegistry。
     *
     * @param Preflight 已成功完成的规范化预检数据。
     * @param Outer 新蓝图所属 package。
     * @param AssetName 新蓝图对象名。
     * @param bTransient 是否创建仅用于测试或临时工作的对象。
     * @param OutDiagnostics 接收物化与编译诊断。
     * @return 成功编译的原生 AnimBlueprint；失败返回 nullptr。
     */
    UAnimBlueprint* MaterializeBlueprint(
        const FPreflightData& Preflight,
        UObject& Outer,
        const FName AssetName,
        const bool bTransient,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
    {
        UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>(GetTransientPackage());
        Factory->ParentClass = Preflight.ParentClass;
        Factory->TargetSkeleton = Preflight.TargetSkeleton;
        Factory->bTemplate = false;

        UObject* CreatedObject = Factory->FactoryCreateNew(
            UAnimBlueprint::StaticClass(),
            &Outer,
            AssetName,
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn,
            TEXT("SekiroAnimGraphIR"));
        UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(CreatedObject);
        if (AnimBlueprint == nullptr)
        {
            AddError(
                OutDiagnostics,
                BlueprintCreationFailed,
                TEXT("UAnimBlueprintFactory failed to create an AnimBlueprint."),
                Preflight.Blueprint.SourceModule,
                Preflight.Blueprint.SourceLocation);
            return nullptr;
        }

        AnimBlueprint->Modify();
        AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;

        if (bTransient)
        {
            AnimBlueprint->SetFlags(RF_Transient);
            AnimBlueprint->ClearFlags(RF_Standalone);
        }

        FNativeAnimBlueprintBuilder Builder(Preflight, *AnimBlueprint, OutDiagnostics);
        if (!Builder.Build() || HasErrors(OutDiagnostics))
        {
            DiscardCreatedBlueprint(AnimBlueprint);
            return nullptr;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
        FCompilerResultsLog CompileResults;
        CompileResults.bSilentMode = true;
        FKismetEditorUtilities::CompileBlueprint(
            AnimBlueprint,
            EBlueprintCompileOptions::SkipGarbageCollection,
            &CompileResults);
        if (CompileResults.NumErrors > 0
            || AnimBlueprint->Status == BS_Error
            || AnimBlueprint->GeneratedClass == nullptr)
        {
            AddError(
                OutDiagnostics,
                BlueprintCompileFailed,
                FString::Printf(
                    TEXT("Generated AnimBlueprint failed native compilation with %d error(s)."),
                    CompileResults.NumErrors),
                Preflight.Blueprint.SourceModule,
                Preflight.Blueprint.SourceLocation);
            DiscardCreatedBlueprint(AnimBlueprint);
            return nullptr;
        }

        return AnimBlueprint;
    }

    /**
     * 将已预检 IR 写入目标 AnimBlueprint 的现有 UObject，缺失图外壳由 ResetLuaOwnedBlueprint 自动恢复。
     * 可选 staging 仅供显式工具在进入原生编译前验证；编译前回调必须关闭 staging，避免嵌套原生编译。
     * 必须在游戏线程且 GEditor 可用时调用。函数在同一事务中关闭多线程动画更新，
     * 只准备 Graph，不调用目标 Blueprint 的原生编译。
     *
     * @param Blueprint 接收生成结构的标准动画蓝图。
     * @param Preflight 已完成资源解析和规范化的 IR 数据。
     * @param bRunNativeStagingCompile true 时先完整编译 transient staging 蓝图。
     * @param OutDiagnostics 接收 staging、Graph 重建和物化诊断。
     * @return 目标 Graph 已成功替换并标记结构变化时返回 true；失败时撤销本轮事务并返回 false。
     */
    bool BuildPreparedGraph(
        UAnimBlueprint& Blueprint,
        const FPreflightData& Preflight,
        const bool bRunNativeStagingCompile,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
    {
        if (GEditor == nullptr)
        {
            AddError(
                OutDiagnostics,
                TransactionUnavailable,
                TEXT("The editor transaction system is unavailable; refusing an unprotected Lua Graph rebuild."),
                Blueprint.GetPathName(),
                Preflight.Blueprint.SourceLocation);
            return false;
        }

        UAnimBlueprint* StagingBlueprint = nullptr;
        if (bRunNativeStagingCompile)
        {
            const FName StagingName = MakeUniqueObjectName(
                GetTransientPackage(),
                UAnimBlueprint::StaticClass(),
                TEXT("SekiroLuaAnimBlueprintStaging"));
            StagingBlueprint = MaterializeBlueprint(
                Preflight,
                *GetTransientPackage(),
                StagingName,
                true,
                OutDiagnostics);
            if (StagingBlueprint == nullptr) return false;
        }

        GEditor->BeginTransaction(
            TEXT("SekiroLuaAnimBlueprint"),
            NSLOCTEXT("SekiroLuaAnimBlueprint", "PrepareTransaction", "Prepare Lua Animation Blueprint Graph"),
            &Blueprint);
        Blueprint.Modify();
        Blueprint.bUseMultiThreadedAnimationUpdate = false;

        bool bPrepared = ResetLuaOwnedBlueprint(
            Blueprint,
            OutDiagnostics,
            Preflight.Blueprint.SourceLocation);
        if (bPrepared)
        {
            FNativeAnimBlueprintBuilder Builder(Preflight, Blueprint, OutDiagnostics);
            bPrepared = Builder.Build() && !HasErrors(OutDiagnostics);
        }
        if (bPrepared)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
            Blueprint.GetOutermost()->MarkPackageDirty();
            GEditor->EndTransaction();
        }
        else
        {
            RollbackInPlaceTransaction(Blueprint);
        }

        DiscardCreatedBlueprint(StagingBlueprint);
        return bPrepared;
    }
}

/**
 * 从已验证 IR 在 transient package 中创建、连接并编译 UE 原生 AnimBlueprint。
 * 函数在任何 UObject 创建前完成 Validator 与资源预检；只允许游戏线程调用。
 *
 * @param Blueprint 待物化的完整只读 IR，函数不会修改调用方数据。
 * @param OutDiagnostics 接收 Validator、预检、物化和编译诊断；调用开始时会清空。
 * @return 成功时返回 transient UAnimBlueprint，任一 Error 时返回 nullptr 且不产生可见资产。
 */
UAnimBlueprint* USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
    const FSekiroAnimBlueprintIR& Blueprint,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    FPreflightData Preflight;
    if (!PrepareBuild(Blueprint, Preflight, OutDiagnostics)) return nullptr;

    const FName AssetName = MakeUniqueObjectName(
        GetTransientPackage(),
        UAnimBlueprint::StaticClass(),
        TEXT("SekiroGeneratedAnimBlueprint"));
    return MaterializeBlueprint(
        Preflight,
        *GetTransientPackage(),
        AssetName,
        true,
        OutDiagnostics);
}

/**
 * 从已验证 IR 在指定内容目录创建、连接并编译 UE 原生 AnimBlueprint 资产对象。
 * PackagePath 是目录形式的长包名（例如 /Game/Animation），AssetName 是对象名；函数拒绝覆盖任何已加载或磁盘已有包。
 * 只允许游戏线程调用，成功后才通知 AssetRegistry 并标记 package dirty，不负责保存磁盘文件。
 *
 * @param Blueprint 待物化的完整只读 IR，函数不会修改调用方数据。
 * @param PackagePath 可写内容目录的长包路径，不包含资产名。
 * @param AssetName 新资产对象名。
 * @param OutDiagnostics 接收路径、Validator、预检、物化和编译诊断；调用开始时会清空。
 * @return 成功时返回新建 UAnimBlueprint；路径非法、已存在或任一构建 Error 时返回 nullptr。
 */
UAnimBlueprint* USekiroAnimBlueprintFactoryLibrary::CreateAnimBlueprintAsset(
    const FSekiroAnimBlueprintIR& Blueprint,
    const FString& PackagePath,
    const FString& AssetName,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    if (!IsInGameThread())
    {
        AddError(
            OutDiagnostics,
            WrongThread,
            TEXT("AnimBlueprint factory must run on the game thread."),
            Blueprint.SourceModule,
            Blueprint.SourceLocation);
        return nullptr;
    }

    FString NormalizedPath = PackagePath;
    NormalizedPath.RemoveFromEnd(TEXT("/"));
    const FString LongPackageName = NormalizedPath + TEXT("/") + AssetName;
    const FString ObjectPath = LongPackageName + TEXT(".") + AssetName;
    FText PathError;
    if (AssetName.IsEmpty()
        || !FPackageName::IsValidLongPackageName(LongPackageName, false, &PathError)
        || !FPackageName::IsValidObjectPath(ObjectPath))
    {
        AddError(
            OutDiagnostics,
            InvalidPackagePath,
            FString::Printf(
                TEXT("Invalid AnimBlueprint asset path '%s': %s"),
                *ObjectPath,
                *PathError.ToString()),
            ObjectPath,
            Blueprint.SourceLocation);
        return nullptr;
    }

    if (FPackageName::DoesPackageExist(LongPackageName)
        || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
    {
        AddError(
            OutDiagnostics,
            AssetAlreadyExists,
            FString::Printf(TEXT("Asset '%s' already exists; overwrite is disabled."), *ObjectPath),
            ObjectPath,
            Blueprint.SourceLocation);
        return nullptr;
    }

    FPreflightData Preflight;
    if (!PrepareBuild(Blueprint, Preflight, OutDiagnostics)) return nullptr;

    UPackage* Package = CreatePackage(*LongPackageName);
    UAnimBlueprint* AnimBlueprint = MaterializeBlueprint(
        Preflight,
        *Package,
        FName(*AssetName),
        false,
        OutDiagnostics);
    if (AnimBlueprint == nullptr) return nullptr;

    FAssetRegistryModule::AssetCreated(AnimBlueprint);
    Package->MarkPackageDirty();
    return AnimBlueprint;
}

/**
 * 将 Lua 模块编译为规范 IR，创建全新的原生 AnimBlueprint，并同步保存对应 package 文件。
 * 函数只接受长包目录与独立 AssetName，沿用 CreateAnimBlueprintAsset 的拒绝覆盖策略；不接受磁盘绝对路径。
 * 必须在游戏线程调用；保存失败会撤销 AssetRegistry 可见性并把新蓝图标记回收，但不会删除既有资产。
 *
 * @param LuaModuleName 交给 UnLua require 的模块名，同时作为生成 EventGraph 的规则模块名。
 * @param PackagePath 目标内容目录长包名，例如 /Game/Animation，不包含资产名。
 * @param AssetName 新 AnimBlueprint 对象名；已存在时明确失败。
 * @param OutDiagnostics 接收 Importer、Validator、Factory、编译和保存诊断；调用开始时会清空。
 * @return 编译且保存成功的新 UAnimBlueprint；任一步失败时返回 nullptr。
 */
UAnimBlueprint* USekiroAnimBlueprintFactoryLibrary::CompileLuaModuleToAnimBlueprintAsset(
    const FString& LuaModuleName,
    const FString& PackagePath,
    const FString& AssetName,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    FSekiroAnimBlueprintIR BlueprintIR;
    if (!USekiroAnimGraphIRLibrary::CompileLuaModule(
            LuaModuleName,
            BlueprintIR,
            OutDiagnostics))
    {
        return nullptr;
    }

    UAnimBlueprint* AnimBlueprint = CreateAnimBlueprintAsset(
        BlueprintIR,
        PackagePath,
        AssetName,
        OutDiagnostics);
    if (AnimBlueprint == nullptr) return nullptr;

    UPackage* Package = AnimBlueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(),
        FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, AnimBlueprint, *Filename, SaveArgs))
    {
        AddError(
            OutDiagnostics,
            SavePackageFailed,
            FString::Printf(TEXT("Failed to save generated AnimBlueprint package '%s'."), *Filename),
            Package->GetName(),
            BlueprintIR.SourceLocation);
        FAssetRegistryModule::AssetDeleted(AnimBlueprint);
        DiscardCreatedBlueprint(AnimBlueprint);
        return nullptr;
    }

    return AnimBlueprint;
}

/**
 * 为已有的标准 UAnimBlueprint 附加或更新 Lua 源扩展，并将资产标记为等待首次显式编译。
 * 函数配置源身份并立即关闭多线程动画更新，但不读取 Lua、不清理现有 Graph，也不执行原生编译；
 * 因此可先安全接管旧动画蓝图，
 * 再通过 CompileLuaAnimBlueprintInPlace 完成带 staging 和事务回滚的结构替换。
 * 必须在非 PIE 的游戏线程调用，且目标必须是精确 UAnimBlueprint 类型以继续使用 UE5.2 原生动画编译器。
 *
 * @param AnimBlueprint 要由 Lua 接管的现有标准动画蓝图；对象身份、路径、父类和 Skeleton 均保持不变。
 * @param LuaModuleName UnLua 模块名，例如 Animation.Sekiro.ABP_Sekiro；不能为空，也不是文件系统路径。
 * @return 扩展配置成功并将 Blueprint/package 标脏时返回 true；线程、PIE、对象类型或模块名无效时返回 false。
 */
bool USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
    UAnimBlueprint* AnimBlueprint,
    const FString& LuaModuleName)
{
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass()
        || LuaModuleName.IsEmpty()
        || (GEditor != nullptr && GEditor->PlayWorld != nullptr))
    {
        return false;
    }

    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Request(AnimBlueprint);
    if (Extension == nullptr) return false;

    AnimBlueprint->Modify();
    Extension->Modify();
    AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
    Extension->LuaModuleName = LuaModuleName;
    Extension->SourceMode = ESekiroLuaAnimBlueprintSourceMode::Lua;
    Extension->MarkSourceDirty(TEXT("Lua source configured; compile the Animation Blueprint or start PIE."));
    FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
    AnimBlueprint->GetOutermost()->MarkPackageDirty();
    return true;
}

/**
 * 只读取扩展指定的 Lua 模块，构建并验证 IR 与资产预检结果，然后缓存最近成功 IR。
 * 函数不修改 AnimGraph、EventGraph 或 GeneratedClass，不调用 UE 原生编译，也不保存或标脏 package。
 * 必须在非 PIE 的游戏线程调用；失败会保留旧 IR，但旧修订不会被 Generate From Lua 采用。
 *
 * @param AnimBlueprint 带 Lua 扩展的精确标准 UAnimBlueprint；函数不取得对象所有权。
 * @param OutDiagnostics 接收 Lua 文件、行列、IR 验证和资源预检诊断；调用开始时清空。
 * @return 当前源码完整通过检查并已缓存时返回 true；线程、配置、Lua 或预检失败时返回 false。
 */
bool USekiroAnimBlueprintFactoryLibrary::CheckLuaAnimBlueprint(
    UAnimBlueprint* AnimBlueprint,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    FSekiroAnimIRSourceLocation AssetLocation;
    AssetLocation.LuaModule = AnimBlueprint != nullptr ? AnimBlueprint->GetPathName() : TEXT("None");
    AssetLocation.Line = 1;
    AssetLocation.Column = 1;
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass())
    {
        AddError(
            OutDiagnostics,
            WrongThread,
            TEXT("Check Lua requires an exact UAnimBlueprint on the game thread."),
            AssetLocation.LuaModule,
            AssetLocation);
        return false;
    }

    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (Extension == nullptr || Extension->LuaModuleName.IsEmpty())
    {
        AddError(
            OutDiagnostics,
            Extension == nullptr ? MissingLuaBlueprintExtension : EmptyLuaModuleName,
            Extension == nullptr
                ? TEXT("The target AnimBlueprint has no Sekiro Lua source metadata.")
                : TEXT("LuaModuleName must be configured before Check Lua."),
            AnimBlueprint->GetPathName(),
            AssetLocation);
        return false;
    }

    UUnLuaFunctionLibrary::HotReload();
    FSekiroAnimBlueprintIR CheckedIR;
    bool bSucceeded = USekiroAnimGraphIRLibrary::CompileLuaModule(
        Extension->LuaModuleName,
        CheckedIR,
        OutDiagnostics);
    FPreflightData Preflight;
    if (bSucceeded) bSucceeded = PrepareBuild(CheckedIR, Preflight, OutDiagnostics);
    if (bSucceeded
        && (AnimBlueprint->ParentClass != Preflight.ParentClass
            || AnimBlueprint->TargetSkeleton != Preflight.TargetSkeleton))
    {
        AddError(
            OutDiagnostics,
            AssetConfigurationMismatch,
            TEXT("Lua IR ParentAnimInstanceClass or TargetSkeleton differs from the configured AnimBlueprint asset."),
            AnimBlueprint->GetPathName(),
            CheckedIR.SourceLocation);
        bSucceeded = false;
    }

    if (bSucceeded)
    {
        Extension->MarkCheckSucceeded(CheckedIR);
        return true;
    }

    Extension->MarkCheckFailed(
        OutDiagnostics.Num() > 0
            ? OutDiagnostics.Last().Message
            : TEXT("Lua animation source check failed."));
    return false;
}

/**
 * 使用最近一次与当前 SourceRevision 匹配的成功 IR 事务性重建 Lua 所有的动画蓝图结构。
 * 缓存不存在或过期时先执行 Check Lua；预检完成前不改目标，提交失败时撤销整个编辑器事务。
 * 函数可重建缺失的 AnimGraph、EventGraph 和 Root，但不调用目标 UE 原生编译，也不保存 package。
 *
 * @param AnimBlueprint 接收生成 Graph 的精确标准 UAnimBlueprint；对象路径、父类和 Skeleton 保持不变。
 * @param OutDiagnostics 接收自动检查、预检、Graph 物化与事务错误；调用开始时清空。
 * @return 当前修订 IR 已完整生成到目标 Graph 时返回 true；失败时返回 false 且保留调用前 Graph。
 */
bool USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
    UAnimBlueprint* AnimBlueprint,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass())
    {
        return false;
    }

    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (Extension == nullptr) return false;

    if (!Extension->bHasLastSuccessfulIR
        || Extension->LastCheckedSourceRevision != Extension->SourceRevision)
    {
        if (!CheckLuaAnimBlueprint(AnimBlueprint, OutDiagnostics)) return false;
    }

    FPreflightData Preflight;
    if (!PrepareBuild(Extension->LastSuccessfulIR, Preflight, OutDiagnostics)) return false;
    if (AnimBlueprint->ParentClass != Preflight.ParentClass
        || AnimBlueprint->TargetSkeleton != Preflight.TargetSkeleton)
    {
        AddError(
            OutDiagnostics,
            AssetConfigurationMismatch,
            TEXT("Cached Lua IR no longer matches the AnimBlueprint parent class or target Skeleton."),
            AnimBlueprint->GetPathName(),
            Extension->LastSuccessfulIR.SourceLocation);
        return false;
    }

    if (!BuildPreparedGraph(*AnimBlueprint, Preflight, false, OutDiagnostics))
    {
        Extension->MarkCompileFailed(
            OutDiagnostics.Num() > 0
                ? OutDiagnostics.Last().Message
                : TEXT("Generate From Lua failed."));
        return false;
    }

    Extension->bSourceDirty = false;
    Extension->CompileStatus = ESekiroLuaAnimBlueprintCompileStatus::OutOfDate;
    Extension->LastCompileMessage =
        TEXT("Lua Graph generated successfully; UE native compilation is pending.");
    return true;
}

/**
 * 按 Check、Generate、UE 原生 Compile 的固定顺序同步更新一个 Lua 动画蓝图，并可选保存 package。
 * Check 或 Generate 失败时不会调用原生编译；成功路径恰好调用一次 FKismetEditorUtilities::CompileBlueprint。
 * 必须在非 PIE 的游戏线程调用；全局重入会被拒绝，函数不会根据 SourceMode 改走 Native 分支。
 *
 * @param AnimBlueprint 带 Lua 扩展的标准 UAnimBlueprint；对象身份和路径保持不变。
 * @param bSavePackage true 时仅在全部编译成功后保存当前 package，false 时保留编辑器 Dirty 状态。
 * @param OutDiagnostics 接收 Check、Generate、原生编译和保存诊断；调用开始时清空。
 * @return Graph 生成、唯一一次原生编译及可选保存全部成功时返回 true，否则返回 false。
 */
bool USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
    UAnimBlueprint* AnimBlueprint,
    const bool bSavePackage,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    if (AnimBlueprint == nullptr || !IsInGameThread()) return false;
    if (GIsCompilingLuaAnimBlueprintInPlace)
    {
        FSekiroAnimIRSourceLocation Location;
        Location.LuaModule = AnimBlueprint->GetPathName();
        AddError(
            OutDiagnostics,
            CompileAssetReentry,
            TEXT("Recursive Lua AnimBlueprint in-place compilation was rejected."),
            AnimBlueprint->GetPathName(),
            Location);
        return false;
    }

    TGuardValue<bool> CompileGuard(GIsCompilingLuaAnimBlueprintInPlace, true);
    if (!CheckLuaAnimBlueprint(AnimBlueprint, OutDiagnostics)) return false;
    if (!GenerateLuaAnimBlueprintGraph(AnimBlueprint, OutDiagnostics)) return false;

    FCompilerResultsLog CompileResults;
    CompileResults.bSilentMode = true;
    FKismetEditorUtilities::CompileBlueprint(
        AnimBlueprint,
        EBlueprintCompileOptions::SkipGarbageCollection,
        &CompileResults);
    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (CompileResults.NumErrors > 0
        || AnimBlueprint->Status == BS_Error
        || AnimBlueprint->GeneratedClass == nullptr)
    {
        AddError(
            OutDiagnostics,
            BlueprintCompileFailed,
            FString::Printf(
                TEXT("Lua AnimBlueprint failed native compilation with %d error(s)."),
                CompileResults.NumErrors),
            AnimBlueprint->GetPathName(),
            FSekiroAnimIRSourceLocation());
        if (Extension != nullptr) Extension->MarkCompileFailed(OutDiagnostics.Last().Message);
        return false;
    }

    if (Extension != nullptr) Extension->MarkCompileSucceeded();
    if (bSavePackage)
    {
        UPackage* Package = AnimBlueprint->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(),
            FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, AnimBlueprint, *Filename, SaveArgs))
        {
            AddError(
                OutDiagnostics,
                SavePackageFailed,
                FString::Printf(TEXT("Failed to save Lua AnimBlueprint package '%s'."), *Filename),
                Package->GetName(),
                FSekiroAnimIRSourceLocation());
            return false;
        }
    }
    return true;
}
/**
 * 将当前编辑器进程中全部已加载、已配置 Lua 模块的标准 AnimBlueprint 标记为源已过期。
 * Animation 目录内任意 Lua 都可能是多个蓝图共享的基类或节点依赖，因此在缺少依赖图时保守标脏全部已加载 Lua 资产。
 * 函数不读取 Lua、不重建 Graph、不调用 HotReload，PIE 期间也可以安全调用。只能在游戏线程调用。
 *
 * @param Reason 写入扩展 LastCompileMessage 的变更原因；允许为空。
 * @return 本次标记的已加载 Lua AnimBlueprint 数量；非游戏线程返回 0。
 */
int32 USekiroAnimBlueprintFactoryLibrary::MarkLoadedLuaAnimBlueprintsDirty(
    const FString& Reason)
{
    if (!IsInGameThread()) return 0;

    int32 DirtyAssetCount = 0;
    for (TObjectIterator<UAnimBlueprint> Iterator; Iterator; ++Iterator)
    {
        UAnimBlueprint* AnimBlueprint = *Iterator;
        if (!IsValid(AnimBlueprint)
            || !AnimBlueprint->IsAsset()
            || AnimBlueprint->GetOutermost() == GetTransientPackage())
        {
            continue;
        }

        USekiroLuaAnimBlueprintExtension* Extension =
            USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
        if (Extension == nullptr || Extension->LuaModuleName.IsEmpty()) continue;

        AnimBlueprint->Modify();
        Extension->Modify();
        Extension->MarkSourceDirty(Reason);
        FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
        ++DirtyAssetCount;
    }
    return DirtyAssetCount;
}

/**
 * 对当前编辑器进程中全部已加载、带 Lua 源扩展且 bSourceDirty 的标准 AnimBlueprint 执行批量原地编译。
 * 函数先统一调用 UnLua HotReload，再逐资产调用 CompileLuaAnimBlueprintInPlace；单个失败不会阻止其他资产。
 * 必须在游戏线程且非 PIE/SIE 调用；全局重入会被拒绝，且不会主动加载未进入内存的资产。
 *
 * @param bSavePackages true 时成功后保存各资产 package；false 时只更新内存 Graph 并标记 package Dirty。
 * @param OutDiagnostics 汇总所有 Dirty 资产的结构化诊断；调用开始时清空，并同步写入编辑器日志。
 * @return 没有 Dirty 资产或全部 Dirty 资产编译成功时返回 true；线程、PIE、重入或任一失败时返回 false。
 */
bool USekiroAnimBlueprintFactoryLibrary::CompileDirtyLoadedLuaAnimBlueprints(
    const bool bSavePackages,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    FSekiroAnimIRSourceLocation BatchLocation;
    BatchLocation.LuaModule = TEXT("LoadedLuaAnimBlueprints");
    BatchLocation.Line = 1;
    BatchLocation.Column = 1;
    if (!IsInGameThread())
    {
        AddError(
            OutDiagnostics,
            WrongThread,
            TEXT("CompileDirtyLoadedLuaAnimBlueprints must run on the game thread."),
            TEXT("LoadedLuaAnimBlueprints"),
            BatchLocation);
        return false;
    }
    if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
    {
        AddError(
            OutDiagnostics,
            PIECompileForbidden,
            TEXT("Lua AnimBlueprint structure compilation is queued until PIE has ended."),
            TEXT("LoadedLuaAnimBlueprints"),
            BatchLocation);
        return false;
    }
    if (GIsCompilingDirtyLuaAnimBlueprints)
    {
        AddError(
            OutDiagnostics,
            CompileAllReentry,
            TEXT("A loaded Lua AnimBlueprint compilation batch is already running."),
            TEXT("LoadedLuaAnimBlueprints"),
            BatchLocation);
        return false;
    }

    TGuardValue<bool> CompileGuard(GIsCompilingDirtyLuaAnimBlueprints, true);
    TArray<UAnimBlueprint*> LoadedLuaBlueprints;
    for (TObjectIterator<UAnimBlueprint> Iterator; Iterator; ++Iterator)
    {
        UAnimBlueprint* AnimBlueprint = *Iterator;
        if (!IsValid(AnimBlueprint)
            || !AnimBlueprint->IsAsset()
            || AnimBlueprint->GetOutermost() == GetTransientPackage())
        {
            continue;
        }

        USekiroLuaAnimBlueprintExtension* Extension =
            USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
        if (Extension == nullptr
            || Extension->LuaModuleName.IsEmpty()
            || Extension->SourceMode != ESekiroLuaAnimBlueprintSourceMode::Lua
            || !Extension->bSourceDirty)
        {
            continue;
        }
        LoadedLuaBlueprints.Add(AnimBlueprint);
    }
    LoadedLuaBlueprints.Sort([](const UAnimBlueprint& Left, const UAnimBlueprint& Right)
    {
        return Left.GetPathName() < Right.GetPathName();
    });

    if (LoadedLuaBlueprints.Num() == 0)
    {
        UE_LOG(
            LogSekiroLuaAnimBlueprintCompiler,
            Verbose,
            TEXT("No loaded dirty Lua AnimBlueprint assets require compilation."));
        return true;
    }

    UUnLuaFunctionLibrary::HotReload();
    bool bAllSucceeded = true;
    for (UAnimBlueprint* AnimBlueprint : LoadedLuaBlueprints)
    {
        TArray<FSekiroAnimIRDiagnostic> AssetDiagnostics;
        const bool bAssetSucceeded = CompileLuaAnimBlueprintInPlace(
            AnimBlueprint,
            bSavePackages,
            AssetDiagnostics);
        bAllSucceeded &= bAssetSucceeded;
        if (bAssetSucceeded)
        {
            UE_LOG(
                LogSekiroLuaAnimBlueprintCompiler,
                Display,
                TEXT("Compiled dirty Lua AnimBlueprint '%s'."),
                *AnimBlueprint->GetPathName());
        }

        for (const FSekiroAnimIRDiagnostic& Diagnostic : AssetDiagnostics)
        {
            if (Diagnostic.Severity == ESekiroAnimIRDiagnosticSeverity::Error)
            {
                UE_LOG(
                    LogSekiroLuaAnimBlueprintCompiler,
                    Error,
                    TEXT("[%s] %s: %s (%s:%d:%d)"),
                    *AnimBlueprint->GetPathName(),
                    *Diagnostic.Code.ToString(),
                    *Diagnostic.Message,
                    *Diagnostic.SourceLocation.LuaModule,
                    Diagnostic.SourceLocation.Line,
                    Diagnostic.SourceLocation.Column);
            }
            else
            {
                UE_LOG(
                    LogSekiroLuaAnimBlueprintCompiler,
                    Warning,
                    TEXT("[%s] %s: %s (%s:%d:%d)"),
                    *AnimBlueprint->GetPathName(),
                    *Diagnostic.Code.ToString(),
                    *Diagnostic.Message,
                    *Diagnostic.SourceLocation.LuaModule,
                    Diagnostic.SourceLocation.Line,
                    Diagnostic.SourceLocation.Column);
            }
        }
        OutDiagnostics.Append(AssetDiagnostics);
    }

    return bAllSucceeded;
}
