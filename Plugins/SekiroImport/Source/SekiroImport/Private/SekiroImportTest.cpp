#include "SekiroImportTest.h"
#include "SekiroImportData.h"
#include "SekiroImportLog.h"
#include "SekiroModelParser.h"
#include "SekiroAnimationParser.h"
#include "SekiroSkeletonBuilder.h"
#include "SekiroSkeletalMeshBuilder.h"
#include "SekiroMaterialBuilder.h"
#include "SekiroAnimationBuilder.h"
#include "SekiroImportPipeline.h"
#include "SekiroImportSettings.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "ReferenceSkeleton.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

// Havok Y-up → UE5 Z-up 坐标转换: RotZ(180°) * RotX(90°)
const FQuat DiagOrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);

// ============================================================================
// 工具：写诊断文件
// ============================================================================

static void DumpIntermediateState(const TArray<FSekiroImportBone>& Bones, const FString& Dir, const TCHAR* Filename)
{
	// 输出关键骨骼的World和Local变换，用于诊断Y/Z交换的根源
	static const TArray<FString> KeyNames = {
		TEXT("Master"), TEXT("RootPos"), TEXT("RootRotXZ"), TEXT("RootRotY"),
		TEXT("Pelvis"), TEXT("L_Hip"), TEXT("Spine"), TEXT("face_root"),
	};
	FString Out;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FSekiroImportBone& B = Bones[i];
		if (!KeyNames.Contains(B.Name.ToString()))
			continue;
		Out += FString::Printf(TEXT("%d|%s|%d|W=(%.2f,%.2f,%.2f)|L=(%.2f,%.2f,%.2f)|LR=(%.4f,%.4f,%.4f,%.4f)\n"),
			i, *B.Name.ToString(), B.ParentIndex,
			B.WorldTranslation.X, B.WorldTranslation.Y, B.WorldTranslation.Z,
			B.LocalTranslation.X, B.LocalTranslation.Y, B.LocalTranslation.Z,
			B.LocalRotation.X, B.LocalRotation.Y, B.LocalRotation.Z, B.LocalRotation.W);
	}
	FFileHelper::SaveStringToFile(Out, *(Dir / Filename),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

static void DumpBonesFK(const TArray<FSekiroImportBone>& Bones, const FString& Dir)
{
	// FK计算世界位置
	TArray<FTransform> World;
	World.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		FTransform Local;
		Local.SetRotation(Bones[i].LocalRotation);
		Local.SetTranslation(Bones[i].LocalTranslation);
		Local.SetScale3D(Bones[i].LocalScale);

		if (Bones[i].ParentIndex >= 0 && Bones[i].ParentIndex < i)
			World[i] = Local * World[Bones[i].ParentIndex];
		else
			World[i] = Local;
	}

	FString Out;
	Out.Reserve(Bones.Num() * 128);
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		FVector P = World[i].GetTranslation();
		Out += FString::Printf(TEXT("%d|%s|%d|%.4f|%.4f|%.4f\n"),
			i, *Bones[i].Name.ToString(), Bones[i].ParentIndex, P.X, P.Y, P.Z);
	}
	FFileHelper::SaveStringToFile(Out, *(Dir / TEXT("Bones_Cpp.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Bones_Cpp.txt: %d根骨骼FK位置"), Bones.Num());
}

static void DumpRefSkeleton(const FReferenceSkeleton& RefSkel, const FString& Dir)
{
	FString Out;
	Out.Reserve(RefSkel.GetNum() * 128);
	const TArray<FTransform>& Poses = RefSkel.GetRefBonePose();
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		FVector Loc = Poses[i].GetTranslation();
		FQuat Rot = Poses[i].GetRotation();
		Out += FString::Printf(TEXT("%d|%s|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f\n"),
			i, *RefSkel.GetBoneName(i).ToString(), RefSkel.GetParentIndex(i),
			Loc.X, Loc.Y, Loc.Z,
			Rot.X, Rot.Y, Rot.Z, Rot.W);
	}
	FFileHelper::SaveStringToFile(Out, *(Dir / TEXT("RefSkel_Cpp.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] RefSkel_Cpp.txt: %d根骨骼Local变换"), RefSkel.GetNum());
}

