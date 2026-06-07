#pragma once

#include "CoreMinimal.h"

class UTexture2D;

struct FSKTextureResolver
{
	// Find an already-imported texture by guessing its UE5 asset path from the FLVER param name.
	// Looks in /Game/ textures recursively via AssetRegistry.
	static UTexture2D* FindTexture(const FString& ParamName, const TArray<FString>& SearchPaths);

	// Determine which UE5 material property this FLVER texture param should connect to.
	// Returns MP_MAX (=255) if the param is not a known texture input.
	static EMaterialProperty PropertyForParam(const FString& ParamName);

	// Determine correct sampler type for a given material property.
	static EMaterialSamplerType SamplerForProperty(EMaterialProperty Prop);
};
