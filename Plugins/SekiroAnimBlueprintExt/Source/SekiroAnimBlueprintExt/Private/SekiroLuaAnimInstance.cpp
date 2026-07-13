#include "SekiroLuaAnimInstance.h"

#include "SekiroAnimBlueprintExt.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "GameFramework/Actor.h"
#include "UnLua.h"
#include "UnLuaInterface.h"
#include "UnLuaModule.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeRWLock.h"

#include <limits>

namespace
{
    static const char* LuaAnimConfigureFunctionName = "Configure";
    static const char* LuaAnimInitializeFunctionName = "Initialize";
    static const char* LuaAnimUpdateFunctionName = "Update";
    static const char* LuaAnimUpdateLayerFunctionName = "UpdateLayer";
    static const TCHAR* DefaultLuaAnimLayerNameText = TEXT("Default");

    /** 作用：把脚本模式名解析为根运动旋转枚举。@param ModeName FName，模式名。@param OutMode ESekiroLuaRootMotionRotationMode&，输出枚举。@return bool，名称受支持时为 true。 */
    bool TryResolveLuaRootMotionRotationMode(FName ModeName, ESekiroLuaRootMotionRotationMode& OutMode)
    {
        if (ModeName == FName(TEXT("Extract")))
        {
            OutMode = ESekiroLuaRootMotionRotationMode::Extract;
            return true;
        }
        if (ModeName == FName(TEXT("Ignore")))
        {
            OutMode = ESekiroLuaRootMotionRotationMode::Ignore;
            return true;
        }
        if (ModeName == FName(TEXT("WarpToTarget")))
        {
            OutMode = ESekiroLuaRootMotionRotationMode::WarpToTarget;
            return true;
        }
        if (ModeName == FName(TEXT("SteerToTarget")))
        {
            OutMode = ESekiroLuaRootMotionRotationMode::SteerToTarget;
            return true;
        }

        return false;
    }

    /** 作用：在对象类型中查找经过裁剪的属性名。@param Object const UObject*，反射对象。@param PropertyName const FString&，属性名。@return const FProperty*，属性描述，失败返回 nullptr。 */
    const FProperty* FindLuaAnimProperty(const UObject* Object, const FString& PropertyName)
    {
        if (!Object) return nullptr;

        const FString TrimmedPropertyName = PropertyName.TrimStartAndEnd();
        if (TrimmedPropertyName.IsEmpty()) return nullptr;

        return FindFProperty<FProperty>(Object->GetClass(), FName(*TrimmedPropertyName));
    }

    /** 作用：读取整数反射属性。@param NumericProperty const FNumericProperty*，数值属性。@param ValuePtr const void*，属性值地址。@return int64，整数值，参数无效返回 0。 */
    int64 GetLuaAnimIntegerValue(const FNumericProperty* NumericProperty, const void* ValuePtr)
    {
        if (!NumericProperty || !ValuePtr) return 0;
        return NumericProperty->GetSignedIntPropertyValue(ValuePtr);
    }

