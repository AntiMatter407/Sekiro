#include "LuaAnimBlueprintFactoryLibrary.h"

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
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LegIK.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraph/AnimGraphNode_FootPlacement.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimLayerInterface.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNodes/AnimNode_BlendListBase.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/StructOnScope.h"
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
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/MemberReference.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "LuaAnimGraphIRLibrary.h"
#include "LuaAnimBlueprintIRReader.h"
#include "LuaAnimGraphIRLuaWriter.h"
#include "LuaAnimGraphNodeRegistry.h"
#include "LuaAnimBlueprintExtension.h"
#include "LuaTransitionRuntimeLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UnLuaFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuaAnimBlueprintCompiler, Log, All);

namespace LuaAnimBlueprintFactoryPrivate
{
    const FName WrongThread = TEXT("Factory.WrongThread");
    const FName UnsupportedLayerCount = TEXT("Factory.UnsupportedLayerCount");
    const FName InterfaceClassLoadFailed = TEXT("Factory.InterfaceClassLoadFailed");
    const FName InterfaceClassTypeMismatch = TEXT("Factory.InterfaceClassTypeMismatch");
    const FName LayerFunctionNotFound = TEXT("Factory.LayerFunctionNotFound");
    const FName LayerSignatureMismatch = TEXT("Factory.LayerSignatureMismatch");
    const FName ParentClassLoadFailed = TEXT("Factory.ParentClassLoadFailed");
    const FName ParentClassTypeMismatch = TEXT("Factory.ParentClassTypeMismatch");
    const FName ParentSkeletonMismatch = TEXT("Factory.ParentSkeletonMismatch");
    const FName InheritedVariableTypeMismatch = TEXT("Factory.InheritedVariableTypeMismatch");
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
    const FName ReflectionPropertyWriteFailed = TEXT("Factory.ReflectionPropertyWriteFailed");
    const FName InheritedDefaultPropertyNotFound = TEXT("Factory.InheritedDefaultPropertyNotFound");
    const FName InheritedDefaultWriteFailed = TEXT("Factory.InheritedDefaultWriteFailed");
    const FName AnimNodeFunctionBindingFailed = TEXT("Factory.AnimNodeFunctionBindingFailed");
    const FName InvalidFootPlacementLegDefinitions = TEXT("Factory.InvalidFootPlacementLegDefinitions");
    const FName InvalidLegIKDefinitions = TEXT("Factory.InvalidLegIKDefinitions");
    const FName InvalidLayeredBlendBranchFilters = TEXT("Factory.InvalidLayeredBlendBranchFilters");
    const FName InvalidLayeredBlendCurveOption = TEXT("Factory.InvalidLayeredBlendCurveOption");
    const FName InvalidFootPlacementPlantSpeedMode = TEXT("Factory.InvalidFootPlacementPlantSpeedMode");
    const FName InvalidFootPlacementPlantLockType = TEXT("Factory.InvalidFootPlacementPlantLockType");
    const FName InvalidTwoBoneIKLocationSpace = TEXT("Factory.InvalidTwoBoneIKLocationSpace");
    const FName InvalidTwoBoneIKTarget = TEXT("Factory.InvalidTwoBoneIKTarget");
    const FName InvalidTwoBoneIKAlphaInputType = TEXT("Factory.InvalidTwoBoneIKAlphaInputType");
    const FName InvalidTwoBoneIKAlphaCurve = TEXT("Factory.InvalidTwoBoneIKAlphaCurve");
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
    const FName InvalidTransitionBoolProperty = TEXT("Factory.InvalidTransitionBoolProperty");
    const FName InvalidGraphProperty = TEXT("Factory.InvalidGraphProperty");
    const FName BlueprintCompileFailed = TEXT("Factory.BlueprintCompileFailed");
    const FName SavePackageFailed = TEXT("Factory.SavePackageFailed");
    const FName EnumTypeLoadFailed = TEXT("Factory.EnumTypeLoadFailed");
    const FName MemberVariableCreationFailed = TEXT("Factory.MemberVariableCreationFailed");
    const FName MissingLuaBlueprintExtension = TEXT("Factory.MissingLuaBlueprintExtension");
    const FName EmptyLuaModuleName = TEXT("Factory.EmptyLuaModuleName");
    const FName InPlaceCommitFailed = TEXT("Factory.InPlaceCommitFailed");
    const FName TransactionUnavailable = TEXT("Factory.TransactionUnavailable");
    const FName PIECompileForbidden = TEXT("Factory.PIECompileForbidden");
    const FName CompileAssetReentry = TEXT("Factory.CompileAssetReentry");

    bool GIsCompilingLuaAnimBlueprintInPlace = false;
    bool GIsPreparingLuaAnimBlueprintGraph = false;
    TSet<UAnimBlueprint*> GPreparingLuaAnimBlueprints;

    /** 保存创建前解析出的 UObject 与注册表类，确保物化阶段不再遇到可预见的加载失败。 */
    struct FPreflightData
    {
        FLuaAnimBlueprintIR Blueprint;
        UClass* ParentClass = nullptr;
        USkeleton* TargetSkeleton = nullptr;
        TMap<FString, UClass*> NodeClasses;
        TMap<FString, UClass*> NodeInstanceClasses;
        TMap<FString, UClass*> NodeInterfaceClasses;
        TArray<UClass*> ImplementedInterfaceClasses;
        TMap<FString, UAnimSequenceBase*> SequenceAssets;
        TMap<FName, UEnum*> VariableEnums;
        TSet<FName> InheritedVariableNames; // 由 Blueprint 父类提供、子类不得重复创建的 IR 变量
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
        TArray<FLuaAnimIRDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& SubjectId,
        const FLuaAnimIRSourceLocation& SourceLocation)
    {
        FLuaAnimIRDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ELuaAnimIRDiagnosticSeverity::Error;
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
    bool HasErrors(const TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        for (const FLuaAnimIRDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Severity == ELuaAnimIRDiagnosticSeverity::Error) return true;
        }

        return false;
    }

