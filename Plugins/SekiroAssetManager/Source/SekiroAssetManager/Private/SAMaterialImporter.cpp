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

/**
 * 将模型文件提供的材质槽名转换为 UE 包和对象名称可复用的稳定片段。
 * 函数只处理传入字符串副本，不访问 UObject，可在任意线程调用；不会添加材质资产的 M_ 前缀。
 *
 * @param RawName 外部材质槽原名，可以包含空格和 UE 对象名称不允许的标点符号。
 * @return 替换非法字符并清理首尾下划线后的名称；结果为空时返回 Unknown。
 */
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

    bool bUseEye = false;      // eye-specific (no metallic, emissive driven)
    float DefaultMetallic = 0.0f;
    float DefaultRoughness = 0.65f;

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
        BlendMode = BLEND_Masked;
        bUsePBR = true;
        DefaultRoughness = 0.8f;
        Material->TwoSided = true;
        break;

    case ESekiroShaderType::SSSCloth:
        BlendMode = BLEND_Masked;
        bUsePBR = true;
     // Subsurface for thin fabric/SSS
        DefaultRoughness = 0.7f;
        Material->TwoSided = true;
        break;

    case ESekiroShaderType::SSS:
    case ESekiroShaderType::Skin:
        BlendMode = BLEND_Opaque;
        bUsePBR = true;
        
        DefaultRoughness = 0.7f;   // skin is rough, prevents specular washout
        break;

    case ESekiroShaderType::Eye:
        BlendMode = BLEND_Opaque;
        bUsePBR = false;       // Eye uses emissive, not metallic
        DefaultRoughness = 0.4f;   // eyes are slightly glossy
        break;

    case ESekiroShaderType::DetailBlend:
    case ESekiroShaderType::FresnelBlend:
    case ESekiroShaderType::Standard:
    default:
        BlendMode = BLEND_Opaque;
        bUsePBR = true;
        DefaultRoughness = 0.65f;  // non-metallic materials should be rougher
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
    // Track current blend output per semantic for multi-entry chaining
    TMap<FString, UMaterialExpression*> BlendedOutputMap;

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
        BlendedOutputMap.Add(Slot.SemanticKey, TexSample);

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
    //   Simple Lerp chain: each detail blends with the accumulated result of previous details.
    //   Always use default output pin ("") for both TextureSample and Lerp nodes.
    // ========================================================================
    {
        TMap<FString, TArray<TPair<FString, UTexture2D*>>> MultiBySemantic;
        for (const auto& P : LoadedTextures)
        {
            if (P.Key.Contains(TEXT(":")))
            {
                int32 ColonIdx = 0;
                P.Key.FindChar(TCHAR(':'), ColonIdx);
                FString BaseSem = P.Key.Left(ColonIdx);
                int32 DetailIdx = FCString::Atoi(*P.Key.RightChop(ColonIdx + 1));
                if (DetailIdx >= 1 && BlendedOutputMap.Contains(BaseSem))
                    MultiBySemantic.FindOrAdd(BaseSem).Add(TPair<FString, UTexture2D*>(P.Key, P.Value));
            }
        }

        for (auto& SemPair : MultiBySemantic)
        {
            const FString& Sem = SemPair.Key;
            EMaterialProperty Prop = PropertyForSemantic(*Sem);
            if (Prop == MP_MAX) continue;

            UMaterialExpression* Current = BlendedOutputMap.FindRef(Sem);
            if (!Current) continue;

            uint8 Idx = 1;
            for (const auto& Entry : SemPair.Value)
            {
                UTexture2D* Tex = Entry.Value;
                if (!Tex) continue;

                int32 Y = 400 - (int32)(Idx * 200);

                auto* Sample = Cast<UMaterialExpressionTextureSample>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionTextureSample::StaticClass(), -400, Y));
                if (!Sample) continue;
                Sample->Texture = Tex;
                Sample->SamplerType = SamplerTypeForSemantic(*Sem);

                FString PName = FString::Printf(TEXT("Blend_%s_%d"), *Sem, Idx);
                auto* Factor = Cast<UMaterialExpressionScalarParameter>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionScalarParameter::StaticClass(), -200, Y + 40));
                if (!Factor) continue;
                Factor->ParameterName = FName(*PName);
                Factor->DefaultValue = 0.15f;
                Factor->Group = TEXT("MultiEntry");

                auto* Lerp = Cast<UMaterialExpressionLinearInterpolate>(
                    UMaterialEditingLibrary::CreateMaterialExpression(
                        Material, UMaterialExpressionLinearInterpolate::StaticClass(), 0, Y + 20));
                if (!Lerp) continue;

                // Use default output ("") for both TextureSample and Lerp
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Current, TEXT(""), Lerp, TEXT("A"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Sample), TEXT(""), Lerp, TEXT("B"));
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    Cast<UMaterialExpression>(Factor), TEXT(""), Lerp, TEXT("Alpha"));

                Current = Lerp;
                Idx++;
            }

            if (Current)
            {
                UMaterialEditingLibrary::ConnectMaterialProperty(Current, TEXT(""), Prop);
                UE_LOG(LogTemp, Log, TEXT("[SAMaterialImporter] Multi-entry chain '%s': %d detail(s)"),
                    *Sem, SemPair.Value.Num());
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

        // Default AO for materials without ao texture (prevents flat/washed-out look)
        if (!ConnectedProperties.Contains(MP_AmbientOcclusion))
        {
            UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionConstant::StaticClass(), -200, -850);
            if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
            {
                C->R = 0.6f;  // moderate occlusion darkens crevices naturally
                UMaterialEditingLibrary::ConnectMaterialProperty(C, TEXT(""), MP_AmbientOcclusion);
                ConnectedProperties.Add(MP_AmbientOcclusion);
            }
        }

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
                    C->R = DefaultRoughness;
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
