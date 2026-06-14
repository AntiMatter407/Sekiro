#include "SekiroAnimNotifies.h"
#include "SekiroImportLog.h"

// ============================================================================
// UAnimNotify_SKAttackHitbox
// ============================================================================
void UAnimNotify_SKAttackHitbox::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKAttackHitbox] Frame notify: Judge=%d, AtkType=%d, Source=%d on %s"),
        BehaviorJudgeID, AttackType, Source, *MeshComp->GetOwner()->GetName());

    // TODO: 触发 Sekiro Combat System 的攻击盒生成
    // 通过 Interface 或 Delegate 通知 CombatComponent 在指定帧生成攻击碰撞体
}

// ============================================================================
// UAnimNotify_SKBulletBehavior
// ============================================================================
void UAnimNotify_SKBulletBehavior::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKBulletBehavior] DummyPoly=%d, Judge=%d"),
        DummyPolyID, BehaviorJudgeID);

    // TODO: 通过 BulletManager 生成射弹
}

// ============================================================================
// UAnimNotify_SKSpEffect
// ============================================================================
void UAnimNotify_SKSpEffect::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKSpEffect] SpEffectID=%d"), SpEffectID);

    // TODO: 通过 GAS 或状态系统应用 SpEffect
}

// ============================================================================
// UAnimNotifyState_SKBehaviorFlag
// ============================================================================
void UAnimNotifyState_SKBehaviorFlag::NotifyBegin(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, float TotalDuration,
    const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKBehaviorFlag] Begin: %s = %s"),
        *FlagName.ToString(), bEnable ? TEXT("true") : TEXT("false"));

    // TODO: 通过 Interface 设置角色行为标记
}

void UAnimNotifyState_SKBehaviorFlag::NotifyEnd(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKBehaviorFlag] End: %s = %s"),
        *FlagName.ToString(), bEnable ? TEXT("false") : TEXT("true"));

    // TODO: 清除标记
}

// ============================================================================
// UAnimNotify_SKRumbleCam
// ============================================================================
void UAnimNotify_SKRumbleCam::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    if (!MeshComp || !MeshComp->GetOwner())
        return;

    UE_LOG(LogSekiroImport, Verbose, TEXT("[SKRumbleCam] ID=%d, Global=%d"),
        RumbleCamID, bIsGlobal);

    // TODO: 通过 CameraManager 触发镜头震动
}
