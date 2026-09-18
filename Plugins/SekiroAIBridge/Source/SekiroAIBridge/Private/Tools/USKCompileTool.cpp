#include "Tools/USKCompileTool.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/Engine.h"
#include "Misc/StringOutputDevice.h"

FString USKCompileTool::GetToolDescription() const
{
    return TEXT("编译Blueprint或触发LiveCoding。支持编译所有BP、单个BP、或保存并编译。");
}

FString USKCompileTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\",\"description\":\"Compile target: all, asset path, livecoding, save\"}},\"required\":[\"target\"]}");
}

FString USKCompileTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Target;
    if (!ArgsObj->TryGetStringField(TEXT("target"), Target))
    {
        OutError = TEXT("缺少 target 参数");
        return FString();
    }

    if (Target == TEXT("all"))
    {
        return CompileAllBlueprints(OutError);
    }
    else if (Target == TEXT("livecoding"))
    {
        return CompileLiveCoding(OutError);
    }
    else if (Target == TEXT("save"))
    {
        return SaveAndCompile(Target, OutError);
    }
    else
    {
        return CompileSingleBlueprint(Target, OutError);
    }
}

FString USKCompileTool::CompileAllBlueprints(FString& OutError)
{
    int32 CompiledCount = 0;
    int32 ErrorCount = 0;
    int32 TotalCount = 0;

    // 通过AssetRegistry查找所有Blueprint资产
    IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(FName("/Game"));

    TArray<FAssetData> BlueprintAssets;
    AssetRegistry.GetAssets(Filter, BlueprintAssets);
    TotalCount = BlueprintAssets.Num();

    for (const FAssetData& Asset : BlueprintAssets)
    {
        UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
        if (BP && BP->Status != BS_UpToDate)
        {
            FKismetEditorUtilities::CompileBlueprint(BP);
            CompiledCount++;

            if (BP->Status == BS_Error)
            {
                ErrorCount++;
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("target"), TEXT("all"));
    ResultObj->SetNumberField(TEXT("totalBlueprints"), TotalCount);
    ResultObj->SetNumberField(TEXT("compiled"), CompiledCount);
    ResultObj->SetNumberField(TEXT("errors"), ErrorCount);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKCompileTool::CompileSingleBlueprint(const FString& AssetPath, FString& OutError)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP)
    {
        OutError = FString::Printf(TEXT("Blueprint未找到: %s"), *AssetPath);
        return FString();
    }

    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("target"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), BP->GetName());
    ResultObj->SetBoolField(TEXT("success"), BP->Status != BS_Error);
    ResultObj->SetStringField(TEXT("status"), BP->Status == BS_UpToDate ? TEXT("UpToDate") : (BP->Status == BS_Error ? TEXT("Error") : TEXT("Compiled")));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKCompileTool::CompileLiveCoding(FString& OutError)
{
    if (!GEngine)
    {
        OutError = TEXT("GEngine 不可用");
        return FString();
    }

    GEngine->Exec(nullptr, TEXT("LiveCoding.Compile"));

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("target"), TEXT("livecoding"));
    ResultObj->SetBoolField(TEXT("triggered"), true);
    ResultObj->SetStringField(TEXT("note"), TEXT("LiveCoding编译已触发，请在编辑器中查看结果"));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKCompileTool::SaveAndCompile(const FString& AssetPath, FString& OutError)
{
    if (!GEngine)
    {
        OutError = TEXT("GEngine 不可用");
        return FString();
    }

    // 保存所有已修改的关卡和资产
    // 通过控制台命令触发
    FStringOutputDevice OutputDevice;
    GEngine->Exec(nullptr, TEXT("SaveAll"), OutputDevice);

    // 然后编译所有Blueprint
    return CompileAllBlueprints(OutError);
}
