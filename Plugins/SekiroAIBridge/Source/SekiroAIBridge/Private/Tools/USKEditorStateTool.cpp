#include "Tools/USKEditorStateTool.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

FString USKEditorStateTool::GetToolDescription() const
{
    return TEXT("查询编辑器当前状态：打开的关卡、选中的Actor、项目信息、运行时等。只读操作，无副作用。");
}

FString USKEditorStateTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"level\",\"selection\",\"project\",\"time\",\"all\"],\"description\":\"Query type\"}},\"required\":[\"action\"]}");
}

FString USKEditorStateTool::Execute(const FString& ArgsJson, FString& OutError)
{
    // 解析 action 参数
    FString Action = TEXT("all");

    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        ArgsObj->TryGetStringField(TEXT("action"), Action);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

    if (Action == TEXT("level") || Action == TEXT("all"))
    {
        ResultObj->SetStringField(TEXT("level"), QueryOpenLevel());
    }

    if (Action == TEXT("selection") || Action == TEXT("all"))
    {
        TSharedPtr<FJsonObject> SelObj;
        TSharedRef<TJsonReader<>> SelReader = TJsonReaderFactory<>::Create(QuerySelectedActors());
        if (FJsonSerializer::Deserialize(SelReader, SelObj) && SelObj.IsValid())
        {
            ResultObj->SetObjectField(TEXT("selection"), SelObj);
        }
    }

    if (Action == TEXT("project") || Action == TEXT("all"))
    {
        TSharedPtr<FJsonObject> ProjectObj;
        TSharedRef<TJsonReader<>> ProjectReader = TJsonReaderFactory<>::Create(QueryProjectInfo());
        if (FJsonSerializer::Deserialize(ProjectReader, ProjectObj) && ProjectObj.IsValid())
        {
            ResultObj->SetObjectField(TEXT("project"), ProjectObj);
        }
    }

    if (Action == TEXT("time") || Action == TEXT("all"))
    {
        ResultObj->SetStringField(TEXT("editorTime"), QueryEditorTime());
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKEditorStateTool::QueryOpenLevel() const
{
    if (!GEditor)
    {
        return TEXT("GEditor不可用");
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return TEXT("无World");
    }

    return World->GetMapName();
}

FString USKEditorStateTool::QuerySelectedActors() const
{
    UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    if (!ActorSubsystem)
    {
        return TEXT("无法获取Actor子系统");
    }

    TArray<AActor*> SelectedActors = ActorSubsystem->GetSelectedLevelActors();

    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetNumberField(TEXT("count"), SelectedActors.Num());

    TArray<TSharedPtr<FJsonValue>> NamesArray;
    for (AActor* Actor : SelectedActors)
    {
        if (Actor)
        {
            NamesArray.Add(MakeShareable(new FJsonValueString(Actor->GetName())));
        }
    }
    Obj->SetArrayField(TEXT("actors"), NamesArray);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Output;
}

FString USKEditorStateTool::QueryProjectInfo() const
{
    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetStringField(TEXT("name"), FApp::GetProjectName());
    Obj->SetStringField(TEXT("version"), FEngineVersion::Current().ToString());
    Obj->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Output;
}

FString USKEditorStateTool::QueryEditorTime() const
{
    double CurrentTime = FPlatformTime::Seconds();
    // 仅返回可用时间戳，运行时长由调用方计算
    return FString::Printf(TEXT("%.0f"), CurrentTime);
}
