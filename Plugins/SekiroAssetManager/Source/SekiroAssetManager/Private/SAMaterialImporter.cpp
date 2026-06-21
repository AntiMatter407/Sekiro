#include "SAMaterialImporter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/TextureFactory.h"

// ============================================================================
// 
// ============================================================================

FString SAMaterialImporter::SanitizeMaterialName(const FString& RawName)
{
    FString Clean = RawName;
    for (int32 i = 0; i < Clean.Len(); ++i)
    {
        const TCHAR C = Clean[i];
        if (C == '#' || C == ' ' || C == '/' || C == '\\' || C == ':' ||
            C == '*' || C == '?' || C == '"' || C == '<' || C == '>' || C == '|' || C == '.')
        {
            Clean[i] = '_';
        }
    }
    Clean.TrimStartAndEndInline();
    while (Clean.StartsWith(TEXT("_"))) Clean.RightChopInline(1);
    while (Clean.EndsWith(TEXT("_"))) Clean.LeftChopInline(1);
    if (Clean.IsEmpty()) Clean = TEXT("Unknown");
    return Clean;
}

EBlendMode SAMaterialImporter::ParseBlendMode(const FString& ResolvedBlendMode,
                                               const FString& MatName,
                                               bool bIsFurHair)
{
    if (bIsFurHair)
    {
        UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] '%s': fur/hair  Masked"), *MatName);
        return BLEND_Masked;
    }

    if (ResolvedBlendMode.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase))
        return BLEND_Translucent;

    if (ResolvedBlendMode.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
        return BLEND_Masked;

    //  Opaque
    return BLEND_Opaque;
}

TEnumAsByte<EMaterialSamplerType> SAMaterialImporter::SamplerTypeForSemantic(const FString& SemanticKey)
{
    // Keep SAMPLERTYPE_Color for all — texture asset SRGB/Compression handles conversion
    return SAMPLERTYPE_Color;
}

EMaterialProperty SAMaterialImporter::PropertyForSemantic(const FString& SemanticKey)
{
    const FString Key = SemanticKey.ToLower();

    if (Key == TEXT("albedo") || Key == TEXT("color") || Key == TEXT("diffuse"))
        return MP_BaseColor;

    if (Key == TEXT("normal"))
        return MP_Normal;

    if (Key == TEXT("metallic"))
        return MP_Metallic;

    if (Key == TEXT("roughness"))
        return MP_Roughness;

    if (Key == TEXT("ao") || Key == TEXT("ambientocclusion") || Key == TEXT("emissive") || Key == TEXT("em"))
        return MP_EmissiveColor;

    if (Key == TEXT("mask") || Key == TEXT("opacity"))
        return MP_OpacityMask;

    //  ?BaseColor
    return MP_BaseColor;
}

// ============================================================================
// 
// ============================================================================

