#include "SekiroAnimBlueprintExtEditor.h"

DEFINE_LOG_CATEGORY(LogSekiroAnimBlueprintExtEditor);

#define LOCTEXT_NAMESPACE "FSekiroAnimBlueprintExtEditorModule"

void FSekiroAnimBlueprintExtEditorModule::StartupModule()
{
    UE_LOG(LogSekiroAnimBlueprintExtEditor, Log, TEXT("SekiroAnimBlueprintExt editor module started."));
}

void FSekiroAnimBlueprintExtEditorModule::ShutdownModule()
{
    UE_LOG(LogSekiroAnimBlueprintExtEditor, Log, TEXT("SekiroAnimBlueprintExt editor module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSekiroAnimBlueprintExtEditorModule, SekiroAnimBlueprintExtEditor)
