#include "LuaGameplayUIImportLibrary.h"

#include "LuaGameplayTagParser.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace LuaUIImport
{
    const TCHAR* GeneratorKey = TEXT("LuaGameplay.UI.Generator");
    const TCHAR* GeneratorValue = TEXT("UITextureManifest.v1");
    const TCHAR* SourceKey = TEXT("LuaGameplay.UI.SourceFile");
    constexpr int64 MaxDecodedBytes = 256LL * 1024 * 1024;

    struct FPreparedTexture
    {
        FLuaUITextureImportEntry Entry; // 完整预检后的只读记录
        TArray64<uint8> Pixels; // 从同一次读取获得的 BGRA8 像素，生成时不再读取来源
        UTexture2D* Existing = nullptr; // 同源且已预检的既有纹理，调用内借用
        FString AssetFilename; // 已解析的目标磁盘路径
    };

    struct FTextureSettingsSnapshot
    {
        bool bSRGB = true; // 修改前的颜色空间
        bool bNeverStream = true; // 修改前的流送标志
        bool bCompressionNoAlpha = false; // 修改前的 alpha 压缩标志
        TextureCompressionSettings Compression = TC_EditorIcon; // 修改前的压缩类型
        TextureGroup Group = TEXTUREGROUP_UI; // 修改前的纹理组
        TextureMipGenSettings Mips = TMGS_NoMipmaps; // 修改前的 mip 生成方式
        TextureAddress AddressX = TA_Clamp; // 修改前的横向采样方式
        TextureAddress AddressY = TA_Clamp; // 修改前的纵向采样方式
    };

    /** 规范化路径；游戏线程。Path 可相对 BaseDir，返回绝对路径，不创建或改动文件。 */
    FString ResolvePath(const FString& BaseDir, const FString& Path)
    {
        FString Result = FPaths::ConvertRelativePathToFull(BaseDir, Path);
        FPaths::NormalizeFilename(Result);
        FPaths::CollapseRelativeDirectories(Result);
        return Result;
    }

    /** 保存可随工程移动的来源标识；游戏线程。AbsolutePath 为规范路径，工程外来源保持绝对路径。 */
    FString SourceIdentity(const FString& AbsolutePath)
    {
        FString Relative = AbsolutePath;
        if (FPaths::MakePathRelativeTo(Relative, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()))
            && !Relative.StartsWith(TEXT("../")) && FPaths::IsRelative(Relative)) return Relative;
        return AbsolutePath;
    }

    /** 预检目标可写性；游戏线程。Filename 为磁盘路径，Error 仅失败时填写；不自动签出、解除只读或写文件。 */
    bool CheckWritable(const FString& Filename, FString& Error)
    {
        if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
        {
            Error = TEXT("目标文件只读，请先签出：") + Filename;
            return false;
        }
        if (ISourceControlModule::Get().IsEnabled())
        {
            FSourceControlStatePtr State = ISourceControlModule::Get().GetProvider().GetState(Filename, EStateCacheUsage::Use);
            if (State.IsValid() && (State->IsCheckedOutOther() || State->CanCheckout()))
            {
                Error = TEXT("版本控制不允许写入，请先完成签出：") + Filename;
                return false;
            }
        }
        return true;
    }

    /** 读取可选说明字段；游戏线程。Object/Key 指明 JSON 字段，OutValue 缺省为空，类型错误填写 Error 并返回 false。 */
    bool ReadOptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& OutValue, FString& Error)
    {
        OutValue.Reset();
        if (!Object->HasField(Key)) return true;
        if (!Object->TryGetStringField(Key, OutValue))
        {
            Error = FString::Printf(TEXT("%s 必须是字符串"), Key);
            return false;
        }
        return true;
    }

    /** 比对可选预期尺寸；游戏线程。Key 为尺寸字段，Actual 为实测像素数；字段存在时必须为正整数且相同。 */
    bool ValidateExpectedSize(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Actual, FString& Error)
    {
        if (!Object->HasField(Key)) return true;
        double Value = 0;
        if (!Object->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value) || Value != Actual)
        {
            Error = FString::Printf(TEXT("%s 与 PNG 实测尺寸 %d 不匹配"), Key, Actual);
            return false;
        }
        return true;
    }

    /** 检查可选图集矩形并保存原元数据；游戏线程。Object 为条目，OutJson 缺省为空；只记录，不裁切、不验证图集分辨率。 */
    bool ReadAtlasRect(const TSharedPtr<FJsonObject>& Object, FString& OutJson, FString& Error)
    {
        if (!Object->HasField(TEXT("AtlasRect"))) return true;
        const TSharedPtr<FJsonObject>* Rect = nullptr;
        if (!Object->TryGetObjectField(TEXT("AtlasRect"), Rect) || !Rect || !Rect->IsValid())
        {
            Error = TEXT("AtlasRect 必须包含 X/Y/Width/Height 整数字段");
            return false;
        }
        const TArray<FString> Fields = { TEXT("X"), TEXT("Y"), TEXT("Width"), TEXT("Height") };
        for (const FString& Field : Fields)
        {
            double Number = 0;
            const bool bDimension = Field == TEXT("Width") || Field == TEXT("Height");
            if (!(*Rect)->TryGetNumberField(Field, Number) || !FMath::IsFinite(Number) || Number < (bDimension ? 1 : 0)
                || Number > MAX_int32 || FMath::FloorToDouble(Number) != Number)
            {
                Error = TEXT("AtlasRect 的坐标必须非负、尺寸必须正值，且均为32位整数");
                return false;
            }
        }
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
        return FJsonSerializer::Serialize(Rect->ToSharedRef(), Writer);
    }

    /** 校验目标资产边界和同源所有权；游戏线程。Prepared 输入路径并输出既有纹理/磁盘路径，失败不修改资产。 */
    bool ValidateTarget(FPreparedTexture& Prepared, FString& Error)
    {
        const FString& PackagePath = Prepared.Entry.AssetPackagePath;
        if (!FLuaGameplayTagParser::ValidateAssetPackagePath(PackagePath, Error)) return false;
        if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, Prepared.AssetFilename, FPackageName::GetAssetPackageExtension()))
        {
            Error = TEXT("无法解析目标资产文件路径：") + PackagePath;
            return false;
        }
        Prepared.AssetFilename = FPaths::ConvertRelativePathToFull(Prepared.AssetFilename);
        if (!CheckWritable(Prepared.AssetFilename, Error)) return false;
        const FString ObjectPath = PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath);
        UObject* ExistingObject = FindObject<UObject>(nullptr, *ObjectPath);
        const bool bOnDisk = FPackageName::DoesPackageExist(PackagePath);
        if (!ExistingObject && bOnDisk) ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
        Prepared.Existing = Cast<UTexture2D>(ExistingObject);
        if ((ExistingObject && !Prepared.Existing) || (!Prepared.Existing && (bOnDisk || FindPackage(nullptr, *PackagePath))))
        {
            Error = TEXT("拒绝覆盖已有非工具纹理资产或同名包：") + PackagePath;
            return false;
        }
        if (!Prepared.Existing) return true;
        UTexture2D* Texture = Prepared.Existing;
