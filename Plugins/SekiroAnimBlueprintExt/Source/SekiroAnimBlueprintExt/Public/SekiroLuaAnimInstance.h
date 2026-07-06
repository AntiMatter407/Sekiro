#pragma once

#include "CoreMinimal.h"
#include "SekiroAnimBlueprintInstance.h"
#include "SekiroLuaAnimGraphAsset.h"
#include "SekiroLuaAnimInstance.generated.h"

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimLayerBinding
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName LayerName = NAME_None;          // 动画层名称

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<USekiroLuaAnimGraphAsset> GraphAsset = nullptr; // 动画图配置
};

UCLASS(Blueprintable, BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroLuaAnimInstance : public USekiroAnimBlueprintInstance
{
    GENERATED_BODY()

public:
    USekiroLuaAnimInstance();

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimation(float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimationLayer(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, float DeltaSeconds);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimSnapshot() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimLayerSnapshot(FName LayerName) const;

    const FSekiroLuaAnimSnapshot& GetLuaAnimSnapshotRef() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    USekiroLuaAnimGraphAsset* GetLuaAnimGraphAsset() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    USekiroLuaAnimGraphAsset* GetLuaAnimLayerGraphAsset(FName LayerName) const;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FString LuaAnimModuleName;            // Lua 动画模块名称

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName DefaultLuaAnimLayerName = FName(TEXT("Default")); // 默认动画层名称

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<USekiroLuaAnimGraphAsset> LuaAnimGraphAsset = nullptr; // 默认动画图配置

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TArray<FSekiroLuaAnimLayerBinding> LuaAnimLayers; // Lua 动画层配置

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot LuaAnimSnapshot; // 默认动画快照

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TMap<FName, FSekiroLuaAnimSnapshot> LuaAnimLayerSnapshots; // 动画层快照

    FName ResolveLuaAnimLayerName(FName LayerName) const;
    bool EvaluateLuaAnimDecision(FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision);
    bool ApplyLuaAnimDecision(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, const FSekiroLuaAnimDecision& Decision);
    void AdvanceLuaAnimSnapshot(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, float DeltaSeconds);
};