static void DumpVertSkinning(const FSekiroModelData& ModelData, const FReferenceSkeleton& RefSkel, const FString& Dir)
{
	// FK计算所有骨骼的WorldTransform
	TArray<FTransform> BoneWorld;
	{
		const TArray<FTransform>& Poses = RefSkel.GetRefBonePose();
		BoneWorld.SetNum(RefSkel.GetNum());
		for (int32 i = 0; i < RefSkel.GetNum(); ++i)
		{
			int32 P = RefSkel.GetParentIndex(i);
			if (P >= 0 && P < i)
				BoneWorld[i] = Poses[i] * BoneWorld[P];
			else
				BoneWorld[i] = Poses[i];
		}
	}

	// 构建骨骼名→索引查找
	TMap<FName, int32> BoneNameToIdx;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
		BoneNameToIdx.Add(RefSkel.GetBoneName(i), i);

	FString Out;
	int32 GlobalVertIdx = 0;
	int32 TotalInfluences = 0;

	for (int32 s = 0; s < ModelData.Meshes.Num(); ++s)
	{
		const FSekiroImportMeshSection& Sec = ModelData.Meshes[s];
		for (int32 v = 0; v < Sec.Vertices.Num(); ++v)
		{
			const FSekiroImportVertex& Vert = Sec.Vertices[v];
			FVector VPos = DiagOrientQ.RotateVector(FVector(Vert.Position));

			for (int32 inf = 0; inf < Vert.GetNumInfluences(); ++inf)
			{
				float Weight = Vert.BoneWeights[inf];
				if (Weight <= 0.0f) continue;

				int32 LocalIdx = Vert.BoneIndices[inf];
				const FName* BoneName = Sec.BoneIdxToName.Find(LocalIdx);
				FString Name = BoneName ? BoneName->ToString() : TEXT("?");
				int32 SkelIdx = BoneName ? BoneNameToIdx.FindRef(*BoneName) : INDEX_NONE;
				FVector BonePos = (SkelIdx >= 0 && SkelIdx < BoneWorld.Num())
					? BoneWorld[SkelIdx].GetTranslation() : FVector::ZeroVector;

				Out += FString::Printf(TEXT("%d|%d|%.4f|%.4f|%.4f|%.4f|%.4f|%d|%s|%d|%.6f|%.4f|%.4f|%.4f\n"),
					s, GlobalVertIdx,
					VPos.X, VPos.Y, VPos.Z,
					Vert.UV.X, Vert.UV.Y,
					LocalIdx, *Name, SkelIdx, Weight,
					BonePos.X, BonePos.Y, BonePos.Z);
				++TotalInfluences;
			}
			++GlobalVertIdx;
		}
	}
	FFileHelper::SaveStringToFile(Out, *(Dir / TEXT("Verts_Cpp.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Verts_Cpp.txt: %d顶点, %d条影响, 含UV"), GlobalVertIdx, TotalInfluences);
}

static void DumpAnimTracks(const FSekiroAnimationClip& Clip, const FReferenceSkeleton& RefSkel, const FString& Dir)
{
    const FQuat OrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);

    // 构建骨骼名→动画骨骼索引查找表
    TMap<FName, int32> BoneNameToAnimIdx;
    for (int32 i = 0; i < Clip.BoneNames.Num(); ++i)
    {
        BoneNameToAnimIdx.Add(Clip.BoneNames[i], i);
    }

    const int32 SkeletonBoneCount = RefSkel.GetNum();

    FString Out;
    Out.Reserve(Clip.FrameData.Num() * SkeletonBoneCount * 128);

    TArray<FTransform> WorldUE;
    WorldUE.SetNum(SkeletonBoneCount);

    for (int32 Frame = 0; Frame < Clip.FrameData.Num(); ++Frame)
    {
        // Pass 1+2: FK(HKX) + OrientQ → WorldUE
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            const int32* AnimBoneIdxPtr = BoneNameToAnimIdx.Find(BoneName);

            FTransform LocalHKX = FTransform::Identity;
            if (AnimBoneIdxPtr != nullptr && *AnimBoneIdxPtr < Clip.FrameData[Frame].Num())
            {
                LocalHKX = Clip.FrameData[Frame][*AnimBoneIdxPtr];
            }

            // Pass 1: FK World HKX
            FTransform WorldHKX;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ParentWorldHKX;
                ParentWorldHKX.SetTranslation(OrientQInv.RotateVector(WorldUE[ParentIdx].GetTranslation()));
                ParentWorldHKX.SetRotation(OrientQInv * WorldUE[ParentIdx].GetRotation());
                ParentWorldHKX.SetScale3D(WorldUE[ParentIdx].GetScale3D());
                WorldHKX = LocalHKX * ParentWorldHKX;
            }
            else
            {
                WorldHKX = LocalHKX;
            }

            // Pass 2: OrientQ → World UE (左乘，对齐SkeletonBuilder)
            WorldUE[BoneIdx].SetTranslation(OrientQ.RotateVector(WorldHKX.GetTranslation()));
            WorldUE[BoneIdx].SetRotation(OrientQ * WorldHKX.GetRotation());
            WorldUE[BoneIdx].SetScale3D(WorldHKX.GetScale3D());
        }

        // Pass 3: Derive Local UE → 输出
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);

            FTransform LocalUE;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                LocalUE = WorldUE[BoneIdx].GetRelativeTransform(WorldUE[ParentIdx]);
            }
            else
            {
                LocalUE = WorldUE[BoneIdx];
            }

            FVector Pos = LocalUE.GetTranslation();
            FQuat Rot = LocalUE.GetRotation();
            Out += FString::Printf(TEXT("%s|%d|%d|%s|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f\n"),
                *Clip.Name, Frame, BoneIdx, *BoneName.ToString(),
                Pos.X, Pos.Y, Pos.Z,
                Rot.X, Rot.Y, Rot.Z, Rot.W);
        }
    }

    FString FileName = FString::Printf(TEXT("Anim_%s_Cpp.txt"), *Clip.Name);
    FFileHelper::SaveStringToFile(Out, *(Dir / FileName),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] %s: %s, %d帧, %d骨骼"),
        *FileName, *Clip.Name, Clip.FrameData.Num(), SkeletonBoneCount);
}

