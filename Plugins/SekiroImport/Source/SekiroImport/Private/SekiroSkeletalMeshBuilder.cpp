#include "SekiroSkeletalMeshBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "SkinnedAssetCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ============================================================================
// ExportRoot方向转换：RotZ(180°) * RotX(90°)
// 与SekiroSkeletonBuilder.cpp一致，用于将Y-up数据转换到UE5空间
// ============================================================================
static const FQuat MeshOrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);

// ============================================================================
// 骨骼名映射
// ============================================================================

TMap<FName, int32> FSekiroSkeletalMeshBuilder::BuildSkeletonBoneMap(const FReferenceSkeleton& RefSkel)
{
    TMap<FName, int32> BoneMap;
    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
    {
        BoneMap.Add(RefSkel.GetBoneName(i), i);
    }
    return BoneMap;
}

// ============================================================================
// 顶点权重重映射
// ============================================================================

void FSekiroSkeletalMeshBuilder::RemapInfluences(
    TArray<SkeletalMeshImportData::FRawBoneInfluence>& OutInfluences,
    const FSekiroImportMeshSection& Section, int32 GlobalVertOffset,
    const TMap<FName, int32>& BoneNameToSkelIndex, const TMap<int32, FName>& BoneIdxToName,
    const TArray<FVector>& BoneWorldPositions)
{
    int32 SkelBoneCount = BoneWorldPositions.Num();
    int32 OrphanVertexCount = 0;

    for (int32 v = 0; v < Section.Vertices.Num(); ++v)
    {
        const FSekiroImportVertex& Vert = Section.Vertices[v];

        bool bHasAnyValidInfluence = false;
        float TotalValidWeight = 0.0f;

        for (int32 inf = 0; inf < Vert.GetNumInfluences(); ++inf)
        {
            float Weight = Vert.BoneWeights[inf];
            if (Weight <= 0.0f) continue;

            int32 LocalBoneIdx = Vert.BoneIndices[inf];

            // 通过BoneIdxToName查找局部骨骼索引对应的骨骼名
            const FName* BoneName = BoneIdxToName.Find(LocalBoneIdx);
            if (!BoneName)
            {
                UE_LOG(LogSekiroImport, Warning,
                    TEXT("顶点 %d: 局部骨骼索引 %d 在BoneIdxToName中未找到, 跳过该影响"),
                    GlobalVertOffset + v, LocalBoneIdx);
                continue;
            }

            // 通过骨骼名查找全局骨架索引
            const int32* SkelIdx = BoneNameToSkelIndex.Find(*BoneName);
            if (!SkelIdx)
            {
                // 骨骼不在骨架中 → 跳过该影响（不回退到Root，避免拉出长刺）
                continue;
            }

            SkeletalMeshImportData::FRawBoneInfluence Influence;
            Influence.VertexIndex = GlobalVertOffset + v;
            Influence.BoneIndex = *SkelIdx;
            Influence.Weight = Weight;
            OutInfluences.Add(Influence);
            bHasAnyValidInfluence = true;
            TotalValidWeight += Weight;
        }

        // 所有影响都指向不存在的骨骼 → 用3D距离找最近的骨架骨骼
        if (!bHasAnyValidInfluence)
        {
            FVector VertPos = MeshOrientQ.RotateVector(FVector(Vert.Position));
            float BestDist = FLT_MAX;
            int32 BestBoneIdx = 0;

            for (int32 b = 0; b < SkelBoneCount; ++b)
            {
                float Dist = FVector::DistSquared(VertPos, BoneWorldPositions[b]);
                if (Dist < BestDist)
                {
                    BestDist = Dist;
                    BestBoneIdx = b;
                }
            }

            SkeletalMeshImportData::FRawBoneInfluence FallbackInf;
            FallbackInf.VertexIndex = GlobalVertOffset + v;
            FallbackInf.BoneIndex = BestBoneIdx;
            FallbackInf.Weight = 1.0f;
            OutInfluences.Add(FallbackInf);

            UE_LOG(LogSekiroImport, Warning,
                TEXT("顶点 %d: 所有骨骼(%d个)均不在骨架中, 用3D距离回退到骨架骨骼[%d] (距离=%.1fcm)"),
                GlobalVertOffset + v, Vert.GetNumInfluences(), BestBoneIdx, FMath::Sqrt(BestDist));

            ++OrphanVertexCount;
        }
    }

    if (OrphanVertexCount > 0)
    {
        UE_LOG(LogSekiroImport, Warning,
            TEXT("Section '%s': %d 个顶点无有效蒙皮骨骼, 已用最近骨骼回退"),
            *Section.PartName, OrphanVertexCount);
    }
}

