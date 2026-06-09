#include "SekiroImportPipeline.h"
#include "SekiroImportSettings.h"
#include "SekiroImportLog.h"
#include "SekiroModelParser.h"
#include "SekiroAnimationParser.h"
#include "SekiroSkeletonBuilder.h"
#include "SekiroSkeletalMeshBuilder.h"
#include "SekiroAnimationBuilder.h"
#include "SekiroMaterialBuilder.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "ReferenceSkeleton.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"

FSekiroImportPipeline::FOnProgress FSekiroImportPipeline::OnProgress;

#define REPORT_PROGRESS(Format, ...) \
{ \
    FString Msg = FString::Printf(Format, ##__VA_ARGS__); \
    UE_LOG(LogSekiroImport, Log, TEXT("%s"), *Msg); \
    if (OnProgress.IsBound()) { OnProgress.Execute(Msg); } \
}

// 验证日志: 使用Warning级别确保在Output Log中可见
#define VERIFY_LOG(Format, ...) \
    UE_LOG(LogSekiroImport, Warning, TEXT("[验证] " Format), ##__VA_ARGS__)

#define VERIFY_CHECK(Condition, Format, ...) \
    if (!(Condition)) { UE_LOG(LogSekiroImport, Error, TEXT("[验证失败] " Format), ##__VA_ARGS__); } \
    else { UE_LOG(LogSekiroImport, Warning, TEXT("[验证通过] " Format), ##__VA_ARGS__); }

// ============================================================================
// 步骤1 验证: 解析后的骨骼数据 (动画JSON → FSekiroImportBone[])
// ============================================================================
static void VerifyStep1_Bones(const TArray<FSekiroImportBone>& Bones, const FString& Source)
{
    VERIFY_LOG(TEXT("========== S1: 骨骼数据验证 (%s) =========="), *Source);
    VERIFY_LOG(TEXT("  骨骼总数: %d"), Bones.Num());

    VERIFY_CHECK(Bones.Num() > 0, TEXT("骨骼数>0"));

    // 层级顺序检查 (父骨骼必须在子骨骼之前)
    int32 HierarchyErrors = 0;
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        if (Bones[i].ParentIndex != INDEX_NONE && Bones[i].ParentIndex >= i)
        {
            VERIFY_LOG(TEXT("  [层级错误] Bone[%d] '%s' ParentIndex=%d >= %d"),
                i, *Bones[i].Name.ToString(), Bones[i].ParentIndex, i);
            ++HierarchyErrors;
        }
    }
    VERIFY_CHECK(HierarchyErrors == 0, TEXT("层级顺序: %d 错误"), HierarchyErrors);

    // 重复名检查
    TSet<FName> Names;
    int32 Duplicates = 0;
    for (const auto& B : Bones)
    {
        if (Names.Contains(B.Name)) { ++Duplicates; VERIFY_LOG(TEXT("  [重复名] %s"), *B.Name.ToString()); }
        else { Names.Add(B.Name); }
    }
    VERIFY_CHECK(Duplicates == 0, TEXT("骨骼名唯一: %d 重复"), Duplicates);

    // Root骨骼检查 (索引0, 无父骨骼)
    if (Bones.Num() > 0)
    {
        VERIFY_LOG(TEXT("  Root骨骼: '%s' ParentIndex=%d"), *Bones[0].Name.ToString(), Bones[0].ParentIndex);
        VERIFY_LOG(TEXT("  Root LocalPos: (%.2f, %.2f, %.2f)"), Bones[0].LocalTranslation.X, Bones[0].LocalTranslation.Y, Bones[0].LocalTranslation.Z);
        VERIFY_LOG(TEXT("  Root LocalRot: (%.4f, %.4f, %.4f, %.4f)"), Bones[0].LocalRotation.X, Bones[0].LocalRotation.Y, Bones[0].LocalRotation.Z, Bones[0].LocalRotation.W);
        VERIFY_LOG(TEXT("  Root LocalScale: (%.3f, %.3f, %.3f)"), Bones[0].LocalScale.X, Bones[0].LocalScale.Y, Bones[0].LocalScale.Z);
        VERIFY_CHECK(Bones[0].ParentIndex == INDEX_NONE, TEXT("Root骨骼ParentIndex==-1"));
    }

    // 采样: 打印前5根和后3根骨骼
    VERIFY_LOG(TEXT("  --- 前5根骨骼 ---"));
    for (int32 i = 0; i < FMath::Min(5, Bones.Num()); ++i)
    {
        VERIFY_LOG(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
            i, *Bones[i].Name.ToString(), Bones[i].ParentIndex,
            Bones[i].LocalTranslation.X, Bones[i].LocalTranslation.Y, Bones[i].LocalTranslation.Z);
    }
    if (Bones.Num() > 5)
    {
        VERIFY_LOG(TEXT("  --- 后3根骨骼 ---"));
        for (int32 i = FMath::Max(5, Bones.Num() - 3); i < Bones.Num(); ++i)
        {
            VERIFY_LOG(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
                i, *Bones[i].Name.ToString(), Bones[i].ParentIndex,
                Bones[i].LocalTranslation.X, Bones[i].LocalTranslation.Y, Bones[i].LocalTranslation.Z);
        }
    }

    // IK骨骼检测 (名中含"_Target"或"IK")
    int32 IKBoneCount = 0;
    for (const auto& B : Bones)
    {
        FString Name = B.Name.ToString();
        if (Name.Contains(TEXT("_Target")) || Name.Contains(TEXT("IK"))) ++IKBoneCount;
    }
    VERIFY_LOG(TEXT("  IK辅助骨骼数: %d"), IKBoneCount);

    VERIFY_LOG(TEXT("========== S1结束 =========="));
}

// ============================================================================
// 步骤2 验证: 构建后的USkeleton
// ============================================================================
static void VerifyStep2_Skeleton(USkeleton* Skeleton, int32 ExpectedBoneCount)
{
    VERIFY_LOG(TEXT("========== S2: 骨架验证 =========="));
    if (!Skeleton) { VERIFY_LOG(TEXT("  [失败] Skeleton == nullptr")); return; }

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    VERIFY_LOG(TEXT("  资产名: %s"), *Skeleton->GetName());
    VERIFY_LOG(TEXT("  参考骨骼数: %d (期望: %d)"), RefSkel.GetNum(), ExpectedBoneCount);
    VERIFY_CHECK(RefSkel.GetNum() == ExpectedBoneCount, TEXT("骨骼数匹配: %d == %d"), RefSkel.GetNum(), ExpectedBoneCount);

    // 验证父索引范围
    int32 ParentErrors = 0;
    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
    {
        int32 P = RefSkel.GetParentIndex(i);
        if (P != INDEX_NONE && (P < 0 || P >= RefSkel.GetNum()))
        {
            VERIFY_LOG(TEXT("  [父索引越界] Bone[%d] '%s' Parent=%d"), i, *RefSkel.GetBoneName(i).ToString(), P);
            ++ParentErrors;
        }
    }
    VERIFY_CHECK(ParentErrors == 0, TEXT("所有父索引合法: %d 错误"), ParentErrors);

    // 抽取骨骼名对照S1的前5根
    VERIFY_LOG(TEXT("  --- RefSkeleton前5根 ---"));
    for (int32 i = 0; i < FMath::Min(5, RefSkel.GetNum()); ++i)
    {
        const FTransform& Pose = RefSkel.GetRefBonePose()[i];
        VERIFY_LOG(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
            i, *RefSkel.GetBoneName(i).ToString(), RefSkel.GetParentIndex(i),
            Pose.GetTranslation().X, Pose.GetTranslation().Y, Pose.GetTranslation().Z);
    }

    VERIFY_LOG(TEXT("========== S2结束 =========="));
}

// ============================================================================
// 步骤3 验证: 解析后的模型数据 (模型JSON → FSekiroModelData)
// ============================================================================
static void VerifyStep3_ModelData(const FSekiroModelData& ModelData)
{
    VERIFY_LOG(TEXT("========== S3: 模型数据验证 =========="));

    VERIFY_LOG(TEXT("  SkeletonName: %s"), *ModelData.SkeletonName);
    VERIFY_LOG(TEXT("  Mesh Section数: %d"), ModelData.Meshes.Num());
    VERIFY_LOG(TEXT("  材质数: %d"), ModelData.Materials.Num());
    VERIFY_CHECK(ModelData.Meshes.Num() > 0, TEXT("存在Mesh Section"));

    // 每个Section的统计
    int32 TotalVerts = 0, TotalTris = 0;
    int32 TotalBoneMappings = 0;
    int32 TriIndexErrors = 0;

    for (int32 s = 0; s < ModelData.Meshes.Num(); ++s)
    {
        const FSekiroImportMeshSection& Sec = ModelData.Meshes[s];
        TotalVerts += Sec.Vertices.Num();
        TotalTris += Sec.Triangles.Num();
        TotalBoneMappings += Sec.BoneIdxToName.Num();

        // 三角形索引范围检查
        for (const FIntVector& Tri : Sec.Triangles)
        {
            if (Tri.X < 0 || Tri.X >= Sec.Vertices.Num() ||
                Tri.Y < 0 || Tri.Y >= Sec.Vertices.Num() ||
                Tri.Z < 0 || Tri.Z >= Sec.Vertices.Num())
            {
                ++TriIndexErrors;
            }
        }

        VERIFY_LOG(TEXT("  Section[%d] '%s': %d顶点, %d三角形, MatIdx=%d, %d蒙皮骨骼"),
            s, *Sec.PartName, Sec.Vertices.Num(), Sec.Triangles.Num(),
            Sec.MaterialIndex, Sec.BoneIdxToName.Num());

        // 每Section打印BoneIdxToName前3个映射
        int32 MapCount = 0;
        for (const auto& Pair : Sec.BoneIdxToName)
        {
            if (MapCount >= 3) break;
            VERIFY_LOG(TEXT("    BoneIdxToName[%d] = '%s'"), Pair.Key, *Pair.Value.ToString());
            ++MapCount;
        }
    }

    VERIFY_LOG(TEXT("  总计: %d顶点, %d三角形, %d骨骼映射条目"), TotalVerts, TotalTris, TotalBoneMappings);
    VERIFY_CHECK(TriIndexErrors == 0, TEXT("三角形索引范围: %d 越界"), TriIndexErrors);
    VERIFY_CHECK(TotalVerts > 0, TEXT("总顶点数>0"));
    VERIFY_CHECK(TotalTris > 0, TEXT("总三角形数>0"));

    // 材质详情
    for (int32 m = 0; m < ModelData.Materials.Num(); ++m)
    {
        const FSekiroImportMaterial& Mat = ModelData.Materials[m];
        VERIFY_LOG(TEXT("  材质[%d]: %-40s Blend=%s 纹理槽=%d"),
            m, *Mat.Name, *Mat.BlendMode, Mat.TextureSlots.Num());
    }

    // 模型骨骼数 (模型JSON自带93骨骼)
    VERIFY_LOG(TEXT("  模型自带骨骼数: %d (应为93, 子集)"), ModelData.Bones.Num());

    VERIFY_LOG(TEXT("========== S3结束 =========="));
}

// ============================================================================
// 步骤4 验证: 构建后的USkeletalMesh
// ============================================================================
static void VerifyStep4_SkeletalMesh(USkeletalMesh* Mesh, const FSekiroModelData& ModelData, USkeleton* Skeleton)
{
    VERIFY_LOG(TEXT("========== S4: 骨骼网格体验证 =========="));
    if (!Mesh) { VERIFY_LOG(TEXT("  [失败] SkeletalMesh == nullptr")); return; }

    VERIFY_LOG(TEXT("  资产名: %s"), *Mesh->GetName());

    // 骨架引用
    VERIFY_CHECK(Mesh->GetSkeleton() == Skeleton, TEXT("骨架引用正确"));

    // 材质槽
    VERIFY_LOG(TEXT("  材质槽数: %d (期望: %d)"), Mesh->GetMaterials().Num(), ModelData.Materials.Num());
    VERIFY_CHECK(Mesh->GetMaterials().Num() == ModelData.Materials.Num(),
        TEXT("材质槽数匹配: %d == %d"), Mesh->GetMaterials().Num(), ModelData.Materials.Num());

    // RefBasesInvMatrix (这个之前崩溃过)
    {
        const TArray<FMatrix44f>& InvMat = Mesh->GetRefBasesInvMatrix();
        const int32 ExpectedBones = Skeleton ? Skeleton->GetReferenceSkeleton().GetNum() : 0;
        VERIFY_LOG(TEXT("  RefBasesInvMatrix.Num() = %d (期望: %d)"), InvMat.Num(), ExpectedBones);
        VERIFY_CHECK(InvMat.Num() > 0, TEXT("RefBasesInvMatrix非空: %d 个"), InvMat.Num());
    }

    // 渲染数据
    {
        FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        VERIFY_CHECK(RenderData != nullptr, TEXT("GetResourceForRendering() != nullptr"));
        if (RenderData)
        {
            VERIFY_LOG(TEXT("  LODRenderData.Num() = %d"), RenderData->LODRenderData.Num());
            VERIFY_CHECK(RenderData->LODRenderData.Num() > 0, TEXT("有LOD渲染数据"));
        }
    }

    // LOD源数据
    {
        FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
        if (ImportedModel)
        {
            VERIFY_LOG(TEXT("  LODModels.Num() = %d"), ImportedModel->LODModels.Num());
            if (ImportedModel->LODModels.Num() > 0)
            {
                const FSkeletalMeshLODModel& LOD0 = ImportedModel->LODModels[0];
                VERIFY_LOG(TEXT("  LOD0: %d Sections, %d Vertices, %d IndexBuffer"),
                    LOD0.Sections.Num(), LOD0.NumVertices, LOD0.IndexBuffer.Num());
            }
        }
    }

    // 验证Mesh的RefSkeleton与Skeleton同步
    {
        const FReferenceSkeleton& MeshRefSkel = Mesh->GetRefSkeleton();
        const FReferenceSkeleton& SkelRefSkel = Skeleton->GetReferenceSkeleton();
        VERIFY_LOG(TEXT("  Mesh.RefSkeleton骨骼数: %d, Skeleton.RefSkeleton骨骼数: %d"),
            MeshRefSkel.GetNum(), SkelRefSkel.GetNum());

        // 对照前5根骨骼名
        int32 NameMismatches = 0;
        for (int32 i = 0; i < FMath::Min(5, FMath::Min(MeshRefSkel.GetNum(), SkelRefSkel.GetNum())); ++i)
        {
            if (MeshRefSkel.GetBoneName(i) != SkelRefSkel.GetBoneName(i))
            {
                VERIFY_LOG(TEXT("  [不匹配] Bone[%d]: Mesh='%s' vs Skeleton='%s'"),
                    i, *MeshRefSkel.GetBoneName(i).ToString(), *SkelRefSkel.GetBoneName(i).ToString());
                ++NameMismatches;
            }
        }
        VERIFY_CHECK(NameMismatches == 0, TEXT("Mesh与Skeleton前5根骨骼名一致"));
    }

    VERIFY_LOG(TEXT("========== S4结束 =========="));
}

// ============================================================================
// 步骤5 验证: 材质实例
// ============================================================================
static void VerifyStep5_Materials(const TArray<UMaterialInstanceConstant*>& Materials, USkeletalMesh* Mesh)
{
    VERIFY_LOG(TEXT("========== S5: 材质验证 =========="));
    VERIFY_LOG(TEXT("  材质实例数: %d"), Materials.Num());

    for (int32 i = 0; i < Materials.Num(); ++i)
    {
        UMaterialInstanceConstant* MI = Materials[i];
        if (MI)
        {
            VERIFY_LOG(TEXT("  MI[%d]: %s Parent=%s BlendMode=%d"),
                i, *MI->GetName(),
                MI->Parent ? *MI->Parent->GetName() : TEXT("null"),
                (int32)MI->BasePropertyOverrides.BlendMode);
        }
    }

    // 确认材质已分配到Mesh
    if (Mesh)
    {
        int32 AssignedCount = 0;
        for (int32 i = 0; i < Mesh->GetMaterials().Num(); ++i)
        {
            if (Mesh->GetMaterials()[i].MaterialInterface) ++AssignedCount;
        }
        VERIFY_LOG(TEXT("  Mesh材质槽已分配: %d/%d"), AssignedCount, Mesh->GetMaterials().Num());
        VERIFY_CHECK(AssignedCount == Materials.Num(), TEXT("所有材质槽已分配到Mesh"));
    }

    VERIFY_LOG(TEXT("========== S5结束 =========="));
}

// ============================================================================
// 步骤6 验证: 动画序列
// ============================================================================
static void VerifyStep6_Animations(const TArray<UAnimSequence*>& Animations, USkeleton* Skeleton)
{
    VERIFY_LOG(TEXT("========== S6: 动画验证 =========="));
    VERIFY_LOG(TEXT("  动画总数: %d"), Animations.Num());
    VERIFY_CHECK(Animations.Num() > 0, TEXT("至少有1个动画"));

    for (int32 i = 0; i < FMath::Min(3, Animations.Num()); ++i) // 只详细检查前3个
    {
        UAnimSequence* Anim = Animations[i];
        if (!Anim) continue;

        VERIFY_LOG(TEXT("  --- Anim[%d]: %s ---"), i, *Anim->GetName());
        VERIFY_LOG(TEXT("    Skeleton: %s"), Anim->GetSkeleton() ? *Anim->GetSkeleton()->GetName() : TEXT("null"));
        VERIFY_LOG(TEXT("    NumFrames: %d"), Anim->GetNumberOfSampledKeys());
        VERIFY_LOG(TEXT("    SequenceLength: %.2f秒"), Anim->GetPlayLength());
        VERIFY_LOG(TEXT("    FrameRate: %.1f"), Anim->GetSamplingFrameRate().AsDecimal());

        VERIFY_CHECK(Anim->GetSkeleton() == Skeleton, TEXT("动画骨架引用正确"));
        VERIFY_CHECK(Anim->GetNumberOfSampledKeys() > 0, TEXT("有关键帧数据"));

        // 骨骼轨道数
        int32 NumTracks = Anim->GetNumberOfSampledKeys() > 0 ? 1 : 0; // 简化的轨道检测
        VERIFY_LOG(TEXT("    (完整轨道验证需进入Persona查看)"));
    }

    VERIFY_LOG(TEXT("========== S6结束 =========="));
}

// ============================================================================
// 主入口
// ============================================================================

FSekiroImportPipeline::FImportResult FSekiroImportPipeline::Run(const USekiroImportSettings& Settings)
{
    FImportResult Result;

    const FString& OutputBase = Settings.OutputBasePath;
    const FString& SkeletonName = Settings.SkeletonName;

    // ============================================================
    // 步骤1: 解析动画JSON → 获取完整骨架骨骼(146根，含IK)
    // ============================================================
    FSekiroAnimationParser::FParseResult AnimParseResult;

    if (Settings.bImportSkeleton && !Settings.AnimationJsonPath.IsEmpty())
    {
        REPORT_PROGRESS(TEXT("S1: 解析动画JSON: %s"), *Settings.AnimationJsonPath);

        if (!FSekiroAnimationParser::ParseFromFile(Settings.AnimationJsonPath, AnimParseResult, Settings.bImportAnimations ? Settings.MaxAnimations : -1, Settings.AnimationPrefixFilter))
        {
            Result.Errors.Add(TEXT("动画JSON解析失败"));
        }
        else
        {
            VerifyStep1_Bones(AnimParseResult.Bones, TEXT("动画JSON"));
        }
    }

    // ============================================================
    // 步骤2: 构建USkeleton
    // ============================================================
    USkeleton* Skeleton = nullptr;
    TArray<FSekiroImportBone> SkeletonBones;

    if (Settings.bImportSkeleton)
    {
        if (AnimParseResult.Bones.Num() > 0)
        {
            SkeletonBones = AnimParseResult.Bones;

            // 如果有模型JSON，用模型的WorldPos修正参考姿态
            if (!Settings.ModelJsonPath.IsEmpty())
            {
                FSekiroModelData ModelForBones;
                if (FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelForBones) && ModelForBones.Bones.Num() > 0)
                {
                    FSekiroSkeletonBuilder::MergeModelWorldTransforms(SkeletonBones, ModelForBones.Bones);
                    {
                        TSet<FName> MeshBoneNames;
                        for (const FSekiroImportMeshSection& Sec : ModelForBones.Meshes)
                            for (const auto& Pair : Sec.BoneIdxToName)
                                MeshBoneNames.Add(Pair.Value);
                        FSekiroSkeletonBuilder::AppendModelOnlyBones(SkeletonBones, ModelForBones.Bones, MeshBoneNames);
                    }
                    REPORT_PROGRESS(TEXT("S2: 动画%d骨骼 + 模型WorldPos合并 + %dModelOnly追加"), AnimParseResult.Bones.Num(), SkeletonBones.Num() - AnimParseResult.Bones.Num());
                }
                else
                {
                    REPORT_PROGRESS(TEXT("S2: 使用动画JSON构建骨架: %d 根骨骼（含IK）"), SkeletonBones.Num());
                }
            }
            else
            {
                REPORT_PROGRESS(TEXT("S2: 使用动画JSON构建骨架: %d 根骨骼（含IK）"), SkeletonBones.Num());
            }
        }
        else if (!Settings.ModelJsonPath.IsEmpty())
        {
            // 仅模型JSON：解析模型93骨骼
            FSekiroModelData ModelData;
            if (FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelData) && ModelData.Bones.Num() > 0)
            {
                FSekiroSkeletonBuilder::DeriveLocalFromWorld(ModelData.Bones);
                SkeletonBones = ModelData.Bones;
                REPORT_PROGRESS(TEXT("S2: 使用模型JSON构建骨架: %d 根骨骼"), SkeletonBones.Num());
            }
        }

        if (SkeletonBones.Num() > 0)
        {
            FString SkeletonPath = FString::Printf(TEXT("%s/%s"), *OutputBase, *SkeletonName);
            Skeleton = FSekiroSkeletonBuilder::Build(SkeletonBones, SkeletonName, SkeletonPath);
            if (Skeleton)
            {
                Result.Skeleton = Skeleton;
                REPORT_PROGRESS(TEXT("S2: 骨架构建完成: %s"), *Skeleton->GetName());
                VerifyStep2_Skeleton(Skeleton, SkeletonBones.Num());
            }
            else
            {
                Result.Errors.Add(TEXT("骨架构建失败"));
            }
        }
        else
        {
            Result.Errors.Add(TEXT("无可用骨骼数据，跳过骨架构建"));
        }
    }

    // ============================================================
    // 步骤3: 解析模型JSON → 构建骨骼网格体
    // ============================================================
    FSekiroModelData ModelData;
    USkeletalMesh* SkeletalMesh = nullptr;

    if (Settings.bImportSkeletalMesh && !Settings.ModelJsonPath.IsEmpty())
    {
        REPORT_PROGRESS(TEXT("S3: 解析模型JSON: %s"), *Settings.ModelJsonPath);

        if (!FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelData))
        {
            Result.Errors.Add(TEXT("模型JSON解析失败"));
        }
        else
        {
            VerifyStep3_ModelData(ModelData);
        }
    }

    if (ModelData.Meshes.Num() > 0)
    {
        if (!Skeleton)
        {
            Result.Errors.Add(TEXT("构建骨骼网格体需要骨架，请提供动画JSON或启用骨架导入"));
        }
        else
        {
            FString MeshPath = FString::Printf(TEXT("%s/%s_SkeletalMesh"), *OutputBase, *ModelData.SkeletonName);
            REPORT_PROGRESS(TEXT("S4: 构建骨骼网格体 → %s"), *MeshPath);
            SkeletalMesh = FSekiroSkeletalMeshBuilder::Build(ModelData, Skeleton, MeshPath);

            if (SkeletalMesh)
            {
                Result.SkeletalMesh = SkeletalMesh;
                REPORT_PROGRESS(TEXT("S4: 骨骼网格体构建完成: %s (%d Section)"),
                    *SkeletalMesh->GetName(), ModelData.Meshes.Num());
                VerifyStep4_SkeletalMesh(SkeletalMesh, ModelData, Skeleton);
            }
            else
            {
                Result.Errors.Add(TEXT("骨骼网格体构建失败"));
            }
        }
    }

    // ============================================================
    // 步骤4.5: 导入贴图（移至步骤6动画之后，且仅首次运行需要）
    // 贴图同步导入极易触发Stall，暂跳过；材质可先创建无贴图版本
    // ============================================================
    REPORT_PROGRESS(TEXT("S4.5: 贴图导入跳过（避免主线程Stall，请手动导入或首次运行）"));

    // ============================================================
    // 步骤5: 构建材质
    // ============================================================
    if (Settings.bImportMaterials && ModelData.Materials.Num() > 0)
    {
        FString MaterialsPath = FString::Printf(TEXT("%s/Materials"), *OutputBase);
        REPORT_PROGRESS(TEXT("S5: 构建材质 → %s/"), *MaterialsPath);
        Result.Materials = FSekiroMaterialBuilder::BuildAll(ModelData, SkeletalMesh, MaterialsPath);
        REPORT_PROGRESS(TEXT("S5: 材质构建完成: %d 个"), Result.Materials.Num());
        VerifyStep5_Materials(Result.Materials, SkeletalMesh);
    }

    // ============================================================
    // 步骤6: 构建动画
    // ============================================================
    if (Settings.bImportAnimations && AnimParseResult.Clips.Num() > 0)
    {
        if (!Skeleton)
        {
            Result.Errors.Add(TEXT("构建动画需要骨架，跳过动画导入"));
        }
        else
        {
            FString AnimsPath = FString::Printf(TEXT("%s/Animations"), *OutputBase);
            REPORT_PROGRESS(TEXT("S6: 开始构建 %d 个动画 → %s/"), AnimParseResult.Clips.Num(), *AnimsPath);

            Result.Animations = FSekiroAnimationBuilder::BuildBatch(AnimParseResult.Clips, Skeleton, SkeletalMesh, AnimsPath);

            REPORT_PROGRESS(TEXT("S6: 动画构建完成: %d/%d 成功"),
                Result.Animations.Num(), AnimParseResult.Clips.Num());
            VerifyStep6_Animations(Result.Animations, Skeleton);

            if (Result.Animations.Num() < AnimParseResult.Clips.Num())
            {
                Result.Errors.Add(FString::Printf(TEXT("部分动画构建失败: %d/%d"),
                    AnimParseResult.Clips.Num() - Result.Animations.Num(), AnimParseResult.Clips.Num()));
            }
        }
    }

    // ============================================================
    // 汇总
    // ============================================================
    Result.bSuccess = Result.Errors.Num() == 0;

    REPORT_PROGRESS(TEXT("=== 导入完成 === 骨架=%s 网格体=%s 材质=%d 动画=%d 错误=%d"),
        Result.Skeleton ? TEXT("✓") : TEXT("✗"),
        Result.SkeletalMesh ? TEXT("✓") : TEXT("✗"),
        Result.Materials.Num(),
        Result.Animations.Num(),
        Result.Errors.Num());

    for (const FString& Err : Result.Errors)
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("  导入错误: %s"), *Err);
    }

    // 清理Undo栈，防止用户Ctrl+Z撤销导入导致资产损坏
    if (GEditor)
    {
        GEditor->ResetTransaction(NSLOCTEXT("SekiroImport", "ImportComplete", "Sekiro导入完成"));
    }

    return Result;
}

#undef REPORT_PROGRESS
#undef VERIFY_LOG
#undef VERIFY_CHECK
