#include "SekiroMaterialBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "Import/SKTextureResolver.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"

/// 清理材质名中的非法字符（UE路径不允许 # 空格等）
static FString SanitizeMaterialName(const FString& RawName)
{
    FString Clean = RawName;
    // 替换UE Package路径中的非法字符
    for (int32 i = 0; i < Clean.Len(); ++i)
    {
        TCHAR C = Clean[i];
        if (C == '#' || C == ' ' || C == '/' || C == '\\' || C == ':' ||
            C == '*' || C == '?' || C == '"' || C == '<' || C == '>' || C == '|' || C == '.')
        {
            Clean[i] = '_';
        }
    }
    // 去除首尾下划线
    Clean.TrimStartAndEndInline();
    while (Clean.StartsWith(TEXT("_"))) Clean.RightChopInline(1);
    while (Clean.EndsWith(TEXT("_"))) Clean.LeftChopInline(1);
    if (Clean.IsEmpty()) Clean = TEXT("Unknown");
    return Clean;
}

/// 从文件名提取无扩展名的纯净名（如 "BD_M_9000_tops_a.png" -> "BD_M_9000_tops_a"）
static FString TextureNameFromPath(const FString& Path)
{
    FString Name = FPaths::GetCleanFilename(Path);
    // 去除扩展名
    int32 DotIdx;
    if (Name.FindLastChar('.', DotIdx))
    {
        Name.LeftInline(DotIdx);
    }
    return Name;
}

UMaterialInstanceConstant* FSekiroMaterialBuilder::CreateMaterialInstance(const FSekiroImportMaterial& Material, const FString& PackagePath)
{
    // 创建Package（路径已在BuildAll中清理过）
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackagePath);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建材质Package: %s"), *PackagePath);
        return nullptr;
    }

    // 创建UMaterialInstanceConstant（名称已在BuildAll中清理过）
    FString SafeName = SanitizeMaterialName(Material.Name);
    FString AssetName = FString::Printf(TEXT("MI_%s"), *SafeName);
    UMaterialInstanceConstant* MatInst = NewObject<UMaterialInstanceConstant>(Package, UMaterialInstanceConstant::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    if (!MatInst)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建材质实例: %s"), *AssetName);
        return nullptr;
    }

    // 设置父材质为默认Surface材质
    UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface);
    if (DefaultMat)
    {
        MatInst->SetParentEditorOnly(DefaultMat);
    }

    // 根据MTD BlendMode设置混合模式相关的材质参数
    if (Material.BlendMode.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase) ||
        Material.BlendMode.Equals(TEXT("Transparent"), ESearchCase::IgnoreCase))
    {
        MatInst->BasePropertyOverrides.bOverride_BlendMode = true;
        MatInst->BasePropertyOverrides.BlendMode = BLEND_Translucent;
    }
    else if (Material.BlendMode.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
    {
        MatInst->BasePropertyOverrides.bOverride_BlendMode = true;
        MatInst->BasePropertyOverrides.BlendMode = BLEND_Masked;
    }

    // 贴图查找路径
    TArray<FString> SearchPaths;
    SearchPaths.Add(TEXT("/Game/Characters/Sekiro/Textures"));

    // 从TextureSlots（ParamName→Path）分配贴图参数
    int32 TexAssigned = 0;
    for (const auto& Slot : Material.TextureSlots)
    {
        const FString& ParamName = Slot.Key;
        EMaterialProperty Prop = FSKTextureResolver::PropertyForParam(ParamName);
        if (Prop == MP_MAX) continue;

        // 尝试通过参数名查找贴图
        UTexture2D* Tex = FSKTextureResolver::FindTexture(ParamName, SearchPaths);
        if (!Tex && !Slot.Value.IsEmpty())
        {
            // 回退：通过Path查找
            FString TexName = TextureNameFromPath(Slot.Value);
            Tex = FSKTextureResolver::FindTexture(TexName, SearchPaths);
        }

        if (Tex)
        {
            FName ParamFName(*ParamName);
            MatInst->SetTextureParameterValueEditorOnly(ParamFName, Tex);
            ++TexAssigned;

            UE_LOG(LogSekiroImport, Verbose, TEXT("    [%s] %s -> %s"),
                *SafeName, *ParamName, *Tex->GetName());
        }
    }

    // 从AvailableTextures补充未分配的参数
    for (const FString& TexPath : Material.AvailableTextures)
    {
        FString TexName = TextureNameFromPath(TexPath);
        UTexture2D* Tex = FSKTextureResolver::FindTexture(TexName, SearchPaths);
        if (Tex)
        {
            // 尝试根据贴图名推断参数类型
            EMaterialProperty Prop = FSKTextureResolver::PropertyForParam(TexName);
            if (Prop == MP_MAX) continue;

            FName ParamFName;
            switch (Prop)
            {
            case MP_BaseColor:        ParamFName = TEXT("AlbedoMap"); break;
            case MP_Normal:           ParamFName = TEXT("NormalMap"); break;
            case MP_Metallic:         ParamFName = TEXT("MetallicMap"); break;
            case MP_Specular:         ParamFName = TEXT("SpecularMap"); break;
            case MP_Roughness:        ParamFName = TEXT("RoughnessMap"); break;
            case MP_AmbientOcclusion: ParamFName = TEXT("AOMap"); break;
            case MP_EmissiveColor:    ParamFName = TEXT("EmissiveMap"); break;
            default: continue;
            }

            MatInst->SetTextureParameterValueEditorOnly(ParamFName, Tex);
            ++TexAssigned;

            UE_LOG(LogSekiroImport, Verbose, TEXT("    [%s] %s -> %s (推断)"),
                *SafeName, *ParamFName.ToString(), *Tex->GetName());
        }
    }

    MatInst->MarkPackageDirty();

    // 保存
    FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    UPackage::SavePackage(Package, MatInst, *PackageFileName, SaveArgs);

    UE_LOG(LogSekiroImport, Log, TEXT("材质 '%s': %d 贴图参数已分配 (共 %d 纹理槽)"),
        *SafeName, TexAssigned, Material.TextureSlots.Num() + Material.AvailableTextures.Num());

    return MatInst;
}

