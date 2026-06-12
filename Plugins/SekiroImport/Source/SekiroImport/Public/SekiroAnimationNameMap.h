#pragma once

#include "CoreMinimal.h"

/// 将 Sekiro HKX 动画 ID 翻译为人类可读名称。
/// 基于 FromSoftware 社区 ANIMATION_MAP + ID 段位推断。
struct SEKIROIMPORT_API FSekiroAnimationNameMap
{
	/// 查表：返回 6 位 ID 对应的可读名称，未命中返回空
	static FString Lookup(const FString& AnimId);

	/// ID 段位 → 类别前缀（用于未命中映射的 ID）
	static FString GetCategoryPrefix(const FString& AnimId);

	/// 完整翻译： "Sekiro_a000_201030" → "Sekiro_Parry_Deflect_Success"
	/// 未命中时回退到段位前缀 → "Sekiro_Attack_a010_100500"
	static FString Translate(const FString& RawName);
};
