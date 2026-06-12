#include "Tools/USKAssetTool.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Factories/PhysicsAssetFactory.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/SkeletalMesh.h"
#include "AssetImportTask.h"
#include "Factories/TextureFactory.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

FString USKAssetTool::GetToolDescription() const
{
    return TEXT("资产CRUD操作：列出目录资产、查询资产信息、创建、删除、复制、重命名、保存资产。支持创建PhysicsAsset。");
}

FString USKAssetTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"list\",\"info\",\"exists\",\"create\",\"create_physics_asset\",\"delete\",\"duplicate\",\"rename\",\"save\",\"import_file\"]},\"path\":{\"type\":\"string\",\"description\":\"Asset path\"},\"source_file\":{\"type\":\"string\",\"description\":\"Source file path for import_file\"},\"skeletal_mesh_path\":{\"type\":\"string\",\"description\":\"SkeletalMesh path for create_physics_asset\"},\"destination\":{\"type\":\"string\",\"description\":\"Target path\"},\"recursive\":{\"type\":\"boolean\",\"default\":false}},\"required\":[\"action\",\"path\"]}");
}

bool USKAssetTool::RequiresConfirmation() const
{
    return true;
}

FString USKAssetTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Action = ArgsObj->GetStringField(TEXT("action"));
        FString Path = ArgsObj->GetStringField(TEXT("path"));
        return FString::Printf(TEXT("资产操作: %s -> %s"), *Action, *Path);
    }
    return TEXT("资产操作");
}

bool USKAssetTool::IsDangerousAction(const FString& Action) const
{
    return Action == TEXT("delete") || Action == TEXT("rename") || Action == TEXT("create");
}

FString USKAssetTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Action;
    if (!ArgsObj->TryGetStringField(TEXT("action"), Action))
    {
        OutError = TEXT("缺少 action 参数");
        return FString();
    }

    if (Action == TEXT("list"))      return HandleList(ArgsObj, OutError);
    if (Action == TEXT("info"))      return HandleInfo(ArgsObj, OutError);
    if (Action == TEXT("exists"))    return HandleExists(ArgsObj, OutError);
    if (Action == TEXT("create"))    return HandleCreate(ArgsObj, OutError);
    if (Action == TEXT("create_physics_asset")) return HandleCreatePhysicsAsset(ArgsObj, OutError);
    if (Action == TEXT("delete"))    return HandleDelete(ArgsObj, OutError);
    if (Action == TEXT("duplicate")) return HandleDuplicate(ArgsObj, OutError);
    if (Action == TEXT("rename"))    return HandleRename(ArgsObj, OutError);
    if (Action == TEXT("save"))        return HandleSave(ArgsObj, OutError);
    if (Action == TEXT("import_file")) return HandleImportFile(ArgsObj, OutError);

    OutError = FString::Printf(TEXT("未知操作: %s"), *Action);
    return FString();
}

FString USKAssetTool::HandleList(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString DirectoryPath = Args->GetStringField(TEXT("path"));
    bool bRecursive = false;
    Args->TryGetBoolField(TEXT("recursive"), bRecursive);

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*DirectoryPath));
    Filter.bRecursivePaths = bRecursive;

    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    TArray<FAssetData> AssetList;
    AssetRegistry.GetAssets(Filter, AssetList);

    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    for (const FAssetData& Asset : AssetList)
    {
        TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject());
        AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        AssetObj->SetStringField(TEXT("path"), Asset.PackageName.ToString());
        AssetObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("directory"), DirectoryPath);
    ResultObj->SetNumberField(TEXT("count"), AssetList.Num());
    ResultObj->SetArrayField(TEXT("assets"), AssetsArray);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));

    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("资产未找到: %s"), *AssetPath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
    ResultObj->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
    ResultObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
    ResultObj->SetStringField(TEXT("packageName"), AssetData.PackageName.ToString());

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleExists(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    bool bExists = UEditorAssetLibrary::DoesAssetExist(AssetPath);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetBoolField(TEXT("exists"), bExists);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
	FString AssetPath = Args->GetStringField(TEXT("path"));
	FString ClassName;
	Args->TryGetStringField(TEXT("class"), ClassName);

	if (ClassName.IsEmpty())
	{
		ClassName = TEXT("DataAsset");
	}

	UClass* AssetClass = UClass::TryFindTypeSlow<UClass>(*ClassName);
	if (!AssetClass)
	{
		// 尝试查找Blueprint类
		FString ClassPath = FString::Printf(TEXT("Class'/Script/Engine.%s'"), *ClassName);
		AssetClass = LoadObject<UClass>(nullptr, *ClassPath);
	}

	if (!AssetClass)
	{
		// 尝试Blueprint
		AssetClass = LoadObject<UClass>(nullptr, *ClassName);
	}

	if (!AssetClass)
	{
		OutError = FString::Printf(TEXT("类未找到: %s"), *ClassName);
		return FString();
	}

	// 解析路径和名称
	int32 LastSlash;
	FString PackagePath;
	FString AssetName;
	if (AssetPath.FindLastChar('/', LastSlash))
	{
		PackagePath = AssetPath.Left(LastSlash);
		AssetName = AssetPath.RightChop(LastSlash + 1);
	}
	else
	{
		OutError = TEXT("无效的资产路径格式");
		return FString();
	}

	// UE5.2: 使用 IAssetTools::CreateAsset（UEditorAssetLibrary 无 CreateAsset）
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, AssetClass, nullptr);
	if (!NewAsset)
	{
		OutError = FString::Printf(TEXT("创建资产失败: %s"), *AssetPath);
		return FString();
	}

	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
	ResultObj->SetStringField(TEXT("path"), AssetPath);
	ResultObj->SetStringField(TEXT("class"), ClassName);
	ResultObj->SetBoolField(TEXT("success"), true);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
	return Output;
}