#if UE_VERSION_NEWER_THAN(5, 7, 0)
        FMetaData* Metadata = &Texture->GetOutermost()->GetMetaData();
#else
        UMetaData* Metadata = Texture->GetOutermost()->GetMetaData();
#endif
        const FString SavedSource = ResolvePath(FPaths::ProjectDir(), Metadata->GetValue(Texture, SourceKey));
        if (Metadata->GetValue(Texture, GeneratorKey) != GeneratorValue || !FPaths::IsSamePath(SavedSource, Prepared.Entry.SourceFile))
        {
            Error = TEXT("拒绝覆盖非本工具或其他来源创建的纹理：") + PackagePath;
            return false;
        }
        if (Texture->GetOutermost()->IsDirty())
        {
            Error = TEXT("目标纹理包有未保存修改，请先保存或撤销：") + PackagePath;
            return false;
        }
        if (Texture->Source.GetFormat() != TSF_BGRA8 || Texture->Source.GetNumMips() != 1 || Texture->Source.GetNumSlices() != 1
            || Texture->Source.GetNumBlocks() != 1 || Texture->Source.GetNumLayers() != 1)
        {
            Error = TEXT("目标纹理的源格式已被改为非本工具的单层 BGRA8，拒绝自动覆盖：") + PackagePath;
            return false;
        }
        Prepared.Entry.bUpdatesExistingAsset = true;
        return true;
    }

    /** 读取并解码单个 PNG 条目；游戏线程。Root 为源目录、TotalBytes 为累积像素预算，成功填充 Prepared，失败不改资产。 */
    bool PrepareEntry(const TSharedPtr<FJsonObject>& Object, const FString& Root, int64& TotalBytes, FPreparedTexture& Prepared, FString& Error)
    {
        FString RelativeFile;
        FLuaUITextureImportEntry& Entry = Prepared.Entry;
        if (!Object->TryGetStringField(TEXT("SourceFile"), RelativeFile) || RelativeFile.IsEmpty() || !FPaths::IsRelative(RelativeFile)
            || !Object->TryGetStringField(TEXT("AssetPackagePath"), Entry.AssetPackagePath) || !Object->TryGetBoolField(TEXT("sRGB"), Entry.bSRGB))
        {
            Error = TEXT("条目必须提供相对 SourceFile、AssetPackagePath 字符串和 sRGB 布尔值");
            return false;
        }
        Entry.SourceFile = ResolvePath(Root, RelativeFile);
        if (!FPaths::IsUnderDirectory(Entry.SourceFile, Root) || !FPaths::GetExtension(Entry.SourceFile).Equals(TEXT("png"), ESearchCase::IgnoreCase))
        {
            Error = TEXT("SourceFile 必须位于 SourceRoot 内且是 PNG 文件：") + RelativeFile;
            return false;
        }
        if (!ReadOptionalString(Object, TEXT("AlphaMode"), Entry.AlphaMode, Error)
            || !ReadOptionalString(Object, TEXT("SourceSymbol"), Entry.SourceSymbol, Error)
            || !ReadOptionalString(Object, TEXT("SourceArchive"), Entry.SourceArchive, Error)
            || !ReadOptionalString(Object, TEXT("SourceHash"), Entry.SourceHash, Error)
            || !ReadAtlasRect(Object, Entry.AtlasRectJson, Error)) return false;
        if (Entry.AlphaMode.IsEmpty()) Entry.AlphaMode = TEXT("Straight");
        if (Entry.AlphaMode != TEXT("Straight") && Entry.AlphaMode != TEXT("Premultiplied") && Entry.AlphaMode != TEXT("Opaque"))
        {
            Error = TEXT("AlphaMode 只支持 Straight、Premultiplied、Opaque");
            return false;
        }
        const int64 FileSize = IFileManager::Get().FileSize(*Entry.SourceFile);
        if (FileSize <= 0 || FileSize > 64LL * 1024 * 1024)
        {
            Error = TEXT("PNG 不存在、为空或超过 64 MiB：") + Entry.SourceFile;
            return false;
        }
        TArray<uint8> Compressed;
        if (!FFileHelper::LoadFileToArray(Compressed, *Entry.SourceFile) || Compressed.Num() > 64 * 1024 * 1024)
        {
            Error = TEXT("无法读取 PNG：") + Entry.SourceFile;
            return false;
        }
        IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()))
        {
            Error = TEXT("无法解析 PNG 头：") + Entry.SourceFile;
            return false;
        }
        if (Wrapper->GetBitDepth() != 8)
        {
            Error = TEXT("只接受 8 位通道 PNG，避免导入时隐式降低原图精度：") + Entry.SourceFile;
            return false;
        }
        Entry.Width = Wrapper->GetWidth();
        Entry.Height = Wrapper->GetHeight();
        const int64 PixelBytes = static_cast<int64>(Entry.Width) * Entry.Height * 4;
        if (Entry.Width <= 0 || Entry.Height <= 0 || Entry.Width > 8192 || Entry.Height > 8192 || PixelBytes > MaxDecodedBytes - TotalBytes)
        {
            Error = TEXT("纹理尺寸须在 1..8192，且清单解码总量不能超过 256 MiB");
            return false;
        }
        if (!ValidateExpectedSize(Object, TEXT("ExpectedWidth"), Entry.Width, Error)
            || !ValidateExpectedSize(Object, TEXT("ExpectedHeight"), Entry.Height, Error)) return false;
        if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Prepared.Pixels) || Prepared.Pixels.Num() != PixelBytes)
        {
            Error = TEXT("PNG 像素解码失败：") + Entry.SourceFile;
            return false;
        }
        if (Entry.AlphaMode == TEXT("Opaque"))
        {
            for (int64 Offset = 3; Offset < Prepared.Pixels.Num(); Offset += 4)
            {
                if (Prepared.Pixels[Offset] != 255) { Error = TEXT("Opaque 条目含非不透明 alpha：") + Entry.SourceFile; return false; }
            }
        }
        TotalBytes += PixelBytes;
        Entry.PNGSHA1 = FSHA1::HashBuffer(Compressed.GetData(), Compressed.Num()).ToString();
        return ValidateTarget(Prepared, Error);
    }

    /**
     * 完整预检清单及所有像素，生成前不修改外部资产；仅游戏线程、非 PIE。
     * ManifestFilePath 是用户选择的 JSON 路径；OutPrepared 成功时包含本次解码快照，失败清空并填写 Error。
     * 返回 true 表示所有文件、尺寸、重复目标、所有权和可写性均通过，不保证之后磁盘保存必然成功。
     */
    bool PrepareManifest(const FString& ManifestFilePath, TArray<FPreparedTexture>& OutPrepared, FString& Error)
    {
        OutPrepared.Reset();
        Error.Reset();
        if (!IsInGameThread() || !GIsEditor || (GEditor && GEditor->PlayWorld))
        {
            Error = TEXT("UI 导入必须在非 PIE 编辑器的游戏线程执行");
            return false;
        }
        const FString Filename = ResolvePath(FPaths::ProjectDir(), ManifestFilePath);
        const int64 FileSize = IFileManager::Get().FileSize(*Filename);
        if (ManifestFilePath.IsEmpty() || FileSize <= 0 || FileSize > 1024 * 1024)
        {
            Error = TEXT("请选择存在且不超过 1 MiB 的 JSON 清单：") + Filename;
            return false;
        }
        FString Source;
        if (!FFileHelper::LoadFileToString(Source, *Filename)) { Error = TEXT("无法读取清单：") + Filename; return false; }
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Source);
        TSharedPtr<FJsonObject> Manifest;
        if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
        {
            Error = TEXT("JSON 清单解析失败：") + Reader->GetErrorMessage();
            return false;
        }
        double Version = 0;
        FString RootPath;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Manifest->TryGetNumberField(TEXT("Version"), Version) || Version != 1
            || !Manifest->TryGetArrayField(TEXT("Textures"), Entries) || !Entries || Entries->IsEmpty() || Entries->Num() > 1024
            || !ReadOptionalString(Manifest, TEXT("SourceRoot"), RootPath, Error))
        {
            Error = TEXT("清单要求 Version=1，Textures 为 1..1024 条对象数组，SourceRoot 可选字符串");
            return false;
        }
        if (RootPath.IsEmpty()) RootPath = TEXT(".");
        const FString Root = ResolvePath(FPaths::GetPath(Filename), RootPath);
        if (!IFileManager::Get().DirectoryExists(*Root)) { Error = TEXT("SourceRoot 目录不存在：") + Root; return false; }
        TSet<FString> Targets;
        int64 TotalBytes = 0;
        for (int32 Index = 0; Index < Entries->Num(); ++Index)
        {
            const TSharedPtr<FJsonValue>& Value = (*Entries)[Index];
            FPreparedTexture Prepared;
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid() || !PrepareEntry(*Object, Root, TotalBytes, Prepared, Error))
            {
                Error = FString::Printf(TEXT("Textures[%d]：%s"), Index, Error.IsEmpty() ? TEXT("必须是对象") : *Error);
                OutPrepared.Reset();
                return false;
            }
            const FString FoldedTarget = Prepared.Entry.AssetPackagePath.ToLower();
            if (Targets.Contains(FoldedTarget))
            {
                Error = TEXT("清单含重复或大小写冲突目标：") + Prepared.Entry.AssetPackagePath;
                OutPrepared.Reset();
                return false;
            }
            Targets.Add(FoldedTarget);
            OutPrepared.Add(MoveTemp(Prepared));
        }
        return true;
    }

    /** 记录原纹理设置用于保存失败恢复；游戏线程。Texture 非空借用对象，返回独立值快照，不修改纹理。 */
    FTextureSettingsSnapshot CaptureSettings(const UTexture2D* Texture)
    {
        FTextureSettingsSnapshot Result;
        Result.bSRGB = Texture->SRGB;
        Result.bNeverStream = Texture->NeverStream;
        Result.bCompressionNoAlpha = Texture->CompressionNoAlpha;
        Result.Compression = Texture->CompressionSettings;
        Result.Group = Texture->LODGroup;
        Result.Mips = Texture->MipGenSettings;
        Result.AddressX = Texture->AddressX;
        Result.AddressY = Texture->AddressY;
        return Result;
    }

    /** 恢复本工具修改的纹理设置；游戏线程。Texture 非空，Snapshot 为此前值；不保存、不触发资源构建。 */
    void RestoreSettings(UTexture2D* Texture, const FTextureSettingsSnapshot& Snapshot)
    {
        Texture->SRGB = Snapshot.bSRGB;
        Texture->NeverStream = Snapshot.bNeverStream;
        Texture->CompressionNoAlpha = Snapshot.bCompressionNoAlpha;
        Texture->CompressionSettings = Snapshot.Compression;
        Texture->LODGroup = Snapshot.Group;
        Texture->MipGenSettings = Snapshot.Mips;
        Texture->AddressX = Snapshot.AddressX;
        Texture->AddressY = Snapshot.AddressY;
    }

    /**
     * 保存单个完整预检的纹理；游戏线程，不使用 AssetImportTask 或外部程序。
     * Prepared 含借用既有资产及不可变像素快照；OutObjectPath 成功时返回完整资产路径，失败填写 Error。
     * 已有纹理仅更新同来源；保存失败恢复像素、设置和元数据内存，新建失败移出目标包以允许重试。
     */
    bool SaveTexture(const FPreparedTexture& Prepared, FString& OutObjectPath, FString& Error)
    {
        const FLuaUITextureImportEntry& Entry = Prepared.Entry;
        if (!CheckWritable(Prepared.AssetFilename, Error)) return false;
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Prepared.AssetFilename), true))
        {
            Error = TEXT("无法创建资产目录：") + Prepared.AssetFilename;
            return false;
        }
        const bool bNew = !Prepared.Existing;
        UTexture2D* Texture = Prepared.Existing;
        UPackage* Package = Texture ? Texture->GetOutermost() : CreatePackage(*Entry.AssetPackagePath);
        TArray64<uint8> PreviousPixels;
        FTextureSettingsSnapshot PreviousSettings;
        int32 PreviousWidth = 0;
        int32 PreviousHeight = 0;
        if (Texture)
        {
            if (!Texture->Source.GetMipData(PreviousPixels, 0)) { Error = TEXT("无法备份既有纹理源像素，未修改资产"); return false; }
            PreviousWidth = Texture->Source.GetSizeX();
            PreviousHeight = Texture->Source.GetSizeY();
            PreviousSettings = CaptureSettings(Texture);
        }
        else
        {
            const FString AssetName = FPackageName::GetLongPackageAssetName(Entry.AssetPackagePath);
            Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        }
