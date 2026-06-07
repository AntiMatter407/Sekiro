#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "SekiroImportUI.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "FSekiroImportModule"

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
