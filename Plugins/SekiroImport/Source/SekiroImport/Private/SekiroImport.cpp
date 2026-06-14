#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "SekiroImportUI.h"
#include "SekiroImportPipeline.h"
#include "SekiroImportSettings.h"
#include "SekiroAnimBlueprintBuilder.h"
#include "SekiroTAEImporter.h"
#include "SekiroAnimationNameMap.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "FSekiroImportModule"

// 文件作用域控制台命令（DLL生命周期内有效）
static FAutoConsoleCommand GSekiroRunPipelineCmd(
    TEXT("SekiroImport.Run"),
    TEXT("Run the full SekiroImport pipeline. Usage: SekiroImport.Run <ModelJson> <AnimJson> [OutputBasePath]"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 2)
        {
            UE_LOG(LogSekiroImport, Error, TEXT("Usage: SekiroImport.Run <ModelJson> <AnimJson> [OutputBasePath]"));
            return;
        }

        USekiroImportSettings* Settings = NewObject<USekiroImportSettings>();
        Settings->ModelJsonPath = Args[0];
        Settings->AnimationJsonPath = Args[1];
        Settings->OutputBasePath = Args.Num() >= 3 ? Args[2] : TEXT("/Game/Characters/Sekiro");
        Settings->SkeletonName = TEXT("Sekiro_Skeleton");

        UE_LOG(LogSekiroImport, Log, TEXT("SekiroImport.Run: Starting full pipeline..."));
        FSekiroImportPipeline::FImportResult Result = FSekiroImportPipeline::Run(*Settings);
        UE_LOG(LogSekiroImport, Log, TEXT("SekiroImport.Run: Pipeline complete — %d materials, %d errors"),
            Result.Materials.Num(), Result.Errors.Num());
    })
);

// ============================================================================
// AnimBlueprint 构建命令
// ============================================================================

// 辅助：尝试加载骨架（不依赖AssetRegistry也可以工作的方式）
static USkeleton* TryLoadSkeleton(const FString& SkeletonPath)
{
    // 方式1: LoadPackage 直接从磁盘加载（最可靠，不依赖AssetRegistry）
    UPackage* Pkg = LoadPackage(nullptr, *SkeletonPath, LOAD_None);
    if (Pkg)
    {
        // 按类型搜索包内对象（对象名可能与资产名不同）
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Pkg, Objects, false);
        for (UObject* Obj : Objects)
        {
            if (USkeleton* Skel = Cast<USkeleton>(Obj))
            {
                UE_LOG(LogSekiroImport, Log, TEXT("[TryLoadSkeleton] Found Skeleton '%s' in package"), *Skel->GetName());
                return Skel;
            }
        }
        UE_LOG(LogSekiroImport, Warning, TEXT("[TryLoadSkeleton] LoadPackage OK but no USkeleton found in package (object count: %d)"), Objects.Num());
    }
    else
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("[TryLoadSkeleton] LoadPackage returned null for: %s"), *SkeletonPath);
    }

    // 方式2: StaticLoadObject（需要引擎已挂载路径）
    USkeleton* Skel = Cast<USkeleton>(StaticLoadObject(USkeleton::StaticClass(), nullptr, *SkeletonPath));
    if (Skel)
    {
        UE_LOG(LogSekiroImport, Log, TEXT("[TryLoadSkeleton] StaticLoadObject OK"));
        return Skel;
    }

    // 方式3: AssetRegistry（需要扫描完成）
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FAssetData Asset = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(SkeletonPath));
    if (Asset.IsValid())
    {
        USkeleton* ARSkel = Cast<USkeleton>(Asset.GetAsset());
        if (ARSkel) return ARSkel;
        // AssetRegistry 找到索引但 GetAsset 失败时，也回退到按类型遍历
        if (UPackage* ARPkg = FindPackage(nullptr, *SkeletonPath))
        {
            TArray<UObject*> Objects;
            GetObjectsWithOuter(ARPkg, Objects, false);
            for (UObject* Obj : Objects)
            {
                if (USkeleton* Found = Cast<USkeleton>(Obj))
                    return Found;
            }
        }
    }
    return nullptr;
}

