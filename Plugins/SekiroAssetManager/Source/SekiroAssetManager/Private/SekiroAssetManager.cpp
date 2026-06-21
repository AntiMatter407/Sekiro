#include "SekiroAssetManager.h"
#include "SAModelImporter.h"
#include "SAMaterialImporter.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/ObjectLibrary.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_MODULE(FSekiroAssetManagerModule, SekiroAssetManager)

// ── 编辑器控制台命令：SAMaterialCreate ─────────────────────────────
// 从模型 JSON 创建材质，可选分配到已存在的 USkeletalMesh
// 用法: SAMaterialCreate Model="<JSON路径>" MeshPath="<网格体资产路径>" Output="<材质包路径>"
static FAutoConsoleCommand GSAMaterialCreateCommand(
    TEXT("SAMaterialCreate"),
    TEXT("从模型 JSON 创建材质到编辑器内容浏览器。\n")
    TEXT("参数:\n")
    TEXT("  Model=<JSON路径>   - 模型 JSON 文件路径（必填）\n")
    TEXT("  MeshPath=<资产路径> - 目标 USkeletalMesh 资产路径（可选）\n")
    TEXT("  Output=<包路径>     - 材质输出包路径（必填）\n")
    TEXT("示例:\n")
    TEXT("  SAMaterialCreate Model=\"F:/model.json\" MeshPath=\"/Game/Test.Test\" Output=\"/Game/MyMaterials\""),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        // ── 解析参数 ──
        FString ModelPath;
        FString MeshPath;
        FString OutputPath;

        for (const FString& Arg : Args)
        {
            FString Key, Value;
            if (!Arg.Split(TEXT("="), &Key, &Value))
                continue;

            Key.TrimStartAndEndInline();
            Value.TrimStartAndEndInline();

            // 去除可能的外层引号
            if (Value.Len() >= 2 && Value[0] == TEXT('\"') && Value[Value.Len() - 1] == TEXT('\"'))
                Value = Value.Mid(1, Value.Len() - 2);

            if (Key.Equals(TEXT("Model"), ESearchCase::IgnoreCase))
                ModelPath = Value;
            else if (Key.Equals(TEXT("MeshPath"), ESearchCase::IgnoreCase))
                MeshPath = Value;
            else if (Key.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
                OutputPath = Value;
        }

        // ── 参数校验 ──
        if (ModelPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("[SAMaterialCreate] 缺少 Model 参数"));
            return;
        }
        if (OutputPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("[SAMaterialCreate] 缺少 Output 参数"));
            return;
        }

        // 检查 JSON 文件是否存在
        if (!IFileManager::Get().FileExists(*ModelPath))
        {
            UE_LOG(LogTemp, Error, TEXT("[SAMaterialCreate] 模型 JSON 文件不存在: %s"), *ModelPath);
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] Model=%s"), *ModelPath);
        UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] Output=%s"), *OutputPath);
        if (!MeshPath.IsEmpty())
            UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] MeshPath=%s"), *MeshPath);

        // ── 解析 JSON ──
        FSAModelData ModelData;
        if (!SAModelImporter::ParseFromFile(ModelPath, ModelData))
        {
            UE_LOG(LogTemp, Error, TEXT("[SAMaterialCreate] JSON 解析失败: %s"), *ModelPath);
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] JSON 解析成功: %d 材质, %d 网格体"),
            ModelData.Materials.Num(), ModelData.Meshes.Num());

        // ── 加载 USkeletalMesh（如果指定了 MeshPath） ──
        USkeletalMesh* SkeletalMesh = nullptr;
        if (!MeshPath.IsEmpty())
        {
            // MeshPath 可能是 "/Game/Path.AssetName" 完整对象路径，或 "/Game/Path/AssetName.AssetName"
            SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
            if (!SkeletalMesh)
            {
                UE_LOG(LogTemp, Warning, TEXT("[SAMaterialCreate] 无法加载 USkeletalMesh: %s，将继续创建材质但不分配"), *MeshPath);
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] 已加载 USkeletalMesh: %s (%d 材质槽)"),
                    *MeshPath, SkeletalMesh->GetMaterials().Num());
            }
        }

        // ── 创建材质 ──
        TArray<UMaterial*> CreatedMaterials = SAMaterialImporter::BuildAll(ModelData, SkeletalMesh, OutputPath);

        UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] 完成: 成功创建 %d 个材质"), CreatedMaterials.Num());

        // 汇总统计
        int32 SuccessCount = 0;
        for (UMaterial* Mat : CreatedMaterials)
        {
            if (Mat) ++SuccessCount;
        }
        UE_LOG(LogTemp, Log, TEXT("[SAMaterialCreate] 材质 %d/%d 创建成功"), SuccessCount, CreatedMaterials.Num());
    })
);

void FSekiroAssetManagerModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("SekiroAssetManager 模块已启动"));
}

void FSekiroAssetManagerModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("SekiroAssetManager 模块已关闭"));
}