    /** 作用：把数值或布尔反射属性转换为 float。@param Property const FProperty*，属性描述。@param ValuePtr const void*，值地址。@param DefaultValue float，不支持时返回值。@return float，转换结果或默认值。 */
    float GetLuaAnimNumberValue(const FProperty* Property, const void* ValuePtr, float DefaultValue)
    {
        if (!Property || !ValuePtr) return DefaultValue;

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            if (NumericProperty->IsFloatingPoint())
            {
                return static_cast<float>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
            }

            if (NumericProperty->IsInteger())
            {
                return static_cast<float>(GetLuaAnimIntegerValue(NumericProperty, ValuePtr));
            }
        }

        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            return BoolProperty->GetPropertyValue(ValuePtr) ? 1.0f : 0.0f;
        }

        return DefaultValue;
    }

    /** 作用：把枚举值转换为稳定名称。@param Enum const UEnum*，枚举描述，可为空。@param Value int64，枚举数值。@return FString，枚举名或数字文本。 */
    FString GetLuaAnimEnumText(const UEnum* Enum, int64 Value)
    {
        if (Enum)
        {
            return Enum->GetNameStringByValue(Value);
        }

        return LexToString(Value);
    }

    /** 作用：把常见反射属性转换为文本。@param Property const FProperty*，属性描述。@param ValuePtr const void*，值地址。@param DefaultValue const FString&，不支持时返回值。@return FString，文本结果或默认值。 */
    FString GetLuaAnimPropertyTextValue(const FProperty* Property, const void* ValuePtr, const FString& DefaultValue)
    {
        if (!Property || !ValuePtr) return DefaultValue;

        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
            return GetLuaAnimEnumText(EnumProperty->GetEnum(), Value);
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            const uint8 Value = ByteProperty->GetPropertyValue(ValuePtr);
            return ByteProperty->Enum ? GetLuaAnimEnumText(ByteProperty->Enum, Value) : LexToString(Value);
        }

        if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            return NameProperty->GetPropertyValue(ValuePtr).ToString();
        }

        if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
        {
            return StringProperty->GetPropertyValue(ValuePtr);
        }

        if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
        {
            return TextProperty->GetPropertyValue(ValuePtr).ToString();
        }

        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            return BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("true") : TEXT("false");
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            if (NumericProperty->IsFloatingPoint())
            {
                return FString::SanitizeFloat(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
            }

            if (NumericProperty->IsInteger())
            {
                return LexToString(GetLuaAnimIntegerValue(NumericProperty, ValuePtr));
            }
        }

        return DefaultValue;
    }

    /** 作用：将 UTF-8 字符串压入 Lua 栈。@param LuaState lua_State*，Lua 状态。@param Value const FString&，待压入文本。@return void，无返回值。 */
    void PushLuaAnimString(lua_State* LuaState, const FString& Value)
    {
        const FTCHARToUTF8 ConvertedValue(*Value);
        lua_pushstring(LuaState, ConvertedValue.Get());
    }

    /** 作用：设置栈顶 Lua table 的字符串字段。@param LuaState lua_State*，Lua 状态。@param FieldName const char*，UTF-8 字段名。@param Value const FString&，字段值。@return void，无返回值。 */
    void SetLuaAnimStringField(lua_State* LuaState, const char* FieldName, const FString& Value)
    {
        PushLuaAnimString(LuaState, Value);
        lua_setfield(LuaState, -2, FieldName);
    }

    /** 作用：设置栈顶 Lua table 的数值字段。@param LuaState lua_State*，Lua 状态。@param FieldName const char*，字段名。@param Value double，字段值。@return void，无返回值。 */
    void SetLuaAnimNumberField(lua_State* LuaState, const char* FieldName, double Value)
    {
        lua_pushnumber(LuaState, Value);
        lua_setfield(LuaState, -2, FieldName);
    }

    /** 作用：设置栈顶 Lua table 的布尔字段。@param LuaState lua_State*，Lua 状态。@param FieldName const char*，字段名。@param bValue bool，字段值。@return void，无返回值。 */
    void SetLuaAnimBoolField(lua_State* LuaState, const char* FieldName, bool bValue)
    {
        lua_pushboolean(LuaState, bValue);
        lua_setfield(LuaState, -2, FieldName);
    }

    /** 作用：将受支持的 UE 属性值压入 Lua 栈。@param LuaState lua_State*，Lua 状态。@param Property const FProperty*，属性描述。@param ValuePtr const void*，值地址。@return bool，类型受支持且成功压栈时为 true。 */
    bool PushLuaAnimPropertyValue(lua_State* LuaState, const FProperty* Property, const void* ValuePtr)
    {
        if (!LuaState || !Property || !ValuePtr) return false;

        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
            PushLuaAnimString(LuaState, GetLuaAnimEnumText(EnumProperty->GetEnum(), Value));
            return true;
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            const uint8 Value = ByteProperty->GetPropertyValue(ValuePtr);
            if (ByteProperty->Enum)
            {
                PushLuaAnimString(LuaState, GetLuaAnimEnumText(ByteProperty->Enum, Value));
            }
            else
            {
                lua_pushinteger(LuaState, Value);
            }
            return true;
        }

        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            lua_pushboolean(LuaState, BoolProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            if (NumericProperty->IsFloatingPoint())
            {
                lua_pushnumber(LuaState, NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
                return true;
            }

            if (NumericProperty->IsInteger())
            {
                lua_pushinteger(LuaState, GetLuaAnimIntegerValue(NumericProperty, ValuePtr));
                return true;
            }
        }

        if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            PushLuaAnimString(LuaState, NameProperty->GetPropertyValue(ValuePtr).ToString());
            return true;
        }

        if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
        {
            PushLuaAnimString(LuaState, StringProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
        {
            PushLuaAnimString(LuaState, TextProperty->GetPropertyValue(ValuePtr).ToString());
            return true;
        }

        return false;
    }

    /** 作用：把动画快照转换为新的 Lua table 并压栈。@param LuaState lua_State*，Lua 状态。@param Snapshot const FSekiroLuaAnimSnapshot&，快照副本。@return void，无返回值。 */
    void PushLuaAnimSnapshotTable(lua_State* LuaState, const FSekiroLuaAnimSnapshot& Snapshot)
    {
        lua_newtable(LuaState);
        SetLuaAnimStringField(LuaState, "CurrentStateName", Snapshot.CurrentStateName.ToString());
        SetLuaAnimStringField(LuaState, "PreviousStateName", Snapshot.PreviousStateName.ToString());
        SetLuaAnimNumberField(LuaState, "CurrentTime", Snapshot.CurrentTime);
        SetLuaAnimNumberField(LuaState, "PreviousTime", Snapshot.PreviousTime);
        SetLuaAnimNumberField(LuaState, "CurrentPlayRate", Snapshot.CurrentPlayRate);
        SetLuaAnimNumberField(LuaState, "PreviousPlayRate", Snapshot.PreviousPlayRate);
        SetLuaAnimStringField(LuaState, "CurrentRootMotionRotationMode", StaticEnum<ESekiroLuaRootMotionRotationMode>()->GetNameStringByValue(static_cast<int64>(Snapshot.CurrentRootMotionRotationMode)));
        SetLuaAnimNumberField(LuaState, "CurrentRootMotionTargetWorldYaw", Snapshot.CurrentRootMotionTargetWorldYaw);
        SetLuaAnimNumberField(LuaState, "CurrentRootMotionMaxYawRate", Snapshot.CurrentRootMotionMaxYawRate);
        SetLuaAnimNumberField(LuaState, "CurrentRootMotionCompletionTimeSeconds", Snapshot.CurrentRootMotionCompletionTimeSeconds);
        SetLuaAnimStringField(LuaState, "PreviousRootMotionRotationMode", StaticEnum<ESekiroLuaRootMotionRotationMode>()->GetNameStringByValue(static_cast<int64>(Snapshot.PreviousRootMotionRotationMode)));
        SetLuaAnimNumberField(LuaState, "PreviousRootMotionTargetWorldYaw", Snapshot.PreviousRootMotionTargetWorldYaw);
        SetLuaAnimNumberField(LuaState, "PreviousRootMotionMaxYawRate", Snapshot.PreviousRootMotionMaxYawRate);
        SetLuaAnimNumberField(LuaState, "PreviousRootMotionCompletionTimeSeconds", Snapshot.PreviousRootMotionCompletionTimeSeconds);
        SetLuaAnimNumberField(LuaState, "BlendAlpha", Snapshot.BlendAlpha);
        SetLuaAnimNumberField(LuaState, "BlendTime", Snapshot.BlendTime);
        SetLuaAnimNumberField(LuaState, "BlendElapsedTime", Snapshot.BlendElapsedTime);
        SetLuaAnimBoolField(LuaState, "CurrentLoop", Snapshot.bCurrentLoop);
        SetLuaAnimBoolField(LuaState, "PreviousLoop", Snapshot.bPreviousLoop);
        SetLuaAnimBoolField(LuaState, "HasPose", Snapshot.bHasPose);
        SetLuaAnimNumberField(LuaState, "CurrentBlendInputX", Snapshot.CurrentBlendInput.X);
        SetLuaAnimNumberField(LuaState, "CurrentBlendInputY", Snapshot.CurrentBlendInput.Y);
        SetLuaAnimNumberField(LuaState, "CurrentBlendInputZ", Snapshot.CurrentBlendInput.Z);
        SetLuaAnimNumberField(LuaState, "PreviousBlendInputX", Snapshot.PreviousBlendInput.X);
        SetLuaAnimNumberField(LuaState, "PreviousBlendInputY", Snapshot.PreviousBlendInput.Y);
        SetLuaAnimNumberField(LuaState, "PreviousBlendInputZ", Snapshot.PreviousBlendInput.Z);
        SetLuaAnimBoolField(LuaState, "UseInertialization", Snapshot.bUseInertialization);
        SetLuaAnimNumberField(LuaState, "InertialBlendTime", Snapshot.InertialBlendTime);
        SetLuaAnimBoolField(LuaState, "OrientationWarpingEnabled", Snapshot.OrientationWarpingPolicy.bEnabled);
        SetLuaAnimNumberField(LuaState, "OrientationAngle", Snapshot.OrientationWarpingPolicy.OrientationAngle);
        SetLuaAnimNumberField(LuaState, "OrientationWarpingAlpha", Snapshot.OrientationWarpingPolicy.WarpingAlpha);
        SetLuaAnimBoolField(LuaState, "WarpRootMotionTranslation", Snapshot.OrientationWarpingPolicy.bWarpRootMotionTranslation);
        SetLuaAnimNumberField(LuaState, "RootMotionTranslationAngle", Snapshot.OrientationWarpingPolicy.RootMotionTranslationAngle);

        const UAnimationAsset* CurrentAnimationAsset = Snapshot.CurrentAnimationAsset.Get();
        const UAnimationAsset* PreviousAnimationAsset = Snapshot.PreviousAnimationAsset.Get();
        SetLuaAnimStringField(LuaState, "CurrentAnimationPath", CurrentAnimationAsset ? CurrentAnimationAsset->GetPathName() : FString());
        SetLuaAnimStringField(LuaState, "PreviousAnimationPath", PreviousAnimationAsset ? PreviousAnimationAsset->GetPathName() : FString());
        SetLuaAnimStringField(LuaState, "CurrentAnimationName", Snapshot.CurrentAnimationName.ToString());
        SetLuaAnimStringField(LuaState, "PreviousAnimationName", Snapshot.PreviousAnimationName.ToString());
    }

    /** 作用：构造包含反射属性和快照的 Lua 运行时上下文。@param LuaEnv UnLua::FLuaEnv*，UnLua 环境。@param ContextObject const UObject*，动画实例。@param LayerName FName，目标层。@return int32，Lua 栈中 table 索引，失败返回 INDEX_NONE。 */
    int32 PushLuaAnimRuntimeContextTable(UnLua::FLuaEnv* LuaEnv, const UObject* ContextObject, FName LayerName)
    {
        lua_State* LuaState = LuaEnv ? LuaEnv->GetMainState() : nullptr;
        if (!LuaState) return INDEX_NONE;

        lua_newtable(LuaState);
        const int32 TableIndex = lua_gettop(LuaState);
        SetLuaAnimStringField(LuaState, "LayerName", LayerName.ToString());

        if (ContextObject)
        {
            for (TFieldIterator<FProperty> PropertyIt(ContextObject->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
            {
                const FProperty* Property = *PropertyIt;
                const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContextObject);
                if (PushLuaAnimPropertyValue(LuaState, Property, ValuePtr))
                {
                    const FTCHARToUTF8 FieldNameUtf8(*Property->GetName());
                    lua_setfield(LuaState, TableIndex, FieldNameUtf8.Get());
                }
            }
        }

        const USekiroLuaAnimInstance* LuaAnimInstance = Cast<USekiroLuaAnimInstance>(ContextObject);
        if (LuaAnimInstance)
        {
            const FSekiroLuaAnimSnapshot Snapshot = LuaAnimInstance->GetLuaAnimLayerSnapshot(LayerName);
            PushLuaAnimSnapshotTable(LuaState, Snapshot);
            lua_setfield(LuaState, TableIndex, "Snapshot");
        }

        return TableIndex;
    }

    /** 作用：过滤非有限播放倍率。@param PlayRate float，脚本输入倍率。@return float，有限倍率，非法时返回 1。 */
    float GetSafeLuaPlayRate(float PlayRate)
    {
        if (!FMath::IsFinite(PlayRate))
        {
            return 1.0f;
        }

        return PlayRate;
    }

    /** 作用：逐分量过滤非法 BlendSpace 输入。@param BlendInput const FVector&，脚本输入。@return FVector，有限输入，非法分量置 0。 */
    FVector GetSafeLuaBlendInput(const FVector& BlendInput)
    {
        return FVector(
            FMath::IsFinite(BlendInput.X) ? BlendInput.X : 0.0f,
            FMath::IsFinite(BlendInput.Y) ? BlendInput.Y : 0.0f,
            FMath::IsFinite(BlendInput.Z) ? BlendInput.Z : 0.0f);
    }

    /** 作用：获取动画资产调试名称。@param AnimationAsset const UAnimationAsset*，动画资产。@return FString，资产名，空资产返回 None。 */
    FString GetLuaAnimDebugAssetName(const UAnimationAsset* AnimationAsset)
    {
        return AnimationAsset ? AnimationAsset->GetName() : FString(TEXT("None"));
    }

    /** 作用：组合资产名和 Lua 别名。@param AnimationAsset const UAnimationAsset*，动画资产。@param AnimationName FName，Lua 别名。@return FString，调试标签。 */
    FString GetLuaAnimDebugAssetLabel(const UAnimationAsset* AnimationAsset, FName AnimationName)
    {
        const FString AssetName = GetLuaAnimDebugAssetName(AnimationAsset);
        if (AnimationName.IsNone()) return AssetName;

        return FString::Printf(TEXT("%s [%s]"), *AssetName, *AnimationName.ToString());
    }

    /** 作用：生成 BlendSpace 有效样本及权重文本。@param AnimationAsset const UAnimationAsset*，候选混合空间。@param BlendInput const FVector&，混合输入。@return FString，样本文本；非混合空间返回空。 */
    FString GetLuaAnimDebugBlendSampleText(const UAnimationAsset* AnimationAsset, const FVector& BlendInput)
    {
        const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset);
        if (!BlendSpace) return FString();

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return FString(TEXT("BlendSamples=None"));
        }

        TArray<FString> SampleTexts;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            SampleTexts.Add(FString::Printf(TEXT("%s %.1f%%"), *GetNameSafe(BlendSample.Animation), SampleWeight * 100.0f));
        }

        if (SampleTexts.Num() <= 0)
        {
            return FString(TEXT("BlendSamples=None"));
        }

        return FString::Printf(TEXT("BlendSamples=%s"), *FString::Join(SampleTexts, TEXT(", ")));
    }

    /** 作用：计算 BlendSpace 指定输入下的加权播放长度。@param BlendSpace const UBlendSpace*，混合空间。@param BlendInput const FVector&，混合输入。@return float，加权长度，单位秒；失败返回 0。 */
    float GetSekiroLuaBlendSpaceSampleLength(const UBlendSpace* BlendSpace, const FVector& BlendInput)
    {
        if (!BlendSpace) return 0.0f;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return 0.0f;
        }

        float WeightedPlayLength = 0.0f;
        float TotalWeight = 0.0f;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequence* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleRateScale = Sequence->RateScale * SampleData.SamplePlayRate;
            const float SafeRateScale = FMath::IsNearlyZero(SampleRateScale) ? 1.0f : FMath::Abs(SampleRateScale);
            WeightedPlayLength += (Sequence->GetPlayLength() / SafeRateScale) * SampleWeight;
            TotalWeight += SampleWeight;
        }

        if (TotalWeight <= UE_KINDA_SMALL_NUMBER) return 0.0f;
        return WeightedPlayLength / TotalWeight;
    }

    /** 作用：获取序列或 BlendSpace 的有效播放长度。@param AnimationAsset const UAnimationAsset*，动画资产。@param BlendInput const FVector&，混合输入。@return float，长度，单位秒；无资产返回 0。 */
    float GetSekiroLuaAnimationPlayLength(const UAnimationAsset* AnimationAsset, const FVector& BlendInput)
    {
        if (!AnimationAsset) return 0.0f;

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            const float BlendSpaceSampleLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendInput);
            if (BlendSpaceSampleLength > UE_KINDA_SMALL_NUMBER) return BlendSpaceSampleLength;
        }

        return AnimationAsset->GetPlayLength();
    }

    /** 作用：限制非循环 Root Motion 预览的提取时间。@param CurrentTime float，当前时间。@param PreviewDeltaTime float，预览步长。@param PlayLength float，动画长度。@param bLoop bool，是否循环。@return float，可提取时间，单位秒。 */
    float GetSekiroLuaRootMotionDeltaTime(float CurrentTime, float PreviewDeltaTime, float PlayLength, bool bLoop)
    {
        if (PreviewDeltaTime <= UE_KINDA_SMALL_NUMBER || PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;
        if (bLoop) return PreviewDeltaTime;

        return FMath::Max(0.0f, FMath::Min(CurrentTime + PreviewDeltaTime, PlayLength) - CurrentTime);
    }

    /** 作用：提取序列的短时局部 Root Motion 供调试。@param Sequence const UAnimSequenceBase*，动画序列。@param CurrentTime float，当前时间。@param PreviewDeltaTime float，预览步长。@param bLoop bool，是否循环。@return FTransform，局部根运动增量。 */
    FTransform ExtractSekiroLuaSequenceRootMotionPreview(const UAnimSequenceBase* Sequence, float CurrentTime, float PreviewDeltaTime, bool bLoop)
    {
        if (!Sequence) return FTransform::Identity;

        const float PlayLength = Sequence->GetPlayLength();
        const float DeltaTime = GetSekiroLuaRootMotionDeltaTime(CurrentTime, PreviewDeltaTime, PlayLength, bLoop);
        if (DeltaTime <= UE_KINDA_SMALL_NUMBER) return FTransform::Identity;

        return Sequence->ExtractRootMotion(CurrentTime, DeltaTime, bLoop);
    }

    /** 作用：加权提取 BlendSpace 样本的短时 Root Motion。@param BlendSpace const UBlendSpace*，混合空间。@param CurrentTime float，当前时间。@param PreviewDeltaTime float，预览步长。@param bLoop bool，是否循环。@param BlendInput const FVector&，混合输入。@return FTransform，加权局部根运动。 */
    FTransform ExtractSekiroLuaBlendSpaceRootMotionPreview(const UBlendSpace* BlendSpace, float CurrentTime, float PreviewDeltaTime, bool bLoop, const FVector& BlendInput)
    {
        FRootMotionMovementParams RootMotionParams;
        if (!BlendSpace) return FTransform::Identity;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return FTransform::Identity;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendInput);
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return FTransform::Identity;

        const float CurrentNormalizedTime = FMath::Clamp(CurrentTime / PlayLength, 0.0f, 1.0f);
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequence* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleTime = CurrentNormalizedTime * Sequence->GetPlayLength();
            RootMotionParams.AccumulateWithBlend(
                ExtractSekiroLuaSequenceRootMotionPreview(Sequence, SampleTime, PreviewDeltaTime, bLoop),
                SampleWeight);
        }

        if (!RootMotionParams.bHasRootMotion) return FTransform::Identity;
        return RootMotionParams.GetRootMotionTransform();
    }

    /** 作用：按资产类型分派 Root Motion 预览提取。@param AnimationAsset const UAnimationAsset*，动画资产。@param CurrentTime float，当前时间。@param PreviewDeltaTime float，预览步长。@param bLoop bool，是否循环。@param BlendInput const FVector&，混合输入。@return FTransform，局部根运动。 */
    FTransform ExtractSekiroLuaRootMotionPreview(const UAnimationAsset* AnimationAsset, float CurrentTime, float PreviewDeltaTime, bool bLoop, const FVector& BlendInput)
    {
        if (!AnimationAsset) return FTransform::Identity;

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return ExtractSekiroLuaBlendSpaceRootMotionPreview(BlendSpace, CurrentTime, PreviewDeltaTime, bLoop, BlendInput);
        }

        return ExtractSekiroLuaSequenceRootMotionPreview(Cast<UAnimSequenceBase>(AnimationAsset), CurrentTime, PreviewDeltaTime, bLoop);
    }

    /** 作用：按骨骼网格旋转把局部 Root Motion 预览转换到世界空间。@param MeshComponent const USkeletalMeshComponent*，骨骼网格组件。@param LocalRootMotion const FTransform&，局部增量。@return FTransform，世界空间增量。 */
    FTransform ConvertSekiroLuaRootMotionPreviewToWorld(const USkeletalMeshComponent* MeshComponent, const FTransform& LocalRootMotion)
    {
        if (!MeshComponent) return LocalRootMotion;

        const AActor* Owner = MeshComponent->GetOwner();
        if (!Owner) return LocalRootMotion;

        const FTransform ActorToWorld = Owner->GetTransform();
        const FTransform ComponentToWorld = MeshComponent->GetComponentTransform();
        const FTransform ComponentToActor = ActorToWorld.GetRelativeTransform(ComponentToWorld);
        const FTransform NewComponentToWorld = LocalRootMotion * ComponentToWorld;
        const FTransform NewActorTransform = ComponentToActor * NewComponentToWorld;
        const FVector DeltaWorldTranslation = NewActorTransform.GetTranslation() - ActorToWorld.GetTranslation();
        const FQuat NewWorldRotation = ComponentToWorld.GetRotation() * LocalRootMotion.GetRotation();
        const FQuat DeltaWorldRotation = NewWorldRotation * ComponentToWorld.GetRotation().Inverse();

        return FTransform(DeltaWorldRotation, DeltaWorldTranslation);
    }

    /** 作用：按循环、反播和资产长度推进动画时间。@param AnimationAsset const UAnimationAsset*，动画资产。@param BlendInput const FVector&，混合输入。@param bLoop bool，是否循环。@param CurrentTime float，当前时间。@param PlayRate float，播放倍率。@param DeltaSeconds float，本帧秒数。@return float，推进后的时间。 */
    float AdvanceSekiroAnimTime(const UAnimationAsset* AnimationAsset, const FVector& BlendInput, bool bLoop, float CurrentTime, float PlayRate, float DeltaSeconds)
    {
        if (!AnimationAsset) return 0.0f;

        const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, BlendInput);
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;

        const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset);
        const float AssetRateScale = Sequence ? Sequence->RateScale : 1.0f;
        const float MoveDelta = DeltaSeconds * PlayRate * AssetRateScale;
        float NewTime = CurrentTime + MoveDelta;

        if (bLoop)
        {
            NewTime = FMath::Fmod(NewTime, PlayLength);
            if (NewTime < 0.0f)
            {
                NewTime += PlayLength;
            }

            return NewTime;
        }

        return FMath::Clamp(NewTime, 0.0f, PlayLength);
    }

    /** 作用：把决策的归一化起播位置转换为资产时间。@param AnimationAsset const UAnimationAsset*，目标资产。@param BlendInput const FVector&，混合输入。@param Decision const FSekiroLuaAnimDecision&，动画决策。@return float，起播时间，单位秒。 */
    float GetSekiroAnimStartTime(const UAnimationAsset* AnimationAsset, const FVector& BlendInput, const FSekiroLuaAnimDecision& Decision)
    {
        if (!AnimationAsset || !Decision.bUseStartPosition) return 0.0f;

        const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, BlendInput);
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;

        return FMath::Clamp(Decision.StartPosition, 0.0f, 1.0f) * PlayLength;
    }

    /** 作用：尝试求值序列中的标量曲线。@param Sequence const UAnimSequenceBase*，动画序列。@param CurveName FName，曲线名。@param Time float，采样时间。@param OutValue float&，输出曲线值。@return bool，曲线存在并成功求值时为 true。 */
    bool TryEvaluateSekiroLuaSequenceCurveValue(const UAnimSequenceBase* Sequence, FName CurveName, float Time, float& OutValue)
    {
        if (!Sequence || CurveName.IsNone()) return false;

        const USkeleton* Skeleton = Sequence->GetSkeleton();
        if (!Skeleton) return false;

        FSmartName SmartName;
        if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, SmartName))
        {
            return false;
        }

        const FAnimCurveBase* CurveBase = Sequence->GetCurveData().GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);
        const FFloatCurve* FloatCurve = static_cast<const FFloatCurve*>(CurveBase);
        if (!FloatCurve) return false;

        OutValue = FloatCurve->Evaluate(Time);
        return true;
    }

    /** 作用：求值序列标量曲线。@param Sequence const UAnimSequenceBase*，动画序列。@param CurveName FName，曲线名。@param Time float，采样时间。@param DefaultValue float，失败返回值。@return float，曲线值或默认值。 */
    float EvaluateSekiroLuaSequenceCurveValue(const UAnimSequenceBase* Sequence, FName CurveName, float Time, float DefaultValue)
    {
        float Value = DefaultValue;
        return TryEvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, Time, Value) ? Value : DefaultValue;
    }

    /** 作用：求值序列曲线并四舍五入为整数。@param Sequence const UAnimSequenceBase*，动画序列。@param CurveName FName，曲线名。@param Time float，采样时间。@param DefaultValue int32，失败返回值。@return int32，整数曲线值。 */
    int32 EvaluateSekiroLuaSequenceCurveIntValue(const UAnimSequenceBase* Sequence, FName CurveName, float Time, int32 DefaultValue)
    {
        return FMath::RoundToInt(EvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, Time, static_cast<float>(DefaultValue)));
    }

    /** 作用：按样本权重求值 BlendSpace 标量曲线。@param BlendSpace const UBlendSpace*，混合空间。@param CurveName FName，曲线名。@param CurrentTime float，当前时间。@param BlendInput const FVector&，混合输入。@param DefaultValue float，失败返回值。@return float，加权曲线值。 */
    float EvaluateSekiroLuaBlendSpaceCurveValue(const UBlendSpace* BlendSpace, FName CurveName, float CurrentTime, const FVector& BlendInput, float DefaultValue)
    {
        if (!BlendSpace || CurveName.IsNone()) return DefaultValue;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return DefaultValue;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendInput);
        const float NormalizedTime = PlayLength > UE_KINDA_SMALL_NUMBER ? CurrentTime / PlayLength : 0.0f;
        const float ClampedNormalizedTime = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
        float WeightedValue = 0.0f;
        float TotalWeight = 0.0f;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequenceBase* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleTime = ClampedNormalizedTime * Sequence->GetPlayLength();
            const float SampleValue = EvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, SampleTime, 0.0f);
            WeightedValue += SampleValue * SampleWeight;
            TotalWeight += SampleWeight;
        }

        if (TotalWeight <= UE_KINDA_SMALL_NUMBER) return DefaultValue;
        return WeightedValue / TotalWeight;
    }

    /** 作用：把曲线相位回绕到 [0,1)。@param Value float，任意相位值。@return float，回绕后的相位。 */
    float WrapSekiroLuaCircularCurveValue(float Value)
    {
        float WrappedValue = FMath::Fmod(Value, 1.0f);
        if (WrappedValue < 0.0f)
        {
            WrappedValue += 1.0f;
        }

        return WrappedValue;
    }

    /** 作用：计算两个归一化环形值的最短距离。@param FirstValue float，第一个值。@param SecondValue float，第二个值。@return float，范围为 [0,0.5] 的距离。 */
    float GetSekiroLuaCircularCurveDistance(float FirstValue, float SecondValue)
    {
        const float Distance = FMath::Abs(
            WrapSekiroLuaCircularCurveValue(FirstValue)
            - WrapSekiroLuaCircularCurveValue(SecondValue));
        return FMath::Min(Distance, 1.0f - Distance);
    }

    /** 作用：用单位圆向量加权 BlendSpace 相位曲线。@param BlendSpace const UBlendSpace*，混合空间。@param CurveName FName，曲线名。@param CurrentTime float，当前时间。@param BlendInput const FVector&，混合输入。@param DefaultValue float，失败返回值。@return float，环形相位或默认值。 */
    float EvaluateSekiroLuaBlendSpaceCircularCurveValue(const UBlendSpace* BlendSpace, FName CurveName, float CurrentTime, const FVector& BlendInput, float DefaultValue)
    {
        if (!BlendSpace || CurveName.IsNone()) return DefaultValue;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return DefaultValue;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendInput);
        const float NormalizedTime = PlayLength > UE_KINDA_SMALL_NUMBER ? CurrentTime / PlayLength : 0.0f;
        const float ClampedNormalizedTime = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
        float WeightedSin = 0.0f;
        float WeightedCos = 0.0f;
        float TotalWeight = 0.0f;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequenceBase* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleTime = ClampedNormalizedTime * Sequence->GetPlayLength();
            float SampleValue = 0.0f;
            if (!TryEvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, SampleTime, SampleValue)) continue;

            const float Angle = WrapSekiroLuaCircularCurveValue(SampleValue) * 2.0f * PI;
            WeightedSin += FMath::Sin(Angle) * SampleWeight;
            WeightedCos += FMath::Cos(Angle) * SampleWeight;
            TotalWeight += SampleWeight;
        }

        if (TotalWeight <= UE_KINDA_SMALL_NUMBER
            || (FMath::Abs(WeightedSin) <= UE_KINDA_SMALL_NUMBER && FMath::Abs(WeightedCos) <= UE_KINDA_SMALL_NUMBER))
        {
            return DefaultValue;
        }

        float Phase = FMath::Atan2(WeightedSin, WeightedCos) / (2.0f * PI);
        if (Phase < 0.0f)
        {
            Phase += 1.0f;
        }
        return Phase;
    }

    /** 作用：合并 BlendSpace 有效样本的整数曲线标志。@param BlendSpace const UBlendSpace*，混合空间。@param CurveName FName，曲线名。@param CurrentTime float，当前时间。@param BlendInput const FVector&，混合输入。@param DefaultValue int32，失败返回值。@return int32，按位合并结果或默认值。 */
    int32 EvaluateSekiroLuaBlendSpaceCurveIntValue(const UBlendSpace* BlendSpace, FName CurveName, float CurrentTime, const FVector& BlendInput, int32 DefaultValue)
    {
        if (!BlendSpace || CurveName.IsNone()) return DefaultValue;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return DefaultValue;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendInput);
        const float NormalizedTime = PlayLength > UE_KINDA_SMALL_NUMBER ? CurrentTime / PlayLength : 0.0f;
        const float ClampedNormalizedTime = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
        int32 MaskValue = 0;
        bool bFoundCurve = false;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequenceBase* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleTime = ClampedNormalizedTime * Sequence->GetPlayLength();
            const int32 SampleValue = EvaluateSekiroLuaSequenceCurveIntValue(Sequence, CurveName, SampleTime, -1);
            if (SampleValue < 0) continue;

            MaskValue |= SampleValue;
            bFoundCurve = true;
        }

        return bFoundCurve ? MaskValue : DefaultValue;
    }

    /** 作用：按资产类型求值普通曲线。@param AnimationAsset const UAnimationAsset*，动画资产。@param CurveName FName，曲线名。@param Time float，采样时间。@param BlendInput const FVector&，混合输入。@param DefaultValue float，失败返回值。@return float，曲线值。 */
    float EvaluateSekiroLuaAnimationCurveValue(const UAnimationAsset* AnimationAsset, FName CurveName, float Time, const FVector& BlendInput, float DefaultValue)
    {
        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return EvaluateSekiroLuaBlendSpaceCurveValue(BlendSpace, CurveName, Time, BlendInput, DefaultValue);
        }

        if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            return EvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, Time, DefaultValue);
        }

        return DefaultValue;
    }

    /** 作用：按资产类型求值环形曲线。@param AnimationAsset const UAnimationAsset*，动画资产。@param CurveName FName，曲线名。@param Time float，采样时间。@param BlendInput const FVector&，混合输入。@param DefaultValue float，失败返回值。@return float，环形曲线值。 */
    float EvaluateSekiroLuaAnimationCircularCurveValue(const UAnimationAsset* AnimationAsset, FName CurveName, float Time, const FVector& BlendInput, float DefaultValue)
    {
        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return EvaluateSekiroLuaBlendSpaceCircularCurveValue(BlendSpace, CurveName, Time, BlendInput, DefaultValue);
        }

        if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            float Value = DefaultValue;
            if (TryEvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, Time, Value))
            {
                return WrapSekiroLuaCircularCurveValue(Value);
            }
        }

        return DefaultValue;
    }

    /** 作用：按资产类型求值整数曲线。@param AnimationAsset const UAnimationAsset*，动画资产。@param CurveName FName，曲线名。@param Time float，采样时间。@param BlendInput const FVector&，混合输入。@param DefaultValue int32，失败返回值。@return int32，整数曲线值。 */
    int32 EvaluateSekiroLuaAnimationCurveIntValue(const UAnimationAsset* AnimationAsset, FName CurveName, float Time, const FVector& BlendInput, int32 DefaultValue)
    {
        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return EvaluateSekiroLuaBlendSpaceCurveIntValue(BlendSpace, CurveName, Time, BlendInput, DefaultValue);
        }

        if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            return EvaluateSekiroLuaSequenceCurveIntValue(Sequence, CurveName, Time, DefaultValue);
        }

        return DefaultValue;
    }

    /** 作用：读取 Lua table 字符串字段为 FName。@param LuaTable UnLua::FLuaTable&，来源 table。@param FieldName const char*，字段名。@param OutName FName&，输出名称。@return bool，字段有效且非 None 时为 true。 */
    bool ReadLuaNameField(UnLua::FLuaTable& LuaTable, const char* FieldName, FName& OutName)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TSTRING) return false;

        const FString FieldText = FieldValue.Value<FString>();
        OutName = FName(*FieldText);
        return !OutName.IsNone();
    }

    /** 作用：读取 Lua table 字符串字段。@param LuaTable UnLua::FLuaTable&，来源 table。@param FieldName const char*，字段名。@param OutValue FString&，输出文本。@return bool，字段为非空字符串时为 true。 */
    bool ReadLuaStringField(UnLua::FLuaTable& LuaTable, const char* FieldName, FString& OutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TSTRING) return false;

        OutValue = FieldValue.Value<FString>();
        return !OutValue.IsEmpty();
    }

    /** 作用：读取 Lua table 数值字段。@param LuaTable UnLua::FLuaTable&，来源 table。@param FieldName const char*，字段名。@param OutValue float&，输出数值。@return bool，字段为数值时为 true。 */
    bool ReadLuaFloatField(UnLua::FLuaTable& LuaTable, const char* FieldName, float& OutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TNUMBER) return false;

        OutValue = FieldValue.Value<float>();
        return true;
    }

    /** 作用：读取 Lua table 布尔字段。@param LuaTable UnLua::FLuaTable&，来源 table。@param FieldName const char*，字段名。@param bOutValue bool&，输出布尔值。@return bool，字段为布尔值时为 true。 */
    bool ReadLuaBoolField(UnLua::FLuaTable& LuaTable, const char* FieldName, bool& bOutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TBOOLEAN) return false;

        bOutValue = FieldValue.Value<bool>();
        return true;
    }

    /** 作用：读取 Lua userdata 字段为动画资产。@param LuaTable UnLua::FLuaTable&，来源 table。@param FieldName const char*，字段名。@param OutAnimationAsset UAnimationAsset*&，输出资产。@return bool，userdata 是动画资产时为 true。 */
    bool ReadLuaAnimationAssetField(UnLua::FLuaTable& LuaTable, const char* FieldName, UAnimationAsset*& OutAnimationAsset)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TUSERDATA) return false;

        UObject* ObjectValue = FieldValue.Value<UObject*>();
        OutAnimationAsset = Cast<UAnimationAsset>(ObjectValue);
        return OutAnimationAsset != nullptr;
    }

    /** 作用：将 Lua Update 返回值解析为动画决策。@param LuaEnv UnLua::FLuaEnv*，UnLua 环境。@param ReturnValue const UnLua::FLuaValue&，字符串或 table 返回值。@param OutDecision FSekiroLuaAnimDecision&，输出决策。@return bool，返回值包含有效状态时为 true。 */
    bool ReadLuaAnimDecision(UnLua::FLuaEnv* LuaEnv, const UnLua::FLuaValue& ReturnValue, FSekiroLuaAnimDecision& OutDecision)
    {
        if (!LuaEnv) return false;

        if (ReturnValue.GetType() == LUA_TSTRING)
        {
            const FString StateNameText = ReturnValue.Value<FString>();
            OutDecision.StateName = FName(*StateNameText);
            return !OutDecision.StateName.IsNone();
        }

        if (ReturnValue.GetType() != LUA_TTABLE) return false;

        UnLua::FLuaTable DecisionTable(LuaEnv, ReturnValue);
        FName StateName = NAME_None;
        if (!ReadLuaNameField(DecisionTable, "StateName", StateName))
        {
            ReadLuaNameField(DecisionTable, "State", StateName);
        }

        if (StateName.IsNone()) return false;

        OutDecision.StateName = StateName;
        if (!ReadLuaNameField(DecisionTable, "AnimationName", OutDecision.AnimationName))
        {
            if (!ReadLuaNameField(DecisionTable, "AnimationAlias", OutDecision.AnimationName))
            {
                ReadLuaNameField(DecisionTable, "AssetName", OutDecision.AnimationName);
            }
        }

        UAnimationAsset* AnimationAsset = nullptr;
        ReadLuaAnimationAssetField(DecisionTable, "AnimationAsset", AnimationAsset);
        if (!AnimationAsset)
        {
            ReadLuaAnimationAssetField(DecisionTable, "Animation", AnimationAsset);
        }

        OutDecision.AnimationAsset = AnimationAsset;

        if (OutDecision.AnimationPath.IsEmpty())
        {
            ReadLuaStringField(DecisionTable, "AnimationPath", OutDecision.AnimationPath);
        }

        if (OutDecision.AnimationPath.IsEmpty())
        {
            ReadLuaStringField(DecisionTable, "AssetPath", OutDecision.AnimationPath);
        }

        if (OutDecision.AnimationPath.IsEmpty())
        {
            ReadLuaStringField(DecisionTable, "Animation", OutDecision.AnimationPath);
        }

        ReadLuaFloatField(DecisionTable, "BlendTime", OutDecision.BlendTime);
        ReadLuaFloatField(DecisionTable, "PlayRate", OutDecision.PlayRate);
        if (!ReadLuaBoolField(DecisionTable, "Loop", OutDecision.bLoop))
        {
            ReadLuaBoolField(DecisionTable, "bLoop", OutDecision.bLoop);
        }

        float BlendInputX = static_cast<float>(OutDecision.BlendInput.X);
        float BlendInputY = static_cast<float>(OutDecision.BlendInput.Y);
        float BlendInputZ = static_cast<float>(OutDecision.BlendInput.Z);
        ReadLuaFloatField(DecisionTable, "BlendInputX", BlendInputX);
        ReadLuaFloatField(DecisionTable, "BlendInputY", BlendInputY);
        ReadLuaFloatField(DecisionTable, "BlendInputZ", BlendInputZ);
        if (FMath::IsNearlyZero(BlendInputX))
        {
            ReadLuaFloatField(DecisionTable, "BlendX", BlendInputX);
        }

        if (FMath::IsNearlyZero(BlendInputX))
        {
            ReadLuaFloatField(DecisionTable, "Speed", BlendInputX);
        }

        OutDecision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
        if (!ReadLuaBoolField(DecisionTable, "ResetTime", OutDecision.bResetTime))
        {
            ReadLuaBoolField(DecisionTable, "bResetTime", OutDecision.bResetTime);
        }

        if (!ReadLuaBoolField(DecisionTable, "UseInertialization", OutDecision.bUseInertialization))
        {
            ReadLuaBoolField(DecisionTable, "bUseInertialization", OutDecision.bUseInertialization);
        }
        ReadLuaFloatField(DecisionTable, "InertialBlendTime", OutDecision.InertialBlendTime);

        float StartPosition = OutDecision.StartPosition;
        bool bReadStartPosition = ReadLuaFloatField(DecisionTable, "StartPosition", StartPosition);
        if (!bReadStartPosition)
        {
            bReadStartPosition = ReadLuaFloatField(DecisionTable, "NormalizedStartPosition", StartPosition);
        }

        if (bReadStartPosition)
        {
            OutDecision.StartPosition = FMath::Clamp(StartPosition, 0.0f, 1.0f);
            OutDecision.bUseStartPosition = true;
        }

        return true;
    }

    /** 作用：调用 Lua require 并验证模块返回 table。@param LuaEnv UnLua::FLuaEnv*，UnLua 环境。@param LuaModuleName const FString&，模块名。@param bOutSucceeded bool&，输出是否成功。@return UnLua::FLuaRetValues，require 返回值容器。 */
    UnLua::FLuaRetValues RequireLuaAnimModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim module require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim module must return a table. Module=%s Type=%s"),
                *LuaModuleName,
                UTF8_TO_TCHAR(lua_typename(LuaState, ReturnValues[0].GetType())));
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    /** 作用：调用模块 Configure 或 Initialize 入口。@param LuaEnv UnLua::FLuaEnv*，UnLua 环境。@param ContextObject UObject*，动画实例上下文。@param LuaModuleName const FString&，模块名。@return bool，模块有效且配置调用成功时为 true。 */
    bool TryCallLuaAnimConfigure(UnLua::FLuaEnv* LuaEnv, UObject* ContextObject, const FString& LuaModuleName)
    {
        if (!LuaEnv || !ContextObject || LuaModuleName.IsEmpty()) return false;

        bool bRequireSucceeded = false;
        UnLua::FLuaRetValues RequireReturnValues = RequireLuaAnimModule(LuaEnv, LuaModuleName, bRequireSucceeded);
        if (!bRequireSucceeded) return false;

        UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
        UnLua::FLuaValue ConfigureFunctionValue = ModuleTable[LuaAnimConfigureFunctionName];
        if (ConfigureFunctionValue.GetType() != LUA_TFUNCTION)
        {
            ConfigureFunctionValue = ModuleTable[LuaAnimInitializeFunctionName];
        }

        if (ConfigureFunctionValue.GetType() != LUA_TFUNCTION)
        {
            return true;
        }

        UnLua::FLuaFunction ConfigureFunction(LuaEnv, ConfigureFunctionValue);
        UnLua::FLuaRetValues FunctionReturnValues = ConfigureFunction.Call(ContextObject);
        return FunctionReturnValues.IsValid();
    }

    /** 作用：调用模块 UpdateLayer 或 Update 并解析决策。@param LuaEnv UnLua::FLuaEnv*，UnLua 环境。@param ContextObject UObject*，动画实例。@param LuaModuleName const FString&，模块名。@param LayerName FName，目标层。@param DeltaSeconds float，本帧秒数。@param OutDecision FSekiroLuaAnimDecision&，输出决策。@return bool，Lua 返回有效决策时为 true。 */
    bool TryCallLuaAnimUpdate(UnLua::FLuaEnv* LuaEnv, UObject* ContextObject, const FString& LuaModuleName, FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision)
    {
        if (!LuaEnv || !ContextObject || LuaModuleName.IsEmpty()) return false;

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return false;

        bool bRequireSucceeded = false;
        UnLua::FLuaRetValues RequireReturnValues = RequireLuaAnimModule(LuaEnv, LuaModuleName, bRequireSucceeded);
        if (!bRequireSucceeded) return false;

        UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
        UnLua::FLuaValue LuaFunctionValue = ModuleTable[LuaAnimUpdateLayerFunctionName];
        bool bUseLayerFunction = LuaFunctionValue.GetType() == LUA_TFUNCTION;
        if (!bUseLayerFunction)
        {
            LuaFunctionValue = ModuleTable[LuaAnimUpdateFunctionName];
        }

        if (LuaFunctionValue.GetType() != LUA_TFUNCTION)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim module has no UpdateLayer or Update function. Module=%s"), *LuaModuleName);
            return false;
        }

        const int32 RuntimeContextTableIndex = PushLuaAnimRuntimeContextTable(LuaEnv, ContextObject, LayerName);
        if (RuntimeContextTableIndex == INDEX_NONE) return false;

        UnLua::FLuaTable RuntimeContextTable(LuaEnv, RuntimeContextTableIndex);
        UnLua::FLuaFunction LuaFunction(LuaEnv, LuaFunctionValue);
        UnLua::FLuaRetValues FunctionReturnValues = bUseLayerFunction
            ? LuaFunction.Call(ContextObject, LayerName, DeltaSeconds, RuntimeContextTable)
            : LuaFunction.Call(ContextObject, DeltaSeconds, RuntimeContextTable);

        bool bResult = false;
        if (!FunctionReturnValues.IsValid())
        {
            bResult = false;
        }
        else if (FunctionReturnValues.Num() == 0 || FunctionReturnValues[0].GetType() == LUA_TNIL)
        {
            bResult = false;
        }
        else if (!ReadLuaAnimDecision(LuaEnv, FunctionReturnValues[0], OutDecision))
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim Update returned unsupported value. Module=%s Layer=%s Type=%s"),
                *LuaModuleName,
                *LayerName.ToString(),
                UTF8_TO_TCHAR(lua_typename(LuaState, FunctionReturnValues[0].GetType())));
        }
        else
        {
            bResult = true;
        }

        FunctionReturnValues.Pop();
        lua_remove(LuaState, RuntimeContextTableIndex);
        return bResult;
    }
}

