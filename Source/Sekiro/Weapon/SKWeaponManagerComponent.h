// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UnLuaInterface.h"
#include "SKWeaponManagerComponent.generated.h"

class ASKWeapon;
class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;
class USkeletalMeshComponent;

/** 为 Lua 提供武器生成、挂载展示、动画预览和攻击碰撞的通用原生接口。 */
UCLASS(ClassGroup=(Weapon), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKWeaponManagerComponent : public UActorComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKWeaponManagerComponent();

    // ── Lua 生命周期 ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Lua")
    void SetUseLuaWeaponManagerLogic(bool bNewUseLuaWeaponManagerLogic); // 设置是否由 Lua 管理武器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Lua")
    bool IsUsingLuaWeaponManagerLogic() const;          // 是否启用 Lua 武器管理

    UFUNCTION(BlueprintCallable, Category = "Weapon|Lua")
    void SetLuaWeaponManagerModuleName(const FString& ModuleName); // 设置 Lua 武器管理模块名

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Lua")
    FString GetLuaWeaponManagerModuleName() const;      // 获取 Lua 武器管理模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    /** Lua 可覆盖的武器管理逐帧编排入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Weapon|Gameplay")
    void HandleWeaponManagerTick(float DeltaTime);

    // ── 输入限制区域 ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Restriction")
    void EnterRestrictedZone();                       // 增加禁战区域计数

    UFUNCTION(BlueprintCallable, Category = "Weapon|Restriction")
    void ExitRestrictedZone();                        // 减少禁战区域计数

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Restriction")
    bool IsRestrictedZoneActive() const;              // 是否位于至少一个禁战区域

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Restriction")
    int32 GetRestrictedZoneCount() const;              // 获取禁战区域计数

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Restriction")
    bool IsOwnerReadyForRestrictedWeaponTransition() const; // 是否已站稳到可切换收拔刀动画

    // ── 武器实例与展示 ───────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Runtime")
    bool SpawnWeaponByClassPath(
        const FString& WeaponClassPath,
        FName HandSocket,
        FName HandBoneFallback,
        FName SheathSocket);                            // 按软类路径生成并初始化武器

    UFUNCTION(BlueprintCallable, Category = "Weapon|Runtime")
    void DestroyCurrentWeapon();                        // 销毁当前武器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Runtime")
    ASKWeapon* GetCurrentWeapon() const;                // 获取当前武器

    UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation")
    bool SetWeaponPresentationByName(FName PresentationName); // 按 Drawn/Sheathed 切换挂载

    // ── 角色动画 ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    bool DispatchWeaponAnimationEvent(FName EventName, UAnimSequenceBase* Animation); // 将动画事件转发给 Lua

    /** Lua 可覆盖的武器动画通知处理入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Weapon|Gameplay")
    bool HandleWeaponAnimationEvent(const FString& EventName, UAnimSequenceBase* Animation);

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    bool PlayCharacterAnimationByPath(
        const FString& AnimationPath,
        bool bLooping,
        bool bHoldLastFrame);                           // 按软路径播放角色单节点动画

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Animation")
    bool IsCharacterAnimationPreviewActive() const;    // 是否处于单节点预览模式

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Animation")
    float GetCharacterAnimationPosition() const;       // 获取预览动画当前位置

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    void RestoreCharacterAnimationBlueprint();         // 恢复角色动画蓝图

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    bool SetCharacterAnimFloatPropertyByName(FName PropertyName, float Value); // 写入角色 AnimInstance 浮点属性

    // ── 上半身 Slot 动画 ─────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    bool PlayCharacterSlotAnimationByPath(
        const FString& AnimationPath,
        FName SlotName,
        float BlendInTime,
        float BlendOutTime,
        float PlayRate,
        int32 LoopCount);                               // 通过动态 Montage 播放 Slot 动画

    UFUNCTION(BlueprintCallable, Category = "Weapon|Animation")
    void StopCharacterSlotAnimation(float BlendOutTime); // 停止当前 Slot 动画

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Animation")
    bool IsCharacterSlotAnimationPlaying() const;      // 当前 Slot 动画是否播放中

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Animation")
    float GetCharacterSlotAnimationPosition() const;   // 获取当前 Slot 动画位置

    // ── 攻击碰撞 ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Weapon|Combat")
    bool ActivateWeaponHitbox();                        // 开启当前武器攻击碰撞

    UFUNCTION(BlueprintCallable, Category = "Weapon|Combat")
    bool DeactivateWeaponHitbox();                      // 关闭当前武器攻击碰撞

    UFUNCTION(BlueprintCallable, Category = "Weapon|Combat")
    bool ClearWeaponHitActors();                        // 清空当前攻击命中记录

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lua")
    bool bUseLuaWeaponManagerLogic = true;              // 是否由 Lua 接管武器管理

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lua")
    FString LuaWeaponManagerModuleName = TEXT("Gameplay.Sekiro.Weapon.SKWeaponManager"); // Lua 武器管理模块名

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Runtime")
    TObjectPtr<ASKWeapon> CurrentWeapon;                // 当前装备的武器实例

    int32 RestrictedZoneCount = 0;                      // 当前重叠禁战区域数量

private:
    USkeletalMeshComponent* ResolveCharacterMesh();    // 解析并缓存角色网格
    UAnimSequence* LoadAnimation(const FString& AnimationPath) const; // 加载动画软路径
    void FinishCharacterAnimationPreview();            // 处理单次动画结束状态

    TWeakObjectPtr<USkeletalMeshComponent> CachedCharacterMesh; // 缓存的角色网格
    TSubclassOf<UAnimInstance> CachedAnimInstanceClass; // 需要恢复的动画实例类
    TWeakObjectPtr<UAnimSequence> CurrentPreviewAnimation; // 当前单节点预览动画
    TWeakObjectPtr<UAnimMontage> ActiveSlotMontage;     // 当前上半身动态 Montage
    FTimerHandle AnimationFinishTimer;                  // 单次动画结束定时器
    bool bHoldCurrentAnimationLastFrame = false;        // 单次动画结束后是否保持末帧
};
