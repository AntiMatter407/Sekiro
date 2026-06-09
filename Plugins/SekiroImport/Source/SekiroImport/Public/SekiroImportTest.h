#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroImportTest.generated.h"

/// Python可调用的诊断/验证API，每个函数解析→构建→输出对比文件
UCLASS()
class SEKIROIMPORT_API USekiroImportTest : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/// 解析模型JSON并输出诊断文件（Bones/RefSkel/Verts）
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool ParseModelAndDump(const FString& JsonPath, const FString& OutputDir);

	/// 解析动画JSON并输出诊断文件（Bones/Clips摘要）
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool ParseAnimationAndDump(const FString& JsonPath, const FString& OutputDir);

	/// 从两个JSON构建骨架并输出Bones_Cpp.txt + RefSkel_Cpp.txt
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool BuildSkeletonAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir);

	/// 从模型JSON + 已存骨架构建网格并输出Verts_Cpp.txt
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool BuildMeshAndDump(const FString& ModelJson, const FString& SkeletonPackagePath, const FString& OutputDir);

	/// 构建单个动画片段并输出Anim_Cpp.txt
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool BuildAnimationAndDump(const FString& AnimJson, const FString& SkeletonPackagePath, int32 ClipIndex, const FString& OutputDir);

	/// 全流程：解析→构建→输出所有诊断文件（不做资产保存）
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool RunFullPipelineAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir);

	/// Phase 4: 构建材质并输出诊断（验证 BlendMode + TwoSided 推导 + 材质实例创建）
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool BuildMaterialsAndDump(const FString& ModelJson, const FString& OutputDir);

	/// 完整导入：骨架+网格+材质，保存到Content Browser供肉眼验证
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Test")
	static bool BuildFullModel(const FString& ModelJson, const FString& AnimJson);

	/// Phase 5: 管线编排 — 导入骨架+网格+纹理+材质+动画（全流程）
	/// @param AnimationPrefixFilter 动画名称前缀过滤（如 "Sekiro_a000"），空=全部
	UFUNCTION(BlueprintCallable, Category="SekiroImport|Pipeline")
	static bool RunImportPipeline(const FString& ModelJson, const FString& AnimJson,
		const FString& OutputBasePath = TEXT("/Game/Characters/Sekiro"),
		bool bImportAnimations = false,
		const FString& AnimationPrefixFilter = TEXT(""));
};