// ============================================================================
// HandleCreatePhysicsAsset — 从 SkeletalMesh 创建 PhysicsAsset
// 使用 UPhysicsAssetFactory::FactoryCreateNew 直接传入 Context
// ============================================================================

FString USKAssetTool::HandleCreatePhysicsAsset(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
	FString AssetPath = Args->GetStringField(TEXT("path"));

	FString SkelMeshPath;
	if (!Args->TryGetStringField(TEXT("skeletal_mesh_path"), SkelMeshPath) || SkelMeshPath.IsEmpty())
	{
		OutError = TEXT("缺少 skeletal_mesh_path 参数（SkeletalMesh 资产路径）");
		return FString();
	}

	// 加载 SkeletalMesh
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkelMeshPath);
	if (!SkelMesh)
	{
		OutError = FString::Printf(TEXT("SkeletalMesh 未找到: %s"), *SkelMeshPath);
		return FString();
	}

	if (!SkelMesh->GetSkeleton())
	{
		OutError = TEXT("SkeletalMesh 没有关联的 Skeleton");
		return FString();
	}

	// 解析路径
	int32 LastSlash;
	FString PackagePath;
	FString AssetName;
	if (AssetPath.FindLastChar('/', LastSlash))
	{
		PackagePath = AssetPath.Left(LastSlash);
		AssetName = AssetPath.RightChop(LastSlash + 1);
	}
	else
	{
		OutError = TEXT("无效的资产路径格式");
		return FString();
	}

	// 删除旧资产
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		UEditorAssetLibrary::DeleteAsset(AssetPath);
	}

	// 使用 IAssetTools::CreateAsset 创建空的 PhysicsAsset
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UPhysicsAsset::StaticClass(), nullptr);
	UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(NewAsset);

	if (!PhysicsAsset)
	{
		OutError = TEXT("PhysicsAsset 创建失败");
		return FString();
	}

	// 使用 FPhysicsAssetUtils 从骨骼网格自动生成物理体和约束
	FPhysAssetCreateParams Params;
	Params.MinBoneSize = 8.0f;
	Params.GeomType = EFG_Sphyl;         // 胶囊体
	Params.bBodyForAll = true;            // 为所有骨骼创建物理体
	Params.bCreateConstraints = true;     // 创建骨骼间约束
	Params.bAutoOrientToBone = true;      // 自动对齐骨骼方向

	FText ErrorText;
	bool bCreated = FPhysicsAssetUtils::CreateFromSkeletalMesh(PhysicsAsset, SkelMesh, Params, ErrorText, true);
	if (!bCreated)
	{
		OutError = FString::Printf(TEXT("物理体生成失败: %s"), *ErrorText.ToString());
		return FString();
	}

	// 保存
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
	ResultObj->SetStringField(TEXT("path"), AssetPath);
	ResultObj->SetStringField(TEXT("skeletal_mesh"), SkelMeshPath);
	ResultObj->SetStringField(TEXT("class"), TEXT("PhysicsAsset"));
	ResultObj->SetBoolField(TEXT("success"), true);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
	return Output;
}

