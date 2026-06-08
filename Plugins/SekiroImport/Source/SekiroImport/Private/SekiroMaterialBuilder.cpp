#include "SekiroMaterialBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "SekiroMaterialUtils.h"
#include "Import/SKTextureResolver.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "MaterialEditingLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// ============================================================================
// 工具函数（匿名命名空间）
// ============================================================================

/// 清理材质名中的非法字符
static FString SanitizeMaterialName(const FString& RawName)
{
    FString Clean = RawName;
    for (int32 i = 0; i < Clean.Len(); ++i)
    {
        TCHAR C = Clean[i];
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

/// 从路径提取无扩展名的文件名
static FString TextureNameFromPath(const FString& Path)
{
    FString Name = FPaths::GetCleanFilename(Path);
    int32 DotIdx;
    if (Name.FindLastChar('.', DotIdx)) Name.LeftInline(DotIdx);
    return Name;
}

// ============================================================================
// MTD → BlendMode 推导（对齐 Blender 管线 CLOTH_KEYWORDS 逻辑）
// ============================================================================

/// 从MTD路径推导BlendMode："Opaque" / "Masked" / "Translucent"
static FString DeriveBlendMode(const FString& MatName, const FString& MTDPath)
{
    if (MTDPath.IsEmpty()) return TEXT("Opaque");

    const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();
    const FString MatLower = MatName.ToLower();

    const bool bIsCloth = SekiroContainsClothKeyword(MatLower) || MtdBase.Contains(TEXT("cloth"));
    const bool bIsDecal = MtdBase.Contains(TEXT("decal"));

    if (bIsCloth)  return TEXT("Masked");
    if (bIsDecal)  return TEXT("Translucent");
    return TEXT("Opaque");
}

/// 从MTD路径推导TwoSided（对齐 Blender：is_decal OR is_cloth）
static bool DeriveTwoSided(const FString& MatName, const FString& MTDPath)
{
    if (MTDPath.IsEmpty()) return false;

    const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();
    const FString MatLower = MatName.ToLower();

    const bool bIsDecal = MtdBase.Contains(TEXT("decal"));
    const bool bIsCloth = SekiroContainsClothKeyword(MatLower) || MtdBase.Contains(TEXT("cloth"));

    return bIsDecal || bIsCloth;
}

// ============================================================================
// 父材质管理
// ============================================================================

/// 确保 M_SekiroBase 父材质存在（首次创建，后续复用）
static UMaterial* EnsureBaseMaterial(const FString& ParentPackagePath)
{
    // 尝试加载已有
    FString FullPath = ParentPackagePath + TEXT(".M_SekiroBase");
    UMaterial* ExistingMat = LoadObject<UMaterial>(nullptr, *FullPath);
    if (ExistingMat)
    {
        UE_LOG(LogSekiroImport, Log, TEXT("复用已有父材质: %s"), *FullPath);
        return ExistingMat;
    }

    // 创建 Package
    UPackage* Pkg = FSekiroImportModule::CreatePackageForOverwrite(ParentPackagePath);
    if (!Pkg)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建父材质Package: %s"), *ParentPackagePath);
        return nullptr;
    }

    UMaterial* Mat = NewObject<UMaterial>(Pkg, UMaterial::StaticClass(), TEXT("M_SekiroBase"),
        RF_Public | RF_Standalone);
    if (!Mat) return nullptr;

    Mat->bUsedWithSkeletalMesh = true;

    // 创建4个TextureSampleParameter2D节点，连线到对应材质属性
    // 全部用SAMPLERTYPE_Color——当无真实纹理时DefaultTexture兼容所有类型
    struct FParamDef { const TCHAR* Name; EMaterialProperty Property; int32 X; int32 Y; };
    const FParamDef Params[] = {
        { TEXT("_a"), MP_BaseColor,  -400,  200 },
        { TEXT("_n"), MP_Normal,     -400, -100 },
        { TEXT("_m"), MP_Metallic,   -400, -400 },
        { TEXT("_r"), MP_Roughness,  -400, -700 },
    };

    for (const FParamDef& P : Params)
    {
        UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
            Mat, UMaterialExpressionTextureSampleParameter2D::StaticClass(), P.X, P.Y);
        UMaterialExpressionTextureSampleParameter2D* TexNode =
            Cast<UMaterialExpressionTextureSampleParameter2D>(Expr);
        if (TexNode)
        {
            TexNode->ParameterName = P.Name;
            TexNode->SamplerType = SAMPLERTYPE_Color;
        }
        UMaterialEditingLibrary::ConnectMaterialProperty(Expr, TEXT(""), P.Property);
    }

    // Alpha→Opacity（始终连接，Opaque/Masked中无副作用）
    USekiroMaterialUtils::ConnectAlphaToOpacity(Mat);

    Mat->TwoSided = false;
    Mat->BlendMode = BLEND_Opaque;

    Mat->PreEditChange(nullptr);
    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    // 保存
    FString PackageFileName = FPackageName::LongPackageNameToFilename(
        ParentPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    UPackage::SavePackage(Pkg, Mat, *PackageFileName, SaveArgs);

    UE_LOG(LogSekiroImport, Log, TEXT("创建父材质: %s"), *ParentPackagePath);
    return Mat;
}

// ============================================================================
// Sekiro_Materials.json 加载
// ============================================================================

/// 加载 Sekiro_Materials.json，返回 材质名 → JSON对象 映射
static TMap<FString, TSharedPtr<FJsonObject>> LoadMaterialsJson()
{
    TMap<FString, TSharedPtr<FJsonObject>> Result;

    const FString JsonPath = FPaths::ProjectDir() / TEXT("Extracted/Textures/Sekiro_Materials.json");
    if (!FPaths::FileExists(JsonPath))
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("Sekiro_Materials.json 不存在: %s (将使用MTD推导)"), *JsonPath);
        return Result;
    }

    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("无法读取 Sekiro_Materials.json"));
        return Result;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("Sekiro_Materials.json 解析失败"));
        return Result;
    }

    const TArray<TSharedPtr<FJsonValue>>* MatsArray = nullptr;
    if (!Root->TryGetArrayField(TEXT("materials"), MatsArray)) return Result;

    for (const TSharedPtr<FJsonValue>& Val : *MatsArray)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (!Val->TryGetObject(Obj)) continue;
        FString Name = (*Obj)->GetStringField(TEXT("name"));
        Result.Add(Name, *Obj);
    }

    UE_LOG(LogSekiroImport, Log, TEXT("加载 Sekiro_Materials.json: %d 条材质配置"), Result.Num());
    return Result;
}

