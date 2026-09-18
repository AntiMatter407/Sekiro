#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaGameplayUIImportLibrary.generated.h"

USTRUCT(BlueprintType)
struct LUAGAMEPLAYEDITOR_API FLuaUITextureImportEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString SourceFile; // 完整规范化 PNG 来源路径

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString AssetPackagePath; // 用户清单指定的稳定目标长包名

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    int32 Width = 0; // 解码获得的真实像素宽度

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    int32 Height = 0; // 解码获得的真实像素高度

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    bool bSRGB = true; // 是否以 sRGB 解释 RGB，alpha 始终原样保留

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString AlphaMode; // Straight、Premultiplied 或 Opaque，仅记录声明而不改变像素

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString SourceSymbol; // 原布局符号，可为空

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString SourceArchive; // 原始资源包或成员路径，仅作溯源说明

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString SourceHash; // 管线提供的原始资源哈希，仅作溯源说明

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString PNGSHA1; // 本次实际读取 PNG 字节的 SHA1

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString AtlasRectJson; // 可选原图集矩形元数据，不执行裁切

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    bool bUpdatesExistingAsset = false; // 已存在且所有权校验通过的纹理将被更新
};

USTRUCT(BlueprintType)
struct LUAGAMEPLAYEDITOR_API FLuaUITextureImportResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    int32 ImportedCount = 0; // 已实际保存的资产数量

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    TArray<FString> ImportedAssetPaths; // 已成功保存的完整对象路径，部分失败时仍保留

    UPROPERTY(BlueprintReadOnly, Category = "Gameplay Tools|UI")
    FString Message; // 完整成功或包含部分完成数量的错误诊断
};

/** 编辑器 PNG 清单导入器，不解包原游戏格式，不依赖项目或外部导入任务。 */
UCLASS()
class LUAGAMEPLAYEDITOR_API ULuaGameplayUIImportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── 清单预览与受控导入 ──
    UFUNCTION(BlueprintCallable, Category = "Gameplay Tools|UI")
    static bool PreviewUITextureManifest(const FString& ManifestFilePath, TArray<FLuaUITextureImportEntry>& OutEntries, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Gameplay Tools|UI")
    static bool ImportUITextureManifest(const FString& ManifestFilePath, FLuaUITextureImportResult& OutResult);
};