FString USKAssetTool::HandleDelete(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        OutError = FString::Printf(TEXT("资产不存在: %s"), *AssetPath);
        return FString();
    }

    bool bSuccess = UEditorAssetLibrary::DeleteAsset(AssetPath);
    if (!bSuccess)
    {
        OutError = FString::Printf(TEXT("删除资产失败: %s"), *AssetPath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetBoolField(TEXT("deleted"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleDuplicate(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString SourcePath = Args->GetStringField(TEXT("path"));
    FString DestPath;
    if (!Args->TryGetStringField(TEXT("destination"), DestPath) || DestPath.IsEmpty())
    {
        DestPath = SourcePath + TEXT("_Copy");
    }

    UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
    if (!Duplicated)
    {
        OutError = FString::Printf(TEXT("复制资产失败: %s -> %s"), *SourcePath, *DestPath);
        return FString();
    }

    UEditorAssetLibrary::SaveAsset(DestPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("source"), SourcePath);
    ResultObj->SetStringField(TEXT("destination"), DestPath);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleRename(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString SourcePath = Args->GetStringField(TEXT("path"));
    FString DestPath;
    if (!Args->TryGetStringField(TEXT("destination"), DestPath) || DestPath.IsEmpty())
    {
        OutError = TEXT("缺少 destination 参数（重命名目标路径）");
        return FString();
    }

    bool bSuccess = UEditorAssetLibrary::RenameAsset(SourcePath, DestPath);
    if (!bSuccess)
    {
        OutError = FString::Printf(TEXT("重命名资产失败: %s -> %s"), *SourcePath, *DestPath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("source"), SourcePath);
    ResultObj->SetStringField(TEXT("destination"), DestPath);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleSave(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        OutError = FString::Printf(TEXT("资产不存在: %s"), *AssetPath);
        return FString();
    }

    bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath, false);
    if (!bSuccess)
    {
        OutError = FString::Printf(TEXT("保存资产失败: %s"), *AssetPath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetBoolField(TEXT("saved"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAssetTool::HandleImportFile(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString SourceFile;
    if (!Args->TryGetStringField(TEXT("source_file"), SourceFile) || SourceFile.IsEmpty())
    {
        OutError = TEXT("缺少 source_file 参数（源文件绝对路径）");
        return FString();
    }

    if (!FPaths::FileExists(SourceFile))
    {
        OutError = FString::Printf(TEXT("源文件不存在: %s"), *SourceFile);
        return FString();
    }

    int32 LastSlash;
    FString PackagePath;
    FString AssetName;
    if (AssetPath.FindLastChar('/', LastSlash))
    {
        PackagePath = AssetPath.Left(LastSlash);
        AssetName = AssetPath.RightChop(LastSlash + 1);
    }
    else
    {
        OutError = TEXT("无效的资产路径格式");
        return FString();
    }

    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        UEditorAssetLibrary::DeleteAsset(AssetPath);
    }

    UAssetImportTask* Task = NewObject<UAssetImportTask>();
    Task->Filename = SourceFile;
    Task->DestinationPath = PackagePath;
    Task->DestinationName = AssetName;
    Task->bAutomated = true;
    Task->bReplaceExisting = true;
    Task->bSave = true;

    // Determine factory from file extension
    FString Ext = FPaths::GetExtension(SourceFile).ToLower();
    if (Ext == TEXT("png") || Ext == TEXT("tga") || Ext == TEXT("bmp") || Ext == TEXT("dds"))
    {
        Task->Factory = NewObject<UTextureFactory>();
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    AssetTools.ImportAssetTasks({Task});

    TArray<FString> Imported;
    TArray<UObject*> ImportedObjects = Task->GetObjects();
    for (UObject* Obj : ImportedObjects)
    {
        if (Obj)
        {
            Imported.Add(Obj->GetPathName());
        }
    }
    if (Imported.Num() == 0)
    {
        // Check if asset was created anyway
        if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            Imported.Add(AssetPath);
        }
    }

    if (Imported.Num() == 0)
    {
        OutError = FString::Printf(TEXT("导入失败，未生成资产: %s"), *AssetPath);
        return FString();
    }

    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("source"), SourceFile);
    ResultObj->SetBoolField(TEXT("success"), true);

    TArray<TSharedPtr<FJsonValue>> ImportedArray;
    for (const FString& P : Imported)
    {
        ImportedArray.Add(MakeShareable(new FJsonValueString(P)));
    }
    ResultObj->SetArrayField(TEXT("imported_paths"), ImportedArray);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}
