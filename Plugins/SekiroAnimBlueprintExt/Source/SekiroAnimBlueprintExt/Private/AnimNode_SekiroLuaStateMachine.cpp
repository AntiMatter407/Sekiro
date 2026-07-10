#include "AnimNodes/AnimNode_SekiroLuaStateMachine.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "AnimationRuntime.h"
#include "Logging/TokenizedMessage.h"
#include "SekiroLuaAnimInstance.h"

namespace
{
    float GetSekiroLuaBlendSpaceSampleLength(const UBlendSpace* BlendSpace, const TArray<FBlendSampleData>& BlendSampleData)
    {
        if (!BlendSpace) return 0.0f;

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

    float GetSekiroLuaNodeAnimationPlayLength(const UAnimationAsset* AnimationAsset, const FVector& BlendInput)
    {
        if (!AnimationAsset) return 0.0f;

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            TArray<FBlendSampleData> BlendSampleData;
            int32 CachedTriangulationIndex = INDEX_NONE;
            if (BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
            {
                const float BlendSpaceLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendSampleData);
                if (BlendSpaceLength > UE_KINDA_SMALL_NUMBER) return BlendSpaceLength;
            }
        }

        return AnimationAsset->GetPlayLength();
    }

    float GetSekiroLuaWrappedDeltaTime(float PreviousTime, float CurrentTime, float PlayLength, bool bLoop)
    {
        float DeltaTime = CurrentTime - PreviousTime;
        if (bLoop && PlayLength > UE_KINDA_SMALL_NUMBER && DeltaTime < 0.0f)
        {
            DeltaTime += PlayLength;
        }

        return DeltaTime;
    }

    float EstimateSekiroLuaPreviousTime(const UAnimationAsset* AnimationAsset, const FVector& BlendInput, float CurrentTime, float PlayRate, bool bLoop, float DeltaSeconds)
    {
        if (!AnimationAsset) return CurrentTime;

        const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset);
        const float AssetRateScale = Sequence ? Sequence->RateScale : 1.0f;
        const float PlayLength = GetSekiroLuaNodeAnimationPlayLength(AnimationAsset, BlendInput);
        float PreviousTime = CurrentTime - DeltaSeconds * PlayRate * AssetRateScale;
        if (bLoop && PlayLength > UE_KINDA_SMALL_NUMBER)
        {
            while (PreviousTime < 0.0f)
            {
                PreviousTime += PlayLength;
            }

            while (PreviousTime > PlayLength)
            {
                PreviousTime -= PlayLength;
            }

            return PreviousTime;
        }

