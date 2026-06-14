#include "SekiroImportPipeline.h"
#include "SekiroImportSettings.h"
#include "SekiroImportLog.h"
#include "SekiroImportVerifier.h"
#include "SekiroTextureImporter.h"
#include "SekiroModelParser.h"
#include "SekiroAnimationParser.h"
#include "SekiroSkeletonBuilder.h"
#include "SekiroSkeletalMeshBuilder.h"
#include "SekiroAnimationBuilder.h"
#include "SekiroMaterialBuilder.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"

FSekiroImportPipeline::FOnProgress FSekiroImportPipeline::OnProgress;

namespace
{
	void ReportProgress(const TCHAR* Fmt, ...)
	{
		TCHAR Buf[1024];
		va_list Args;
		va_start(Args, Fmt);
		FCString::GetVarArgs(Buf, UE_ARRAY_COUNT(Buf), Fmt, Args);
		va_end(Args);
		UE_LOG(LogSekiroImport, Log, TEXT("%s"), Buf);
		if (FSekiroImportPipeline::OnProgress.IsBound())
			FSekiroImportPipeline::OnProgress.Execute(FString(Buf));
	}

	/// 过滤物理布料section（fray/frary: Havok运行时模拟，静态模型不需要）
	/// 对齐 Blender common_blender.py:1036-1042
	void RemoveClothSections(FSekiroModelData& ModelData)
	{
		TArray<int32> IndicesToRemove;
		for (int32 i = 0; i < ModelData.Meshes.Num(); ++i)
		{
			int32 MatIdx = ModelData.Meshes[i].MaterialIndex;
			if (MatIdx >= 0 && MatIdx < ModelData.Materials.Num())
			{
				const FString MatLower = ModelData.Materials[MatIdx].Name.ToLower();
				if (MatLower.Contains(TEXT("fray")) || MatLower.Contains(TEXT("frary")))
					IndicesToRemove.Add(i);
			}
		}
		for (int32 j = IndicesToRemove.Num() - 1; j >= 0; --j)
		{
			int32 Idx = IndicesToRemove[j];
			UE_LOG(LogSekiroImport, Log, TEXT("[过滤] 跳过物理布料section: %s (Mat=%s)"),
				*ModelData.Meshes[Idx].PartName, *ModelData.Materials[ModelData.Meshes[Idx].MaterialIndex].Name);
			ModelData.Meshes.RemoveAt(Idx);
		}
		if (IndicesToRemove.Num() > 0)
		{
			UE_LOG(LogSekiroImport, Log, TEXT("[过滤] 共移除 %d 个物理布料section, 剩余 %d"),
				IndicesToRemove.Num(), ModelData.Meshes.Num());
		}
	}
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

