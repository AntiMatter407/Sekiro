#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SekiroImportCommandlet.generated.h"

/// 测试Commandlet: 运行SekiroImport管线并输出结果
UCLASS()
class SEKIROIMPORT_API USekiroImportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};
