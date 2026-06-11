#pragma once

#include "CoreMinimal.h"

/// 从Extracted/Textures导入PNG贴图到Content，跳过已存在，自动修正压缩设置
struct SEKIROIMPORT_API FSekiroTextureImporter
{
	/// @param SourceDir     PNG源目录
	/// @param DestPath      UE内容路径，如 /Game/Characters/Sekiro/Textures
	/// @param OutImported   输出：新导入数
	/// @param OutSkipped    输出：跳过数（已存在）
	/// @param OutFixed      输出：压缩设置修正数
	static void Import(const FString& SourceDir, const FString& DestPath,
		int32& OutImported, int32& OutSkipped, int32& OutFixed);
};