static FAutoConsoleCommand GSekiroBuildABPCmd(
    TEXT("SekiroImport.BuildAnimBlueprint"),
    TEXT("Build/update AnimBlueprint from TAE JSON. Usage: SekiroImport.BuildAnimBlueprint <JsonPath> [OutputBasePath]"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 1)
        {
            UE_LOG(LogSekiroImport, Error, TEXT("Usage: SekiroImport.BuildAnimBlueprint <JsonPath> [OutputBasePath]"));
            UE_LOG(LogSekiroImport, Error, TEXT("  Example: SekiroImport.BuildAnimBlueprint F:/ProjectAI/Sekiro/Extracted/Sekiro_TAE_Logic.json"));
            return;
        }

        FString JsonPath = Args[0];
        FString OutputBasePath = Args.Num() >= 2 ? Args[1] : TEXT("/Game/Characters/Sekiro");

        // 1. 加载 TAE JSON → ABIR（与引擎状态无关，立即执行）
        UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Loading TAE JSON: %s"), *JsonPath);
        FSKAnimLogicImportResult IR = FSekiroTAEImporter::ImportFromFile(JsonPath);
        if (IR.AnimLogicMap.Num() == 0)
        {
            UE_LOG(LogSekiroImport, Error, TEXT("[BuildABP] Failed to parse TAE JSON or empty result"));
            return;
        }
        UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Parsed %d animations, %d events"),
            IR.TotalAnims, IR.TotalEvents);

        // 2. 延迟调度：等引擎就绪后执行资产相关操作
        FString SkeletonPath = FString::Printf(TEXT("%s/Sekiro_Skeleton"), *OutputBasePath);
        TSharedPtr<int32> RetryCount = MakeShared<int32>(0);

        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([JsonPath, OutputBasePath, SkeletonPath, IR = MoveTemp(IR), RetryCount](float Delta) mutable -> bool
            {
                (*RetryCount)++;

                // 先检查 AssetRegistry 是否还在扫描（首次启动会花几秒）
                FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
                if (AssetRegistryModule.Get().IsLoadingAssets())
                {
                    if (*RetryCount < 600) // 等待最多~20秒
                        return true;
                    UE_LOG(LogSekiroImport, Error, TEXT("[BuildABP] Timed out waiting for AssetRegistry scan"));
                    return false;
                }

                // 3. 加载骨架
                USkeleton* Skeleton = TryLoadSkeleton(SkeletonPath);
                if (!Skeleton)
                {
                    if (*RetryCount < 600) // 重试最多~20秒
                        return true;
                    UE_LOG(LogSekiroImport, Error, TEXT("[BuildABP] Skeleton not found after %d retries: %s"), *RetryCount, *SkeletonPath);
                    return false;
                }
                UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Skeleton loaded (retry %d): %s"), *RetryCount, *Skeleton->GetName());

                // 3b. 确保骨架设置了预览网格体
                if (!Skeleton->GetPreviewMesh())
                {
                    FString MeshPath = FString::Printf(TEXT("%s/Sekiro_SkeletalMesh"), *OutputBasePath);
                    USkeletalMesh* PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
                    if (PreviewMesh)
                    {
                        Skeleton->SetPreviewMesh(PreviewMesh);
                        UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Set preview mesh on skeleton: %s"), *MeshPath);
                    }
                }

                // 4. 扫描 Animations 文件夹（复用上面的 AssetRegistryModule）
                FString AnimFolder = FString::Printf(TEXT("%s/Animations"), *OutputBasePath);
                TMap<FString, UAnimSequence*> NameToSequence;

                TArray<FAssetData> AssetList;
                FARFilter Filter;
                Filter.PackagePaths.Add(*AnimFolder);
                Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
                Filter.bRecursivePaths = false;
                AssetRegistryModule.Get().GetAssets(Filter, AssetList);

                for (const FAssetData& Asset : AssetList)
                {
                    UAnimSequence* Seq = Cast<UAnimSequence>(Asset.GetAsset());
                    if (Seq)
                    {
                        NameToSequence.Add(Asset.AssetName.ToString(), Seq);
                    }
                }
                UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Found %d anim sequences in %s"), NameToSequence.Num(), *AnimFolder);

                // 5. 建立 AnimID → UAnimSequence 映射
                TMap<int32, UAnimSequence*> AnimSequences;
                int32 MappedCount = 0;
                for (const auto& Pair : IR.AnimLogicMap)
                {
                    int32 AnimID = Pair.Key;
                    const FString& AnimName = Pair.Value.AnimName;
                    FString AssetName = FString::Printf(TEXT("Anim_%s"), *AnimName);

                    UAnimSequence** Found = NameToSequence.Find(AssetName);
                    if (Found && *Found)
                    {
                        AnimSequences.Add(AnimID, *Found);
                        MappedCount++;
                    }
                }
                UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] Matched %d/%d animations to existing sequences"),
                    MappedCount, IR.AnimLogicMap.Num());

                // 6. 运行 Builder
                FSekiroAnimBlueprintBuilder::FBuildResult Result = FSekiroAnimBlueprintBuilder::Build(
                    IR, Skeleton, AnimSequences, OutputBasePath);

                if (Result.bSuccess)
                {
                    UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] SUCCESS: States=%d Transitions=%d Notifies=%d"),
                        Result.StateCount, Result.TransitionCount, Result.NotifyCount);
                    UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] AnimBP: %s"), *Result.AnimBlueprintPath);
                    UE_LOG(LogSekiroImport, Log, TEXT("[BuildABP] DataAsset: %s"), *Result.DataAssetPath);
                }
                else
                {
                    UE_LOG(LogSekiroImport, Error, TEXT("[BuildABP] Build FAILED"));
                }

                return false; // 停止 ticker
            }),
            0.05f // 延迟50ms开始第一次尝试
        );
    })
);