USekiroLuaAnimInstance::USekiroLuaAnimInstance()
{
}

void USekiroLuaAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    LuaAnimModuleName = DefaultLuaAnimModuleName;
    {
        FWriteScopeLock SnapshotLock(LuaAnimSnapshotLock);
        LuaAnimSnapshot = FSekiroLuaAnimSnapshot();
        LuaAnimLayerSnapshots.Empty();
    }
    PendingLuaAnimDecisions.Empty();
    PendingLuaAnimRootMotionRotationPolicies.Empty();
    PendingLuaAnimPreserveNormalizedTime.Empty();
    PublishedLuaPoseGraphLayers.Empty();
    LuaSequencePlayers.Empty();
    LuaSequencePlayerLayers.Empty();
    LuaStateResults.Empty();
    LuaStateResultLayers.Empty();
    LuaPoseGraphTopologySerials.Empty();
    ActiveLuaPoseNodeIds.Empty();
    ActiveLuaPoseGraphGeneration = 0;
    LuaPoseGraphGeneration = LuaPoseGraphGeneration == MAX_int32 ? 1 : LuaPoseGraphGeneration + 1;
    NextLuaPoseNodeId = 0;
    bLuaAnimConfigured = false;
    bLuaAnimModuleNameOverridden = false;
    ConfigureLuaAnimation();
    PublishLuaAnimSnapshotsToProxy();
}

void USekiroLuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    PublishedLuaPoseGraphLayers.Empty();
    if (bAutoUpdateLuaDrivenAnimation)
    {
        UpdateLuaDrivenAnimation(DeltaSeconds);
    }
    PublishLuaAnimSnapshotsToProxy();
}

FAnimInstanceProxy* USekiroLuaAnimInstance::CreateAnimInstanceProxy()
{
    return new FSekiroLuaAnimInstanceProxy(this);
}

void USekiroLuaAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FSekiroLuaAnimInstanceProxy*>(InProxy);
}

void USekiroLuaAnimInstance::DisplayDebugInstance(FDisplayDebugManager& DisplayDebugManager, float& Indent)
{
    Super::DisplayDebugInstance(DisplayDebugManager, Indent);

#if ENABLE_DRAW_DEBUG
    TMap<FName, FSekiroLuaAnimSnapshot> SnapshotCopies;
    {
        FReadScopeLock SnapshotLock(LuaAnimSnapshotLock);
        SnapshotCopies = LuaAnimLayerSnapshots;
    }

    DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.35f, 0.85f, 1.0f));
    DisplayDebugManager.DrawString(FString::Printf(
        TEXT("Sekiro Lua Animation: Module=%s DefaultLayer=%s Layers=%d"),
        *LuaAnimModuleName,
        *DefaultLuaAnimLayerName.ToString(),
        SnapshotCopies.Num()), Indent);

    FIndenter LuaAnimIndent(Indent);
    if (SnapshotCopies.Num() <= 0)
    {
        DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.9f, 0.9f, 0.9f));
        DisplayDebugManager.DrawString(TEXT("No Lua animation snapshot."), Indent);
        return;
    }

    for (TMap<FName, FSekiroLuaAnimSnapshot>::TConstIterator SnapshotIt(SnapshotCopies); SnapshotIt; ++SnapshotIt)
    {
        const FName LayerName = SnapshotIt.Key();
        const FSekiroLuaAnimSnapshot& Snapshot = SnapshotIt.Value();
        const bool bIsTransitioning = Snapshot.PreviousAnimationAsset.Get() && Snapshot.BlendAlpha < 1.0f;
        const float CurrentPoseWeight = bIsTransitioning ? Snapshot.BlendAlpha : 1.0f;
        const float PreviousPoseWeight = bIsTransitioning ? 1.0f - Snapshot.BlendAlpha : 0.0f;
        const FString CurrentAnimName = GetLuaAnimDebugAssetLabel(Snapshot.CurrentAnimationAsset.Get(), Snapshot.CurrentAnimationName);
        const FString PreviousAnimName = GetLuaAnimDebugAssetLabel(Snapshot.PreviousAnimationAsset.Get(), Snapshot.PreviousAnimationName);

        DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.85f, 0.92f, 1.0f));
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("Layer %s  State=%s  Previous=%s  HasPose=%s"),
            *LayerName.ToString(),
            *Snapshot.CurrentStateName.ToString(),
            *Snapshot.PreviousStateName.ToString(),
            Snapshot.bHasPose ? TEXT("true") : TEXT("false")), Indent);

        FIndenter LayerIndent(Indent);
        DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.9f, 0.9f, 0.9f));
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("Anim=%s  Weight=%.1f%%  PrevAnim=%s  PrevWeight=%.1f%%  Time=%.2f  Rate=%.2f  Loop=%s"),
            *CurrentAnimName,
            CurrentPoseWeight * 100.0f,
            *PreviousAnimName,
            PreviousPoseWeight * 100.0f,
            Snapshot.CurrentTime,
            Snapshot.CurrentPlayRate,
            Snapshot.bCurrentLoop ? TEXT("true") : TEXT("false")), Indent);
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("BlendAlpha=%.2f  BlendTime=%.2f  BlendElapsed=%.2f  BlendInput=%s"),
            Snapshot.BlendAlpha,
            Snapshot.BlendTime,
            Snapshot.BlendElapsedTime,
            *Snapshot.CurrentBlendInput.ToCompactString()), Indent);
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("RootRotation Current=%s TargetWorldYaw=%.1f MaxRate=%.1f CompletionTime=%.2f  Previous=%s TargetWorldYaw=%.1f MaxRate=%.1f CompletionTime=%.2f"),
            *StaticEnum<ESekiroLuaRootMotionRotationMode>()->GetNameStringByValue(static_cast<int64>(Snapshot.CurrentRootMotionRotationMode)),
            Snapshot.CurrentRootMotionTargetWorldYaw,
            Snapshot.CurrentRootMotionMaxYawRate,
            Snapshot.CurrentRootMotionCompletionTimeSeconds,
            *StaticEnum<ESekiroLuaRootMotionRotationMode>()->GetNameStringByValue(static_cast<int64>(Snapshot.PreviousRootMotionRotationMode)),
            Snapshot.PreviousRootMotionTargetWorldYaw,
            Snapshot.PreviousRootMotionMaxYawRate,
            Snapshot.PreviousRootMotionCompletionTimeSeconds), Indent);
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("OrientationWarping Enabled=%s Angle=%.1f Alpha=%.2f WarpTranslation=%s TranslationAngle=%.1f"),
            Snapshot.OrientationWarpingPolicy.bEnabled ? TEXT("true") : TEXT("false"),
            Snapshot.OrientationWarpingPolicy.OrientationAngle,
            Snapshot.OrientationWarpingPolicy.WarpingAlpha,
            Snapshot.OrientationWarpingPolicy.bWarpRootMotionTranslation ? TEXT("true") : TEXT("false"),
            Snapshot.OrientationWarpingPolicy.RootMotionTranslationAngle), Indent);

        const FString CurrentBlendSampleText = GetLuaAnimDebugBlendSampleText(Snapshot.CurrentAnimationAsset.Get(), Snapshot.CurrentBlendInput);
        if (!CurrentBlendSampleText.IsEmpty())
        {
            DisplayDebugManager.DrawString(FString::Printf(TEXT("Current %s"), *CurrentBlendSampleText), Indent);
        }

        USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
        const AActor* MeshOwner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
        const float ActorYaw = MeshOwner ? MeshOwner->GetActorRotation().Yaw : 0.0f;
        const float MeshYaw = MeshComponent ? MeshComponent->GetComponentRotation().Yaw : 0.0f;
        const float PreviewDeltaTime = (1.0f / 30.0f) * FMath::Abs(GetSafeLuaPlayRate(Snapshot.CurrentPlayRate));
        const FTransform LocalRootMotionPreview = ExtractSekiroLuaRootMotionPreview(
            Snapshot.CurrentAnimationAsset.Get(),
            Snapshot.CurrentTime,
            PreviewDeltaTime,
            Snapshot.bCurrentLoop,
            Snapshot.CurrentBlendInput);
        const FTransform WorldRootMotionPreview = ConvertSekiroLuaRootMotionPreviewToWorld(MeshComponent, LocalRootMotionPreview);
        DisplayDebugManager.DrawString(FString::Printf(
            TEXT("RootMotionPreview ActorYaw=%.1f MeshYaw=%.1f LocalDelta=%s WorldDelta=%s"),
            ActorYaw,
            MeshYaw,
            *LocalRootMotionPreview.GetTranslation().ToCompactString(),
            *WorldRootMotionPreview.GetTranslation().ToCompactString()), Indent);

        const FString PreviousBlendSampleText = GetLuaAnimDebugBlendSampleText(Snapshot.PreviousAnimationAsset.Get(), Snapshot.PreviousBlendInput);
        if (!PreviousBlendSampleText.IsEmpty())
        {
            DisplayDebugManager.DrawString(FString::Printf(TEXT("Previous %s"), *PreviousBlendSampleText), Indent);
        }
    }
#endif
}

bool USekiroLuaAnimInstance::UpdateLuaDrivenAnimation(float DeltaSeconds)
{
    ConfigureLuaAnimation();
    PublishedLuaPoseGraphLayers.Empty();

    bool bHasAnyDecision = false;
    if (LuaAnimLayerNames.Num() > 0)
    {
        for (const FName& LayerName : LuaAnimLayerNames)
        {
            bHasAnyDecision |= UpdateLuaDrivenAnimationLayer(LayerName, DeltaSeconds);
        }
    }
    else
    {
        bHasAnyDecision = UpdateLuaDrivenAnimationLayer(DefaultLuaAnimLayerName, DeltaSeconds);
    }

    PublishLuaAnimSnapshotsToProxy();
    return bHasAnyDecision;
}

bool USekiroLuaAnimInstance::UpdateLuaDrivenAnimationLayer(FName LayerName, float DeltaSeconds)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    FSekiroLuaAnimDecision Decision;
    const bool bHasDecision = EvaluateLuaAnimDecision(ResolvedLayerName, DeltaSeconds, Decision);
    bool bAppliedDecision = false;
    if (bHasDecision)
    {
        bAppliedDecision = ApplyLuaAnimDecision(ResolvedLayerName, Decision);
    }

    const bool bPublishedPoseGraph = PublishedLuaPoseGraphLayers.Contains(ResolvedLayerName);
    AdvanceLuaAnimSnapshot(ResolvedLayerName, DeltaSeconds);
    PublishLuaAnimSnapshotsToProxy();
    return bAppliedDecision || bPublishedPoseGraph;
}

