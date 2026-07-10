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

namespace
{
    static const char* LuaAnimConfigureFunctionName = "Configure";
    static const char* LuaAnimInitializeFunctionName = "Initialize";
    static const char* LuaAnimUpdateFunctionName = "Update";
    static const char* LuaAnimUpdateLayerFunctionName = "UpdateLayer";
    static const TCHAR* DefaultLuaAnimLayerNameText = TEXT("Default");

    const FProperty* FindLuaAnimProperty(const UObject* Object, const FString& PropertyName)
    {
        if (!Object) return nullptr;

        const FString TrimmedPropertyName = PropertyName.TrimStartAndEnd();
        if (TrimmedPropertyName.IsEmpty()) return nullptr;

        return FindFProperty<FProperty>(Object->GetClass(), FName(*TrimmedPropertyName));
    }

    int64 GetLuaAnimIntegerValue(const FNumericProperty* NumericProperty, const void* ValuePtr)
    {
        if (!NumericProperty || !ValuePtr) return 0;
        return NumericProperty->GetSignedIntPropertyValue(ValuePtr);
    }

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

    FString GetLuaAnimEnumText(const UEnum* Enum, int64 Value)
    {
        if (Enum)
        {
            return Enum->GetNameStringByValue(Value);
        }

        return LexToString(Value);
    }

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

    void PushLuaAnimString(lua_State* LuaState, const FString& Value)
    {
        const FTCHARToUTF8 ConvertedValue(*Value);
        lua_pushstring(LuaState, ConvertedValue.Get());
    }

    void SetLuaAnimStringField(lua_State* LuaState, const char* FieldName, const FString& Value)
    {
        PushLuaAnimString(LuaState, Value);
        lua_setfield(LuaState, -2, FieldName);
    }

    void SetLuaAnimNumberField(lua_State* LuaState, const char* FieldName, double Value)
    {
        lua_pushnumber(LuaState, Value);
        lua_setfield(LuaState, -2, FieldName);
    }

    void SetLuaAnimBoolField(lua_State* LuaState, const char* FieldName, bool bValue)
    {
        lua_pushboolean(LuaState, bValue);
        lua_setfield(LuaState, -2, FieldName);
    }

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

    void PushLuaAnimSnapshotTable(lua_State* LuaState, const FSekiroLuaAnimSnapshot& Snapshot)
    {
        lua_newtable(LuaState);
        SetLuaAnimStringField(LuaState, "CurrentStateName", Snapshot.CurrentStateName.ToString());
        SetLuaAnimStringField(LuaState, "PreviousStateName", Snapshot.PreviousStateName.ToString());
        SetLuaAnimNumberField(LuaState, "CurrentTime", Snapshot.CurrentTime);
        SetLuaAnimNumberField(LuaState, "PreviousTime", Snapshot.PreviousTime);
        SetLuaAnimNumberField(LuaState, "CurrentPlayRate", Snapshot.CurrentPlayRate);
        SetLuaAnimNumberField(LuaState, "PreviousPlayRate", Snapshot.PreviousPlayRate);
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

        const UAnimationAsset* CurrentAnimationAsset = Snapshot.CurrentAnimationAsset.Get();
        const UAnimationAsset* PreviousAnimationAsset = Snapshot.PreviousAnimationAsset.Get();
        SetLuaAnimStringField(LuaState, "CurrentAnimationPath", CurrentAnimationAsset ? CurrentAnimationAsset->GetPathName() : FString());
        SetLuaAnimStringField(LuaState, "PreviousAnimationPath", PreviousAnimationAsset ? PreviousAnimationAsset->GetPathName() : FString());
        SetLuaAnimStringField(LuaState, "CurrentAnimationName", Snapshot.CurrentAnimationName.ToString());
        SetLuaAnimStringField(LuaState, "PreviousAnimationName", Snapshot.PreviousAnimationName.ToString());
    }

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

