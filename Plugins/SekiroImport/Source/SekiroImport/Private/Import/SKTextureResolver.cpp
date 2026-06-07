#include "Import/SKTextureResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"

UTexture2D* FSKTextureResolver::FindTexture(const FString& ParamName, const TArray<FString>& SearchPaths)
{
	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Extract a search-friendly name from the param name
	// e.g. "Character_AMSN__snp_Texture2D_7_AlbedoMap" -> search for "*AlbedoMap*" or "*_d*"
	FString SearchTerm;

	// Known suffix patterns
	static const TArray<FString> KnownSuffixes = {
		TEXT("AlbedoMap"), TEXT("_a"), TEXT("_d"),     // Diffuse/Albedo
		TEXT("NormalMap"), TEXT("_n"), TEXT("_ncl1"), // Normal
		TEXT("MetallicMap"), TEXT("_m"),               // Metallic
		TEXT("SpecularMap"), TEXT("_s"),               // Specular
		TEXT("RoughnessMap"), TEXT("_r"),              // Roughness
		TEXT("AOMap"), TEXT("_ao"),                     // AO
		TEXT("EmissiveMap"), TEXT("_em"),               // Emissive
		TEXT("Mask1Map"),                                // Mask
	};

	for (const FString& Suffix : KnownSuffixes)
	{
		if (ParamName.EndsWith(Suffix))
		{
			SearchTerm = Suffix;
			break;
		}
	}

	if (SearchTerm.IsEmpty())
	{
		// Use the last underscore-separated segment
		int32 LastUnderscore;
		if (ParamName.FindLastChar('_', LastUnderscore))
			SearchTerm = ParamName.RightChop(LastUnderscore + 1);
		else
			SearchTerm = ParamName;
	}

	// Build FARFilter
	FARFilter Filter;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	if (SearchPaths.Num() > 0)
	{
		for (const FString& Path : SearchPaths)
			Filter.PackagePaths.Add(FName(*Path));
	}
	else
	{
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.Get().GetAssets(Filter, AssetList);

	// Match by suffix
	for (const FAssetData& Asset : AssetList)
	{
		FString AssetName = Asset.AssetName.ToString();
		if (AssetName.EndsWith(SearchTerm))
			return Cast<UTexture2D>(Asset.GetAsset());
	}

	// Fallback: try matching by containing the search term
	for (const FAssetData& Asset : AssetList)
	{
		FString AssetName = Asset.AssetName.ToString();
		if (AssetName.Contains(SearchTerm))
			return Cast<UTexture2D>(Asset.GetAsset());
	}

	return nullptr;
}

EMaterialProperty FSKTextureResolver::PropertyForParam(const FString& ParamName)
{
	if (ParamName.Contains(TEXT("AlbedoMap")) || ParamName.Contains(TEXT("g_Diffuse")) || ParamName.EndsWith(TEXT("_d")) || ParamName.EndsWith(TEXT("_a")))
		return MP_BaseColor;
	if (ParamName.Contains(TEXT("NormalMap")) || ParamName.Contains(TEXT("g_Bumpmap")) || ParamName.EndsWith(TEXT("_n")) || ParamName.EndsWith(TEXT("_ncl1")))
		return MP_Normal;
	if (ParamName.Contains(TEXT("MetallicMap")) || ParamName.EndsWith(TEXT("_m")))
		return MP_Metallic;
	if (ParamName.Contains(TEXT("SpecularMap")) || ParamName.Contains(TEXT("g_Specular")) || ParamName.EndsWith(TEXT("_s")))
		return MP_Specular;
	if (ParamName.Contains(TEXT("RoughnessMap")) || ParamName.EndsWith(TEXT("_r")))
		return MP_Roughness;
	if (ParamName.Contains(TEXT("AOMap")) || ParamName.EndsWith(TEXT("_ao")))
		return MP_AmbientOcclusion;
	if (ParamName.Contains(TEXT("EmissiveMap")) || ParamName.EndsWith(TEXT("_em")))
		return MP_EmissiveColor;
	if (ParamName.Contains(TEXT("Mask1Map")) || ParamName.Contains(TEXT("g_Mask1")))
		return MP_OpacityMask;

	return MP_MAX; // Unknown
}

EMaterialSamplerType FSKTextureResolver::SamplerForProperty(EMaterialProperty Prop)
{
	switch (Prop)
	{
	case MP_Normal:         return SAMPLERTYPE_Normal;
	case MP_BaseColor:
	case MP_EmissiveColor:
	case MP_Opacity:
	case MP_OpacityMask:    return SAMPLERTYPE_Color;
	default:                return SAMPLERTYPE_LinearGrayscale;
	}
}