void USekiroLuaAnimInstance::SetLuaAnimModuleName(const FString& ModuleName)
{
    if (bLuaAnimModuleNameOverridden && LuaAnimModuleName == ModuleName) return;

    LuaAnimModuleName = ModuleName;
    bLuaAnimModuleNameOverridden = true;
    bLuaAnimConfigured = false;
    LuaAnimLayerNames.Empty();
}

FString USekiroLuaAnimInstance::GetLuaAnimModuleName() const
{
    return ResolveLuaAnimModuleName();
}

FString USekiroLuaAnimInstance::GetModuleName_Implementation() const
{
    if (bLuaAnimModuleNameOverridden && !LuaAnimModuleName.IsEmpty()) return LuaAnimModuleName;
    if (!DefaultLuaAnimModuleName.IsEmpty()) return DefaultLuaAnimModuleName;

    return LuaAnimModuleName;
}

void USekiroLuaAnimInstance::SetDefaultLuaAnimLayerName(FName LayerName)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    FWriteScopeLock SnapshotLock(LuaAnimSnapshotLock);
    DefaultLuaAnimLayerName = ResolvedLayerName;
}

void USekiroLuaAnimInstance::RegisterLuaAnimLayer(FName LayerName)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    LuaAnimLayerNames.AddUnique(ResolvedLayerName);
}

void USekiroLuaAnimInstance::ClearLuaAnimLayers()
{
    LuaAnimLayerNames.Empty();
    PendingLuaAnimDecisions.Empty();
    PendingLuaAnimRootMotionRotationPolicies.Empty();
    PendingLuaAnimPreserveNormalizedTime.Empty();
    PublishedLuaPoseGraphLayers.Empty();
    LuaSequencePlayers.Empty();
    LuaSequencePlayerLayers.Empty();
    LuaStateResults.Empty();
    LuaStateResultLayers.Empty();
    LuaPoseGraphTopologySerials.Empty();
    ActiveLuaPoseNodeIds.Empty();
    ActiveLuaPoseGraphGeneration = 0;
    LuaPoseGraphGeneration = LuaPoseGraphGeneration == MAX_int32 ? 1 : LuaPoseGraphGeneration + 1;
    NextLuaPoseNodeId = 0;
    {
        FWriteScopeLock SnapshotLock(LuaAnimSnapshotLock);
        LuaAnimLayerSnapshots.Empty();
        LuaAnimSnapshot = FSekiroLuaAnimSnapshot();
    }
}

TArray<FName> USekiroLuaAnimInstance::GetLuaAnimLayerNames() const
{
    return LuaAnimLayerNames;
}

UAnimationAsset* USekiroLuaAnimInstance::LoadLuaAnimationAsset(const FString& AssetPath)
{
    if (!IsInGameThread())
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Error, TEXT("Lua animation assets may only be loaded on the game thread. Path=%s"), *AssetPath);
        return nullptr;
    }

    const FString TrimmedAssetPath = AssetPath.TrimStartAndEnd();
    if (TrimmedAssetPath.IsEmpty()) return nullptr;

    TObjectPtr<UAnimationAsset>* CachedAnimationAsset = LuaAnimAssetCache.Find(TrimmedAssetPath);
    if (CachedAnimationAsset)
    {
        return CachedAnimationAsset->Get();
    }

    UObject* LoadedObject = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *TrimmedAssetPath);
    UAnimationAsset* LoadedAnimationAsset = Cast<UAnimationAsset>(LoadedObject);
    if (!LoadedAnimationAsset)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim asset load failed. Path=%s"), *TrimmedAssetPath);
        return nullptr;
    }

    LuaAnimAssetCache.Add(TrimmedAssetPath, LoadedAnimationAsset);
    return LoadedAnimationAsset;
}

FSekiroLuaPoseLink USekiroLuaAnimInstance::CreateLuaSequencePlayer(FName LayerName, FName NodeName)
{
    FSekiroLuaPoseLink InvalidPoseLink;
    if (!IsInGameThread()) return InvalidPoseLink;

    if (NodeName.IsNone())
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua SequencePlayer requires a non-empty node name."));
        return InvalidPoseLink;
    }

    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    for (TMap<int32, FSekiroLuaSequencePlayerSnapshot>::TConstIterator PlayerIt(LuaSequencePlayers); PlayerIt; ++PlayerIt)
    {
        const FName* RegisteredLayerName = LuaSequencePlayerLayers.Find(PlayerIt.Key());
        if (RegisteredLayerName && *RegisteredLayerName == ResolvedLayerName && PlayerIt.Value().NodeName == NodeName)
        {
            return PlayerIt.Value().PoseLink;
        }
    }

    FSekiroLuaSequencePlayerSnapshot SequencePlayer;
    SequencePlayer.PoseLink.NodeId = NextLuaPoseNodeId++;
    SequencePlayer.PoseLink.Generation = LuaPoseGraphGeneration;
    SequencePlayer.NodeName = NodeName;
    LuaSequencePlayers.Add(SequencePlayer.PoseLink.NodeId, SequencePlayer);
    LuaSequencePlayerLayers.Add(SequencePlayer.PoseLink.NodeId, ResolvedLayerName);
    int32& TopologySerial = LuaPoseGraphTopologySerials.FindOrAdd(ResolvedLayerName);
    TopologySerial = TopologySerial == MAX_int32 ? 1 : TopologySerial + 1;
    return SequencePlayer.PoseLink;
}

FSekiroLuaPoseLink USekiroLuaAnimInstance::CreateLuaStateResult(FName LayerName, FName NodeName, FName StateName)
{
    FSekiroLuaPoseLink InvalidPoseLink;
    if (!IsInGameThread()) return InvalidPoseLink;

    if (NodeName.IsNone() || StateName.IsNone())
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning,
            TEXT("Lua StateResult requires non-empty node and state names. Node=%s State=%s"),
            *NodeName.ToString(),
            *StateName.ToString());
        return InvalidPoseLink;
    }

    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    for (TMap<int32, FSekiroLuaStateResultSnapshot>::TIterator StateResultIt(LuaStateResults); StateResultIt; ++StateResultIt)
    {
        const FName* RegisteredLayerName = LuaStateResultLayers.Find(StateResultIt.Key());
        if (RegisteredLayerName && *RegisteredLayerName == ResolvedLayerName && StateResultIt.Value().NodeName == NodeName)
        {
            if (StateResultIt.Value().StateName != StateName)
            {
                StateResultIt.Value().StateName = StateName;
                int32& TopologySerial = LuaPoseGraphTopologySerials.FindOrAdd(ResolvedLayerName);
                TopologySerial = TopologySerial == MAX_int32 ? 1 : TopologySerial + 1;
            }
            return StateResultIt.Value().PoseLink;
        }
    }

    FSekiroLuaStateResultSnapshot StateResult;
    StateResult.PoseLink.NodeId = NextLuaPoseNodeId++;
    StateResult.PoseLink.Generation = LuaPoseGraphGeneration;
    StateResult.NodeName = NodeName;
    StateResult.StateName = StateName;
    LuaStateResults.Add(StateResult.PoseLink.NodeId, StateResult);
    LuaStateResultLayers.Add(StateResult.PoseLink.NodeId, ResolvedLayerName);
    int32& TopologySerial = LuaPoseGraphTopologySerials.FindOrAdd(ResolvedLayerName);
    TopologySerial = TopologySerial == MAX_int32 ? 1 : TopologySerial + 1;
    return StateResult.PoseLink;
}

bool USekiroLuaAnimInstance::SetLuaStateResultInput(
    FSekiroLuaPoseLink StateResultPoseLink,
    FSekiroLuaPoseLink InputPoseLink)
{
    if (!IsInGameThread()) return false;

    FSekiroLuaStateResultSnapshot* StateResult = FindLuaStateResult(StateResultPoseLink);
    FSekiroLuaSequencePlayerSnapshot* SequencePlayer = FindLuaSequencePlayer(InputPoseLink);
    const FName* StateResultLayerName = LuaStateResultLayers.Find(StateResultPoseLink.NodeId);
    const FName* SequencePlayerLayerName = LuaSequencePlayerLayers.Find(InputPoseLink.NodeId);
    if (!StateResult || !SequencePlayer)
    {
        const bool bInputIsStateResult = FindLuaStateResult(InputPoseLink) != nullptr;
        UE_LOG(LogSekiroAnimBlueprintExt, Warning,
            TEXT("Lua StateResult input rejected. StateResultNodeId=%d InputNodeId=%d Reason=%s"),
            StateResultPoseLink.NodeId,
            InputPoseLink.NodeId,
            bInputIsStateResult ? TEXT("StateResult input type is not supported in phase one") : TEXT("invalid input PoseLink"));
        return false;
    }

    if (!StateResultLayerName || !SequencePlayerLayerName || *StateResultLayerName != *SequencePlayerLayerName)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning,
            TEXT("Lua StateResult input rejected because nodes belong to different layers. StateResultNodeId=%d InputNodeId=%d"),
            StateResultPoseLink.NodeId,
            InputPoseLink.NodeId);
        return false;
    }

    if (StateResult->InputNode.PoseLink != SequencePlayer->PoseLink
        || StateResult->InputNode.NodeType != ESekiroLuaPoseNodeType::SequencePlayer)
    {
        StateResult->InputNode.PoseLink = SequencePlayer->PoseLink;
        StateResult->InputNode.NodeType = ESekiroLuaPoseNodeType::SequencePlayer;
        int32& TopologySerial = LuaPoseGraphTopologySerials.FindOrAdd(*StateResultLayerName);
        TopologySerial = TopologySerial == MAX_int32 ? 1 : TopologySerial + 1;
    }
    return true;
}

bool USekiroLuaAnimInstance::IsLuaPoseLinkValid(FSekiroLuaPoseLink PoseLink) const
{
    return FindLuaSequencePlayer(PoseLink) != nullptr || FindLuaStateResult(PoseLink) != nullptr;
}

bool USekiroLuaAnimInstance::IsLuaPoseLinkActive(FSekiroLuaPoseLink PoseLink) const
{
    if (!IsInGameThread()
        || !PoseLink.IsValid()
        || PoseLink.Generation != ActiveLuaPoseGraphGeneration)
    {
        return false;
    }
    return ActiveLuaPoseNodeIds.Contains(PoseLink.NodeId);
}

bool USekiroLuaAnimInstance::SetLuaSequencePlayerAsset(
    FSekiroLuaPoseLink PoseLink,
    UAnimSequenceBase* Sequence,
    FName AnimationName,
    bool bResetTime,
    float StartPosition)
{
    if (!IsInGameThread()) return false;

    FSekiroLuaSequencePlayerSnapshot* SequencePlayer = FindLuaSequencePlayer(PoseLink);
    if (!SequencePlayer || !Sequence) return false;

    const bool bAssetChanged = SequencePlayer->Sequence.Get() != Sequence;
    SequencePlayer->Sequence = Sequence;
    SequencePlayer->AnimationName = AnimationName;
    if (bResetTime || bAssetChanged)
    {
        SequencePlayer->PlaybackTargetTime = FMath::Clamp(StartPosition, 0.0f, 1.0f) * Sequence->GetPlayLength();
        SequencePlayer->CurrentTime = SequencePlayer->PlaybackTargetTime;
        SequencePlayer->PlaybackResetSerial = SequencePlayer->PlaybackResetSerial == MAX_int32
            ? 1
            : SequencePlayer->PlaybackResetSerial + 1;
    }
    else
    {
        SequencePlayer->CurrentTime = FMath::Clamp(SequencePlayer->CurrentTime, 0.0f, Sequence->GetPlayLength());
    }

    return true;
}

bool USekiroLuaAnimInstance::SetLuaSequencePlayerAssetByPath(
    FSekiroLuaPoseLink PoseLink,
    const FString& AnimationPath,
    FName AnimationName,
    bool bResetTime,
    float StartPosition)
{
    UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(LoadLuaAnimationAsset(AnimationPath));
    if (!Sequence)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning,
            TEXT("Lua SequencePlayer path is not an AnimSequenceBase. NodeId=%d Path=%s"),
            PoseLink.NodeId,
            *AnimationPath);
        return false;
    }

    return SetLuaSequencePlayerAsset(PoseLink, Sequence, AnimationName, bResetTime, StartPosition);
}

bool USekiroLuaAnimInstance::SetLuaSequencePlayerParameters(FSekiroLuaPoseLink PoseLink, float PlayRate, bool bLoop)
{
    if (!IsInGameThread()) return false;

    FSekiroLuaSequencePlayerSnapshot* SequencePlayer = FindLuaSequencePlayer(PoseLink);
    if (!SequencePlayer) return false;

    SequencePlayer->PlayRate = GetSafeLuaPlayRate(PlayRate);
    SequencePlayer->bLoop = bLoop;
    return true;
}