/// 在 JSON Map 中模糊匹配材质名（处理 Blender 的 _cloth 变体命名差异）
static const TSharedPtr<FJsonObject>* FuzzyFindJsonEntry(
    const TMap<FString, TSharedPtr<FJsonObject>>& Map, const FString& MatName)
{
    // 精确匹配
    if (const TSharedPtr<FJsonObject>* Exact = Map.Find(MatName))
        return Exact;

    // 模糊匹配
    for (const auto& Pair : Map)
    {
        if (Pair.Key.Contains(MatName) || MatName.Contains(Pair.Key))
            return &Pair.Value;
    }
    return nullptr;
}

// ============================================================================
// 材质实例创建
// ============================================================================

static UMaterialInstanceConstant* BuildSingleMIC(
    const FSekiroImportMaterial& Material,
    const FString& PackagePath,
    UMaterial* ParentMaterial,
    const TSharedPtr<FJsonObject>* JsonEntry,
    const TArray<FString>& TextureSearchPaths)
{
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackagePath);
    if (!Package) return nullptr;

    FString SafeName = SanitizeMaterialName(Material.Name);
    FString AssetName = FString::Printf(TEXT("MI_%s"), *SafeName);
    UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
        Package, UMaterialInstanceConstant::StaticClass(), FName(*AssetName),
        RF_Public | RF_Standalone);
    if (!MIC) return nullptr;

    MIC->SetParentEditorOnly(ParentMaterial);

    // --- BlendMode & TwoSided（MTD推导优先，JSON的blend_mode不可信——Blender管线已知有误）---
    FString BlendModeStr = DeriveBlendMode(Material.Name, Material.MTDPath);
    bool bTwoSided = DeriveTwoSided(Material.Name, Material.MTDPath);

    if (BlendModeStr.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase) ||
        BlendModeStr.Equals(TEXT("Transparent"), ESearchCase::IgnoreCase))
    {
        MIC->BasePropertyOverrides.bOverride_BlendMode = true;
        MIC->BasePropertyOverrides.BlendMode = BLEND_Translucent;
    }
    else if (BlendModeStr.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
    {
        MIC->BasePropertyOverrides.bOverride_BlendMode = true;
        MIC->BasePropertyOverrides.BlendMode = BLEND_Masked;
    }
    // Opaque 不需要设置（默认）

    MIC->BasePropertyOverrides.bOverride_TwoSided = true;
    MIC->BasePropertyOverrides.TwoSided = bTwoSided;

    // --- 纹理分配（从 Sekiro_Materials.json 的 textures 字段）---
    int32 TexAssigned = 0;
    if (JsonEntry)
    {
        const TSharedPtr<FJsonObject>* TexObj = nullptr;
        if ((*JsonEntry)->TryGetObjectField(TEXT("textures"), TexObj))
        {
            for (const auto& Pair : (*TexObj)->Values)
            {
                const FString& Suffix = Pair.Key;          // "_a", "_n", "_m", "_r"
                FString TexFilename = Pair.Value->AsString();  // "BD_M_9000_tops_a.png"

                FString TexStem = TextureNameFromPath(TexFilename);
                UTexture2D* Tex = FSKTextureResolver::FindTexture(TexStem, TextureSearchPaths);

                if (Tex)
                {
                    FName ParamName(*Suffix);
                    MIC->SetTextureParameterValueEditorOnly(ParamName, Tex);
                    ++TexAssigned;
                }
            }
        }
    }

    MIC->MarkPackageDirty();
    FString PackageFileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    UPackage::SavePackage(Package, MIC, *PackageFileName, SaveArgs);

    UE_LOG(LogSekiroImport, Log, TEXT("材质 '%s': %s, TwoSided=%d, %d 纹理"),
        *SafeName, *BlendModeStr, bTwoSided, TexAssigned);

    return MIC;
}

