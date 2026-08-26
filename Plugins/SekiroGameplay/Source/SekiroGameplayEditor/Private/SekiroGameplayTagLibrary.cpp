#include "SekiroGameplayTagLibrary.h"

#include "SekiroGameplayTagParser.h"
#include "Containers/StringConv.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SekiroGameplayTagAssets
{
    const TCHAR* GeneratorKey = TEXT("SekiroGameplay.Generator");
    const TCHAR* GeneratorValue = TEXT("LuaGameplayTags.v1");
    const TCHAR* SourceKey = TEXT("SekiroGameplay.LuaSource");

    /** 规范化来源路径；游戏线程。Path 可相对工程目录，返回绝对规范路径，不创建文件。 */
    FString NormalizeSource(const FString& Path)
    {
        FString Result = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
        FPaths::NormalizeFilename(Result);
        FPaths::CollapseRelativeDirectories(Result);
        return Result;
    }

    /** 保存可迁移来源标识；游戏线程。AbsolutePath 已规范化，工程内转为相对路径，外部文件保持绝对路径。 */
    FString MakeSourceIdentity(const FString& AbsolutePath)
    {
        FString Relative = AbsolutePath;
        const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        if (FPaths::MakePathRelativeTo(Relative, *ProjectRoot) && !Relative.StartsWith(TEXT("../")) && FPaths::IsRelative(Relative)) return Relative;
        return AbsolutePath;
    }

    /** 校验文件字节为严格 UTF-8，拒绝 NUL、过长编码和代理码点；任意线程。Bytes 为原始字节，返回编码是否合法。 */
    bool IsValidUtf8(const TArray<uint8>& Bytes)
    {
        for (int32 Index = 0; Index < Bytes.Num();)
        {
            const uint8 First = Bytes[Index++];
            if (First == 0) return false;
            if (First < 0x80) continue;
            int32 Continuations = 0;
            uint32 CodePoint = 0;
            uint32 Minimum = 0;
            if (First >= 0xC2 && First <= 0xDF) { Continuations = 1; CodePoint = First & 0x1F; Minimum = 0x80; }
            else if (First >= 0xE0 && First <= 0xEF) { Continuations = 2; CodePoint = First & 0x0F; Minimum = 0x800; }
            else if (First >= 0xF0 && First <= 0xF4) { Continuations = 3; CodePoint = First & 0x07; Minimum = 0x10000; }
            else return false;
            if (Index + Continuations > Bytes.Num()) return false;
            for (int32 Count = 0; Count < Continuations; ++Count)
            {
                const uint8 NextByte = Bytes[Index++];
                if ((NextByte & 0xC0) != 0x80) return false;
                CodePoint = (CodePoint << 6) | (NextByte & 0x3F);
            }
            if (CodePoint < Minimum || CodePoint > 0x10FFFF || (CodePoint >= 0xD800 && CodePoint <= 0xDFFF)) return false;
        }
        return true;
    }

    /** 检查编辑器调用上下文；游戏线程以外也可调用但会返回错误。OutError 为失败原因；不启动或停止 PIE。 */
    bool CheckEditorContext(FString& OutError)
    {
        if (!IsInGameThread()) { OutError = TEXT("Gameplay 工具只能在游戏线程调用"); return false; }
        if (!GIsEditor || (GEditor && GEditor->PlayWorld)) { OutError = TEXT("只能在非 PIE 的编辑器上下文生成或预览"); return false; }
        return true;
    }

    /** 预检文件可写性，不自动签出或解锁；游戏线程。Filename 为绝对目标，OutError 为失败原因，返回是否允许继续。 */
    bool CheckWritable(const FString& Filename, FString& OutError)
    {
        IFileManager& FileManager = IFileManager::Get();
        if (FileManager.FileExists(*Filename) && FileManager.IsReadOnly(*Filename))
        {
            OutError = TEXT("目标文件只读，请先签出或解除只读：") + Filename;
            return false;
        }
        if (ISourceControlModule::Get().IsEnabled())
        {
            ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
            FSourceControlStatePtr State = Provider.GetState(Filename, EStateCacheUsage::Use);
            if (State.IsValid() && (State->IsCheckedOutOther() || State->CanCheckout()))
            {
                OutError = TEXT("目标文件受版本控制保护，请先完成签出：") + Filename;
                return false;
            }
        }
        return true;
    }

    /** 判定一行是否定义目标数组；任意线程。Line 是去除首尾空白后的 INI 行，返回是否应替换，忽略注释。 */
    bool IsTagTableSetting(const FString& Line)
    {
        int32 EqualIndex = INDEX_NONE;
        if (!Line.FindChar('=', EqualIndex)) return false;
        FString Key = Line.Left(EqualIndex).TrimStartAndEnd();
        if (!Key.IsEmpty() && (Key[0] == '+' || Key[0] == '-' || Key[0] == '!' || Key[0] == '.')) Key.RightChopInline(1);
        return Key.Equals(TEXT("GameplayTagTableList"), ESearchCase::IgnoreCase);
    }

    /**
     * 只替换设置节中的标签表数组，保留其他属性原文；游戏线程，不自动签出。
     * @param TablePath 已保存资产的软对象路径，加入时保留当前有效配置中的其他表。
     * @param OutError 失败说明；失败不更新设置 CDO 或标签树。资产文件不在本函数回滚范围。
     * @return 配置持久化成功时 true；false 可能发生于资产已经保存之后。
     */
    bool RegisterTableSource(const FSoftObjectPath& TablePath, FString& OutError)
    {
        UGameplayTagsSettings* Settings = GetMutableDefault<UGameplayTagsSettings>();
        const FString ConfigFilename = FPaths::ConvertRelativePathToFull(Settings->GetDefaultConfigFilename());
        if (!CheckWritable(ConfigFilename, OutError)) return false;
        FString Existing;
        if (IFileManager::Get().FileExists(*ConfigFilename) && !FFileHelper::LoadFileToString(Existing, *ConfigFilename))
        {
            OutError = TEXT("无法读取 GameplayTags 默认配置：") + ConfigFilename;
            return false;
        }
        TArray<FSoftObjectPath> Desired = Settings->GameplayTagTableList;
        Desired.AddUnique(TablePath);
        const FString TargetSection = TEXT("[/Script/GameplayTags.GameplayTagsSettings]");
        TArray<FString> Lines;
        Existing.ParseIntoArrayLines(Lines, false);
        TArray<FString> Updated;
        bool bInsideTarget = false;
        bool bInserted = false;
        for (const FString& Line : Lines)
        {
            const FString Trimmed = Line.TrimStartAndEnd();
            if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
            {
                bInsideTarget = Trimmed.Equals(TargetSection, ESearchCase::IgnoreCase);
                Updated.Add(Line);
                if (bInsideTarget && !bInserted)
                {
                    Updated.Add(TEXT("!GameplayTagTableList=ClearArray"));
                    for (const FSoftObjectPath& Path : Desired) Updated.Add(TEXT("+GameplayTagTableList=") + Path.ToString());
                    bInserted = true;
                }
                continue;
            }
            if (!bInsideTarget || !IsTagTableSetting(Trimmed)) Updated.Add(Line);
        }
        if (!bInserted)
        {
            Updated.Add(TargetSection);
            Updated.Add(TEXT("!GameplayTagTableList=ClearArray"));
            for (const FSoftObjectPath& Path : Desired) Updated.Add(TEXT("+GameplayTagTableList=") + Path.ToString());
        }
        const FString Content = FString::Join(Updated, TEXT("\r\n")) + TEXT("\r\n");
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString TempFilename = ConfigFilename + TEXT(".") + Token + TEXT(".tmp");
        const FString BackupFilename = ConfigFilename + TEXT(".") + Token + TEXT(".bak");
        const bool bHadConfig = IFileManager::Get().FileExists(*ConfigFilename);
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigFilename), true)
            || !FFileHelper::SaveStringToFile(Content, *TempFilename, FFileHelper::EEncodingOptions::ForceUTF8))
        {
            IFileManager::Get().Delete(*TempFilename, false, false, true);
            OutError = TEXT("无法写入 GameplayTags 配置临时文件：") + ConfigFilename;
            return false;
        }
        if (bHadConfig && IFileManager::Get().Copy(*BackupFilename, *ConfigFilename, false, false) != COPY_OK)
        {
            IFileManager::Get().Delete(*TempFilename, false, false, true);
            OutError = TEXT("无法备份 GameplayTags 配置，未替换原文件：") + ConfigFilename;
            return false;
        }
        const bool bMoved = IFileManager::Get().Move(*ConfigFilename, *TempFilename, true, false, false, true);
        FString Verified;
        const bool bVerified = bMoved && FFileHelper::LoadFileToString(Verified, *ConfigFilename) && Verified == Content;
        if (!bVerified)
        {
            IFileManager::Get().Delete(*TempFilename, false, false, true);
            // UE5.2 Move 会先删目标，不能声称原子替换；替换或校验失败时显式恢复备份。
            const bool bRestored = bHadConfig
                ? IFileManager::Get().Copy(*ConfigFilename, *BackupFilename, true, false) == COPY_OK
                : IFileManager::Get().Delete(*ConfigFilename, false, false, true);
            if (bRestored) IFileManager::Get().Delete(*BackupFilename, false, false, true);
            OutError = bRestored ? TEXT("配置写入失败，原配置已恢复：") + ConfigFilename
                : TEXT("配置写入及自动恢复均失败，请从备份手动恢复：") + BackupFilename;
            return false;
        }
        IFileManager::Get().Delete(*BackupFilename, false, false, true);
        Settings->GameplayTagTableList = Desired;
        // UE5.2 的 UpdateSinglePropertyInConfigFile 不支持数组，显式同步已合并的运行配置缓存。
        TArray<FString> Paths;
        for (const FSoftObjectPath& Path : Desired) Paths.Add(Path.ToString());
        const FString ConfigName = Settings->GetClass()->GetConfigName();
        GConfig->SetArray(TEXT("/Script/GameplayTags.GameplayTagsSettings"), TEXT("GameplayTagTableList"), Paths, ConfigName);
        GConfig->UnloadFile(ConfigFilename);
        return true;
    }
}