// ============================================================================
// 模块生命周期
// ============================================================================

void FSekiroImportModule::StartupModule()
{
    UE_LOG(LogSekiroImport, Log, TEXT("SekiroImport 插件已加载"));

    // 加载MaterialEditor模块（供SekiroMaterialUtils使用）
    FModuleManager::Get().LoadModule(TEXT("MaterialEditor"));

    RegisterMenus();
}

void FSekiroImportModule::ShutdownModule()
{
    UnregisterMenus();
    UE_LOG(LogSekiroImport, Log, TEXT("SekiroImport 插件已卸载"));
}

// ============================================================================
// 打开对话框
// ============================================================================

void FSekiroImportModule::OpenImportDialog()
{
    SSekiroImportDialog::OpenModal();
}

UPackage* FSekiroImportModule::CreatePackageForOverwrite(const FString& PackagePath)
{
    // 删除旧.uasset文件，避免SavePackage因"部分加载"拒绝覆盖
    FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (FPaths::FileExists(FilePath))
    {
        IFileManager::Get().Delete(*FilePath);
    }

    // 清除内存中残留Package的旧对象（避免NewObject同名冲突）和Linker（避免SavePackage拒绝覆盖）
    UPackage* StalePackage = FindPackage(nullptr, *PackagePath);
    if (StalePackage)
    {
        // 旧UObject移到瞬态包，释放同名槽位，由GC延迟清理
        TArray<UObject*> ObjectsInPackage;
        GetObjectsWithOuter(StalePackage, ObjectsInPackage, false);
        for (UObject* Obj : ObjectsInPackage)
        {
            Obj->ClearFlags(RF_Standalone | RF_Public);
            Obj->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
        }

        ResetLoaders(StalePackage);
        StalePackage->ClearFlags(RF_WasLoaded);
    }

    return CreatePackage(*PackagePath);
}

// ============================================================================
// 菜单注册
// ============================================================================

void FSekiroImportModule::PopulateSekiroMenu(UToolMenu* Menu)
{
    FToolMenuSection& Section = Menu->FindOrAddSection("SekiroImport");
    Section.AddMenuEntry(
        "ImportAssets",
        LOCTEXT("SekiroImportMenu", "Import Assets..."),
        LOCTEXT("SekiroImportMenuTooltip", "从JSON文件导入Sekiro模型、骨骼和动画资产"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateStatic(&FSekiroImportModule::OpenImportDialog))
    );
}

void FSekiroImportModule::RegisterMenus()
{
    // Commandlet模式无UI，跳过菜单注册避免Slate断言崩溃
    if (IsRunningCommandlet())
        return;

    // 注册顶层菜单
    UToolMenus::Get()->RegisterMenu(
        "LevelEditor.MainMenu.SekiroImport",
        FName("SekiroImport"),
        EMultiBoxType::Menu,
        false
    );

    // 添加到主菜单栏 (与 File/Edit/Window 同级)
    {
        UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
        FToolMenuSection& Section = MainMenu->FindOrAddSection("SekiroImportSection");
        Section.AddSubMenu(
            "SekiroImport",
            LOCTEXT("SekiroImportMenuBar", "Sekiro"),
            LOCTEXT("SekiroImportMenuBarTooltip", "Sekiro导入工具"),
            FNewToolMenuDelegate::CreateStatic(&FSekiroImportModule::PopulateSekiroMenu)
        );
    }

    // Content Browser文件夹右键菜单
    {
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.FolderContextMenu");
        FToolMenuSection& Section = Menu->FindOrAddSection("SekiroImportSection");

        Section.AddMenuEntry(
            "SekiroImport",
            LOCTEXT("SekiroImportContentBrowser", "Import Sekiro Assets..."),
            LOCTEXT("SekiroImportContentBrowserTooltip", "从JSON文件导入Sekiro模型、骨骼和动画资产到当前目录"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.SkeletalMesh"),
            FUIAction(FExecuteAction::CreateStatic(&FSekiroImportModule::OpenImportDialog))
        );
    }

    UE_LOG(LogSekiroImport, Log, TEXT("菜单已注册: 顶部Sekiro菜单, Content Browser右键菜单"));
}

void FSekiroImportModule::UnregisterMenus()
{
    if (UToolMenus* ToolMenus = UToolMenus::TryGet())
    {
        ToolMenus->RemoveSection("ContentBrowser.FolderContextMenu", "SekiroImportSection");
    }
}

IMPLEMENT_MODULE(FSekiroImportModule, SekiroImport)

#undef LOCTEXT_NAMESPACE