TArray<UMaterialInstanceConstant*> FSekiroMaterialBuilder::BuildAll(const FSekiroModelData& ModelData, USkeletalMesh* SkeletalMesh, const FString& BasePath)
{
    TArray<UMaterialInstanceConstant*> Results;

    if (ModelData.Materials.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("模型数据中没有材质信息"));
        return Results;
    }

    UE_LOG(LogSekiroImport, Log, TEXT("开始创建 %d 个材质实例 -> %s/"), ModelData.Materials.Num(), *BasePath);

    for (int32 i = 0; i < ModelData.Materials.Num(); ++i)
    {
        const FSekiroImportMaterial& Mat = ModelData.Materials[i];
        FString SafeName = SanitizeMaterialName(Mat.Name);
        FString MatPackagePath = FString::Printf(TEXT("%s/MI_%s"), *BasePath, *SafeName);

        UMaterialInstanceConstant* MatInst = CreateMaterialInstance(Mat, MatPackagePath);
        Results.Add(MatInst);

        if (MatInst && SkeletalMesh && i < SkeletalMesh->GetMaterials().Num())
        {
            SkeletalMesh->GetMaterials()[i].MaterialInterface = MatInst;
        }

        UE_LOG(LogSekiroImport, Verbose, TEXT("  材质 [%d]: %s (BlendMode=%s, %d 纹理槽)"),
            i, *Mat.Name, *Mat.BlendMode, Mat.TextureSlots.Num());
    }

    // 确保骨骼网格体的材质槽已更新
    if (SkeletalMesh)
    {
        SkeletalMesh->MarkPackageDirty();
    }

    UE_LOG(LogSekiroImport, Log, TEXT("材质创建完成: %d 个材质实例"), Results.Num());
    return Results;
}
