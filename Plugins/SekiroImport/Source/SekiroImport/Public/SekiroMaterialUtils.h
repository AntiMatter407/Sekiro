#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroMaterialUtils.generated.h"

UCLASS()
class SEKIROIMPORT_API USekiroMaterialUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Wire Alpha→Opacity on a translucent material. */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static bool ConnectAlphaToOpacity(UMaterial* Material);

    /** Set blend mode: 0=Opaque, 1=Masked, 2=Translucent, 3=Additive, 4=Modulate */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static void SetBlendMode(UMaterial* Material, int32 BlendMode);

    /** Set material two-sided flag */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static void SetTwoSided(UMaterial* Material, bool bTwoSided);

    /** Set shading model: 0=DefaultLit, 1=Subsurface, 2=TranslucentVolume, etc. */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static void SetShadingModel(UMaterial* Material, int32 ShadingModel);

    /** Fix sampler types on all TextureSample nodes in a material.
     *  Reads texture name suffix to decide: _n→Normal, _m/_r/_ao→Grayscale,
     *  _d/_em→LinearColor, others→Color. */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static void FixTextureSamplersInMaterial(UMaterial* Material);

    /** Remove all TextureSample nodes that have no texture assigned.
     *  FBX importer sometimes creates empty nodes that cause SM6 compile errors. */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Material")
    static int32 RemoveEmptyTextureSamples(UMaterial* Material);
};
