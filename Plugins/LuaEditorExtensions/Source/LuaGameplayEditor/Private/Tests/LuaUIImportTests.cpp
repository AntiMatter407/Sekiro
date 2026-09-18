#include "LuaGameplayUIImportLibrary.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLuaUIManifestPreviewTest, "LuaGameplay.Tools.UI.ManifestPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 校验完整 PNG 清单预检及失败清空契约；由游戏线程测试框架显式调用，不生成 UE 资产、不运行 PIE。
 * Parameters 未使用；仅创建唯一临时目录中的 1 像素测试图片和 JSON，结束删除自己的文件。
 * 返回全部断言是否通过。此测试必须由用户明确运行，编译插件不会自动执行。
 */
bool FLuaUIManifestPreviewTest::RunTest(const FString& Parameters)
{
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir(), TEXT("UIManifest_") + Token);
    if (!TestTrue(TEXT("创建独立临时目录"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString ImagePath = Directory / TEXT("pixel.png");
    const FString ManifestPath = Directory / TEXT("manifest.json");
    IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(EImageFormat::PNG);
    const uint8 Pixel[4] = { 32, 64, 128, 128 };
    if (!Wrapper.IsValid() || !Wrapper->SetRaw(Pixel, sizeof(Pixel), 1, 1, ERGBFormat::BGRA, 8))
    {
        AddError(TEXT("无法编码测试 PNG"));
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
        return false;
    }
    const TArray64<uint8>& Compressed = Wrapper->GetCompressed();
    TestTrue(TEXT("写入测试 PNG"), FFileHelper::SaveArrayToFile(Compressed, *ImagePath));
    const FString Entry = FString::Printf(TEXT("{\"SourceFile\":\"pixel.png\",\"AssetPackagePath\":\"/Game/Automation/UIManifest_%s\",\"sRGB\":true,\"AlphaMode\":\"Straight\",\"ExpectedWidth\":1,\"ExpectedHeight\":1}"), *Token);
    const FString Manifest = TEXT("{\"Version\":1,\"SourceRoot\":\".\",\"Textures\":[") + Entry + TEXT("]}");
    TestTrue(TEXT("写入清单"), FFileHelper::SaveStringToFile(Manifest, *ManifestPath));
    TArray<FLuaUITextureImportEntry> Entries;
    FString Error;
    if (TestTrue(TEXT("有效清单预检成功"), ULuaGameplayUIImportLibrary::PreviewUITextureManifest(ManifestPath, Entries, Error)) && Entries.Num() == 1)
    {
        TestEqual(TEXT("实测宽度"), Entries[0].Width, 1);
        TestEqual(TEXT("实测高度"), Entries[0].Height, 1);
        TestFalse(TEXT("预览没有生成资产"), Entries[0].bUpdatesExistingAsset);
        TestEqual(TEXT("实际PNG哈希完整"), Entries[0].PNGSHA1.Len(), 40);
    }
    const TArray<FString> Invalid = {
        Manifest.Replace(TEXT("\"ExpectedWidth\":1"), TEXT("\"ExpectedWidth\":2")),
        Manifest.Replace(TEXT("\"Straight\""), TEXT("\"Opaque\"")),
        Manifest.Replace(TEXT("pixel.png"), TEXT("../outside.png")),
        TEXT("{\"Version\":1,\"Textures\":[") + Entry + TEXT(",") + Entry + TEXT("]}"),
        Manifest.Replace(TEXT("/Game/Automation/"), TEXT("/Engine/Automation/"))
    };
    for (const FString& InvalidManifest : Invalid)
    {
        FFileHelper::SaveStringToFile(InvalidManifest, *ManifestPath);
        Entries.AddDefaulted();
        TestFalse(TEXT("拒绝无效映射或像素声明"), ULuaGameplayUIImportLibrary::PreviewUITextureManifest(ManifestPath, Entries, Error));
        TestEqual(TEXT("失败不保留旧预览"), Entries.Num(), 0);
        TestFalse(TEXT("失败提供诊断"), Error.IsEmpty());
    }
    IFileManager::Get().Delete(*ManifestPath, false, false, true);
    IFileManager::Get().Delete(*ImagePath, false, false, true);
    IFileManager::Get().DeleteDirectory(*Directory, false, false);
    return !HasAnyErrors();
}

#endif
