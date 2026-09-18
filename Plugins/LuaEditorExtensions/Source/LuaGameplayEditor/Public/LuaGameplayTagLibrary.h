#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaGameplayTagLibrary.generated.h"

USTRUCT(BlueprintType)
struct LUAGAMEPLAYEDITOR_API FLuaGameplayTagEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    FString Tag; // 完整点分标签名，保留源大小写

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    FString Comment; // 当前节点的开发说明
};

USTRUCT(BlueprintType)
struct LUAGAMEPLAYEDITOR_API FLuaGameplayTagGenerationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    FString AssetObjectPath; // 生成目标的完整对象路径

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    int32 TagCount = 0; // 已解析的父节点和叶节点总数

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    bool bAssetSaved = false; // 数据表资产已成功写盘

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    bool bSourceRegistered = false; // 数据表已成功注册为标签源

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools")
    FString Message; // 成功说明或包含部分完成状态的错误
};

/** 声明式 Lua 到 GameplayTag 数据表的通用编辑器接口。 */
UCLASS()
class LUAGAMEPLAYEDITOR_API ULuaGameplayTagLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── 预览与生成 ──
    UFUNCTION(BlueprintCallable, Category = "Gameplay Tools|Gameplay Tags")
    static bool PreviewGameplayTags(const FString& LuaFilePath, TArray<FLuaGameplayTagEntry>& OutTags, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Gameplay Tools|Gameplay Tags", meta = (AdvancedDisplay = "bRegisterTagSource", CPP_Default_bRegisterTagSource = "true"))
    static bool GenerateGameplayTagTable(const FString& LuaFilePath, const FString& AssetPackagePath, bool bRegisterTagSource, FLuaGameplayTagGenerationResult& OutResult);
};