/**
 * 读取 Lua 文件并返回只读预览；仅游戏线程且禁止 PIE，不执行 Lua，不创建资产。
 * @param LuaFilePath 用户选择的 .lua 文件，可为绝对路径或相对工程目录。
 * @param OutTags 输出完整排序标签，失败清空。
 * @param OutError 输出上下文、文件或解析错误，成功为空。
 * @return 全部输入通过校验时 true。
 */
bool USekiroGameplayTagLibrary::PreviewGameplayTags(const FString& LuaFilePath, TArray<FSekiroGameplayTagEntry>& OutTags, FString& OutError)
{
    OutTags.Reset();
    OutError.Reset();
    if (!SekiroGameplayTagAssets::CheckEditorContext(OutError)) return false;
    const FString Filename = SekiroGameplayTagAssets::NormalizeSource(LuaFilePath);
    const int64 FileSize = IFileManager::Get().FileSize(*Filename);
    if (LuaFilePath.IsEmpty() || !FPaths::GetExtension(Filename).Equals(TEXT("lua"), ESearchCase::IgnoreCase) || FileSize < 0 || FileSize > 1024 * 1024)
    {
        OutError = TEXT("请选择存在且不超过 1 MiB 的 .lua 文件：") + Filename;
        return false;
    }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename)) { OutError = TEXT("读取 Lua 文件失败：") + Filename; return false; }
    if (Bytes.Num() > 1024 * 1024 || !SekiroGameplayTagAssets::IsValidUtf8(Bytes))
    {
        OutError = Filename + TEXT(":1:1: 文件必须是无 NUL 的有效 UTF-8，且不超过 1 MiB");
        return false;
    }
    const int32 Start = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF ? 3 : 0;
    Bytes.Add(0);
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Start), Bytes.Num() - Start - 1);
    const FString Source(Converted.Length(), Converted.Get());
    return FSekiroGameplayTagParser::Parse(Source, Filename, OutTags, OutError);
}

