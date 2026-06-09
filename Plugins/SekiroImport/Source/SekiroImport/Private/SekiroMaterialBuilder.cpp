#include "SekiroMaterialBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "Import/SKTextureResolver.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "MaterialEditingLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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

/// 推导BlendMode（对齐Blender common_blender.py:623-652）
/// 优先级: ParsedBlendMode(模型JSON已解析, UE5格式) > Sekiro_Materials.json MTDInfo > 关键词推导 > 默认Opaque
static FString DeriveBlendMode(const FString& ParsedBlendMode, const FString& MatName,
    const FString& MTDPath, const TSharedPtr<FJsonObject>* JsonEntry)
{
    // 优先级1: 模型JSON中已解析的BlendMode（已通过MTD→UE5映射）
    if (!ParsedBlendMode.IsEmpty())
    {
        return ParsedBlendMode;
    }

    const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();
    const FString MatLower = MatName.ToLower();

    const bool bIsCloth = SekiroContainsClothKeyword(MatLower) || SekiroContainsClothKeyword(MtdBase);
    const bool bIsDecal = MtdBase.Contains(TEXT("decal"));

    // 优先级2: Sekiro_Materials.json 的 MTDInfo.BlendMode
    if (JsonEntry)
    {
        const TSharedPtr<FJsonObject>* MtdInfoObj = nullptr;
        if ((*JsonEntry)->TryGetObjectField(TEXT("MTDInfo"), MtdInfoObj))
        {
            FString MtdBlend = (*MtdInfoObj)->GetStringField(TEXT("BlendMode"));
            if (!MtdBlend.IsEmpty())
            {
                static const TMap<FString, FString> MtdToUE5 = {
                    { TEXT("Normal"),   TEXT("Opaque") },
                    { TEXT("TexEdge"),  TEXT("Masked") },
                    { TEXT("Blend"),    TEXT("Translucent") },
                    { TEXT("Water"),    TEXT("Translucent") },
                    { TEXT("Add"),      TEXT("Translucent") },
                    { TEXT("Sub"),      TEXT("Translucent") },
                    { TEXT("Mul"),      TEXT("Translucent") },
                    { TEXT("LSBlend"),  TEXT("Translucent") },
                    { TEXT("LSAdd"),    TEXT("Translucent") },
                };
                if (const FString* UeBlend = MtdToUE5.Find(MtdBlend))
                {
                    return *UeBlend;
                }
            }
        }
    }

    // 优先级3: 关键词推导
    if (bIsCloth)  { UE_LOG(LogSekiroImport, Warning, TEXT("[BlendMode] %s -> Masked"), *MatName); return TEXT("Masked"); }
    if (bIsDecal)  { UE_LOG(LogSekiroImport, Warning, TEXT("[BlendMode] %s -> Translucent"), *MatName); return TEXT("Translucent"); }
    UE_LOG(LogSekiroImport, Warning, TEXT("[BlendMode] %s -> Opaque (bIsCloth=%d)"), *MatName, bIsCloth);
    return TEXT("Opaque");
}

/// 从MTD路径推导TwoSided：贴花/布料材质需要双面渲染
static bool DeriveTwoSided(const FString& MatName, const FString& MTDPath)
{
    if (MTDPath.IsEmpty()) return false;
    const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();
    const FString MatLower = MatName.ToLower();
    const bool bIsDecal = MtdBase.Contains(TEXT("decal"));
    const bool bIsCloth = SekiroContainsClothKeyword(MatLower) || SekiroContainsClothKeyword(MtdBase);
    return bIsDecal || bIsCloth;
}

// ============================================================================
// 纹理匹配系统（对齐 Blender assign_textures_globally / score_texture_candidate）
// ============================================================================

/// 从纹理文件名拆分核心名和语义后缀
/// "BD_M_9000_tops_a" → ("BD_M_9000_tops", "_a")
static TPair<FString, FString> StripTextureSuffix(const FString& Stem)
{
	for (const TCHAR* Suffix : { TEXT("_a"), TEXT("_n"), TEXT("_m"), TEXT("_r") })
	{
		if (Stem.EndsWith(Suffix))
		{
			return TPair<FString, FString>(Stem.LeftChop(FCString::Strlen(Suffix)), Suffix);
		}
	}
	return TPair<FString, FString>(Stem, FString());
}