        return FMath::Clamp(PreviousTime, 0.0f, PlayLength);
    }

    bool FindSekiroLuaPreviousSnapshotTime(const FSekiroLuaAnimSnapshot& Snapshot, const UAnimationAsset* AnimationAsset, float& OutTime)
    {
        if (!AnimationAsset) return false;

        if (Snapshot.CurrentAnimationAsset.Get() == AnimationAsset)
        {
            OutTime = Snapshot.CurrentTime;
            return true;
        }

        if (Snapshot.PreviousAnimationAsset.Get() == AnimationAsset)
        {
            OutTime = Snapshot.PreviousTime;
            return true;
        }

        return false;
    }

    FTransform ExtractSekiroLuaSequenceRootMotion(const UAnimSequenceBase* Sequence, float PreviousTime, float CurrentTime, bool bLoop)
    {
        if (!Sequence) return FTransform::Identity;

        const float PlayLength = Sequence->GetPlayLength();
        const float DeltaTime = GetSekiroLuaWrappedDeltaTime(PreviousTime, CurrentTime, PlayLength, bLoop);
        if (FMath::IsNearlyZero(DeltaTime)) return FTransform::Identity;

        return Sequence->ExtractRootMotion(PreviousTime, DeltaTime, bLoop);
    }

    FRootMotionMovementParams ExtractSekiroLuaBlendSpaceRootMotion(const UBlendSpace* BlendSpace, float PreviousTime, float CurrentTime, bool bLoop, const FVector& BlendInput)
    {
        FRootMotionMovementParams RootMotionParams;
        if (!BlendSpace) return RootMotionParams;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            return RootMotionParams;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendSampleData);
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return RootMotionParams;

        float PreviousNormalizedTime = FMath::Clamp(PreviousTime / PlayLength, 0.0f, 1.0f);
        float CurrentNormalizedTime = FMath::Clamp(CurrentTime / PlayLength, 0.0f, 1.0f);
        if (bLoop && CurrentTime < PreviousTime)
        {
            CurrentNormalizedTime += 1.0f;
        }

        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            const UAnimSequence* Sequence = BlendSample.Animation;
            if (!Sequence) continue;

            const float SampleLength = Sequence->GetPlayLength();
            const float SamplePreviousTime = FMath::Clamp(PreviousNormalizedTime, 0.0f, 1.0f) * SampleLength;
            float SampleCurrentTime = CurrentNormalizedTime * SampleLength;
            if (bLoop && SampleCurrentTime > SampleLength)
            {
                SampleCurrentTime -= SampleLength;
            }

            RootMotionParams.AccumulateWithBlend(
                ExtractSekiroLuaSequenceRootMotion(Sequence, SamplePreviousTime, SampleCurrentTime, bLoop),
                SampleWeight);
        }

        return RootMotionParams;
    }

    FRootMotionMovementParams ExtractSekiroLuaAnimationRootMotion(const UAnimationAsset* AnimationAsset, float PreviousTime, float CurrentTime, bool bLoop, const FVector& BlendInput)
    {
        FRootMotionMovementParams RootMotionParams;
        if (!AnimationAsset) return RootMotionParams;

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            return ExtractSekiroLuaBlendSpaceRootMotion(BlendSpace, PreviousTime, CurrentTime, bLoop, BlendInput);
        }

        if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            RootMotionParams.Accumulate(ExtractSekiroLuaSequenceRootMotion(Sequence, PreviousTime, CurrentTime, bLoop));
        }

        return RootMotionParams;
    }

    void AccumulateSekiroLuaAnimationRootMotion(
        FAnimInstanceProxy* AnimInstanceProxy,
        const UAnimationAsset* AnimationAsset,
        float PreviousTime,
        float CurrentTime,
        bool bLoop,
        const FVector& BlendInput,
        float Weight)
    {
        if (!AnimInstanceProxy || !AnimationAsset || Weight <= UE_KINDA_SMALL_NUMBER) return;

        FRootMotionMovementParams RootMotionParams = ExtractSekiroLuaAnimationRootMotion(AnimationAsset, PreviousTime, CurrentTime, bLoop, BlendInput);
        if (RootMotionParams.bHasRootMotion)
        {
            AnimInstanceProxy->GetExtractedRootMotion().AccumulateWithBlend(RootMotionParams, Weight);
        }
    }

    void AccumulateSekiroLuaRootMotion(
        const FAnimationUpdateContext& Context,
        const FSekiroLuaAnimSnapshot* PreviousSnapshot,
        const FSekiroLuaAnimSnapshot& CurrentSnapshot)
    {
        FAnimInstanceProxy* AnimInstanceProxy = Context.AnimInstanceProxy;
        if (!AnimInstanceProxy || !AnimInstanceProxy->ShouldExtractRootMotion()) return;
        if (!CurrentSnapshot.bHasPose || !CurrentSnapshot.CurrentAnimationAsset.Get()) return;

        const float DeltaSeconds = Context.GetDeltaTime();
        if (DeltaSeconds <= UE_KINDA_SMALL_NUMBER) return;

        const float RootMotionWeight = Context.GetFinalBlendWeight() * Context.GetRootMotionWeightModifier();
        if (RootMotionWeight <= UE_KINDA_SMALL_NUMBER) return;

        const UAnimationAsset* CurrentAnimationAsset = CurrentSnapshot.CurrentAnimationAsset.Get();
        float PreviousCurrentTime = 0.0f;
        if (!PreviousSnapshot || !FindSekiroLuaPreviousSnapshotTime(*PreviousSnapshot, CurrentAnimationAsset, PreviousCurrentTime))
        {
            PreviousCurrentTime = EstimateSekiroLuaPreviousTime(
                CurrentAnimationAsset,
                CurrentSnapshot.CurrentBlendInput,
                CurrentSnapshot.CurrentTime,
                CurrentSnapshot.CurrentPlayRate,
                CurrentSnapshot.bCurrentLoop,
                DeltaSeconds);
        }

        const bool bIsTransitioning = CurrentSnapshot.PreviousAnimationAsset.Get() && CurrentSnapshot.BlendAlpha < 1.0f;
        const float CurrentPoseWeight = bIsTransitioning ? FMath::Clamp(CurrentSnapshot.BlendAlpha, 0.0f, 1.0f) : 1.0f;
        AccumulateSekiroLuaAnimationRootMotion(
            AnimInstanceProxy,
            CurrentAnimationAsset,
            PreviousCurrentTime,
            CurrentSnapshot.CurrentTime,
            CurrentSnapshot.bCurrentLoop,
            CurrentSnapshot.CurrentBlendInput,
            RootMotionWeight * CurrentPoseWeight);

        if (!bIsTransitioning) return;

        const UAnimationAsset* PreviousAnimationAsset = CurrentSnapshot.PreviousAnimationAsset.Get();
        const float PreviousPoseWeight = 1.0f - CurrentPoseWeight;
        float PreviousPreviousTime = 0.0f;
        if (!PreviousSnapshot || !FindSekiroLuaPreviousSnapshotTime(*PreviousSnapshot, PreviousAnimationAsset, PreviousPreviousTime))
        {
            PreviousPreviousTime = EstimateSekiroLuaPreviousTime(
                PreviousAnimationAsset,
                CurrentSnapshot.PreviousBlendInput,
                CurrentSnapshot.PreviousTime,
                CurrentSnapshot.PreviousPlayRate,
                CurrentSnapshot.bPreviousLoop,
                DeltaSeconds);
        }

        AccumulateSekiroLuaAnimationRootMotion(
            AnimInstanceProxy,
            PreviousAnimationAsset,
            PreviousPreviousTime,
            CurrentSnapshot.PreviousTime,
            CurrentSnapshot.bPreviousLoop,
            CurrentSnapshot.PreviousBlendInput,
            RootMotionWeight * PreviousPoseWeight);
    }

    void ResetSekiroLuaRootBoneForRootMotion(FPoseContext& Output)
    {
        if (!Output.AnimInstanceProxy || !Output.AnimInstanceProxy->ShouldExtractRootMotion()) return;
        if (Output.Pose.GetNumBones() <= 0) return;

        const FCompactPoseBoneIndex RootBoneIndex(0);
        Output.Pose[RootBoneIndex] = Output.Pose.GetBoneContainer().GetRefPoseTransform(RootBoneIndex);
    }

    void EvaluateSekiroLuaSequencePose(const UAnimSequenceBase* Sequence, float Time, bool bLoop, FPoseContext& Output)
    {
        if (!Sequence)
        {
            Output.ResetToRefPose();
            return;
        }

        const bool bExpectedAdditive = Output.ExpectsAdditivePose();
        const bool bIsAdditive = Sequence->IsValidAdditive();
        if (bExpectedAdditive && !bIsAdditive)
        {
            FText Message = FText::Format(
                NSLOCTEXT("AnimNode_SekiroLuaStateMachine", "AdditiveMismatchWarning", "Trying to play a non-additive animation '{0}' into a pose that is expected to be additive."),
                FText::FromString(Sequence->GetName()));
            Output.LogMessage(EMessageSeverity::Warning, Message);
        }

        FAnimationPoseData AnimationPoseData(Output);
        Sequence->GetAnimationPose(AnimationPoseData, FAnimExtractContext(static_cast<double>(Time), Output.AnimInstanceProxy->ShouldExtractRootMotion(), FDeltaTimeRecord(), bLoop));
        ResetSekiroLuaRootBoneForRootMotion(Output);
    }

    void EvaluateSekiroLuaBlendSpacePose(const UBlendSpace* BlendSpace, float Time, bool bLoop, const FVector& BlendInput, FPoseContext& Output)
    {
        if (!BlendSpace)
        {
            Output.ResetToRefPose();
            return;
        }

        const bool bExpectedAdditive = Output.ExpectsAdditivePose();
        const bool bIsAdditive = BlendSpace->IsValidAdditive();
        if (bExpectedAdditive && !bIsAdditive)
        {
            FText Message = FText::Format(
                NSLOCTEXT("AnimNode_SekiroLuaStateMachine", "AdditiveMismatchWarning", "Trying to play a non-additive animation '{0}' into a pose that is expected to be additive."),
                FText::FromString(BlendSpace->GetName()));
            Output.LogMessage(EMessageSeverity::Warning, Message);
        }

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true))
        {
            Output.ResetToRefPose();
            return;
        }

        const float PlayLength = GetSekiroLuaBlendSpaceSampleLength(BlendSpace, BlendSampleData);
        const float NormalizedTime = PlayLength > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(Time / PlayLength, 0.0f, 1.0f) : 0.0f;
        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            SampleData.Animation = BlendSample.Animation;
            if (BlendSample.Animation)
            {
                SampleData.Time = NormalizedTime * BlendSample.Animation->GetPlayLength();
            }
        }

        FAnimationPoseData AnimationPoseData(Output);
        BlendSpace->GetAnimationPose(BlendSampleData, FAnimExtractContext(static_cast<double>(Time), Output.AnimInstanceProxy->ShouldExtractRootMotion(), FDeltaTimeRecord(), bLoop), AnimationPoseData);
        ResetSekiroLuaRootBoneForRootMotion(Output);
    }

    void EvaluateSekiroLuaAnimationPose(const UAnimationAsset* AnimationAsset, float Time, bool bLoop, const FVector& BlendInput, FPoseContext& Output)
    {
        if (!AnimationAsset || !AnimationAsset->GetSkeleton())
        {
            Output.ResetToRefPose();
            return;
        }

        if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset))
        {
            EvaluateSekiroLuaBlendSpacePose(BlendSpace, Time, bLoop, BlendInput, Output);
            return;
        }

        if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            EvaluateSekiroLuaSequencePose(Sequence, Time, bLoop, Output);
            return;
        }

        Output.ResetToRefPose();
    }

    FString GetSekiroLuaAnimDebugAssetName(const UAnimationAsset* AnimationAsset)
    {
        return AnimationAsset ? AnimationAsset->GetName() : FString(TEXT("None"));
    }

    FString GetSekiroLuaAnimDebugAssetLabel(const UAnimationAsset* AnimationAsset, FName AnimationName)
    {
        const FString AssetName = GetSekiroLuaAnimDebugAssetName(AnimationAsset);
        if (AnimationName.IsNone()) return AssetName;

        return FString::Printf(TEXT("%s [%s]"), *AssetName, *AnimationName.ToString());
    }

    void AddSekiroLuaBlendSpaceSampleDebugData(FNodeDebugData& DebugData, const UAnimationAsset* AnimationAsset, const FVector& BlendInput)
    {
        const UBlendSpace* BlendSpace = Cast<UBlendSpace>(AnimationAsset);
        if (!BlendSpace) return;

        TArray<FBlendSampleData> BlendSampleData;
        int32 CachedTriangulationIndex = INDEX_NONE;
        if (!BlendSpace->GetSamplesFromBlendInput(BlendInput, BlendSampleData, CachedTriangulationIndex, true)) return;

        for (int32 SampleIndex = 0; SampleIndex < BlendSampleData.Num(); ++SampleIndex)
        {
            const FBlendSampleData& SampleData = BlendSampleData[SampleIndex];
            const float SampleWeight = SampleData.GetClampedWeight();
            if (SampleWeight <= UE_KINDA_SMALL_NUMBER) continue;

            const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SampleData.SampleDataIndex);
            FNodeDebugData& SampleDebugData = DebugData.BranchFlow(SampleWeight);
            SampleDebugData.AddDebugItem(FString::Printf(
                TEXT("BlendSample: %s Weight: %.1f%%"),
                *GetNameSafe(BlendSample.Animation),
                SampleWeight * 100.0f), true);
        }
    }

    bool ResolveSekiroLuaAnimInstanceSnapshot(FAnimInstanceProxy* AnimInstanceProxy, FName LayerName, FSekiroLuaAnimSnapshot& OutSnapshot)
    {
        if (!AnimInstanceProxy) return false;

        UObject* AnimInstanceObject = AnimInstanceProxy->GetAnimInstanceObject();
        const USekiroLuaAnimInstance* LuaAnimInstance = Cast<USekiroLuaAnimInstance>(AnimInstanceObject);
        if (!LuaAnimInstance) return false;

        OutSnapshot = LuaAnimInstance->GetLuaAnimLayerSnapshot(LayerName);
        return true;
    }
}

