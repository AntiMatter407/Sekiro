#include "Tools/USKAssetTool.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

FString USKAssetTool::GetToolDescription() const
{
    return TEXT("资产CRUD操作：列出目录资产、查询资产信息、创建、删除、复制、重命名、保存资产。");
}

FString USKAssetTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"list\",\"info\",\"exists\",\"create\",\"delete\",\"duplicate\",\"rename\",\"save\"]},\"path\":{\"type\":\"string\",\"description\":\"Asset path\"},\"destination\":{\"type\":\"string\",\"description\":\"Target path\"},\"recursive\":{\"type\":\"boolean\",\"default\":false}},\"required\":[\"action\",\"path\"]}");
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
    if (Action == TEXT("delete"))    return HandleDelete(ArgsObj, OutError);
    if (Action == TEXT("duplicate")) return HandleDuplicate(ArgsObj, OutError);
    if (Action == TEXT("rename"))    return HandleRename(ArgsObj, OutError);
    if (Action == TEXT("save"))      return HandleSave(ArgsObj, OutError);

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
