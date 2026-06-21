#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SAImportCommandlet.generated.h"

/// SekiroAssetManager 导入 Commandlet
/// 用法: UnrealEditor-Cmd.exe Sekiro.uproject -run=SAImport -Model=<json路径> -Output=<UE路径>
UCLASS()
class SEKIROASSETMANAGER_API USAImportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};