// ============================================================================
// 公共API
// ============================================================================

UMaterialInstanceConstant* FSekiroMaterialBuilderBuildSingleMIC(
    const FSekiroImportMaterial& Material, const FString& PackagePath)
{
    // 公开API保留兼容性（Pipeline中不用此接口，用BuildAll）
    UE_LOG(LogSekiroImport, Warning, TEXT("CreateMaterialInstance 单个调用已弃用，请使用 BuildAll"));
    return nullptr;
}

TArray<UMaterialInstanceConstant*> FSekiroMaterialBuilder::BuildAll(
    const FSekiroModelData& ModelData, USkeletalMesh* SkeletalMesh, const FString& BasePath)
{
    TArray<UMaterialInstanceConstant*> Results;

    if (ModelData.Materials.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("模型数据中没有材质信息"));
        return Results;
    }

    // 1. 加载 Sekiro_Materials.json
    TMap<FString, TSharedPtr<FJsonObject>> MatJsonMap = LoadMaterialsJson();

    // 2. 确保父材质存在
    //    BasePath = /Game/Characters/Sekiro/Materials
    //    → ParentPath = /Game/Characters/Sekiro/Materials
    FString ParentMatPath = BasePath;
    UMaterial* ParentMat = EnsureBaseMaterial(ParentMatPath);
    if (!ParentMat)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建/加载父材质，材质构建终止"));
        return Results;
    }

    // 3. 构建纹理搜索路径
    TArray<FString> TextureSearchPaths;
    // Textures 目录在 Materials 的兄弟目录
    FString SekiroBase = FPaths::GetPath(BasePath);  // /Game/Characters/Sekiro
    TextureSearchPaths.Add(SekiroBase / TEXT("Textures"));

    UE_LOG(LogSekiroImport, Log, TEXT("开始创建 %d 个材质实例 -> %s/"), ModelData.Materials.Num(), *BasePath);

    for (int32 i = 0; i < ModelData.Materials.Num(); ++i)
    {
        const FSekiroImportMaterial& Mat = ModelData.Materials[i];
        FString SafeName = SanitizeMaterialName(Mat.Name);
        FString MatPackagePath = FString::Printf(TEXT("%s/MI_%s"), *BasePath, *SafeName);

        const TSharedPtr<FJsonObject>* JsonEntry = FuzzyFindJsonEntry(MatJsonMap, Mat.Name);

        UMaterialInstanceConstant* MIC = BuildSingleMIC(
            Mat, MatPackagePath, ParentMat, JsonEntry, TextureSearchPaths);
        Results.Add(MIC);

        // 分配到骨骼网格体
        if (MIC && SkeletalMesh && i < SkeletalMesh->GetMaterials().Num())
        {
            SkeletalMesh->GetMaterials()[i].MaterialInterface = MIC;
        }
    }

    if (SkeletalMesh)
    {
        SkeletalMesh->MarkPackageDirty();
        SkeletalMesh->PostEditChange();
    }

    UE_LOG(LogSekiroImport, Log, TEXT("材质创建完成: %d 个材质实例"), Results.Num());
    return Results;
}