    float GetSafeLuaPlayRate(float PlayRate)
    {
        if (!FMath::IsFinite(PlayRate))
        {
            return 1.0f;
        }

        return PlayRate;
    }

    FVector GetSafeLuaBlendInput(const FVector& BlendInput)
    {
        return FVector(
            FMath::IsFinite(BlendInput.X) ? BlendInput.X : 0.0f,
            FMath::IsFinite(BlendInput.Y) ? BlendInput.Y : 0.0f,
            FMath::IsFinite(BlendInput.Z) ? BlendInput.Z : 0.0f);
    }

    FString GetLuaAnimDebugAssetName(const UAnimationAsset* AnimationAsset)
    {
        return AnimationAsset ? AnimationAsset->GetName() : FString(TEXT("None"));
    }

    FString GetLuaAnimDebugAssetLabel(const UAnimationAsset* AnimationAsset, FName AnimationName)
    {
        const FString AssetName = GetLuaAnimDebugAssetName(AnimationAsset);
        if (AnimationName.IsNone()) return AssetName;

        return FString::Printf(TEXT("%s [%s]"), *AssetName, *AnimationName.ToString());
    }

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

    float GetSekiroLuaRootMotionDeltaTime(float CurrentTime, float PreviewDeltaTime, float PlayLength, bool bLoop)
    {
        if (PreviewDeltaTime <= UE_KINDA_SMALL_NUMBER || PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;
        if (bLoop) return PreviewDeltaTime;

        return FMath::Max(0.0f, FMath::Min(CurrentTime + PreviewDeltaTime, PlayLength) - CurrentTime);
    }

    FTransform ExtractSekiroLuaSequenceRootMotionPreview(const UAnimSequenceBase* Sequence, float CurrentTime, float PreviewDeltaTime, bool bLoop)
    {
        if (!Sequence) return FTransform::Identity;

        const float PlayLength = Sequence->GetPlayLength();
        const float DeltaTime = GetSekiroLuaRootMotionDeltaTime(CurrentTime, PreviewDeltaTime, PlayLength, bLoop);
        if (DeltaTime <= UE_KINDA_SMALL_NUMBER) return FTransform::Identity;

        return Sequence->ExtractRootMotion(CurrentTime, DeltaTime, bLoop);
    }

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

    FTransform ExtractSekiroLuaRootMotionPreview(const UAnimationAsset* AnimationAsset, float CurrentTime, float PreviewDeltaTime, bool bLoop, const FVector& BlendInput)
    {
        if (!AnimationAsset) return FTransform::Identity;

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return ExtractSekiroLuaBlendSpaceRootMotionPreview(BlendSpace, CurrentTime, PreviewDeltaTime, bLoop, BlendInput);
        }

        return ExtractSekiroLuaSequenceRootMotionPreview(Cast<UAnimSequenceBase>(AnimationAsset), CurrentTime, PreviewDeltaTime, bLoop);
    }

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

    float GetSekiroAnimStartTime(const UAnimationAsset* AnimationAsset, const FVector& BlendInput, const FSekiroLuaAnimDecision& Decision)
    {
        if (!AnimationAsset || !Decision.bUseStartPosition) return 0.0f;

        const float PlayLength = GetSekiroLuaAnimationPlayLength(AnimationAsset, BlendInput);
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;

        return FMath::Clamp(Decision.StartPosition, 0.0f, 1.0f) * PlayLength;
    }