bool USekiroLuaAnimInstance::PublishLuaOutputPose(FName LayerName, FSekiroLuaPoseLink PoseLink, float TransitionTime)
{
    if (!IsInGameThread()) return false;

    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    const FSekiroLuaStateResultSnapshot* StateResult = FindLuaStateResult(PoseLink);
    const FSekiroLuaSequencePlayerSnapshot* SequencePlayer = FindLuaStateResultInputSequencePlayer(PoseLink);
    const FName* RegisteredLayerName = LuaStateResultLayers.Find(PoseLink.NodeId);
    if (!StateResult || !SequencePlayer || !SequencePlayer->Sequence.Get() || !RegisteredLayerName || *RegisteredLayerName != ResolvedLayerName)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning,
            TEXT("Lua OutputPose rejected an invalid or cross-layer PoseLink. Layer=%s NodeId=%d Generation=%d"),
            *ResolvedLayerName.ToString(),
            PoseLink.NodeId,
            PoseLink.Generation);
        return false;
    }

    FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(ResolvedLayerName);
    FSekiroLuaRootMotionRotationPolicy RootMotionPolicy;
    RootMotionPolicy.Mode = Snapshot.CurrentRootMotionRotationMode;
    RootMotionPolicy.TargetWorldYaw = Snapshot.CurrentRootMotionTargetWorldYaw;
    RootMotionPolicy.MaxYawRate = Snapshot.CurrentRootMotionMaxYawRate;
    RootMotionPolicy.CompletionTimeSeconds = Snapshot.CurrentRootMotionCompletionTimeSeconds;
    PendingLuaAnimRootMotionRotationPolicies.RemoveAndCopyValue(ResolvedLayerName, RootMotionPolicy);
    const bool bOutputChanged = !Snapshot.PoseGraph.bHasOutputPose || Snapshot.PoseGraph.CurrentPose != PoseLink;
    if (bOutputChanged)
    {
        Snapshot.PoseGraph.OutputSerial = Snapshot.PoseGraph.OutputSerial == MAX_int32
            ? 1
            : Snapshot.PoseGraph.OutputSerial + 1;
        Snapshot.PoseGraph.PreviousPose = Snapshot.PoseGraph.CurrentPose;
        Snapshot.PoseGraph.CurrentPose = PoseLink;
        Snapshot.PoseGraph.TransitionTime = Snapshot.PoseGraph.PreviousPose.IsValid()
            ? FMath::Max(0.0f, TransitionTime)
            : 0.0f;
        Snapshot.PoseGraph.TransitionElapsedTime = 0.0f;
        Snapshot.PoseGraph.TransitionAlpha = Snapshot.PoseGraph.TransitionTime > UE_KINDA_SMALL_NUMBER ? 0.0f : 1.0f;

        Snapshot.PreviousStateName = Snapshot.CurrentStateName;
        Snapshot.PreviousAnimationAsset = Snapshot.CurrentAnimationAsset;
        Snapshot.PreviousAnimationName = Snapshot.CurrentAnimationName;
        Snapshot.PreviousTime = Snapshot.CurrentTime;
        Snapshot.PreviousPlayRate = Snapshot.CurrentPlayRate;
        Snapshot.PreviousRootMotionRotationMode = Snapshot.CurrentRootMotionRotationMode;
        Snapshot.PreviousRootMotionTargetWorldYaw = Snapshot.CurrentRootMotionTargetWorldYaw;
        Snapshot.PreviousRootMotionMaxYawRate = Snapshot.CurrentRootMotionMaxYawRate;
        Snapshot.PreviousRootMotionCompletionTimeSeconds = Snapshot.CurrentRootMotionCompletionTimeSeconds;
        Snapshot.bPreviousLoop = Snapshot.bCurrentLoop;

    }

    Snapshot.CurrentRootMotionRotationMode = RootMotionPolicy.Mode;
    Snapshot.CurrentRootMotionTargetWorldYaw = RootMotionPolicy.TargetWorldYaw;
    Snapshot.CurrentRootMotionMaxYawRate = RootMotionPolicy.MaxYawRate;
    Snapshot.CurrentRootMotionCompletionTimeSeconds = RootMotionPolicy.CompletionTimeSeconds;

    Snapshot.PoseGraph.bHasOutputPose = true;
    Snapshot.PoseGraph.Generation = LuaPoseGraphGeneration;
    Snapshot.bUseInertialization = false;
    Snapshot.InertialBlendTime = 0.0f;
    BuildLuaPoseGraphSnapshot(ResolvedLayerName, Snapshot);
    PublishLuaAnimSnapshot(ResolvedLayerName, Snapshot);
    PublishedLuaPoseGraphLayers.Add(ResolvedLayerName);
    return true;
}

bool USekiroLuaAnimInstance::SubmitLuaAnimPose(FName LayerName, const FSekiroLuaAnimDecision& Decision)
{
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimPose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    FSekiroLuaAnimDecision Decision;
    Decision.StateName = StateName;
    Decision.AnimationAsset = AnimationAsset;
    Decision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
    Decision.BlendTime = BlendTime;
    Decision.PlayRate = PlayRate;
    Decision.bLoop = bLoop;
    Decision.bResetTime = bResetTime;
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimPoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    FSekiroLuaAnimDecision Decision;
    Decision.StateName = StateName;
    Decision.AnimationPath = AnimationPath;
    Decision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
    Decision.BlendTime = BlendTime;
    Decision.PlayRate = PlayRate;
    Decision.bLoop = bLoop;
    Decision.bResetTime = bResetTime;
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimPoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    FSekiroLuaAnimDecision Decision;
    Decision.StateName = StateName;
    Decision.AnimationName = AnimationName;
    Decision.AnimationPath = AnimationPath;
    Decision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
    Decision.BlendTime = BlendTime;
    Decision.PlayRate = PlayRate;
    Decision.bLoop = bLoop;
    Decision.bResetTime = bResetTime;
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimPoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition)
{
    FSekiroLuaAnimDecision Decision;
    Decision.StateName = StateName;
    Decision.AnimationName = AnimationName;
    Decision.AnimationPath = AnimationPath;
    Decision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
    Decision.BlendTime = BlendTime;
    Decision.PlayRate = PlayRate;
    Decision.bLoop = bLoop;
    Decision.bResetTime = bResetTime;
    Decision.StartPosition = FMath::Clamp(StartPosition, 0.0f, 1.0f);
    Decision.bUseStartPosition = true;
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimPoseByPathWithNameAndInertialization(
    FName LayerName,
    FName StateName,
    FName AnimationName,
    const FString& AnimationPath,
    float BlendInputX,
    float BlendInputY,
    float BlendInputZ,
    float PlayRate,
    bool bLoop,
    bool bResetTime,
    bool bUseStartPosition,
    float StartPosition,
    float InertialBlendTime)
{
    FSekiroLuaAnimDecision Decision;
    Decision.StateName = StateName;
    Decision.AnimationName = AnimationName;
    Decision.AnimationPath = AnimationPath;
    Decision.BlendInput = FVector(BlendInputX, BlendInputY, BlendInputZ);
    Decision.BlendTime = 0.0f;
    Decision.PlayRate = PlayRate;
    Decision.bLoop = bLoop;
    Decision.bResetTime = bResetTime;
    Decision.bUseStartPosition = bUseStartPosition;
    Decision.StartPosition = FMath::Clamp(StartPosition, 0.0f, 1.0f);
    Decision.bUseInertialization = true;
    Decision.InertialBlendTime = FMath::Max(0.0f, InertialBlendTime);
    return QueueLuaAnimPose(LayerName, Decision);
}

bool USekiroLuaAnimInstance::SetLuaAnimSequencePose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPose(LayerName, StateName, AnimationAsset, 0.0f, 0.0f, 0.0f, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimSequencePoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPoseByPath(LayerName, StateName, AnimationPath, 0.0f, 0.0f, 0.0f, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimSequencePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPoseByPathWithName(LayerName, StateName, AnimationName, AnimationPath, 0.0f, 0.0f, 0.0f, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimSequencePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition)
{
    return SetLuaAnimPoseByPathWithNameAndStartPosition(LayerName, StateName, AnimationName, AnimationPath, 0.0f, 0.0f, 0.0f, BlendTime, PlayRate, bLoop, bResetTime, StartPosition);
}

bool USekiroLuaAnimInstance::SetLuaAnimBlendSpacePose(FName LayerName, FName StateName, UAnimationAsset* BlendSpaceAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPose(LayerName, StateName, BlendSpaceAsset, BlendInputX, BlendInputY, BlendInputZ, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimBlendSpacePoseByPath(FName LayerName, FName StateName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPoseByPath(LayerName, StateName, BlendSpacePath, BlendInputX, BlendInputY, BlendInputZ, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimBlendSpacePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime)
{
    return SetLuaAnimPoseByPathWithName(LayerName, StateName, AnimationName, BlendSpacePath, BlendInputX, BlendInputY, BlendInputZ, BlendTime, PlayRate, bLoop, bResetTime);
}

bool USekiroLuaAnimInstance::SetLuaAnimBlendSpacePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition)
{
    return SetLuaAnimPoseByPathWithNameAndStartPosition(LayerName, StateName, AnimationName, BlendSpacePath, BlendInputX, BlendInputY, BlendInputZ, BlendTime, PlayRate, bLoop, bResetTime, StartPosition);
}

float USekiroLuaAnimInstance::GetLuaAnimNumberProperty(const FString& PropertyName, float DefaultValue) const
{
    const FProperty* Property = FindLuaAnimProperty(this, PropertyName);
    if (!Property) return DefaultValue;

    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(this);
    return GetLuaAnimNumberValue(Property, ValuePtr, DefaultValue);
}

bool USekiroLuaAnimInstance::GetLuaAnimBoolProperty(const FString& PropertyName, bool bDefaultValue) const
{
    const FProperty* Property = FindLuaAnimProperty(this, PropertyName);
    if (!Property) return bDefaultValue;

    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(this);
    if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        return BoolProperty->GetPropertyValue(ValuePtr);
    }

    if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        return !FMath::IsNearlyZero(GetLuaAnimNumberValue(NumericProperty, ValuePtr, 0.0f));
    }

    return bDefaultValue;
}

FString USekiroLuaAnimInstance::GetLuaAnimPropertyText(const FString& PropertyName, const FString& DefaultValue) const
{
    const FProperty* Property = FindLuaAnimProperty(this, PropertyName);
    if (!Property) return DefaultValue;

    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(this);
    return GetLuaAnimPropertyTextValue(Property, ValuePtr, DefaultValue);
}

FSekiroLuaAnimSnapshot USekiroLuaAnimInstance::GetLuaAnimSnapshot() const
{
    FReadScopeLock SnapshotLock(LuaAnimSnapshotLock);
    return LuaAnimSnapshot;
}

FSekiroLuaAnimSnapshot USekiroLuaAnimInstance::GetLuaAnimLayerSnapshot(FName LayerName) const
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    FReadScopeLock SnapshotLock(LuaAnimSnapshotLock);
    const FSekiroLuaAnimSnapshot* FoundSnapshot = LuaAnimLayerSnapshots.Find(ResolvedLayerName);
    if (FoundSnapshot)
    {
        return *FoundSnapshot;
    }

    return FSekiroLuaAnimSnapshot();
}

void USekiroLuaAnimInstance::SetLuaAnimOrientationWarpingPolicyByName(
    FName LayerName,
    bool bEnabled,
    float OrientationAngle,
    float WarpingAlpha,
    bool bWarpRootMotionTranslation,
    float RootMotionTranslationAngle)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    FSekiroLuaOrientationWarpingPolicy Policy;
    Policy.bEnabled = bEnabled;
    Policy.OrientationAngle = FRotator::NormalizeAxis(
        FMath::IsFinite(OrientationAngle) ? OrientationAngle : 0.0f);
    Policy.WarpingAlpha = FMath::Clamp(
        FMath::IsFinite(WarpingAlpha) ? WarpingAlpha : 0.0f,
        0.0f,
        1.0f);
    Policy.bWarpRootMotionTranslation = bWarpRootMotionTranslation;
    Policy.RootMotionTranslationAngle = FRotator::NormalizeAxis(
        FMath::IsFinite(RootMotionTranslationAngle) ? RootMotionTranslationAngle : 0.0f);

    FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(ResolvedLayerName);
    Snapshot.OrientationWarpingPolicy = Policy;
    PublishLuaAnimSnapshot(ResolvedLayerName, Snapshot);
}

FSekiroLuaOrientationWarpingPolicy USekiroLuaAnimInstance::GetLuaAnimOrientationWarpingPolicySnapshot(FName LayerName) const
{
    return GetLuaAnimLayerSnapshot(LayerName).OrientationWarpingPolicy;
}

bool USekiroLuaAnimInstance::SetLuaAnimRootMotionRotationModeByName(FName LayerName, FName ModeName)
{
    return SetLuaAnimRootMotionRotationPolicyByName(LayerName, ModeName, 0.0f, 0.0f);
}

bool USekiroLuaAnimInstance::SetLuaAnimRootMotionRotationPolicyByName(
    FName LayerName,
    FName ModeName,
    float TargetWorldYaw,
    float MaxYawRate,
    float CompletionTimeSeconds)
{
    ESekiroLuaRootMotionRotationMode Mode = ESekiroLuaRootMotionRotationMode::Extract;
    if (!TryResolveLuaRootMotionRotationMode(ModeName, Mode))
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Unknown Lua Anim root motion rotation mode. Layer=%s Mode=%s"),
            *ResolveLuaAnimLayerName(LayerName).ToString(),
            *ModeName.ToString());
        return false;
    }

    FSekiroLuaRootMotionRotationPolicy Policy;
    Policy.Mode = Mode;
    Policy.TargetWorldYaw = FRotator::NormalizeAxis(FMath::IsFinite(TargetWorldYaw) ? TargetWorldYaw : 0.0f);
    Policy.MaxYawRate = FMath::Max(0.0f, FMath::IsFinite(MaxYawRate) ? MaxYawRate : 0.0f);
    Policy.CompletionTimeSeconds = FMath::IsFinite(CompletionTimeSeconds) ? CompletionTimeSeconds : 0.0f;
    PendingLuaAnimRootMotionRotationPolicies.Add(ResolveLuaAnimLayerName(LayerName), Policy);
    return true;
}

void USekiroLuaAnimInstance::SetLuaAnimPreserveNormalizedTimeOnAssetChange(
    FName LayerName,
    bool bPreserveNormalizedTime)
{
    PendingLuaAnimPreserveNormalizedTime.Add(
        ResolveLuaAnimLayerName(LayerName),
        bPreserveNormalizedTime);
}

ESekiroLuaRootMotionRotationMode USekiroLuaAnimInstance::GetLuaAnimRootMotionRotationMode(FName LayerName) const
{
    return GetLuaAnimLayerSnapshot(LayerName).CurrentRootMotionRotationMode;
}

FName USekiroLuaAnimInstance::GetLuaAnimRootMotionRotationModeName(FName LayerName) const
{
    const ESekiroLuaRootMotionRotationMode Mode = GetLuaAnimRootMotionRotationMode(LayerName);
    return FName(*StaticEnum<ESekiroLuaRootMotionRotationMode>()->GetNameStringByValue(static_cast<int64>(Mode)));
}

float USekiroLuaAnimInstance::GetLuaAnimRootMotionTargetWorldYaw(FName LayerName) const
{
    return GetLuaAnimLayerSnapshot(LayerName).CurrentRootMotionTargetWorldYaw;
}

float USekiroLuaAnimInstance::GetLuaAnimRootMotionMaxYawRate(FName LayerName) const
{
    return GetLuaAnimLayerSnapshot(LayerName).CurrentRootMotionMaxYawRate;
}

float USekiroLuaAnimInstance::GetLuaAnimRootMotionCompletionTimeSeconds(FName LayerName) const
{
    return GetLuaAnimLayerSnapshot(LayerName).CurrentRootMotionCompletionTimeSeconds;
}

float USekiroLuaAnimInstance::GetLuaAnimCurveValue(FName LayerName, FName CurveName, float DefaultValue) const
{
    const FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    return EvaluateSekiroLuaAnimationCurveValue(
        Snapshot.CurrentAnimationAsset.Get(),
        CurveName,
        Snapshot.CurrentTime,
        Snapshot.CurrentBlendInput,
        DefaultValue);
}

float USekiroLuaAnimInstance::GetLuaAnimCircularCurveValue(FName LayerName, FName CurveName, float DefaultValue) const
{
    const FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    return EvaluateSekiroLuaAnimationCircularCurveValue(
        Snapshot.CurrentAnimationAsset.Get(),
        CurveName,
        Snapshot.CurrentTime,
        Snapshot.CurrentBlendInput,
        DefaultValue);
}