/// 从 Shader 参数名提取纹理语义后缀（对齐 Blender PARAM_NAME_SEMANTIC_MAP）
static FString ParamNameToSuffix(const FString& ParamName)
{
	if (ParamName.IsEmpty()) return FString();

	const FString Low = ParamName.ToLower();
	static const TPair<const TCHAR*, const TCHAR*> Map[] = {
		{ TEXT("albedomap"),    TEXT("_a") },
		{ TEXT("diffusemap"),   TEXT("_a") },
		{ TEXT("basecolormap"), TEXT("_a") },
		{ TEXT("normalmap"),    TEXT("_n") },
		{ TEXT("bumpmap"),      TEXT("_n") },
		{ TEXT("detailbumpmap"),TEXT("_n") },
		{ TEXT("specularmap"),  TEXT("_m") },
		{ TEXT("metallicmap"),  TEXT("_m") },
		{ TEXT("reflectancemap"),TEXT("_m") },
		{ TEXT("roughnessmap"), TEXT("_r") },
		{ TEXT("shininessmap"), TEXT("_r") },
	};
	for (const auto& Pair : Map)
	{
		if (Low.Contains(Pair.Key))
		{
			return Pair.Value;
		}
	}
	return FString();
}

/// 评分: 纹理stem与材质匹配度（对齐 Blender score_texture_candidate）
/// 返回 (score, suffix)，score=0 表示不匹配
static TPair<int32, FString> ScoreTextureCandidate(
	const FString& PartName,
	const FString& MatName,
	const FString& MTDPath,
	const FString& Stem)
{
	auto [Core, Suffix] = StripTextureSuffix(Stem);

	// 仅处理已知语义后缀
	if (Suffix != TEXT("_a") && Suffix != TEXT("_n") &&
		Suffix != TEXT("_m") && Suffix != TEXT("_r"))
	{
		return {0, Suffix};
	}

	const FString PartLower = PartName.ToLower();
	const FString StemLower = Stem.ToLower();
	const FString MatLower = MatName.ToLower();
	const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();

	// Part前缀检查
	if (!PartLower.IsEmpty() && !StemLower.StartsWith(PartLower + TEXT("_")))
	{
		// Decal材质例外：HD_* 可以使用 FC_* 贴图
		if (!(MtdBase.Contains(TEXT("decal")) && StemLower.StartsWith(TEXT("fc_"))))
		{
			return {0, Suffix};
		}
	}

	const FString PartPrefix = PartLower + TEXT("_");
	const FString CoreLower = Core.ToLower();

	// 剥离Part前缀后的核心名
	FString MatCore = MatLower;
	if (MatLower.StartsWith(PartPrefix))
	{
		MatCore = MatLower.Mid(PartPrefix.Len());
	}

	FString MtdCore = MtdBase;
	if (MtdBase.StartsWith(TEXT("p_") + PartPrefix))
	{
		MtdCore = MtdBase.Mid(3 + PartPrefix.Len()); // Skip "p_" + PartPrefix
	}
	else if (MtdBase.StartsWith(TEXT("p_")))
	{
		MtdCore = MtdBase.Mid(2);
	}

	FString TexCore = CoreLower;
	if (TexCore.StartsWith(PartPrefix))
	{
		TexCore = TexCore.Mid(PartPrefix.Len());
	}
	if (TexCore.EndsWith(TEXT("_new")))
	{
		TexCore = TexCore.LeftChop(4);
	}

	int32 Score = 0;
	const FString Haystack = MatCore + TEXT(" ") + MtdCore;

	// 核心名完全包含 → +100
	if (!TexCore.IsEmpty() && Haystack.Contains(TexCore))
	{
		Score += 100;
	}

	// Token匹配
	TArray<FString> TexTokens;
	TexCore.Replace(TEXT("-"), TEXT("_")).ParseIntoArray(TexTokens, TEXT("_"), true);

	static const TSet<FString> IgnoredTokens = {
		TEXT("p"), TEXT("fb"), TEXT("m"), TEXT("e"), TEXT("a"),
		TEXT("cloth"), TEXT("material"), TEXT("new"), TEXT("9000"), TEXT("9510"), TEXT("00")
	};

	FString HaystackClean = Haystack;
	HaystackClean.ReplaceInline(TEXT("-"), TEXT("_"));
	HaystackClean.ReplaceInline(TEXT("[a]"), TEXT(""));

	for (const FString& Token : TexTokens)
	{
		if (Token.Len() < 2 || IgnoredTokens.Contains(Token)) continue;

		if (HaystackClean.Contains(Token))
		{
			Score += 20;
		}
		else
		{
			// 部分匹配
			TArray<FString> HayTokens;
			HaystackClean.ParseIntoArray(HayTokens, TEXT("_"), true);
			for (const FString& HT : HayTokens)
			{
				if (Token.Len() >= 3 && HT.Len() >= 3)
				{
					if (Token.StartsWith(HT) || HT.StartsWith(Token))
					{
						Score += 10;
						break;
					}
				}
			}
		}
	}

	// FC face/hair type-match bonus
	if (PartLower.StartsWith(TEXT("fc_")))
	{
		static const TPair<const TCHAR*, const TCHAR*> Synonyms[] = {
			{ TEXT("hair"),  TEXT("fur") },
			{ TEXT("hair2"), TEXT("fur") },
			{ TEXT("head"),  TEXT("head") },
			{ TEXT("head"),  TEXT("face") },
			{ TEXT("head"),  TEXT("skin") },
			{ TEXT("beard"), TEXT("beard") },
			{ TEXT("beard"), TEXT("decal") },
			{ TEXT("eye"),   TEXT("eye") },
			{ TEXT("skin"),  TEXT("skin") },
		};
		for (const auto& Syn : Synonyms)
		{
			if (TexCore.Contains(Syn.Key) && MtdCore.Contains(Syn.Value))
			{
				Score += 60;
				break;
			}
		}
	}

	return {Score, Suffix};
}