#if UE_VERSION_NEWER_THAN(5, 7, 0)
        FMetaData* Metadata = &Package->GetMetaData();
#else
        UMetaData* Metadata = Package->GetMetaData();
#endif
        const TMap<FString, FString> Values = {
            { GeneratorKey, GeneratorValue }, { SourceKey, SourceIdentity(Entry.SourceFile) },
            { TEXT("LuaGameplay.UI.PNGSHA1"), Entry.PNGSHA1 }, { TEXT("LuaGameplay.UI.SourceSymbol"), Entry.SourceSymbol },
            { TEXT("LuaGameplay.UI.SourceArchive"), Entry.SourceArchive }, { TEXT("LuaGameplay.UI.SourceHash"), Entry.SourceHash },
            { TEXT("LuaGameplay.UI.AtlasRect"), Entry.AtlasRectJson }, { TEXT("LuaGameplay.UI.AlphaMode"), Entry.AlphaMode }
        };
        TMap<FString, FString> PreviousValues;
        TSet<FString> ExistingKeys;
        for (const TPair<FString, FString>& Pair : Values)
        {
            if (Metadata->HasValue(Texture, *Pair.Key)) ExistingKeys.Add(Pair.Key);
            PreviousValues.Add(Pair.Key, Metadata->GetValue(Texture, *Pair.Key));
            Metadata->SetValue(Texture, *Pair.Key, *Pair.Value);
        }
        Texture->PreEditChange(nullptr);
        Texture->Source.Init(Entry.Width, Entry.Height, 1, 1, TSF_BGRA8, Prepared.Pixels.GetData());
        Texture->SRGB = Entry.bSRGB;
        Texture->LODGroup = TEXTUREGROUP_UI;
        Texture->CompressionSettings = TC_EditorIcon;
        Texture->CompressionNoAlpha = false;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->NeverStream = true;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->PostEditChange();
        Package->MarkPackageDirty();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, Texture, *Prepared.AssetFilename, SaveArgs))
        {
            for (const TPair<FString, FString>& Pair : PreviousValues)
            {
                if (ExistingKeys.Contains(Pair.Key)) Metadata->SetValue(Texture, *Pair.Key, *Pair.Value);
                else Metadata->RemoveValue(Texture, *Pair.Key);
            }
            if (bNew)
            {
                Texture->ClearFlags(RF_Public | RF_Standalone);
                Texture->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
                Package->Rename(*FString::Printf(TEXT("/Temp/LuaUIImportFailed_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
            }
            else
            {
                Texture->PreEditChange(nullptr);
                Texture->Source.Init(PreviousWidth, PreviousHeight, 1, 1, TSF_BGRA8, PreviousPixels.GetData());
                RestoreSettings(Texture, PreviousSettings);
                Texture->PostEditChange();
            }
            Package->SetDirtyFlag(false);
            Error = TEXT("资产保存失败，已恢复该纹理内存：") + Prepared.AssetFilename;
            return false;
        }
        Package->SetDirtyFlag(false);
        if (bNew) FAssetRegistryModule::AssetCreated(Texture);
        OutObjectPath = Texture->GetPathName();
        return true;
    }
}

/**
 * 读取 JSON 清单并预览真实尺寸、源哈希、目标及更新状态；仅游戏线程、非 PIE，不生成或保存资产。
 * @param ManifestFilePath 用户提供的清单路径，可相对工程根目录。
 * @param OutEntries 完整成功时输出全部条目；任何失败清空，避免显示过期或部分预览。
 * @param OutError 文件、清单、图片或所有权校验失败的诊断；成功为空。
 * @return 所有条目通过预检时 true，预览不代表磁盘保存成功。
 */
bool ULuaGameplayUIImportLibrary::PreviewUITextureManifest(const FString& ManifestFilePath, TArray<FLuaUITextureImportEntry>& OutEntries, FString& OutError)
{
    OutEntries.Reset();
    TArray<LuaUIImport::FPreparedTexture> Prepared;
    if (!LuaUIImport::PrepareManifest(ManifestFilePath, Prepared, OutError)) return false;
    for (const LuaUIImport::FPreparedTexture& Texture : Prepared) OutEntries.Add(Texture.Entry);
    return true;
}

/**
 * 完整预检清单后逐个生成/更新 PNG 纹理；仅游戏线程、非 PIE，保留原 RGBA，不执行解包或改变 alpha 表示。
 * @param ManifestFilePath 用户清单路径，规则与预览一致；生成独立重读，不信任界面缓存。
 * @param OutResult 输出已实际保存的资产及说明，某个保存失败时保留之前成功列表，并明确部分完成。
 * @return 所有资产成功保存时 true；输入预检失败时零写入，保存阶段失败不回滚其他已成功资产。
 */
bool ULuaGameplayUIImportLibrary::ImportUITextureManifest(const FString& ManifestFilePath, FLuaUITextureImportResult& OutResult)
{
    OutResult = FLuaUITextureImportResult();
    TArray<LuaUIImport::FPreparedTexture> Prepared;
    if (!LuaUIImport::PrepareManifest(ManifestFilePath, Prepared, OutResult.Message)) return false;
    for (const LuaUIImport::FPreparedTexture& Texture : Prepared)
    {
        FString ObjectPath;
        FString Error;
        if (!LuaUIImport::SaveTexture(Texture, ObjectPath, Error))
        {
            OutResult.Message = FString::Printf(TEXT("已保存 %d/%d 个纹理，导入停止：%s"), OutResult.ImportedCount, Prepared.Num(), *Error);
            return false;
        }
        OutResult.ImportedAssetPaths.Add(ObjectPath);
        ++OutResult.ImportedCount;
    }
    OutResult.Message = FString::Printf(TEXT("成功导入 %d 个 UI 纹理；RGBA 原样保留，未生成 Widget 或材质。"), OutResult.ImportedCount);
    return true;
}