void FAnimNode_SekiroLuaStateMachine::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);

    CachedSnapshot = FSekiroLuaAnimSnapshot();
    PreviousRootMotionSnapshot = FSekiroLuaAnimSnapshot();
    bHasPreviousRootMotionSnapshot = false;
}

void FAnimNode_SekiroLuaStateMachine::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    FAnimNode_Base::Update_AnyThread(Context);

    FSekiroLuaAnimSnapshot NewSnapshot;
    if (!ResolveSekiroLuaAnimInstanceSnapshot(Context.AnimInstanceProxy, LayerName, NewSnapshot))
    {
        CachedSnapshot = FSekiroLuaAnimSnapshot();
        PreviousRootMotionSnapshot = FSekiroLuaAnimSnapshot();
        bHasPreviousRootMotionSnapshot = false;
        return;
    }

    AccumulateSekiroLuaRootMotion(
        Context,
        bHasPreviousRootMotionSnapshot ? &PreviousRootMotionSnapshot : nullptr,
        NewSnapshot);

    CachedSnapshot = NewSnapshot;
    PreviousRootMotionSnapshot = NewSnapshot;
    bHasPreviousRootMotionSnapshot = NewSnapshot.bHasPose;
}

void FAnimNode_SekiroLuaStateMachine::Evaluate_AnyThread(FPoseContext& Output)
{
    if (!CachedSnapshot.bHasPose || !CachedSnapshot.CurrentAnimationAsset.Get())
    {
        Output.ResetToRefPose();
        return;
    }

    const bool bNeedsBlend = CachedSnapshot.PreviousAnimationAsset.Get() && CachedSnapshot.BlendAlpha < 1.0f;
    if (!bNeedsBlend)
    {
        EvaluateSekiroLuaAnimationPose(CachedSnapshot.CurrentAnimationAsset.Get(), CachedSnapshot.CurrentTime, CachedSnapshot.bCurrentLoop, CachedSnapshot.CurrentBlendInput, Output);
        return;
    }

    FPoseContext PreviousPose(Output);
    FPoseContext CurrentPose(Output);
    EvaluateSekiroLuaAnimationPose(CachedSnapshot.PreviousAnimationAsset.Get(), CachedSnapshot.PreviousTime, CachedSnapshot.bPreviousLoop, CachedSnapshot.PreviousBlendInput, PreviousPose);
    EvaluateSekiroLuaAnimationPose(CachedSnapshot.CurrentAnimationAsset.Get(), CachedSnapshot.CurrentTime, CachedSnapshot.bCurrentLoop, CachedSnapshot.CurrentBlendInput, CurrentPose);

    const FAnimationPoseData PreviousPoseData(PreviousPose);
    const FAnimationPoseData CurrentPoseData(CurrentPose);
    FAnimationPoseData OutputPoseData(Output);
    FAnimationRuntime::BlendTwoPosesTogether(PreviousPoseData, CurrentPoseData, 1.0f - CachedSnapshot.BlendAlpha, OutputPoseData);
}