/// 构建纹理缓存：AssetRegistry → stem→path映射
static TMap<FString, FString> BuildTextureCache(const TArray<FString>& SearchPaths)
{
	TMap<FString, FString> Cache;

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	for (const FString& Path : SearchPaths)
	{
		Filter.PackagePaths.Add(FName(*Path));
	}

	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssets(Filter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		FString Stem = Asset.AssetName.ToString().ToLower();
		FString Path = Asset.GetObjectPathString();
		if (!Cache.Contains(Stem))
		{
			Cache.Add(Stem, Path);
		}
		else if (Path.EndsWith(TEXT(".png")))
		{
			Cache[Stem] = Path; // PNG覆盖DDS
		}
	}

	UE_LOG(LogSekiroImport, Log, TEXT("纹理缓存: %d 个纹理 (%d 搜索路径)"), Cache.Num(), SearchPaths.Num());
	return Cache;
}

/// 3-pass纹理分配（对齐 Blender assign_textures_globally）
/// @param Materials 材质列表
/// @param TextureCache stem→asset_path 映射
/// @param MaterialParts 每个材质的Part名 (从MeshSection推导)
/// @return 每材质 suffix→asset_path 映射
static TArray<TMap<FString, FString>> AssignTexturesGlobally(
	const TArray<FSekiroImportMaterial>& Materials,
	const TMap<FString, FString>& TextureCache,
	const TArray<FString>& MaterialParts)
{
	TArray<TMap<FString, FString>> Result;
	Result.SetNum(Materials.Num());

	TSet<TPair<int32, FString>> FilledSlots; // (MatIdx, suffix)

	// ---- Pass 0: ParamName-based matching from FLVER texture metadata ----
	for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
	{
		const FSekiroImportMaterial& Mat = Materials[Mi];
		const FString PartPrefix = MaterialParts.IsValidIndex(Mi) ?
			MaterialParts[Mi].ToLower() + TEXT("_") : TEXT("");

		// 0a: FLVER texture slots (ParamName → path)
		for (const auto& Slot : Mat.TextureSlots)
		{
			FString Suffix = ParamNameToSuffix(Slot.Key);
			if (Suffix.IsEmpty()) continue;

			TPair<int32, FString> SlotKey(Mi, Suffix);
			if (FilledSlots.Contains(SlotKey)) continue;

			FString TexStem = FPaths::GetBaseFilename(Slot.Value).ToLower();
			if (const FString* Found = TextureCache.Find(TexStem))
			{
				Result[Mi].Add(Suffix, *Found);
				FilledSlots.Add(SlotKey);
			}
		}

		// 0b: AvailableTextures (TPF filename list)
		for (const FString& TexFn : Mat.AvailableTextures)
		{
			FString Stem = FPaths::GetBaseFilename(TexFn).ToLower();
			auto [Core, Suffix] = StripTextureSuffix(Stem);
			if (Suffix.IsEmpty()) continue;

			TPair<int32, FString> SlotKey(Mi, Suffix);
			if (FilledSlots.Contains(SlotKey)) continue;
			if (!PartPrefix.IsEmpty() && !Stem.StartsWith(PartPrefix)) continue;

			if (const FString* Found = TextureCache.Find(Stem))
			{
				Result[Mi].Add(Suffix, *Found);
				FilledSlots.Add(SlotKey);
			}
		}
	}

	// ---- Pass 1: Score-based greedy assignment ----
	struct FCandidate { int32 Score; int32 MatIdx; FString Suffix; FString Path; };
	TArray<FCandidate> Candidates;

	for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
	{
		const FString PartName = MaterialParts.IsValidIndex(Mi) ? MaterialParts[Mi] : TEXT("");
		for (const auto& CacheEntry : TextureCache)
		{
			auto [Score, Suffix] = ScoreTextureCandidate(
				PartName, Materials[Mi].Name, Materials[Mi].MTDPath, CacheEntry.Key);
			if (Score > 0)
			{
				Candidates.Add({Score, Mi, Suffix, CacheEntry.Value});
			}
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });

	for (const FCandidate& C : Candidates)
	{
		TPair<int32, FString> SlotKey(C.MatIdx, C.Suffix);
		if (FilledSlots.Contains(SlotKey)) continue;
		Result[C.MatIdx].Add(C.Suffix, C.Path);
		FilledSlots.Add(SlotKey);
	}

	// ---- Pass 2: Fallback — same part any textures ----
	TMap<FString, TArray<TPair<FString, FString>>> PartTextures;
	for (const auto& CacheEntry : TextureCache)
	{
		auto [Core, Suffix] = StripTextureSuffix(CacheEntry.Key);
		if (Suffix.IsEmpty()) continue;

		for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
		{
			FString P = MaterialParts.IsValidIndex(Mi) ? MaterialParts[Mi].ToLower() : TEXT("");
			if (!P.IsEmpty() && CacheEntry.Key.StartsWith(P + TEXT("_")))
			{
				PartTextures.FindOrAdd(P).Add({Suffix, CacheEntry.Value});
				break;
			}
		}
	}

	for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
	{
		if (Result[Mi].Num() > 0) continue;

		FString P = MaterialParts.IsValidIndex(Mi) ? MaterialParts[Mi].ToLower() : TEXT("");
		if (P.IsEmpty() || !PartTextures.Contains(P)) continue;

		const TArray<TPair<FString, FString>>& Available = PartTextures[P];
		for (const TCHAR* Preferred : { TEXT("_a"), TEXT("_n"), TEXT("_m") })
		{
			if (FilledSlots.Contains({Mi, Preferred})) continue;
			for (const auto& Tex : Available)
			{
				if (Tex.Key == Preferred)
				{
					Result[Mi].Add(Preferred, Tex.Value);
					FilledSlots.Add({Mi, Preferred});
					break;
				}
			}
		}
	}

	// Decal cross-part override: non-FC face decal materials → FC_* face textures
	{
		TArray<TPair<FString, FString>> FaceCache;
		for (const auto& CacheEntry : TextureCache)
		{
			auto [Core, Suffix] = StripTextureSuffix(CacheEntry.Key);
			if (!Suffix.IsEmpty() && CacheEntry.Key.StartsWith(TEXT("fc_")))
			{
				FaceCache.Add({Suffix, CacheEntry.Value});
			}
		}

		if (FaceCache.Num() > 0)
		{
			for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
			{
				const FString MtdBase = FPaths::GetBaseFilename(Materials[Mi].MTDPath).ToLower();
				const FString Part = MaterialParts.IsValidIndex(Mi) ? MaterialParts[Mi].ToLower() : TEXT("");
				if (!MtdBase.Contains(TEXT("decal")) || Part.StartsWith(TEXT("fc_"))) continue;

				// Clear existing & reassign FC textures
				for (const auto& Assigned : Result[Mi])
				{
					FilledSlots.Remove({Mi, Assigned.Key});
				}
				Result[Mi].Empty();

				for (const auto& Tex : FaceCache)
				{
					TPair<int32, FString> SlotKey(Mi, Tex.Key);
					if (!FilledSlots.Contains(SlotKey))
					{
						Result[Mi].Add(Tex.Key, Tex.Value);
						FilledSlots.Add(SlotKey);
					}
				}
			}
		}
	}

	// Stats
	int32 AssignedAny = 0, AssignedAlbedo = 0, AssignedNormal = 0;
	for (int32 Mi = 0; Mi < Materials.Num(); ++Mi)
	{
		if (Result[Mi].Num() > 0) ++AssignedAny;
		if (Result[Mi].Contains(TEXT("_a"))) ++AssignedAlbedo;
		if (Result[Mi].Contains(TEXT("_n"))) ++AssignedNormal;
	}
	UE_LOG(LogSekiroImport, Log, TEXT("纹理匹配: %d/%d 材质 (Albedo=%d, Normal=%d)"),
		AssignedAny, Materials.Num(), AssignedAlbedo, AssignedNormal);

	return Result;
}

