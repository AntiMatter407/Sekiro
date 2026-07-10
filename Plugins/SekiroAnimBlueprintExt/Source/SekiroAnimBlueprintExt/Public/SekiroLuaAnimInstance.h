#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SekiroLuaAnimTypes.h"
#include "UnLuaInterface.h"
#include "SekiroLuaAnimInstance.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroLuaAnimInstance : public UAnimInstance, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USekiroLuaAnimInstance();

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void DisplayDebugInstance(FDisplayDebugManager& DisplayDebugManager, float& Indent) override;

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimation(float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimationLayer(FName LayerName, float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetLuaAnimModuleName(const FString& ModuleName);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FString GetLuaAnimModuleName() const;

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetDefaultLuaAnimLayerName(FName LayerName);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void RegisterLuaAnimLayer(FName LayerName);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void ClearLuaAnimLayers();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    TArray<FName> GetLuaAnimLayerNames() const;

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    UAnimationAsset* LoadLuaAnimationAsset(const FString& AssetPath);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePose(FName LayerName, FName StateName, UAnimationAsset* BlendSpaceAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPath(FName LayerName, FName StateName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimNumberProperty(const FString& PropertyName, float DefaultValue) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool GetLuaAnimBoolProperty(const FString& PropertyName, bool bDefaultValue) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FString GetLuaAnimPropertyText(const FString& PropertyName, const FString& DefaultValue) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimSnapshot() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimLayerSnapshot(FName LayerName) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimCurveValue(FName LayerName, FName CurveName, float DefaultValue) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    int32 GetLuaAnimCurveIntValue(FName LayerName, FName CurveName, int32 DefaultValue) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool HasLuaAnimCurveFlag(FName LayerName, FName CurveName, int32 FlagMask) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimNormalizedTime(FName LayerName, float DefaultValue) const;

    const FSekiroLuaAnimSnapshot& GetLuaAnimSnapshotRef() const;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bAutoUpdateLuaDrivenAnimation = true; // 是否在 NativeUpdateAnimation 中自动更新 Lua 动画

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FString DefaultLuaAnimModuleName;       // 默认 Lua 动画模块名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FString LuaAnimModuleName;            // Lua 动画模块名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName DefaultLuaAnimLayerName = FName(TEXT("Default")); // 默认动画层名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TArray<FName> LuaAnimLayerNames;       // Lua 注册的动画层名称

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot LuaAnimSnapshot; // 默认动画快照

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TMap<FName, FSekiroLuaAnimSnapshot> LuaAnimLayerSnapshots; // 动画层快照

    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UAnimationAsset>> LuaAnimAssetCache; // Lua 动画资源缓存

    FString ResolveLuaAnimModuleName() const;
    FName ResolveLuaAnimLayerName(FName LayerName) const;
    bool ConfigureLuaAnimation();
    bool EvaluateLuaAnimDecision(FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision);
    bool ApplyLuaAnimDecision(FName LayerName, const FSekiroLuaAnimDecision& Decision);
    void AdvanceLuaAnimSnapshot(FName LayerName, float DeltaSeconds);

private:
    bool QueueLuaAnimPose(FName LayerName, const FSekiroLuaAnimDecision& Decision);

    bool bLuaAnimConfigured = false;       // Lua 动画配置是否已执行
    bool bLuaAnimModuleNameOverridden = false; // Lua 模块名是否被运行时手动覆盖

    TMap<FName, FSekiroLuaAnimDecision> PendingLuaAnimDecisions; // Lua 通过 C++ 接口提交的待应用姿势
};