// ============================================================================
// 公共API
// ============================================================================

bool USekiroImportTest::ParseModelAndDump(const FString& JsonPath, const FString& OutputDir)
{
	FSekiroModelData Data;
	if (!FSekiroModelParser::ParseFromFile(JsonPath, Data))
		return false;

	IFileManager::Get().MakeDirectory(*OutputDir, true);

	// 输出FK骨骼位置（当前Y-up空间）
	DumpBonesFK(Data.Bones, OutputDir);

	// 输出材质摘要
	{
		FString MatSummary;
		for (int32 i = 0; i < Data.Materials.Num(); ++i)
		{
			const FSekiroImportMaterial& M = Data.Materials[i];
			MatSummary += FString::Printf(TEXT("%d|%s|%s|%s|%d\n"),
				i, *M.Name, *M.BlendMode, *M.MTDPath, M.TextureSlots.Num());
		}
		FFileHelper::SaveStringToFile(MatSummary, *(OutputDir / TEXT("Materials_Cpp.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] ParseModelAndDump: %d骨骼, %d材质, %dMesh -> %s"),
		Data.Bones.Num(), Data.Materials.Num(), Data.Meshes.Num(), *OutputDir);
	return true;
}

bool USekiroImportTest::ParseAnimationAndDump(const FString& JsonPath, const FString& OutputDir)
{
	FSekiroAnimationParser::FParseResult Result;
	if (!FSekiroAnimationParser::ParseFromFile(JsonPath, Result, 3)) // 仅前3个动画
		return false;

	IFileManager::Get().MakeDirectory(*OutputDir, true);

	DumpBonesFK(Result.Bones, OutputDir);

	// 动画摘要
	{
		FString AnimSummary;
		for (int32 i = 0; i < Result.Clips.Num(); ++i)
		{
			const FSekiroAnimationClip& C = Result.Clips[i];
			AnimSummary += FString::Printf(TEXT("%d|%s|%.3f|%d|%.1f\n"),
				i, *C.Name, C.Duration, C.FrameCount, C.SampleRate);
		}
		FFileHelper::SaveStringToFile(AnimSummary, *(OutputDir / TEXT("Anims_Cpp.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// 第一个动画的Anim_Cpp.txt
	if (Result.Clips.Num() > 0)
	{
		// 构建轻量FReferenceSkeleton用于ParentIndex查找
		TArray<FSekiroImportBone> TempBones = Result.Bones;
		FSekiroSkeletonBuilder::ApplyExportRootOrientation(TempBones);
		FReferenceSkeleton TempRefSkel;
		{
			FReferenceSkeletonModifier Modifier(TempRefSkel, nullptr);
			for (const FSekiroImportBone& B : TempBones)
			{
				Modifier.Add(FMeshBoneInfo(B.Name, B.Name.ToString(), B.ParentIndex),
					FTransform(B.LocalRotation, B.LocalTranslation, B.LocalScale));
			}
		}
		DumpAnimTracks(Result.Clips[0], TempRefSkel, OutputDir);
	}

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] ParseAnimationAndDump: %d骨骼, %d动画 -> %s"),
		Result.Bones.Num(), Result.Clips.Num(), *OutputDir);
	return true;
}

bool USekiroImportTest::BuildSkeletonAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir)
{
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	// 解析动画JSON → 146骨骼（HKX Local transforms）
	FSekiroAnimationParser::FParseResult AnimResult;
	if (!FSekiroAnimationParser::ParseFromFile(AnimJson, AnimResult, -1))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 动画JSON解析失败"));
		return false;
	}

	// 解析模型JSON → 93骨骼（WorldPos）
	FSekiroModelData ModelData;
	if (!FSekiroModelParser::ParseFromFile(ModelJson, ModelData))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 模型JSON解析失败"));
		return false;
	}

	// 构建Union Skeleton
	TArray<FSekiroImportBone> SkeletonBones = AnimResult.Bones;
	FSekiroSkeletonBuilder::MergeModelWorldTransforms(SkeletonBones, ModelData.Bones);
	{
		TSet<FName> MeshBoneNames;
		for (const FSekiroImportMeshSection& Sec : ModelData.Meshes)
			for (const auto& Pair : Sec.BoneIdxToName)
				MeshBoneNames.Add(Pair.Value);
		FSekiroSkeletonBuilder::AppendModelOnlyBones(SkeletonBones, ModelData.Bones, MeshBoneNames);
	}

	// 构建USkeleton
	FString PkgPath = TEXT("/Game/SekiroTest/TestSkeleton");
	USkeleton* Skeleton = FSekiroSkeletonBuilder::Build(SkeletonBones, TEXT("TestSkeleton"), PkgPath);

	if (!Skeleton)
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 骨架构建失败"));
		return false;
	}

	// 输出诊断文件
	DumpBonesFK(SkeletonBones, OutputDir);
	DumpRefSkeleton(Skeleton->GetReferenceSkeleton(), OutputDir);

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] BuildSkeletonAndDump: %d骨骼 -> %s"), SkeletonBones.Num(), *OutputDir);
	return true;
}