	if ((Settings.bImportSkeleton || Settings.bImportAnimations) && !Settings.AnimationJsonPath.IsEmpty())
	{
		ReportProgress(TEXT("S1: 解析动画JSON: %s"), *Settings.AnimationJsonPath);

		if (!FSekiroAnimationParser::ParseFromFile(Settings.AnimationJsonPath, AnimParseResult,
			Settings.bImportAnimations ? Settings.MaxAnimations : -1, Settings.AnimationPrefixFilter))
		{
			Result.Errors.Add(TEXT("动画JSON解析失败"));
		}
		else
		{
			FSekiroImportVerifier::Bones(AnimParseResult.Bones, TEXT("动画JSON"));
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

			if (!Settings.ModelJsonPath.IsEmpty())
			{
				FSekiroModelData ModelForBones;
				if (FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelForBones) && ModelForBones.Bones.Num() > 0)
				{
					FSekiroSkeletonBuilder::MergeModelWorldTransforms(SkeletonBones, ModelForBones.Bones);

					TSet<FName> MeshBoneNames;
					for (const FSekiroImportMeshSection& Sec : ModelForBones.Meshes)
						for (const auto& Pair : Sec.BoneIdxToName)
							MeshBoneNames.Add(Pair.Value);
					FSekiroSkeletonBuilder::AppendModelOnlyBones(SkeletonBones, ModelForBones.Bones, MeshBoneNames);

					ReportProgress(TEXT("S2: 动画%d骨骼 + 模型WorldPos合并 + %dModelOnly追加"),
						AnimParseResult.Bones.Num(), SkeletonBones.Num() - AnimParseResult.Bones.Num());
				}
				else
				{
					ReportProgress(TEXT("S2: 使用动画JSON构建骨架: %d 根骨骼（含IK）"), SkeletonBones.Num());
				}
			}
			else
			{
				ReportProgress(TEXT("S2: 使用动画JSON构建骨架: %d 根骨骼（含IK）"), SkeletonBones.Num());
			}
		}
		else if (!Settings.ModelJsonPath.IsEmpty())
		{
			FSekiroModelData ModelData;
			if (FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelData) && ModelData.Bones.Num() > 0)
			{
				FSekiroSkeletonBuilder::DeriveLocalFromWorld(ModelData.Bones);
				SkeletonBones = ModelData.Bones;
				ReportProgress(TEXT("S2: 使用模型JSON构建骨架: %d 根骨骼"), SkeletonBones.Num());
			}
		}

		if (SkeletonBones.Num() > 0)
		{
			FString SkeletonPath = FString::Printf(TEXT("%s/%s"), *OutputBase, *SkeletonName);
			Skeleton = FSekiroSkeletonBuilder::Build(SkeletonBones, SkeletonName, SkeletonPath);
			if (Skeleton)
			{
				Result.Skeleton = Skeleton;
				ReportProgress(TEXT("S2: 骨架构建完成: %s"), *Skeleton->GetName());
				FSekiroImportVerifier::Skeleton(Skeleton, SkeletonBones.Num());
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

	// 仅导入动画时：加载已存在的骨架
	if (!Skeleton && Settings.bImportAnimations && !Settings.bImportSkeleton)
	{
		FString SkeletonPath = FString::Printf(TEXT("%s/%s"), *OutputBase, *SkeletonName);

		// Try direct load first
		Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);

		// Fallback: search by class in output directory via AssetRegistry
		if (!Skeleton)
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AssetDataList;
			AssetRegistryModule.Get().GetAssetsByPath(FName(*OutputBase), AssetDataList, true);
			for (const FAssetData& Data : AssetDataList)
			{
				if (Data.AssetClassPath == USkeleton::StaticClass()->GetClassPathName())
				{
					Skeleton = Cast<USkeleton>(Data.GetAsset());
					if (Skeleton)
					{
						UE_LOG(LogSekiroImport, Log, TEXT("S2: 通过AssetRegistry找到骨架: %s"), *Data.GetObjectPathString());
						break;
					}
				}
			}
		}

		if (Skeleton)
		{
			ReportProgress(TEXT("S2: 加载已有骨架: %s"), *Skeleton->GetPathName());
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("骨架不存在: %s，请先导入骨架"), *SkeletonPath));
		}
	}

	// ============================================================
	// 步骤3: 解析模型JSON → 构建骨骼网格体
	// ============================================================
	FSekiroModelData ModelData;
	USkeletalMesh* SkeletalMesh = nullptr;

	if (Settings.bImportSkeletalMesh && !Settings.ModelJsonPath.IsEmpty())
	{
		ReportProgress(TEXT("S3: 解析模型JSON: %s"), *Settings.ModelJsonPath);

		if (!FSekiroModelParser::ParseFromFile(Settings.ModelJsonPath, ModelData))
		{
			Result.Errors.Add(TEXT("模型JSON解析失败"));
		}
		else
		{
			FSekiroImportVerifier::ModelData(ModelData);
			RemoveClothSections(ModelData);
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
			ReportProgress(TEXT("S4: 构建骨骼网格体 → %s"), *MeshPath);
			SkeletalMesh = FSekiroSkeletalMeshBuilder::Build(ModelData, Skeleton, MeshPath);

			if (SkeletalMesh)
			{
				Result.SkeletalMesh = SkeletalMesh;
				ReportProgress(TEXT("S4: 骨骼网格体构建完成: %s (%d Section)"),
					*SkeletalMesh->GetName(), ModelData.Meshes.Num());
				if (Skeleton) Skeleton->SetPreviewMesh(SkeletalMesh);
				FSekiroImportVerifier::SkeletalMesh(SkeletalMesh, ModelData, Skeleton);
			}
			else
			{
				Result.Errors.Add(TEXT("骨骼网格体构建失败"));
			}
		}
	}

	// ============================================================
	// 步骤4.5: 导入贴图（跳过已存在，分批避免Stall）
	// ============================================================
	{
		const FString TextureSourceDir = FPaths::ProjectDir() / TEXT("Extracted/Textures");
		const FString TextureDestPath = FString::Printf(TEXT("%s/Textures"), *OutputBase);
		int32 Imported = 0, Skipped = 0, Fixed = 0;
		FSekiroTextureImporter::Import(TextureSourceDir, TextureDestPath, Imported, Skipped, Fixed);
		ReportProgress(TEXT("S4.5: 贴图导入: %d 新建, %d 跳过, %d 压缩修正"), Imported, Skipped, Fixed);
	}

	// ============================================================
	// 步骤5: 构建材质
	// ============================================================
	if (Settings.bImportMaterials && ModelData.Materials.Num() > 0)
	{
		FString MaterialsPath = FString::Printf(TEXT("%s/Materials"), *OutputBase);
		ReportProgress(TEXT("S5: 构建材质 → %s/"), *MaterialsPath);
		Result.Materials = FSekiroMaterialBuilder::BuildAll(ModelData, SkeletalMesh, MaterialsPath);
		ReportProgress(TEXT("S5: 材质构建完成: %d 个"), Result.Materials.Num());
		FSekiroImportVerifier::Materials(Result.Materials, SkeletalMesh);
	}

	// 仅导入动画时：加载已存在的骨骼网格体（用作动画预览Mesh）
	if (!SkeletalMesh && Settings.bImportAnimations && !Settings.bImportSkeletalMesh && Skeleton)
	{
		FString MeshPath = FString::Printf(TEXT("%s/%s_SkeletalMesh"), *OutputBase, *SkeletonName);
		SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
		if (SkeletalMesh)
		{
			ReportProgress(TEXT("S5.5: 加载已有网格体: %s"), *SkeletalMesh->GetPathName());
				Skeleton->SetPreviewMesh(SkeletalMesh);
		}
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
			ReportProgress(TEXT("S6: 开始构建 %d 个动画 → %s/"), AnimParseResult.Clips.Num(), *AnimsPath);

			Result.Animations = FSekiroAnimationBuilder::BuildBatch(AnimParseResult.Clips, Skeleton, SkeletalMesh, AnimsPath);

			ReportProgress(TEXT("S6: 动画构建完成: %d/%d 成功"),
				Result.Animations.Num(), AnimParseResult.Clips.Num());
			FSekiroImportVerifier::Animations(Result.Animations, Skeleton);

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

	ReportProgress(TEXT("=== 导入完成 === 骨架=%s 网格体=%s 材质=%d 动画=%d 错误=%d"),
		Result.Skeleton ? TEXT("✓") : TEXT("✗"),
		Result.SkeletalMesh ? TEXT("✓") : TEXT("✗"),
		Result.Materials.Num(),
		Result.Animations.Num(),
		Result.Errors.Num());

	for (const FString& Err : Result.Errors)
	{
		UE_LOG(LogSekiroImport, Warning, TEXT("  导入错误: %s"), *Err);
	}

	if (GEditor)
	{
		GEditor->ResetTransaction(NSLOCTEXT("SekiroImport", "ImportComplete", "Sekiro导入完成"));
	}

	return Result;
}
