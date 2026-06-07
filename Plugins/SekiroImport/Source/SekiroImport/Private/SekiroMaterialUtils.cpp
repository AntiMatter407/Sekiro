#include "SekiroMaterialUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialEditingLibrary.h"
#include "Engine/Texture.h"

// Forward declaration — defined below
static UMaterialExpression* SafeGetMaterialPropertyInputNode(UMaterial* Material, EMaterialProperty Property);

bool USekiroMaterialUtils::ConnectAlphaToOpacity(UMaterial* Material)
{
    if (!Material) return false;

    UMaterialExpression* Expr = SafeGetMaterialPropertyInputNode(
        Material, EMaterialProperty::MP_BaseColor);
    UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr);
    if (!TexSample) return false;

    return UMaterialEditingLibrary::ConnectMaterialProperty(
        TexSample, TEXT("A"), EMaterialProperty::MP_Opacity);
}

void USekiroMaterialUtils::SetBlendMode(UMaterial* Material, int32 BlendMode)
{
    if (!Material) return;
    Material->BlendMode = static_cast<EBlendMode>(FMath::Clamp(BlendMode, 0, 4));
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
}

void USekiroMaterialUtils::SetTwoSided(UMaterial* Material, bool bTwoSided)
{
    if (!Material) return;
    Material->TwoSided = bTwoSided;
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
}

void USekiroMaterialUtils::SetShadingModel(UMaterial* Material, int32 ShadingModel)
{
    if (!Material) return;
    Material->SetShadingModel(static_cast<EMaterialShadingModel>(
        FMath::Clamp(ShadingModel, 0, static_cast<int32>(MSM_MAX) - 1)));
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
}

// Correct sampler type based on which material property the node is connected to.
// NOT based on texture filename suffix — that fails on _ncl materials where the importer
// wired everything to BaseColor.
static EMaterialSamplerType SamplerForProperty(EMaterialProperty Prop)
{
    switch (Prop)
    {
    case MP_Normal:         return SAMPLERTYPE_Normal;
    case MP_BaseColor:
    case MP_EmissiveColor:  return SAMPLERTYPE_Color;
    default:                return SAMPLERTYPE_LinearGrayscale;  // Metallic, Roughness, AO
    }
}

// Safe accessor: bypasses UE5.2 bug where GetMaterialPropertyInputNode
// dereferences nullptr for unrecognized EMaterialProperty values (e.g. MP_SpecularColor).
static UMaterialExpression* SafeGetMaterialPropertyInputNode(UMaterial* Material, EMaterialProperty Property)
{
    FExpressionInput* ExpressionInput = Material->GetExpressionInputForProperty(Property);
    if (!ExpressionInput)
        return nullptr;
    return ExpressionInput->Expression;
}

void USekiroMaterialUtils::FixTextureSamplersInMaterial(UMaterial* Material)
{
    if (!Material) return;

    struct FPropPair { EMaterialProperty Prop; EMaterialSamplerType Sampler; };
    static const FPropPair Props[] = {
        { MP_BaseColor,         SAMPLERTYPE_Color },
        { MP_Normal,            SAMPLERTYPE_Normal },
        { MP_Metallic,          SAMPLERTYPE_LinearGrayscale },
        { MP_Specular,          SAMPLERTYPE_LinearGrayscale },
        { MP_Roughness,         SAMPLERTYPE_LinearGrayscale },
        { MP_AmbientOcclusion,  SAMPLERTYPE_LinearGrayscale },
        { MP_EmissiveColor,     SAMPLERTYPE_Color },
        { MP_Opacity,           SAMPLERTYPE_Color },
    };

    for (const auto& P : Props)
    {
        UMaterialExpression* Expr = SafeGetMaterialPropertyInputNode(Material, P.Prop);
        UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr);
        if (TexSample && TexSample->Texture)
            TexSample->SamplerType = P.Sampler;
    }

    Material->PreEditChange(nullptr);
    Material->PostEditChange();
}

int32 USekiroMaterialUtils::RemoveEmptyTextureSamples(UMaterial* Material)
{
    if (!Material) return 0;

    // Collect empty nodes first, then delete (avoid mutating while iterating)
    TArray<UMaterialExpression*> ToDelete;
    for (UMaterialExpression* Expr : Material->GetExpressions())
    {
        UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr);
        if (TexSample && !TexSample->Texture)
            ToDelete.Add(TexSample);
    }

    for (UMaterialExpression* Expr : ToDelete)
        UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);

    if (ToDelete.Num() > 0)
    {
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
    }
    return ToDelete.Num();
}