bool USekiroImportTest::BuildMeshAndDump(const FString& ModelJson, const FString& SkeletonPackagePath, const FString& OutputDir)
{
	// 加载已存骨架
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPackagePath);
	if (!Skeleton)
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 无法加载骨架: %s"), *SkeletonPackagePath);
		return false;
	}

	FSekiroModelData ModelData;
	if (!FSekiroModelParser::ParseFromFile(ModelJson, ModelData))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 模型JSON解析失败"));
		return false;
	}

	IFileManager::Get().MakeDirectory(*OutputDir, true);

	// 输出原始顶点/蒙皮数据（构建前）
	DumpVertSkinning(ModelData, Skeleton->GetReferenceSkeleton(), OutputDir);

	// 执行网格构建（但不关心结果，仅要副作用验证）
	FString MeshPath = TEXT("/Game/SekiroTest/TestMesh");
	USkeletalMesh* Mesh = FSekiroSkeletalMeshBuilder::Build(ModelData, Skeleton, MeshPath);

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] BuildMeshAndDump: %d Sections, Mesh=%s -> %s"),
		ModelData.Meshes.Num(), Mesh ? TEXT("OK") : TEXT("FAIL"), *OutputDir);
	return Mesh != nullptr;
}

bool USekiroImportTest::BuildAnimationAndDump(const FString& AnimJson, const FString& SkeletonPackagePath, int32 ClipIndex, const FString& OutputDir)
{
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPackagePath);
	if (!Skeleton)
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 无法加载骨架: %s"), *SkeletonPackagePath);
		return false;
	}

	FSekiroAnimationParser::FParseResult AnimResult;
	if (!FSekiroAnimationParser::ParseFromFile(AnimJson, AnimResult, 0))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] 动画JSON解析失败"));
		return false;
	}

	if (ClipIndex < 0 || ClipIndex >= AnimResult.Clips.Num())
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] ClipIndex=%d 越界 (共%d个)"), ClipIndex, AnimResult.Clips.Num());
		return false;
	}

	IFileManager::Get().MakeDirectory(*OutputDir, true);

	const FSekiroAnimationClip& Clip = AnimResult.Clips[ClipIndex];
	DumpAnimTracks(Clip, Skeleton->GetReferenceSkeleton(), OutputDir);

	// 可选：实际构建动画Sequence验证
	FString AnimPath = FString::Printf(TEXT("/Game/SekiroTest/TestAnim_%s"), *Clip.Name);
	UAnimSequence* Seq = FSekiroAnimationBuilder::Build(Clip, Skeleton, nullptr, AnimPath);

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] BuildAnimationAndDump: %s (%d帧) Seq=%s -> %s"),
		*Clip.Name, Clip.FrameCount, Seq ? TEXT("OK") : TEXT("FAIL"), *OutputDir);
	return Seq != nullptr;
}

