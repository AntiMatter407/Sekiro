#include "SekiroImportVerifier.h"
#include "SekiroImportLog.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "ReferenceSkeleton.h"

namespace
{
	void Log(const TCHAR* Fmt, ...)
	{
		TCHAR Buf[1024];
		va_list Args;
		va_start(Args, Fmt);
		FCString::GetVarArgs(Buf, UE_ARRAY_COUNT(Buf), Fmt, Args);
		va_end(Args);
		UE_LOG(LogSekiroImport, Warning, TEXT("[验证] %s"), Buf);
	}

	void Check(bool bOK, const TCHAR* Fmt, ...)
	{
		TCHAR Buf[1024];
		va_list Args;
		va_start(Args, Fmt);
		FCString::GetVarArgs(Buf, UE_ARRAY_COUNT(Buf), Fmt, Args);
		va_end(Args);
		if (!bOK)
		{
			UE_LOG(LogSekiroImport, Error, TEXT("[验证失败] %s"), Buf);
		}
		else
		{
			UE_LOG(LogSekiroImport, Warning, TEXT("[验证通过] %s"), Buf);
		}
	}
}

void FSekiroImportVerifier::Bones(const TArray<FSekiroImportBone>& Bones, const FString& Source)
{
	Log(TEXT("========== S1: 骨骼数据验证 (%s) =========="), *Source);
	Log(TEXT("  骨骼总数: %d"), Bones.Num());
	Check(Bones.Num() > 0, TEXT("骨骼数>0"));

	int32 HierarchyErrors = 0;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (Bones[i].ParentIndex != INDEX_NONE && Bones[i].ParentIndex >= i)
		{
			Log(TEXT("  [层级错误] Bone[%d] '%s' ParentIndex=%d >= %d"),
				i, *Bones[i].Name.ToString(), Bones[i].ParentIndex, i);
			++HierarchyErrors;
		}
	}
	Check(HierarchyErrors == 0, TEXT("层级顺序: %d 错误"), HierarchyErrors);

	TSet<FName> Names;
	int32 Duplicates = 0;
	for (const auto& B : Bones)
	{
		if (Names.Contains(B.Name)) { ++Duplicates; Log(TEXT("  [重复名] %s"), *B.Name.ToString()); }
		else { Names.Add(B.Name); }
	}
	Check(Duplicates == 0, TEXT("骨骼名唯一: %d 重复"), Duplicates);

	if (Bones.Num() > 0)
	{
		Log(TEXT("  Root骨骼: '%s' ParentIndex=%d"), *Bones[0].Name.ToString(), Bones[0].ParentIndex);
		Log(TEXT("  Root LocalPos: (%.2f, %.2f, %.2f)"), Bones[0].LocalTranslation.X, Bones[0].LocalTranslation.Y, Bones[0].LocalTranslation.Z);
		Log(TEXT("  Root LocalRot: (%.4f, %.4f, %.4f, %.4f)"), Bones[0].LocalRotation.X, Bones[0].LocalRotation.Y, Bones[0].LocalRotation.Z, Bones[0].LocalRotation.W);
		Log(TEXT("  Root LocalScale: (%.3f, %.3f, %.3f)"), Bones[0].LocalScale.X, Bones[0].LocalScale.Y, Bones[0].LocalScale.Z);
		Check(Bones[0].ParentIndex == INDEX_NONE, TEXT("Root骨骼ParentIndex==-1"));
	}

	Log(TEXT("  --- 前5根骨骼 ---"));
	for (int32 i = 0; i < FMath::Min(5, Bones.Num()); ++i)
	{
		Log(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
			i, *Bones[i].Name.ToString(), Bones[i].ParentIndex,
			Bones[i].LocalTranslation.X, Bones[i].LocalTranslation.Y, Bones[i].LocalTranslation.Z);
	}
	if (Bones.Num() > 5)
	{
		Log(TEXT("  --- 后3根骨骼 ---"));
		for (int32 i = FMath::Max(5, Bones.Num() - 3); i < Bones.Num(); ++i)
		{
			Log(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
				i, *Bones[i].Name.ToString(), Bones[i].ParentIndex,
				Bones[i].LocalTranslation.X, Bones[i].LocalTranslation.Y, Bones[i].LocalTranslation.Z);
		}
	}

	int32 IKBoneCount = 0;
	for (const auto& B : Bones)
	{
		FString Name = B.Name.ToString();
		if (Name.Contains(TEXT("_Target")) || Name.Contains(TEXT("IK"))) ++IKBoneCount;
	}
	Log(TEXT("  IK辅助骨骼数: %d"), IKBoneCount);
	Log(TEXT("========== S1结束 =========="));
}