void FAnimNode_SekiroLuaStateMachine::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = DebugData.GetNodeName(this);
    const UAnimationAsset* CurrentAnimationAsset = CachedSnapshot.CurrentAnimationAsset.Get();
    const UAnimationAsset* PreviousAnimationAsset = CachedSnapshot.PreviousAnimationAsset.Get();
    const bool bIsTransitioning = PreviousAnimationAsset && CachedSnapshot.BlendAlpha < 1.0f;
    const float CurrentPoseWeight = bIsTransitioning ? FMath::Clamp(CachedSnapshot.BlendAlpha, 0.0f, 1.0f) : 1.0f;
    const float PreviousPoseWeight = bIsTransitioning ? 1.0f - CurrentPoseWeight : 0.0f;

    DebugLine += FString::Printf(TEXT("(Layer: %s, State: %s, Current: %.1f%%, Previous: %.1f%%, BlendTime: %.2f, BlendElapsed: %.2f)"),
        *LayerName.ToString(),
        *CachedSnapshot.CurrentStateName.ToString(),
        CurrentPoseWeight * 100.0f,
        PreviousPoseWeight * 100.0f,
        CachedSnapshot.BlendTime,
        CachedSnapshot.BlendElapsedTime);
    DebugData.AddDebugItem(DebugLine);

    if (PreviousPoseWeight > UE_KINDA_SMALL_NUMBER)
    {
        FNodeDebugData& PreviousDebugData = DebugData.BranchFlow(PreviousPoseWeight, TEXT("Previous"));
        PreviousDebugData.AddDebugItem(FString::Printf(
            TEXT("Previous Anim: %s Time: %.2f BlendInput: %s"),
            *GetSekiroLuaAnimDebugAssetLabel(PreviousAnimationAsset, CachedSnapshot.PreviousAnimationName),
            CachedSnapshot.PreviousTime,
            *CachedSnapshot.PreviousBlendInput.ToCompactString()), true);
        AddSekiroLuaBlendSpaceSampleDebugData(PreviousDebugData, PreviousAnimationAsset, CachedSnapshot.PreviousBlendInput);
    }

    FNodeDebugData& CurrentDebugData = DebugData.BranchFlow(CurrentPoseWeight, TEXT("Current"));
    CurrentDebugData.AddDebugItem(FString::Printf(
        TEXT("Current Anim: %s Time: %.2f BlendInput: %s"),
        *GetSekiroLuaAnimDebugAssetLabel(CurrentAnimationAsset, CachedSnapshot.CurrentAnimationName),
        CachedSnapshot.CurrentTime,
        *CachedSnapshot.CurrentBlendInput.ToCompactString()), true);
    AddSekiroLuaBlendSpaceSampleDebugData(CurrentDebugData, CurrentAnimationAsset, CachedSnapshot.CurrentBlendInput);
}