bool USekiroImportTest::RunFullPipelineAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir)
{
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] === 全流程诊断开始 ==="));
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Model=%s"), *ModelJson);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Anim=%s"), *AnimJson);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Out=%s"), *OutputDir);

	// 解析（取前3个动画用于验证Phase 3）
	FSekiroAnimationParser::FParseResult AnimResult;
	if (!FSekiroAnimationParser::ParseFromFile(AnimJson, AnimResult, 3))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] FAIL: 动画JSON解析"));
		return false;
	}

	FSekiroModelData ModelData;
	if (!FSekiroModelParser::ParseFromFile(ModelJson, ModelData))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] FAIL: 模型JSON解析"));
		return false;
	}

	// 骨架
	TArray<FSekiroImportBone> SkeletonBones = AnimResult.Bones;
	FSekiroSkeletonBuilder::MergeModelWorldTransforms(SkeletonBones, ModelData.Bones);
	// 诊断：Merge后World/Local中间状态
	DumpIntermediateState(SkeletonBones, OutputDir, TEXT("State_AfterMerge.txt"));
	{
		TSet<FName> MeshBoneNames;
		for (const FSekiroImportMeshSection& Sec : ModelData.Meshes)
			for (const auto& Pair : Sec.BoneIdxToName)
				MeshBoneNames.Add(Pair.Value);
		FSekiroSkeletonBuilder::AppendModelOnlyBones(SkeletonBones, ModelData.Bones, MeshBoneNames);
	}

	FString SkelPath = TEXT("/Game/SekiroTest/TestSkeleton");
	USkeleton* Skeleton = FSekiroSkeletonBuilder::Build(SkeletonBones, TEXT("TestSkeleton"), SkelPath);
	if (!Skeleton) { UE_LOG(LogSekiroImport, Error, TEXT("[TestDump] FAIL: 骨架构建")); return false; }

	DumpBonesFK(SkeletonBones, OutputDir);
	DumpRefSkeleton(Skeleton->GetReferenceSkeleton(), OutputDir);

	// 网格
	DumpVertSkinning(ModelData, Skeleton->GetReferenceSkeleton(), OutputDir);

	FString MeshPath = TEXT("/Game/SekiroTest/TestMesh");
	USkeletalMesh* Mesh = FSekiroSkeletalMeshBuilder::Build(ModelData, Skeleton, MeshPath);
	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] Mesh=%s"), Mesh ? TEXT("OK") : TEXT("FAIL"));

	// 动画（取前3个各输出）
	for (int32 i = 0; i < FMath::Min(3, AnimResult.Clips.Num()); ++i)
	{
		FString AnimFile = FString::Printf(TEXT("Anim_%d_Cpp.txt"), i);
		// 临时修改输出目录
		DumpAnimTracks(AnimResult.Clips[i], Skeleton->GetReferenceSkeleton(), OutputDir);
	}

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] === 全流程诊断完成 ==="));
	return Mesh != nullptr;
}