// ============================================================================
// 参考骨骼填充
// ============================================================================

void FSekiroSkeletalMeshBuilder::FillRefBones(const FReferenceSkeleton& RefSkel, TArray<SkeletalMeshImportData::FBone>& OutRefBones)
{
    const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();
    OutRefBones.Reserve(RefSkel.GetNum());

    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
    {
        SkeletalMeshImportData::FBone ImpBone;
        ImpBone.Name = RefSkel.GetBoneName(i).ToString();
        ImpBone.ParentIndex = RefSkel.GetParentIndex(i);
        ImpBone.BonePos.Transform = FTransform3f(RefPose[i]);
        OutRefBones.Add(ImpBone);
    }
}

// ============================================================================
// 主构建入口
// ============================================================================

USkeletalMesh* FSekiroSkeletalMeshBuilder::Build(const FSekiroModelData& ModelData, USkeleton* Skeleton, const FString& PackagePath)
{
    if (ModelData.Meshes.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("模型数据中没有网格体，无法构建SkeletalMesh"));
        return nullptr;
    }

    if (!Skeleton)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("骨架为空，无法构建SkeletalMesh"));
        return nullptr;
    }

    UE_LOG(LogSekiroImport, Log, TEXT("开始构建骨骼网格体 → %s (%d 个Section)"), *PackagePath, ModelData.Meshes.Num());

    // 创建Package
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackagePath);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建Package: %s"), *PackagePath);
        return nullptr;
    }

    // 创建USkeletalMesh
    FString AssetName = FString::Printf(TEXT("%s_SkeletalMesh"), *ModelData.SkeletonName);
    USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, USkeletalMesh::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    if (!SkeletalMesh)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建USkeletalMesh: %s"), *AssetName);
        return nullptr;
    }

    SkeletalMesh->SetSkeleton(Skeleton);

    // 构建BoneName→SkeletonIndex查找表
    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TMap<FName, int32> BoneNameToSkelIndex = BuildSkeletonBoneMap(RefSkel);

    // 预计算FK骨骼World位置（用于Fallback最近骨骼查找）
    TArray<FVector> BoneWorldPositions;
    BoneWorldPositions.SetNum(RefSkel.GetNum());
    {
        const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();
        TArray<FTransform> BoneWorldTransforms;
        BoneWorldTransforms.SetNum(RefSkel.GetNum());
        for (int32 b = 0; b < RefSkel.GetNum(); ++b)
        {
            int32 ParentIdx = RefSkel.GetParentIndex(b);
            if (ParentIdx != INDEX_NONE && ParentIdx < b)
            {
                BoneWorldTransforms[b] = RefPose[b] * BoneWorldTransforms[ParentIdx];
            }
            else
            {
                BoneWorldTransforms[b] = RefPose[b];
            }
            BoneWorldPositions[b] = BoneWorldTransforms[b].GetTranslation();
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("骨架有 %d 根骨骼, 查找表已构建"), RefSkel.GetNum());

    // ============== 填充FSkeletalMeshImportData ==============

    FSkeletalMeshImportData ImportData;
    ImportData.bHasNormals = true;
    ImportData.bHasVertexColors = false;
    ImportData.NumTexCoords = 1;
    ImportData.MaxMaterialIndex = FMath::Max(0, ModelData.Materials.Num() - 1);

    int32 GlobalVertexOffset = 0;

    for (int32 SectionIdx = 0; SectionIdx < ModelData.Meshes.Num(); ++SectionIdx)
    {
        const FSekiroImportMeshSection& Section = ModelData.Meshes[SectionIdx];

        // 跳过Havok物理模拟的布料网格（fray/frary），与Blender行为一致
        bool bIsDecal = false;
        if (Section.MaterialIndex >= 0 && Section.MaterialIndex < ModelData.Materials.Num())
        {
            const FSekiroImportMaterial& Mat = ModelData.Materials[Section.MaterialIndex];
            FString MatName = Mat.Name.ToLower();
            if (MatName.Contains(TEXT("fray")) || MatName.Contains(TEXT("frary")))
            {
                UE_LOG(LogSekiroImport, Log, TEXT("  跳过布料物理Section[%d] '%s' (材质=%s)"), SectionIdx, *Section.PartName, *MatName);
                continue;
            }
            bIsDecal = Mat.MTDPath.ToLower().Contains(TEXT("decal"));
        }

        // --- Points（顶点位置）---
        for (const FSekiroImportVertex& Vert : Section.Vertices)
        {
            FVector3f Pos = Vert.Position;

            // Decal法线偏移（对齐Blender管线: pos += normal * 0.0008m → 0.08cm）
            if (bIsDecal)
                Pos += Vert.Normal * 0.08f;

            // 施加ExportRoot旋转，使顶点与骨架在同一坐标空间
            Pos = FVector3f(MeshOrientQ.RotateVector(FVector(Pos)));

            ImportData.Points.Add(Pos);
        }

        // --- Wedges + Faces（每个三角形角点一个Wedge，满足NumFaces*3==NumWedges约束）---
        int32 SectionWedgeStart = ImportData.Wedges.Num();
        for (const FIntVector& Tri : Section.Triangles)
        {
            // 验证三角形索引合法性
            if (Tri.X < 0 || Tri.X >= Section.Vertices.Num() ||
                Tri.Y < 0 || Tri.Y >= Section.Vertices.Num() ||
                Tri.Z < 0 || Tri.Z >= Section.Vertices.Num())
            {
                UE_LOG(LogSekiroImport, Warning,
                    TEXT("无效三角形索引 (%d,%d,%d) 在Section '%s'中 (顶点数=%d), 跳过"),
                    Tri.X, Tri.Y, Tri.Z, *Section.PartName, Section.Vertices.Num());
                continue;
            }

            const FSekiroImportVertex& V0 = Section.Vertices[Tri.X];
            const FSekiroImportVertex& V1 = Section.Vertices[Tri.Y];
            const FSekiroImportVertex& V2 = Section.Vertices[Tri.Z];

            SkeletalMeshImportData::FVertex Wedge0;
            Wedge0.VertexIndex = GlobalVertexOffset + Tri.X;
            Wedge0.UVs[0] = V0.UV;
            Wedge0.Color = FColor::White;
            Wedge0.MatIndex = Section.MaterialIndex;
            ImportData.Wedges.Add(Wedge0);

            // 三角形绕序反转: (X,Y,Z) → (X,Z,Y)，对齐Blender管线
            SkeletalMeshImportData::FVertex Wedge1;
            Wedge1.VertexIndex = GlobalVertexOffset + Tri.Z;
            Wedge1.UVs[0] = V2.UV;
            Wedge1.Color = FColor::White;
            Wedge1.MatIndex = Section.MaterialIndex;
            ImportData.Wedges.Add(Wedge1);

            SkeletalMeshImportData::FVertex Wedge2;
            Wedge2.VertexIndex = GlobalVertexOffset + Tri.Y;
            Wedge2.UVs[0] = V1.UV;
            Wedge2.Color = FColor::White;
            Wedge2.MatIndex = Section.MaterialIndex;
            ImportData.Wedges.Add(Wedge2);

            int32 CurWedge = ImportData.Wedges.Num() - 3;
            SkeletalMeshImportData::FTriangle Face;
            Face.WedgeIndex[0] = CurWedge;
            Face.WedgeIndex[1] = CurWedge + 1;
            Face.WedgeIndex[2] = CurWedge + 2;
            Face.MatIndex = Section.MaterialIndex;
            Face.SmoothingGroups = 0;
            ImportData.Faces.Add(Face);
        }

        // --- Influences（顶点蒙皮权重，重映射局部骨骼索引→骨架索引）---
        RemapInfluences(ImportData.Influences, Section, GlobalVertexOffset,
            BoneNameToSkelIndex, Section.BoneIdxToName, BoneWorldPositions);

        GlobalVertexOffset += Section.Vertices.Num();

        UE_LOG(LogSekiroImport, Verbose, TEXT("  Section[%d] '%s': %d顶点, %d三角形, MatIndex=%d"),
            SectionIdx, *Section.PartName, Section.Vertices.Num(), Section.Triangles.Num(), Section.MaterialIndex);
    }

    // --- RefBonesBinary（参考姿势，从骨架获取）---
    FillRefBones(RefSkel, ImportData.RefBonesBinary);

    // --- PointToRawMap（恒等映射，新导入数据无重映射）---
    ImportData.PointToRawMap.AddUninitialized(ImportData.Points.Num());
    for (int32 i = 0; i < ImportData.Points.Num(); ++i)
    {
        ImportData.PointToRawMap[i] = i;
    }

    // --- Materials（材质导入名）---
    for (const FSekiroImportMaterial& Mat : ModelData.Materials)
    {
        SkeletalMeshImportData::FMaterial ImpMat;
        ImpMat.MaterialImportName = Mat.Name;
        ImportData.Materials.Add(ImpMat);
    }

    UE_LOG(LogSekiroImport, Log, TEXT("ImportData: %d Points, %d Wedges, %d Faces, %d Influences, %d Materials, %d RefBones"),
        ImportData.Points.Num(), ImportData.Wedges.Num(), ImportData.Faces.Num(),
        ImportData.Influences.Num(), ImportData.Materials.Num(), ImportData.RefBonesBinary.Num());

    // ============== 初始化LOD模型并导入几何数据 ==============

    // 初始化LOD0模型（参照FBX导入流程，避免ReplaceSkeletalMeshGeometryImportData check失败）
    FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
    ImportedResource->LODModels.Empty();
    ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
    SkeletalMesh->AddLODInfo();

    // 设置材质槽（必须在Replace之前，因为该函数从mesh读取材质列表）
    SkeletalMesh->GetMaterials().Reset();
    for (const FSekiroImportMaterial& Mat : ModelData.Materials)
    {
        FSkeletalMaterial SkeletalMat;
        SkeletalMat.MaterialSlotName = FName(*Mat.Name);
        SkeletalMat.ImportedMaterialSlotName = FName(*Mat.Name);
        SkeletalMesh->GetMaterials().Add(SkeletalMat);
    }

    // 将参考骨骼同步到SkeletalMesh自身的RefSkeleton（Builder构建时需要）
    {
        FReferenceSkeletonModifier RefSkelModifier(SkeletalMesh->GetRefSkeleton(), Skeleton);
        for (int32 i = 0; i < RefSkel.GetNum(); ++i)
        {
            RefSkelModifier.Add(
                FMeshBoneInfo(RefSkel.GetBoneName(i), RefSkel.GetBoneName(i).ToString(), RefSkel.GetParentIndex(i)),
                RefSkel.GetRefBonePose()[i]);
        }
    }

    // 保存LOD0原始导入数据（首次导入，非Reimport，无需合并旧数据）
    SkeletalMesh->InvalidateDeriveDataCacheGUID();
    SkeletalMesh->SaveLODImportedData(0, ImportData);
    SkeletalMesh->SetLODImportedDataVersions(0,
        ESkeletalMeshGeoImportVersions::LatestVersion,
        ESkeletalMeshSkinningImportVersions::LatestVersion);

    // 调用USkeletalMesh::Build()构建LOD源数据和渲染数据
    SkeletalMesh->Build();
    if (SkeletalMesh->IsCompiling())
    {
        FSkinnedAssetCompilingManager::Get().FinishCompilation({SkeletalMesh});
    }

    // Build()不会调用CalculateInvRefMatrices(), 必须在保存前显式计算,
    // 否则序列化的RefBasesInvMatrix为空, 缩略图渲染时check()失败
    SkeletalMesh->CalculateInvRefMatrices();

    // ============== LOD构建后验证 ==============
    {
        FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
        if (ImportedModel && ImportedModel->LODModels.Num() > 0)
        {
            const FSkeletalMeshLODModel& LOD0 = ImportedModel->LODModels[0];
            UE_LOG(LogSekiroImport, Warning, TEXT("[LOD检查] Sections=%d, Vertices=%d, Indices=%d, ActiveBones=%d, RequiredBones=%d"),
                LOD0.Sections.Num(), LOD0.NumVertices, LOD0.IndexBuffer.Num(),
                LOD0.ActiveBoneIndices.Num(), LOD0.RequiredBones.Num());

            for (int32 s = 0; s < FMath::Min(3, LOD0.Sections.Num()); ++s)
            {
                const FSkelMeshSection& Sec = LOD0.Sections[s];
                UE_LOG(LogSekiroImport, Warning, TEXT("  LOD.Sec[%d]: Material=%d, Verts=%d, Tris=%d, BoneMap=%d, bDisabled=%d"),
                    s, Sec.MaterialIndex, Sec.NumVertices, Sec.NumTriangles, Sec.BoneMap.Num(), Sec.bDisabled);
            }

            // LOD诊断: 基本健全性检查
            if (LOD0.NumVertices > 0 && LOD0.Sections.Num() > 0)
            {
                // 非fray段数量（LOD应与此一致）
                int32 NonFraySections = 0;
                for (const FSekiroImportMeshSection& Sec : ModelData.Meshes)
                {
                    bool bSkip = false;
                    if (Sec.MaterialIndex >= 0 && Sec.MaterialIndex < ModelData.Materials.Num())
                    {
                        FString MatName = ModelData.Materials[Sec.MaterialIndex].Name.ToLower();
                        bSkip = MatName.Contains(TEXT("fray")) || MatName.Contains(TEXT("frary"));
                    }
                    if (!bSkip) ++NonFraySections;
                }
                UE_LOG(LogSekiroImport, Warning, TEXT("[LOD诊断] LOD0=%d顶点 %dSections | Import=%d顶点 %d非fraySections | wedge膨胀率=%.2fx"),
                    LOD0.NumVertices, LOD0.Sections.Num(),
                    ImportData.Points.Num(), NonFraySections,
                    (float)LOD0.NumVertices / FMath::Max(1, ImportData.Points.Num()));
                if (LOD0.Sections.Num() != NonFraySections)
                {
                    UE_LOG(LogSekiroImport, Error, TEXT("[LOD诊断] Section数量不匹配! LOD=%d, 非fray=%d"), LOD0.Sections.Num(), NonFraySections);
                }
            }
        }
    }

    // ============== 顶点-骨骼距离诊断 ==============
    // 验证顶点和参考姿态骨骼在同一坐标空间中
    {
        const FReferenceSkeleton& DiagRefSkel = Skeleton->GetReferenceSkeleton();
        const TArray<FTransform>& RefPose = DiagRefSkel.GetRefBonePose();

        // 检查Build()是否修改了Mesh的RefSkeleton
        {
            const FReferenceSkeleton& MeshRefSkel2 = SkeletalMesh->GetRefSkeleton();
            const TArray<FTransform>& MeshRefPose2 = MeshRefSkel2.GetRefBonePose();
            int32 Mismatches = 0;
            for (int32 b = 0; b < FMath::Min(RefPose.Num(), MeshRefPose2.Num()); ++b)
            {
                float PosDiff = FVector::Dist(RefPose[b].GetTranslation(), MeshRefPose2[b].GetTranslation());
                if (PosDiff > 0.01f)
                {
                    UE_LOG(LogSekiroImport, Warning, TEXT("[RefSkelBuildCheck] Bone[%d] '%s': PreBuild=(%.1f,%.1f,%.1f) PostBuild=(%.1f,%.1f,%.1f) delta=%.2f"),
                        b, *DiagRefSkel.GetBoneName(b).ToString(),
                        RefPose[b].GetTranslation().X, RefPose[b].GetTranslation().Y, RefPose[b].GetTranslation().Z,
                        MeshRefPose2[b].GetTranslation().X, MeshRefPose2[b].GetTranslation().Y, MeshRefPose2[b].GetTranslation().Z,
                        PosDiff);
                    ++Mismatches;
                }
            }
            if (Mismatches == 0)
            {
                UE_LOG(LogSekiroImport, Warning, TEXT("[RefSkelBuildCheck] Build()未修改RefSkeleton: %d骨骼全部匹配"), RefPose.Num());
            }
            else
            {
                UE_LOG(LogSekiroImport, Error, TEXT("[RefSkelBuildCheck] Build()修改了%d根骨骼的RefSkeleton!"), Mismatches);
            }
        }

        // FK计算所有骨骼的WorldTransform
        TArray<FTransform> BoneWorldTransforms;
        BoneWorldTransforms.SetNum(RefPose.Num());
        for (int32 b = 0; b < RefPose.Num(); ++b)
        {
            const FTransform& Local = RefPose[b];
            int32 ParentIdx = DiagRefSkel.GetParentIndex(b);
            if (ParentIdx != INDEX_NONE && ParentIdx < b)
            {
                BoneWorldTransforms[b] = Local * BoneWorldTransforms[ParentIdx];
            }
            else
            {
                BoneWorldTransforms[b] = Local;
            }
        }

        // 采样前3个顶点及其主骨骼
        int32 SamplesLogged = 0;
        for (int32 s = 0; s < ModelData.Meshes.Num() && SamplesLogged < 5; ++s)
        {
            const FSekiroImportMeshSection& Sec = ModelData.Meshes[s];
            for (int32 v = 0; v < FMath::Min(3, Sec.Vertices.Num()) && SamplesLogged < 5; ++v)
            {
                const FSekiroImportVertex& Vert = Sec.Vertices[v];

                // 找到主骨骼（最高权重）
                int32 BestLocalIdx = 0;
                float BestWeight = 0.0f;
                for (int32 inf = 0; inf < Vert.GetNumInfluences(); ++inf)
                {
                    if (Vert.BoneWeights[inf] > BestWeight)
                    {
                        BestWeight = Vert.BoneWeights[inf];
                        BestLocalIdx = Vert.BoneIndices[inf];
                    }
                }

                // 查找骨骼名
                const FName* BoneName = Sec.BoneIdxToName.Find(BestLocalIdx);
                if (!BoneName) continue;

                // 查找骨架索引
                int32 SkelIdx = INDEX_NONE;
                for (int32 b = 0; b < DiagRefSkel.GetNum(); ++b)
                {
                    if (DiagRefSkel.GetBoneName(b) == *BoneName)
                    {
                        SkelIdx = b;
                        break;
                    }
                }
                if (SkelIdx == INDEX_NONE) continue;

                FVector VertPosUE = MeshOrientQ.RotateVector(FVector(Vert.Position));
                FVector BoneFKPos = BoneWorldTransforms[SkelIdx].GetTranslation();
                float Dist = FVector::Dist(VertPosUE, BoneFKPos);

                UE_LOG(LogSekiroImport, Warning,
                    TEXT("[顶点诊断] Sec[%d].Vert[%d] Pos_UE=(%.1f,%.1f,%.1f) 主骨骼='%s'(SkIdx=%d) FK=(%.1f,%.1f,%.1f) Dist=%.1fcm Weight=%.2f"),
                    s, v,
                    VertPosUE.X, VertPosUE.Y, VertPosUE.Z,
                    *BoneName->ToString(), SkelIdx,
                    BoneFKPos.X, BoneFKPos.Y, BoneFKPos.Z,
                    Dist, BestWeight);
                ++SamplesLogged;
            }
        }

        // 打印RefBasesInvMatrix前3个矩阵的诊断
        const TArray<FMatrix44f>& InvMat = SkeletalMesh->GetRefBasesInvMatrix();
        UE_LOG(LogSekiroImport, Warning, TEXT("[RefBasesInvMatrix] 总数=%d"), InvMat.Num());
        for (int32 b = 0; b < FMath::Min(3, InvMat.Num()); ++b)
        {
            FVector InvTrans = FVector(InvMat[b].M[3][0], InvMat[b].M[3][1], InvMat[b].M[3][2]);
            FVector BonePos = BoneWorldTransforms[b].GetTranslation();
            UE_LOG(LogSekiroImport, Warning, TEXT("  Bone[%d] '%s': RefPos=(%.1f,%.1f,%.1f) InvTrans=(%.3f,%.3f,%.3f)"),
                b, *DiagRefSkel.GetBoneName(b).ToString(),
                BonePos.X, BonePos.Y, BonePos.Z,
                InvTrans.X, InvTrans.Y, InvTrans.Z);
        }
    }

    // ============== 蒙皮诊断文件输出 ==============
    {
        const FReferenceSkeleton& DiagRefSkel = Skeleton->GetReferenceSkeleton();
        const TArray<FTransform>& RefPose = DiagRefSkel.GetRefBonePose();

        // FK计算所有骨骼WorldTransform
        TArray<FTransform> BoneWorldTransforms;
        BoneWorldTransforms.SetNum(RefPose.Num());
        for (int32 b = 0; b < RefPose.Num(); ++b)
        {
            int32 ParentIdx = DiagRefSkel.GetParentIndex(b);
            if (ParentIdx != INDEX_NONE && ParentIdx < b)
                BoneWorldTransforms[b] = RefPose[b] * BoneWorldTransforms[ParentIdx];
            else
                BoneWorldTransforms[b] = RefPose[b];
        }

        FString DiagDir = FPaths::ProjectSavedDir() / TEXT("Logs/SekiroSkinDiag");
        IFileManager::Get().MakeDirectory(*DiagDir, true);

        // 文件1: 骨骼FK位置
        {
            FString BonesOut;
            BonesOut.Reserve(RefPose.Num() * 128);
            for (int32 b = 0; b < RefPose.Num(); ++b)
            {
                FVector Pos = BoneWorldTransforms[b].GetTranslation();
                BonesOut += FString::Printf(TEXT("%d|%s|%d|%.4f|%.4f|%.4f\n"),
                    b, *DiagRefSkel.GetBoneName(b).ToString(), DiagRefSkel.GetParentIndex(b),
                    Pos.X, Pos.Y, Pos.Z);
            }
            FFileHelper::SaveStringToFile(BonesOut, *(DiagDir / TEXT("Bones_Cpp.txt")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogSekiroImport, Warning, TEXT("[皮肤诊断] 骨骼文件已写入: %d根"), RefPose.Num());
        }

        // 文件2: 顶点->骨骼映射
        {
            TMap<FName, int32> BoneNameToSkelIdx;
            for (int32 b = 0; b < DiagRefSkel.GetNum(); ++b)
                BoneNameToSkelIdx.Add(DiagRefSkel.GetBoneName(b), b);

            FString VertOut;
            int32 TotalInfluences = 0;
            int32 GlobalVertIdx = 0;

            for (int32 s = 0; s < ModelData.Meshes.Num(); ++s)
            {
                const FSekiroImportMeshSection& Sec = ModelData.Meshes[s];
                for (int32 v = 0; v < Sec.Vertices.Num(); ++v)
                {
                    const FSekiroImportVertex& Vert = Sec.Vertices[v];
                    FVector VPos = MeshOrientQ.RotateVector(FVector(Vert.Position));

                    for (int32 inf = 0; inf < Vert.GetNumInfluences(); ++inf)
                    {
                        float Weight = Vert.BoneWeights[inf];
                        if (Weight <= 0.0f) continue;

                        int32 LocalBoneIdx = Vert.BoneIndices[inf];
                        const FName* BoneName = Sec.BoneIdxToName.Find(LocalBoneIdx);
                        FString ResolvedName = BoneName ? BoneName->ToString() : TEXT("?");
                        int32 SkelIdx = BoneName ? BoneNameToSkelIdx.FindRef(*BoneName) : INDEX_NONE;
                        FVector BonePos = (SkelIdx >= 0 && SkelIdx < BoneWorldTransforms.Num())
                            ? BoneWorldTransforms[SkelIdx].GetTranslation() : FVector::ZeroVector;

                        VertOut += FString::Printf(TEXT("%d|%d|%.4f|%.4f|%.4f|%d|%s|%d|%.6f|%.4f|%.4f|%.4f\n"),
                            s, GlobalVertIdx,
                            VPos.X, VPos.Y, VPos.Z,
                            LocalBoneIdx, *ResolvedName, SkelIdx, Weight,
                            BonePos.X, BonePos.Y, BonePos.Z);
                        ++TotalInfluences;
                    }
                    ++GlobalVertIdx;
                }
            }
            FFileHelper::SaveStringToFile(VertOut, *(DiagDir / TEXT("Verts_Cpp.txt")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogSekiroImport, Warning, TEXT("[皮肤诊断] 顶点文件已写入: %d顶点, %d条影响"),
                GlobalVertIdx, TotalInfluences);
        }
    }

    SkeletalMesh->MarkPackageDirty();

    // ============== 保存 ==============

    FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;

    if (UPackage::SavePackage(Package, SkeletalMesh, *PackageFileName, SaveArgs))
    {
        UE_LOG(LogSekiroImport, Log, TEXT("骨骼网格体构建成功: %s"), *PackageFileName);
    }
    else
    {
        UE_LOG(LogSekiroImport, Error, TEXT("骨骼网格体保存失败: %s"), *PackageFileName);
    }

    return SkeletalMesh;
}