/**
 * 从声明式 Lua 创建或更新自有 GameplayTag DataTable 并可选注册标签源；仅游戏线程、非 PIE。
 * @param LuaFilePath 来源 .lua 文件；既有资产必须由本工具且同一规范化来源生成。
 * @param AssetPackagePath 用户指定的 /Game/ 长包名，不接受对象路径或扩展名。
 * @param bRegisterTagSource 是否更新项目标签表列表；false 只保存资产，不主动将其加入字典。
 * @param OutResult 输出资产保存与注册的独立状态，任何失败均包含明确原因。
 * @return 所有请求步骤成功时 true。保存失败恢复表内存；注册失败不删除已保存资产，返回 false 和部分结果。
 */
bool USekiroGameplayTagLibrary::GenerateGameplayTagTable(const FString& LuaFilePath, const FString& AssetPackagePath, bool bRegisterTagSource, FSekiroGameplayTagGenerationResult& OutResult)
{
    OutResult = FSekiroGameplayTagGenerationResult();
    TArray<FSekiroGameplayTagEntry> Tags;
    if (!PreviewGameplayTags(LuaFilePath, Tags, OutResult.Message)
        || !FSekiroGameplayTagParser::ValidateAssetPackagePath(AssetPackagePath, OutResult.Message)) return false;
    OutResult.TagCount = Tags.Num();
    const FString SourcePath = SekiroGameplayTagAssets::NormalizeSource(LuaFilePath);
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPackagePath);
    OutResult.AssetObjectPath = AssetPackagePath + TEXT(".") + AssetName;
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPackagePath, Filename, FPackageName::GetAssetPackageExtension()))
    {
        OutResult.Message = TEXT("无法将资产包名转换为磁盘路径");
        return false;
    }
    Filename = FPaths::ConvertRelativePathToFull(Filename);
    if (!SekiroGameplayTagAssets::CheckWritable(Filename, OutResult.Message)) return false;
    const bool bPackageOnDisk = FPackageName::DoesPackageExist(AssetPackagePath);
    UObject* ExistingObject = FindObject<UObject>(nullptr, *OutResult.AssetObjectPath);
    if (!ExistingObject && bPackageOnDisk) ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *OutResult.AssetObjectPath);
    UDataTable* Table = Cast<UDataTable>(ExistingObject);
    UPackage* ExistingPackage = FindPackage(nullptr, *AssetPackagePath);
    if ((ExistingObject && !Table) || (!Table && (bPackageOnDisk || ExistingPackage)))
    {
        OutResult.Message = TEXT("目标已存在且不是本工具可管理的数据表，拒绝覆盖");
        return false;
    }
    if (Table)
    {
        UMetaData* Metadata = Table->GetOutermost()->GetMetaData();
        if (Table->GetRowStruct() != FGameplayTagTableRow::StaticStruct()
            || Metadata->GetValue(Table, SekiroGameplayTagAssets::GeneratorKey) != SekiroGameplayTagAssets::GeneratorValue
            || !FPaths::IsSamePath(SekiroGameplayTagAssets::NormalizeSource(Metadata->GetValue(Table, SekiroGameplayTagAssets::SourceKey)), SourcePath))
        {
            OutResult.Message = TEXT("拒绝覆盖：目标必须是本工具从同一 Lua 来源生成的 GameplayTag 数据表");
            return false;
        }
        if (Table->GetOutermost()->IsDirty())
        {
            OutResult.Message = TEXT("目标包有未保存修改，请先保存或撤销后再生成");
            return false;
        }
    }
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
    {
        OutResult.Message = TEXT("无法创建目标资产目录：") + Filename;
        return false;
    }
    const bool bNewAsset = !Table;
    UPackage* Package = Table ? Table->GetOutermost() : CreatePackage(*AssetPackagePath);
    TMap<FName, FGameplayTagTableRow> PreviousRows;
    if (Table)
    {
        for (const TPair<FName, uint8*>& Pair : Table->GetRowMap()) PreviousRows.Add(Pair.Key, *reinterpret_cast<const FGameplayTagTableRow*>(Pair.Value));
    }
    else
    {
        Table = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        Table->RowStruct = FGameplayTagTableRow::StaticStruct();
    }
    Table->EmptyTable();
    for (const FSekiroGameplayTagEntry& Entry : Tags)
    {
        FGameplayTagTableRow Row;
        Row.Tag = FName(*Entry.Tag);
        Row.DevComment = Entry.Comment;
        Table->AddRow(Row.Tag, Row);
    }
    UMetaData* Metadata = Package->GetMetaData();
    const FString PreviousGenerator = Metadata->GetValue(Table, SekiroGameplayTagAssets::GeneratorKey);
    const FString PreviousSource = Metadata->GetValue(Table, SekiroGameplayTagAssets::SourceKey);
    Metadata->SetValue(Table, SekiroGameplayTagAssets::GeneratorKey, SekiroGameplayTagAssets::GeneratorValue);
    Metadata->SetValue(Table, SekiroGameplayTagAssets::SourceKey, *SekiroGameplayTagAssets::MakeSourceIdentity(SourcePath));
    Package->MarkPackageDirty();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Table, *Filename, SaveArgs))
    {
        Table->EmptyTable();
        for (const TPair<FName, FGameplayTagTableRow>& Pair : PreviousRows) Table->AddRow(Pair.Key, Pair.Value);
        if (bNewAsset)
        {
            Metadata->RemoveValue(Table, SekiroGameplayTagAssets::GeneratorKey);
            Metadata->RemoveValue(Table, SekiroGameplayTagAssets::SourceKey);
            Table->ClearFlags(RF_Public | RF_Standalone);
            Table->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Package->Rename(*FString::Printf(TEXT("/Temp/SekiroGameplayFailed_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
        }
        else
        {
            Metadata->SetValue(Table, SekiroGameplayTagAssets::GeneratorKey, *PreviousGenerator);
            Metadata->SetValue(Table, SekiroGameplayTagAssets::SourceKey, *PreviousSource);
        }
        Package->SetDirtyFlag(false);
        OutResult.Message = TEXT("资产保存失败；已恢复原表内存，请检查磁盘和权限：") + Filename;
        return false;
    }
    Package->SetDirtyFlag(false);
    OutResult.bAssetSaved = true;
    if (bNewAsset) FAssetRegistryModule::AssetCreated(Table);
    if (bRegisterTagSource)
    {
        if (!SekiroGameplayTagAssets::RegisterTableSource(FSoftObjectPath(Table), OutResult.Message))
        {
            OutResult.Message = TEXT("数据表已保存，但标签源注册失败：") + OutResult.Message;
            return false;
        }
        OutResult.bSourceRegistered = true;
    }
    UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();
    OutResult.Message = FString::Printf(TEXT("已保存 %d 个标签到 %s%s"), Tags.Num(), *OutResult.AssetObjectPath,
        bRegisterTagSource ? TEXT("，已注册并刷新标签字典") : TEXT("（未请求注册标签源）"));
    return true;
}