float USekiroLuaAnimInstance::FindLuaAnimCircularCurveMatchingNormalizedTimeByPath(
    const FString& AnimationPath,
    FName CurveName,
    float TargetValue,
    float BlendInputX,
    float BlendInputY,
    float BlendInputZ,
    float ReferenceNormalizedTime,
    int32 SampleCount,
    float DefaultValue)
{
    UAnimationAsset* AnimationAsset = LoadLuaAnimationAsset(AnimationPath);
    if (!AnimationAsset || CurveName.IsNone()) return DefaultValue;

    const FVector BlendInput = GetSafeLuaBlendInput(FVector(BlendInputX, BlendInputY, BlendInputZ));
    const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, BlendInput);
    if (PlayLength <= UE_KINDA_SMALL_NUMBER) return DefaultValue;

    const float WrappedTargetValue = WrapSekiroLuaCircularCurveValue(TargetValue);
    const float WrappedReferenceTime = WrapSekiroLuaCircularCurveValue(ReferenceNormalizedTime);
    const int32 ResolvedSampleCount = FMath::Clamp(SampleCount, 16, 512);
    float BestNormalizedTime = DefaultValue;
    float BestCurveDistance = TNumericLimits<float>::Max();
    float BestReferenceDistance = TNumericLimits<float>::Max();
    bool bFoundCurve = false;
    for (int32 SampleIndex = 0; SampleIndex < ResolvedSampleCount; ++SampleIndex)
    {
        const float NormalizedTime = static_cast<float>(SampleIndex) / static_cast<float>(ResolvedSampleCount);
        const float CurveValue = EvaluateSekiroLuaAnimationCircularCurveValue(
            AnimationAsset,
            CurveName,
            NormalizedTime * PlayLength,
            BlendInput,
            std::numeric_limits<float>::quiet_NaN());
        if (!FMath::IsFinite(CurveValue)) continue;

        const float CurveDistance = GetSekiroLuaCircularCurveDistance(CurveValue, WrappedTargetValue);
        const float ReferenceDistance = GetSekiroLuaCircularCurveDistance(NormalizedTime, WrappedReferenceTime);
        if (!bFoundCurve
            || CurveDistance < BestCurveDistance - UE_KINDA_SMALL_NUMBER
            || (FMath::IsNearlyEqual(CurveDistance, BestCurveDistance) && ReferenceDistance < BestReferenceDistance))
        {
            BestNormalizedTime = NormalizedTime;
            BestCurveDistance = CurveDistance;
            BestReferenceDistance = ReferenceDistance;
            bFoundCurve = true;
        }
    }

    return bFoundCurve ? BestNormalizedTime : DefaultValue;
}

int32 USekiroLuaAnimInstance::GetLuaAnimCurveIntValue(FName LayerName, FName CurveName, int32 DefaultValue) const
{
    const FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    return EvaluateSekiroLuaAnimationCurveIntValue(
        Snapshot.CurrentAnimationAsset.Get(),
        CurveName,
        Snapshot.CurrentTime,
        Snapshot.CurrentBlendInput,
        DefaultValue);
}

bool USekiroLuaAnimInstance::HasLuaAnimCurveFlag(FName LayerName, FName CurveName, int32 FlagMask) const
{
    if (FlagMask == 0) return false;

    const int32 MaskValue = GetLuaAnimCurveIntValue(LayerName, CurveName, 0);
    return (MaskValue & FlagMask) != 0;
}

float USekiroLuaAnimInstance::GetLuaAnimNormalizedTime(FName LayerName, float DefaultValue) const
{
    const FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    const UAnimationAsset* AnimationAsset = Snapshot.CurrentAnimationAsset.Get();
    if (!AnimationAsset) return DefaultValue;

    const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, Snapshot.CurrentBlendInput);
    if (PlayLength <= UE_KINDA_SMALL_NUMBER) return DefaultValue;

    return FMath::Clamp(Snapshot.CurrentTime / PlayLength, 0.0f, 1.0f);
}

float USekiroLuaAnimInstance::GetLuaAnimRemainingTime(FName LayerName, float DefaultValue) const
{
    const FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    const UAnimationAsset* AnimationAsset = Snapshot.CurrentAnimationAsset.Get();
    if (!AnimationAsset) return DefaultValue;

    const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, Snapshot.CurrentBlendInput);
    const float PlayRate = FMath::Abs(Snapshot.CurrentPlayRate);
    if (PlayLength <= UE_KINDA_SMALL_NUMBER || PlayRate <= UE_KINDA_SMALL_NUMBER) return DefaultValue;

    const float ClampedTime = FMath::Clamp(Snapshot.CurrentTime, 0.0f, PlayLength);
    return FMath::Max((PlayLength - ClampedTime) / PlayRate, 0.0f);
}

FString USekiroLuaAnimInstance::ResolveLuaAnimModuleName() const
{
    if (bLuaAnimModuleNameOverridden && !LuaAnimModuleName.IsEmpty()) return LuaAnimModuleName;

    const UClass* AnimClass = GetClass();
    if (AnimClass && AnimClass->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USekiroLuaAnimInstance*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaAnimModuleName;
}

FName USekiroLuaAnimInstance::ResolveLuaAnimLayerName(FName LayerName) const
{
    if (!LayerName.IsNone()) return LayerName;

    FReadScopeLock SnapshotLock(LuaAnimSnapshotLock);
    if (!DefaultLuaAnimLayerName.IsNone()) return DefaultLuaAnimLayerName;

    return FName(DefaultLuaAnimLayerNameText);
}

bool USekiroLuaAnimInstance::ConfigureLuaAnimation()
{
    if (bLuaAnimConfigured) return true;

    const FString ModuleName = ResolveLuaAnimModuleName();
    if (ModuleName.IsEmpty()) return false;

    LuaAnimModuleName = ModuleName;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim configure skipped because UnLua env is not available. Module=%s"), *LuaAnimModuleName);
        return false;
    }

    LuaAnimLayerNames.Empty();
    const bool bConfigured = TryCallLuaAnimConfigure(LuaEnv, this, LuaAnimModuleName);
    bLuaAnimConfigured = bConfigured;
    return bConfigured;
}

bool USekiroLuaAnimInstance::EvaluateLuaAnimDecision(FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision)
{
    const FString ModuleName = ResolveLuaAnimModuleName();
    if (ModuleName.IsEmpty()) return false;

    LuaAnimModuleName = ModuleName;
    PendingLuaAnimDecisions.Remove(LayerName);
    PendingLuaAnimRootMotionRotationPolicies.Remove(LayerName);
    PendingLuaAnimPreserveNormalizedTime.Remove(LayerName);
    PublishedLuaPoseGraphLayers.Remove(LayerName);

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim skipped because UnLua env is not available. Module=%s Layer=%s"),
            *LuaAnimModuleName,
            *LayerName.ToString());
        return false;
    }

    if (TryCallLuaAnimUpdate(LuaEnv, this, LuaAnimModuleName, LayerName, DeltaSeconds, OutDecision))
    {
        return true;
    }

    return PendingLuaAnimDecisions.RemoveAndCopyValue(LayerName, OutDecision);
}

bool USekiroLuaAnimInstance::ApplyLuaAnimDecision(FName LayerName, const FSekiroLuaAnimDecision& Decision)
{
    if (Decision.StateName.IsNone()) return false;

    UAnimationAsset* TargetAnimationAsset = Decision.AnimationAsset.Get();
    if (!TargetAnimationAsset)
    {
        TargetAnimationAsset = LoadLuaAnimationAsset(Decision.AnimationPath);
    }

    if (!TargetAnimationAsset)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim state has no valid animation. Layer=%s State=%s Path=%s"),
            *LayerName.ToString(),
            *Decision.StateName.ToString(),
            *Decision.AnimationPath);
        return false;
    }

    FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    FSekiroLuaRootMotionRotationPolicy TargetRootMotionRotationPolicy;
    TargetRootMotionRotationPolicy.Mode = Snapshot.CurrentRootMotionRotationMode;
    TargetRootMotionRotationPolicy.TargetWorldYaw = Snapshot.CurrentRootMotionTargetWorldYaw;
    TargetRootMotionRotationPolicy.MaxYawRate = Snapshot.CurrentRootMotionMaxYawRate;
    TargetRootMotionRotationPolicy.CompletionTimeSeconds = Snapshot.CurrentRootMotionCompletionTimeSeconds;
    PendingLuaAnimRootMotionRotationPolicies.RemoveAndCopyValue(LayerName, TargetRootMotionRotationPolicy);
    const float TargetPlayRate = GetSafeLuaPlayRate(Decision.PlayRate);
    const FVector TargetBlendInput = GetSafeLuaBlendInput(Decision.BlendInput);
    const bool bAnimationAssetChanged = Snapshot.CurrentAnimationAsset.Get() != TargetAnimationAsset;
    const bool bStateNameChanged = Snapshot.CurrentStateName != Decision.StateName;
    const bool bStateChanged = !Snapshot.bHasPose || bStateNameChanged || bAnimationAssetChanged;
    bool bPreserveNormalizedTime = false;
    PendingLuaAnimPreserveNormalizedTime.RemoveAndCopyValue(LayerName, bPreserveNormalizedTime);
    const bool bCanPreserveNormalizedTime = bPreserveNormalizedTime
        && Snapshot.bHasPose
        && !bStateNameChanged
        && bAnimationAssetChanged
        && Snapshot.bCurrentLoop
        && Decision.bLoop
        && !Decision.bResetTime
        && !Decision.bUseStartPosition;
    float PreservedNormalizedTime = 0.0f;
    bool bHasPreservedNormalizedTime = false;
    if (bCanPreserveNormalizedTime)
    {
        const float PreviousPlayLength = GetSekiroLuaAnimationPlayLength(
            Snapshot.CurrentAnimationAsset.Get(),
            Snapshot.CurrentBlendInput);
        if (PreviousPlayLength > UE_KINDA_SMALL_NUMBER)
        {
            PreservedNormalizedTime = FMath::Clamp(Snapshot.CurrentTime / PreviousPlayLength, 0.0f, 1.0f);
            bHasPreservedNormalizedTime = true;
        }
    }

    if (bStateChanged)
    {
        Snapshot.PreviousStateName = Snapshot.CurrentStateName;
        Snapshot.PreviousAnimationAsset = Snapshot.CurrentAnimationAsset;
        Snapshot.PreviousAnimationName = Snapshot.CurrentAnimationName;
        Snapshot.PreviousTime = Snapshot.CurrentTime;
        Snapshot.PreviousPlayRate = Snapshot.CurrentPlayRate;
        Snapshot.PreviousRootMotionRotationMode = Snapshot.CurrentRootMotionRotationMode;
        Snapshot.PreviousRootMotionTargetWorldYaw = Snapshot.CurrentRootMotionTargetWorldYaw;
        Snapshot.PreviousRootMotionMaxYawRate = Snapshot.CurrentRootMotionMaxYawRate;
        Snapshot.PreviousRootMotionCompletionTimeSeconds = Snapshot.CurrentRootMotionCompletionTimeSeconds;
        Snapshot.bPreviousLoop = Snapshot.bCurrentLoop;
        Snapshot.PreviousBlendInput = Snapshot.CurrentBlendInput;

        Snapshot.CurrentStateName = Decision.StateName;
        Snapshot.CurrentAnimationAsset = TargetAnimationAsset;
        Snapshot.CurrentAnimationName = Decision.AnimationName;
        Snapshot.CurrentTime = GetSekiroAnimStartTime(TargetAnimationAsset, TargetBlendInput, Decision);
        if (bHasPreservedNormalizedTime)
        {
            const float TargetPlayLength = GetSekiroLuaAnimationPlayLength(TargetAnimationAsset, TargetBlendInput);
            if (TargetPlayLength > UE_KINDA_SMALL_NUMBER)
            {
                Snapshot.CurrentTime = PreservedNormalizedTime * TargetPlayLength;
            }
        }
        Snapshot.CurrentPlayRate = TargetPlayRate;
        Snapshot.CurrentRootMotionRotationMode = TargetRootMotionRotationPolicy.Mode;
        Snapshot.CurrentRootMotionTargetWorldYaw = TargetRootMotionRotationPolicy.TargetWorldYaw;
        Snapshot.CurrentRootMotionMaxYawRate = TargetRootMotionRotationPolicy.MaxYawRate;
        Snapshot.CurrentRootMotionCompletionTimeSeconds = TargetRootMotionRotationPolicy.CompletionTimeSeconds;
        Snapshot.bCurrentLoop = Decision.bLoop;
        Snapshot.CurrentBlendInput = TargetBlendInput;
        Snapshot.bHasPose = true;

        const bool bCanUseInertialization = Snapshot.PreviousAnimationAsset.Get() && Decision.bUseInertialization;
        Snapshot.bUseInertialization = bCanUseInertialization;
        Snapshot.InertialBlendTime = bCanUseInertialization ? FMath::Max(0.0f, Decision.InertialBlendTime) : 0.0f;
        Snapshot.BlendTime = Snapshot.PreviousAnimationAsset.Get() && !bCanUseInertialization
            ? FMath::Max(0.0f, Decision.BlendTime)
            : 0.0f;
        Snapshot.BlendElapsedTime = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : Snapshot.BlendTime;
        Snapshot.BlendAlpha = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : 1.0f;
        if (bCanUseInertialization)
        {
            // 下游惯性化节点保存历史姿势；插件节点不得同时保留旧姿势做线性交叉混合。
            Snapshot.PreviousStateName = NAME_None;
            Snapshot.PreviousAnimationAsset = nullptr;
            Snapshot.PreviousAnimationName = NAME_None;
        }
        PublishLuaAnimSnapshot(LayerName, Snapshot);
        return true;
    }

    Snapshot.bUseInertialization = false;
    Snapshot.InertialBlendTime = 0.0f;
    Snapshot.CurrentPlayRate = TargetPlayRate;
    Snapshot.CurrentRootMotionRotationMode = TargetRootMotionRotationPolicy.Mode;
    Snapshot.CurrentRootMotionTargetWorldYaw = TargetRootMotionRotationPolicy.TargetWorldYaw;
    Snapshot.CurrentRootMotionMaxYawRate = TargetRootMotionRotationPolicy.MaxYawRate;
    Snapshot.CurrentRootMotionCompletionTimeSeconds = TargetRootMotionRotationPolicy.CompletionTimeSeconds;
    if (!Decision.AnimationName.IsNone())
    {
        Snapshot.CurrentAnimationName = Decision.AnimationName;
    }

    Snapshot.bCurrentLoop = Decision.bLoop;
    const float PreviousPlayLength = GetSekiroLuaAnimationPlayLength(Snapshot.CurrentAnimationAsset.Get(), Snapshot.CurrentBlendInput);
    const float TargetPlayLength = GetSekiroLuaAnimationPlayLength(TargetAnimationAsset, TargetBlendInput);
    if (PreviousPlayLength > UE_KINDA_SMALL_NUMBER && TargetPlayLength > UE_KINDA_SMALL_NUMBER)
    {
        const float NormalizedTime = FMath::Clamp(Snapshot.CurrentTime / PreviousPlayLength, 0.0f, 1.0f);
        Snapshot.CurrentTime = NormalizedTime * TargetPlayLength;
    }
    Snapshot.CurrentBlendInput = TargetBlendInput;
    if (Decision.bResetTime)
    {
        Snapshot.CurrentTime = GetSekiroAnimStartTime(TargetAnimationAsset, TargetBlendInput, Decision);
    }

    PublishLuaAnimSnapshot(LayerName, Snapshot);
    return true;
}