void FSekiroImportVerifier::Skeleton(USkeleton* Skeleton, int32 ExpectedBoneCount)
{
	Log(TEXT("========== S2: 骨架验证 =========="));
	if (!Skeleton) { Log(TEXT("  [失败] Skeleton == nullptr")); return; }

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	Log(TEXT("  资产名: %s"), *Skeleton->GetName());
	Log(TEXT("  参考骨骼数: %d (期望: %d)"), RefSkel.GetNum(), ExpectedBoneCount);
	Check(RefSkel.GetNum() == ExpectedBoneCount, TEXT("骨骼数匹配: %d == %d"), RefSkel.GetNum(), ExpectedBoneCount);

	int32 ParentErrors = 0;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		int32 P = RefSkel.GetParentIndex(i);
		if (P != INDEX_NONE && (P < 0 || P >= RefSkel.GetNum()))
		{
			Log(TEXT("  [父索引越界] Bone[%d] '%s' Parent=%d"), i, *RefSkel.GetBoneName(i).ToString(), P);
			++ParentErrors;
		}
	}
	Check(ParentErrors == 0, TEXT("所有父索引合法: %d 错误"), ParentErrors);

	Log(TEXT("  --- RefSkeleton前5根 ---"));
	for (int32 i = 0; i < FMath::Min(5, RefSkel.GetNum()); ++i)
	{
		const FTransform& Pose = RefSkel.GetRefBonePose()[i];
		Log(TEXT("  [%d] %-30s Parent=%d Pos=(%.1f, %.1f, %.1f)"),
			i, *RefSkel.GetBoneName(i).ToString(), RefSkel.GetParentIndex(i),
			Pose.GetTranslation().X, Pose.GetTranslation().Y, Pose.GetTranslation().Z);
	}
	Log(TEXT("========== S2结束 =========="));
}

void FSekiroImportVerifier::ModelData(const FSekiroModelData& ModelData)
{
	Log(TEXT("========== S3: 模型数据验证 =========="));
	Log(TEXT("  SkeletonName: %s"), *ModelData.SkeletonName);
	Log(TEXT("  Mesh Section数: %d"), ModelData.Meshes.Num());
	Log(TEXT("  材质数: %d"), ModelData.Materials.Num());
	Check(ModelData.Meshes.Num() > 0, TEXT("存在Mesh Section"));

	int32 TotalVerts = 0, TotalTris = 0;
	int32 TotalBoneMappings = 0;
	int32 TriIndexErrors = 0;

	for (int32 s = 0; s < ModelData.Meshes.Num(); ++s)
	{
		const FSekiroImportMeshSection& Sec = ModelData.Meshes[s];
		TotalVerts += Sec.Vertices.Num();
		TotalTris += Sec.Triangles.Num();
		TotalBoneMappings += Sec.BoneIdxToName.Num();

		for (const FIntVector& Tri : Sec.Triangles)
		{
			if (Tri.X < 0 || Tri.X >= Sec.Vertices.Num() ||
				Tri.Y < 0 || Tri.Y >= Sec.Vertices.Num() ||
				Tri.Z < 0 || Tri.Z >= Sec.Vertices.Num())
			{
				++TriIndexErrors;
			}
		}

		Log(TEXT("  Section[%d] '%s': %d顶点, %d三角形, MatIdx=%d, %d蒙皮骨骼"),
			s, *Sec.PartName, Sec.Vertices.Num(), Sec.Triangles.Num(),
			Sec.MaterialIndex, Sec.BoneIdxToName.Num());

		int32 MapCount = 0;
		for (const auto& Pair : Sec.BoneIdxToName)
		{
			if (MapCount >= 3) break;
			Log(TEXT("    BoneIdxToName[%d] = '%s'"), Pair.Key, *Pair.Value.ToString());
			++MapCount;
		}
	}

	Log(TEXT("  总计: %d顶点, %d三角形, %d骨骼映射条目"), TotalVerts, TotalTris, TotalBoneMappings);
	Check(TriIndexErrors == 0, TEXT("三角形索引范围: %d 越界"), TriIndexErrors);
	Check(TotalVerts > 0, TEXT("总顶点数>0"));
	Check(TotalTris > 0, TEXT("总三角形数>0"));

	for (int32 m = 0; m < ModelData.Materials.Num(); ++m)
	{
		const FSekiroImportMaterial& Mat = ModelData.Materials[m];
		Log(TEXT("  材质[%d]: %-40s Blend=%s 纹理槽=%d"),
			m, *Mat.Name, *Mat.BlendMode, Mat.TextureSlots.Num());
	}

	Log(TEXT("  模型自带骨骼数: %d (应为93, 子集)"), ModelData.Bones.Num());
	Log(TEXT("========== S3结束 =========="));
}