bool USekiroImportTest::BuildMaterialsAndDump(const FString& ModelJson, const FString& OutputDir)
{
	FSekiroModelData Data;
	if (!FSekiroModelParser::ParseFromFile(ModelJson, Data))
		return false;

	IFileManager::Get().MakeDirectory(*OutputDir, true);

	// 确定材质路径
	FString MaterialsPath = TEXT("/Game/Characters/Sekiro/Materials");

	// 构建材质（不绑定SkeletalMesh，仅验证创建流程）
	TArray<UMaterial*> Results = FSekiroMaterialBuilder::BuildAll(
		Data, nullptr, MaterialsPath);

	// 输出材质构建摘要
	FString Summary;
	Summary += FString::Printf(TEXT("Total: %d materials, %d created\n\n"),
		Data.Materials.Num(), Results.Num());
	Summary += TEXT("Idx|Name|BlendMode|TwoSided|MTDPath\n");
	Summary += TEXT("---|---|---|---|---\n");

	for (int32 i = 0; i < Data.Materials.Num(); ++i)
	{
		const FSekiroImportMaterial& M = Data.Materials[i];
		FString BlendInfo = M.BlendMode.IsEmpty() ? TEXT("(empty)") : M.BlendMode;

		// 从MIC读取最终属性
		FString FinalBlend = BlendInfo;
		bool bTwoSided = false;
		if (i < Results.Num() && Results[i])
		{
			UMaterial* Mat = Results[i];
			EBlendMode BM = Mat->BlendMode;
			switch (BM)
			{
			case BLEND_Opaque:      FinalBlend = TEXT("Opaque"); break;
			case BLEND_Masked:      FinalBlend = TEXT("Masked"); break;
			case BLEND_Translucent: FinalBlend = TEXT("Translucent"); break;
			case BLEND_Additive:    FinalBlend = TEXT("Additive"); break;
			case BLEND_Modulate:    FinalBlend = TEXT("Modulate"); break;
			default:                FinalBlend = FString::Printf(TEXT("Unknown(%d)"), (int32)BM); break;
			}
			bTwoSided = Mat->TwoSided;
		}

		Summary += FString::Printf(TEXT("%d|%s|%s|%s|%s\n"),
			i, *M.Name, *FinalBlend, bTwoSided ? TEXT("Y") : TEXT("N"), *M.MTDPath);
	}

	FFileHelper::SaveStringToFile(Summary, *(OutputDir / TEXT("MatBuild_Cpp.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogSekiroImport, Warning, TEXT("[TestDump] BuildMaterialsAndDump: %d/%d MICs -> %s"),
		Results.Num(), Data.Materials.Num(), *OutputDir);
	return Results.Num() > 0;
}

bool USekiroImportTest::BuildFullModel(const FString& ModelJson, const FString& AnimJson)
{
	UE_LOG(LogSekiroImport, Warning, TEXT("[FullImport] === 完整导入开始 ==="));

	// 1. 解析
	FSekiroAnimationParser::FParseResult AnimResult;
	if (!FSekiroAnimationParser::ParseFromFile(AnimJson, AnimResult, -1))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[FullImport] FAIL: 动画JSON解析"));
		return false;
	}

	FSekiroModelData ModelData;
	if (!FSekiroModelParser::ParseFromFile(ModelJson, ModelData))
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[FullImport] FAIL: 模型JSON解析"));
		return false;
	}

	// 2. 构建骨架
	TArray<FSekiroImportBone> SkeletonBones = AnimResult.Bones;
	FSekiroSkeletonBuilder::MergeModelWorldTransforms(SkeletonBones, ModelData.Bones);
	{
		TSet<FName> MeshBoneNames;
		for (const FSekiroImportMeshSection& Sec : ModelData.Meshes)
			for (const auto& Pair : Sec.BoneIdxToName)
				MeshBoneNames.Add(Pair.Value);
		FSekiroSkeletonBuilder::AppendModelOnlyBones(SkeletonBones, ModelData.Bones, MeshBoneNames);
	}

	FString SkelPath = TEXT("/Game/SekiroTest/TestSkeleton");
	USkeleton* Skeleton = FSekiroSkeletonBuilder::Build(SkeletonBones, TEXT("TestSkeleton"), SkelPath);
	if (!Skeleton) { UE_LOG(LogSekiroImport, Error, TEXT("[FullImport] FAIL: 骨架构建")); return false; }
	UE_LOG(LogSekiroImport, Warning, TEXT("[FullImport] 骨架: %d骨骼 -> %s"), SkeletonBones.Num(), *SkelPath);

	// 3. 构建网格
	FString MeshPath = TEXT("/Game/SekiroTest/TestMesh");
	USkeletalMesh* Mesh = FSekiroSkeletalMeshBuilder::Build(ModelData, Skeleton, MeshPath);
	if (!Mesh) { UE_LOG(LogSekiroImport, Error, TEXT("[FullImport] FAIL: 网格构建")); return false; }
	UE_LOG(LogSekiroImport, Warning, TEXT("[FullImport] 网格: %d Sections -> %s"), ModelData.Meshes.Num(), *MeshPath);

	// 4. 构建材质并分配到网格
	FString MaterialsPath = TEXT("/Game/Characters/Sekiro/Materials");
	TArray<UMaterial*> Materials = FSekiroMaterialBuilder::BuildAll(
		ModelData, Mesh, MaterialsPath);
	UE_LOG(LogSekiroImport, Warning, TEXT("[FullImport] 材质: %d/%d MICs -> %s"),
		Materials.Num(), ModelData.Materials.Num(), *MaterialsPath);

	// 5. 保存网格（材质引用已写入）
	Mesh->MarkPackageDirty();
	Mesh->PostEditChange();
	FString MeshFile = FPackageName::LongPackageNameToFilename(
		MeshPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Mesh->GetOutermost(), Mesh, *MeshFile, SaveArgs);

	UE_LOG(LogSekiroImport, Warning, TEXT("[FullImport] === 完整导入完成，请在Content Browser打开 %s 查看 ==="), *MeshPath);
	return true;
}