    /**
     * 判断完整 IR 是否只包含可在动画工作线程求值的原生 Transition Rule。
     * 本函数只读遍历值类型 IR，不加载 UObject；任一旧式 RuleFunctionName 都会使资产回退游戏线程更新。
     *
     * @param Blueprint 已通过 Canonicalize 的动画蓝图 IR。
     * @return 所有 Transition 均无 Lua RuleFunctionName 时返回 true，否则返回 false。
     */
    bool CanUseMultiThreadedAnimationUpdate(const FLuaAnimBlueprintIR& Blueprint)
    {
        for (const FLuaAnimIRLayer& Layer : Blueprint.Layers)
        {
            for (const FLuaAnimIRGraph& Graph : Layer.Graphs)
            {
                for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    if (!Transition.RuleFunctionName.IsNone()) return false;
                }
            }
        }
        return true;
    }

    /**
     * 在节点属性数组中按注册名查找只读属性。
     * 本函数不加载 UObject，可在任意线程调用；返回指针只在节点属性数组不扩容时有效。
     *
     * @param Node 待查询的 IR 节点。
     * @param PropertyName 注册表定义的属性名。
     * @return 找到时返回数组元素指针，否则返回 nullptr。
     */
    const FLuaAnimIRProperty* FindProperty(const FLuaAnimIRNode& Node, const FName PropertyName)
    {
        for (const FLuaAnimIRProperty& Property : Node.Properties)
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
     * 将 IR 中由 Lua 原生 UENUM 产生的底层数值还原为强类型枚举。
     * 本函数依赖目标引擎枚举的反射元数据校验值域，不接受成员名称字符串。
     *
     * @param RawValue Lua IR 保存的底层整数。
     * @param OutValue 接收类型化枚举；校验失败时保持原值。
     * @return RawValue 属于目标 UENUM 时返回 true。
     */
    template <typename EnumType>
    bool ParseNativeEnumValue(const int64 RawValue, EnumType& OutValue)
    {
        const UEnum* Enum = StaticEnum<EnumType>();
        if (!Enum || !Enum->IsValidEnumValue(RawValue)) return false;
        OutValue = static_cast<EnumType>(RawValue);
        return true;
    }

    /**
     * 将 Lua 原生枚举底层值写入 UE 旧式 TEnumAsByte 字段。
     * StaticEnum 不支持 TEnumAsByte 包装类型，因此必须用其底层枚举取得反射元数据。
     * 本函数只校验并赋值，不修改 UObject，可在任意线程调用。
     *
     * @param RawValue Lua IR 保存的底层整数。
     * @param OutValue 接收底层枚举值的 TEnumAsByte 字段；校验失败时保持原值。
     * @return RawValue 属于底层 UENUM 时返回 true。
     */
    template <typename EnumType>
    bool ParseNativeEnumValue(
        const int64 RawValue,
        TEnumAsByte<EnumType>& OutValue)
    {
        const UEnum* Enum = StaticEnum<EnumType>();
        if (!Enum || !Enum->IsValidEnumValue(RawValue)) return false;
        OutValue = static_cast<EnumType>(RawValue);
        return true;
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
     * @return 类是非抽象 AnimGraph 节点或受支持 K2 数据节点时返回 true。
     */
    bool IsSupportedNodeClass(const UClass* NodeClass)
    {
        return NodeClass != nullptr
            && !NodeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
            && (NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass())
                || NodeClass == UK2Node_VariableGet::StaticClass());
    }

    /**
     * 在编辑器节点或其 FAnimNode_Base 运行时结构中解析点分隔属性路径。
     * 路径可写成 `Yaw`、`Node.Yaw` 或 `PlantSettings.SpeedThreshold`；只返回属性与容器地址。
     * 必须在游戏线程调用，Node 必须是当前生成事务独占的新节点。
     *
     * @param Node 待查询的原生编辑器节点。
     * @param PropertyPath 点分隔反射属性路径。
     * @param OutProperty 接收叶属性。
     * @param OutContainer 接收叶属性所属容器地址。
     * @return 完整路径可解析且所有中间项均为结构体时返回 true。
     */
    bool ResolveReflectedProperty(
        UEdGraphNode& Node,
        const FString& PropertyPath,
        FProperty*& OutProperty,
        void*& OutContainer)
    {
        OutProperty = nullptr;
        OutContainer = nullptr;

        TArray<FString> Segments;
        PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
        if (Segments.IsEmpty()) return false;

        UStruct* CurrentStruct = Node.GetClass();
        void* CurrentContainer = &Node;
        int32 SegmentIndex = 0;
        FProperty* CurrentProperty = FindFProperty<FProperty>(CurrentStruct, *Segments[0]);
        if (CurrentProperty == nullptr)
        {
            for (TFieldIterator<FStructProperty> PropertyIt(Node.GetClass()); PropertyIt; ++PropertyIt)
            {
                if (PropertyIt->Struct != nullptr
                    && PropertyIt->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
                {
                    CurrentContainer = PropertyIt->ContainerPtrToValuePtr<void>(&Node);
                    CurrentStruct = PropertyIt->Struct;
                    CurrentProperty = FindFProperty<FProperty>(CurrentStruct, *Segments[0]);
                    if (CurrentProperty != nullptr) break;
                }
            }
        }
        else if (Segments[0] == TEXT("Node"))
        {
            FStructProperty* RuntimeNodeProperty = CastField<FStructProperty>(CurrentProperty);
            if (RuntimeNodeProperty == nullptr
                || RuntimeNodeProperty->Struct == nullptr
                || !RuntimeNodeProperty->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
            {
                return false;
            }
            CurrentContainer = RuntimeNodeProperty->ContainerPtrToValuePtr<void>(&Node);
            CurrentStruct = RuntimeNodeProperty->Struct;
            CurrentProperty = nullptr;
            SegmentIndex = 1;
        }

        for (; SegmentIndex < Segments.Num(); ++SegmentIndex)
        {
            if (CurrentProperty == nullptr)
            {
                CurrentProperty = FindFProperty<FProperty>(CurrentStruct, *Segments[SegmentIndex]);
            }
            if (CurrentProperty == nullptr) return false;
            if (SegmentIndex == Segments.Num() - 1)
            {
                OutProperty = CurrentProperty;
                OutContainer = CurrentContainer;
                return true;
            }

            FStructProperty* StructProperty = CastField<FStructProperty>(CurrentProperty);
            if (StructProperty == nullptr || StructProperty->Struct == nullptr) return false;
            CurrentContainer = StructProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
            CurrentStruct = StructProperty->Struct;
            CurrentProperty = nullptr;
        }

        return false;
    }

    /**
     * 把类型化 IR 值写入一个已解析的 UE 反射属性。
     * 支持布尔、整数、浮点、枚举、Name、String、结构体、对象/类引用及软对象/软类，不执行运行时 Lua。
     * 必须在游戏线程调用，Container 由 ResolveReflectedProperty 返回。
     *
     * @param Property 叶反射属性。
     * @param Container 叶属性所属容器地址。
     * @param Value Lua IR 提供的类型化值。
     * @return 属性类型与 IR 值可安全转换并完成写入时返回 true。
     */
    bool WriteReflectedValue(
        FProperty& Property,
        void* Container,
        const FLuaAnimIRValue& Value)
    {
        void* ValueAddress = Property.ContainerPtrToValuePtr<void>(Container);
        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
        {
            if (Value.Type != ELuaAnimIRValueType::Bool) return false;
            BoolProperty->SetPropertyValue(ValueAddress, Value.BoolValue);
            return true;
        }
        if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
        {
            if (Value.Type != ELuaAnimIRValueType::Enum
                || !EnumProperty->GetEnum()->IsValidEnumValue(Value.IntegerValue))
            {
                return false;
            }
            const int64 EnumValue = Value.IntegerValue;
            EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValueAddress, EnumValue);
            return true;
        }
        if (FByteProperty* ByteProperty = CastField<FByteProperty>(&Property))
        {
            if (ByteProperty->Enum != nullptr)
            {
                if (Value.Type != ELuaAnimIRValueType::Enum
                    || !ByteProperty->Enum->IsValidEnumValue(Value.IntegerValue))
                {
                    return false;
                }
                if (Value.IntegerValue < 0 || Value.IntegerValue > MAX_uint8) return false;
                ByteProperty->SetPropertyValue(ValueAddress, static_cast<uint8>(Value.IntegerValue));
                return true;
            }
            const int64 ByteValue = Value.Type == ELuaAnimIRValueType::Integer
                ? Value.IntegerValue
                : INDEX_NONE;
            if (ByteValue < 0 || ByteValue > MAX_uint8) return false;
            ByteProperty->SetPropertyValue(ValueAddress, static_cast<uint8>(ByteValue));
            return true;
        }
        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(&Property))
        {
            if (NumericProperty->IsInteger())
            {
                if (Value.Type != ELuaAnimIRValueType::Integer) return false;
                NumericProperty->SetIntPropertyValue(ValueAddress, Value.IntegerValue);
                return true;
            }
            if (Value.Type != ELuaAnimIRValueType::Float
                && Value.Type != ELuaAnimIRValueType::Integer)
            {
                return false;
            }
            const double Number = Value.Type == ELuaAnimIRValueType::Float
                ? Value.FloatValue
                : static_cast<double>(Value.IntegerValue);
            NumericProperty->SetFloatingPointPropertyValue(ValueAddress, Number);
            return true;
        }
        if (FNameProperty* NameProperty = CastField<FNameProperty>(&Property))
        {
            if (Value.Type != ELuaAnimIRValueType::Name
                && Value.Type != ELuaAnimIRValueType::String)
            {
                return false;
            }
            NameProperty->SetPropertyValue(
                ValueAddress,
                Value.Type == ELuaAnimIRValueType::Name
                    ? Value.NameValue
                    : FName(*Value.StringValue));
            return true;
        }
        if (FStrProperty* StringProperty = CastField<FStrProperty>(&Property))
        {
            if (Value.Type != ELuaAnimIRValueType::String
                && Value.Type != ELuaAnimIRValueType::Name)
            {
                return false;
            }
            StringProperty->SetPropertyValue(
                ValueAddress,
                Value.Type == ELuaAnimIRValueType::String
                    ? Value.StringValue
                    : Value.NameValue.ToString());
            return true;
        }
        if (FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
        {
            if (Value.Type != ELuaAnimIRValueType::Struct
                || Value.StructValue.IsEmpty())
            {
                return false;
            }

            FStructOnScope ParsedValue(StructProperty->Struct);
            const TCHAR* ImportEnd = StructProperty->ImportText_Direct(
                *Value.StructValue,
                ParsedValue.GetStructMemory(),
                nullptr,
                PPF_None,
                nullptr);
            if (ImportEnd == nullptr) return false;
            while (FChar::IsWhitespace(*ImportEnd)) ++ImportEnd;
            if (*ImportEnd != TEXT('\0')) return false;

            StructProperty->CopyCompleteValue(
                ValueAddress,
                ParsedValue.GetStructMemory());
            return true;
        }
        if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(&Property))
        {
            FSoftObjectPath Path;
            if (Value.Type == ELuaAnimIRValueType::SoftObjectPath)
            {
                Path = Value.SoftObjectPathValue;
            }
            else if (Value.Type == ELuaAnimIRValueType::SoftClassPath)
            {
                Path = FSoftObjectPath(Value.SoftClassPathValue.ToString());
            }
            else if (Value.Type == ELuaAnimIRValueType::String)
            {
                Path = FSoftObjectPath(Value.StringValue);
            }
            else
            {
                return false;
            }
            SoftObjectProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(Path));
            return true;
        }
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(&Property))
        {
            UObject* ObjectValue = nullptr;
            if (Value.Type == ELuaAnimIRValueType::SoftObjectPath)
            {
                ObjectValue = Value.SoftObjectPathValue.TryLoad();
            }
            else if (Value.Type == ELuaAnimIRValueType::SoftClassPath)
            {
                ObjectValue = Value.SoftClassPathValue.TryLoadClass<UObject>();
            }
            else if (Value.Type == ELuaAnimIRValueType::String)
            {
                ObjectValue = FSoftObjectPath(Value.StringValue).TryLoad();
            }
            else
            {
                return false;
            }
            if (ObjectValue == nullptr || !ObjectValue->IsA(ObjectProperty->PropertyClass)) return false;
            ObjectProperty->SetObjectPropertyValue(ValueAddress, ObjectValue);
            return true;
        }

        return false;
    }

    /**
     * 将 IR 显式列出的父类属性默认值写入当前 AnimBlueprint GeneratedClass CDO。
     * 只接受 ParentClass 已声明或继承的直接 UPROPERTY，不创建变量、不修改未列出的默认值；对象路径
     * 由通用类型化值写入器加载并校验。必须在游戏线程、Blueprint 已拥有 GeneratedClass 时调用。
     * 调用方负责把本函数放入创建清理或编辑器事务中，以便任一失败可以原子撤销。
     *
     * @param Blueprint 接收默认值的动画蓝图；函数会修改其 GeneratedClass 默认对象。
     * @param BlueprintIR 已规范化并验证的 Lua IR，只读取 InheritedDefaults。
     * @param OutDiagnostics 接收缺失属性和类型不兼容诊断。
     * @return 全部显式默认值成功写入时返回 true；GeneratedClass、父属性或值类型无效时返回 false。
     */
    bool ApplyInheritedDefaults(
        UAnimBlueprint& Blueprint,
        const FLuaAnimBlueprintIR& BlueprintIR,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
    {
        if (BlueprintIR.InheritedDefaults.IsEmpty()) return true;

        UClass* GeneratedClass = Blueprint.GeneratedClass;
        UObject* DefaultObject = GeneratedClass != nullptr
            ? GeneratedClass->GetDefaultObject()
            : nullptr;
        if (GeneratedClass == nullptr
            || DefaultObject == nullptr
            || Blueprint.ParentClass == nullptr)
        {
            AddError(
                OutDiagnostics,
                InheritedDefaultPropertyNotFound,
                TEXT("AnimBlueprint has no GeneratedClass CDO or ParentClass for inherited defaults."),
                Blueprint.GetPathName(),
                BlueprintIR.SourceLocation);
            return false;
        }

        DefaultObject->Modify();
        for (const FLuaAnimIRProperty& Default : BlueprintIR.InheritedDefaults)
        {
            FProperty* ParentProperty = FindFProperty<FProperty>(
                Blueprint.ParentClass,
                Default.Name);
            FProperty* GeneratedProperty = FindFProperty<FProperty>(
                GeneratedClass,
                Default.Name);
            if (ParentProperty == nullptr
                || GeneratedProperty == nullptr
                || ParentProperty != GeneratedProperty
                || GeneratedProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
            {
                AddError(
                    OutDiagnostics,
                    InheritedDefaultPropertyNotFound,
                    FString::Printf(
                        TEXT("Inherited default '%s' is not a persistent ParentClass UPROPERTY."),
                        *Default.Name.ToString()),
                    Default.Name.ToString(),
                    BlueprintIR.SourceLocation);
                return false;
            }
            if (!WriteReflectedValue(*GeneratedProperty, DefaultObject, Default.Value))
            {
                AddError(
                    OutDiagnostics,
                    InheritedDefaultWriteFailed,
                    FString::Printf(
                        TEXT("Inherited default '%s' cannot accept the declared IR value."),
                        *Default.Name.ToString()),
                    Default.Name.ToString(),
                    BlueprintIR.SourceLocation);
                return false;
            }
        }
        return true;
    }

    /**
     * 在 Reconstruct 后重放反射属性，并将对象属性同步到同名原生输入 Pin 默认对象。
     * 部分 AnimGraph 节点会在重建 Pin 时从旧默认值覆盖运行时结构；因此所有属性都需要重放一次，
     * UObject 属性还必须同步 Pin，Blueprint 编译器才能持久化该默认输入。
     * 必须在游戏线程、节点完成 Reconstruct 后调用；Node 由当前 Factory 事务独占。
     *
     * @param Node 已完成属性写入和 Pin 重建的原生动画节点。
     * @param Properties Lua IR 请求写入的反射属性集合。
     * @return 全部属性重放且同名对象 Pin 成功同步时返回 true；路径失效或类型异常时返回 false。
     */
    bool SynchronizeReflectedObjectPinDefaults(
        UAnimGraphNode_Base& Node,
        const TArray<FLuaAnimIRProperty>& Properties)
    {
        for (const FLuaAnimIRProperty& Property : Properties)
        {
            FProperty* NativeProperty = nullptr;
            void* PropertyContainer = nullptr;
            if (!ResolveReflectedProperty(
                    Node,
                    Property.Name.ToString(),
                    NativeProperty,
                    PropertyContainer)
                || NativeProperty == nullptr
                || !WriteReflectedValue(
                    *NativeProperty,
                    PropertyContainer,
                    Property.Value))
            {
                return false;
            }

            FObjectPropertyBase* ObjectProperty =
                CastField<FObjectPropertyBase>(NativeProperty);
            if (ObjectProperty == nullptr) continue;

            UEdGraphPin* ObjectPin = Node.FindPin(
                NativeProperty->GetFName(),
                EGPD_Input);
            if (ObjectPin == nullptr) continue;

            void* ValueAddress =
                NativeProperty->ContainerPtrToValuePtr<void>(PropertyContainer);
            UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(
                ValueAddress);
            if (ObjectValue == nullptr
                || !ObjectValue->IsA(ObjectProperty->PropertyClass))
            {
                return false;
            }

            ObjectPin->Modify();
            const UEdGraphSchema* PinSchema = ObjectPin->GetSchema();
            if (PinSchema == nullptr) return false;

            PinSchema->TrySetDefaultObject(*ObjectPin, ObjectValue, false);
            if (ObjectPin->DefaultObject != ObjectValue) return false;
        }

        return true;
    }

    /**
     * 将反射动画节点的输入 Pin 规范化为当前引擎属性与 Lua IR 声明的交集。
     * 只能在游戏线程、节点完成 Reconstruct 后调用。增量生成会复用稳定 GUID 节点，
     * 因而先断开并移除引擎升级后留下的孤儿 Pin，再展开 IR 声明的原生可选属性；
     * 本函数不创建虚构 Pin，也不保留已从当前原生结构删除的旧版本连接。
     *
     * @param Node 当前生成事务独占的原生动画节点。
     * @param DeclaredPins Lua IR 对该反射节点声明的 Pin 集合。
     */
    void ExposeDeclaredReflectedInputPins(
        UAnimGraphNode_Base& Node,
        const TArray<FLuaAnimIRPin>& DeclaredPins)
    {
        for (int32 PinIndex = Node.Pins.Num() - 1; PinIndex >= 0; --PinIndex)
        {
            UEdGraphPin* Pin = Node.Pins[PinIndex];
            if (Pin == nullptr || !Pin->bOrphanedPin) continue;

            Pin->BreakAllPinLinks(false);
            Node.RemovePin(Pin);
        }

        for (const FLuaAnimIRPin& DeclaredPin : DeclaredPins)
        {
            if (DeclaredPin.Direction != ELuaAnimIRPinDirection::Input
                || Node.FindPin(FName(*DeclaredPin.Name), EGPD_Input) != nullptr)
            {
                continue;
            }

            const int32 OptionalPinIndex = Node.ShowPinForProperties.IndexOfByPredicate(
                [&DeclaredPin](const FOptionalPinFromProperty& OptionalPin)
                {
                    return OptionalPin.PropertyName == FName(*DeclaredPin.Name);
                });
            if (OptionalPinIndex != INDEX_NONE)
            {
                Node.SetPinVisibility(true, OptionalPinIndex);
            }
        }
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
     * 解析并验证工厂阶段所需的父类、Skeleton、Transition 属性、注册节点类、动画资源和混合模式。
     * 函数首先调用权威 IR Validator，再规范化私有副本；只有游戏线程可调用，因为软路径解析会加载 UObject。
     *
     * @param Blueprint 调用方提供的只读 IR。
     * @param OutData 接收规范化 IR 与已加载对象，失败时内容不可用于物化。
     * @param OutDiagnostics 接收 Validator 与工厂预检诊断；函数调用期间必须由当前线程独占。
     * @return 所有检查通过且没有 Error 时返回 true，否则返回 false。
     */
    bool PrepareBuild(
        const FLuaAnimBlueprintIR& Blueprint,
        FPreflightData& OutData,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
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

        if (!ULuaAnimGraphIRLibrary::Validate(Blueprint, OutDiagnostics)) return false;

        OutData.Blueprint = Blueprint;
        ULuaAnimGraphIRLibrary::Canonicalize(OutData.Blueprint);

        if (OutData.Blueprint.BlueprintKind == ELuaAnimIRBlueprintKind::AnimBlueprint)
        {
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
        }

        for (const FSoftClassPath& InterfacePath : OutData.Blueprint.ImplementedInterfaces)
        {
            UClass* InterfaceClass = InterfacePath.TryLoadClass<UAnimLayerInterface>();
            if (InterfaceClass == nullptr)
            {
                AddError(
                    OutDiagnostics,
                    InterfaceClassLoadFailed,
                    FString::Printf(
                        TEXT("Failed to load Animation Layer Interface '%s'."),
                        *InterfacePath.ToString()),
                    InterfacePath.ToString(),
                    OutData.Blueprint.SourceLocation);
            }
            else if (!InterfaceClass->IsChildOf(UAnimLayerInterface::StaticClass()))
            {
                AddError(
                    OutDiagnostics,
                    InterfaceClassTypeMismatch,
                    FString::Printf(
                        TEXT("Class '%s' is not an Animation Layer Interface."),
                        *InterfaceClass->GetPathName()),
                    InterfacePath.ToString(),
                    OutData.Blueprint.SourceLocation);
            }
            else
            {
                OutData.ImplementedInterfaceClasses.Add(InterfaceClass);
            }
        }

        const UAnimBlueprintGeneratedClass* ParentAnimBlueprintClass =
            Cast<UAnimBlueprintGeneratedClass>(OutData.ParentClass);
        USkeleton* ParentSkeleton = ParentAnimBlueprintClass != nullptr
            ? ParentAnimBlueprintClass->GetTargetSkeleton()
            : nullptr;
        if (ParentSkeleton != nullptr
            && OutData.TargetSkeleton != nullptr
            && ParentSkeleton != OutData.TargetSkeleton)
        {
            AddError(
                OutDiagnostics,
                ParentSkeletonMismatch,
                FString::Printf(
                    TEXT("TargetSkeleton '%s' must match Blueprint parent skeleton '%s'."),
                    *OutData.TargetSkeleton->GetPathName(),
                    *ParentSkeleton->GetPathName()),
                TEXT("Blueprint.TargetSkeleton"),
                OutData.Blueprint.SourceLocation);
        }

        for (const FLuaAnimIRVariable& Variable : OutData.Blueprint.Variables)
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

        if (ParentAnimBlueprintClass != nullptr)
        {
            for (const FLuaAnimIRVariable& Variable : OutData.Blueprint.Variables)
            {
                FProperty* ParentProperty =
                    FindFProperty<FProperty>(OutData.ParentClass, Variable.Name);
                if (ParentProperty == nullptr) continue;

                bool bTypeMatches = false;
                if (Variable.DataType == TEXT("Bool"))
                {
                    bTypeMatches = CastField<FBoolProperty>(ParentProperty) != nullptr;
                }
                else if (Variable.DataType == TEXT("Float"))
                {
                    bTypeMatches = CastField<FFloatProperty>(ParentProperty) != nullptr;
                }
                else if (Variable.DataType == TEXT("Byte"))
                {
                    const FByteProperty* ByteProperty =
                        CastField<FByteProperty>(ParentProperty);
                    bTypeMatches = ByteProperty != nullptr && ByteProperty->Enum == nullptr;
                }
                else if (Variable.DataType == TEXT("Enum"))
                {
                    UEnum* ExpectedEnum = OutData.VariableEnums.FindRef(Variable.Name);
                    const FByteProperty* ByteProperty =
                        CastField<FByteProperty>(ParentProperty);
                    const FEnumProperty* EnumProperty =
                        CastField<FEnumProperty>(ParentProperty);
                    UEnum* ParentEnum = ByteProperty != nullptr
                        ? ByteProperty->Enum.Get()
                        : EnumProperty != nullptr
                            ? EnumProperty->GetEnum()
                            : nullptr;
                    bTypeMatches = ExpectedEnum != nullptr && ParentEnum == ExpectedEnum;
                }

                if (!bTypeMatches
                    || !ParentProperty->HasAnyPropertyFlags(CPF_BlueprintVisible))
                {
                    AddError(
                        OutDiagnostics,
                        InheritedVariableTypeMismatch,
                        FString::Printf(
                            TEXT("IR variable '%s' declares type '%s', but Blueprint parent property '%s' has reflected type '%s' or is not Blueprint-visible."),
                            *Variable.Name.ToString(),
                            *Variable.DataType.ToString(),
                            *ParentProperty->GetName(),
                            *ParentProperty->GetCPPType()),
                        Variable.Name.ToString(),
                        Variable.SourceLocation);
                    continue;
                }

                OutData.InheritedVariableNames.Add(Variable.Name);
            }
        }

        for (const FLuaAnimIRLayer& BlueprintLayer : OutData.Blueprint.Layers)
        {
            for (const FLuaAnimIRGraph& Graph : BlueprintLayer.Graphs)
            {
                for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    for (const FLuaAnimIRTransitionGateNode& GateNode : Transition.Gate.Nodes)
                    {
                        if (GateNode.Type != TEXT("BoolProperty")) continue;

                        const FLuaAnimIRVariable* DeclaredVariable = OutData.Blueprint.Variables.FindByPredicate(
                            [&GateNode](const FLuaAnimIRVariable& Variable)
                            {
                                return Variable.Name == GateNode.Name;
                            });
                        if (DeclaredVariable != nullptr)
                        {
                            if (DeclaredVariable->DataType == TEXT("Bool")) continue;
                            AddError(
                                OutDiagnostics,
                                InvalidTransitionBoolProperty,
                                FString::Printf(
                                    TEXT("Transition BoolProperty '%s' refers to generated variable type '%s', not Bool."),
                                    *GateNode.Name.ToString(),
                                    *DeclaredVariable->DataType.ToString()),
                                Transition.Id,
                                Transition.SourceLocation);
                            continue;
                        }

                        FProperty* NativeProperty = OutData.ParentClass != nullptr
                            ? FindFProperty<FProperty>(OutData.ParentClass, GateNode.Name)
                            : nullptr;
                        if (NativeProperty == nullptr)
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidTransitionBoolProperty,
                                FString::Printf(
                                    TEXT("Transition BoolProperty '%s' was not found on the AnimInstance class or generated IR variables."),
                                    *GateNode.Name.ToString()),
                                Transition.Id,
                                Transition.SourceLocation);
                        }
                        else if (CastField<FBoolProperty>(NativeProperty) == nullptr
                            || !NativeProperty->HasAnyPropertyFlags(CPF_BlueprintVisible))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidTransitionBoolProperty,
                                FString::Printf(
                                    TEXT("Transition BoolProperty '%s' must be a Blueprint-visible Bool property."),
                                    *GateNode.Name.ToString()),
                                Transition.Id,
                                Transition.SourceLocation);
                        }
                    }
                }
            }
        }

        for (const FLuaAnimIRLayer& Layer : OutData.Blueprint.Layers)
        {
            for (const FLuaAnimIRGraph& Graph : Layer.Graphs)
            {
                for (const FLuaAnimIRNode& Node : Graph.Nodes)
                {
                    if (!OutData.NodeClasses.Contains(Node.Id))
                    {
                        const FLuaAnimIRNodeContract* Contract =
                            FLuaAnimGraphNodeRegistry::Find(Node.NodeType);
                        UClass* NodeClass = !Node.EditorNodeClass.IsNull()
                            ? Node.EditorNodeClass.TryLoadClass<UEdGraphNode>()
                            : Contract != nullptr
                                ? Contract->EditorNodeClassPath.TryLoadClass<UEdGraphNode>()
                                : nullptr;
                        if (NodeClass == nullptr)
                        {
                            AddError(
                                OutDiagnostics,
                                NodeClassLoadFailed,
                                FString::Printf(
                                    TEXT("Failed to load editor node class '%s' for NodeType '%s'."),
                                    *Node.EditorNodeClass.ToString(),
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
                        else if (NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass()))
                        {
                            const FLuaAnimIRProperty* PropertyName =
                                FindProperty(Node, TEXT("PropertyName"));
                            const FName ReferencedName = PropertyName != nullptr
                                ? PropertyName->Value.NameValue
                                : NAME_None;
                            const FLuaAnimIRVariable* DeclaredVariable =
                                OutData.Blueprint.Variables.FindByPredicate(
                                    [ReferencedName](const FLuaAnimIRVariable& Variable)
                                    {
                                        return Variable.Name == ReferencedName;
                                    });
                            FProperty* ParentProperty = OutData.ParentClass != nullptr
                                ? FindFProperty<FProperty>(OutData.ParentClass, ReferencedName)
                                : nullptr;
                            if (DeclaredVariable == nullptr && ParentProperty == nullptr)
                            {
                                AddError(
                                    OutDiagnostics,
                                    InvalidGraphProperty,
                                    FString::Printf(
                                        TEXT("Graph Property Getter '%s' was not found on the AnimInstance parent class or generated IR variables."),
                                        *ReferencedName.ToString()),
                                    Node.Id,
                                    Node.SourceLocation);
                            }
                            else if (DeclaredVariable == nullptr
                                && !ParentProperty->HasAnyPropertyFlags(CPF_BlueprintVisible))
                            {
                                AddError(
                                    OutDiagnostics,
                                    InvalidGraphProperty,
                                    FString::Printf(
                                        TEXT("Graph Property Getter '%s' must reference a Blueprint-visible parent property."),
                                        *ReferencedName.ToString()),
                                    Node.Id,
                                    Node.SourceLocation);
                            }
                        }
                        if (NodeClass != nullptr && IsSupportedNodeClass(NodeClass))
                        {
                            OutData.NodeClasses.Add(Node.Id, NodeClass);
                        }
                    }

                    UClass* const* RegisteredClass = OutData.NodeClasses.Find(Node.Id);
                if (RegisteredClass != nullptr && *RegisteredClass == UAnimGraphNode_BlendListByEnum::StaticClass())
                {
                    const FLuaAnimIRProperty* EnumTypeProperty = FindProperty(Node, TEXT("EnumType"));
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
                    const FLuaAnimIRProperty* LegDefinitionsProperty =
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

                    const FLuaAnimIRProperty* PlantSpeedModeProperty =
                        FindProperty(Node, TEXT("PlantSpeedMode"));
                    EWarpingEvaluationMode PlantSpeedMode = EWarpingEvaluationMode::Graph;
                    if (PlantSpeedModeProperty != nullptr
                        && !ParseNativeEnumValue(
                            PlantSpeedModeProperty->Value.IntegerValue,
                            PlantSpeedMode))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidFootPlacementPlantSpeedMode,
                            FString::Printf(
                                TEXT("FootPlacement PlantSpeedMode value %lld is invalid."),
                                PlantSpeedModeProperty->Value.IntegerValue),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* PlantLockTypeProperty =
                        FindProperty(Node, TEXT("PlantLockType"));
                    if (PlantLockTypeProperty != nullptr)
                    {
                        EFootPlacementLockType PlantLockType = EFootPlacementLockType::PivotAroundBall;
                        if (!ParseNativeEnumValue(
                            PlantLockTypeProperty->Value.IntegerValue,
                            PlantLockType))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidFootPlacementPlantLockType,
                                FString::Printf(
                                    TEXT("FootPlacement PlantLockType value %lld is invalid."),
                                    PlantLockTypeProperty->Value.IntegerValue),
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
                    const FLuaAnimIRProperty* LegDefinitionsProperty =
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
                if (RegisteredClass != nullptr && *RegisteredClass == UAnimGraphNode_TwoBoneIK::StaticClass())
                {
                    const FLuaAnimIRProperty* EffectorSpaceProperty =
                        FindProperty(Node, TEXT("EffectorLocationSpace"));
                    EBoneControlSpace EffectorSpace = BCS_ComponentSpace;
                    if (EffectorSpaceProperty != nullptr
                        && !ParseNativeEnumValue(EffectorSpaceProperty->Value.IntegerValue, EffectorSpace))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKLocationSpace,
                            FString::Printf(
                                TEXT("TwoBoneIK EffectorLocationSpace value %lld is invalid."),
                                EffectorSpaceProperty->Value.IntegerValue),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* JointSpaceProperty =
                        FindProperty(Node, TEXT("JointTargetLocationSpace"));
                    EBoneControlSpace JointSpace = BCS_ComponentSpace;
                    if (JointSpaceProperty != nullptr
                        && !ParseNativeEnumValue(JointSpaceProperty->Value.IntegerValue, JointSpace))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKLocationSpace,
                            FString::Printf(
                                TEXT("TwoBoneIK JointTargetLocationSpace value %lld is invalid."),
                                JointSpaceProperty->Value.IntegerValue),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* EffectorBoneProperty =
                        FindProperty(Node, TEXT("EffectorTargetBoneName"));
                    const FLuaAnimIRProperty* EffectorSocketProperty =
                        FindProperty(Node, TEXT("EffectorTargetSocketName"));
                    const bool bHasEffectorBone = EffectorBoneProperty != nullptr
                        && !EffectorBoneProperty->Value.NameValue.IsNone();
                    const bool bHasEffectorSocket = EffectorSocketProperty != nullptr
                        && !EffectorSocketProperty->Value.NameValue.IsNone();
                    if (bHasEffectorBone && bHasEffectorSocket)
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKTarget,
                            TEXT("TwoBoneIK Effector target must use either a bone or a Socket, not both."),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else if ((EffectorSpace == BCS_ParentBoneSpace || EffectorSpace == BCS_BoneSpace)
                        && !bHasEffectorBone
                        && !bHasEffectorSocket)
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKTarget,
                            TEXT("TwoBoneIK bone-relative Effector space requires EffectorTargetBoneName or EffectorTargetSocketName."),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* JointBoneProperty =
                        FindProperty(Node, TEXT("JointTargetBoneName"));
                    const FLuaAnimIRProperty* JointSocketProperty =
                        FindProperty(Node, TEXT("JointTargetSocketName"));
                    const bool bHasJointBone = JointBoneProperty != nullptr
                        && !JointBoneProperty->Value.NameValue.IsNone();
                    const bool bHasJointSocket = JointSocketProperty != nullptr
                        && !JointSocketProperty->Value.NameValue.IsNone();
                    if (bHasJointBone && bHasJointSocket)
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKTarget,
                            TEXT("TwoBoneIK Joint target must use either a bone or a Socket, not both."),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else if ((JointSpace == BCS_ParentBoneSpace || JointSpace == BCS_BoneSpace)
                        && !bHasJointBone
                        && !bHasJointSocket)
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKTarget,
                            TEXT("TwoBoneIK bone-relative Joint target requires JointTargetBoneName or JointTargetSocketName."),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* AlphaInputTypeProperty =
                        FindProperty(Node, TEXT("AlphaInputType"));
                    EAnimAlphaInputType AlphaInputType = EAnimAlphaInputType::Float;
                    if (AlphaInputTypeProperty != nullptr
                        && !ParseNativeEnumValue(
                            AlphaInputTypeProperty->Value.IntegerValue,
                            AlphaInputType))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKAlphaInputType,
                            FString::Printf(
                                TEXT("TwoBoneIK AlphaInputType value %lld is invalid."),
                                AlphaInputTypeProperty->Value.IntegerValue),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    const FLuaAnimIRProperty* AlphaCurveProperty =
                        FindProperty(Node, TEXT("AlphaCurveName"));
                    if (AlphaInputType == EAnimAlphaInputType::Curve
                        && (AlphaCurveProperty == nullptr || AlphaCurveProperty->Value.NameValue.IsNone()))
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTwoBoneIKAlphaCurve,
                            TEXT("TwoBoneIK Curve AlphaInputType requires a non-empty AlphaCurveName."),
                            Node.Id,
                            Node.SourceLocation);
                    }
                }
                if (RegisteredClass != nullptr
                    && *RegisteredClass == UAnimGraphNode_LayeredBoneBlend::StaticClass())
                {
                    const FLuaAnimIRProperty* BranchFiltersProperty =
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

                    const FLuaAnimIRProperty* CurveOptionProperty =
                        FindProperty(Node, TEXT("CurveBlendOption"));
                    if (CurveOptionProperty != nullptr)
                    {
                        ECurveBlendOption::Type CurveOption = ECurveBlendOption::Override;
                        if (!ParseNativeEnumValue(
                            CurveOptionProperty->Value.IntegerValue,
                            CurveOption))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidLayeredBlendCurveOption,
                                FString::Printf(
                                    TEXT("LayeredBlendPerBone CurveBlendOption value %lld is invalid."),
                                    CurveOptionProperty->Value.IntegerValue),
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

                const FLuaAnimIRProperty* SequenceProperty = FindProperty(Node, TEXT("Sequence"));
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

            if (Graph.GraphType != LuaAnimGraphIRNames::StateMachineGraph) continue;
            for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                if (Transition.Settings.BlendMode != EAlphaBlendOption::Linear)
                {
                    AddError(
                        OutDiagnostics,
                        UnsupportedTransitionBlendMode,
                        FString::Printf(
                            TEXT("Transition blend mode value %d is not supported; this stage supports Linear only."),
                            static_cast<int32>(Transition.Settings.BlendMode)),
                        Transition.Id,
                        Transition.SourceLocation);
                }
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
            TArray<FLuaAnimIRDiagnostic>& InDiagnostics,
            const FLuaAnimBlueprintIR* InPreviousGeneratedIR = nullptr)
            : Preflight(InPreflight)
            , Blueprint(InBlueprint)
            , Diagnostics(InDiagnostics)
            , PreviousGeneratedIR(InPreviousGeneratedIR)
            , bIncremental(InPreviousGeneratedIR != nullptr)
        {
            TArray<UEdGraph*> ExistingGraphs;
            InBlueprint.GetAllGraphs(ExistingGraphs);
            for (UEdGraph* ExistingGraph : ExistingGraphs)
            {
                if (ExistingGraph != nullptr)
                {
                    InitialNativeGraphs.Add(ExistingGraph);
                }
            }
            for (const FLuaAnimIRLayer& Layer : InPreflight.Blueprint.Layers)
            {
                for (const FLuaAnimIRGraph& Graph : Layer.Graphs)
                {
                    GraphsById.Add(Graph.Id, &Graph);
                    for (const FLuaAnimIRNode& Node : Graph.Nodes)
                    {
                        NodesById.Add(Node.Id, &Node);
                    }
                }
            }
            if (PreviousGeneratedIR != nullptr)
            {
                for (const FLuaAnimIRLayer& Layer : PreviousGeneratedIR->Layers)
                {
                    for (const FLuaAnimIRGraph& Graph : Layer.Graphs)
                    {
                        PreviousGraphsById.Add(Graph.Id, &Graph);
                        for (const FLuaAnimIRNode& Node : Graph.Nodes)
                        {
                            PreviousOwnedNodeGuids.Add(MakeStableGuid(TEXT("Node"), Node.Id));
                        }
                        for (const FLuaAnimIRState& State : Graph.StateMachine.States)
                        {
                            PreviousOwnedNodeGuids.Add(MakeStableGuid(TEXT("State"), State.Id));
                        }
                        for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                        {
                            PreviousOwnedNodeGuids.Add(
                                MakeStableGuid(TEXT("Transition"), Transition.Id));
                        }
                    }
                }
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
            if (Preflight.Blueprint.BlueprintKind
                == ELuaAnimIRBlueprintKind::AnimationLayerInterface)
            {
                return BuildAnimationLayerInterface();
            }

            if (Preflight.Blueprint.Layers.IsEmpty())
            {
                AddError(
                    Diagnostics,
                    MainAnimGraphNotFound,
                    TEXT("AnimBlueprint IR must contain a main AnimGraph layer."),
                    Preflight.Blueprint.SourceModule,
                    Preflight.Blueprint.SourceLocation);
                return false;
            }

            for (UClass* InterfaceClass : Preflight.ImplementedInterfaceClasses)
            {
                const bool bAlreadyImplemented = InterfaceClass != nullptr
                    && Blueprint.ImplementedInterfaces.ContainsByPredicate(
                        [InterfaceClass](const FBPInterfaceDescription& Description)
                        {
                            return Description.Interface == InterfaceClass;
                        });
                if (InterfaceClass == nullptr
                    || (!bAlreadyImplemented
                        && !FBlueprintEditorUtils::ImplementNewInterface(
                            &Blueprint,
                            InterfaceClass->GetClassPathName())))
                {
                    AddError(
                        Diagnostics,
                        InterfaceClassTypeMismatch,
                        TEXT("Failed to implement an Animation Layer Interface on the generated AnimBlueprint."),
                        InterfaceClass != nullptr ? InterfaceClass->GetPathName() : FString(),
                        Preflight.Blueprint.SourceLocation);
                    return false;
                }
            }

            const FLuaAnimIRLayer& MainLayer = Preflight.Blueprint.Layers[0];
            UAnimationGraph* MainGraph = nullptr;
            for (UEdGraph* FunctionGraph : Blueprint.FunctionGraphs)
            {
                UAnimationGraph* Candidate = Cast<UAnimationGraph>(FunctionGraph);
                if (Candidate != nullptr
                    && !Candidate->IsA<UAnimationStateGraph>()
                    && Candidate->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
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
                    MainLayer.RootGraphId,
                    MainLayer.SourceLocation);
                return false;
            }

            const FLuaAnimIRGraph* RootGraph = FindGraph(MainLayer.RootGraphId);
            if (RootGraph == nullptr)
            {
                AddError(
                    Diagnostics,
                    NativeGraphMissing,
                    TEXT("Layer root Graph could not be found after validation."),
                    MainLayer.RootGraphId,
                    MainLayer.SourceLocation);
                return false;
            }

            if (!BuildMemberVariables())
            {
                return ReportUnhandledBuildFailure(TEXT("member variables"));
            }
            CurrentLayer = &MainLayer;
            if (!BuildPoseGraph(*RootGraph, *MainGraph))
            {
                return ReportUnhandledBuildFailure(TEXT("main AnimGraph"));
            }
            for (int32 LayerIndex = 1; LayerIndex < Preflight.Blueprint.Layers.Num(); ++LayerIndex)
            {
                if (!BuildAnimationLayer(Preflight.Blueprint.Layers[LayerIndex]))
                {
                    return ReportUnhandledBuildFailure(
                        FString::Printf(
                            TEXT("Animation Layer '%s'"),
                            *Preflight.Blueprint.Layers[LayerIndex].Name));
                }
            }
            CurrentLayer = nullptr;
            if (!BuildEventGraph())
            {
                return ReportUnhandledBuildFailure(TEXT("EventGraph"));
            }
            return true;
        }

    private:
        /**
         * 为 Builder 子步骤遗漏的失败诊断补充稳定错误，保证事务回滚原因始终可见。
         * 已有 Error 时保持原诊断不变；本函数不修改 Blueprint，只能在当前 Builder 所在游戏线程调用。
         *
         * @param BuildStage 失败的物化阶段可读名称，不能为空。
         * @return 始终返回 false，便于调用方直接结束构建。
         */
        bool ReportUnhandledBuildFailure(const FString& BuildStage)
        {
            if (!HasErrors(Diagnostics))
            {
                AddError(
                    Diagnostics,
                    InPlaceCommitFailed,
                    FString::Printf(
                        TEXT("Native AnimBlueprint Builder failed while materializing %s without a more specific diagnostic."),
                        *BuildStage),
                    Preflight.Blueprint.SourceModule,
                    Preflight.Blueprint.SourceLocation);
            }
            return false;
        }

        /**
         * 按 IR 稳定 ID 在指定 Graph 中复用同类型节点；类型变化时只替换该 Lua 节点。
         * 复用节点保留对象身份、编辑器坐标以及 Lua 未显式写入的原生属性。
         *
         * @param Graph 节点所属原生 Graph。
         * @param StableId Lua IR 稳定 ID。
         * @return 找到的同类型节点；不存在时按 UE 标准生命周期创建新节点。
         */
        template <typename NodeType>
        NodeType* CreateNativeNode(
            UEdGraph& Graph,
            const FString& StableId,
            const int32 PositionX,
            const int32 PositionY)
        {
            if (bIncremental)
            {
                const FGuid CandidateGuids[] = {
                    MakeStableGuid(TEXT("Node"), StableId),
                    MakeStableGuid(TEXT("State"), StableId),
                    MakeStableGuid(TEXT("Transition"), StableId),
                };
                const TArray<UEdGraphNode*> ExistingNodes = Graph.Nodes;
                for (UEdGraphNode* ExistingNode : ExistingNodes)
                {
                    if (ExistingNode == nullptr) continue;
                    bool bGuidMatches = false;
                    for (const FGuid& CandidateGuid : CandidateGuids)
                    {
                        if (ExistingNode->NodeGuid == CandidateGuid)
                        {
                            bGuidMatches = true;
                            break;
                        }
                    }
                    if (!bGuidMatches) continue;

                    NodeType* TypedNode = Cast<NodeType>(ExistingNode);
                    if (TypedNode != nullptr)
                    {
                        TypedNode->Modify();
                        ReusedNativeNodes.Add(TypedNode);
                        return TypedNode;
                    }

                    ExistingNode->Modify();
                    FBlueprintEditorUtils::RemoveNode(&Blueprint, ExistingNode, true);
                    break;
                }
            }

            return LuaAnimBlueprintFactoryPrivate::CreateNativeNode<NodeType>(
                Graph,
                StableId,
                PositionX,
                PositionY);
        }

        /**
         * 创建或复用原生函数调用节点；只在 Lua 声明该调用时更新函数引用。
         *
         * @param Graph 调用节点所属 K2 Graph。
         * @param StableId Lua IR 稳定 ID。
         * @param Function 目标原生函数。
         * @return 可用的函数调用节点，参数非法时返回 nullptr。
         */
        UK2Node_CallFunction* CreateCallFunctionNode(
            UEdGraph& Graph,
            const FString& StableId,
            UFunction* Function,
            const int32 PositionX,
            const int32 PositionY)
        {
            if (Function == nullptr) return nullptr;
            const FGuid ExpectedGuid = MakeStableGuid(TEXT("K2Call"), StableId);
            if (bIncremental)
            {
                for (UEdGraphNode* ExistingNode : Graph.Nodes)
                {
                    if (ExistingNode == nullptr || ExistingNode->NodeGuid != ExpectedGuid) continue;
                    UK2Node_CallFunction* ExistingCall = Cast<UK2Node_CallFunction>(ExistingNode);
                    if (ExistingCall != nullptr)
                    {
                        ExistingCall->Modify();
                        ReusedNativeNodes.Add(ExistingCall);
                        if (ExistingCall->GetTargetFunction() != Function)
                        {
                            ExistingCall->SetFromFunction(Function);
                            ExistingCall->ReconstructNode();
                        }
                        return ExistingCall;
                    }
                    ExistingNode->Modify();
                    FBlueprintEditorUtils::RemoveNode(&Blueprint, ExistingNode, true);
                    break;
                }
            }
            return LuaAnimBlueprintFactoryPrivate::CreateCallFunctionNode(
                Graph,
                StableId,
                Function,
                PositionX,
                PositionY);
        }

        /** 仅为新建节点应用自动布局，增量复用节点保持编辑器中现有坐标。 */
        void ApplyGeneratedPosition(UEdGraphNode* Node, const int32 PositionX, const int32 PositionY)
        {
            if (Node == nullptr || (bIncremental && ReusedNativeNodes.Contains(Node))) return;
            Node->NodePosX = PositionX;
            Node->NodePosY = PositionY;
        }

        /**
         * 以 Animation Layer Interface 的 UFunction 为权威签名，补齐 Linked Anim Layer 缺失的 Pose 输入 Pin。
         * UE5.2 的自层节点在宿主 SkeletonGeneratedClass 尚未因 ImplementNewInterface 重编译时，
         * ReconstructNode 无法从宿主类解析新接口函数；本函数只补接口明确声明且原生节点尚不存在的 Pose 输入，
         * 不创建标量可选 Pin、不修改接口或宿主类，也不触发 Blueprint 编译。
         * 必须在游戏线程、节点已完成最后一次 Reconstruct 且当前 Builder 独占 Graph 时调用。
         *
         * @param LinkedLayerNode 待补齐输入 Pin 的原生 Linked Anim Layer 节点。
         * @param InterfaceClass 已预检且已加载的 Animation Layer Interface 类，不可为空。
         * @param LayerName 节点引用的接口函数名，必须能在 InterfaceClass 中解析。
         * @param SourceNode IR 节点，仅用于稳定错误主题和源码位置。
         * @return 接口函数存在且全部 Pose 输入均已存在或成功创建时返回 true，否则追加诊断并返回 false。
         */
        bool EnsureLinkedAnimLayerPosePins(
            UAnimGraphNode_LinkedAnimLayer& LinkedLayerNode,
            UClass& InterfaceClass,
            const FName LayerName,
            const FLuaAnimIRNode& SourceNode)
        {
            UFunction* SignatureFunction =
                InterfaceClass.FindFunctionByName(LayerName);
            if (SignatureFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    LayerSignatureMismatch,
                    FString::Printf(
                        TEXT("Animation Layer Interface '%s' has no function '%s'."),
                        *InterfaceClass.GetPathName(),
                        *LayerName.ToString()),
                    SourceNode.Id,
                    SourceNode.SourceLocation);
                return false;
            }
            const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
            for (const FLuaAnimIRPin& DeclaredPin : SourceNode.Pins)
            {
                if (DeclaredPin.Direction != ELuaAnimIRPinDirection::Input
                    || DeclaredPin.DataType != LuaAnimGraphIRNames::PoseData)
                {
                    continue;
                }

                FProperty* Parameter = FindFProperty<FProperty>(
                    SignatureFunction,
                    FName(*DeclaredPin.Name));
                if (Parameter == nullptr)
                {
                    AddError(
                        Diagnostics,
                        LayerSignatureMismatch,
                        FString::Printf(
                            TEXT("Animation Layer '%s' function '%s' has no parameter named '%s'."),
                            *LayerName.ToString(),
                            *SignatureFunction->GetPathName(),
                            *DeclaredPin.Name),
                        SourceNode.Id,
                        SourceNode.SourceLocation);
                    return false;
                }
                const bool bIsFunctionInput =
                    Parameter->HasAnyPropertyFlags(CPF_Parm)
                    && (!Parameter->HasAnyPropertyFlags(CPF_OutParm)
                        || Parameter->HasAnyPropertyFlags(CPF_ReferenceParm));
                if (!bIsFunctionInput)
                {
                    AddError(
                        Diagnostics,
                        LayerSignatureMismatch,
                        FString::Printf(
                            TEXT("Animation Layer '%s' parameter '%s' is not an input parameter."),
                            *LayerName.ToString(),
                            *DeclaredPin.Name),
                        SourceNode.Id,
                        SourceNode.SourceLocation);
                    return false;
                }

                FEdGraphPinType PinType;
                if (!K2Schema->ConvertPropertyToPinType(Parameter, PinType))
                {
                    AddError(
                        Diagnostics,
                        LayerSignatureMismatch,
                        FString::Printf(
                            TEXT("Animation Layer '%s' parameter '%s' cannot convert to an editor Pin type."),
                            *LayerName.ToString(),
                            *DeclaredPin.Name),
                        SourceNode.Id,
                        SourceNode.SourceLocation);
                    return false;
                }
                if (!UAnimationGraphSchema::IsPosePin(PinType))
                {
                    AddError(
                        Diagnostics,
                        LayerSignatureMismatch,
                        FString::Printf(
                            TEXT("Animation Layer '%s' parameter '%s' is '%s', not a Pose input."),
                            *LayerName.ToString(),
                            *DeclaredPin.Name,
                            *Parameter->GetCPPType()),
                        SourceNode.Id,
                        SourceNode.SourceLocation);
                    return false;
                }

                if (LinkedLayerNode.FindPin(
                    Parameter->GetFName(),
                    EGPD_Input) != nullptr)
                {
                    continue;
                }
                UEdGraphPin* PosePin = LinkedLayerNode.CreatePin(
                    EGPD_Input,
                    UAnimationGraphSchema::MakeLocalSpacePosePin(),
                    Parameter->GetFName());
                if (PosePin == nullptr)
                {
                    AddError(
                        Diagnostics,
                        LayerSignatureMismatch,
                        FString::Printf(
                            TEXT("Failed to create Pose input '%s' for Animation Layer '%s'."),
                            *Parameter->GetName(),
                            *LayerName.ToString()),
                        SourceNode.Id,
                        SourceNode.SourceLocation);
                    return false;
                }
                PosePin->PinFriendlyName = FText::FromName(Parameter->GetFName());
            }
            return true;
        }

        /** 将接口 IR 的每个 Layer 声明物化为 UE 原生 Animation Layer Interface FunctionGraph。 */
        bool BuildAnimationLayerInterface()
        {
            for (const FLuaAnimIRLayer& Layer : Preflight.Blueprint.Layers)
            {
                const FName FunctionName = Layer.FunctionName.IsNone()
                    ? FName(*Layer.Name)
                    : Layer.FunctionName;
                UAnimationGraph* FunctionGraph = Cast<UAnimationGraph>(
                    FBlueprintEditorUtils::CreateNewGraph(
                        &Blueprint,
                        FunctionName,
                        UAnimationGraph::StaticClass(),
                        UAnimationGraphSchema::StaticClass()));
                if (FunctionGraph == nullptr)
                {
                    AddError(
                        Diagnostics,
                        NativeGraphMissing,
                        TEXT("Failed to create Animation Layer Interface graph."),
                        Layer.Id,
                        Layer.SourceLocation);
                    return false;
                }
                FBlueprintEditorUtils::AddDomainSpecificGraph(
                    &Blueprint,
                    FunctionGraph);
                if (!BuildLayerSignatureNodes(Layer, *FunctionGraph, nullptr)) return false;
            }
            return true;
        }

        /** 创建或定位一个动画层 FunctionGraph，并把对应 IR Pose 图物化为函数实现。 */
        bool BuildAnimationLayer(const FLuaAnimIRLayer& Layer)
        {
            const FName FunctionName = Layer.FunctionName.IsNone()
                ? FName(*Layer.Name)
                : Layer.FunctionName;
            UFunction* SignatureFunction = nullptr;
            UClass* SignatureClass = nullptr;
            if (!Layer.InterfaceClass.IsNull())
            {
                SignatureClass = Layer.InterfaceClass.TryLoadClass<UAnimLayerInterface>();
            }
            else if (Layer.bOverride && Preflight.ParentClass != nullptr)
            {
                SignatureClass = Preflight.ParentClass;
            }
            if (SignatureClass != nullptr)
            {
                SignatureFunction = SignatureClass->FindFunctionByName(FunctionName);
            }

            UAnimationGraph* FunctionGraph = nullptr;
            TArray<UEdGraph*> AllGraphs;
            Blueprint.GetAllGraphs(AllGraphs);
            for (UEdGraph* Graph : AllGraphs)
            {
                if (Graph != nullptr && Graph->GetFName() == FunctionName)
                {
                    FunctionGraph = Cast<UAnimationGraph>(Graph);
                    break;
                }
            }
            if (FunctionGraph == nullptr)
            {
                FunctionGraph = Cast<UAnimationGraph>(FBlueprintEditorUtils::CreateNewGraph(
                    &Blueprint,
                    FunctionName,
                    UAnimationGraph::StaticClass(),
                    UAnimationGraphSchema::StaticClass()));
                if (FunctionGraph != nullptr)
                {
                    FBlueprintEditorUtils::AddFunctionGraph(
                        &Blueprint,
                        FunctionGraph,
                        !Layer.bOverride,
                        SignatureFunction);
                }
            }
            if (FunctionGraph == nullptr)
            {
                AddError(
                    Diagnostics,
                    NativeGraphMissing,
                    TEXT("Failed to create or locate Animation Layer graph."),
                    Layer.Id,
                    Layer.SourceLocation);
                return false;
            }

            if (!bIncremental)
            {
                const TArray<UEdGraphNode*> ExistingNodes = FunctionGraph->Nodes;
                for (UEdGraphNode* ExistingNode : ExistingNodes)
                {
                    if (ExistingNode == nullptr || ExistingNode->IsA<UAnimGraphNode_Root>()) continue;
                    FBlueprintEditorUtils::RemoveNode(&Blueprint, ExistingNode, true);
                }
            }
            if (!BuildLayerSignatureNodes(Layer, *FunctionGraph, SignatureFunction)) return false;
            const FLuaAnimIRGraph* RootGraph = FindGraph(Layer.RootGraphId);
            if (RootGraph == nullptr) return false;
            CurrentLayer = &Layer;
            return BuildPoseGraph(*RootGraph, *FunctionGraph);
        }

        /** 按 IR 或已有 UFunction 签名创建动画层的原生 Linked Input Pose 节点。 */
        bool BuildLayerSignatureNodes(
            const FLuaAnimIRLayer& Layer,
            UAnimationGraph& FunctionGraph,
            UFunction* SignatureFunction)
        {
            if (FunctionGraph.Nodes.IsEmpty())
            {
                FunctionGraph.GetSchema()->CreateDefaultNodesForGraph(FunctionGraph);
            }
            if (SignatureFunction != nullptr)
            {
                UAnimationGraphSchema::ConformAnimGraphToInterface(
                    &Blueprint,
                    FunctionGraph,
                    SignatureFunction);
            }

            TArray<const FLuaAnimIRFunctionParameter*> PoseParameters;
            TArray<const FLuaAnimIRFunctionParameter*> ValueParameters;
            for (const FLuaAnimIRFunctionParameter& Parameter : Layer.Parameters)
            {
                if (Parameter.bIsPose) PoseParameters.Add(&Parameter);
                else ValueParameters.Add(&Parameter);
            }
            if (PoseParameters.IsEmpty() && !ValueParameters.IsEmpty())
            {
                AddError(
                    Diagnostics,
                    LayerSignatureMismatch,
                    TEXT("An Animation Layer with scalar parameters must declare at least one Pose input."),
                    Layer.Id,
                    Layer.SourceLocation);
                return false;
            }

            for (int32 PoseIndex = 0; PoseIndex < PoseParameters.Num(); ++PoseIndex)
            {
                const FString StableInputId =
                    Layer.Id + TEXT(".Input.") + PoseParameters[PoseIndex]->Name.ToString();
                const FGuid StableInputGuid = MakeStableGuid(TEXT("Node"), StableInputId);
                UAnimGraphNode_LinkedInputPose* InputNode = nullptr;
                for (UEdGraphNode* ExistingNode : FunctionGraph.Nodes)
                {
                    UAnimGraphNode_LinkedInputPose* ExistingInput =
                        Cast<UAnimGraphNode_LinkedInputPose>(ExistingNode);
                    if (ExistingInput == nullptr
                        || (ExistingInput->NodeGuid != StableInputGuid
                            && ExistingInput->Node.Name
                                != PoseParameters[PoseIndex]->Name))
                    {
                        continue;
                    }
                    InputNode = ExistingInput;
                    InputNode->Modify();
                    if (bIncremental) ReusedNativeNodes.Add(InputNode);
                    break;
                }
                if (InputNode == nullptr)
                {
                    FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> NodeCreator(FunctionGraph);
                    InputNode = NodeCreator.CreateNode(false);
                    if (InputNode == nullptr) return false;
                    InputNode->Node.Name = PoseParameters[PoseIndex]->Name;
                    InputNode->InputPoseIndex = PoseIndex;
                    NodeCreator.Finalize();
                }
                InputNode->Node.Name = PoseParameters[PoseIndex]->Name;
                InputNode->InputPoseIndex = PoseIndex;
                InputNode->Inputs.Reset();
                if (PoseIndex == 0)
                {
                    for (const FLuaAnimIRFunctionParameter* ValueParameter : ValueParameters)
                    {
                        FAnimBlueprintFunctionPinInfo& PinInfo =
                            InputNode->Inputs.AddDefaulted_GetRef();
                        PinInfo.Name = ValueParameter->Name;
                        if (ValueParameter->DataType == TEXT("Bool"))
                        {
                            PinInfo.Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
                        }
                        else if (ValueParameter->DataType == TEXT("Float"))
                        {
                            PinInfo.Type.PinCategory = UEdGraphSchema_K2::PC_Real;
                            PinInfo.Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
                        }
                        else if (ValueParameter->DataType == TEXT("Integer"))
                        {
                            PinInfo.Type.PinCategory = UEdGraphSchema_K2::PC_Int;
                        }
                        else if (ValueParameter->DataType == TEXT("Name"))
                        {
                            PinInfo.Type.PinCategory = UEdGraphSchema_K2::PC_Name;
                        }
                        else if (ValueParameter->DataType == TEXT("String"))
                        {
                            PinInfo.Type.PinCategory = UEdGraphSchema_K2::PC_String;
                        }
                        else
                        {
                            AddError(
                                Diagnostics,
                                LayerSignatureMismatch,
                                FString::Printf(
                                    TEXT("Unsupported Animation Layer scalar parameter type '%s'."),
                                    *ValueParameter->DataType.ToString()),
                                Layer.Id,
                                ValueParameter->SourceLocation);
                            return false;
                        }
                    }
                }
                InputNode->ReconstructNode();
                InputNode->NodeGuid = StableInputGuid;
                ApplyGeneratedPosition(InputNode, -400, PoseIndex * 180);
            }
            return true;
        }

        /**
         * 将 IR Variable 生成成 AnimBlueprint Member Variable，并写入类型化默认值与 Transient 标记。
         * 必须在游戏线程、任何 Graph 编译前调用；Enum 对象来自 Preflight，不在此函数加载资产。
         *
         * @return 全部变量成功加入 Blueprint 时返回 true；名称冲突或类型物化失败时追加诊断并返回 false。
         */
        bool BuildMemberVariables()
        {
            for (const FLuaAnimIRVariable& Variable : Preflight.Blueprint.Variables)
            {
                if (Preflight.InheritedVariableNames.Contains(Variable.Name)) continue;
                FBPVariableDescription* ExistingDescription =
                    Blueprint.NewVariables.FindByPredicate(
                        [&Variable](const FBPVariableDescription& Description)
                        {
                            return Description.VarName == Variable.Name;
                        });
                if (ExistingDescription != nullptr)
                {
                    ExistingDescription->PropertyFlags |= CPF_BlueprintVisible;
                    ExistingDescription->PropertyFlags &=
                        ~(CPF_BlueprintReadOnly | CPF_DisableEditOnInstance);
                    if (Variable.bTransient) ExistingDescription->PropertyFlags |= CPF_Transient;
                    else ExistingDescription->PropertyFlags &= ~CPF_Transient;
                    continue;
                }

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
        const FLuaAnimIRGraph* FindGraph(const FString& GraphId) const
        {
            const FLuaAnimIRGraph* const* Graph = GraphsById.Find(GraphId);
            return Graph != nullptr ? *Graph : nullptr;
        }

        /**
         * 从持久化生成基线中查找状态机过渡，用于避免重建未变化的 Rule Graph。
         *
         * @param GraphId 状态机 Graph 稳定 ID。
         * @param TransitionId 过渡稳定 ID。
         * @return 找到时返回上次实际生成的过渡，否则返回 nullptr。
         */
        const FLuaAnimIRTransition* FindPreviousTransition(
            const FString& GraphId,
            const FString& TransitionId) const
        {
            const FLuaAnimIRGraph* const* PreviousGraph = PreviousGraphsById.Find(GraphId);
            if (PreviousGraph == nullptr || *PreviousGraph == nullptr) return nullptr;
            for (const FLuaAnimIRTransition& Transition : (*PreviousGraph)->StateMachine.Transitions)
            {
                if (Transition.Id == TransitionId) return &Transition;
            }
            return nullptr;
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

            const ULuaAnimBlueprintExtension* ExistingExtension =
                ULuaAnimBlueprintExtension::Find(&Blueprint);
            if (!EventGraph->Nodes.IsEmpty()
                && (ExistingExtension == nullptr || !ExistingExtension->bHasLastGeneratedIR))
            {
                // 首次导入原生 AnimBlueprint 时 EventGraph 仍以编辑器为准；
                // 只有已持久化生成基线的资产才允许工厂维护 Lua Update Bridge。
                return true;
            }

            UFunction* UpdateFunction = UAnimInstance::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(UAnimInstance, BlueprintUpdateAnimation));
            UFunction* LuaUpdateFunction = ULuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(ULuaTransitionRuntimeLibrary, EvaluateBlueprintUpdateAnimation));
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
            const TFunction<bool(UEdGraphPin*, UEdGraphPin*)> ConnectOrKeep =
                [K2Schema](UEdGraphPin* Source, UEdGraphPin* Target)
            {
                return Source != nullptr
                    && Target != nullptr
                    && (Source->LinkedTo.Contains(Target)
                        || K2Schema->TryCreateConnection(Source, Target));
            };
            if (!ConnectOrKeep(EventExecPin, LuaUpdateExec)
                || !ConnectOrKeep(SelfPin, LuaUpdateInstance)
                || !ConnectOrKeep(EventDelta, LuaUpdateDelta))
            {
                AddError(Diagnostics, K2ConnectionFailed,
                    TEXT("Failed to connect Lua BlueprintUpdateAnimation bridge."),
                    Preflight.Blueprint.SourceModule, Preflight.Blueprint.SourceLocation);
                return false;
            }
            return true;
        }

        /** 把 Graph.Layout 中的 Grid 单元格和精确坐标合并为确定性编辑器像素坐标，精确坐标最后覆盖 Grid。 */
        TMap<FString, FIntPoint> BuildExplicitLayoutPositions(const FLuaAnimIRGraph& Graph) const
        {
            constexpr int32 RegionWidth = 2400;
            constexpr int32 RegionHeight = 1600;
            TMap<FString, FIntPoint> Positions;
            for (const FLuaAnimIRLayoutGrid& Grid : Graph.Layout.Grids)
            {
                const int32 OriginX = Grid.RegionColumn * RegionWidth;
                const int32 OriginY = Grid.RegionRow * RegionHeight;
                for (const FLuaAnimIRLayoutItem& Item : Grid.Items)
                {
                    Positions.Add(Item.ElementId, FIntPoint(
                        OriginX + Item.Column * Grid.CellWidth,
                        OriginY + Item.Row * Grid.CellHeight));
                }
            }
            for (const FLuaAnimIRLayoutPosition& Position : Graph.Layout.Positions)
            {
                Positions.Add(Position.ElementId, FIntPoint(Position.X, Position.Y));
            }
            return Positions;
        }

        /**
         * 强制应用 Pose/StatePose Graph 的精确像素坐标，使其在增量生成时也不被旧编辑器坐标保留规则忽略。
         * 函数只修改已物化节点的 NodePosX/NodePosY，不改变 Graph 拓扑或 IR。
         *
         * @param Graph 待应用 Positions 的 Pose 或 StatePose IR Graph。
         * @return 无返回值；未找到的原生节点会安全跳过。
         */
        void ApplyExactPoseGraphPositions(const FLuaAnimIRGraph& Graph)
        {
            for (const FLuaAnimIRLayoutPosition& Position : Graph.Layout.Positions)
            {
                UEdGraphNode* const* NativeNode = NativeNodes.Find(Position.ElementId);
                if (NativeNode == nullptr || *NativeNode == nullptr) continue;
                (*NativeNode)->Modify();
                (*NativeNode)->NodePosX = Position.X;
                (*NativeNode)->NodePosY = Position.Y;
            }
        }

        /** 根据风格把拓扑主轴层级和同层序号转换为画布坐标。 */
        FIntPoint MakeFlowPosition(
            const ELuaAnimIRLayoutStyle Style,
            const int32 Depth,
            const int32 SecondaryIndex,
            const int32 ElementIndex) const
        {
            constexpr int32 PrimarySpacing = 400;
            constexpr int32 SecondarySpacing = 240;
            if (Style == ELuaAnimIRLayoutStyle::CompactGrid)
            {
                return FIntPoint((ElementIndex % 4) * PrimarySpacing, (ElementIndex / 4) * SecondarySpacing);
            }
            if (Style == ELuaAnimIRLayoutStyle::RightToLeft)
            {
                return FIntPoint(Depth * PrimarySpacing, SecondaryIndex * SecondarySpacing);
            }
            if (Style == ELuaAnimIRLayoutStyle::TopToBottom)
            {
                return FIntPoint(SecondaryIndex * PrimarySpacing, Depth * SecondarySpacing);
            }
            if (Style == ELuaAnimIRLayoutStyle::BottomToTop)
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
            const FLuaAnimIRGraph& Graph,
            const TMap<FString, FIntPoint>& ExplicitPositions)
        {
            constexpr int32 HorizontalSpacing = 460;
            constexpr int32 NodeBlockHeight = 140;
            constexpr int32 SiblingSpacing = 40;
            constexpr int32 DisconnectedHorizontalSpacing = 360;
            constexpr int32 DisconnectedVerticalSpacing = 200;
            constexpr int32 DisconnectedSectionSpacing = 320;

            TArray<const FLuaAnimIRNode*> OrderedNodes;
            TSet<FString> GraphNodeIds;
            for (const FLuaAnimIRNode& Node : Graph.Nodes)
            {
                OrderedNodes.Add(&Node);
                GraphNodeIds.Add(Node.Id);
            }
            OrderedNodes.Sort([](const FLuaAnimIRNode& Left, const FLuaAnimIRNode& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            TMap<FString, TArray<const FLuaAnimIRLink*>> IncomingLinks;
            for (const FLuaAnimIRLink& Link : Graph.Links)
            {
                if (!GraphNodeIds.Contains(Link.Source.NodeId)
                    || !GraphNodeIds.Contains(Link.Target.NodeId)) continue;
                IncomingLinks.FindOrAdd(Link.Target.NodeId).Add(&Link);
            }
            for (TPair<FString, TArray<const FLuaAnimIRLink*>>& Pair : IncomingLinks)
            {
                Pair.Value.Sort([](const FLuaAnimIRLink& Left, const FLuaAnimIRLink& Right)
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
                const TArray<const FLuaAnimIRLink*>* Links = IncomingLinks.Find(TargetNodeId);
                if (Links == nullptr) return;
                TSet<FString> DirectInputs;
                for (const FLuaAnimIRLink* Link : *Links)
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

            TArray<const FLuaAnimIRNode*> DisconnectedNodes;
            for (const FLuaAnimIRNode* Node : OrderedNodes)
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

            for (const FLuaAnimIRNode* Node : OrderedNodes)
            {
                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node->Id);
                if (NativeNode == nullptr || *NativeNode == nullptr) continue;
                const FIntPoint* Position = ExplicitPositions.Find(Node->Id);
                if (Position == nullptr) Position = AutomaticPositions.Find(Node->Id);
                if (Position == nullptr) continue;
                ApplyGeneratedPosition(*NativeNode, Position->X, Position->Y);
            }
        }

        /**
         * 将显式 Grid 位置应用为固定锚点，并按 Pose Link 到 Result 的反向深度排列其余节点。
         * Radial 使用反向深度作为半径分层；未连接节点按声明顺序落到额外深度。
         */
        void ApplyPoseGraphLayout(const FLuaAnimIRGraph& Graph)
        {
            ELuaAnimIRLayoutStyle Style = Graph.Layout.Style;
            const TMap<FString, FIntPoint> ExplicitPositions = BuildExplicitLayoutPositions(Graph);
            if (Style == ELuaAnimIRLayoutStyle::Auto
                || Style == ELuaAnimIRLayoutStyle::HierarchicalBlocks)
            {
                ApplyHierarchicalPoseGraphLayout(Graph, ExplicitPositions);
                ApplyExactPoseGraphPositions(Graph);
                return;
            }

            TMap<FString, int32> Depths;
            Depths.Add(Graph.RootNodeId, 0);
            for (int32 Pass = 0; Pass < Graph.Nodes.Num(); ++Pass)
            {
                bool bChanged = false;
                for (const FLuaAnimIRLink& Link : Graph.Links)
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

            TArray<const FLuaAnimIRNode*> OrderedNodes;
            for (const FLuaAnimIRNode& Node : Graph.Nodes) OrderedNodes.Add(&Node);
            OrderedNodes.Sort([](const FLuaAnimIRNode& Left, const FLuaAnimIRNode& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });
            int32 MaximumDepth = 0;
            for (const TPair<FString, int32>& Pair : Depths) MaximumDepth = FMath::Max(MaximumDepth, Pair.Value);
            for (const FLuaAnimIRNode* Node : OrderedNodes)
            {
                if (!Depths.Contains(Node->Id)) Depths.Add(Node->Id, ++MaximumDepth);
            }
            TMap<int32, int32> LayerSizes;
            for (const FLuaAnimIRNode* Node : OrderedNodes)
            {
                if (!ExplicitPositions.Contains(Node->Id)) ++LayerSizes.FindOrAdd(Depths[Node->Id]);
            }
            TMap<int32, int32> LayerCounts;
            TSet<FIntPoint> Occupied;
            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions) Occupied.Add(Pair.Value);

            for (int32 ElementIndex = 0; ElementIndex < OrderedNodes.Num(); ++ElementIndex)
            {
                const FLuaAnimIRNode& Node = *OrderedNodes[ElementIndex];
                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                if (NativeNode == nullptr || *NativeNode == nullptr) continue;
                const FIntPoint* Explicit = ExplicitPositions.Find(Node.Id);
                if (Explicit != nullptr)
                {
                    ApplyGeneratedPosition(*NativeNode, Explicit->X, Explicit->Y);
                    continue;
                }

                const int32 Depth = Depths[Node.Id];
                const int32 SecondaryIndex = LayerCounts.FindOrAdd(Depth)++;
                FIntPoint Position;
                if (Style == ELuaAnimIRLayoutStyle::Radial)
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
                    if (Style == ELuaAnimIRLayoutStyle::TopToBottom
                        || Style == ELuaAnimIRLayoutStyle::BottomToTop)
                    {
                        Position.X += 400;
                    }
                    else if (Style != ELuaAnimIRLayoutStyle::Radial)
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
                ApplyGeneratedPosition(*NativeNode, Position.X, Position.Y);
            }
            ApplyExactPoseGraphPositions(Graph);
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
        bool BuildPoseGraph(const FLuaAnimIRGraph& Graph, UAnimationGraph& NativeGraph)
        {
            NativeGraph.GraphGuid = MakeStableGuid(TEXT("Graph"), Graph.Id);
            NativeGraphs.Add(Graph.Id, &NativeGraph);

            const FLuaAnimIRNode* RootNode = nullptr;
            for (const FLuaAnimIRNode& Node : Graph.Nodes)
            {
                if (Node.Id == Graph.RootNodeId)
                {
                    RootNode = &Node;
                    break;
                }
            }

            if (RootNode == nullptr || !BindDefaultRootNode(*RootNode, NativeGraph)) return false;

            int32 NodeIndex = 0;
            for (const FLuaAnimIRNode& Node : Graph.Nodes)
            {
                if (Node.Id == Graph.RootNodeId) continue;
                if (!CreateRegisteredNode(Node, NativeGraph, NodeIndex)) return false;
                ++NodeIndex;
            }

            if (!BindCachedPoseNodes(Graph)) return false;

            for (const FLuaAnimIRLink& Link : Graph.Links)
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
        bool BindDefaultRootNode(const FLuaAnimIRNode& RootNode, UAnimationGraph& NativeGraph)
        {
            UClass* const* RootClass = Preflight.NodeClasses.Find(RootNode.Id);
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

            if (bIncremental && PreviousOwnedNodeGuids.Contains(NativeRoot->NodeGuid))
            {
                NativeRoot->Modify();
                ReusedNativeNodes.Add(NativeRoot);
            }
            NativeRoot->NodeGuid = MakeStableGuid(TEXT("Node"), RootNode.Id);
            ApplyGeneratedPosition(NativeRoot, 500, 0);
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
            const FLuaAnimIRNode& Node,
            UAnimationGraph& NativeGraph,
            const int32 NodeIndex)
        {
            UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.Id);
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
            if (!Node.EditorNodeClass.IsNull()
                && FLuaAnimGraphNodeRegistry::Find(Node.NodeType) == nullptr)
            {
                if ((*NodeClass)->IsChildOf(UK2Node_VariableGet::StaticClass()))
                {
                    const FLuaAnimIRProperty* PropertyName =
                        FindProperty(Node, TEXT("PropertyName"));
                    if (PropertyName == nullptr
                        || PropertyName->Value.Type
                            != ELuaAnimIRValueType::Name)
                    {
                        return ReportNodeCreationFailure(Node);
                    }

                    UK2Node_VariableGet* GetterNode =
                        CreateNativeNode<UK2Node_VariableGet>(
                            NativeGraph,
                            Node.Id,
                            PositionX,
                            PositionY);
                    if (GetterNode == nullptr)
                    {
                        return ReportNodeCreationFailure(Node);
                    }
                    GetterNode->VariableReference.SetSelfMember(
                        PropertyName->Value.NameValue);
                    GetterNode->ReconstructNode();
                    GetterNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                    ApplyGeneratedPosition(GetterNode, PositionX, PositionY);
                    NativeNodes.Add(Node.Id, GetterNode);
                    return true;
                }

                UAnimGraphNode_Base* AnimNode = nullptr;
                const FGuid StableNodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                if (bIncremental)
                {
                    for (UEdGraphNode* ExistingNode : NativeGraph.Nodes)
                    {
                        if (ExistingNode == nullptr || ExistingNode->NodeGuid != StableNodeGuid) continue;
                        if (ExistingNode->GetClass() == *NodeClass)
                        {
                            AnimNode = Cast<UAnimGraphNode_Base>(ExistingNode);
                            if (AnimNode != nullptr)
                            {
                                AnimNode->Modify();
                                ReusedNativeNodes.Add(AnimNode);
                            }
                        }
                        else
                        {
                            ExistingNode->Modify();
                            FBlueprintEditorUtils::RemoveNode(&Blueprint, ExistingNode, true);
                        }
                        break;
                    }
                }
                if (AnimNode == nullptr)
                {
                    UEdGraphNode* ReflectedNode = NewObject<UEdGraphNode>(
                        &NativeGraph,
                        *NodeClass,
                        NAME_None,
                        RF_Transactional);
                    AnimNode = Cast<UAnimGraphNode_Base>(ReflectedNode);
                    if (AnimNode == nullptr) return ReportNodeCreationFailure(Node);
                    NativeGraph.AddNode(AnimNode, false, false);
                    AnimNode->CreateNewGuid();
                    AnimNode->PostPlacedNewNode();
                    if (AnimNode->Pins.IsEmpty()) AnimNode->AllocateDefaultPins();
                }
                AnimNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                ApplyGeneratedPosition(AnimNode, PositionX, PositionY);

                for (const FLuaAnimIRProperty& Property : Node.Properties)
                {
                    FProperty* NativeProperty = nullptr;
                    void* PropertyContainer = nullptr;
                    if (!ResolveReflectedProperty(
                            *AnimNode,
                            Property.Name.ToString(),
                            NativeProperty,
                            PropertyContainer)
                        || NativeProperty == nullptr
                        || !WriteReflectedValue(
                            *NativeProperty,
                            PropertyContainer,
                            Property.Value))
                    {
                        AddError(
                            Diagnostics,
                            ReflectionPropertyWriteFailed,
                            FString::Printf(
                                TEXT("Reflection node class '%s' cannot write Property '%s' from IR value type %d."),
                                *(*NodeClass)->GetPathName(),
                                *Property.Name.ToString(),
                                static_cast<int32>(Property.Value.Type)),
                            Node.Id,
                            Node.SourceLocation);
                        return false;
                    }
                }

                if (!ApplyAnimNodeFunctionBindings(*AnimNode, Node)) return false;

                AnimNode->ReconstructNode();
                ExposeDeclaredReflectedInputPins(*AnimNode, Node.Pins);
                if (!SynchronizeReflectedObjectPinDefaults(
                        *AnimNode,
                        Node.Properties))
                {
                    AddError(
                        Diagnostics,
                        ReflectionPropertyWriteFailed,
                        FString::Printf(
                            TEXT("Reflection node class '%s' cannot synchronize an object Property to its native input Pin."),
                            *(*NodeClass)->GetPathName()),
                        Node.Id,
                        Node.SourceLocation);
                    return false;
                }
                NativeNodes.Add(Node.Id, AnimNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LinkedInputPose::StaticClass())
            {
                const FLuaAnimIRProperty* PoseNameProperty =
                    FindProperty(Node, TEXT("PoseName"));
                if (PoseNameProperty == nullptr) return ReportNodeCreationFailure(Node);

                UAnimGraphNode_LinkedInputPose* LinkedInputNode = nullptr;
                TArray<UAnimGraphNode_LinkedInputPose*> ExistingInputs;
                NativeGraph.GetNodesOfClass(ExistingInputs);
                for (UAnimGraphNode_LinkedInputPose* ExistingInput : ExistingInputs)
                {
                    if (ExistingInput != nullptr
                        && (ExistingInput->NodeGuid == MakeStableGuid(TEXT("Node"), Node.Id)
                            || ExistingInput->Node.Name == PoseNameProperty->Value.NameValue))
                    {
                        LinkedInputNode = ExistingInput;
                        if (bIncremental)
                        {
                            LinkedInputNode->Modify();
                            ReusedNativeNodes.Add(LinkedInputNode);
                        }
                        break;
                    }
                }
                if (LinkedInputNode == nullptr)
                {
                    FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> NodeCreator(NativeGraph);
                    LinkedInputNode = NodeCreator.CreateNode(false);
                    if (LinkedInputNode == nullptr) return ReportNodeCreationFailure(Node);
                    LinkedInputNode->Node.Name = PoseNameProperty->Value.NameValue;
                    if (CurrentLayer != nullptr)
                    {
                        bool bFirstPose = true;
                        for (const FLuaAnimIRFunctionParameter& Parameter : CurrentLayer->Parameters)
                        {
                            if (Parameter.bIsPose)
                            {
                                if (Parameter.Name == LinkedInputNode->Node.Name) break;
                                bFirstPose = false;
                            }
                        }
                        if (bFirstPose)
                        {
                            for (const FLuaAnimIRFunctionParameter& Parameter : CurrentLayer->Parameters)
                            {
                                if (Parameter.bIsPose) continue;
                                FAnimBlueprintFunctionPinInfo& Input =
                                    LinkedInputNode->Inputs.AddDefaulted_GetRef();
                                Input.Name = Parameter.Name;
                                if (Parameter.DataType == TEXT("Bool"))
                                {
                                    Input.Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
                                }
                                else if (Parameter.DataType == TEXT("Float"))
                                {
                                    Input.Type.PinCategory = UEdGraphSchema_K2::PC_Real;
                                    Input.Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
                                }
                                else if (Parameter.DataType == TEXT("Integer"))
                                {
                                    Input.Type.PinCategory = UEdGraphSchema_K2::PC_Int;
                                }
                                else if (Parameter.DataType == TEXT("Name"))
                                {
                                    Input.Type.PinCategory = UEdGraphSchema_K2::PC_Name;
                                }
                                else if (Parameter.DataType == TEXT("String"))
                                {
                                    Input.Type.PinCategory = UEdGraphSchema_K2::PC_String;
                                }
                            }
                        }
                    }
                    NodeCreator.Finalize();
                }
                LinkedInputNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                ApplyGeneratedPosition(LinkedInputNode, PositionX, PositionY);
                NativeNodes.Add(Node.Id, LinkedInputNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LinkedAnimGraph::StaticClass())
            {
                const FLuaAnimIRProperty* InstanceClassProperty =
                    FindProperty(Node, TEXT("InstanceClass"));
                UClass* InstanceClass = InstanceClassProperty != nullptr
                    ? InstanceClassProperty->Value.SoftClassPathValue.TryLoadClass<UAnimInstance>()
                    : nullptr;
                if (InstanceClass == nullptr) return ReportNodeCreationFailure(Node);

                UAnimGraphNode_LinkedAnimGraph* LinkedGraphNode =
                    CreateNativeNode<UAnimGraphNode_LinkedAnimGraph>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (LinkedGraphNode == nullptr) return ReportNodeCreationFailure(Node);
                LinkedGraphNode->Node.InstanceClass = InstanceClass;
                const FLuaAnimIRProperty* GraphNameProperty =
                    FindProperty(Node, TEXT("GraphName"));
                const FName GraphName = GraphNameProperty != nullptr
                    && !GraphNameProperty->Value.NameValue.IsNone()
                    ? GraphNameProperty->Value.NameValue
                    : UEdGraphSchema_K2::GN_AnimGraph;
                FStructProperty* FunctionReferenceProperty = FindFProperty<FStructProperty>(
                    UAnimGraphNode_LinkedAnimGraphBase::StaticClass(),
                    TEXT("FunctionReference"));
                FMemberReference* FunctionReference = FunctionReferenceProperty != nullptr
                    ? FunctionReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(
                        LinkedGraphNode)
                    : nullptr;
                if (FunctionReference == nullptr) return ReportNodeCreationFailure(Node);
                FunctionReference->SetExternalMember(GraphName, InstanceClass);
                LinkedGraphNode->ReconstructNode();
                LinkedGraphNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                ApplyGeneratedPosition(LinkedGraphNode, PositionX, PositionY);
                NativeNodes.Add(Node.Id, LinkedGraphNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_LinkedAnimLayer::StaticClass())
            {
                const FLuaAnimIRProperty* LayerNameProperty =
                    FindProperty(Node, TEXT("LayerName"));
                if (LayerNameProperty == nullptr) return ReportNodeCreationFailure(Node);
                UClass* InterfaceClass = nullptr;
                const FLuaAnimIRProperty* InterfaceClassProperty =
                    FindProperty(Node, TEXT("InterfaceClass"));
                if (InterfaceClassProperty != nullptr)
                {
                    InterfaceClass = InterfaceClassProperty->Value.SoftClassPathValue
                        .TryLoadClass<UAnimLayerInterface>();
                }
                if (InterfaceClass == nullptr)
                {
                    for (UClass* CandidateInterface : Preflight.ImplementedInterfaceClasses)
                    {
                        if (CandidateInterface != nullptr
                            && CandidateInterface->FindFunctionByName(
                                LayerNameProperty->Value.NameValue) != nullptr)
                        {
                            InterfaceClass = CandidateInterface;
                            break;
                        }
                    }
                }

                UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode =
                    CreateNativeNode<UAnimGraphNode_LinkedAnimLayer>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (LinkedLayerNode == nullptr) return ReportNodeCreationFailure(Node);
                LinkedLayerNode->Node.Interface = InterfaceClass;
                const FLuaAnimIRProperty* InstanceClassProperty =
                    FindProperty(Node, TEXT("InstanceClass"));
                if (InstanceClassProperty != nullptr)
                {
                    LinkedLayerNode->Node.InstanceClass =
                        InstanceClassProperty->Value.SoftClassPathValue.TryLoadClass<UAnimInstance>();
                }
                LinkedLayerNode->Node.Layer = LayerNameProperty->Value.NameValue;
                FStructProperty* FunctionReferenceProperty = FindFProperty<FStructProperty>(
                    UAnimGraphNode_LinkedAnimGraphBase::StaticClass(),
                    TEXT("FunctionReference"));
                FMemberReference* FunctionReference = FunctionReferenceProperty != nullptr
                    ? FunctionReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(
                        LinkedLayerNode)
                    : nullptr;
                if (FunctionReference == nullptr) return ReportNodeCreationFailure(Node);
                if (InterfaceClass != nullptr)
                {
                    FGuid FunctionGuid;
                    FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
                        FBlueprintEditorUtils::GetMostUpToDateClass(InterfaceClass),
                        LayerNameProperty->Value.NameValue,
                        FunctionGuid);
                    FunctionReference->SetExternalMember(
                        LayerNameProperty->Value.NameValue,
                        InterfaceClass,
                        FunctionGuid);
                    LinkedLayerNode->InterfaceGuid =
                        FBlueprintEditorUtils::FindInterfaceGraphGuid(
                            LayerNameProperty->Value.NameValue,
                            InterfaceClass);
                }
                else
                {
                    FunctionReference->SetSelfMember(
                        LayerNameProperty->Value.NameValue);
                }
                LinkedLayerNode->ReconstructNode();
                if (InterfaceClass != nullptr
                    && !EnsureLinkedAnimLayerPosePins(
                        *LinkedLayerNode,
                        *InterfaceClass,
                        LayerNameProperty->Value.NameValue,
                        Node))
                {
                    return false;
                }
                LinkedLayerNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                ApplyGeneratedPosition(LinkedLayerNode, PositionX, PositionY);
                NativeNodes.Add(Node.Id, LinkedLayerNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_SequencePlayer::StaticClass())
            {
                UAnimGraphNode_SequencePlayer* SequenceNode =
                    CreateNativeNode<UAnimGraphNode_SequencePlayer>(NativeGraph, Node.Id, PositionX, PositionY);
                if (SequenceNode == nullptr) return ReportNodeCreationFailure(Node);

                UAnimSequenceBase* const* Sequence = Preflight.SequenceAssets.Find(Node.Id);
                if (Sequence == nullptr) return ReportNodeCreationFailure(Node);
                SequenceNode->Node.SetSequence(*Sequence);

                const FLuaAnimIRProperty* LoopProperty = FindProperty(Node, TEXT("bLoopAnimation"));
                if (LoopProperty != nullptr)
                {
                    SequenceNode->Node.SetLoopAnimation(LoopProperty->Value.BoolValue);
                }

                const FLuaAnimIRProperty* PlayRateProperty = FindProperty(Node, TEXT("PlayRate"));
                if (PlayRateProperty != nullptr)
                {
                    SequenceNode->Node.SetPlayRate(static_cast<float>(PlayRateProperty->Value.FloatValue));
                }

                const FLuaAnimIRProperty* StartPositionProperty = FindProperty(Node, TEXT("StartPosition"));
                if (StartPositionProperty != nullptr)
                {
                    SequenceNode->Node.SetStartPosition(
                        static_cast<float>(StartPositionProperty->Value.FloatValue));
                }

                const FLuaAnimIRProperty* GroupNameProperty = FindProperty(Node, TEXT("GroupName"));
                if (GroupNameProperty != nullptr)
                {
                    SequenceNode->Node.SetGroupName(GroupNameProperty->Value.NameValue);
                    SequenceNode->Node.SetGroupMethod(EAnimSyncMethod::SyncGroup);
                }
                const FLuaAnimIRProperty* GroupRoleProperty = FindProperty(Node, TEXT("GroupRole"));
                if (GroupRoleProperty != nullptr)
                {
                    EAnimGroupRole::Type NativeRole = EAnimGroupRole::CanBeLeader;
                    ParseNativeEnumValue(GroupRoleProperty->Value.IntegerValue, NativeRole);
                    SequenceNode->Node.SetGroupRole(NativeRole);
                }
                const FLuaAnimIRProperty* GroupMethodProperty = FindProperty(Node, TEXT("GroupMethod"));
                if (GroupMethodProperty != nullptr)
                {
                    EAnimSyncMethod NativeMethod = EAnimSyncMethod::SyncGroup;
                    ParseNativeEnumValue(GroupMethodProperty->Value.IntegerValue, NativeMethod);
                    SequenceNode->Node.SetGroupMethod(NativeMethod);
                }

                NativeNodes.Add(Node.Id, SequenceNode);
                return true;
            }

            if (*NodeClass == UK2Node_VariableGet::StaticClass())
            {
                const FLuaAnimIRProperty* PropertyName = FindProperty(Node, TEXT("PropertyName"));
                if (PropertyName == nullptr) return ReportNodeCreationFailure(Node);
                UK2Node_VariableGet* GetterNode =
                    CreateNativeNode<UK2Node_VariableGet>(
                        NativeGraph,
                        Node.Id,
                        PositionX,
                        PositionY);
                if (GetterNode == nullptr) return ReportNodeCreationFailure(Node);
                GetterNode->VariableReference.SetSelfMember(PropertyName->Value.NameValue);
                GetterNode->ReconstructNode();
                GetterNode->NodeGuid = MakeStableGuid(TEXT("Node"), Node.Id);
                ApplyGeneratedPosition(GetterNode, PositionX, PositionY);
                NativeNodes.Add(Node.Id, GetterNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_BlendListByBool::StaticClass())
            {
                UAnimGraphNode_BlendListByBool* BlendNode =
                    CreateNativeNode<UAnimGraphNode_BlendListByBool>(NativeGraph, Node.Id, PositionX, PositionY);
                if (BlendNode == nullptr) return ReportNodeCreationFailure(Node);
                const FLuaAnimIRProperty* BlendTime = FindProperty(Node, TEXT("BlendTime"));
                if (BlendTime != nullptr) SetBlendListTimes(BlendNode->Node, static_cast<float>(BlendTime->Value.FloatValue));
                NativeNodes.Add(Node.Id, BlendNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_BlendListByEnum::StaticClass())
            {
                UAnimGraphNode_BlendListByEnum* BlendNode =
                    CreateNativeNode<UAnimGraphNode_BlendListByEnum>(NativeGraph, Node.Id, PositionX, PositionY);
                UEnum* const* EnumType = Preflight.NodeEnums.Find(Node.Id);
                const FLuaAnimIRProperty* EntriesProperty = FindProperty(Node, TEXT("EnumEntries"));
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
                const FLuaAnimIRProperty* BlendTime = FindProperty(Node, TEXT("BlendTime"));
                if (BlendTime != nullptr) SetBlendListTimes(BlendNode->Node, static_cast<float>(BlendTime->Value.FloatValue));
                NativeNodes.Add(Node.Id, BlendNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_Slot::StaticClass())
            {
                UAnimGraphNode_Slot* SlotNode =
                    CreateNativeNode<UAnimGraphNode_Slot>(NativeGraph, Node.Id, PositionX, PositionY);
                const FLuaAnimIRProperty* SlotNameProperty = FindProperty(Node, TEXT("SlotName"));
                if (SlotNode == nullptr || SlotNameProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                SlotNode->Node.SlotName = SlotNameProperty->Value.NameValue;
                const FLuaAnimIRProperty* AlwaysUpdateProperty =
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
                const FLuaAnimIRProperty* RotationBlend =
                    FindProperty(Node, TEXT("bMeshSpaceRotationBlend"));
                if (RotationBlend != nullptr)
                {
                    LayeredNode->Node.bMeshSpaceRotationBlend = RotationBlend->Value.BoolValue;
                }
                const FLuaAnimIRProperty* ScaleBlend =
                    FindProperty(Node, TEXT("bMeshSpaceScaleBlend"));
                if (ScaleBlend != nullptr)
                {
                    LayeredNode->Node.bMeshSpaceScaleBlend = ScaleBlend->Value.BoolValue;
                }
                const FLuaAnimIRProperty* CurveOption =
                    FindProperty(Node, TEXT("CurveBlendOption"));
                if (CurveOption != nullptr)
                {
                    ECurveBlendOption::Type ParsedCurveOption = ECurveBlendOption::Override;
                    if (!ParseNativeEnumValue(CurveOption->Value.IntegerValue, ParsedCurveOption))
                    {
                        return ReportNodeCreationFailure(Node);
                    }
                    LayeredNode->Node.CurveBlendOption = ParsedCurveOption;
                }
                const FLuaAnimIRProperty* BlendRootMotion =
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
                const FLuaAnimIRProperty* SpineBonesProperty = FindProperty(Node, TEXT("SpineBones"));
                const FLuaAnimIRProperty* FootRootProperty = FindProperty(Node, TEXT("IKFootRootBone"));
                const FLuaAnimIRProperty* FootBonesProperty = FindProperty(Node, TEXT("IKFootBones"));
                if (OrientationNode == nullptr
                    || SpineBonesProperty == nullptr
                    || FootRootProperty == nullptr
                    || FootBonesProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                const FLuaAnimIRProperty* ModeProperty = FindProperty(Node, TEXT("Mode"));
                if (ModeProperty != nullptr)
                {
                    ParseNativeEnumValue(
                        ModeProperty->Value.IntegerValue,
                        OrientationNode->Node.Mode);
                }
                if (!ReusedNativeNodes.Contains(OrientationNode))
                {
                    OrientationNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                }
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

                const FLuaAnimIRProperty* RotationAxisProperty = FindProperty(Node, TEXT("RotationAxis"));
                if (RotationAxisProperty != nullptr)
                {
                    ParseNativeEnumValue(
                        RotationAxisProperty->Value.IntegerValue,
                        OrientationNode->Node.RotationAxis);
                }

                const FLuaAnimIRProperty* DistributionProperty =
                    FindProperty(Node, TEXT("DistributedBoneOrientationAlpha"));
                if (DistributionProperty != nullptr)
                {
                    OrientationNode->Node.DistributedBoneOrientationAlpha = FMath::Clamp(
                        static_cast<float>(DistributionProperty->Value.FloatValue),
                        0.0f,
                        1.0f);
                }

                const FLuaAnimIRProperty* InterpSpeedProperty =
                    FindProperty(Node, TEXT("RotationInterpSpeed"));
                if (InterpSpeedProperty != nullptr)
                {
                    OrientationNode->Node.RotationInterpSpeed = FMath::Max(
                        0.0f,
                        static_cast<float>(InterpSpeedProperty->Value.FloatValue));
                }

                const FLuaAnimIRProperty* MinRootMotionSpeedProperty =
                    FindProperty(Node, TEXT("MinRootMotionSpeedThreshold"));
                if (MinRootMotionSpeedProperty != nullptr)
                {
                    OrientationNode->Node.MinRootMotionSpeedThreshold = FMath::Max(
                        0.0f,
                        static_cast<float>(MinRootMotionSpeedProperty->Value.FloatValue));
                }

                const FLuaAnimIRProperty* LocomotionDeltaProperty =
                    FindProperty(Node, TEXT("LocomotionAngleDeltaThreshold"));
                if (LocomotionDeltaProperty != nullptr)
                {
                    OrientationNode->Node.LocomotionAngleDeltaThreshold = FMath::Clamp(
                        static_cast<float>(LocomotionDeltaProperty->Value.FloatValue),
                        0.0f,
                        180.0f);
                }

#if UE_VERSION_OLDER_THAN(5, 8, 0)
                const FLuaAnimIRProperty* WarpingAlphaProperty =
                    FindProperty(Node, TEXT("WarpingAlpha"));
                if (WarpingAlphaProperty != nullptr)
                {
                    OrientationNode->Node.WarpingAlpha = FMath::Clamp(
                        static_cast<float>(WarpingAlphaProperty->Value.FloatValue),
                        0.0f,
                        1.0f);
                }

                const FLuaAnimIRProperty* OffsetAlphaProperty =
                    FindProperty(Node, TEXT("OffsetAlpha"));
                if (OffsetAlphaProperty != nullptr)
                {
                    OrientationNode->Node.OffsetAlpha = FMath::Clamp(
                        static_cast<float>(OffsetAlphaProperty->Value.FloatValue),
                        0.0f,
                        1.0f);
                }

                const FLuaAnimIRProperty* MaxOffsetAngleProperty =
                    FindProperty(Node, TEXT("MaxOffsetAngle"));
                if (MaxOffsetAngleProperty != nullptr)
                {
                    OrientationNode->Node.MaxOffsetAngle = FMath::Clamp(
                        static_cast<float>(MaxOffsetAngleProperty->Value.FloatValue),
                        0.0f,
                        180.0f);
                }
#endif

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
                const FLuaAnimIRProperty* FootRootProperty = FindProperty(Node, TEXT("IKFootRootBone"));
                const FLuaAnimIRProperty* PelvisProperty = FindProperty(Node, TEXT("PelvisBone"));
                const TArray<FFootPlacemenLegDefinition>* LegDefinitions =
                    Preflight.FootPlacementLegDefinitions.Find(Node.Id);
                if (FootPlacementNode == nullptr
                    || FootRootProperty == nullptr
                    || PelvisProperty == nullptr
                    || LegDefinitions == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                if (!ReusedNativeNodes.Contains(FootPlacementNode))
                {
                    FootPlacementNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                }
                FootPlacementNode->Node.IKFootRootBone = FBoneReference(FootRootProperty->Value.NameValue);
                FootPlacementNode->Node.PelvisBone = FBoneReference(PelvisProperty->Value.NameValue);
                FootPlacementNode->Node.LegDefinitions = *LegDefinitions;
                const FLuaAnimIRProperty* PlantSpeedModeProperty =
                    FindProperty(Node, TEXT("PlantSpeedMode"));
                if (PlantSpeedModeProperty != nullptr)
                {
                    ParseNativeEnumValue(
                        PlantSpeedModeProperty->Value.IntegerValue,
                        FootPlacementNode->Node.PlantSpeedMode);
                }
                const EFootPlacementLockType* PlantLockType =
                    Preflight.FootPlacementLockTypes.Find(Node.Id);
                if (PlantLockType != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.LockType = *PlantLockType;
                }

                const FLuaAnimIRProperty* PelvisMaxOffset = FindProperty(Node, TEXT("PelvisMaxOffset"));
                if (PelvisMaxOffset != nullptr)
                {
                    FootPlacementNode->Node.PelvisSettings.MaxOffset =
                        static_cast<float>(PelvisMaxOffset->Value.FloatValue);
                }
                const FLuaAnimIRProperty* PelvisRebalancing =
                    FindProperty(Node, TEXT("PelvisHorizontalRebalancingWeight"));
                if (PelvisRebalancing != nullptr)
                {
                    FootPlacementNode->Node.PelvisSettings.HorizontalRebalancingWeight =
                        static_cast<float>(PelvisRebalancing->Value.FloatValue);
                }
                const FLuaAnimIRProperty* PlantSpeedThreshold =
                    FindProperty(Node, TEXT("PlantSpeedThreshold"));
                if (PlantSpeedThreshold != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.SpeedThreshold =
                        static_cast<float>(PlantSpeedThreshold->Value.FloatValue);
                }
                const FLuaAnimIRProperty* PlantDistanceToGround =
                    FindProperty(Node, TEXT("PlantDistanceToGround"));
                if (PlantDistanceToGround != nullptr)
                {
                    FootPlacementNode->Node.PlantSettings.DistanceToGround =
                        static_cast<float>(PlantDistanceToGround->Value.FloatValue);
                }
                const FLuaAnimIRProperty* TraceStartOffset = FindProperty(Node, TEXT("TraceStartOffset"));
                if (TraceStartOffset != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.StartOffset =
                        static_cast<float>(TraceStartOffset->Value.FloatValue);
                }
                const FLuaAnimIRProperty* TraceEndOffset = FindProperty(Node, TEXT("TraceEndOffset"));
                if (TraceEndOffset != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.EndOffset =
                        static_cast<float>(TraceEndOffset->Value.FloatValue);
                }
                const FLuaAnimIRProperty* TraceSweepRadius = FindProperty(Node, TEXT("TraceSweepRadius"));
                if (TraceSweepRadius != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.SweepRadius =
                        static_cast<float>(TraceSweepRadius->Value.FloatValue);
                }
                const FLuaAnimIRProperty* TraceMaxGroundPenetration =
                    FindProperty(Node, TEXT("TraceMaxGroundPenetration"));
                if (TraceMaxGroundPenetration != nullptr)
                {
                    FootPlacementNode->Node.TraceSettings.MaxGroundPenetration =
                        static_cast<float>(TraceMaxGroundPenetration->Value.FloatValue);
                }
                const FLuaAnimIRProperty* TraceEnabled = FindProperty(Node, TEXT("bTraceEnabled"));
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

                if (!ReusedNativeNodes.Contains(LegIKNode))
                {
                    LegIKNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
                }
                LegIKNode->Node.LegsDefinition = *LegDefinitions;
                const FLuaAnimIRProperty* ReachPrecision = FindProperty(Node, TEXT("ReachPrecision"));
                if (ReachPrecision != nullptr)
                {
                    LegIKNode->Node.ReachPrecision = static_cast<float>(ReachPrecision->Value.FloatValue);
                }
                const FLuaAnimIRProperty* MaxIterations = FindProperty(Node, TEXT("MaxIterations"));
                if (MaxIterations != nullptr)
                {
                    LegIKNode->Node.MaxIterations = static_cast<int32>(MaxIterations->Value.IntegerValue);
                }

                LegIKNode->ReconstructNode();
                NativeNodes.Add(Node.Id, LegIKNode);
                return true;
            }

            if (*NodeClass == UAnimGraphNode_TwoBoneIK::StaticClass())
            {
                UAnimGraphNode_TwoBoneIK* TwoBoneIKNode = CreateNativeNode<UAnimGraphNode_TwoBoneIK>(
                    NativeGraph,
                    Node.Id,
                    PositionX,
                    PositionY);
                const FLuaAnimIRProperty* IKBoneProperty = FindProperty(Node, TEXT("IKBone"));
                if (TwoBoneIKNode == nullptr || IKBoneProperty == nullptr)
                {
                    return ReportNodeCreationFailure(Node);
                }

                TwoBoneIKNode->Node.IKBone = FBoneReference(IKBoneProperty->Value.NameValue);
                const FLuaAnimIRProperty* EffectorSpaceProperty =
                    FindProperty(Node, TEXT("EffectorLocationSpace"));
                if (EffectorSpaceProperty != nullptr)
                {
                    EBoneControlSpace EffectorSpace = BCS_ComponentSpace;
                    ParseNativeEnumValue(
                        EffectorSpaceProperty->Value.IntegerValue,
                        EffectorSpace);
                    TwoBoneIKNode->Node.EffectorLocationSpace = EffectorSpace;
                }
                const FLuaAnimIRProperty* JointSpaceProperty =
                    FindProperty(Node, TEXT("JointTargetLocationSpace"));
                if (JointSpaceProperty != nullptr)
                {
                    EBoneControlSpace JointSpace = BCS_ComponentSpace;
                    ParseNativeEnumValue(
                        JointSpaceProperty->Value.IntegerValue,
                        JointSpace);
                    TwoBoneIKNode->Node.JointTargetLocationSpace = JointSpace;
                }

                const FLuaAnimIRProperty* EffectorBoneProperty =
                    FindProperty(Node, TEXT("EffectorTargetBoneName"));
                const FLuaAnimIRProperty* EffectorSocketProperty =
                    FindProperty(Node, TEXT("EffectorTargetSocketName"));
                if (EffectorSocketProperty != nullptr
                    && !EffectorSocketProperty->Value.NameValue.IsNone())
                {
                    TwoBoneIKNode->Node.EffectorTarget =
                        FBoneSocketTarget(EffectorSocketProperty->Value.NameValue, true);
                }
                else if (EffectorBoneProperty != nullptr)
                {
                    TwoBoneIKNode->Node.EffectorTarget =
                        FBoneSocketTarget(EffectorBoneProperty->Value.NameValue, false);
                }

                const FLuaAnimIRProperty* JointBoneProperty =
                    FindProperty(Node, TEXT("JointTargetBoneName"));
                const FLuaAnimIRProperty* JointSocketProperty =
                    FindProperty(Node, TEXT("JointTargetSocketName"));
                if (JointSocketProperty != nullptr && !JointSocketProperty->Value.NameValue.IsNone())
                {
                    TwoBoneIKNode->Node.JointTarget =
                        FBoneSocketTarget(JointSocketProperty->Value.NameValue, true);
                }
                else if (JointBoneProperty != nullptr)
                {
                    TwoBoneIKNode->Node.JointTarget =
                        FBoneSocketTarget(JointBoneProperty->Value.NameValue, false);
                }

                const auto ReadFloatProperty = [&Node](const TCHAR* PropertyName, const double DefaultValue)
                {
                    const FLuaAnimIRProperty* Property = FindProperty(Node, PropertyName);
                    return Property != nullptr ? Property->Value.FloatValue : DefaultValue;
                };
                TwoBoneIKNode->Node.EffectorLocation = FVector(
                    ReadFloatProperty(
                        TEXT("EffectorLocationX"),
                        TwoBoneIKNode->Node.EffectorLocation.X),
                    ReadFloatProperty(
                        TEXT("EffectorLocationY"),
                        TwoBoneIKNode->Node.EffectorLocation.Y),
                    ReadFloatProperty(
                        TEXT("EffectorLocationZ"),
                        TwoBoneIKNode->Node.EffectorLocation.Z));
                TwoBoneIKNode->Node.JointTargetLocation = FVector(
                    ReadFloatProperty(
                        TEXT("JointTargetLocationX"),
                        TwoBoneIKNode->Node.JointTargetLocation.X),
                    ReadFloatProperty(
                        TEXT("JointTargetLocationY"),
                        TwoBoneIKNode->Node.JointTargetLocation.Y),
                    ReadFloatProperty(
                        TEXT("JointTargetLocationZ"),
                        TwoBoneIKNode->Node.JointTargetLocation.Z));

                const FLuaAnimIRProperty* TakeEffectorRotationProperty =
                    FindProperty(Node, TEXT("bTakeRotationFromEffectorSpace"));
                if (TakeEffectorRotationProperty != nullptr)
                {
                    TwoBoneIKNode->Node.bTakeRotationFromEffectorSpace =
                        TakeEffectorRotationProperty->Value.BoolValue;
                }
                const FLuaAnimIRProperty* AllowStretchingProperty =
                    FindProperty(Node, TEXT("bAllowStretching"));
                if (AllowStretchingProperty != nullptr)
                {
                    TwoBoneIKNode->Node.bAllowStretching = AllowStretchingProperty->Value.BoolValue;
                }
                TwoBoneIKNode->Node.StartStretchRatio = FMath::Max(
                    0.0,
                    ReadFloatProperty(
                        TEXT("StartStretchRatio"),
                        TwoBoneIKNode->Node.StartStretchRatio));
                TwoBoneIKNode->Node.MaxStretchScale = FMath::Max(
                    0.0,
                    ReadFloatProperty(
                        TEXT("MaxStretchScale"),
                        TwoBoneIKNode->Node.MaxStretchScale));

                const FLuaAnimIRProperty* AlphaInputTypeProperty =
                    FindProperty(Node, TEXT("AlphaInputType"));
                if (AlphaInputTypeProperty != nullptr)
                {
                    ParseNativeEnumValue(
                        AlphaInputTypeProperty->Value.IntegerValue,
                        TwoBoneIKNode->Node.AlphaInputType);
                }
                const FLuaAnimIRProperty* AlphaCurveProperty =
                    FindProperty(Node, TEXT("AlphaCurveName"));
                if (AlphaCurveProperty != nullptr)
                {
                    TwoBoneIKNode->Node.AlphaCurveName = AlphaCurveProperty->Value.NameValue;
                }

                TwoBoneIKNode->ReconstructNode();
                NativeNodes.Add(Node.Id, TwoBoneIKNode);
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
                const FLuaAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
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
                const FLuaAnimIRGraph* OwnedGraph = FindGraph(Node.OwnedGraphId);
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
        bool BindCachedPoseNodes(const FLuaAnimIRGraph& Graph)
        {
            TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveNodesByCacheName;
            for (const FLuaAnimIRNode& Node : Graph.Nodes)
            {
                UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.Id);
                if (NodeClass == nullptr || *NodeClass != UAnimGraphNode_SaveCachedPose::StaticClass()) continue;

                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                UAnimGraphNode_SaveCachedPose* SaveNode = NativeNode != nullptr
                    ? Cast<UAnimGraphNode_SaveCachedPose>(*NativeNode)
                    : nullptr;
                const FLuaAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
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

            for (const FLuaAnimIRNode& Node : Graph.Nodes)
            {
                UClass* const* NodeClass = Preflight.NodeClasses.Find(Node.Id);
                if (NodeClass == nullptr || *NodeClass != UAnimGraphNode_UseCachedPose::StaticClass()) continue;

                UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
                UAnimGraphNode_UseCachedPose* UseNode = NativeNode != nullptr
                    ? Cast<UAnimGraphNode_UseCachedPose>(*NativeNode)
                    : nullptr;
                const FLuaAnimIRProperty* CacheNameProperty = FindProperty(Node, TEXT("CacheName"));
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
         * 将 IR 声明的线程安全函数引用写入反射 AnimGraph 节点。
         * 只能在游戏线程、节点尚未编译且归当前 Builder 独占时调用；本函数只绑定 Self/父类函数，
         * 不创建函数图、不执行回调，也不允许 IR 覆盖节点类声明的 PrototypeFunction 元数据。
         *
         * @param AnimNode 接收 FMemberReference 的原生动画节点，必须与 Node.EditorNodeClass 一致。
         * @param Node 提供函数属性名、目标函数名与原型路径的只读 IR 节点。
         * @return 所有绑定都能解析、签名兼容且线程安全时返回 true；否则追加稳定诊断并返回 false。
         */
        bool ApplyAnimNodeFunctionBindings(
            UAnimGraphNode_Base& AnimNode,
            const FLuaAnimIRNode& Node)
        {
            UClass* FunctionScope = Blueprint.SkeletonGeneratedClass != nullptr
                ? Blueprint.SkeletonGeneratedClass
                : Blueprint.ParentClass;
            for (const FLuaAnimIRNodeFunctionBinding& Binding : Node.FunctionBindings)
            {
                FStructProperty* ReferenceProperty = FindFProperty<FStructProperty>(
                    AnimNode.GetClass(),
                    Binding.PropertyName);
                const FString NativePrototypePath = ReferenceProperty != nullptr
                    ? ReferenceProperty->GetMetaData(TEXT("PrototypeFunction"))
                    : FString();
                UFunction* PrototypeFunction = !NativePrototypePath.IsEmpty()
                    ? FindObject<UFunction>(nullptr, *NativePrototypePath)
                    : nullptr;
                UFunction* TargetFunction = FunctionScope != nullptr
                    ? FunctionScope->FindFunctionByName(Binding.FunctionName)
                    : nullptr;
                const bool bBindingValid = ReferenceProperty != nullptr
                    && ReferenceProperty->Struct == FMemberReference::StaticStruct()
                    && NativePrototypePath == Binding.PrototypeFunction
                    && PrototypeFunction != nullptr
                    && TargetFunction != nullptr
                    && PrototypeFunction->IsSignatureCompatibleWith(TargetFunction)
                    && FBlueprintEditorUtils::HasFunctionBlueprintThreadSafeMetaData(TargetFunction);
                if (!bBindingValid)
                {
                    AddError(
                        Diagnostics,
                        AnimNodeFunctionBindingFailed,
                        FString::Printf(
                            TEXT("Node '%s' cannot bind Anim Node Function property '%s' to self function '%s' with prototype '%s'."),
                            *Node.Id,
                            *Binding.PropertyName.ToString(),
                            *Binding.FunctionName.ToString(),
                            *Binding.PrototypeFunction),
                        Node.Id,
                        Node.SourceLocation);
                    return false;
                }

                FMemberReference* FunctionReference =
                    ReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(&AnimNode);
                if (FunctionReference == nullptr) return false;
                FunctionReference->SetSelfMember(Binding.FunctionName);
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
        bool ReportNodeCreationFailure(const FLuaAnimIRNode& Node)
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
         * 为单个 Gate AST 节点生成稳定、可读且不依赖编辑器显示名的采样标签。
         * 本函数只格式化值类型字符串，可在当前 Builder 所在线程调用，不访问或修改 UObject。
         *
         * @param GateNode 提供节点类型和可选参数名的只读 IR 节点。
         * @param GateIndex 节点在扁平 Gate 数组中的零基稳定索引，必须非负。
         * @return 格式为“索引:类型[:名称]”的标签；Name 为空时省略末段。
         */
        FString MakeTransitionExpressionLabel(
            const FLuaAnimIRTransitionGateNode& GateNode,
            const int32 GateIndex) const
        {
            const FString Prefix = FString::Printf(
                TEXT("%d:%s"),
                GateIndex,
                *GateNode.Type.ToString());
            return GateNode.Name.IsNone()
                ? Prefix
                : Prefix + TEXT(":") + GateNode.Name.ToString();
        }

        /**
         * 在现有 Bool 叶节点之后插入单个 BlueprintPure 调试透传节点，并返回其唯一 Result 输出。
         * ActualValue 和 Result 分别连接既有 Getter 与比较节点，调用方后续只消费透传输出，因此不会重复构建表达式。
         * 必须在游戏线程构建独占的 Transition Graph 时调用；函数只修改 Graph，不执行采样。
         *
         * @param Transition 提供稳定 ID 与诊断位置的只读 Transition。
         * @param GateIndex 当前 BoolProperty 节点的零基稳定索引。
         * @param ExpressionLabel 当前 AST 节点稳定标签，不能为空。
         * @param ParameterName 被读取的 AnimInstance Bool 属性名，不能为空。
         * @param ExpectedValue 该叶节点比较使用的期望布尔值。
         * @param Graph 接收调试调用节点的 Transition Graph，由当前 Builder 独占。
         * @param SelfPin 当前 AnimInstance Self 输出 Pin，所有权仍属于 Graph。
         * @param ActualValuePin 既有 Property Getter 的 Bool 输出 Pin。
         * @param ResultValuePin 既有 Bool 比较的结果 Pin。
         * @return 成功时返回调试透传 Bool 输出；函数或 Pin 缺失、连接失败时返回 nullptr。
         */
        UEdGraphPin* BuildBoolTransitionDebugValueNode(
            const FLuaAnimIRTransition& Transition,
            const int32 GateIndex,
            const FString& ExpressionLabel,
            const FString& ParameterName,
            const bool ExpectedValue,
            UAnimationTransitionGraph& Graph,
            UEdGraphPin& SelfPin,
            UEdGraphPin& ActualValuePin,
            UEdGraphPin& ResultValuePin)
        {
            UFunction* RecordFunction = ULuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(
                    ULuaTransitionRuntimeLibrary,
                    RecordBoolTransitionDebugValue));
            if (RecordFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2FunctionNotFound,
                    TEXT("RecordBoolTransitionDebugValue function was not found."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            UK2Node_CallFunction* RecordNode = CreateCallFunctionNode(
                Graph,
                TEXT("Gate.Trace.Bool.") + Transition.Id + TEXT(".") + FString::FromInt(GateIndex),
                RecordFunction,
                560,
                GateIndex * 120);
            UEdGraphPin* AnimInstancePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("AnimInstance"), EGPD_Input)
                : nullptr;
            UEdGraphPin* TransitionIdPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("TransitionId"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ExpressionLabelPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ExpressionLabel"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ParameterNamePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ParameterName"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ActualValueInput = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ActualValue"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ExpectedValuePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ExpectedValue"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ResultInput = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("Result"), EGPD_Input)
                : nullptr;
            UEdGraphPin* IsFinalPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("bIsFinal"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ReturnPin = RecordNode != nullptr ? RecordNode->GetReturnValuePin() : nullptr;
            if (AnimInstancePin == nullptr
                || TransitionIdPin == nullptr
                || ExpressionLabelPin == nullptr
                || ParameterNamePin == nullptr
                || ActualValueInput == nullptr
                || ExpectedValuePin == nullptr
                || ResultInput == nullptr
                || IsFinalPin == nullptr
                || ReturnPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("Bool Transition debug pass-through did not expose required Pins."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            Schema->TrySetDefaultValue(*TransitionIdPin, Transition.Id, false);
            Schema->TrySetDefaultValue(*ExpressionLabelPin, ExpressionLabel, false);
            Schema->TrySetDefaultValue(*ParameterNamePin, ParameterName, false);
            Schema->TrySetDefaultValue(*ExpectedValuePin, ExpectedValue ? TEXT("true") : TEXT("false"), false);
            Schema->TrySetDefaultValue(*IsFinalPin, TEXT("false"), false);
            if (!Schema->TryCreateConnection(&SelfPin, AnimInstancePin)
                || !Schema->TryCreateConnection(&ActualValuePin, ActualValueInput)
                || !Schema->TryCreateConnection(&ResultValuePin, ResultInput))
            {
                AddError(
                    Diagnostics,
                    K2ConnectionFailed,
                    TEXT("Failed to connect Bool Transition debug pass-through."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }
            return ReturnPin;
        }

        /**
         * 在现有 Float 叶节点之后插入单个 BlueprintPure 调试透传节点，并返回其唯一 Result 输出。
         * Getter 输出会同时馈入既有比较和采样参数，但表达式子树只构建一次，后续仅消费透传结果。
         * 必须在游戏线程构建独占的 Transition Graph 时调用；函数只修改 Graph，不执行采样。
         *
         * @param Transition 提供稳定 ID 与诊断位置的只读 Transition。
         * @param GateIndex 当前 Float Gate 节点的零基稳定索引。
         * @param ExpressionLabel 当前 AST 节点稳定标签，不能为空。
         * @param ParameterName 曲线名或 RelevantTimeRemaining 等稳定参数名，不能为空。
         * @param Threshold 既有比较节点使用的有限阈值。
         * @param Graph 接收调试调用节点的 Transition Graph，由当前 Builder 独占。
         * @param SelfPin 当前 AnimInstance Self 输出 Pin，所有权仍属于 Graph。
         * @param ActualValuePin 既有 Curve/Time Getter 的 Float 输出 Pin。
         * @param ResultValuePin 既有 Float 比较的 Bool 结果 Pin。
         * @return 成功时返回调试透传 Bool 输出；函数或 Pin 缺失、连接失败时返回 nullptr。
         */
        UEdGraphPin* BuildFloatTransitionDebugValueNode(
            const FLuaAnimIRTransition& Transition,
            const int32 GateIndex,
            const FString& ExpressionLabel,
            const FString& ParameterName,
            const float Threshold,
            UAnimationTransitionGraph& Graph,
            UEdGraphPin& SelfPin,
            UEdGraphPin& ActualValuePin,
            UEdGraphPin& ResultValuePin)
        {
            UFunction* RecordFunction = ULuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(
                    ULuaTransitionRuntimeLibrary,
                    RecordFloatTransitionDebugValue));
            if (RecordFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2FunctionNotFound,
                    TEXT("RecordFloatTransitionDebugValue function was not found."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            UK2Node_CallFunction* RecordNode = CreateCallFunctionNode(
                Graph,
                TEXT("Gate.Trace.Float.") + Transition.Id + TEXT(".") + FString::FromInt(GateIndex),
                RecordFunction,
                560,
                GateIndex * 120);
            UEdGraphPin* AnimInstancePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("AnimInstance"), EGPD_Input)
                : nullptr;
            UEdGraphPin* TransitionIdPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("TransitionId"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ExpressionLabelPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ExpressionLabel"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ParameterNamePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ParameterName"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ActualValueInput = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ActualValue"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ThresholdPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("Threshold"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ResultInput = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("Result"), EGPD_Input)
                : nullptr;
            UEdGraphPin* IsFinalPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("bIsFinal"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ReturnPin = RecordNode != nullptr ? RecordNode->GetReturnValuePin() : nullptr;
            if (AnimInstancePin == nullptr
                || TransitionIdPin == nullptr
                || ExpressionLabelPin == nullptr
                || ParameterNamePin == nullptr
                || ActualValueInput == nullptr
                || ThresholdPin == nullptr
                || ResultInput == nullptr
                || IsFinalPin == nullptr
                || ReturnPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("Float Transition debug pass-through did not expose required Pins."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            Schema->TrySetDefaultValue(*TransitionIdPin, Transition.Id, false);
            Schema->TrySetDefaultValue(*ExpressionLabelPin, ExpressionLabel, false);
            Schema->TrySetDefaultValue(*ParameterNamePin, ParameterName, false);
            Schema->TrySetDefaultValue(*ThresholdPin, FString::SanitizeFloat(Threshold), false);
            Schema->TrySetDefaultValue(*IsFinalPin, TEXT("false"), false);
            if (!Schema->TryCreateConnection(&SelfPin, AnimInstancePin)
                || !Schema->TryCreateConnection(&ActualValuePin, ActualValueInput)
                || !Schema->TryCreateConnection(&ResultValuePin, ResultInput))
            {
                AddError(
                    Diagnostics,
                    K2ConnectionFailed,
                    TEXT("Failed to connect Float Transition debug pass-through."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }
            return ReturnPin;
        }

        /**
         * 在组合表达式或最终 RuleResult 后插入 BlueprintPure Bool 透传采样，并返回唯一输出。
         * 调用方必须用返回 Pin 替代原表达式继续连接，确保已有表达式只构建、求值一次。
         * 必须在游戏线程构建独占的 Transition Graph 时调用；函数只修改 Graph，不执行采样。
         *
         * @param Transition 提供稳定 ID 与诊断位置的只读 Transition。
         * @param StableSuffix 当前调用节点稳定后缀；Gate 节点使用索引，最终记录使用 Final。
         * @param ExpressionLabel 当前表达式稳定可读标签，不能为空。
         * @param bIsFinal 是否代表接入 Transition Result 前的权威最终结果。
         * @param Graph 接收调试调用节点的 Transition Graph，由当前 Builder 独占。
         * @param SelfPin 当前 AnimInstance Self 输出 Pin，所有权仍属于 Graph。
         * @param ResultValuePin 既有组合或最终 Bool 结果 Pin。
         * @return 成功时返回调试透传 Bool 输出；函数或 Pin 缺失、连接失败时返回 nullptr。
         */
        UEdGraphPin* BuildTransitionExpressionDebugValueNode(
            const FLuaAnimIRTransition& Transition,
            const FString& StableSuffix,
            const FString& ExpressionLabel,
            const bool bIsFinal,
            UAnimationTransitionGraph& Graph,
            UEdGraphPin& SelfPin,
            UEdGraphPin& ResultValuePin)
        {
            UFunction* RecordFunction = ULuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(
                    ULuaTransitionRuntimeLibrary,
                    RecordTransitionExpressionDebugValue));
            if (RecordFunction == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2FunctionNotFound,
                    TEXT("RecordTransitionExpressionDebugValue function was not found."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            UK2Node_CallFunction* RecordNode = CreateCallFunctionNode(
                Graph,
                TEXT("Gate.Trace.Expression.") + Transition.Id + TEXT(".") + StableSuffix,
                RecordFunction,
                680,
                bIsFinal ? 0 : FCString::Atoi(*StableSuffix) * 120);
            UEdGraphPin* AnimInstancePin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("AnimInstance"), EGPD_Input)
                : nullptr;
            UEdGraphPin* TransitionIdPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("TransitionId"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ExpressionLabelPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("ExpressionLabel"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ResultInput = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("Result"), EGPD_Input)
                : nullptr;
            UEdGraphPin* IsFinalPin = RecordNode != nullptr
                ? RecordNode->FindPin(TEXT("bIsFinal"), EGPD_Input)
                : nullptr;
            UEdGraphPin* ReturnPin = RecordNode != nullptr ? RecordNode->GetReturnValuePin() : nullptr;
            if (AnimInstancePin == nullptr
                || TransitionIdPin == nullptr
                || ExpressionLabelPin == nullptr
                || ResultInput == nullptr
                || IsFinalPin == nullptr
                || ReturnPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("Transition expression debug pass-through did not expose required Pins."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }

            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            Schema->TrySetDefaultValue(*TransitionIdPin, Transition.Id, false);
            Schema->TrySetDefaultValue(*ExpressionLabelPin, ExpressionLabel, false);
            Schema->TrySetDefaultValue(*IsFinalPin, bIsFinal ? TEXT("true") : TEXT("false"), false);
            if (!Schema->TryCreateConnection(&SelfPin, AnimInstancePin)
                || !Schema->TryCreateConnection(&ResultValuePin, ResultInput))
            {
                AddError(
                    Diagnostics,
                    K2ConnectionFailed,
                    TEXT("Failed to connect Transition expression debug pass-through."),
                    Transition.Id,
                    Transition.SourceLocation);
                return nullptr;
            }
            return ReturnPin;
        }

        /**
         * 在 Transition 自动创建的 UAnimationTransitionGraph 中物化完整规则并连接默认 Result。
         * RuleFunctionName 非空时生成兼容的 Lua 调用；Gate 非空时生成原生 AST；两者并存时保持 AND 语义。
         * 必须在游戏线程调用，TransitionNode 及其 BoundGraph 由当前 Builder 独占；函数不执行运行时规则。
         *
         * @param Transition 提供可选 Lua 规则、原生 Gate、稳定 ID 和源码位置的 IR Transition。
         * @param TransitionNode 已经完成 PostPlacedNewNode、拥有默认 Result 的原生 Transition 节点。
         * @param SourceState Transition 源状态，用于原生剩余时间 Getter 绑定。
         * @return 完整规则与 Result 成功连接时返回 true；所需节点、Pin 或连接失败时返回 false。
         */
        bool BuildTransitionRuleGraph(
            const FLuaAnimIRTransition& Transition,
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

            const bool bHasLuaRule = !Transition.RuleFunctionName.IsNone();
            const bool bHasNativeGate = Transition.Gate.RootIndex != INDEX_NONE;
            if (bIncremental && ReusedNativeNodes.Contains(&TransitionNode))
            {
                ResultNode->Modify();
                ReusedNativeNodes.Add(ResultNode);
            }
            ResultNode->NodeGuid = MakeStableGuid(TEXT("TransitionResult"), Transition.Id);
            ApplyGeneratedPosition(ResultNode, 600, 0);
            UK2Node_Self* SelfNode = CreateNativeNode<UK2Node_Self>(
                *TransitionGraph,
                TEXT("TransitionRule.Self.") + Transition.Id,
                0,
                160);
            UEdGraphPin* SelfPin = SelfNode != nullptr
                ? SelfNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Output)
                : nullptr;
            UEdGraphPin* ResultPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
            if (SelfPin == nullptr || ResultPin == nullptr)
            {
                AddError(
                    Diagnostics,
                    K2PinNotFound,
                    TEXT("Transition Rule Self or Result did not expose required K2 Pins."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }

            const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
            UEdGraphPin* LuaReturnPin = nullptr;
            if (bHasLuaRule)
            {
                UFunction* EvaluateFunction = ULuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(
                        ULuaTransitionRuntimeLibrary,
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

                UK2Node_CallFunction* EvaluateNode = CreateCallFunctionNode(
                    *TransitionGraph,
                    TEXT("EvaluateTransitionRule.") + Transition.Id,
                    EvaluateFunction,
                    260,
                    0);
                UEdGraphPin* AnimInstancePin = EvaluateNode != nullptr
                    ? EvaluateNode->FindPin(TEXT("AnimInstance"), EGPD_Input)
                    : nullptr;
                UEdGraphPin* ModulePin = EvaluateNode != nullptr
                    ? EvaluateNode->FindPin(TEXT("LuaModuleName"), EGPD_Input)
                    : nullptr;
                UEdGraphPin* RulePin = EvaluateNode != nullptr
                    ? EvaluateNode->FindPin(TEXT("RuleFunctionName"), EGPD_Input)
                    : nullptr;
                LuaReturnPin = EvaluateNode != nullptr ? EvaluateNode->GetReturnValuePin() : nullptr;
                if (AnimInstancePin == nullptr
                    || ModulePin == nullptr
                    || RulePin == nullptr
                    || LuaReturnPin == nullptr)
                {
                    AddError(
                        Diagnostics,
                        K2PinNotFound,
                        TEXT("Lua Transition Rule call did not expose required K2 Pins."),
                        Transition.Id,
                        Transition.SourceLocation);
                    return false;
                }

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
            }

            UEdGraphPin* FinalRulePin = LuaReturnPin;
            if (bHasNativeGate)
            {
                UEdGraphPin* NativeGatePin = BuildTransitionGateNode(
                    Transition,
                    Transition.Gate.RootIndex,
                    *TransitionGraph,
                    SourceState,
                    *SelfPin,
                    LuaReturnPin);
                if (NativeGatePin == nullptr) return false;
                if (LuaReturnPin == nullptr)
                {
                    FinalRulePin = NativeGatePin;
                }
                else
                {
                    UFunction* AndFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanAND));
                    UK2Node_CallFunction* AndNode = CreateCallFunctionNode(
                        *TransitionGraph, TEXT("Gate.AndLua.") + Transition.Id, AndFunction, 480, 0);
                    UEdGraphPin* AndA = AndNode != nullptr ? AndNode->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                    UEdGraphPin* AndB = AndNode != nullptr ? AndNode->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                    UEdGraphPin* AndResult = AndNode != nullptr ? AndNode->GetReturnValuePin() : nullptr;
                    if (AndA == nullptr || AndB == nullptr || AndResult == nullptr)
                    {
                        AddError(
                            Diagnostics,
                            K2ConnectionFailed,
                            TEXT("Failed to create Lua/Gate AND Pins."),
                            Transition.Id,
                            Transition.SourceLocation);
                        return false;
                    }
                    const bool bLuaBoolConnected = K2Schema->TryCreateConnection(LuaReturnPin, AndA);
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
            }

            if (bHasNativeGate && FinalRulePin != nullptr)
            {
                FinalRulePin = BuildTransitionExpressionDebugValueNode(
                    Transition,
                    TEXT("Final"),
                    TEXT("RuleResult"),
                    true,
                    *TransitionGraph,
                    *SelfPin,
                    *FinalRulePin);
            }
            if (FinalRulePin == nullptr || !K2Schema->TryCreateConnection(FinalRulePin, ResultPin))
            {
                AddError(
                    Diagnostics,
                    K2ConnectionFailed,
                    TEXT("Failed to connect the final Transition Rule to Result."),
                    Transition.Id,
                    Transition.SourceLocation);
                return false;
            }
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
         * @param LuaBoolPin 当前 Transition Graph 内 Lua Rule 的直接 Bool 输出；纯原生规则可为空。
         * @return 成功时返回子树 Bool 输出 Pin；节点或连接创建失败时返回 nullptr。
         */
        UEdGraphPin* BuildTransitionGateNode(
            const FLuaAnimIRTransition& Transition,
            const int32 GateIndex,
            UAnimationTransitionGraph& Graph,
            UAnimStateNode& SourceState,
            UEdGraphPin& SelfPin,
            UEdGraphPin* LuaBoolPin)
        {
            const FLuaAnimIRTransitionGateNode& GateNode = Transition.Gate.Nodes[GateIndex];
            if (GateNode.Type == TEXT("LuaBool")) return LuaBoolPin;
            const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
            if (GateNode.Type == TEXT("BoolProperty"))
            {
                FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(Graph);
                UK2Node_VariableGet* Getter = GetterCreator.CreateNode(false);
                if (Getter == nullptr) return nullptr;
                Getter->VariableReference.SetSelfMember(GateNode.Name);
                GetterCreator.Finalize();
                Getter->NodeGuid = MakeStableGuid(
                    TEXT("GateBoolProperty"),
                    Transition.Id + FString::FromInt(GateIndex));
                Getter->NodePosX = 120;
                Getter->NodePosY = GateIndex * 120;

                UFunction* CompareFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_BoolBool));
                UK2Node_CallFunction* Compare = CreateCallFunctionNode(
                    Graph,
                    TEXT("Gate.BoolCompare.") + Transition.Id + FString::FromInt(GateIndex),
                    CompareFunction,
                    360,
                    GateIndex * 120);
                UEdGraphPin* PropertyValue = Getter->GetValuePin();
                UEdGraphPin* A = Compare != nullptr ? Compare->FindPin(TEXT("A"), EGPD_Input) : nullptr;
                UEdGraphPin* B = Compare != nullptr ? Compare->FindPin(TEXT("B"), EGPD_Input) : nullptr;
                UEdGraphPin* CompareResult = Compare != nullptr ? Compare->GetReturnValuePin() : nullptr;
                if (PropertyValue == nullptr || A == nullptr || B == nullptr || CompareResult == nullptr)
                {
                    AddError(
                        Diagnostics,
                        K2PinNotFound,
                        TEXT("BoolProperty Gate Getter or comparison did not expose required Pins."),
                        Transition.Id,
                        Transition.SourceLocation);
                    return nullptr;
                }
                if (!Schema->TryCreateConnection(PropertyValue, A))
                {
                    AddError(
                        Diagnostics,
                        K2ConnectionFailed,
                        FString::Printf(
                            TEXT("BoolProperty Gate '%s' rejected Getter-to-comparison connection."),
                            *GateNode.Name.ToString()),
                        Transition.Id,
                        Transition.SourceLocation);
                    return nullptr;
                }
                Schema->TrySetDefaultValue(
                    *B,
                    GateNode.bExpectedBool ? TEXT("true") : TEXT("false"),
                    false);
                return BuildBoolTransitionDebugValueNode(
                    Transition,
                    GateIndex,
                    MakeTransitionExpressionLabel(GateNode, GateIndex),
                    GateNode.Name.ToString(),
                    GateNode.bExpectedBool,
                    Graph,
                    SelfPin,
                    *PropertyValue,
                    *CompareResult);
            }
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
                    return nullptr;
                }
                return BuildFloatTransitionDebugValueNode(
                    Transition,
                    GateIndex,
                    MakeTransitionExpressionLabel(GateNode, GateIndex),
                    TEXT("RelevantTimeRemaining"),
                    GateNode.Threshold,
                    Graph,
                    SelfPin,
                    *GetterOutput,
                    *CompareResult);
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
                UEdGraphPin* CurveValue = Curve != nullptr ? Curve->GetReturnValuePin() : nullptr;
                UEdGraphPin* CompareResult = Compare != nullptr ? Compare->GetReturnValuePin() : nullptr;
                if (A == nullptr
                    || B == nullptr
                    || CurveValue == nullptr
                    || CompareResult == nullptr
                    || !Schema->TryCreateConnection(CurveValue, A))
                {
                    return nullptr;
                }
                Schema->TrySetDefaultValue(*B, FString::SanitizeFloat(GateNode.Threshold), false);
                return BuildFloatTransitionDebugValueNode(
                    Transition,
                    GateIndex,
                    MakeTransitionExpressionLabel(GateNode, GateIndex),
                    GateNode.Name.ToString(),
                    GateNode.Threshold,
                    Graph,
                    SelfPin,
                    *CurveValue,
                    *CompareResult);
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
                UEdGraphPin* NotResult = NotNode->GetReturnValuePin();
                if (NotResult == nullptr) return nullptr;
                return BuildTransitionExpressionDebugValueNode(
                    Transition,
                    FString::FromInt(GateIndex),
                    MakeTransitionExpressionLabel(GateNode, GateIndex),
                    false,
                    Graph,
                    SelfPin,
                    *NotResult);
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
            return Accumulator != nullptr
                ? BuildTransitionExpressionDebugValueNode(
                    Transition,
                    FString::FromInt(GateIndex),
                    MakeTransitionExpressionLabel(GateNode, GateIndex),
                    false,
                    Graph,
                    SelfPin,
                    *Accumulator)
                : nullptr;
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
            const FLuaAnimIRGraph& Graph,
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
            for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                EffectiveStateIds.Add(Transition.SourceStateId);
                EffectiveStateIds.Add(Transition.TargetStateId);
            }

            TArray<const FLuaAnimIRState*> OrderedStates;
            for (const FLuaAnimIRState& State : Graph.StateMachine.States) OrderedStates.Add(&State);
            OrderedStates.Sort([](const FLuaAnimIRState& Left, const FLuaAnimIRState& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            TArray<const FLuaAnimIRState*> AutomaticEffectiveStates;
            TArray<const FLuaAnimIRState*> DisconnectedStates;
            for (const FLuaAnimIRState* State : OrderedStates)
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
                ApplyGeneratedPosition(*StateNode, Position.X, Position.Y);
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
                ApplyGeneratedPosition(*StateNode, Position.X, Position.Y);
            }

            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions)
            {
                UAnimStateNode* const* StateNode = StateNodes.Find(Pair.Key);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                ApplyGeneratedPosition(*StateNode, Pair.Value.X, Pair.Value.Y);
            }

            UAnimStateNode* const* EntryStateNode = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode != nullptr
                && EntryStateNode != nullptr
                && *EntryStateNode != nullptr
                && (!bIncremental || !ReusedNativeNodes.Contains(*EntryStateNode)))
            {
                NativeGraph.EntryNode->NodePosX = (*EntryStateNode)->NodePosX - EntryHorizontalOffset;
                NativeGraph.EntryNode->NodePosY = (*EntryStateNode)->NodePosY;
            }
        }

        /**
         * 将 StateMachine 显式 Grid 作为固定锚点，并按选定风格排列其余 State。
         * Auto、CompactGrid 和旧 HierarchicalBlocks 都使用有效状态平方根上取整的紧凑网格；传统流式风格仍从 Entry 做 BFS 分层。
         */
        void ApplyStateMachineLayout(
            const FLuaAnimIRGraph& Graph,
            UAnimationStateMachineGraph& NativeGraph,
            const TMap<FString, UAnimStateNode*>& StateNodes)
        {
            ELuaAnimIRLayoutStyle Style = Graph.Layout.Style;
            const TMap<FString, FIntPoint> ExplicitPositions = BuildExplicitLayoutPositions(Graph);
            if (Style == ELuaAnimIRLayoutStyle::Auto
                || Style == ELuaAnimIRLayoutStyle::CompactGrid
                || Style == ELuaAnimIRLayoutStyle::HierarchicalBlocks)
            {
                ApplyHierarchicalStateMachineLayout(Graph, NativeGraph, StateNodes, ExplicitPositions);
                for (const FLuaAnimIRLayoutPosition& Position : Graph.Layout.Positions)
                {
                    UAnimStateNode* const* StateNode = StateNodes.Find(Position.ElementId);
                    if (StateNode == nullptr || *StateNode == nullptr) continue;
                    (*StateNode)->Modify();
                    (*StateNode)->NodePosX = Position.X;
                    (*StateNode)->NodePosY = Position.Y;
                }
                UAnimStateNode* const* EntryStateNode =
                    StateNodes.Find(Graph.StateMachine.EntryStateId);
                if (NativeGraph.EntryNode != nullptr
                    && EntryStateNode != nullptr
                    && *EntryStateNode != nullptr)
                {
                    NativeGraph.EntryNode->NodePosX = (*EntryStateNode)->NodePosX - 240;
                    NativeGraph.EntryNode->NodePosY = (*EntryStateNode)->NodePosY;
                }
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
                for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    if (Transition.SourceStateId != Current || Depths.Contains(Transition.TargetStateId)) continue;
                    Depths.Add(Transition.TargetStateId, CurrentDepth + 1);
                    Queue.Add(Transition.TargetStateId);
                }
            }

            TArray<const FLuaAnimIRState*> OrderedStates;
            for (const FLuaAnimIRState& State : Graph.StateMachine.States) OrderedStates.Add(&State);
            OrderedStates.Sort([](const FLuaAnimIRState& Left, const FLuaAnimIRState& Right)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });
            TMap<int32, int32> LayerCounts;
            int32 FallbackDepth = Depths.Num();
            TSet<FIntPoint> Occupied;
            for (const TPair<FString, FIntPoint>& Pair : ExplicitPositions) Occupied.Add(Pair.Value);
            for (int32 StateIndex = 0; StateIndex < OrderedStates.Num(); ++StateIndex)
            {
                const FLuaAnimIRState& State = *OrderedStates[StateIndex];
                UAnimStateNode* const* StateNode = StateNodes.Find(State.Id);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                const FIntPoint* Explicit = ExplicitPositions.Find(State.Id);
                if (Explicit != nullptr)
                {
                    ApplyGeneratedPosition(*StateNode, Explicit->X, Explicit->Y);
                    continue;
                }

                const int32 Depth = Depths.Contains(State.Id) ? Depths[State.Id] : FallbackDepth++;
                const int32 SecondaryIndex = LayerCounts.FindOrAdd(Depth)++;
                FIntPoint Position;
                if (Style == ELuaAnimIRLayoutStyle::Radial)
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
                    if (Style == ELuaAnimIRLayoutStyle::LeftToRight
                        || Style == ELuaAnimIRLayoutStyle::RightToLeft)
                    {
                        Position.X = -Position.X;
                    }
                }
                while (Occupied.Contains(Position)) Position.Y += 260;
                Occupied.Add(Position);
                ApplyGeneratedPosition(*StateNode, Position.X, Position.Y);
            }

            UAnimStateNode* const* EntryStateNode = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode != nullptr
                && EntryStateNode != nullptr
                && *EntryStateNode != nullptr
                && (!bIncremental || !ReusedNativeNodes.Contains(*EntryStateNode)))
            {
                NativeGraph.EntryNode->NodePosX = (*EntryStateNode)->NodePosX - 280;
                NativeGraph.EntryNode->NodePosY = (*EntryStateNode)->NodePosY;
            }
            for (const FLuaAnimIRLayoutPosition& Position : Graph.Layout.Positions)
            {
                UAnimStateNode* const* StateNode = StateNodes.Find(Position.ElementId);
                if (StateNode == nullptr || *StateNode == nullptr) continue;
                (*StateNode)->Modify();
                (*StateNode)->NodePosX = Position.X;
                (*StateNode)->NodePosY = Position.Y;
            }
            EntryStateNode = StateNodes.Find(Graph.StateMachine.EntryStateId);
            if (NativeGraph.EntryNode != nullptr
                && EntryStateNode != nullptr
                && *EntryStateNode != nullptr)
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
            const FLuaAnimIRGraph& Graph,
            UAnimationStateMachineGraph& NativeGraph)
        {
            NativeGraphs.Add(Graph.Id, &NativeGraph);
            TMap<FString, UAnimStateNode*> StateNodes;
            int32 StateIndex = 0;
            for (const FLuaAnimIRState& State : Graph.StateMachine.States)
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
                const FLuaAnimIRGraph* StateGraph = FindGraph(State.GraphId);
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

            for (const FLuaAnimIRState& State : Graph.StateMachine.States)
            {
                UAnimStateNode* const* StateNode = StateNodes.Find(State.Id);
                const FLuaAnimIRGraph* StateGraph = FindGraph(State.GraphId);
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
            UEdGraphPin* EntryPin = NativeGraph.EntryNode != nullptr
                && NativeGraph.EntryNode->Pins.Num() > 0
                ? NativeGraph.EntryNode->Pins[0]
                : nullptr;
            UEdGraphPin* EntryStatePin = EntryState != nullptr
                ? (*EntryState)->GetInputPin()
                : nullptr;
            bool bEntryConnected = EntryPin != nullptr
                && EntryStatePin != nullptr
                && EntryPin->LinkedTo.Contains(EntryStatePin);
            if (!bEntryConnected && bIncremental && EntryPin != nullptr)
            {
                const TArray<UEdGraphPin*> ExistingEntryLinks = EntryPin->LinkedTo;
                for (UEdGraphPin* ExistingStatePin : ExistingEntryLinks)
                {
                    UEdGraphNode* ExistingState =
                        ExistingStatePin != nullptr ? ExistingStatePin->GetOwningNode() : nullptr;
                    if (ExistingState != nullptr
                        && PreviousOwnedNodeGuids.Contains(ExistingState->NodeGuid))
                    {
                        EntryPin->Modify();
                        ExistingStatePin->Modify();
                        EntryPin->BreakLinkTo(ExistingStatePin);
                    }
                }
            }
            if (!bEntryConnected && EntryPin != nullptr && EntryStatePin != nullptr)
            {
                bEntryConnected =
                    NativeGraph.GetSchema()->TryCreateConnection(EntryPin, EntryStatePin);
            }
            if (!bEntryConnected)
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
            for (const FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
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

                const bool bTransitionWasReused =
                    ReusedNativeNodes.Contains(TransitionNode);
                TransitionNode->NodeGuid = MakeStableGuid(TEXT("Transition"), Transition.Id);
                TransitionNode->CrossfadeDuration = Transition.Settings.BlendDuration;
                TransitionNode->PriorityOrder = Transition.Settings.PriorityOrder;
                TransitionNode->BlendMode = Transition.Settings.BlendMode;
                if (TransitionNode->GetPreviousState() != *SourceState
                    || TransitionNode->GetNextState() != *TargetState)
                {
                    TransitionNode->CreateConnections(*SourceState, *TargetState);
                }
                TransitionNode->BoundGraph->GraphGuid =
                    MakeStableGuid(TEXT("TransitionGraph"), Transition.Id);
                FEdGraphUtilities::RenameGraphToNameOrCloseToName(
                    TransitionNode->BoundGraph,
                    Transition.Key);
                const FLuaAnimIRTransition* PreviousTransition =
                    FindPreviousTransition(Graph.Id, Transition.Id);
                const bool bRuleUnchanged = bTransitionWasReused
                    && PreviousTransition != nullptr
                    && FLuaAnimIRTransition::StaticStruct()->CompareScriptStruct(
                        PreviousTransition,
                        &Transition,
                        0);
                if (!bRuleUnchanged)
                {
                    const TArray<UEdGraphNode*> RuleNodes = TransitionNode->BoundGraph->Nodes;
                    for (UEdGraphNode* RuleNode : RuleNodes)
                    {
                        if (RuleNode == nullptr
                            || RuleNode->IsA<UAnimGraphNode_TransitionResult>())
                        {
                            continue;
                        }
                        RuleNode->Modify();
                        FBlueprintEditorUtils::RemoveNode(&Blueprint, RuleNode, true);
                    }
                    if (!BuildTransitionRuleGraph(Transition, *TransitionNode, **SourceState))
                    {
                        return false;
                    }
                }
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
        bool ConnectPoseLink(const FLuaAnimIRLink& Link, UAnimationGraph& NativeGraph)
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

            if (SourcePin->LinkedTo.Contains(TargetPin)) return true;
            if (bIncremental && !TargetPin->LinkedTo.IsEmpty())
            {
                const TArray<UEdGraphPin*> ExistingLinks = TargetPin->LinkedTo;
                for (UEdGraphPin* ExistingSourcePin : ExistingLinks)
                {
                    UEdGraphNode* ExistingSourceNode =
                        ExistingSourcePin != nullptr ? ExistingSourcePin->GetOwningNode() : nullptr;
                    const bool bCurrentLuaOwned =
                        ExistingSourceNode != nullptr
                        && NativeNodes.FindKey(ExistingSourceNode) != nullptr;
                    const bool bSchemaOwnedNewGraphInput =
                        ExistingSourceNode != nullptr
                        && !InitialNativeGraphs.Contains(&NativeGraph)
                        && ExistingSourceNode->IsA<UAnimGraphNode_LinkedInputPose>();
                    const bool bCurrentLayerRootConnection =
                        CurrentLayer != nullptr
                        && TargetNode != nullptr
                        && (*TargetNode)->IsA<UAnimGraphNode_Root>();
                    if (ExistingSourceNode == nullptr
                        || (!PreviousOwnedNodeGuids.Contains(ExistingSourceNode->NodeGuid)
                            && !bCurrentLuaOwned
                            && !bSchemaOwnedNewGraphInput
                            && !bCurrentLayerRootConnection))
                    {
                        AddError(
                            Diagnostics,
                            ConnectionFailed,
                            FString::Printf(
                                TEXT("Lua Link '%s' conflicts with an editor-owned connection on '%s.%s' from node '%s' (%s); graph existed before build: %s."),
                                *Link.Id,
                                *Link.Target.NodeId,
                                *Link.Target.PinName,
                                *ExistingSourceNode->GetName(),
                                *ExistingSourceNode->GetClass()->GetPathName(),
                                InitialNativeGraphs.Contains(&NativeGraph)
                                    ? TEXT("true")
                                    : TEXT("false")),
                            Link.Id,
                            Link.SourceLocation);
                        return false;
                    }
                    ExistingSourcePin->Modify();
                    TargetPin->Modify();
                    ExistingSourcePin->BreakLinkTo(TargetPin);
                }
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
            const FLuaAnimIRNode* const* IRNode = NodesById.Find(NodeId);
            if (IRNode == nullptr) return nullptr;
            FString NativePinName = IRPinName;
            if (NativeNode.IsA<UK2Node_VariableGet>())
            {
                const FLuaAnimIRProperty* PropertyName = FindProperty(**IRNode, TEXT("PropertyName"));
                if (PropertyName != nullptr) NativePinName = PropertyName->Value.NameValue.ToString();
            }
            else if ((*IRNode)->NodeType == LuaAnimGraphIRNames::BlendListByBoolNode)
            {
                if (IRPinName == TEXT("TruePose")) NativePinName = TEXT("BlendPose_0");
                else if (IRPinName == TEXT("FalsePose")) NativePinName = TEXT("BlendPose_1");
                else if (IRPinName == TEXT("ActiveValue")) NativePinName = TEXT("bActiveValue");
            }
            else if ((*IRNode)->NodeType == LuaAnimGraphIRNames::BlendListByEnumNode)
            {
                if (IRPinName == TEXT("DefaultPose")) NativePinName = TEXT("BlendPose_0");
                else if (IRPinName.StartsWith(TEXT("Pose")) && IRPinName.Len() > 4)
                {
                    NativePinName = FString::Printf(TEXT("BlendPose_%d"), FCString::Atoi(*IRPinName.Mid(4)) + 1);
                }
                else if (IRPinName == TEXT("ActiveValue")) NativePinName = TEXT("ActiveEnumValue");
            }
            else if ((*IRNode)->NodeType == LuaAnimGraphIRNames::LayeredBlendPerBoneNode)
            {
                if (IRPinName == TEXT("BlendPose")) NativePinName = TEXT("BlendPoses_0");
                else if (IRPinName == TEXT("BlendWeight")) NativePinName = TEXT("BlendWeights_0");
            }
            else if ((*IRNode)->NodeType == LuaAnimGraphIRNames::LinkedInputPoseNode)
            {
                const FLuaAnimIRProperty* PoseName = FindProperty(**IRNode, TEXT("PoseName"));
                if (PoseName != nullptr
                    && IRPinName == PoseName->Value.NameValue.ToString())
                {
                    NativePinName = TEXT("Pose");
                }
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
        TArray<FLuaAnimIRDiagnostic>& Diagnostics;
        const FLuaAnimBlueprintIR* PreviousGeneratedIR = nullptr;
        bool bIncremental = false;
        const FLuaAnimIRLayer* CurrentLayer = nullptr;
        TMap<FString, const FLuaAnimIRGraph*> GraphsById;
        TMap<FString, const FLuaAnimIRNode*> NodesById;
        TMap<FString, const FLuaAnimIRGraph*> PreviousGraphsById;
        TSet<FGuid> PreviousOwnedNodeGuids;
        TSet<UEdGraphNode*> ReusedNativeNodes;
        TSet<UEdGraph*> InitialNativeGraphs;
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
     * 清理 Lua 拥有的成员变量、主 AnimGraph 非 Root 节点和 EventGraph 更新桥节点。
     * 编辑器创建的变量、EventGraph 业务节点以及资产级配置保持不变；缺失图外壳时按 UE 原生流程恢复。
     * 必须在游戏线程且编辑器事务已开启时调用；函数不编译 Blueprint。
     *
     * @param Blueprint 待原地重建的标准动画蓝图，调用方保证其全部业务 Graph 由 Lua 管理。
     * @param Preflight 当前规范 IR 与已解析接口，用于删除上一轮同名 Layer Graph 和接口实现。
     * @param LuaVariableNames 上一次成功生成记录的 Lua 成员变量名；其他变量不会删除。
     * @param OutDiagnostics 接收缺失原生基础图的稳定错误。
     * @param SourceLocation Lua 模块根声明位置，用于定位诊断。
     * @return 基础图已恢复并清理时返回 true，否则不继续物化并返回 false。
     */
    bool ResetLuaOwnedBlueprint(
        UAnimBlueprint& Blueprint,
        const FPreflightData& Preflight,
        const TArray<FName>& LuaVariableNames,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics,
        const FLuaAnimIRSourceLocation& SourceLocation)
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

        FBlueprintEditorUtils::BulkRemoveMemberVariables(&Blueprint, LuaVariableNames);

        for (const FSoftClassPath& InterfacePath : Preflight.Blueprint.ImplementedInterfaces)
        {
            FBlueprintEditorUtils::RemoveInterface(
                &Blueprint,
                InterfacePath.GetAssetPath(),
                false);
        }

        for (int32 LayerIndex = 1; LayerIndex < Preflight.Blueprint.Layers.Num(); ++LayerIndex)
        {
            const FLuaAnimIRLayer& Layer = Preflight.Blueprint.Layers[LayerIndex];
            const FName FunctionName = Layer.FunctionName.IsNone()
                ? FName(*Layer.Name)
                : Layer.FunctionName;
            TArray<UEdGraph*> AllGraphs;
            Blueprint.GetAllGraphs(AllGraphs);
            for (UEdGraph* Graph : AllGraphs)
            {
                if (Graph != nullptr && Graph->GetFName() == FunctionName)
                {
                    FBlueprintEditorUtils::RemoveGraph(
                        &Blueprint,
                        Graph,
                        EGraphRemoveFlags::MarkTransient);
                    break;
                }
            }
        }

        const TArray<UEdGraphNode*> MainNodes = MainGraph->Nodes;
        for (UEdGraphNode* Node : MainNodes)
        {
            if (Node == nullptr || Node->IsA<UAnimGraphNode_Root>()) continue;
            Node->Modify();
            FBlueprintEditorUtils::RemoveNode(&Blueprint, Node, true);
        }

        return true;
    }

    /** 判断两轮 IR 中同名变量的 Lua 所有配置是否保持一致。 */
    bool AreGeneratedVariablesEquivalent(
        const FLuaAnimIRVariable& Previous,
        const FLuaAnimIRVariable& Current)
    {
        return Previous.DataType == Current.DataType
            && Previous.TypeObjectPath == Current.TypeObjectPath
            && Previous.bTransient == Current.bTransient
            && FLuaAnimIRValue::StaticStruct()->CompareScriptStruct(
                &Previous.DefaultValue,
                &Current.DefaultValue,
                0);
    }

    /**
     * 以 Lua 为动画结构的唯一权威来源，清空主 AnimGraph 并删除新旧 IR 声明的全部动画层 Graph。
     * 状态机、状态、过渡及其内部 Graph 会随所属节点一并销毁，后续 Builder 从当前 IR 完整重建。
     * EventGraph、用户函数和非 Lua 变量不属于本函数的全量替换范围；Lua 变量与接口仍按新旧 IR 做增量维护。
     * 主 AnimGraph UObject 及其唯一 Schema Root 作为 UE 不可删除的资产外壳保留，其余节点全部删除。
     * 必须在游戏线程且编辑器事务已开启时调用；函数不编译 Blueprint。
     *
     * @param Blueprint 待原地替换动画结构的标准动画蓝图。
     * @param PreviousIR 上一次成功写入该资产的 IR。
     * @param Preflight 当前已预检 IR。
     * @param OutDiagnostics 接收外壳恢复失败诊断。
     * @return 动画结构已清空、默认 Root 有效且非结构配置已完成增量维护时返回 true。
     */
    bool PrepareLuaOwnedBlueprintFullGraphReplacement(
        UAnimBlueprint& Blueprint,
        const FLuaAnimBlueprintIR& PreviousIR,
        const FPreflightData& Preflight,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
    {
        Blueprint.Modify();
        if (!EnsureLuaBlueprintShell(Blueprint))
        {
            AddError(
                OutDiagnostics,
                InPlaceCommitFailed,
                TEXT("The target AnimBlueprint native shell could not be restored for incremental generation."),
                Blueprint.GetPathName(),
                Preflight.Blueprint.SourceLocation);
            return false;
        }

        UAnimationGraph* MainGraph = FindMainAnimationGraph(Blueprint);
        if (MainGraph == nullptr)
        {
            AddError(
                OutDiagnostics,
                InPlaceCommitFailed,
                TEXT("The target AnimBlueprint has no main AnimGraph after restoring its native shell."),
                Blueprint.GetPathName(),
                Preflight.Blueprint.SourceLocation);
            return false;
        }
        MainGraph->Modify();
        const TArray<UEdGraphNode*> ExistingMainNodes = MainGraph->Nodes;
        UAnimGraphNode_Root* MainRootNode = nullptr;
        for (UEdGraphNode* ExistingNode : ExistingMainNodes)
        {
            if (ExistingNode == nullptr) continue;
            if (UAnimGraphNode_Root* CandidateRoot = Cast<UAnimGraphNode_Root>(ExistingNode))
            {
                if (MainRootNode == nullptr)
                {
                    MainRootNode = CandidateRoot;
                    continue;
                }
            }
            ExistingNode->Modify();
            FBlueprintEditorUtils::RemoveNode(&Blueprint, ExistingNode, true);
        }
        if (MainRootNode == nullptr)
        {
            AddError(
                OutDiagnostics,
                InPlaceCommitFailed,
                TEXT("The target AnimBlueprint main AnimGraph has no default Root after restoring its native shell."),
                Blueprint.GetPathName(),
                Preflight.Blueprint.SourceLocation);
            return false;
        }

        TSet<FName> LuaLayerGraphNames;
        for (int32 LayerIndex = 1; LayerIndex < PreviousIR.Layers.Num(); ++LayerIndex)
        {
            const FLuaAnimIRLayer& Layer = PreviousIR.Layers[LayerIndex];
            LuaLayerGraphNames.Add(
                Layer.FunctionName.IsNone() ? FName(*Layer.Name) : Layer.FunctionName);
        }
        for (int32 LayerIndex = 1; LayerIndex < Preflight.Blueprint.Layers.Num(); ++LayerIndex)
        {
            const FLuaAnimIRLayer& Layer = Preflight.Blueprint.Layers[LayerIndex];
            LuaLayerGraphNames.Add(
                Layer.FunctionName.IsNone() ? FName(*Layer.Name) : Layer.FunctionName);
        }

        TArray<UEdGraph*> AllGraphs;
        Blueprint.GetAllGraphs(AllGraphs);
        for (UEdGraph* Graph : AllGraphs)
        {
            if (Graph == nullptr
                || Graph == MainGraph
                || !LuaLayerGraphNames.Contains(Graph->GetFName()))
            {
                continue;
            }
            Graph->Modify();
            FBlueprintEditorUtils::RemoveGraph(
                &Blueprint,
                Graph,
                EGraphRemoveFlags::MarkTransient);
        }

        for (const FSoftClassPath& PreviousInterface : PreviousIR.ImplementedInterfaces)
        {
            if (Preflight.Blueprint.ImplementedInterfaces.Contains(PreviousInterface)) continue;
            FBlueprintEditorUtils::RemoveInterface(
                &Blueprint,
                PreviousInterface.GetAssetPath(),
                false);
        }

        TArray<FName> VariablesToReplace;
        for (const FLuaAnimIRVariable& PreviousVariable : PreviousIR.Variables)
        {
            const FLuaAnimIRVariable* CurrentVariable =
                Preflight.Blueprint.Variables.FindByPredicate(
                    [&PreviousVariable](const FLuaAnimIRVariable& Candidate)
                    {
                        return Candidate.Name == PreviousVariable.Name;
                    });
            if (CurrentVariable == nullptr
                || !AreGeneratedVariablesEquivalent(PreviousVariable, *CurrentVariable))
            {
                VariablesToReplace.Add(PreviousVariable.Name);
            }
        }
        if (!VariablesToReplace.IsEmpty())
        {
            FBlueprintEditorUtils::BulkRemoveMemberVariables(&Blueprint, VariablesToReplace);
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
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
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
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
    {
        UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>(GetTransientPackage());
        Factory->BlueprintType =
            Preflight.Blueprint.BlueprintKind == ELuaAnimIRBlueprintKind::AnimationLayerInterface
            ? BPTYPE_Interface
            : BPTYPE_Normal;
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
            TEXT("LuaAnimGraphIR"));
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
        AnimBlueprint->bUseMultiThreadedAnimationUpdate =
            CanUseMultiThreadedAnimationUpdate(Preflight.Blueprint);

        if (bTransient)
        {
            AnimBlueprint->SetFlags(RF_Transient);
            AnimBlueprint->ClearFlags(RF_Standalone);
        }

        if (Preflight.Blueprint.BlueprintKind == ELuaAnimIRBlueprintKind::AnimBlueprint
            && !EnsureLuaBlueprintShell(*AnimBlueprint))
        {
            AddError(
                OutDiagnostics,
                MainAnimGraphNotFound,
                TEXT("The generated AnimBlueprint could not create an owned Lua AnimGraph shell."),
                Preflight.Blueprint.SourceModule,
                Preflight.Blueprint.SourceLocation);
            DiscardCreatedBlueprint(AnimBlueprint);
            return nullptr;
        }

        FNativeAnimBlueprintBuilder Builder(Preflight, *AnimBlueprint, OutDiagnostics);
        if (!Builder.Build()
            || HasErrors(OutDiagnostics)
            || !ApplyInheritedDefaults(
                *AnimBlueprint,
                Preflight.Blueprint,
                OutDiagnostics))
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
        if (!ApplyInheritedDefaults(
            *AnimBlueprint,
            Preflight.Blueprint,
            OutDiagnostics))
        {
            DiscardCreatedBlueprint(AnimBlueprint);
            return nullptr;
        }
        return AnimBlueprint;
    }

    /**
     * 将已预检 IR 写入目标 AnimBlueprint 的现有 UObject，缺失图外壳由 ResetLuaOwnedBlueprint 自动恢复。
     * 可选 staging 仅供显式工具在进入原生编译前验证；编译前回调必须关闭 staging，避免嵌套原生编译。
     * 必须在游戏线程且 GEditor 可用时调用。函数保留目标资产当前的多线程动画更新设置，
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
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
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
                TEXT("LuaAnimBlueprintStaging"));
            StagingBlueprint = MaterializeBlueprint(
                Preflight,
                *GetTransientPackage(),
                StagingName,
                true,
                OutDiagnostics);
            if (StagingBlueprint == nullptr) return false;
        }

        GEditor->BeginTransaction(
            TEXT("LuaAnimBlueprint"),
            NSLOCTEXT("LuaAnimBlueprint", "PrepareTransaction", "Prepare Lua Animation Blueprint Graph"),
            &Blueprint);
        Blueprint.Modify();
        ULuaAnimBlueprintExtension* Extension =
            ULuaAnimBlueprintExtension::Find(&Blueprint);
        if (Extension == nullptr)
        {
            AddError(
                OutDiagnostics,
                MissingLuaBlueprintExtension,
                TEXT("The target AnimBlueprint lost its Lua source metadata before Graph generation."),
                Blueprint.GetPathName(),
                Preflight.Blueprint.SourceLocation);
            RollbackInPlaceTransaction(Blueprint);
            DiscardCreatedBlueprint(StagingBlueprint);
            return false;
        }
        Extension->Modify();

        const bool bUseFullGraphReplacement =
            Preflight.Blueprint.BlueprintKind == ELuaAnimIRBlueprintKind::AnimBlueprint;
        const FLuaAnimBlueprintIR* PreviousGeneratedIR = nullptr;
        if (bUseFullGraphReplacement)
        {
            PreviousGeneratedIR = Extension->bHasLastGeneratedIR
                && Extension->LastGeneratedIR.BlueprintKind
                    == ELuaAnimIRBlueprintKind::AnimBlueprint
                ? &Extension->LastGeneratedIR
                : &Preflight.Blueprint;
        }
        bool bPrepared = bUseFullGraphReplacement
            ? PrepareLuaOwnedBlueprintFullGraphReplacement(
                Blueprint,
                *PreviousGeneratedIR,
                Preflight,
                OutDiagnostics)
            : ResetLuaOwnedBlueprint(
                Blueprint,
                Preflight,
                Extension->GeneratedVariableNames,
                OutDiagnostics,
                Preflight.Blueprint.SourceLocation);
        if (bPrepared)
        {
            FNativeAnimBlueprintBuilder Builder(
                Preflight,
                Blueprint,
                OutDiagnostics,
                bUseFullGraphReplacement ? PreviousGeneratedIR : nullptr);
            bPrepared = Builder.Build() && !HasErrors(OutDiagnostics);
        }
        if (bPrepared)
        {
            bPrepared = ApplyInheritedDefaults(
                Blueprint,
                Preflight.Blueprint,
                OutDiagnostics);
        }
        if (bPrepared)
        {
            Extension->GeneratedVariableNames.Reset(Preflight.Blueprint.Variables.Num());
            for (const FLuaAnimIRVariable& Variable : Preflight.Blueprint.Variables)
            {
                if (Preflight.InheritedVariableNames.Contains(Variable.Name)) continue;
                Extension->GeneratedVariableNames.Add(Variable.Name);
            }
            Extension->LastGeneratedIR = Preflight.Blueprint;
            Extension->bHasLastGeneratedIR = true;
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
UAnimBlueprint* ULuaAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
    const FLuaAnimBlueprintIR& Blueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

    FPreflightData Preflight;
    if (!PrepareBuild(Blueprint, Preflight, OutDiagnostics)) return nullptr;

    const FName AssetName = MakeUniqueObjectName(
        GetTransientPackage(),
        UAnimBlueprint::StaticClass(),
        TEXT("LuaGeneratedAnimBlueprint"));
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
UAnimBlueprint* ULuaAnimBlueprintFactoryLibrary::CreateAnimBlueprintAsset(
    const FLuaAnimBlueprintIR& Blueprint,
    const FString& PackagePath,
    const FString& AssetName,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

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
UAnimBlueprint* ULuaAnimBlueprintFactoryLibrary::CompileLuaModuleToAnimBlueprintAsset(
    const FString& LuaModuleName,
    const FString& PackagePath,
    const FString& AssetName,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    FLuaAnimBlueprintIR BlueprintIR;
    if (!ULuaAnimGraphIRLibrary::CompileLuaModule(
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
 * 在指定 USkeletalMesh 的 Mesh-only Socket 列表中按名称创建或更新一个 Socket，并同步保存资产 package。
 * 本函数不会修改 Skeleton 级 Socket，也不会创建资产、修改参考骨架或回滚保存失败前的内存变更。
 * 只能在非 PIE 的编辑器游戏线程调用；成功后会触发 PostEditChange、重建 Socket 映射并写入磁盘。
 *
 * @param SkeletalMesh 要修改并保存的已持久化 SkeletalMesh 资产；不可为空、transient 或非资产对象。
 * @param SocketName Mesh Socket 的唯一名称；None 无效，同名 Mesh Socket 会原地更新。
 * @param BoneName Socket 依附的参考骨架骨骼名；必须存在于 SkeletalMesh 的 ReferenceSkeleton。
 * @param RelativeTransform 相对 BoneName 的位置、旋转和缩放；不得包含 NaN。
 * @param OutError 接收失败原因；成功时清空。调用方持有字符串，函数不保留引用。
 * @return 创建或更新并成功保存 package 时返回 true；任一校验或保存步骤失败时返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::UpsertSkeletalMeshSocket(
    USkeletalMesh* SkeletalMesh,
    const FName SocketName,
    const FName BoneName,
    const FTransform& RelativeTransform,
    FString& OutError)
{
    OutError.Reset();
    if (!IsInGameThread() || !GIsEditor)
    {
        OutError = TEXT("UpsertSkeletalMeshSocket must run on the editor game thread.");
        return false;
    }
    if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
    {
        OutError = TEXT("UpsertSkeletalMeshSocket cannot modify assets during PIE or SIE.");
        return false;
    }
    if (SkeletalMesh == nullptr || !SkeletalMesh->IsAsset())
    {
        OutError = TEXT("SkeletalMesh must be a persistent asset object.");
        return false;
    }
    if (SocketName.IsNone())
    {
        OutError = TEXT("SocketName must not be None.");
        return false;
    }
    if (BoneName.IsNone()
        || SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
    {
        OutError = FString::Printf(
            TEXT("BoneName '%s' does not exist in SkeletalMesh '%s'."),
            *BoneName.ToString(),
            *SkeletalMesh->GetPathName());
        return false;
    }
    if (RelativeTransform.ContainsNaN())
    {
        OutError = TEXT("RelativeTransform must contain only finite values.");
        return false;
    }

    UPackage* Package = SkeletalMesh->GetOutermost();
    if (Package == nullptr
        || Package == GetTransientPackage()
        || !FPackageName::IsValidLongPackageName(Package->GetName()))
    {
        OutError = TEXT("SkeletalMesh must belong to a valid persistent content package.");
        return false;
    }

    SkeletalMesh->Modify();
#if UE_VERSION_NEWER_THAN(5, 7, 0)
    TArray<TObjectPtr<USkeletalMeshSocket>>& MeshSockets = SkeletalMesh->GetMeshOnlySocketList();
#else
    TArray<USkeletalMeshSocket*>& MeshSockets = SkeletalMesh->GetMeshOnlySocketList();
#endif
    USkeletalMeshSocket* Socket = nullptr;
    for (USkeletalMeshSocket* ExistingSocket : MeshSockets)
    {
        if (ExistingSocket != nullptr && ExistingSocket->SocketName == SocketName)
        {
            Socket = ExistingSocket;
            break;
        }
    }
    if (Socket == nullptr)
    {
        Socket = NewObject<USkeletalMeshSocket>(SkeletalMesh, NAME_None, RF_Transactional);
        if (Socket == nullptr)
        {
            OutError = TEXT("Failed to allocate a mesh Socket object.");
            return false;
        }
        MeshSockets.Add(Socket);
    }
    else
    {
        Socket->Modify();
    }

    Socket->SocketName = SocketName;
    Socket->BoneName = BoneName;
    Socket->RelativeLocation = RelativeTransform.GetLocation();
    Socket->RelativeRotation = RelativeTransform.Rotator();
    Socket->RelativeScale = RelativeTransform.GetScale3D();
    SkeletalMesh->RebuildSocketMap();
    SkeletalMesh->PostEditChange();
    Package->MarkPackageDirty();

    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(),
        FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, SkeletalMesh, *Filename, SaveArgs))
    {
        OutError = FString::Printf(
            TEXT("Failed to save SkeletalMesh package '%s'."),
            *Filename);
        return false;
    }

    return true;
}

/**
 * 为已有的标准 UAnimBlueprint 附加或更新 Lua 源扩展，并将资产标记为等待首次显式导入。
 * 函数配置源身份并立即关闭多线程动画更新，但不读取 Lua、不清理现有 Graph，也不执行原生编译；
 * 因此可先安全接管旧动画蓝图，
 * 再通过 Lua → AnimBlueprint 完成带 staging 和事务回滚的结构替换。
 * 必须在非 PIE 的游戏线程调用，且目标必须是精确 UAnimBlueprint 类型以继续使用 UE5.2 原生动画编译器。
 *
 * @param AnimBlueprint 要由 Lua 接管的现有标准动画蓝图；对象身份、路径、父类和 Skeleton 均保持不变。
 * @param LuaModuleName UnLua 模块名，例如 Animation.Lua.ABP_Lua；不能为空，也不是文件系统路径。
 * @return 扩展配置成功并将 Blueprint/package 标脏时返回 true；线程、PIE、对象类型或模块名无效时返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
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

    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Request(AnimBlueprint);
    if (Extension == nullptr) return false;

    AnimBlueprint->Modify();
    Extension->Modify();
    AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
    Extension->LuaModuleName = LuaModuleName;
    Extension->GeneratedLuaModuleName.Reset();
    Extension->LastSynchronizedBlueprintHash.Reset();
    Extension->LastSynchronizedLuaHash.Reset();
    Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::NeverSynchronized;
    Extension->MarkSourceDirty(
        TEXT("Lua source configured; use Lua → AnimBlueprint to import it explicitly."));
    FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
    AnimBlueprint->GetOutermost()->MarkPackageDirty();
    return true;
}

/**
 * 只读取扩展指定的 Lua 模块，以资产当前 ParentClass 和 TargetSkeleton 覆盖源码中的创建期提示，
 * 构建并验证 IR 与资产预检结果，然后缓存最近成功 IR。
 * 函数不修改 AnimGraph、EventGraph 或 GeneratedClass，不调用 UE 原生编译，也不保存或标脏 package。
 * 必须在非 PIE 的游戏线程调用；失败会保留旧 IR，但旧修订不会被 Lua → AnimBlueprint 采用。
 *
 * @param AnimBlueprint 带 Lua 扩展的精确标准 UAnimBlueprint；函数不取得对象所有权。
 * @param OutDiagnostics 接收 Lua 文件、行列、IR 验证和资源预检诊断；调用开始时清空。
 * @return 当前源码完整通过检查并已缓存时返回 true；线程、配置、Lua 或预检失败时返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::CheckLuaAnimBlueprint(
    UAnimBlueprint* AnimBlueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    FLuaAnimIRSourceLocation AssetLocation;
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

    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (Extension == nullptr || Extension->GetExchangeLuaModuleName().IsEmpty())
    {
        AddError(
            OutDiagnostics,
            Extension == nullptr ? MissingLuaBlueprintExtension : EmptyLuaModuleName,
            Extension == nullptr
                ? TEXT("The target AnimBlueprint has no Lua Lua source metadata.")
                : TEXT("LuaModuleName must be configured before Check Lua."),
            AnimBlueprint->GetPathName(),
            AssetLocation);
        return false;
    }

    UUnLuaFunctionLibrary::HotReload();
    if (Extension->GeneratedVariableNames.IsEmpty() && Extension->bHasLastSuccessfulIR)
    {
        for (const FLuaAnimIRVariable& Variable : Extension->LastSuccessfulIR.Variables)
        {
            const bool bOwnedByBlueprint =
                AnimBlueprint->NewVariables.ContainsByPredicate(
                    [&Variable](const FBPVariableDescription& Description)
                    {
                        return Description.VarName == Variable.Name;
                    });
            if (bOwnedByBlueprint)
            {
                Extension->GeneratedVariableNames.AddUnique(Variable.Name);
            }
        }
    }

    FLuaAnimBlueprintIR CheckedIR;
    bool bSucceeded = ULuaAnimGraphIRLibrary::CompileLuaModule(
        Extension->GetExchangeLuaModuleName(),
        CheckedIR,
        OutDiagnostics);
    if (bSucceeded)
    {
        CheckedIR.ParentAnimInstanceClass = AnimBlueprint->ParentClass != nullptr
            ? FSoftClassPath(AnimBlueprint->ParentClass->GetPathName())
            : FSoftClassPath();
        CheckedIR.TargetSkeleton = AnimBlueprint->TargetSkeleton != nullptr
            ? FSoftObjectPath(AnimBlueprint->TargetSkeleton->GetPathName())
            : FSoftObjectPath();
    }
    FPreflightData Preflight;
    if (bSucceeded) bSucceeded = PrepareBuild(CheckedIR, Preflight, OutDiagnostics);

    if (bSucceeded)
    {
        Extension->MarkCheckSucceeded(CheckedIR);
        return true;
    }

    Extension->MarkCheckFailed(
        OutDiagnostics.Num() > 0
            ? OutDiagnostics.Last().Message
            : TEXT("Lua animation source check failed."));
    Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::Error;
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
bool ULuaAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
    UAnimBlueprint* AnimBlueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass())
    {
        return false;
    }

    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (Extension == nullptr) return false;

    if (!Extension->bHasLastSuccessfulIR
        || Extension->LastCheckedSourceRevision != Extension->SourceRevision)
    {
        if (!CheckLuaAnimBlueprint(AnimBlueprint, OutDiagnostics)) return false;
    }

    FLuaAnimBlueprintIR BuildIR = Extension->LastSuccessfulIR;
    BuildIR.ParentAnimInstanceClass = AnimBlueprint->ParentClass != nullptr
        ? FSoftClassPath(AnimBlueprint->ParentClass->GetPathName())
        : FSoftClassPath();
    BuildIR.TargetSkeleton = AnimBlueprint->TargetSkeleton != nullptr
        ? FSoftObjectPath(AnimBlueprint->TargetSkeleton->GetPathName())
        : FSoftObjectPath();
    FPreflightData Preflight;
    if (!PrepareBuild(BuildIR, Preflight, OutDiagnostics)) return false;

    if (!BuildPreparedGraph(*AnimBlueprint, Preflight, false, OutDiagnostics))
    {
        Extension->MarkCompileFailed(
            OutDiagnostics.Num() > 0
                ? OutDiagnostics.Last().Message
                : TEXT("Lua → AnimBlueprint failed."));
        return false;
    }

    Extension->bSourceDirty = false;
    Extension->CompileStatus = ELuaAnimBlueprintCompileStatus::OutOfDate;
    Extension->LastCompileMessage =
        TEXT("Lua Graph generated successfully; UE native compilation is pending.");
    return true;
}

/**
 * 按 Check、Generate、UE 原生 Compile 的固定顺序同步更新一个 Lua 动画蓝图，并可选保存 package。
 * Check 或 Generate 失败时不会调用原生编译；成功路径恰好调用一次 FKismetEditorUtilities::CompileBlueprint。
 * 必须在非 PIE 的游戏线程调用；全局重入会被拒绝，函数不读取旧 SourceMode 兼容字段。
 *
 * @param AnimBlueprint 带 Lua 扩展的标准 UAnimBlueprint；对象身份和路径保持不变。
 * @param bSavePackage true 时仅在全部编译成功后保存当前 package，false 时保留编辑器 Dirty 状态。
 * @param OutDiagnostics 接收 Check、Generate、原生编译和保存诊断；调用开始时清空。
 * @return Graph 生成、唯一一次原生编译及可选保存全部成功时返回 true，否则返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
    UAnimBlueprint* AnimBlueprint,
    const bool bSavePackage,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;

    OutDiagnostics.Reset();
    if (AnimBlueprint == nullptr || !IsInGameThread()) return false;
    if (GIsCompilingLuaAnimBlueprintInPlace)
    {
        FLuaAnimIRSourceLocation Location;
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
    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
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
            FLuaAnimIRSourceLocation());
        if (Extension != nullptr) Extension->MarkCompileFailed(OutDiagnostics.Last().Message);
        return false;
    }
    if (Extension == nullptr
        || !Extension->bHasLastGeneratedIR
        || !ApplyInheritedDefaults(
            *AnimBlueprint,
            Extension->LastGeneratedIR,
            OutDiagnostics))
    {
        if (Extension != nullptr)
        {
            Extension->MarkCompileFailed(
                OutDiagnostics.Num() > 0
                    ? OutDiagnostics.Last().Message
                    : TEXT("Failed to restore Lua-owned inherited defaults after native compilation."));
        }
        return false;
    }
    if (Extension != nullptr)
    {
        Extension->MarkCompileSucceeded();
        FLuaAnimBlueprintIR BlueprintIR;
        TArray<FLuaAnimIRDiagnostic> SyncDiagnostics;
        FString BlueprintHash;
        FString LuaHash;
        const bool bHasBlueprintHash = ReadAnimBlueprintToIR(
            AnimBlueprint,
            BlueprintIR,
            SyncDiagnostics)
            && ComputeCanonicalIRHash(BlueprintIR, BlueprintHash, SyncDiagnostics);
        const bool bHasLuaHash = ComputeCanonicalIRHash(
            Extension->LastSuccessfulIR,
            LuaHash,
            SyncDiagnostics);
        OutDiagnostics.Append(SyncDiagnostics);
        if (bHasBlueprintHash && bHasLuaHash)
        {
            Extension->MarkSynchronized(BlueprintHash, LuaHash);
        }
        else
        {
            Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::Error;
            AddError(
                OutDiagnostics,
                TEXT("Factory.SyncHashFailed"),
                TEXT("Lua import compiled successfully, but the synchronization hashes could not be recorded."),
                AnimBlueprint->GetPathName(),
                FLuaAnimIRSourceLocation());
            return false;
        }
    }
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
                FLuaAnimIRSourceLocation());
            return false;
        }
    }
    return true;
}
/**
 * 将当前编辑器进程中全部已加载、已配置 Lua 模块的标准 AnimBlueprint 标记为源已过期。
 * Animation 目录内任意 Lua 都可能是多个蓝图共享的基类或节点依赖，因此在缺少依赖图时保守标脏全部已加载 Lua 资产。
 * 函数只更新扩展内存状态，不创建事务、不标脏 package，也不读取 Lua、重建 Graph 或调用 HotReload。
 * PIE 期间也可以安全调用；只能在游戏线程调用。
 *
 * @param Reason 写入扩展 LastCompileMessage 的变更原因；允许为空。
 * @return 本次标记的已加载 Lua AnimBlueprint 数量；非游戏线程返回 0。
 */
int32 ULuaAnimBlueprintFactoryLibrary::MarkLoadedLuaAnimBlueprintsDirty(
    const FString& Reason)
{
    if (!IsInGameThread()) return 0;

    int32 StaleSourceCount = 0;
    for (TObjectIterator<UAnimBlueprint> Iterator; Iterator; ++Iterator)
    {
        UAnimBlueprint* AnimBlueprint = *Iterator;
        if (!IsValid(AnimBlueprint)
            || !AnimBlueprint->IsAsset()
            || AnimBlueprint->GetOutermost() == GetTransientPackage())
        {
            continue;
        }

        ULuaAnimBlueprintExtension* Extension =
            ULuaAnimBlueprintExtension::Find(AnimBlueprint);
        if (Extension == nullptr || Extension->GetExchangeLuaModuleName().IsEmpty()) continue;

        Extension->MarkLuaChanged(Reason);
        ++StaleSourceCount;
    }
    return StaleSourceCount;
}

/**
 * 只读地把现有标准 AnimBlueprint 转换为完整 Canonical IR。
 * 函数将所有严格支持边界委托给通用 Reader，不调用 Modify、原生编译、保存或资产注册；失败时 OutBlueprint
 * 保持为空值。只能在游戏线程调用。
 *
 * @param AnimBlueprint 待读动画蓝图，可为空，函数不持有对象。
 * @param OutBlueprint 成功时接收可验证的完整 IR，失败时为空值。
 * @param OutDiagnostics 接收 Reader 与 Validator 的结构化诊断。
 * @return 完整读取成功时返回 true，否则返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::ReadAnimBlueprintToIR(
    const UAnimBlueprint* AnimBlueprint,
    FLuaAnimBlueprintIR& OutBlueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    return FLuaAnimBlueprintIRReader::Read(AnimBlueprint, OutBlueprint, OutDiagnostics);
}

/**
 * 将 IR 复制、Canonicalize、Validate 后通过确定性 Lua Writer 生成完整结构文本，再对 UTF-8 字节计算 MD5。
 * Writer 会反射写出 FLuaAnimBlueprintIR 的全部 UPROPERTY，包括每个 Graph 的 Layout.Positions；数组已由
 * Canonicalize 稳定排序，因此哈希不受输入声明顺序影响。函数不访问文件系统或 UObject 实例。
 *
 * @param Blueprint 待计算的完整 IR；函数不修改调用方值。
 * @param OutHash 成功时接收 32 位小写十六进制哈希，失败时为空。
 * @param OutDiagnostics 接收 Validator 或 Writer 诊断；调用开始时清空。
 * @return IR 可规范化并确定性写出时返回 true，否则返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::ComputeCanonicalIRHash(
    const FLuaAnimBlueprintIR& Blueprint,
    FString& OutHash,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    OutHash.Reset();
    FString CanonicalText;
    if (!FLuaAnimGraphIRLuaWriter::WriteModule(
        Blueprint,
        CanonicalText,
        OutDiagnostics))
    {
        return false;
    }

    const FTCHARToUTF8 Utf8Text(*CanonicalText);
    OutHash = FMD5::HashBytes(
        reinterpret_cast<const uint8*>(Utf8Text.Get()),
        static_cast<uint64>(Utf8Text.Length())).ToLower();
    return true;
}

/**
 * 分别通过 Blueprint Reader 与 Exchange Lua CompileIR 获取两侧 Canonical IR 和稳定哈希，
 * 再相对最近成功同步点更新状态。函数允许更新扩展的瞬时状态字段，但不调用 Modify、不标脏 package，
 * 不修改任何 Graph/文件，也不执行 Blueprint 编译；任一读取失败时状态为 Error 且保留同步基线哈希。
 * 必须在非 PIE 游戏线程调用。
 *
 * @param AnimBlueprint 待检查的精确标准动画蓝图，不可为空。
 * @param OutDiagnostics 接收 Reader、CompileIR、Validator 或 Writer 诊断；调用开始时清空。
 * @return 两侧 IR 与哈希均成功获得时返回 true；输入无效或任一侧失败时返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::RefreshLuaAnimBlueprintSyncStatus(
    UAnimBlueprint* AnimBlueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintFactoryPrivate;
    OutDiagnostics.Reset();
    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass()
        || Extension == nullptr
        || Extension->GetExchangeLuaModuleName().IsEmpty()
        || (GEditor != nullptr && GEditor->PlayWorld != nullptr))
    {
        if (Extension != nullptr)
        {
            Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::Error;
        }
        FLuaAnimIRSourceLocation Location;
        Location.LuaModule = AnimBlueprint != nullptr
            ? AnimBlueprint->GetPathName()
            : TEXT("None");
        AddError(
            OutDiagnostics,
            WrongThread,
            TEXT("Sync status refresh requires a configured UAnimBlueprint on the non-PIE game thread."),
            Location.LuaModule,
            Location);
        return false;
    }

    FLuaAnimBlueprintIR BlueprintIR;
    FString BlueprintHash;
    if (!ReadAnimBlueprintToIR(AnimBlueprint, BlueprintIR, OutDiagnostics)
        || !ComputeCanonicalIRHash(BlueprintIR, BlueprintHash, OutDiagnostics))
    {
        Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::Error;
        return false;
    }

    UUnLuaFunctionLibrary::HotReload();
    FLuaAnimBlueprintIR LuaIR;
    TArray<FLuaAnimIRDiagnostic> LuaDiagnostics;
    FString LuaHash;
    const bool bLuaReady = ULuaAnimGraphIRLibrary::CompileLuaModule(
        Extension->GetExchangeLuaModuleName(),
        LuaIR,
        LuaDiagnostics)
        && ComputeCanonicalIRHash(LuaIR, LuaHash, LuaDiagnostics);
    OutDiagnostics.Append(LuaDiagnostics);
    if (!bLuaReady)
    {
        Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::Error;
        return false;
    }

    if (Extension->LastSynchronizedBlueprintHash.IsEmpty()
        || Extension->LastSynchronizedLuaHash.IsEmpty())
    {
        Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::NeverSynchronized;
        return true;
    }

    const bool bBlueprintChanged =
        BlueprintHash != Extension->LastSynchronizedBlueprintHash;
    const bool bLuaChanged = LuaHash != Extension->LastSynchronizedLuaHash;
    Extension->SyncStatus = bBlueprintChanged && bLuaChanged
        ? ELuaAnimBlueprintSyncStatus::BothChanged
        : bBlueprintChanged
            ? ELuaAnimBlueprintSyncStatus::BlueprintChanged
            : bLuaChanged
                ? ELuaAnimBlueprintSyncStatus::LuaChanged
                : ELuaAnimBlueprintSyncStatus::InSync;
    return true;
}

/**
 * 把当前标准 AnimBlueprint 读取为 IR，并安全写入运行时模块旁的独立 `.generated.lua` 交换模块。
 * 函数先写同目录唯一临时模块，再通过现有 Importer 回读并比较 Canonical IR；只有完全等价才替换目标。
 * 手写 LuaModuleName 文件永不写入。成功后仅更新扩展 GeneratedLuaModuleName 并标脏资产，不编译、不保存资产。
 * 必须在游戏线程且非 PIE 调用。
 *
 * @param AnimBlueprint 待读写交换元数据的标准动画蓝图；对象和 Graph 不会改变。
 * @param OutGeneratedModuleName 成功时接收 `<LuaModuleName>.generated`，失败时为空。
 * @param OutDiagnostics 接收 Reader、Writer、路径、文件和回读校验诊断。
 * @param bKeepBackup 目标已存在时是否保留一份 `.bak`；false 仍使用临时回滚文件保证失败不覆盖。
 * @return IR 完整读取、回读等价并原子替换成功时返回 true，否则保留旧目标和手写模块并返回 false。
 */
bool ULuaAnimBlueprintFactoryLibrary::AnimBlueprintToLua(
    UAnimBlueprint* AnimBlueprint,
    FString& OutGeneratedModuleName,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics,
    const bool bKeepBackup)
{
    using namespace LuaAnimBlueprintFactoryPrivate;
    OutGeneratedModuleName.Reset();
    OutDiagnostics.Reset();
    FLuaAnimIRSourceLocation Location;
    Location.LuaModule = AnimBlueprint != nullptr ? AnimBlueprint->GetPathName() : TEXT("None");
    if (!IsInGameThread()
        || AnimBlueprint == nullptr
        || AnimBlueprint->GetClass() != UAnimBlueprint::StaticClass()
        || (GEditor != nullptr && GEditor->PlayWorld != nullptr))
    {
        AddError(OutDiagnostics, WrongThread, TEXT("AnimBlueprintToLua requires an exact UAnimBlueprint on the non-PIE game thread."), Location.LuaModule, Location);
        return false;
    }

    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
    const FString RuntimeModule = Extension != nullptr
        ? Extension->LuaModuleName
        : FString();
    TArray<FString> Segments;
    RuntimeModule.ParseIntoArray(Segments, TEXT("."), false);
    bool bSafeModule = !RuntimeModule.IsEmpty()
        && !RuntimeModule.Contains(TEXT(".."))
        && !RuntimeModule.Contains(TEXT("/"))
        && !RuntimeModule.Contains(TEXT("\\"))
        && !RuntimeModule.Contains(TEXT(":"))
        && FPaths::IsRelative(RuntimeModule);
    for (const FString& Segment : Segments)
    {
        if (Segment.IsEmpty())
        {
            bSafeModule = false;
            break;
        }
        for (const TCHAR Character : Segment)
        {
            if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
            {
                bSafeModule = false;
                break;
            }
        }
        if (!bSafeModule) break;
    }
    if (!bSafeModule)
    {
        AddError(OutDiagnostics, TEXT("Writer.UnsafeModulePath"), TEXT("LuaModuleName must be a relative dot-separated identifier without traversal or path separators."), RuntimeModule, Location);
        return false;
    }

    FLuaAnimBlueprintIR ReadIR;
    if (!ReadAnimBlueprintToIR(AnimBlueprint, ReadIR, OutDiagnostics)) return false;
    FString LuaText;
    if (!FLuaAnimGraphIRLuaWriter::WriteModule(ReadIR, LuaText, OutDiagnostics)) return false;

    const FString GeneratedModule = RuntimeModule + TEXT(".generated");
    const FString TempModule = RuntimeModule + TEXT(".generated_tmp_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString ScriptRoot = FPaths::ConvertRelativePathToFull(
        UUnLuaFunctionLibrary::GetScriptRootPath());
    FString NormalizedRoot = ScriptRoot;
    FPaths::NormalizeDirectoryName(NormalizedRoot);
    const FString RelativeTarget = GeneratedModule.Replace(TEXT("."), TEXT("/")) + TEXT(".lua");
    const FString RelativeTemp = TempModule.Replace(TEXT("."), TEXT("/")) + TEXT(".lua");
    FString TargetPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(NormalizedRoot, RelativeTarget));
    FString TempPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(NormalizedRoot, RelativeTemp));
    FPaths::NormalizeFilename(TargetPath);
    FPaths::NormalizeFilename(TempPath);
    FString RootPrefix = NormalizedRoot;
    FPaths::NormalizeFilename(RootPrefix);
    RootPrefix += TEXT("/");
    if (!TargetPath.StartsWith(RootPrefix, ESearchCase::IgnoreCase)
        || !TempPath.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
    {
        AddError(OutDiagnostics, TEXT("Writer.PathEscapesScriptRoot"), TEXT("Resolved generated module path escapes the configured UnLua ScriptRoot."), TargetPath, Location);
        return false;
    }

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.MakeDirectory(*FPaths::GetPath(TargetPath), true)
        || !FFileHelper::SaveStringToFile(
            LuaText,
            *TempPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        AddError(OutDiagnostics, TEXT("Writer.TempWriteFailed"), TEXT("Failed to write generated Lua temporary file."), TempPath, Location);
        FileManager.Delete(*TempPath, false, true);
        return false;
    }

    UUnLuaFunctionLibrary::HotReload();
    FLuaAnimBlueprintIR RoundTripIR;
    TArray<FLuaAnimIRDiagnostic> RoundTripDiagnostics;
    bool bEquivalent = ULuaAnimGraphIRLibrary::CompileLuaModule(
        TempModule,
        RoundTripIR,
        RoundTripDiagnostics);
    if (bEquivalent)
    {
        ULuaAnimGraphIRLibrary::Canonicalize(ReadIR);
        ULuaAnimGraphIRLibrary::Canonicalize(RoundTripIR);
        bEquivalent = FLuaAnimBlueprintIR::StaticStruct()->CompareScriptStruct(
            &ReadIR,
            &RoundTripIR,
            0);
    }
    if (!bEquivalent)
    {
        OutDiagnostics.Append(RoundTripDiagnostics);
        AddError(OutDiagnostics, TEXT("Writer.RoundTripMismatch"), TEXT("Generated Lua did not round-trip to an equivalent Canonical IR; the old target was preserved."), GeneratedModule, Location);
        FileManager.Delete(*TempPath, false, true);
        return false;
    }

    FString BlueprintHash;
    FString LuaHash;
    TArray<FLuaAnimIRDiagnostic> HashDiagnostics;
    const bool bBlueprintHashed = ComputeCanonicalIRHash(
        ReadIR,
        BlueprintHash,
        HashDiagnostics);
    OutDiagnostics.Append(HashDiagnostics);
    const bool bLuaHashed = ComputeCanonicalIRHash(
        RoundTripIR,
        LuaHash,
        HashDiagnostics);
    OutDiagnostics.Append(HashDiagnostics);
    if (!bBlueprintHashed || !bLuaHashed)
    {
        AddError(
            OutDiagnostics,
            TEXT("Writer.SyncHashFailed"),
            TEXT("Generated Lua passed round-trip validation, but synchronization hashes could not be calculated; the old target was preserved."),
            GeneratedModule,
            Location);
        FileManager.Delete(*TempPath, false, true);
        return false;
    }

    const FString BackupPath = TargetPath + (bKeepBackup ? TEXT(".bak") : TEXT(".rollback"));
    const bool bHadTarget = FileManager.FileExists(*TargetPath);
    if (bHadTarget)
    {
        FileManager.Delete(*BackupPath, false, true);
        if (!FileManager.Move(*BackupPath, *TargetPath, true, true))
        {
            AddError(OutDiagnostics, TEXT("Writer.BackupFailed"), TEXT("Existing generated Lua could not be moved to its rollback file."), TargetPath, Location);
            FileManager.Delete(*TempPath, false, true);
            return false;
        }
    }
    if (!FileManager.Move(*TargetPath, *TempPath, true, true))
    {
        if (bHadTarget) FileManager.Move(*TargetPath, *BackupPath, true, true);
        AddError(OutDiagnostics, TEXT("Writer.AtomicReplaceFailed"), TEXT("Generated Lua atomic replacement failed; the previous target was restored."), TargetPath, Location);
        FileManager.Delete(*TempPath, false, true);
        return false;
    }
    if (bHadTarget && !bKeepBackup) FileManager.Delete(*BackupPath, false, true);

    Extension->Modify();
    Extension->GeneratedLuaModuleName = GeneratedModule;
    Extension->bSourceDirty = false;
    Extension->MarkSynchronized(BlueprintHash, LuaHash);
    Extension->LastCompileMessage =
        TEXT("AnimBlueprint IR exported to generated Lua exchange module.");
    AnimBlueprint->GetOutermost()->MarkPackageDirty();
    OutGeneratedModuleName = GeneratedModule;
    return true;
}