    float EvaluateSekiroLuaSequenceCurveValue(const UAnimSequenceBase* Sequence, FName CurveName, float Time, float DefaultValue)
    {
        if (!Sequence || CurveName.IsNone()) return DefaultValue;

        const USkeleton* Skeleton = Sequence->GetSkeleton();
        if (!Skeleton) return DefaultValue;

        FSmartName SmartName;
        if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, SmartName))
        {
            return DefaultValue;
        }

        const FAnimCurveBase* CurveBase = Sequence->GetCurveData().GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);
        const FFloatCurve* FloatCurve = static_cast<const FFloatCurve*>(CurveBase);
        if (!FloatCurve) return DefaultValue;

        return FloatCurve->Evaluate(Time);
    }

    int32 EvaluateSekiroLuaSequenceCurveIntValue(const UAnimSequenceBase* Sequence, FName CurveName, float Time, int32 DefaultValue)
    {
        return FMath::RoundToInt(EvaluateSekiroLuaSequenceCurveValue(Sequence, CurveName, Time, static_cast<float>(DefaultValue)));
    }

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

    bool ReadLuaNameField(UnLua::FLuaTable& LuaTable, const char* FieldName, FName& OutName)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TSTRING) return false;

        const FString FieldText = FieldValue.Value<FString>();
        OutName = FName(*FieldText);
        return !OutName.IsNone();
    }

    bool ReadLuaStringField(UnLua::FLuaTable& LuaTable, const char* FieldName, FString& OutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TSTRING) return false;

        OutValue = FieldValue.Value<FString>();
        return !OutValue.IsEmpty();
    }

    bool ReadLuaFloatField(UnLua::FLuaTable& LuaTable, const char* FieldName, float& OutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TNUMBER) return false;

        OutValue = FieldValue.Value<float>();
        return true;
    }

    bool ReadLuaBoolField(UnLua::FLuaTable& LuaTable, const char* FieldName, bool& bOutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TBOOLEAN) return false;

        bOutValue = FieldValue.Value<bool>();
        return true;
    }

    bool ReadLuaAnimationAssetField(UnLua::FLuaTable& LuaTable, const char* FieldName, UAnimationAsset*& OutAnimationAsset)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TUSERDATA) return false;

        UObject* ObjectValue = FieldValue.Value<UObject*>();
        OutAnimationAsset = Cast<UAnimationAsset>(ObjectValue);
        return OutAnimationAsset != nullptr;
    }

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
    LuaAnimSnapshot = FSekiroLuaAnimSnapshot();
    LuaAnimLayerSnapshots.Empty();
    PendingLuaAnimDecisions.Empty();
    bLuaAnimConfigured = false;
    bLuaAnimModuleNameOverridden = false;
    ConfigureLuaAnimation();
}

void USekiroLuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (bAutoUpdateLuaDrivenAnimation)
    {
        UpdateLuaDrivenAnimation(DeltaSeconds);
    }
}

void USekiroLuaAnimInstance::DisplayDebugInstance(FDisplayDebugManager& DisplayDebugManager, float& Indent)
{
    Super::DisplayDebugInstance(DisplayDebugManager, Indent);

#if ENABLE_DRAW_DEBUG
    DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.35f, 0.85f, 1.0f));
    DisplayDebugManager.DrawString(FString::Printf(
        TEXT("Sekiro Lua Animation: Module=%s DefaultLayer=%s Layers=%d"),
        *LuaAnimModuleName,
        *DefaultLuaAnimLayerName.ToString(),
        LuaAnimLayerSnapshots.Num()), Indent);

    FIndenter LuaAnimIndent(Indent);
    if (LuaAnimLayerSnapshots.Num() <= 0)
    {
        DisplayDebugManager.SetLinearDrawColor(FLinearColor(0.9f, 0.9f, 0.9f));
        DisplayDebugManager.DrawString(TEXT("No Lua animation snapshot."), Indent);
        return;
    }

    for (TMap<FName, FSekiroLuaAnimSnapshot>::TConstIterator SnapshotIt(LuaAnimLayerSnapshots); SnapshotIt; ++SnapshotIt)
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

    LuaAnimSnapshot = GetLuaAnimLayerSnapshot(DefaultLuaAnimLayerName);
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

    AdvanceLuaAnimSnapshot(ResolvedLayerName, DeltaSeconds);
    return bAppliedDecision;
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
    DefaultLuaAnimLayerName = ResolveLuaAnimLayerName(LayerName);
}

