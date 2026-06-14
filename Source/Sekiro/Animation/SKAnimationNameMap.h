#pragma once

#include "CoreMinimal.h"

/// Fetches a human-readable animation name for a given Sekiro animation ID.
/// Based on the ANIMATION_MAP from Script/extract_all_common_anims.py.
struct SEKIRO_API FSKAnimationNameMap
{
	/// Returns the translated name for the given 6-digit animation ID.
	/// If the ID is not in the map, returns an empty string.
	static FString Lookup(const FString& AnimId);

	/// Returns a category prefix based on the ID segment, for unmapped IDs.
	/// e.g. "100123" → "Attack", "250010" → "Hit"
	static FString GetCategoryPrefix(const FString& AnimId);

	/// Full pipeline: extract ID from a raw name like "Sekiro_a000_201030",
	/// look up the map, fall back to category prefix if unmapped.
	/// Returns the final readable name like "Sekiro_Parry_Deflect_Success".
	static FString Translate(const FString& RawName);
};