TMap<FString, UTexture2D*> SAMaterialImporter::LoadTextures(
    const TMap<FString, FString>& ResolvedTextures,
    const TArray<FString>& SearchPaths,
    const TArray<FString>& TextureSourceDirs,
    const FString& TexturePackagePath)
{
    TMap<FString, UTexture2D*> Result;

    if (ResolvedTextures.Num() == 0)
        return Result;

    //  AssetRegistry stem  ?
    TMap<FString, FString> TextureCache;

    //  ?SearchPaths  AssetRegistry
    if (SearchPaths.Num() > 0)
    {
        FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        AssetRegistry.Get().SearchAllAssets(true); // Force scan

        FARFilter Filter;
        Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
        Filter.bRecursivePaths = true;
        for (const FString& Path : SearchPaths)
            Filter.PackagePaths.Add(FName(*Path));

        TArray<FAssetData> Assets;
        AssetRegistry.Get().GetAssets(Filter, Assets);

        for (const FAssetData& Asset : Assets)
        {
            FString Stem = Asset.AssetName.ToString().ToLower();
            FString ObjectPath = Asset.GetObjectPathString();
            if (!TextureCache.Contains(Stem))
                TextureCache.Add(Stem, ObjectPath);
        }

        UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Texture cache: %d textures"), TextureCache.Num());
    }

    //  ?ResolvedTextures 
    for (const auto& Pair : ResolvedTextures)
    {
        const FString& SemanticKey = Pair.Key;
        const FString& TexFileName = Pair.Value;

        //  stem
        FString Stem = FPaths::GetBaseFilename(TexFileName).ToLower();

        UTexture2D* Tex = nullptr;

        if (const FString* CachedPath = TextureCache.Find(Stem))
        {
            Tex = LoadObject<UTexture2D>(nullptr, **CachedPath);
            if (Tex)
                UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Cache hit: %s -> %s"), *TexFileName, **CachedPath);
        }
        if (!Tex)
        {
            FString TryPath = TexturePackagePath / Stem;
            Tex = LoadObject<UTexture2D>(nullptr, *TryPath);
        }

        if (!Tex && TextureSourceDirs.Num() > 0 && !TexturePackagePath.IsEmpty())
        {
            // Try to import from multiple source directories
            for (const FString& SrcDir : TextureSourceDirs)
            {
                if (SrcDir.IsEmpty()) continue;
                FString SourceFile = SrcDir / TexFileName;
                if (!IFileManager::Get().FileExists(*SourceFile))
                    continue;

                FString TexPkgPath = TexturePackagePath / Stem;
                FString TexAssetName = Stem;
                UPackage* TexPkg = CreatePackage(*TexPkgPath);
                if (!TexPkg) continue;

                UTextureFactory* TexFactory = NewObject<UTextureFactory>();
                TexFactory->AddToRoot(); // prevent GC
                TexFactory->bDeferCompression = true; // defer so TC_Normalmap/TC_Grayscale take effect before compression
                bool bOutCancelled = false;
                UObject* ImportedObj = TexFactory->FactoryCreateFile(
                    UTexture2D::StaticClass(), TexPkg, FName(*TexAssetName),
                    RF_Public | RF_Standalone, SourceFile, nullptr, GWarn, bOutCancelled);
                TexFactory->RemoveFromRoot();

                if (ImportedObj)
                {
                    Tex = Cast<UTexture2D>(ImportedObj);
                    if (Tex)
                    {
                        Tex->MarkPackageDirty();
                        FString FileName = FPackageName::LongPackageNameToFilename(TexPkgPath, FPackageName::GetAssetPackageExtension());
                        FSavePackageArgs SaveArgs;
                        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                        SaveArgs.Error = GWarn;
                        UPackage::SavePackage(TexPkg, Tex, *FileName, SaveArgs);
                        UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Imported texture: %s -> %s"), *SourceFile, *TexPkgPath);
                    }
                    break; // Found and imported, stop searching
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("[SAMaterialImporter] Texture import failed: %s"), *SourceFile);
                }
            }
        }

        if (Tex)
        {
            // Texture compression settings (match old SekiroImport FixTextureCompression)
            FString SemanticLower = SemanticKey.ToLower();

            if (SemanticLower == TEXT("albedo") || SemanticLower == TEXT("color"))
            {
                Tex->SRGB = true;
                Tex->CompressionSettings = TC_Default;
            }
            else if (SemanticLower == TEXT("normal"))
            {
                Tex->SRGB = false;
                Tex->CompressionSettings = TC_Normalmap;
            }
            else if (SemanticLower == TEXT("metallic") || SemanticLower == TEXT("roughness"))
            {
                Tex->SRGB = false;
                Tex->CompressionSettings = TC_Grayscale;
            }
            else
            {
                // mask, ao, emissive: linear, default compression
                Tex->SRGB = false;
                Tex->CompressionSettings = TC_Default;
            }
            // Force DDC rebuild by invalidating and updating
            Tex->PostEditChange();
            Tex->UpdateResource();

            UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter]  '%s'  ?'%s' (SRGB=%d, Compression=%d)"),
                *SemanticKey, *TexFileName, Tex->SRGB ? 1 : 0, (int32)Tex->CompressionSettings);
            Result.Add(SemanticKey, Tex);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[SAMaterialImporter]  ? '%s' (stem=%s)"), *TexFileName, *Stem);
        }
    }

    return Result;
}

