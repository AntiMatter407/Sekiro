#include "SekiroAnimBlueprintExt.h"

DEFINE_LOG_CATEGORY(LogSekiroAnimBlueprintExt);

#define LOCTEXT_NAMESPACE "FSekiroAnimBlueprintExtModule"

void FSekiroAnimBlueprintExtModule::StartupModule()
{
    UE_LOG(LogSekiroAnimBlueprintExt, Log, TEXT("SekiroAnimBlueprintExt runtime module started."));
}

void FSekiroAnimBlueprintExtModule::ShutdownModule()
{
    UE_LOG(LogSekiroAnimBlueprintExt, Log, TEXT("SekiroAnimBlueprintExt runtime module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSekiroAnimBlueprintExtModule, SekiroAnimBlueprintExt)