bool USekiroImportTest::RunImportPipeline(const FString& ModelJson, const FString& AnimJson,
	const FString& OutputBasePath, bool bImportAnimations, const FString& AnimationPrefixFilter)
{
	UE_LOG(LogSekiroImport, Warning, TEXT("[Pipeline] === 管线导入开始 ==="));
	UE_LOG(LogSekiroImport, Warning, TEXT("[Pipeline] 模型=%s"), *ModelJson);
	UE_LOG(LogSekiroImport, Warning, TEXT("[Pipeline] 动画=%s"), *AnimJson);
	UE_LOG(LogSekiroImport, Warning, TEXT("[Pipeline] 输出=%s 动画=%d 过滤=%s"), *OutputBasePath, bImportAnimations, *AnimationPrefixFilter);

	// 构造Settings对象
	USekiroImportSettings* Settings = NewObject<USekiroImportSettings>();
	Settings->ModelJsonPath = ModelJson;
	Settings->AnimationJsonPath = AnimJson;
	Settings->OutputBasePath = OutputBasePath;
	Settings->bImportSkeleton = true;
	Settings->bImportSkeletalMesh = true;
	Settings->bImportMaterials = true;
	Settings->bImportAnimations = bImportAnimations;
	Settings->MaxAnimations = 0; // 全部（受AnimationPrefixFilter限制）
	Settings->AnimationPrefixFilter = AnimationPrefixFilter;
	Settings->OverwriteMode = ESekiroOverwriteMode::Overwrite;

	// 执行管线
	FSekiroImportPipeline::FImportResult Result = FSekiroImportPipeline::Run(*Settings);

	UE_LOG(LogSekiroImport, Warning, TEXT("[Pipeline] === 管线导入完成 === 成功=%d 错误=%d"),
		Result.bSuccess, Result.Errors.Num());

	for (const FString& Err : Result.Errors)
	{
		UE_LOG(LogSekiroImport, Error, TEXT("[Pipeline] 错误: %s"), *Err);
	}

	return Result.bSuccess;
}
