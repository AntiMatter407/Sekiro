#include "SekiroTextureImporter.h"
#include "SekiroImportLog.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AutomatedAssetImportData.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"

/// 根据资产名后缀修正贴图压缩/色彩空间（对齐Blender Non-Color逻辑）
/// @return 是否有修改
static bool FixTextureCompression(UTexture2D* Tex, const FString& AssetName)
{
	FString Stem = AssetName.ToLower();
	bool bChanged = false;

	if (Stem.EndsWith(TEXT("_n")))
	{
		if (Tex->SRGB) { Tex->SRGB = false; bChanged = true; }
		if (Tex->CompressionSettings != TC_Normalmap) { Tex->CompressionSettings = TC_Normalmap; bChanged = true; }
	}
	else if (Stem.EndsWith(TEXT("_m")) || Stem.EndsWith(TEXT("_r")))
	{
		if (Tex->SRGB) { Tex->SRGB = false; bChanged = true; }
		if (Tex->CompressionSettings != TC_Grayscale) { Tex->CompressionSettings = TC_Grayscale; bChanged = true; }
	}

	if (bChanged)
	{
		Tex->PostEditChange();
		Tex->MarkPackageDirty();
	}
	return bChanged;
}

void FSekiroTextureImporter::Import(const FString& SourceDir, const FString& DestPath,
	int32& OutImported, int32& OutSkipped, int32& OutFixed)
{
	OutImported = 0;
	OutSkipped = 0;
	OutFixed = 0;

	if (!IFileManager::Get().DirectoryExists(*SourceDir))
	{
		UE_LOG(LogSekiroImport, Warning, TEXT("贴图源目录不存在: %s"), *SourceDir);
		return;
	}

	TArray<FString> PngFiles;
	IFileManager::Get().FindFiles(PngFiles, *(SourceDir / TEXT("*.png")), true, false);
	PngFiles.Sort();

	if (PngFiles.Num() == 0)
	{
		UE_LOG(LogSekiroImport, Warning, TEXT("未找到PNG文件: %s"), *SourceDir);
		return;
	}

	UE_LOG(LogSekiroImport, Log, TEXT("S4.5: 开始导入 %d 张贴图: %s -> %s"), PngFiles.Num(), *SourceDir, *DestPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	const int32 Total = PngFiles.Num();

	for (int32 i = 0; i < Total; ++i)
	{
		const FString& FileName = PngFiles[i];
		FString AssetName = FPaths::GetBaseFilename(FileName);

		// 跳过已存在
		FString AssetObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestPath, *AssetName, *AssetName);
		if (LoadObject<UTexture2D>(nullptr, *AssetObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn))
		{
			++OutSkipped;
			UE_LOG(LogSekiroImport, Verbose, TEXT("  跳过已存在: %s"), *AssetName);
			continue;
		}

		// UTextureFactory 强制走传统同步 FactoryCreateBinary，绕过 Interchange 异步导入死锁
		UTextureFactory* TexFactory = NewObject<UTextureFactory>(GetTransientPackage());
		TexFactory->bDeferCompression = true;

		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames.Add(SourceDir / FileName);
		ImportData->DestinationPath = DestPath;
		ImportData->bReplaceExisting = false;
		ImportData->bSkipReadOnly = true;
		ImportData->Factory = TexFactory;

		TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);
		if (Imported.Num() > 0)
		{
			++OutImported;
			if (UTexture2D* Tex = Cast<UTexture2D>(Imported[0]))
			{
				if (FixTextureCompression(Tex, AssetName)) ++OutFixed;
			}
		}
		else
		{
			UE_LOG(LogSekiroImport, Warning, TEXT("  导入失败: %s"), *FileName);
		}

		if ((i + 1) % 10 == 0 || (i + 1) == Total)
		{
			UE_LOG(LogSekiroImport, Log, TEXT("  贴图进度: %d/%d (导入%d, 跳过%d)"),
				i + 1, Total, OutImported, OutSkipped);
		}
	}

	// 修正所有贴图（含上次导入已存在的）的压缩设置
	{
		TArray<UPackage*> FixedPkgs;
		for (const FString& FileName : PngFiles)
		{
			FString AssetName = FPaths::GetBaseFilename(FileName);
			FString AssetObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestPath, *AssetName, *AssetName);

			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *AssetObjectPath);
			if (!Tex)
			{
				FString PackageName = FString::Printf(TEXT("%s/%s"), *DestPath, *AssetName);
				if (UPackage* Pkg = LoadPackage(nullptr, *PackageName, LOAD_NoWarn | LOAD_Quiet))
					Tex = FindObject<UTexture2D>(Pkg, *AssetName);
			}
			if (!Tex) continue;

			if (FixTextureCompression(Tex, AssetName))
			{
				FixedPkgs.AddUnique(Tex->GetOutermost());
				++OutFixed;
			}
		}

		if (FixedPkgs.Num() > 0)
			UEditorLoadingAndSavingUtils::SavePackages(FixedPkgs, false);
	}

	UE_LOG(LogSekiroImport, Log, TEXT("S4.5: 贴图导入完成: %d 新建, %d 跳过, %d 压缩修正, 共 %d"),
		OutImported, OutSkipped, OutFixed, Total);
}
