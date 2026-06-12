#include "SekiroImportCommandlet.h"
#include "SekiroImportPipeline.h"
#include "SekiroImportSettings.h"
#include "SekiroImportLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

int32 USekiroImportCommandlet::Main(const FString& Params)
{
    // 默认参数（泛型）
    FString ProjectDir = FPaths::ProjectDir();
    FString ModelJsonPath;
    FString AnimJsonPath;
    FString OutputBasePath = TEXT("/Game/Characters");
    FString SkeletonName   = TEXT("Character_Skeleton");

    // 解析命令行参数: -Model= -Anim= -Output= -SkeletonName= -AnimOnly
    FString Left, Right;
    bool bAnimOnly = Params.Contains(TEXT("-AnimOnly"));
    if (Params.Split(TEXT("-Model="), &Left, &Right))
        ModelJsonPath = Right.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Right;
    if (Params.Split(TEXT("-Anim="), &Left, &Right))
        AnimJsonPath = Right.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Right;
    if (Params.Split(TEXT("-Output="), &Left, &Right))
        OutputBasePath = Right.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Right;
    if (Params.Split(TEXT("-SkeletonName="), &Left, &Right))
        SkeletonName = Right.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Right;

    UE_LOG(LogSekiroImport, Display, TEXT("========================================"));
    UE_LOG(LogSekiroImport, Display, TEXT("SekiroImport Commandlet"));
    UE_LOG(LogSekiroImport, Display, TEXT("  Model:        %s"), ModelJsonPath.IsEmpty() ? TEXT("(skip)") : *ModelJsonPath);
    UE_LOG(LogSekiroImport, Display, TEXT("  Anim:         %s"), AnimJsonPath.IsEmpty() ? TEXT("(skip)") : *AnimJsonPath);
    UE_LOG(LogSekiroImport, Display, TEXT("  Output:       %s"), *OutputBasePath);
    UE_LOG(LogSekiroImport, Display, TEXT("  SkeletonName: %s"), *SkeletonName);
    UE_LOG(LogSekiroImport, Display, TEXT("========================================"));

    // 检查文件存在（仅当指定了路径时）
    if (!bAnimOnly && !ModelJsonPath.IsEmpty() && !FPaths::FileExists(ModelJsonPath))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("Model JSON not found: %s"), *ModelJsonPath);
        return 1;
    }
    if (!AnimJsonPath.IsEmpty() && !FPaths::FileExists(AnimJsonPath))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("Anim JSON not found: %s"), *AnimJsonPath);
        return 1;
    }

    // 创建导入设置
    USekiroImportSettings* Settings = NewObject<USekiroImportSettings>();
    Settings->ModelJsonPath = ModelJsonPath;
    Settings->AnimationJsonPath = AnimJsonPath;
    Settings->OutputBasePath = OutputBasePath;
    Settings->SkeletonName = SkeletonName;

    if (bAnimOnly)
    {
        // 仅动画模式：不触碰已有骨架/网格体/材质
        Settings->bImportSkeleton = false;
        Settings->bImportSkeletalMesh = false;
        Settings->bImportMaterials = false;
        Settings->bImportAnimations = true;
        Settings->MaxAnimations = 0;  // 全部动画
        UE_LOG(LogSekiroImport, Display, TEXT("  Mode:  AnimOnly (仅导入动画，保留已有资产)"));
    }
    else
    {
        Settings->bImportSkeleton = true;
        Settings->bImportSkeletalMesh = true;
        Settings->bImportMaterials = true;
        Settings->bImportAnimations = true;
    }

    UE_LOG(LogSekiroImport, Display, TEXT("\nRunning import pipeline...\n"));

    // 执行导入
    FSekiroImportPipeline::FImportResult Result = FSekiroImportPipeline::Run(*Settings);

    UE_LOG(LogSekiroImport, Display, TEXT("\n========================================"));
    UE_LOG(LogSekiroImport, Display, TEXT("IMPORT RESULT"));
    UE_LOG(LogSekiroImport, Display, TEXT("  Skeleton:     %s"), Result.Skeleton ? *Result.Skeleton->GetName() : TEXT("NULL"));
    UE_LOG(LogSekiroImport, Display, TEXT("  SkeletalMesh: %s"), Result.SkeletalMesh ? *Result.SkeletalMesh->GetName() : TEXT("NULL"));
    UE_LOG(LogSekiroImport, Display, TEXT("  Materials:    %d"), Result.Materials.Num());
    UE_LOG(LogSekiroImport, Display, TEXT("  Animations:   %d"), Result.Animations.Num());
    UE_LOG(LogSekiroImport, Display, TEXT("  Success:      %s"), Result.bSuccess ? TEXT("YES") : TEXT("NO"));

    if (Result.Errors.Num() > 0)
    {
        UE_LOG(LogSekiroImport, Display, TEXT("  Errors (%d):"), Result.Errors.Num());
        for (const FString& Err : Result.Errors)
        {
            UE_LOG(LogSekiroImport, Error, TEXT("    %s"), *Err);
        }
    }

    // 验证材质SamplerType
    UE_LOG(LogSekiroImport, Display, TEXT("\n--- Material SamplerType Verification ---"));
    for (int32 i = 0; i < Result.Materials.Num(); ++i)
    {
        UMaterial* Mat = Result.Materials[i];
        if (!Mat) continue;

        UE_LOG(LogSekiroImport, Display, TEXT("  [%d] %s"), i, *Mat->GetName());

        for (UMaterialExpression* Expr : Mat->GetExpressions())
        {
            UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr);
            if (!TexSample || !TexSample->Texture) continue;

            FString Suffix;
            FString TexName = TexSample->Texture->GetName();
            if (TexName.EndsWith(TEXT("_a"))) Suffix = TEXT("_a");
            else if (TexName.EndsWith(TEXT("_n"))) Suffix = TEXT("_n");
            else if (TexName.EndsWith(TEXT("_m"))) Suffix = TEXT("_m");
            else if (TexName.EndsWith(TEXT("_r"))) Suffix = TEXT("_r");

            const TCHAR* SamplerNames[] = {
                TEXT("Color"), TEXT("Grayscale"), TEXT("Alpha"),
                TEXT("Normal"), TEXT("Masks"), TEXT("DistanceFieldFont"),
                TEXT("LinearColor"), TEXT("LinearGrayscale"), TEXT("Data"),
            };
            const TCHAR* SamplerStr = (TexSample->SamplerType >= 0 && TexSample->SamplerType < 9)
                ? SamplerNames[TexSample->SamplerType] : TEXT("Unknown");

            bool bCorrect = false;
            if (Suffix == TEXT("_a") && TexSample->SamplerType == SAMPLERTYPE_Color) bCorrect = true;
            else if (Suffix == TEXT("_n") && TexSample->SamplerType == SAMPLERTYPE_Normal) bCorrect = true;
            else if ((Suffix == TEXT("_m") || Suffix == TEXT("_r")) && TexSample->SamplerType == SAMPLERTYPE_LinearGrayscale) bCorrect = true;

            UE_LOG(LogSekiroImport, Display, TEXT("    %s (%s): SamplerType=%s [%s]"),
                *TexName, *Suffix, SamplerStr, bCorrect ? TEXT("OK") : TEXT("WRONG!"));
        }
    }

    UE_LOG(LogSekiroImport, Display, TEXT("\n========================================"));
    UE_LOG(LogSekiroImport, Display, TEXT("TEST COMPLETE"));

    return Result.bSuccess ? 0 : 1;
}