// ============================================================================
// 
// ============================================================================

UMaterial* SAMaterialImporter::BuildSingle(const FSAImportMaterial& Mat,
                                            const FString& PackagePath,
                                            const TArray<FString>& TextureSourceDirs,
                                            const FString& TexturePackagePath)
{
    //  Package
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("[SAMaterialImporter]  Package: %s"), *PackagePath);
        return nullptr;
    }

    FString SafeName = SanitizeMaterialName(Mat.Name);
    FString AssetName = FString::Printf(TEXT("M_%s"), *SafeName);

    //  UMaterial
    UMaterial* Material = NewObject<UMaterial>(
        Package, UMaterial::StaticClass(), FName(*AssetName),
        RF_Public | RF_Standalone);

    if (!Material)
    {
        UE_LOG(LogTemp, Error, TEXT("[SAMaterialImporter]  UMaterial: %s"), *AssetName);
        return nullptr;
    }

    Material->bUsedWithSkeletalMesh = true;

    // ========================================================================
    // Per-ShaderType configuration
    // ========================================================================
    const ESekiroShaderType ST = Mat.ShaderType;
    EBlendMode BlendMode = BLEND_Opaque;
    bool bUsePBR = true;       // metallic + roughness slots
    bool bUseSSS = false;      // subsurface shading model
    bool bUseEye = false;      // eye-specific (no metallic, emissive driven)
    float DefaultMetallic = 0.0f;
    float DefaultRoughness = 0.5f;

    switch (ST)
    {
    case ESekiroShaderType::Fur:
    case ESekiroShaderType::FurCloth:
        BlendMode = BLEND_Masked;
        bUsePBR = false;       // Fur uses constant Metallic=0, Roughness=0.6
        DefaultRoughness = 0.6f;
        Material->TwoSided = true;
        break;

    case ESekiroShaderType::Cloth:
    case ESekiroShaderType::DetailBlendCloth:
    case ESekiroShaderType::FresnelBlendCloth:
    case ESekiroShaderType::SSSCloth:
        BlendMode = BLEND_Masked;
        bUsePBR = true;
        Material->TwoSided = true;
        break;

    case ESekiroShaderType::SSS:
    case ESekiroShaderType::Skin:
        BlendMode = BLEND_Opaque;
        bUsePBR = true;
        bUseSSS = true;
        break;

    case ESekiroShaderType::Eye:
        BlendMode = BLEND_Opaque;
        bUsePBR = false;       // Eye uses emissive, not metallic
        break;

    case ESekiroShaderType::DetailBlend:
    case ESekiroShaderType::FresnelBlend:
    case ESekiroShaderType::Standard:
    default:
        BlendMode = BLEND_Opaque;
        bUsePBR = true;
        break;
    }

    // Let JSON ResolvedBlendMode override (e.g. "_blend" decal ? Translucent)
    if (Mat.ResolvedBlendMode.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase))
        BlendMode = BLEND_Translucent;
    else if (Mat.ResolvedBlendMode.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
        BlendMode = BLEND_Masked;

    Material->BlendMode = BlendMode;

    if (BlendMode == BLEND_Masked)
        Material->OpacityMaskClipValue = Mat.ClipValue > 0.0f ? Mat.ClipValue : 0.33f;

    if (!Material->TwoSided)
        Material->TwoSided = Mat.bTwoSided || Mat.bIsCloth || Mat.bIsDecal;

    // Subsurface shading model
    if (bUseSSS)
    {
        Material->SetShadingModel(MSM_Subsurface);
        UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter]  '%s': Subsurface shading model"), *AssetName);
    }

    UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter]  '%s': ShaderType=%d BlendMode=%d TwoSided=%d"),
        *AssetName, (int32)ST, (int32)BlendMode, Material->TwoSided ? 1 : 0);

    // Texture loading
    FString BasePath = FPaths::GetPath(PackagePath);
    TArray<FString> SearchPaths;
    SearchPaths.Add(FPaths::GetPath(BasePath) / TEXT("Textures"));
    SearchPaths.Add(TEXT("/Game/Textures"));
    SearchPaths.Add(TEXT("/Game/Textures"));

    TMap<FString, UTexture2D*> LoadedTextures = LoadTextures(Mat.ResolvedTextures, SearchPaths, TextureSourceDirs, TexturePackagePath);

    // ?? Slot definitions ??
    struct FSAMaterialSlot
    {
        const TCHAR* SemanticKey;
        EMaterialProperty Property;
        int32 EditorX;
        int32 EditorY;
    };

    TArray<FSAMaterialSlot> Slots;

    // Core slots (albedo + normal) ? always present
    Slots.Add({ TEXT("albedo"),  MP_BaseColor,       -600,  200 });
    Slots.Add({ TEXT("normal"),  MP_Normal,          -600, -100 });

    // PBR slots ? per shader type
    if (bUsePBR)
    {
        Slots.Add({ TEXT("metallic"),  MP_Metallic,  -600, -400 });
        Slots.Add({ TEXT("roughness"), MP_Roughness, -600, -700 });
    }

    // Opacity mask ? Masked materials
    if (BlendMode == BLEND_Masked)
    {
        Slots.Add({ TEXT("mask"),         MP_OpacityMask,  -600,  500 });
        Slots.Add({ TEXT("opacityMask"),  MP_OpacityMask,  -600,  500 });
    }
    else if (BlendMode == BLEND_Translucent)
    {
        Slots.Add({ TEXT("opacity"), MP_Opacity, -600, 600 });
    }

    // Emissive ? all but Eye gets dedicated emissive slot
    Slots.Add({ TEXT("emissive"), MP_EmissiveColor, -600, 800 });
    Slots.Add({ TEXT("em"),       MP_EmissiveColor, -600, 800 });

    // AO
    Slots.Add({ TEXT("ao"), MP_AmbientOcclusion, -600, 1000 });

    // Eye-specific: connect emissive as primary color driver
    if (bUseEye)
    {
        Slots.Add({ TEXT("emissive"), MP_BaseColor, -600, 200 }); // override albedo with emissive
    }

    TSet<EMaterialProperty> ConnectedProperties;
    TMap<FString, UMaterialExpressionTextureSample*> SemanticSampleMap;

    // Track albedo for Translucent alpha connection
    UMaterialExpressionTextureSample* AlbedoSample = nullptr;

    for (const FSAMaterialSlot& Slot : Slots)
    {
        // 
        UTexture2D* const* FoundTex = LoadedTextures.Find(Slot.SemanticKey);
        if (!FoundTex || !*FoundTex)
            continue;

        if (ConnectedProperties.Contains(Slot.Property))
            continue;

        UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
            Material, UMaterialExpressionTextureSample::StaticClass(),
            Slot.EditorX, Slot.EditorY);

        UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr);
        if (!TexSample)
            continue;

        TexSample->Texture = *FoundTex;
        // Set sampler type per semantic (uses centralized SamplerTypeForSemantic)
        TexSample->SamplerType = SamplerTypeForSemantic(Slot.SemanticKey);
        SemanticSampleMap.Add(Slot.SemanticKey, TexSample);

        if (UMaterialEditingLibrary::ConnectMaterialProperty(TexSample, TEXT(""), Slot.Property))
        {
            ConnectedProperties.Add(Slot.Property);

            //  albedo 
            if (Slot.Property == MP_BaseColor)
                AlbedoSample = TexSample;
        }
    }

    // ========================================================================
    // Multi-entry texture blending
    //   Blend strategy depends on ShaderType + Semantic:
    //   - albedo (DetailBlend/Fur/Cloth): Overlay (Multiply+Screen)
    //   - albedo (Standard/SSS/Skin):     Lerp(base, detail, 0.33)
    //   - normal (all):                   BlendAngleCorrectedNormals
    //   - roughness/metallic:             Lerp(base, detail, 0.33)
    // ========================================================================
    {
        TArray<TPair<FString, UTexture2D*>> MultiEntries;
        for (const auto& P : LoadedTextures)
        {
            if (P.Key.Contains(TEXT(":")))
                MultiEntries.Add(TPair<FString, UTexture2D*>(P.Key, P.Value));
        }

        for (const auto& Entry : MultiEntries)
        {
            const FString& Key = Entry.Key;
            UTexture2D* DetailTex = Entry.Value;
            if (!DetailTex) continue;

            int32 ColonIdx = 0;
            Key.FindChar(TCHAR(':'), ColonIdx);
            FString BaseSemantic = Key.Left(ColonIdx);
            int32 DetailIdx = FCString::Atoi(*Key.RightChop(ColonIdx + 1));
            if (DetailIdx < 1) continue;

            EMaterialProperty TargetProp = PropertyForSemantic(*BaseSemantic);
            if (TargetProp == MP_MAX) continue;

            UTexture2D* const* BaseTexPtr = LoadedTextures.Find(BaseSemantic);
            if (!BaseTexPtr || !*BaseTexPtr) continue;

            uint8 NodeY = 400 - (uint8)(DetailIdx * 250);

            // (1) Detail texture sample
            UMaterialExpressionTextureSample* DSample = Cast<UMaterialExpressionTextureSample>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionTextureSample::StaticClass(), -400, NodeY));
            if (!DSample) continue;
            DSample->Texture = DetailTex;
            DSample->SamplerType = SamplerTypeForSemantic(*BaseSemantic);

            UMaterialExpressionTextureSample* const* BaseSamplePtr = SemanticSampleMap.Find(BaseSemantic);
            if (!BaseSamplePtr || !*BaseSamplePtr) continue;

            const bool bIsAlbedo = (TargetProp == MP_BaseColor);
            const bool bIsNormal = (TargetProp == MP_Normal);
            const bool bIsOverlay = bIsAlbedo && (
                Mat.ShaderType == ESekiroShaderType::Fur ||
                Mat.ShaderType == ESekiroShaderType::DetailBlend ||
                Mat.ShaderType == ESekiroShaderType::Cloth ||
                Mat.ShaderType == ESekiroShaderType::DetailBlendCloth ||
                Mat.ShaderType == ESekiroShaderType::FurCloth ||
                Mat.ShaderType == ESekiroShaderType::FresnelBlendCloth ||
                Mat.ShaderType == ESekiroShaderType::SSSCloth);

            const bool bUseLerp = !bIsNormal && !bIsOverlay;

            if (bUseLerp)
            {
                // ?? Lerp Blend: albedo(Standard/SSS/Skin) or roughness/metallic ??
                FString ParamName = FString::Printf(TEXT("BlendWeight_%s_%d"), *BaseSemantic, DetailIdx);
                auto* Factor = Cast<UMaterialExpressionScalarParameter>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionScalarParameter::StaticClass(), -200, NodeY + 40));
                if (!Factor) continue;
                Factor->ParameterName = FName(*ParamName);
                Factor->DefaultValue = 0.33f;
                Factor->Group = TEXT("MultiEntry");

                auto* Lerp = Cast<UMaterialExpressionLinearInterpolate>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionLinearInterpolate::StaticClass(), 0, NodeY + 20));
                if (!Lerp) continue;

                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(*BaseSamplePtr), TEXT("RGB"),
                    Cast<UMaterialExpression>(Lerp), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(DSample), TEXT("RGB"),
                    Cast<UMaterialExpression>(Lerp), TEXT("B"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Factor), TEXT(""),
                    Cast<UMaterialExpression>(Lerp), TEXT("Alpha"));
                UMaterialEditingLibrary::ConnectMaterialProperty(
                    Cast<UMaterialExpression>(Lerp), TEXT(""), TargetProp);

                UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Multi-entry Lerp: %s:%d -> property %d  param='%s'"),
                    *BaseSemantic, DetailIdx, (int32)TargetProp, *ParamName);
            }
            else if (bIsOverlay)
            {
                // ?? Overlay Blend: albedo for Fur/DetailBlend/Cloth shaders ??
                // Overlay = base < 0.5 ? 2*base*detail : 1-2*(1-base)*(1-detail)
                // Step: Multiply(base, detail) -> Multiply(2) -> ... [complex]
                // Simplified: just use Lerp with 0.5 blend for now
                // TODO: implement full Overlay blend with ComponentMask, Multiply, Add, Subtract
                FString ParamName = FString::Printf(TEXT("BlendWeight_%s_%d"), *BaseSemantic, DetailIdx);
                auto* Factor = Cast<UMaterialExpressionScalarParameter>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionScalarParameter::StaticClass(), -200, NodeY + 40));
                if (!Factor) continue;
                Factor->ParameterName = FName(*ParamName);
                Factor->DefaultValue = 0.5f;
                Factor->Group = TEXT("MultiEntry");

                auto* Lerp = Cast<UMaterialExpressionLinearInterpolate>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionLinearInterpolate::StaticClass(), 0, NodeY + 20));
                if (!Lerp) continue;

                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(*BaseSamplePtr), TEXT("RGB"),
                    Cast<UMaterialExpression>(Lerp), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(DSample), TEXT("RGB"),
                    Cast<UMaterialExpression>(Lerp), TEXT("B"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Factor), TEXT(""),
                    Cast<UMaterialExpression>(Lerp), TEXT("Alpha"));
                UMaterialEditingLibrary::ConnectMaterialProperty(
                    Cast<UMaterialExpression>(Lerp), TEXT(""), TargetProp);

                UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Multi-entry Overlay(apx): %s:%d -> property %d  param='%s'"),
                    *BaseSemantic, DetailIdx, (int32)TargetProp, *ParamName);
            }
            else if (bIsNormal)
            {
                // ?? BlendAngleCorrectedNormals ??
                // Formula: normalize(base*2-1 + detail*2-1) -> (result+1)/2
                // Using individual expression nodes for full math

                // Expand base normal from [0,1] to [-1,1]: Base*2 - 1
                auto* MulBase = Cast<UMaterialExpressionMultiply>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionMultiply::StaticClass(), -200, NodeY));
                auto* Const2a = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), -350, NodeY - 30));
                if (!MulBase || !Const2a) continue;
                Const2a->Constant = FLinearColor(2.0f, 2.0f, 2.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(*BaseSamplePtr), TEXT("RGB"),
                    Cast<UMaterialExpression>(MulBase), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const2a), TEXT(""),
                    Cast<UMaterialExpression>(MulBase), TEXT("B"));

                auto* SubBase = Cast<UMaterialExpressionSubtract>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionSubtract::StaticClass(), -50, NodeY));
                auto* Const1a = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), -200, NodeY - 50));
                if (!SubBase || !Const1a) continue;
                Const1a->Constant = FLinearColor(1.0f, 1.0f, 1.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(MulBase), TEXT(""),
                    Cast<UMaterialExpression>(SubBase), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const1a), TEXT(""),
                    Cast<UMaterialExpression>(SubBase), TEXT("B"));

                // Expand detail normal from [0,1] to [-1,1]: Detail*2 - 1
                auto* MulDet = Cast<UMaterialExpressionMultiply>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionMultiply::StaticClass(), -200, NodeY - 120));
                auto* Const2b = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), -350, NodeY - 150));
                if (!MulDet || !Const2b) continue;
                Const2b->Constant = FLinearColor(2.0f, 2.0f, 2.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(DSample), TEXT("RGB"),
                    Cast<UMaterialExpression>(MulDet), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const2b), TEXT(""),
                    Cast<UMaterialExpression>(MulDet), TEXT("B"));

                auto* SubDet = Cast<UMaterialExpressionSubtract>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionSubtract::StaticClass(), -50, NodeY - 120));
                auto* Const1b = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), -200, NodeY - 170));
                if (!SubDet || !Const1b) continue;
                Const1b->Constant = FLinearColor(1.0f, 1.0f, 1.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(MulDet), TEXT(""),
                    Cast<UMaterialExpression>(SubDet), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const1b), TEXT(""),
                    Cast<UMaterialExpression>(SubDet), TEXT("B"));

                // Add expanded normals: BaseExp + DetailExp
                auto* AddN = Cast<UMaterialExpressionAdd>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionAdd::StaticClass(), 100, NodeY - 60));
                if (!AddN) continue;
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(SubBase), TEXT(""),
                    Cast<UMaterialExpression>(AddN), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(SubDet), TEXT(""),
                    Cast<UMaterialExpression>(AddN), TEXT("B"));

                // Normalize: AddN / sqrt(dot(AddN, AddN))
                auto* Dot = Cast<UMaterialExpressionDotProduct>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionDotProduct::StaticClass(), 250, NodeY - 60));
                if (!Dot) continue;
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(AddN), TEXT(""),
                    Cast<UMaterialExpression>(Dot), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(AddN), TEXT(""),
                    Cast<UMaterialExpression>(Dot), TEXT("B"));

                auto* SqrtN = Cast<UMaterialExpressionSquareRoot>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionSquareRoot::StaticClass(), 400, NodeY - 60));
                if (!SqrtN) continue;
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Dot), TEXT(""),
                    Cast<UMaterialExpression>(SqrtN), TEXT(""));

                auto* DivN = Cast<UMaterialExpressionDivide>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionDivide::StaticClass(), 550, NodeY - 60));
                if (!DivN) continue;
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(AddN), TEXT(""),
                    Cast<UMaterialExpression>(DivN), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(SqrtN), TEXT(""),
                    Cast<UMaterialExpression>(DivN), TEXT("B"));

                // Pack back to [0,1]: (Normalized + 1) / 2
                auto* Add1 = Cast<UMaterialExpressionAdd>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionAdd::StaticClass(), 700, NodeY - 60));
                auto* Const1c = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), 650, NodeY - 100));
                if (!Add1 || !Const1c) continue;
                Const1c->Constant = FLinearColor(1.0f, 1.0f, 1.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(DivN), TEXT(""),
                    Cast<UMaterialExpression>(Add1), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const1c), TEXT(""),
                    Cast<UMaterialExpression>(Add1), TEXT("B"));

                auto* Div2 = Cast<UMaterialExpressionDivide>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionDivide::StaticClass(), 850, NodeY - 60));
                auto* Const2c = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionConstant3Vector::StaticClass(), 800, NodeY - 100));
                if (!Div2 || !Const2c) continue;
                Const2c->Constant = FLinearColor(2.0f, 2.0f, 2.0f);
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Add1), TEXT(""),
                    Cast<UMaterialExpression>(Div2), TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Const2c), TEXT(""),
                    Cast<UMaterialExpression>(Div2), TEXT("B"));

                UMaterialEditingLibrary::ConnectMaterialProperty(
                    Cast<UMaterialExpression>(Div2), TEXT(""), TargetProp);

                UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Multi-entry BlendAngleCorrectedNormals: %s:%d"),
                    *BaseSemantic, DetailIdx);
            }
        }
    }

    //  Masked: fallback to albedo alpha when no explicit mask texture
    if (BlendMode == BLEND_Masked && AlbedoSample && !ConnectedProperties.Contains(MP_OpacityMask))
    {
        UMaterialEditingLibrary::ConnectMaterialProperty(AlbedoSample, TEXT("A"), MP_OpacityMask);
        ConnectedProperties.Add(MP_OpacityMask);
    }

    //  Translucent:  ?albedo  ?Alpha  Opacity 
    if (BlendMode == BLEND_Translucent && AlbedoSample && !ConnectedProperties.Contains(MP_Opacity))
    {
        UMaterialEditingLibrary::ConnectMaterialProperty(AlbedoSample, TEXT("A"), MP_Opacity);
        ConnectedProperties.Add(MP_Opacity);
    }

    // ?? ShaderType-specific default constants ??
    {
        const bool bFur = (ST == ESekiroShaderType::Fur || ST == ESekiroShaderType::FurCloth);
        const bool bEye = (ST == ESekiroShaderType::Eye);

        if (bFur)
        {
            // Fur: Metallic=0, Roughness=0.6
            if (!ConnectedProperties.Contains(MP_Metallic))
            {
                UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionConstant::StaticClass(), -200, -400);
                if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
                {
                    C->R = 0.0f;
                    UMaterialEditingLibrary::ConnectMaterialProperty(C, TEXT(""), MP_Metallic);
                }
            }
            if (!ConnectedProperties.Contains(MP_Roughness))
            {
                UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionConstant::StaticClass(), -200, -700);
                if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
                {
                    C->R = 0.6f;
                    UMaterialEditingLibrary::ConnectMaterialProperty(C, TEXT(""), MP_Roughness);
                }
            }
        }

        if (bEye)
        {
            // Eye: Metallic=0 (eye doesn't use metallic)
            if (!ConnectedProperties.Contains(MP_Metallic))
            {
                UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionConstant::StaticClass(), -200, -400);
                if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
                {
                    C->R = 0.0f;
                    UMaterialEditingLibrary::ConnectMaterialProperty(C, TEXT(""), MP_Metallic);
                }
            }
        }
    }

    //   
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->MarkPackageDirty();

    //  
    FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;

    bool bSaved = UPackage::SavePackage(Package, Material, *FileName, SaveArgs);
    if (!bSaved)
    {
        UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Material save pending (async): %s"), *PackagePath);
    }

    int32 TexCount = 0;
    for (const auto& P : LoadedTextures) if (P.Value) ++TexCount;
    UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter]  '%s': BlendMode=%d, TwoSided=%d, %d"),
        *AssetName, (int32)BlendMode, Material->TwoSided ? 1 : 0, TexCount);

    return Material;
}