void USekiroLuaAnimInstance::RegisterLuaAnimLayer(FName LayerName)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    LuaAnimLayerNames.AddUnique(ResolvedLayerName);
}

void USekiroLuaAnimInstance::ClearLuaAnimLayers()
{
    LuaAnimLayerNames.Empty();
    LuaAnimLayerSnapshots.Empty();
    PendingLuaAnimDecisions.Empty();
    LuaAnimSnapshot = FSekiroLuaAnimSnapshot();
}

TArray<FName> USekiroLuaAnimInstance::GetLuaAnimLayerNames() const
{
    return LuaAnimLayerNames;
}

UAnimationAsset* USekiroLuaAnimInstance::LoadLuaAnimationAsset(const FString& AssetPath)
{
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
    return LuaAnimSnapshot;
}

FSekiroLuaAnimSnapshot USekiroLuaAnimInstance::GetLuaAnimLayerSnapshot(FName LayerName) const
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    const FSekiroLuaAnimSnapshot* FoundSnapshot = LuaAnimLayerSnapshots.Find(ResolvedLayerName);
    if (FoundSnapshot)
    {
        return *FoundSnapshot;
    }

    return FSekiroLuaAnimSnapshot();
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

const FSekiroLuaAnimSnapshot& USekiroLuaAnimInstance::GetLuaAnimSnapshotRef() const
{
    return LuaAnimSnapshot;
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

    FSekiroLuaAnimSnapshot& Snapshot = LuaAnimLayerSnapshots.FindOrAdd(LayerName);
    const float TargetPlayRate = GetSafeLuaPlayRate(Decision.PlayRate);
    const FVector TargetBlendInput = GetSafeLuaBlendInput(Decision.BlendInput);
    const bool bStateChanged = !Snapshot.bHasPose || Snapshot.CurrentStateName != Decision.StateName || Snapshot.CurrentAnimationAsset.Get() != TargetAnimationAsset;

    if (bStateChanged)
    {
        Snapshot.PreviousStateName = Snapshot.CurrentStateName;
        Snapshot.PreviousAnimationAsset = Snapshot.CurrentAnimationAsset;
        Snapshot.PreviousAnimationName = Snapshot.CurrentAnimationName;
        Snapshot.PreviousTime = Snapshot.CurrentTime;
        Snapshot.PreviousPlayRate = Snapshot.CurrentPlayRate;
        Snapshot.bPreviousLoop = Snapshot.bCurrentLoop;
        Snapshot.PreviousBlendInput = Snapshot.CurrentBlendInput;

        Snapshot.CurrentStateName = Decision.StateName;
        Snapshot.CurrentAnimationAsset = TargetAnimationAsset;
        Snapshot.CurrentAnimationName = Decision.AnimationName;
        Snapshot.CurrentTime = GetSekiroAnimStartTime(TargetAnimationAsset, TargetBlendInput, Decision);
        Snapshot.CurrentPlayRate = TargetPlayRate;
        Snapshot.bCurrentLoop = Decision.bLoop;
        Snapshot.CurrentBlendInput = TargetBlendInput;
        Snapshot.bHasPose = true;

        Snapshot.BlendTime = Snapshot.PreviousAnimationAsset.Get() ? FMath::Max(0.0f, Decision.BlendTime) : 0.0f;
        Snapshot.BlendElapsedTime = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : Snapshot.BlendTime;
        Snapshot.BlendAlpha = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : 1.0f;
        return true;
    }

    Snapshot.CurrentPlayRate = TargetPlayRate;
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

    return true;
}

void USekiroLuaAnimInstance::AdvanceLuaAnimSnapshot(FName LayerName, float DeltaSeconds)
{
    FSekiroLuaAnimSnapshot* SnapshotPtr = LuaAnimLayerSnapshots.Find(LayerName);
    if (!SnapshotPtr || !SnapshotPtr->bHasPose) return;

    FSekiroLuaAnimSnapshot& Snapshot = *SnapshotPtr;
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
    }
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
