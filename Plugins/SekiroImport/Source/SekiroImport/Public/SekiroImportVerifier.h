#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class USkeleton;
class USkeletalMesh;
class UMaterial;
class UAnimSequence;

/// 管线验证器：对每个导入步骤执行诊断检查，输出到Output Log
struct SEKIROIMPORT_API FSekiroImportVerifier
{
	static void Bones(const TArray<FSekiroImportBone>& Bones, const FString& Source);
	static void Skeleton(USkeleton* Skeleton, int32 ExpectedBoneCount);
	static void ModelData(const FSekiroModelData& ModelData);
	static void SkeletalMesh(USkeletalMesh* Mesh, const FSekiroModelData& ModelData, USkeleton* Skeleton);
	static void Materials(const TArray<UMaterial*>& Materials, USkeletalMesh* Mesh);
	static void Animations(const TArray<UAnimSequence*>& Animations, USkeleton* Skeleton);
};