// ============================================================================
// 
// ============================================================================

TArray<UMaterial*> SAMaterialImporter::BuildAll(
    const FSAModelData& ModelData,
    USkeletalMesh* SkeletalMesh,
    const FString& BasePackagePath,
    const TArray<FString>& TextureSourceDirs)
{
    TArray<UMaterial*> Results;

    if (ModelData.Materials.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SAMaterialImporter] No materials in model data"));
        return Results;
    }

    UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Creating %d materials -> %s/"),
        ModelData.Materials.Num(), *BasePackagePath);

    // Determine texture source and target paths
    FString MatDir = BasePackagePath / TEXT("Materials");
    FString TexturePackagePath = BasePackagePath / TEXT("Textures");

    for (int32 i = 0; i < ModelData.Materials.Num(); ++i)
    {
        const FSAImportMaterial& Mat = ModelData.Materials[i];

        //  ? BasePath/M_SafeName
        FString SafeName = SanitizeMaterialName(Mat.Name);
        FString MatPackagePath = FString::Printf(TEXT("%s/M_%s"), *MatDir, *SafeName);

        UMaterial* Material = BuildSingle(Mat, MatPackagePath, TextureSourceDirs, TexturePackagePath);
        Results.Add(Material);

        // 
        if (Material && SkeletalMesh && i < SkeletalMesh->GetMaterials().Num())
        {
            SkeletalMesh->GetMaterials()[i].MaterialInterface = Material;
        }
    }

    //  ?    if (SkeletalMesh)
    {
        SkeletalMesh->MarkPackageDirty();
        SkeletalMesh->PostEditChange();

        //  SkeletalMesh
        UPackage* MeshPackage = SkeletalMesh->GetOutermost();
        if (MeshPackage)
        {
            FString MeshFileName = FPackageName::LongPackageNameToFilename(
                MeshPackage->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs MeshSaveArgs;
            MeshSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            MeshSaveArgs.Error = GWarn;
            UPackage::SavePackage(MeshPackage, SkeletalMesh, *MeshFileName, MeshSaveArgs);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Material creation done: %d"), Results.Num());
    return Results;
}