// ============================================================================
// 父材质管理
// ============================================================================

/// 确保 M_SekiroBase 父材质存在（首次创建，后续复用）
/// 对齐 Blender create_materials: _a→BaseColor, _n→Normal(LinearColor), _m→Metallic, _r→Roughness
/// Alpha同时连接Opacity和OpacityMask，MIC根据BlendMode使用对应通道
static UMaterial* EnsureBaseMaterial(const FString& ParentPackagePath)
{
    // 删除旧父材质（避免复用有错误的旧版本）
    FString FullPath = ParentPackagePath + TEXT(".M_SekiroBase");
    {
        FString FilePath = FPackageName::LongPackageNameToFilename(
            ParentPackagePath, FPackageName::GetAssetPackageExtension());
        if (FPaths::FileExists(FilePath))
        {
            IFileManager::Get().Delete(*FilePath);
        }
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
    // ShadingModel 默认 MSM_DefaultLit，无需显式设置

    // 创建4个纹理参数节点，采样类型对齐Sekiro贴图格式
    // Sekiro FLVER贴图: _a=BC1/DXT1(sRGB), _n=BC1/DXT1(DeriveNormalZ), _m/_r=单通道
    struct FParamDef { const TCHAR* Name; EMaterialProperty Property; int32 X; int32 Y; TEnumAsByte<EMaterialSamplerType> Sampler; };
    const FParamDef Params[] = {
        { TEXT("_a"), MP_BaseColor,  -600,  200, SAMPLERTYPE_Color },
        { TEXT("_n"), MP_Normal,     -600, -100, SAMPLERTYPE_Normal },
        { TEXT("_m"), MP_Metallic,   -600, -400, SAMPLERTYPE_LinearGrayscale },
        { TEXT("_r"), MP_Roughness,  -600, -700, SAMPLERTYPE_LinearGrayscale },
    };

    UMaterialExpressionTextureSampleParameter2D* AlphaNode = nullptr;

    for (const FParamDef& P : Params)
    {
        UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
            Mat, UMaterialExpressionTextureSampleParameter2D::StaticClass(), P.X, P.Y);
        UMaterialExpressionTextureSampleParameter2D* TexNode =
            Cast<UMaterialExpressionTextureSampleParameter2D>(Expr);
        if (!TexNode)
        {
            UE_LOG(LogSekiroImport, Error, TEXT("无法创建纹理参数节点: %s"), P.Name);
            continue;
        }
        TexNode->ParameterName = P.Name;
        TexNode->SamplerType = P.Sampler;

        bool bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(
            TexNode, TEXT(""), P.Property);
        if (!bConnected)
        {
            UE_LOG(LogSekiroImport, Warning, TEXT("纹理参数连接失败: %s → %d"), P.Name, (int32)P.Property);
        }

        if (FCString::Strcmp(P.Name, TEXT("_a")) == 0)
        {
            AlphaNode = TexNode;
        }
    }

    // Alpha同时连接Opacity和OpacityMask
    // 必须连 Alpha 通道(TEXT("A"))而非默认RGB，因为Opacity是Scalar类型
    if (AlphaNode)
    {
        UMaterialEditingLibrary::ConnectMaterialProperty(AlphaNode, TEXT("A"), MP_Opacity);
        UMaterialEditingLibrary::ConnectMaterialProperty(AlphaNode, TEXT("A"), MP_OpacityMask);
    }

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
// 独立材质创建（每个材质槽一个完整UMaterial，非MIC实例）
// ============================================================================

static UMaterial* BuildSingleMaterial(
    const FSekiroImportMaterial& Material,
    const FString& PackagePath,
    const TSharedPtr<FJsonObject>* JsonEntry,
    const TMap<FString, FString>& AssignedTextures,
    const TMap<FString, FString>& TextureCache)
{
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackagePath);
    if (!Package) return nullptr;

    FString SafeName = SanitizeMaterialName(Material.Name);
    FString AssetName = FString::Printf(TEXT("M_%s"), *SafeName);
    UMaterial* Mat = NewObject<UMaterial>(
        Package, UMaterial::StaticClass(), FName(*AssetName),
        RF_Public | RF_Standalone);
    if (!Mat) return nullptr;

    Mat->bUsedWithSkeletalMesh = true;

    FString BlendModeStr = DeriveBlendMode(Material.BlendMode, Material.Name, Material.MTDPath, JsonEntry);
    bool bTwoSided = DeriveTwoSided(Material.Name, Material.MTDPath);

    if (BlendModeStr.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase))
    {
        Mat->BlendMode = BLEND_Translucent;
    }
    else if (BlendModeStr.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
    {
        Mat->BlendMode = BLEND_Masked;
        Mat->OpacityMaskClipValue = 0.5f;
    }
    else
    {
        Mat->BlendMode = BLEND_Opaque;
    }
    Mat->TwoSided = bTwoSided;

    // 纹理合并: JSON优先 > C++评分
    TMap<FString, UTexture*> MergedTextures;

    if (JsonEntry)
    {
        const TSharedPtr<FJsonObject>* TexObj = nullptr;
        if ((*JsonEntry)->TryGetObjectField(TEXT("textures"), TexObj))
        {
            for (const auto& Pair : (*TexObj)->Values)
            {
                FString TexStem = TextureNameFromPath(Pair.Value->AsString()).ToLower();
                if (const FString* CachedPath = TextureCache.Find(TexStem))
                {
                    if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, **CachedPath))
                        MergedTextures.Add(Pair.Key, Tex);
                }
            }
        }
    }

    for (const auto& Pair : AssignedTextures)
    {
        if (!MergedTextures.Contains(Pair.Key))
        {
            if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Pair.Value))
                MergedTextures.Add(Pair.Key, Tex);
        }
    }

    // 纹理采样节点 → 材质属性
    // 所有SamplerType设为Color: 导入的PNG纹理均为默认sRGB压缩, 不手动覆盖
    struct FParamDef { const TCHAR* Suffix; EMaterialProperty Property; int32 X; int32 Y; };
    const FParamDef Params[] = {
        { TEXT("_a"), MP_BaseColor,  -600,  200 },
        { TEXT("_n"), MP_Normal,     -600, -100 },
        { TEXT("_m"), MP_Metallic,   -600, -400 },
        { TEXT("_r"), MP_Roughness,  -600, -700 },
    };

    UMaterialExpressionTextureSample* AlphaNode = nullptr;

    for (const FParamDef& P : Params)
    {
        UTexture** Found = MergedTextures.Find(P.Suffix);
        if (!Found || !*Found) continue; // 无纹理则跳过此节点

        UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
            Mat, UMaterialExpressionTextureSample::StaticClass(), P.X, P.Y);
        UMaterialExpressionTextureSample* TexNode = Cast<UMaterialExpressionTextureSample>(Expr);
        if (!TexNode) continue;

        // 不设SamplerType: 引擎从纹理导入时的压缩设置自动推断
        TexNode->Texture = *Found;
        UMaterialEditingLibrary::ConnectMaterialProperty(TexNode, TEXT(""), P.Property);

        if (FCString::Strcmp(P.Suffix, TEXT("_a")) == 0)
            AlphaNode = TexNode;
    }

    if (AlphaNode)
    {
        UMaterialEditingLibrary::ConnectMaterialProperty(AlphaNode, TEXT("A"), MP_Opacity);
        UMaterialEditingLibrary::ConnectMaterialProperty(AlphaNode, TEXT("A"), MP_OpacityMask);
    }

    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    FString PackageFileName = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    UPackage::SavePackage(Package, Mat, *PackageFileName, SaveArgs);

    int32 TexCount = 0;
    for (const auto& P : MergedTextures) if (P.Value) ++TexCount;
    UE_LOG(LogSekiroImport, Log, TEXT("材质 '%s': %s, TwoSided=%d, %d 纹理"),
        *AssetName, *BlendModeStr, bTwoSided, TexCount);

    return Mat;
}