void USekiroLuaAnimInstance::AdvanceLuaAnimSnapshot(FName LayerName, float DeltaSeconds)
{
    FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(LayerName);
    if (Snapshot.PoseGraph.bHasOutputPose) return;
    if (!Snapshot.bHasPose) return;

    Snapshot.CurrentTime = AdvanceSekiroAnimTime(Snapshot.CurrentAnimationAsset.Get(), Snapshot.CurrentBlendInput, Snapshot.bCurrentLoop, Snapshot.CurrentTime, Snapshot.CurrentPlayRate, DeltaSeconds);

    if (Snapshot.PreviousAnimationAsset.Get() && Snapshot.BlendAlpha < 1.0f)
    {
        Snapshot.PreviousTime = AdvanceSekiroAnimTime(Snapshot.PreviousAnimationAsset.Get(), Snapshot.PreviousBlendInput, Snapshot.bPreviousLoop, Snapshot.PreviousTime, Snapshot.PreviousPlayRate, DeltaSeconds);
    }

    if (Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER)
    {
        Snapshot.BlendElapsedTime = FMath::Min(Snapshot.BlendElapsedTime + DeltaSeconds, Snapshot.BlendTime);
        Snapshot.BlendAlpha = FMath::Clamp(Snapshot.BlendElapsedTime / Snapshot.BlendTime, 0.0f, 1.0f);
    }
    else
    {
        Snapshot.BlendElapsedTime = Snapshot.BlendTime;
        Snapshot.BlendAlpha = 1.0f;
    }

    if (Snapshot.BlendAlpha >= 1.0f)
    {
        Snapshot.PreviousStateName = NAME_None;
        Snapshot.PreviousAnimationAsset = nullptr;
        Snapshot.PreviousAnimationName = NAME_None;
        Snapshot.PreviousRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract;
        Snapshot.PreviousRootMotionTargetWorldYaw = 0.0f;
        Snapshot.PreviousRootMotionMaxYawRate = 0.0f;
        Snapshot.PreviousRootMotionCompletionTimeSeconds = 0.0f;
    }

    PublishLuaAnimSnapshot(LayerName, Snapshot);
}

bool USekiroLuaAnimInstance::QueueLuaAnimPose(FName LayerName, const FSekiroLuaAnimDecision& Decision)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    if (Decision.StateName.IsNone())
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim pose ignored because state name is None. Layer=%s"), *ResolvedLayerName.ToString());
        return false;
    }

    if (!Decision.AnimationAsset.Get() && Decision.AnimationPath.TrimStartAndEnd().IsEmpty())
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim pose ignored because animation asset/path is empty. Layer=%s State=%s"),
            *ResolvedLayerName.ToString(),
            *Decision.StateName.ToString());
        return false;
    }

    PendingLuaAnimDecisions.Add(ResolvedLayerName, Decision);
    return true;
}

void USekiroLuaAnimInstance::PublishLuaAnimSnapshot(
    FName LayerName,
    const FSekiroLuaAnimSnapshot& Snapshot)
{
    FWriteScopeLock SnapshotLock(LuaAnimSnapshotLock);
    LuaAnimLayerSnapshots.Add(LayerName, Snapshot);
    if (LayerName == DefaultLuaAnimLayerName)
    {
        LuaAnimSnapshot = Snapshot;
    }
}

FSekiroLuaSequencePlayerSnapshot* USekiroLuaAnimInstance::FindLuaSequencePlayer(const FSekiroLuaPoseLink& PoseLink)
{
    if (!PoseLink.IsValid() || PoseLink.Generation != LuaPoseGraphGeneration) return nullptr;

    FSekiroLuaSequencePlayerSnapshot* SequencePlayer = LuaSequencePlayers.Find(PoseLink.NodeId);
    return SequencePlayer && SequencePlayer->PoseLink == PoseLink ? SequencePlayer : nullptr;
}

const FSekiroLuaSequencePlayerSnapshot* USekiroLuaAnimInstance::FindLuaSequencePlayer(const FSekiroLuaPoseLink& PoseLink) const
{
    if (!PoseLink.IsValid() || PoseLink.Generation != LuaPoseGraphGeneration) return nullptr;

    const FSekiroLuaSequencePlayerSnapshot* SequencePlayer = LuaSequencePlayers.Find(PoseLink.NodeId);
    return SequencePlayer && SequencePlayer->PoseLink == PoseLink ? SequencePlayer : nullptr;
}

FSekiroLuaStateResultSnapshot* USekiroLuaAnimInstance::FindLuaStateResult(const FSekiroLuaPoseLink& PoseLink)
{
    if (!PoseLink.IsValid() || PoseLink.Generation != LuaPoseGraphGeneration) return nullptr;

    FSekiroLuaStateResultSnapshot* StateResult = LuaStateResults.Find(PoseLink.NodeId);
    return StateResult && StateResult->PoseLink == PoseLink ? StateResult : nullptr;
}

const FSekiroLuaStateResultSnapshot* USekiroLuaAnimInstance::FindLuaStateResult(const FSekiroLuaPoseLink& PoseLink) const
{
    if (!PoseLink.IsValid() || PoseLink.Generation != LuaPoseGraphGeneration) return nullptr;

    const FSekiroLuaStateResultSnapshot* StateResult = LuaStateResults.Find(PoseLink.NodeId);
    return StateResult && StateResult->PoseLink == PoseLink ? StateResult : nullptr;
}

FSekiroLuaSequencePlayerSnapshot* USekiroLuaAnimInstance::FindLuaStateResultInputSequencePlayer(
    const FSekiroLuaPoseLink& StateResultPoseLink)
{
    FSekiroLuaStateResultSnapshot* StateResult = FindLuaStateResult(StateResultPoseLink);
    return StateResult && StateResult->InputNode.NodeType == ESekiroLuaPoseNodeType::SequencePlayer
        ? FindLuaSequencePlayer(StateResult->InputNode.PoseLink)
        : nullptr;
}

const FSekiroLuaSequencePlayerSnapshot* USekiroLuaAnimInstance::FindLuaStateResultInputSequencePlayer(
    const FSekiroLuaPoseLink& StateResultPoseLink) const
{
    const FSekiroLuaStateResultSnapshot* StateResult = FindLuaStateResult(StateResultPoseLink);
    return StateResult && StateResult->InputNode.NodeType == ESekiroLuaPoseNodeType::SequencePlayer
        ? FindLuaSequencePlayer(StateResult->InputNode.PoseLink)
        : nullptr;
}

void USekiroLuaAnimInstance::BuildLuaPoseGraphSnapshot(FName LayerName, FSekiroLuaAnimSnapshot& Snapshot) const
{
    Snapshot.PoseGraph.Generation = LuaPoseGraphGeneration;
    const int32* TopologySerial = LuaPoseGraphTopologySerials.Find(LayerName);
    Snapshot.PoseGraph.TopologySerial = TopologySerial ? *TopologySerial : 0;
    Snapshot.PoseGraph.SequencePlayers.Reset();
    for (TMap<int32, FSekiroLuaSequencePlayerSnapshot>::TConstIterator PlayerIt(LuaSequencePlayers); PlayerIt; ++PlayerIt)
    {
        const FName* RegisteredLayerName = LuaSequencePlayerLayers.Find(PlayerIt.Key());
        if (RegisteredLayerName && *RegisteredLayerName == LayerName)
        {
            Snapshot.PoseGraph.SequencePlayers.Add(PlayerIt.Value());
        }
    }
    Snapshot.PoseGraph.SequencePlayers.Sort([](
        const FSekiroLuaSequencePlayerSnapshot& Left,
        const FSekiroLuaSequencePlayerSnapshot& Right)
    {
        return Left.PoseLink.NodeId < Right.PoseLink.NodeId;
    });

    Snapshot.PoseGraph.StateResults.Reset();
    for (TMap<int32, FSekiroLuaStateResultSnapshot>::TConstIterator StateResultIt(LuaStateResults); StateResultIt; ++StateResultIt)
    {
        const FName* RegisteredLayerName = LuaStateResultLayers.Find(StateResultIt.Key());
        if (RegisteredLayerName && *RegisteredLayerName == LayerName)
        {
            Snapshot.PoseGraph.StateResults.Add(StateResultIt.Value());
        }
    }
    Snapshot.PoseGraph.StateResults.Sort([](
        const FSekiroLuaStateResultSnapshot& Left,
        const FSekiroLuaStateResultSnapshot& Right)
    {
        return Left.PoseLink.NodeId < Right.PoseLink.NodeId;
    });

    const FSekiroLuaStateResultSnapshot* CurrentStateResult = Snapshot.PoseGraph.FindStateResult(Snapshot.PoseGraph.CurrentPose);
    const FSekiroLuaSequencePlayerSnapshot* CurrentPlayer = CurrentStateResult
        ? Snapshot.PoseGraph.FindSequencePlayer(CurrentStateResult->InputNode.PoseLink)
        : nullptr;
    if (!CurrentStateResult || !CurrentPlayer || !CurrentPlayer->Sequence.Get())
    {
        Snapshot.PoseGraph.bHasOutputPose = false;
        Snapshot.bHasPose = false;
        return;
    }

    Snapshot.CurrentStateName = CurrentStateResult->StateName;
    Snapshot.CurrentAnimationAsset = CurrentPlayer->Sequence.Get();
    Snapshot.CurrentAnimationName = CurrentPlayer->AnimationName;
    Snapshot.CurrentTime = CurrentPlayer->CurrentTime;
    Snapshot.CurrentPlayRate = CurrentPlayer->PlayRate;
    Snapshot.bCurrentLoop = CurrentPlayer->bLoop;
    Snapshot.CurrentBlendInput = FVector::ZeroVector;
    Snapshot.BlendTime = Snapshot.PoseGraph.TransitionTime;
    Snapshot.BlendElapsedTime = Snapshot.PoseGraph.TransitionElapsedTime;
    Snapshot.BlendAlpha = Snapshot.PoseGraph.TransitionAlpha;
    Snapshot.bHasPose = true;

    const FSekiroLuaStateResultSnapshot* PreviousStateResult = Snapshot.PoseGraph.FindStateResult(Snapshot.PoseGraph.PreviousPose);
    const FSekiroLuaSequencePlayerSnapshot* PreviousPlayer = PreviousStateResult
        ? Snapshot.PoseGraph.FindSequencePlayer(PreviousStateResult->InputNode.PoseLink)
        : nullptr;
    if (PreviousStateResult && PreviousPlayer && Snapshot.PoseGraph.TransitionAlpha < 1.0f)
    {
        Snapshot.PreviousStateName = PreviousStateResult->StateName;
        Snapshot.PreviousAnimationAsset = PreviousPlayer->Sequence.Get();
        Snapshot.PreviousAnimationName = PreviousPlayer->AnimationName;
        Snapshot.PreviousTime = PreviousPlayer->CurrentTime;
        Snapshot.PreviousPlayRate = PreviousPlayer->PlayRate;
        Snapshot.bPreviousLoop = PreviousPlayer->bLoop;
        Snapshot.PreviousBlendInput = FVector::ZeroVector;
        return;
    }

    Snapshot.PreviousStateName = NAME_None;
    Snapshot.PreviousAnimationAsset = nullptr;
    Snapshot.PreviousAnimationName = NAME_None;
    Snapshot.PreviousTime = 0.0f;
    Snapshot.PreviousPlayRate = 1.0f;
    Snapshot.bPreviousLoop = false;
    Snapshot.PreviousBlendInput = FVector::ZeroVector;
}

void USekiroLuaAnimInstance::PublishLuaAnimSnapshotsToProxy()
{
    if (!IsInGameThread()) return;

    FSekiroLuaAnimInstanceProxy& LuaAnimProxy = GetProxyOnGameThread<FSekiroLuaAnimInstanceProxy>();
    LuaAnimProxy.PublishSnapshots(*this);
}

void USekiroLuaAnimInstance::CopyLuaAnimSnapshotsForProxy(
    TArray<FSekiroLuaAnimProxyLayerSnapshot>& OutSnapshots,
    FName& OutDefaultLayerName) const
{
    FReadScopeLock SnapshotLock(LuaAnimSnapshotLock);
    OutDefaultLayerName = DefaultLuaAnimLayerName;
    OutSnapshots.Reset(LuaAnimLayerSnapshots.Num());
    for (TMap<FName, FSekiroLuaAnimSnapshot>::TConstIterator SnapshotIt(LuaAnimLayerSnapshots); SnapshotIt; ++SnapshotIt)
    {
        FSekiroLuaAnimProxyLayerSnapshot& ProxySnapshot = OutSnapshots.AddDefaulted_GetRef();
        ProxySnapshot.LayerName = SnapshotIt.Key();
        ProxySnapshot.Snapshot = SnapshotIt.Value();
    }
    OutSnapshots.Sort([](
        const FSekiroLuaAnimProxyLayerSnapshot& Left,
        const FSekiroLuaAnimProxyLayerSnapshot& Right)
    {
        return Left.LayerName.LexicalLess(Right.LayerName);
    });
}

void USekiroLuaAnimInstance::ApplyLuaAnimProxyRuntimeStates(
    const TArray<FSekiroLuaPoseGraphRuntimeState>& RuntimeStates)
{
    if (!IsInGameThread()) return;

    ActiveLuaPoseNodeIds.Empty();
    ActiveLuaPoseGraphGeneration = LuaPoseGraphGeneration;
    for (const FSekiroLuaPoseGraphRuntimeState& RuntimeState : RuntimeStates)
    {
        if (RuntimeState.Generation != LuaPoseGraphGeneration) continue;

        for (int32 NodeId : RuntimeState.ActiveNodeIds)
        {
            ActiveLuaPoseNodeIds.Add(NodeId);
        }
        for (const FSekiroLuaSequencePlayerRuntimeState& PlayerState : RuntimeState.SequencePlayers)
        {
            FSekiroLuaSequencePlayerSnapshot* SequencePlayer = FindLuaSequencePlayer(PlayerState.PoseLink);
            if (SequencePlayer)
            {
                SequencePlayer->CurrentTime = PlayerState.CurrentTime;
            }
        }

        FSekiroLuaAnimSnapshot Snapshot = GetLuaAnimLayerSnapshot(RuntimeState.LayerName);
        if (!Snapshot.PoseGraph.bHasOutputPose) continue;

        Snapshot.PoseGraph.PreviousPose = RuntimeState.TransitionAlpha < 1.0f
            ? RuntimeState.PreviousPose
            : FSekiroLuaPoseLink();
        Snapshot.PoseGraph.TransitionTime = RuntimeState.TransitionTime;
        Snapshot.PoseGraph.TransitionElapsedTime = RuntimeState.TransitionElapsedTime;
        Snapshot.PoseGraph.TransitionAlpha = RuntimeState.TransitionAlpha;
        BuildLuaPoseGraphSnapshot(RuntimeState.LayerName, Snapshot);
        PublishLuaAnimSnapshot(RuntimeState.LayerName, Snapshot);
    }
}