void FSekiroImportVerifier::SkeletalMesh(USkeletalMesh* Mesh, const FSekiroModelData& ModelData, USkeleton* Skeleton)
{
	Log(TEXT("========== S4: 骨骼网格体验证 =========="));
	if (!Mesh) { Log(TEXT("  [失败] SkeletalMesh == nullptr")); return; }

	Log(TEXT("  资产名: %s"), *Mesh->GetName());
	Check(Mesh->GetSkeleton() == Skeleton, TEXT("骨架引用正确"));
	Log(TEXT("  材质槽数: %d (期望: %d)"), Mesh->GetMaterials().Num(), ModelData.Materials.Num());
	Check(Mesh->GetMaterials().Num() == ModelData.Materials.Num(),
		TEXT("材质槽数匹配: %d == %d"), Mesh->GetMaterials().Num(), ModelData.Materials.Num());

	{
		const TArray<FMatrix44f>& InvMat = Mesh->GetRefBasesInvMatrix();
		const int32 ExpectedBones = Skeleton ? Skeleton->GetReferenceSkeleton().GetNum() : 0;
		Log(TEXT("  RefBasesInvMatrix.Num() = %d (期望: %d)"), InvMat.Num(), ExpectedBones);
		Check(InvMat.Num() > 0, TEXT("RefBasesInvMatrix非空: %d 个"), InvMat.Num());
	}

	{
		FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
		Check(RenderData != nullptr, TEXT("GetResourceForRendering() != nullptr"));
		if (RenderData)
		{
			Log(TEXT("  LODRenderData.Num() = %d"), RenderData->LODRenderData.Num());
			Check(RenderData->LODRenderData.Num() > 0, TEXT("有LOD渲染数据"));
		}
	}

	{
		FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (ImportedModel)
		{
			Log(TEXT("  LODModels.Num() = %d"), ImportedModel->LODModels.Num());
			if (ImportedModel->LODModels.Num() > 0)
			{
				const FSkeletalMeshLODModel& LOD0 = ImportedModel->LODModels[0];
				Log(TEXT("  LOD0: %d Sections, %d Vertices, %d IndexBuffer"),
					LOD0.Sections.Num(), LOD0.NumVertices, LOD0.IndexBuffer.Num());
			}
		}
	}

	{
		const FReferenceSkeleton& MeshRefSkel = Mesh->GetRefSkeleton();
		const FReferenceSkeleton& SkelRefSkel = Skeleton->GetReferenceSkeleton();
		Log(TEXT("  Mesh.RefSkeleton骨骼数: %d, Skeleton.RefSkeleton骨骼数: %d"),
			MeshRefSkel.GetNum(), SkelRefSkel.GetNum());

		int32 NameMismatches = 0;
		for (int32 i = 0; i < FMath::Min(5, FMath::Min(MeshRefSkel.GetNum(), SkelRefSkel.GetNum())); ++i)
		{
			if (MeshRefSkel.GetBoneName(i) != SkelRefSkel.GetBoneName(i))
			{
				Log(TEXT("  [不匹配] Bone[%d]: Mesh='%s' vs Skeleton='%s'"),
					i, *MeshRefSkel.GetBoneName(i).ToString(), *SkelRefSkel.GetBoneName(i).ToString());
				++NameMismatches;
			}
		}
		Check(NameMismatches == 0, TEXT("Mesh与Skeleton前5根骨骼名一致"));
	}

	Log(TEXT("========== S4结束 =========="));
}

void FSekiroImportVerifier::Materials(const TArray<UMaterial*>& Materials, USkeletalMesh* Mesh)
{
	Log(TEXT("========== S5: 材质验证 =========="));
	Log(TEXT("  材质数: %d"), Materials.Num());

	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		UMaterial* M = Materials[i];
		if (M)
		{
			Log(TEXT("  M[%d]: %s BlendMode=%d TwoSided=%d"),
				i, *M->GetName(), (int32)M->BlendMode, (int32)M->TwoSided);
		}
	}

	if (Mesh)
	{
		int32 AssignedCount = 0;
		for (int32 i = 0; i < Mesh->GetMaterials().Num(); ++i)
		{
			if (Mesh->GetMaterials()[i].MaterialInterface) ++AssignedCount;
		}
		Log(TEXT("  Mesh材质槽已分配: %d/%d"), AssignedCount, Mesh->GetMaterials().Num());
		Check(AssignedCount == Materials.Num(), TEXT("所有材质槽已分配到Mesh"));
	}

	Log(TEXT("========== S5结束 =========="));
}

void FSekiroImportVerifier::Animations(const TArray<UAnimSequence*>& Animations, USkeleton* Skeleton)
{
	Log(TEXT("========== S6: 动画验证 =========="));
	Log(TEXT("  动画总数: %d"), Animations.Num());
	Check(Animations.Num() > 0, TEXT("至少有1个动画"));

	for (int32 i = 0; i < FMath::Min(3, Animations.Num()); ++i)
	{
		UAnimSequence* Anim = Animations[i];
		if (!Anim) continue;

		Log(TEXT("  --- Anim[%d]: %s ---"), i, *Anim->GetName());
		Log(TEXT("    Skeleton: %s"), Anim->GetSkeleton() ? *Anim->GetSkeleton()->GetName() : TEXT("null"));
		Log(TEXT("    NumFrames: %d"), Anim->GetNumberOfSampledKeys());
		Log(TEXT("    SequenceLength: %.2f秒"), Anim->GetPlayLength());
		Log(TEXT("    FrameRate: %.1f"), Anim->GetSamplingFrameRate().AsDecimal());

		Check(Anim->GetSkeleton() == Skeleton, TEXT("动画骨架引用正确"));
		Check(Anim->GetNumberOfSampledKeys() > 0, TEXT("有关键帧数据"));
		Log(TEXT("    (完整轨道验证需进入Persona查看)"));
	}

	Log(TEXT("========== S6结束 =========="));
}