// ============================================================================
// 公共API
// ============================================================================

TArray<UMaterial*> FSekiroMaterialBuilder::BuildAll(
    const FSekiroModelData& ModelData, USkeletalMesh* SkeletalMesh, const FString& BasePath)
{
    TArray<UMaterial*> Results;

    if (ModelData.Materials.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("模型数据中没有材质信息"));
        return Results;
    }

    // 1. 从MeshSection推导每个材质的Part名
    //    一个Material可能被多个Part共用，取第一个
    TArray<FString> MaterialParts;
    MaterialParts.SetNum(ModelData.Materials.Num());
    for (const FSekiroImportMeshSection& Sec : ModelData.Meshes)
    {
        if (Sec.MaterialIndex >= 0 && Sec.MaterialIndex < MaterialParts.Num())
        {
            if (MaterialParts[Sec.MaterialIndex].IsEmpty())
            {
                MaterialParts[Sec.MaterialIndex] = Sec.PartName;
            }
        }
    }

    // 2. 构建纹理缓存（从AssetRegistry）
    FString SekiroBase = FPaths::GetPath(BasePath); // /Game/Characters/Sekiro
    TArray<FString> TextureSearchPaths;
    TextureSearchPaths.Add(SekiroBase / TEXT("Textures"));
    TextureSearchPaths.Add(TEXT("/Game/Textures")); // 全局回落

    TMap<FString, FString> TextureCache = BuildTextureCache(TextureSearchPaths);

    // 3. 3-pass纹理评分匹配
    TArray<TMap<FString, FString>> AssignedAll = AssignTexturesGlobally(
        ModelData.Materials, TextureCache, MaterialParts);

    // 4. 加载 Sekiro_Materials.json（用于BlendMode/MTDInfo补充，纹理仅作回落）
    TMap<FString, TSharedPtr<FJsonObject>> MatJsonMap = LoadMaterialsJson();

    // 5. 创建独立材质
    UE_LOG(LogSekiroImport, Log, TEXT("开始创建 %d 个独立材质 -> %s/"), ModelData.Materials.Num(), *BasePath);

    for (int32 i = 0; i < ModelData.Materials.Num(); ++i)
    {
        const FSekiroImportMaterial& Mat = ModelData.Materials[i];
        FString SafeName = SanitizeMaterialName(Mat.Name);
        FString MatPackagePath = FString::Printf(TEXT("%s/M_%s"), *BasePath, *SafeName);

        const TSharedPtr<FJsonObject>* JsonEntry = FuzzyFindJsonEntry(MatJsonMap, Mat.Name);

        UMaterial* Material = BuildSingleMaterial(
            Mat, MatPackagePath, JsonEntry, AssignedAll[i], TextureCache);
        Results.Add(Material);

        // 分配到骨骼网格体
        if (Material && SkeletalMesh && i < SkeletalMesh->GetMaterials().Num())
        {
            SkeletalMesh->GetMaterials()[i].MaterialInterface = Material;
        }
    }

    if (SkeletalMesh)
    {
        SkeletalMesh->MarkPackageDirty();
        SkeletalMesh->PostEditChange();

        // 重新保存SkeletalMesh，确保MaterialInterface持久化到磁盘
        UPackage* MeshPackage = SkeletalMesh->GetOutermost();
        FString MeshFileName = FPackageName::LongPackageNameToFilename(
            MeshPackage->GetName(), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs MeshSaveArgs;
        MeshSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        MeshSaveArgs.Error = GWarn;
        if (UPackage::SavePackage(MeshPackage, SkeletalMesh, *MeshFileName, MeshSaveArgs))
        {
            UE_LOG(LogSekiroImport, Log, TEXT("SkeletalMesh重新保存(含材质分配): %s"), *MeshFileName);
        }
        else
        {
            UE_LOG(LogSekiroImport, Error, TEXT("SkeletalMesh重新保存失败: %s"), *MeshFileName);
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("材质创建完成: %d 个独立材质"), Results.Num());
    return Results;
}
