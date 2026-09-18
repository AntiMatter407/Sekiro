#include "SAModelImporter.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "SAMaterialImporter.h"
#include "Misc/FeedbackContext.h"
#include "SkinnedAssetCompiler.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Misc/AutomationTest.h"
#include "Materials/MaterialInterface.h"

// ============================================================================
// 骨骼解析
// ============================================================================

void SAModelImporter::ParseBones(const TArray<TSharedPtr<FJsonValue>>& BonesArray,
                                  TArray<FSAImportBone>& OutBones)
{
    OutBones.Reserve(BonesArray.Num());
    for (const auto& BoneVal : BonesArray)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (!BoneVal->TryGetObject(Obj)) continue;

        FSAImportBone Bone;
        Bone.Name = FName(*(*Obj)->GetStringField(TEXT("Name")));

        FString ParentName;
        if ((*Obj)->TryGetStringField(TEXT("ParentName"), ParentName) && !ParentName.IsEmpty())
            Bone.ParentName = FName(*ParentName);

        FVector WorldPos = ParseVector3((*Obj)->GetArrayField(TEXT("WorldPos")));
        FQuat WorldRot = ParseQuat((*Obj)->GetArrayField(TEXT("WorldRot")));
        FVector WorldScale = ParseVector3((*Obj)->GetArrayField(TEXT("WorldScale")));

        Bone.WorldTranslation = SekiroToUnreal(WorldPos);
        Bone.WorldRotation = SekiroToUnreal(WorldRot);
        Bone.WorldScale = WorldScale;

        // Read ParentIndex from JSON (Python now outputs it directly)
        if ((*Obj)->HasField(TEXT("ParentIndex")))
            Bone.ParentIndex = (int32)(*Obj)->GetNumberField(TEXT("ParentIndex"));

        // DSAnimStudio-style Nub flag
        if ((*Obj)->HasField(TEXT("IsNub")))
            Bone.bIsNub = (*Obj)->GetBoolField(TEXT("IsNub"));
        if ((*Obj)->HasField(TEXT("Flags")))
            Bone.Flags = (int32)(*Obj)->GetNumberField(TEXT("Flags"));

        OutBones.Add(MoveTemp(Bone));
    }

    TMap<FName, int32> NameToIndex;
    for (int32 i = 0; i < OutBones.Num(); ++i)
        NameToIndex.Add(OutBones[i].Name, i);

    // DSAnimStudio: prefer JSON ParentIndex, fall back to Name lookup
    for (auto& Bone : OutBones)
    {
        if (Bone.ParentIndex == INDEX_NONE && !Bone.ParentName.IsNone())
        {
            if (int32* Idx = NameToIndex.Find(Bone.ParentName))
                Bone.ParentIndex = *Idx;
        }
    }

    for (int32 i = 0; i < OutBones.Num(); ++i)
    {
        auto& Bone = OutBones[i];
        if (Bone.ParentIndex != INDEX_NONE)
        {
            const auto& Parent = OutBones[Bone.ParentIndex];
            FTransform ParentWorld; ParentWorld.SetRotation(Parent.WorldRotation); ParentWorld.SetTranslation(Parent.WorldTranslation); ParentWorld.SetScale3D(Parent.WorldScale);
            FTransform World; World.SetRotation(Bone.WorldRotation); World.SetTranslation(Bone.WorldTranslation); World.SetScale3D(Bone.WorldScale);
            FTransform Local = World.GetRelativeTransform(ParentWorld);
            Bone.LocalRotation = Local.GetRotation(); Bone.LocalTranslation = Local.GetTranslation(); Bone.LocalScale = Local.GetScale3D();
        }
        else
        {
            Bone.LocalRotation = Bone.WorldRotation; Bone.LocalTranslation = Bone.WorldTranslation; Bone.LocalScale = Bone.WorldScale;
        }
    }
}

/**
 * 解析模型 JSON 中可选的 AuxiliaryBones 数组。每项必须声明 Name 与 ParentName，变换字段使用
 * Translation/Rotation/Scale（同时兼容 LocalTranslation/LocalRotation/LocalScale）并直接表示 UE 局部空间：
 * 位移单位为厘米，旋转为 xyzw 四元数，缩放无单位。本函数只校验 JSON 字段形状，不解析父级索引，也不修改主骨架。
 * 只能在模型导入所在的编辑器线程调用。
 *
 * @param BonesArray JSON 的 AuxiliaryBones 数组，调用期间只读且不保留引用。
 * @param OutBones 成功时追加解析结果；任一声明非法时清空本次已追加内容并返回 false。
 * @return 所有声明均合法时返回 true；对象、名称、父级或变换数组格式非法时返回 false。
 */
bool SAModelImporter::ParseAuxiliaryBones(
    const TArray<TSharedPtr<FJsonValue>>& BonesArray,
    TArray<FSAImportBone>& OutBones)
{
    const int32 InitialBoneCount = OutBones.Num();
    OutBones.Reserve(InitialBoneCount + BonesArray.Num());

    for (int32 BoneIndex = 0; BoneIndex < BonesArray.Num(); ++BoneIndex)
    {
        const TSharedPtr<FJsonObject>* BoneObject = nullptr;
        if (!BonesArray[BoneIndex]->TryGetObject(BoneObject) || !BoneObject || !BoneObject->IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones[%d] must be a JSON object"), BoneIndex);
            OutBones.SetNum(InitialBoneCount);
            return false;
        }

        FString BoneName;
        FString ParentName;
        if (!(*BoneObject)->TryGetStringField(TEXT("Name"), BoneName) || BoneName.IsEmpty()
            || !(*BoneObject)->TryGetStringField(TEXT("ParentName"), ParentName) || ParentName.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones[%d] requires non-empty Name and ParentName"), BoneIndex);
            OutBones.SetNum(InitialBoneCount);
            return false;
        }

        FSAImportBone Bone;
        Bone.Name = FName(*BoneName);
        Bone.ParentName = FName(*ParentName);

        const TArray<TSharedPtr<FJsonValue>>* TranslationArray = nullptr;
        const bool bHasTranslation = (*BoneObject)->HasField(TEXT("Translation"));
        const bool bHasLocalTranslation = (*BoneObject)->HasField(TEXT("LocalTranslation"));
        if (bHasTranslation)
            (*BoneObject)->TryGetArrayField(TEXT("Translation"), TranslationArray);
        else if (bHasLocalTranslation)
            (*BoneObject)->TryGetArrayField(TEXT("LocalTranslation"), TranslationArray);
        if ((bHasTranslation || bHasLocalTranslation) && (!TranslationArray || TranslationArray->Num() < 3))
        {
            UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones[%d] Translation must contain three numbers"), BoneIndex);
            OutBones.SetNum(InitialBoneCount);
            return false;
        }
        if (TranslationArray)
            Bone.LocalTranslation = ParseVector3(*TranslationArray);

        const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
        const bool bHasRotation = (*BoneObject)->HasField(TEXT("Rotation"));
        const bool bHasLocalRotation = (*BoneObject)->HasField(TEXT("LocalRotation"));
        if (bHasRotation)
            (*BoneObject)->TryGetArrayField(TEXT("Rotation"), RotationArray);
        else if (bHasLocalRotation)
            (*BoneObject)->TryGetArrayField(TEXT("LocalRotation"), RotationArray);
        if ((bHasRotation || bHasLocalRotation) && (!RotationArray || RotationArray->Num() < 4))
        {
            UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones[%d] Rotation must contain four xyzw numbers"), BoneIndex);
            OutBones.SetNum(InitialBoneCount);
            return false;
        }
        if (RotationArray)
            Bone.LocalRotation = ParseQuat(*RotationArray);

        const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
        const bool bHasScale = (*BoneObject)->HasField(TEXT("Scale"));
        const bool bHasLocalScale = (*BoneObject)->HasField(TEXT("LocalScale"));
        if (bHasScale)
            (*BoneObject)->TryGetArrayField(TEXT("Scale"), ScaleArray);
        else if (bHasLocalScale)
            (*BoneObject)->TryGetArrayField(TEXT("LocalScale"), ScaleArray);
        if ((bHasScale || bHasLocalScale) && (!ScaleArray || ScaleArray->Num() < 3))
        {
            UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones[%d] Scale must contain three numbers"), BoneIndex);
            OutBones.SetNum(InitialBoneCount);
            return false;
        }
        if (ScaleArray)
            Bone.LocalScale = ParseVector3(*ScaleArray);

        OutBones.Add(MoveTemp(Bone));
    }

    return true;
}

// ============================================================================
// 材质解析
// ============================================================================

void SAModelImporter::ParseMaterials(
    const TArray<TSharedPtr<FJsonValue>>& MatsArray,
    const TArray<TSharedPtr<FJsonValue>>* ResolvedMatsArray,
    TArray<FSAImportMaterial>& OutMaterials)
{
    OutMaterials.Reserve(MatsArray.Num());
    for (const auto& MatVal : MatsArray)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (!MatVal->TryGetObject(Obj)) continue;

        FSAImportMaterial Mat;
        Mat.Name = (*Obj)->GetStringField(TEXT("Name"));
        (*Obj)->TryGetStringField(TEXT("MTD"), Mat.MTDPath);

        // ShaderType enum from JSON string
        FString ShaderTypeStr;
        if ((*Obj)->TryGetStringField(TEXT("ShaderType"), ShaderTypeStr))
        {
            static TMap<FString, ESekiroShaderType> ShaderTypeMap = {
                { TEXT("Standard"),         ESekiroShaderType::Standard },
                { TEXT("SSS"),              ESekiroShaderType::SSS },
                { TEXT("Fur"),              ESekiroShaderType::Fur },
                { TEXT("DetailBlend"),      ESekiroShaderType::DetailBlend },
                { TEXT("FurCloth"),         ESekiroShaderType::FurCloth },
                { TEXT("DetailBlendCloth"), ESekiroShaderType::DetailBlendCloth },
                { TEXT("FresnelBlend"),     ESekiroShaderType::FresnelBlend },
                { TEXT("FresnelBlendCloth"),ESekiroShaderType::FresnelBlendCloth },
                { TEXT("SSSCloth"),         ESekiroShaderType::SSSCloth },
                { TEXT("Skin"),             ESekiroShaderType::Skin },
                { TEXT("Eye"),              ESekiroShaderType::Eye },
                { TEXT("Cloth"),            ESekiroShaderType::Cloth },
            };
            Mat.ShaderType = ShaderTypeMap.FindRef(ShaderTypeStr);
        }
        (*Obj)->TryGetStringField(TEXT("ShaderPath"), Mat.ShaderPath);
        (*Obj)->TryGetStringField(TEXT("ResolvedBlendMode"), Mat.ResolvedBlendMode);
        (*Obj)->TryGetNumberField(TEXT("ClipValue"), Mat.ClipValue);
        (*Obj)->TryGetBoolField(TEXT("TwoSided"), Mat.bTwoSided);
        (*Obj)->TryGetBoolField(TEXT("IsFur"), Mat.bIsFur);
        (*Obj)->TryGetBoolField(TEXT("IsHair"), Mat.bIsHair);
        (*Obj)->TryGetBoolField(TEXT("IsCloth"), Mat.bIsCloth);
        (*Obj)->TryGetBoolField(TEXT("IsDecal"), Mat.bIsDecal);
        (*Obj)->TryGetStringField(TEXT("DrawStep"), Mat.DrawStep);

        // 直接从 Materials 对象读取 Textures 映射（Python 端已内联）
        const TSharedPtr<FJsonObject>* InlineTexObj = nullptr;
        if ((*Obj)->TryGetObjectField(TEXT("Textures"), InlineTexObj))
            for (const auto& Pair : (*InlineTexObj)->Values)
                Mat.ResolvedTextures.Add(FString(Pair.Key.Len(), *Pair.Key), Pair.Value->AsString());

        OutMaterials.Add(MoveTemp(Mat));
    }

    // 兼容旧 JSON 格式：通过 ResolvedMaterials 找未匹配的纹理
    if (ResolvedMatsArray)
    {
        for (const auto& RmVal : *ResolvedMatsArray)
        {
            const TSharedPtr<FJsonObject>* RmObj = nullptr;
            if (!RmVal->TryGetObject(RmObj)) continue;

            FString RmName = (*RmObj)->GetStringField(TEXT("Name"));
            FSAImportMaterial* Mat = OutMaterials.FindByPredicate(
                [&RmName](const FSAImportMaterial& M) { return M.Name == RmName; });
            if (!Mat) continue;

            // 仅当 Materials 中未内联 Textures 时才从 ResolvedMaterials 读取
            // ResolvedMaterials always override Materials textures (Python resolution wins)
            {
                const TSharedPtr<FJsonObject>* TexObj = nullptr;
                if ((*RmObj)->TryGetObjectField(TEXT("Textures"), TexObj))
                    for (const auto& Pair : (*TexObj)->Values)
                        Mat->ResolvedTextures.Add(FString(Pair.Key.Len(), *Pair.Key), Pair.Value->AsString());
            }

            // ShaderType and ShaderPath from ResolvedMaterials (authoritative)
            FString RmShaderType;
            if ((*RmObj)->TryGetStringField(TEXT("ShaderType"), RmShaderType) && !RmShaderType.IsEmpty())
            {
                static TMap<FString, ESekiroShaderType> RmShaderTypeMap = {
                    { TEXT("Standard"),         ESekiroShaderType::Standard },
                    { TEXT("SSS"),              ESekiroShaderType::SSS },
                    { TEXT("Fur"),              ESekiroShaderType::Fur },
                    { TEXT("DetailBlend"),      ESekiroShaderType::DetailBlend },
                    { TEXT("FurCloth"),         ESekiroShaderType::FurCloth },
                    { TEXT("DetailBlendCloth"), ESekiroShaderType::DetailBlendCloth },
                    { TEXT("FresnelBlend"),     ESekiroShaderType::FresnelBlend },
                    { TEXT("FresnelBlendCloth"),ESekiroShaderType::FresnelBlendCloth },
                    { TEXT("SSSCloth"),         ESekiroShaderType::SSSCloth },
                    { TEXT("Skin"),             ESekiroShaderType::Skin },
                    { TEXT("Eye"),              ESekiroShaderType::Eye },
                    { TEXT("Cloth"),            ESekiroShaderType::Cloth },
                };
                Mat->ShaderType = RmShaderTypeMap.FindRef(RmShaderType);
            }
            (*RmObj)->TryGetStringField(TEXT("ShaderPath"), Mat->ShaderPath);

            FString RmBlend;
            if ((*RmObj)->TryGetStringField(TEXT("ResolvedBlendMode"), RmBlend) && !RmBlend.IsEmpty())
                Mat->ResolvedBlendMode = RmBlend;
            (*RmObj)->TryGetBoolField(TEXT("TwoSided"), Mat->bTwoSided);
            (*RmObj)->TryGetBoolField(TEXT("IsCloth"), Mat->bIsCloth);
            (*RmObj)->TryGetBoolField(TEXT("IsHair"), Mat->bIsHair);
            (*RmObj)->TryGetBoolField(TEXT("IsFur"), Mat->bIsFur);
            (*RmObj)->TryGetBoolField(TEXT("IsDecal"), Mat->bIsDecal);
            (*RmObj)->TryGetStringField(TEXT("DrawStep"), Mat->DrawStep);
        }
    }
}

// ============================================================================
// 顶点解析
// ============================================================================

FSAImportVertex SAModelImporter::ParseVertex(const TSharedPtr<FJsonObject>& VertObj)
{
    FSAImportVertex Vert;
    Vert.Position = SekiroToUnreal(ParseVector3f(VertObj->GetArrayField(TEXT("Pos"))));
    Vert.Normal = SekiroToUnreal(ParseVector3f(VertObj->GetArrayField(TEXT("Normal"))));
    Vert.Normal.Normalize();
    Vert.UV = ParseVector2f(VertObj->GetArrayField(TEXT("UV")));

    const auto& IdxArr = VertObj->GetArrayField(TEXT("BoneIndices"));
    for (int32 i = 0; i < FMath::Min(4, IdxArr.Num()); ++i)
        Vert.BoneIndices[i] = (uint16)IdxArr[i]->AsNumber();

    const auto& WgtArr = VertObj->GetArrayField(TEXT("BoneWeights"));
    float Sum = 0.0f;
    for (int32 i = 0; i < FMath::Min(4, WgtArr.Num()); ++i) { Vert.BoneWeights[i] = (float)WgtArr[i]->AsNumber(); Sum += Vert.BoneWeights[i]; }
    if (Sum > 0.0f && FMath::Abs(Sum - 1.0f) > KINDA_SMALL_NUMBER)
        for (int32 i = 0; i < 4; ++i) Vert.BoneWeights[i] /= Sum;

    return Vert;
}

// ============================================================================
// 网格体解析
// ============================================================================

void SAModelImporter::ParseMeshes(const TArray<TSharedPtr<FJsonValue>>& MeshesArray,
                                   TArray<FSAImportMeshSection>& OutMeshes)
{
    OutMeshes.Reserve(MeshesArray.Num());
    for (const auto& MeshVal : MeshesArray)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (!MeshVal->TryGetObject(Obj)) continue;

        FSAImportMeshSection Section;
        (*Obj)->TryGetStringField(TEXT("Part"), Section.PartName);
        Section.MaterialIndex = (int32)(*Obj)->GetNumberField(TEXT("MaterialIndex"));

        const TSharedPtr<FJsonObject>* BoneMap = nullptr;
        if ((*Obj)->TryGetObjectField(TEXT("BoneIdxToName"), BoneMap))
            for (const auto& Pair : (*BoneMap)->Values)
                Section.BoneIdxToName.Add(FCString::Atoi(*Pair.Key), FName(*Pair.Value->AsString()));

        const TArray<TSharedPtr<FJsonValue>>* Verts = nullptr;
        if ((*Obj)->TryGetArrayField(TEXT("Vertices"), Verts))
        {
            Section.Vertices.Reserve(Verts->Num());
            for (const auto& V : *Verts)
            {
                const TSharedPtr<FJsonObject>* VObj = nullptr;
                if (V->TryGetObject(VObj))
                    Section.Vertices.Add(ParseVertex(*VObj));
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* Tris = nullptr;
        if ((*Obj)->TryGetArrayField(TEXT("Triangles"), Tris))
        {
            Section.Triangles.Reserve(Tris->Num());
            for (const auto& T : *Tris)
            {
                const auto& TriArr = T->AsArray();
                if (TriArr.Num() >= 3)
                    Section.Triangles.Add(FIntVector((int32)TriArr[0]->AsNumber(), (int32)TriArr[1]->AsNumber(), (int32)TriArr[2]->AsNumber()));
            }
        }

        OutMeshes.Add(MoveTemp(Section));
    }
}

// ============================================================================
// JSON 解析
// ============================================================================

bool SAModelImporter::ParseFromFile(const FString& JsonPath, FSAModelData& OutData)
{
    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("无法读取 JSON: %s"), *JsonPath);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("JSON 解析失败: %s"), *JsonPath);
        return false;
    }

	Root->TryGetStringField(TEXT("AssetName"), OutData.AssetName);
	Root->TryGetStringField(TEXT("OriginalAssetName"), OutData.OriginalAssetName);
    Root->TryGetStringField(TEXT("SkeletonName"), OutData.SkeletonName);

    const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("Bones"), BonesArr))
        ParseBones(*BonesArr, OutData.Bones);

    // 解析可选的 FlverBones（用于 ModelOnly 追加的数据源）
    const TArray<TSharedPtr<FJsonValue>>* FlverBonesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("FlverBones"), FlverBonesArr))
        ParseBones(*FlverBonesArr, OutData.FlverBones);

    const TArray<TSharedPtr<FJsonValue>>* AuxiliaryBonesArray = nullptr;
    if (Root->HasField(TEXT("AuxiliaryBones"))
        && (!Root->TryGetArrayField(TEXT("AuxiliaryBones"), AuxiliaryBonesArray)
            || !AuxiliaryBonesArray
            || !ParseAuxiliaryBones(*AuxiliaryBonesArray, OutData.AuxiliaryBones)))
    {
        UE_LOG(LogTemp, Error, TEXT("AuxiliaryBones 解析失败: %s"), *JsonPath);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* MatsArr = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* ResolvedMatsArr = nullptr;
    Root->TryGetArrayField(TEXT("ResolvedMaterials"), ResolvedMatsArr);
    if (Root->TryGetArrayField(TEXT("Materials"), MatsArr))
        ParseMaterials(*MatsArr, ResolvedMatsArr, OutData.Materials);

    const TArray<TSharedPtr<FJsonValue>>* MeshesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("Meshes"), MeshesArr))
        ParseMeshes(*MeshesArr, OutData.Meshes);

    UE_LOG(LogTemp, Log, TEXT("解析完成: %d 骨骼, %d FlverBones, %d AuxiliaryBones, %d 材质, %d 网格"),
        OutData.Bones.Num(), OutData.FlverBones.Num(), OutData.AuxiliaryBones.Num(),
        OutData.Materials.Num(), OutData.Meshes.Num());
    return true;
}

// ============================================================================
// 收集网格引用的骨骼名
// ============================================================================

// ============================================================================
// 追加 ModelOnly 骨骼
// ============================================================================



/**
 * 将已解析的无蒙皮辅助骨骼追加到完成朝向转换与独立 Root 合成后的参考骨架数组。
 * 声明严格按数组顺序解析父级：父级必须是既有骨骼或更早声明的辅助骨骼；函数不会重排声明，
 * 不会修改任何既有骨骼的名称、父级或变换。校验全部通过后才提交追加，因此失败不会留下部分结果。
 * 只能在模型导入所在的编辑器线程调用。
 *
 * @param InOutSkeletonBones 输入为最终主骨架，成功时在尾部追加辅助骨骼；失败时保持原样。
 * @param AuxiliaryBones 使用 UE 局部空间、厘米单位声明的辅助骨骼，只在调用期间读取。
 * @param SkeletonName 仅用于诊断日志的骨架名称，可以为空。
 * @return 全部辅助骨骼成功追加时返回 true；名称、父级或变换非法时返回 false。
 */
static bool AppendAuxiliaryBones(
    TArray<FSAImportBone>& InOutSkeletonBones,
    const TArray<FSAImportBone>& AuxiliaryBones,
    const FString& SkeletonName)
{
    if (AuxiliaryBones.Num() == 0) return true;
    if (InOutSkeletonBones.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Skeleton '%s' cannot append auxiliary bones to an empty reference skeleton"),
            *SkeletonName);
        return false;
    }

    TMap<FName, int32> ResolvedBoneIndices;
    for (int32 BoneIndex = 0; BoneIndex < InOutSkeletonBones.Num(); ++BoneIndex)
    {
        const FName BoneName = InOutSkeletonBones[BoneIndex].Name;
        if (BoneName.IsNone() || ResolvedBoneIndices.Contains(BoneName))
        {
            UE_LOG(LogTemp, Error, TEXT("Skeleton '%s' has an invalid or duplicate existing bone name '%s'"),
                *SkeletonName, *BoneName.ToString());
            return false;
        }
        ResolvedBoneIndices.Add(BoneName, BoneIndex);
    }

    TSet<FName> DeclaredAuxiliaryNames;
    for (int32 AuxiliaryIndex = 0; AuxiliaryIndex < AuxiliaryBones.Num(); ++AuxiliaryIndex)
    {
        const FName BoneName = AuxiliaryBones[AuxiliaryIndex].Name;
        if (BoneName.IsNone() || ResolvedBoneIndices.Contains(BoneName) || DeclaredAuxiliaryNames.Contains(BoneName))
        {
            UE_LOG(LogTemp, Error, TEXT("Skeleton '%s' AuxiliaryBones[%d] has invalid or duplicate name '%s'"),
                *SkeletonName, AuxiliaryIndex, *BoneName.ToString());
            return false;
        }
        DeclaredAuxiliaryNames.Add(BoneName);
    }

    TArray<int32> ResolvedParentIndices;
    ResolvedParentIndices.Reserve(AuxiliaryBones.Num());
    for (int32 AuxiliaryIndex = 0; AuxiliaryIndex < AuxiliaryBones.Num(); ++AuxiliaryIndex)
    {
        const FSAImportBone& Bone = AuxiliaryBones[AuxiliaryIndex];
        const int32* ParentIndex = ResolvedBoneIndices.Find(Bone.ParentName);
        if (!ParentIndex)
        {
            const TCHAR* FailureReason = DeclaredAuxiliaryNames.Contains(Bone.ParentName)
                ? TEXT("is declared at or after its child")
                : TEXT("does not exist");
            UE_LOG(LogTemp, Error,
                TEXT("Skeleton '%s' AuxiliaryBones[%d] parent '%s' %s"),
                *SkeletonName, AuxiliaryIndex, *Bone.ParentName.ToString(), FailureReason);
            return false;
        }

        const bool bInvalidTransform = Bone.LocalTranslation.ContainsNaN()
            || Bone.LocalRotation.ContainsNaN()
            || Bone.LocalRotation.SizeSquared() <= SMALL_NUMBER
            || Bone.LocalScale.ContainsNaN()
            || FMath::IsNearlyZero(Bone.LocalScale.X)
            || FMath::IsNearlyZero(Bone.LocalScale.Y)
            || FMath::IsNearlyZero(Bone.LocalScale.Z);
        if (bInvalidTransform)
        {
            UE_LOG(LogTemp, Error, TEXT("Skeleton '%s' AuxiliaryBones[%d] '%s' has an invalid local transform"),
                *SkeletonName, AuxiliaryIndex, *Bone.Name.ToString());
            return false;
        }

        ResolvedParentIndices.Add(*ParentIndex);
        ResolvedBoneIndices.Add(Bone.Name, InOutSkeletonBones.Num() + AuxiliaryIndex);
    }

    TArray<FTransform> ReferenceComponentPoses;
    ReferenceComponentPoses.SetNum(InOutSkeletonBones.Num() + AuxiliaryBones.Num());
    for (int32 BoneIndex = 0; BoneIndex < InOutSkeletonBones.Num(); ++BoneIndex)
    {
        const FSAImportBone& Bone = InOutSkeletonBones[BoneIndex];
        const FTransform LocalTransform(Bone.LocalRotation, Bone.LocalTranslation, Bone.LocalScale);
        ReferenceComponentPoses[BoneIndex] = Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex
            ? LocalTransform * ReferenceComponentPoses[Bone.ParentIndex]
            : LocalTransform;
    }

    InOutSkeletonBones.Reserve(InOutSkeletonBones.Num() + AuxiliaryBones.Num());
    for (int32 AuxiliaryIndex = 0; AuxiliaryIndex < AuxiliaryBones.Num(); ++AuxiliaryIndex)
    {
        FSAImportBone Bone = AuxiliaryBones[AuxiliaryIndex];
        Bone.ParentIndex = ResolvedParentIndices[AuxiliaryIndex];
        Bone.LocalRotation.Normalize();

        const int32 NewBoneIndex = InOutSkeletonBones.Num();
        const FTransform LocalTransform(Bone.LocalRotation, Bone.LocalTranslation, Bone.LocalScale);
        const FTransform ComponentTransform = LocalTransform * ReferenceComponentPoses[Bone.ParentIndex];
        ReferenceComponentPoses[NewBoneIndex] = ComponentTransform;
        Bone.WorldTranslation = ComponentTransform.GetTranslation();
        Bone.WorldRotation = ComponentTransform.GetRotation();
        Bone.WorldScale = ComponentTransform.GetScale3D();
        InOutSkeletonBones.Add(MoveTemp(Bone));
    }

    UE_LOG(LogTemp, Display, TEXT("Skeleton '%s' appended %d unweighted auxiliary bones"),
        *SkeletonName, AuxiliaryBones.Num());
    return true;
}

// ============================================================================
// Build Skeleton
// ============================================================================

/**
 * 将源骨架转换为 UE 坐标系，确保唯一的单位变换顶层 Root，并在主骨架完成后追加已声明的辅助骨骼。
 * 函数不改变输入数组，也不允许辅助骨骼重设既有层级；成功时会覆盖并保存目标 Skeleton 包。
 * 只能在编辑器线程调用。
 *
 * @param Bones 按父级先于子级排列的源骨骼，只读且不保留引用。
 * @param AuxiliaryBones 按父级先于子级声明的 UE 局部空间无蒙皮参考骨骼，只读且可为空。
 * @param SkeletonName 新建 Skeleton 对象名称，同时用于错误诊断。
 * @param PackagePath 要覆盖保存的 UE 长包名。
 * @return 成功时返回由 UE 管理的新 Skeleton；骨架拓扑、辅助声明或保存前构建失败时返回 nullptr。
 */
USkeleton* SAModelImporter::BuildSkeleton(
    const TArray<FSAImportBone>& Bones,
    const TArray<FSAImportBone>& AuxiliaryBones,
    const FString& SkeletonName,
    const FString& PackagePath)
{
    if (Bones.Num() == 0) return nullptr;

    const FQuat OrientQ = GetOrientQ();
    const FName IndependentRootBoneName(TEXT("Root"));

    int32 NamedRootIndex = INDEX_NONE;
    int32 NamedRootCount = 0;
    int32 SourceRootIndex = INDEX_NONE;
    int32 SourceRootCount = 0;
    for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
    {
        const FSAImportBone& Bone = Bones[BoneIndex];
        if (Bone.Name == IndependentRootBoneName)
        {
            NamedRootIndex = BoneIndex;
            ++NamedRootCount;
        }
        if (Bone.ParentIndex == INDEX_NONE)
        {
            SourceRootIndex = BoneIndex;
            ++SourceRootCount;
        }
    }

    const bool bSourceHasNamedRoot = NamedRootIndex != INDEX_NONE;
    if (bSourceHasNamedRoot)
    {
        const FSAImportBone& SourceRoot = Bones[NamedRootIndex];
        const bool bIdentityReferencePose = SourceRoot.WorldTranslation.IsNearlyZero()
            && SourceRoot.WorldRotation.Equals(FQuat::Identity)
            && SourceRoot.WorldScale.Equals(FVector::OneVector);
        const bool bLegalSingleRoot = NamedRootCount == 1
            && NamedRootIndex == 0
            && SourceRoot.ParentIndex == INDEX_NONE
            && SourceRootCount == 1
            && bIdentityReferencePose;
        if (!bLegalSingleRoot)
        {
            UE_LOG(LogTemp, Error,
                TEXT("Skeleton '%s' contains bone 'Root', but it is not a legal identity single root; import aborted to avoid duplicate Root bones"),
                *SkeletonName);
            return nullptr;
        }
    }
    else if (SourceRootCount != 1 || SourceRootIndex != 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("Skeleton '%s' must contain one topologically first source root before an independent Root can be synthesized"),
            *SkeletonName);
        return nullptr;
    }

    TArray<FSAImportBone> OrientedBones = Bones;
    for (FSAImportBone& Bone : OrientedBones)
    {
        Bone.WorldTranslation = OrientQ.RotateVector(Bone.WorldTranslation);
        Bone.WorldRotation = OrientQ * Bone.WorldRotation;
    }
    for (int32 i = 0; i < OrientedBones.Num(); ++i)
    {
        FSAImportBone& Bone = OrientedBones[i];
        if (Bone.ParentIndex >= 0 && Bone.ParentIndex < i)
        {
            const FSAImportBone& P = OrientedBones[Bone.ParentIndex];
            FTransform PW; PW.SetRotation(P.WorldRotation); PW.SetTranslation(P.WorldTranslation); PW.SetScale3D(P.WorldScale);
            FTransform W;  W.SetRotation(Bone.WorldRotation);  W.SetTranslation(Bone.WorldTranslation);  W.SetScale3D(Bone.WorldScale);
            FTransform L = W.GetRelativeTransform(PW);
            Bone.LocalRotation = L.GetRotation(); Bone.LocalTranslation = L.GetTranslation(); Bone.LocalScale = L.GetScale3D();
        }
        else { Bone.LocalRotation = Bone.WorldRotation; Bone.LocalTranslation = Bone.WorldTranslation; Bone.LocalScale = Bone.WorldScale; }
    }

    TArray<FSAImportBone> SkeletonBones;
    if (bSourceHasNamedRoot)
    {
        SkeletonBones = MoveTemp(OrientedBones);
        SkeletonBones[0].LocalTranslation = FVector::ZeroVector;
        SkeletonBones[0].LocalRotation = FQuat::Identity;
        SkeletonBones[0].LocalScale = FVector::OneVector;
        UE_LOG(LogTemp, Display, TEXT("Skeleton '%s' uses existing identity Root bone"), *SkeletonName);
    }
    else
    {
        // 在索引 0 合成纯轨迹根；原始根及全部后代整体后移一位。
        SkeletonBones.Reserve(OrientedBones.Num() + 1);

        FSAImportBone IndependentRoot;
        IndependentRoot.Name = IndependentRootBoneName;
        IndependentRoot.ParentIndex = INDEX_NONE;
        IndependentRoot.LocalTranslation = FVector::ZeroVector;
        IndependentRoot.LocalRotation = FQuat::Identity;
        IndependentRoot.LocalScale = FVector::OneVector;
        IndependentRoot.WorldTranslation = FVector::ZeroVector;
        IndependentRoot.WorldRotation = FQuat::Identity;
        IndependentRoot.WorldScale = FVector::OneVector;
        SkeletonBones.Add(IndependentRoot);

        for (int32 SourceBoneIndex = 0; SourceBoneIndex < OrientedBones.Num(); ++SourceBoneIndex)
        {
            FSAImportBone Bone = OrientedBones[SourceBoneIndex];
            Bone.ParentIndex = Bone.ParentIndex >= 0 && Bone.ParentIndex < SourceBoneIndex
                ? Bone.ParentIndex + 1
                : 0;
            SkeletonBones.Add(MoveTemp(Bone));
        }

        UE_LOG(LogTemp, Display, TEXT("Skeleton '%s' synthesized identity Root above %d source bones"),
            *SkeletonName, Bones.Num());
    }

    if (!AppendAuxiliaryBones(SkeletonBones, AuxiliaryBones, SkeletonName))
        return nullptr;

    {
        FString ExistingFilePath = FPackageName::LongPackageNameToFilename(*PackagePath, FPackageName::GetAssetPackageExtension());
        if (FPaths::FileExists(ExistingFilePath))
            IFileManager::Get().Delete(*ExistingFilePath);
        UPackage* StalePackage = FindPackage(nullptr, *PackagePath);
        if (StalePackage)
        {
            TArray<UObject*> ObjectsInPackage;
            GetObjectsWithOuter(StalePackage, ObjectsInPackage, false);
            for (UObject* Obj : ObjectsInPackage)
            {
                Obj->ClearFlags(RF_Standalone | RF_Public);
                Obj->Rename(nullptr, GetTransientPackage(),
                    REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
            }
            ResetLoaders(StalePackage);
            StalePackage->ClearFlags(RF_WasLoaded);
            StalePackage->ClearInternalFlags(EInternalObjectFlags_AsyncLoading);
        }
    }
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package) return nullptr;

    USkeleton* Skeleton = NewObject<USkeleton>(Package, USkeleton::StaticClass(), FName(*SkeletonName), RF_Public | RF_Standalone);
    if (!Skeleton) return nullptr;

    FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(Skeleton);
    for (int32 i = 0; i < SkeletonBones.Num(); ++i)
    {
        const FSAImportBone& B = SkeletonBones[i];
        int32 ParentIdx = B.ParentIndex;
        // Fix invalid parent: only root (bone 0) can have INDEX_NONE
        // and parent must always be before child
        if (i == 0)
            ParentIdx = INDEX_NONE;
        else if (ParentIdx == INDEX_NONE || ParentIdx >= i || ParentIdx < 0)
            ParentIdx = 0;
        FMeshBoneInfo Info(B.Name, B.Name.ToString(), ParentIdx);
        FTransform T; T.SetRotation(B.LocalRotation); T.SetTranslation(B.LocalTranslation); T.SetScale3D(B.LocalScale);
        Modifier.Add(Info, T);
    }

    Skeleton->MarkPackageDirty();
    FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.Error = GWarn;
    UPackage::SavePackage(Package, Skeleton, *FileName, Args);

    UE_LOG(LogTemp, Log, TEXT("Skeleton build success: %s (%d bones)"), *PackagePath, SkeletonBones.Num());
    return Skeleton;
}

// ============================================================================
// Build Skeletal Mesh
// ============================================================================

static FQuat GetMeshOrientQ() { return GetOrientQ(); }

USkeletalMesh* SAModelImporter::BuildSkeletalMesh(const FSAModelData& ModelData, USkeleton* Skeleton, const FString& PackagePath)
{
    if (ModelData.Meshes.Num() == 0 || !Skeleton) return nullptr;

    {
        FString ExistingFilePath = FPackageName::LongPackageNameToFilename(*PackagePath, FPackageName::GetAssetPackageExtension());
        if (FPaths::FileExists(ExistingFilePath))
            IFileManager::Get().Delete(*ExistingFilePath);
        UPackage* StalePackage = FindPackage(nullptr, *PackagePath);
        if (StalePackage)
        {
            TArray<UObject*> ObjectsInPackage;
            GetObjectsWithOuter(StalePackage, ObjectsInPackage, false);
            for (UObject* Obj : ObjectsInPackage)
            {
                Obj->ClearFlags(RF_Standalone | RF_Public);
                Obj->Rename(nullptr, GetTransientPackage(),
                    REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
            }
            ResetLoaders(StalePackage);
            StalePackage->ClearFlags(RF_WasLoaded);
            StalePackage->ClearInternalFlags(EInternalObjectFlags_AsyncLoading);
        }
    }
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package) return nullptr;

    FString AssetName = FPackageName::GetShortName(PackagePath);
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, USkeletalMesh::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    if (!Mesh) return nullptr;

    Mesh->SetSkeleton(Skeleton);

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TMap<FName, int32> BoneNameToSkelIndex;
    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
        BoneNameToSkelIndex.Add(RefSkel.GetBoneName(i), i);

    FSkeletalMeshImportData ImportData;
    ImportData.bHasNormals = true;
    ImportData.bHasVertexColors = false;
    ImportData.NumTexCoords = 1;
    ImportData.MaxMaterialIndex = FMath::Max(0, ModelData.Materials.Num() - 1);

    const FQuat OrientQ = GetMeshOrientQ();
    int32 GlobalVertexOffset = 0;
    int32 SkippedTris = 0;

    TSet<uint64> SeenTriKeys;

    // Compute skeleton bone world positions for orphan fallback
    TArray<FVector> BoneWorldPositions;
    BoneWorldPositions.SetNum(RefSkel.GetNum());
    {
        TArray<FTransform> WT;
        WT.SetNum(RefSkel.GetNum());
        const TArray<FTransform>& RP = RefSkel.GetRefBonePose();
        for (int32 b = 0; b < RefSkel.GetNum(); ++b)
        {
            int32 P = RefSkel.GetParentIndex(b);
            WT[b] = (P >= 0 && P < b) ? RP[b] * WT[P] : RP[b];
            BoneWorldPositions[b] = WT[b].GetTranslation();
        }
    }

    for (int32 SecIdx = 0; SecIdx < ModelData.Meshes.Num(); ++SecIdx)
    {
        const auto& Section = ModelData.Meshes[SecIdx];
        if (Section.Vertices.Num() == 0) { continue; }

        // Vertex positions
        for (const auto& V : Section.Vertices)
        {
            FVector3f Pos = FVector3f(OrientQ.RotateVector(FVector(V.Position)));
            ImportData.Points.Add(Pos);
        }

        int32 SectionWedgeStart = ImportData.Wedges.Num();

        // Triangles
        for (const auto& Tri : Section.Triangles)
        {
            if (Tri.X < 0 || Tri.X >= Section.Vertices.Num() ||
                Tri.Y < 0 || Tri.Y >= Section.Vertices.Num() ||
                Tri.Z < 0 || Tri.Z >= Section.Vertices.Num()) { ++SkippedTris; continue; }
            if (Tri.X == Tri.Y || Tri.Y == Tri.Z || Tri.X == Tri.Z) { ++SkippedTris; continue; }
            {
                const FVector3f& P0 = Section.Vertices[Tri.X].Position;
                const FVector3f& P1 = Section.Vertices[Tri.Y].Position;
                const FVector3f& P2 = Section.Vertices[Tri.Z].Position;
                if (((P1 - P0) ^ (P2 - P0)).SizeSquared() < 1e-12f) { ++SkippedTris; continue; }
            }
            {
                int32 G0 = GlobalVertexOffset + Tri.X, G1 = GlobalVertexOffset + Tri.Y, G2 = GlobalVertexOffset + Tri.Z;
                if (G0 > G1) Swap(G0, G1);
                if (G1 > G2) Swap(G1, G2);
                if (G0 > G1) Swap(G0, G1);
                uint64 Key = ((uint64)G0 << 40) | ((uint64)G1 << 20) | (uint64)G2;
                if (SeenTriKeys.Contains(Key)) { ++SkippedTris; continue; }
                SeenTriKeys.Add(Key);
            }

            SkeletalMeshImportData::FRawBoneInfluence Inf0, Inf1, Inf2;
            const auto& V0 = Section.Vertices[Tri.X], V1 = Section.Vertices[Tri.Y], V2 = Section.Vertices[Tri.Z];

            for (int32 j = 0; j < 4; ++j)
            {
                if (V0.BoneWeights[j] > 0) {
                    const FName* BN = Section.BoneIdxToName.Find(V0.BoneIndices[j]);
                    if (BN && BoneNameToSkelIndex.Contains(*BN))
                    { Inf0.VertexIndex = GlobalVertexOffset + Tri.X; Inf0.BoneIndex = BoneNameToSkelIndex[*BN]; Inf0.Weight = V0.BoneWeights[j]; ImportData.Influences.Add(Inf0); }
                }
                if (V1.BoneWeights[j] > 0) {
                    const FName* BN = Section.BoneIdxToName.Find(V1.BoneIndices[j]);
                    if (BN && BoneNameToSkelIndex.Contains(*BN))
                    { Inf1.VertexIndex = GlobalVertexOffset + Tri.Y; Inf1.BoneIndex = BoneNameToSkelIndex[*BN]; Inf1.Weight = V1.BoneWeights[j]; ImportData.Influences.Add(Inf1); }
                }
                if (V2.BoneWeights[j] > 0) {
                    const FName* BN = Section.BoneIdxToName.Find(V2.BoneIndices[j]);
                    if (BN && BoneNameToSkelIndex.Contains(*BN))
                    { Inf2.VertexIndex = GlobalVertexOffset + Tri.Z; Inf2.BoneIndex = BoneNameToSkelIndex[*BN]; Inf2.Weight = V2.BoneWeights[j]; ImportData.Influences.Add(Inf2); }
                }
            }

            {
                SkeletalMeshImportData::FVertex V;
                V.VertexIndex = GlobalVertexOffset + Tri.X;
                V.UVs[0] = FVector2f(V0.UV.X, V0.UV.Y);
                V.Color = FColor::White;
                V.MatIndex = (uint8)Section.MaterialIndex;
                ImportData.Wedges.Add(V);
            }
            {
                SkeletalMeshImportData::FVertex V;
                V.VertexIndex = GlobalVertexOffset + Tri.Y;
                V.UVs[0] = FVector2f(V1.UV.X, V1.UV.Y);
                V.Color = FColor::White;
                V.MatIndex = (uint8)Section.MaterialIndex;
                ImportData.Wedges.Add(V);
            }
            {
                SkeletalMeshImportData::FVertex V;
                V.VertexIndex = GlobalVertexOffset + Tri.Z;
                V.UVs[0] = FVector2f(V2.UV.X, V2.UV.Y);
                V.Color = FColor::White;
                V.MatIndex = (uint8)Section.MaterialIndex;
                ImportData.Wedges.Add(V);
            }
            {
                SkeletalMeshImportData::FTriangle Face;
                Face.WedgeIndex[0] = SectionWedgeStart;
                Face.WedgeIndex[1] = SectionWedgeStart + 1;
                Face.WedgeIndex[2] = SectionWedgeStart + 2;
                Face.MatIndex = Section.MaterialIndex;
                ImportData.Faces.Add(Face);
            }
            SectionWedgeStart += 3;
        }

        GlobalVertexOffset += Section.Vertices.Num();
    }

    // Reference bones
    const TArray<FTransform>& RefBonePose = RefSkel.GetRefBonePose();
    ImportData.RefBonesBinary.Reserve(RefSkel.GetNum());
    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
    {
        SkeletalMeshImportData::FBone Bone;
        Bone.Name = RefSkel.GetBoneName(i).ToString();
        Bone.ParentIndex = RefSkel.GetParentIndex(i);
        Bone.BonePos.Transform = FTransform3f(RefBonePose[i]);
        ImportData.RefBonesBinary.Add(Bone);
    }

    ImportData.PointToRawMap.SetNum(ImportData.Points.Num());
    for (int32 i = 0; i < ImportData.Points.Num(); ++i)
        ImportData.PointToRawMap[i] = i;

    for (const auto& Mat : ModelData.Materials)
    {
        SkeletalMeshImportData::FMaterial ImpMat;
        ImpMat.MaterialImportName = Mat.Name;
        ImportData.Materials.Add(ImpMat);
    }

    FSkeletalMeshModel* ImportedResource = Mesh->GetImportedModel();
    ImportedResource->LODModels.Empty();
    ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
    Mesh->AddLODInfo();

    Mesh->GetMaterials().Reset();
    for (const auto& Mat : ModelData.Materials)
    {
        FSkeletalMaterial SkMat;
        SkMat.MaterialSlotName = FName(*Mat.Name);
        SkMat.ImportedMaterialSlotName = FName(*Mat.Name);
        Mesh->GetMaterials().Add(SkMat);
    }

    {
        FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton);
        for (int32 i = 0; i < RefSkel.GetNum(); ++i)
            Modifier.Add(FMeshBoneInfo(RefSkel.GetBoneName(i), RefSkel.GetBoneName(i).ToString(), RefSkel.GetParentIndex(i)), RefBonePose[i]);
    }

    Mesh->InvalidateDeriveDataCacheGUID();
    Mesh->SetLODImportedDataVersions(0, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
    Mesh->SaveLODImportedData(0, ImportData);
    Mesh->Build();
    {
        USkinnedAsset* Assets[] = { Mesh };
        FSkinnedAssetCompilingManager::Get().FinishCompilation(Assets);
    }
    Mesh->CalculateInvRefMatrices();

    {
        FBox Box(ForceInit);
        for (const FVector3f& P : ImportData.Points) Box += FVector(P);
        Mesh->SetImportedBounds(FBoxSphereBounds(Box));
    }

    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();

    FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.Error = GWarn;
    UPackage::SavePackage(Package, Mesh, *FileName, Args);

    UE_LOG(LogTemp, Log, TEXT("SkeletalMesh build success: %s"), *PackagePath);
    return Mesh;
}

/**
 * 按模型材质声明的稳定槽位顺序加载目标目录中已经存在的材质，并重新绑定到刚重建的 SkeletalMesh。
 * 本函数不会创建、覆盖或保存材质资产，只会修改并保存传入网格；必须在编辑器游戏线程调用。
 * 找不到某个材质时保留该槽为空并记录明确警告，禁止回退到其他槽位或按索引误绑。
 *
 * @param ModelData 已通过校验的模型数据，只读取材质名称和顺序，不保留引用。
 * @param SkeletalMesh 需要恢复材质引用的已构建网格，不能为空，由 UE 管理生命周期。
 * @param TargetPackagePath 模型资产所在的长包目录；已有材质必须位于其 Materials 子目录。
 * @param OutBoundMaterialCount 返回成功绑定的材质槽数量，调用开始时重置为零。
 * @return 网格材质数组有效且修改后的网格包保存成功时返回 true；输入无效或保存失败时返回 false。
 */
static bool BindExistingMaterials(
    const FSAModelData& ModelData,
    USkeletalMesh* SkeletalMesh,
    const FString& TargetPackagePath,
    int32& OutBoundMaterialCount)
{
    OutBoundMaterialCount = 0;
    if (!SkeletalMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot bind existing materials to a null SkeletalMesh"));
        return false;
    }

    TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMesh->GetMaterials();
    const int32 MaterialSlotCount = FMath::Min(ModelData.Materials.Num(), SkeletalMaterials.Num());
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotCount; ++MaterialIndex)
    {
        const FString SafeName = SAMaterialImporter::SanitizeMaterialName(ModelData.Materials[MaterialIndex].Name);
        const FString MaterialAssetName = FString::Printf(TEXT("M_%s"), *SafeName);
        const FString MaterialObjectPath = FString::Printf(
            TEXT("%s/Materials/%s.%s"),
            *TargetPackagePath,
            *MaterialAssetName,
            *MaterialAssetName);
        UMaterialInterface* ExistingMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialObjectPath);
        if (!ExistingMaterial)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Existing material not found for slot %d '%s': %s"),
                MaterialIndex,
                *ModelData.Materials[MaterialIndex].Name,
                *MaterialObjectPath);
            continue;
        }

        SkeletalMaterials[MaterialIndex].MaterialInterface = ExistingMaterial;
        ++OutBoundMaterialCount;
    }

    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    UPackage* MeshPackage = SkeletalMesh->GetOutermost();
    if (!MeshPackage)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot save material bindings: SkeletalMesh has no package"));
        return false;
    }

    const FString MeshFileName = FPackageName::LongPackageNameToFilename(
        MeshPackage->GetName(),
        FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    if (!UPackage::SavePackage(MeshPackage, SkeletalMesh, *MeshFileName, SaveArgs))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save existing material bindings: %s"), *MeshPackage->GetName());
        return false;
    }

    return true;
}

// ============================================================================
// Main Import
// ============================================================================

/**
 * 从模型 JSON 重建 Skeleton 与 SkeletalMesh，并按需重建材质资产。函数会覆盖目标骨架和网格包；
 * bImportMaterials 为 false 时不会创建或覆盖材质资源，而会按稳定命名重新绑定目标目录中的已有材质。
 * 辅助骨骼仅来自 JSON 声明，不包含项目特定名称。
 * 只能在编辑器线程调用，调用期间会同步等待 SkeletalMesh 编译完成并保存生成的资产包。
 *
 * @param JsonPath 模型 JSON 的本地文件路径，必须可读且包含主骨架与网格数据。
 * @param TargetPackagePath 生成资产所在的 UE 长包目录，不保留调用方字符串引用。
 * @param TextureSourceDirs 材质导入使用的纹理搜索目录；跳过材质时忽略。
 * @param OutSkeletalMesh 成功时接收新建的网格资产，失败时可能为空；对象由 UE 管理。
 * @param OutSkeleton 成功时接收新建的骨架资产，失败时可能为空；对象由 UE 管理。
 * @param bImportMaterials true 时保持完整导入行为，false 时重建骨架与网格并复用已有材质资产。
 * @return 骨架与网格均成功生成时返回 true；解析或任一必要构建步骤失败时返回 false。
 */
bool SAModelImporter::Import(
    const FString& JsonPath,
    const FString& TargetPackagePath,
    const TArray<FString>& TextureSourceDirs,
    USkeletalMesh*& OutSkeletalMesh,
    USkeleton*& OutSkeleton,
    bool bImportMaterials)
{
    FSAModelData ModelData;
    if (!ParseFromFile(JsonPath, ModelData))
        return false;

    // DSAnimStudio approach: Python already merged all needed bones into Bones array.
    // No AppendModelOnlyBones needed — Bones is the authoritative skeleton.

	FString SkeletonName = ModelData.SkeletonName;
	if (SkeletonName.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("SkeletonName is empty in JSON")); return false; }
    FString MeshName = SkeletonName.Replace(TEXT("_Skeleton"), TEXT("_Model"));
    FString SkeletonPackagePath = TargetPackagePath / SkeletonName;
    FString MeshPackagePath     = TargetPackagePath / MeshName;

    OutSkeleton = BuildSkeleton(ModelData.Bones, ModelData.AuxiliaryBones, SkeletonName, SkeletonPackagePath);
    if (!OutSkeleton) { UE_LOG(LogTemp, Error, TEXT("Skeleton build failed")); return false; }

    OutSkeletalMesh = BuildSkeletalMesh(ModelData, OutSkeleton, MeshPackagePath);
    if (!OutSkeletalMesh) { UE_LOG(LogTemp, Error, TEXT("SkeletalMesh build failed")); return false; }

    if (bImportMaterials)
    {
        // Build materials: uses TargetPackagePath as base ? Materials/ and Textures/ subfolders
        const TArray<UMaterial*> Materials = SAMaterialImporter::BuildAll(
            ModelData, OutSkeletalMesh, TargetPackagePath, TextureSourceDirs);
        UE_LOG(LogTemp, Log, TEXT("Materials built: %d"), Materials.Num());
    }
    else
    {
        int32 BoundMaterialCount = 0;
        if (!BindExistingMaterials(ModelData, OutSkeletalMesh, TargetPackagePath, BoundMaterialCount))
            return false;

        UE_LOG(LogTemp, Display,
            TEXT("Material asset rebuild skipped for model '%s'; rebound %d/%d existing materials"),
            *ModelData.AssetName,
            BoundMaterialCount,
            ModelData.Materials.Num());
    }

    return true;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSAModelAuxiliaryBoneParsingTest,
    "Sekiro.AssetManager.Model.AuxiliaryBones.Parse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证可选 AuxiliaryBones JSON 的标准字段、Local 前缀兼容字段、默认变换和 UE 厘米单位解析。
 * 测试只在 Intermediate 目录创建一个短期 JSON 文件并在解析后删除，不创建 UE 资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FSAModelAuxiliaryBoneParsingTest::RunTest(const FString& Parameters)
{
    const FString Json = TEXT(R"JSON(
{
    "AssetName": "AuxiliaryTest",
    "SkeletonName": "AuxiliaryTest_Skeleton",
    "AuxiliaryBones": [
        {
            "Name": "ReferenceA",
            "ParentName": "Root",
            "Translation": [10.0, 20.0, 30.0],
            "Rotation": [0.0, 0.0, 0.0, 1.0],
            "Scale": [1.0, 2.0, 3.0]
        },
        {
            "Name": "ReferenceB",
            "ParentName": "ReferenceA",
            "LocalTranslation": [1.0, 2.0, 3.0]
        }
    ]
}
)JSON");
    const FString JsonPath = FPaths::CreateTempFilename(
        *FPaths::ProjectIntermediateDir(), TEXT("SAModelAuxiliaryBones"), TEXT(".json"));
    if (!TestTrue(TEXT("temporary JSON is written"), FFileHelper::SaveStringToFile(Json, *JsonPath)))
        return true;

    FSAModelData ModelData;
    const bool bParsed = SAModelImporter::ParseFromFile(JsonPath, ModelData);
    IFileManager::Get().Delete(*JsonPath, false, true);

    if (!TestTrue(TEXT("AuxiliaryBones JSON parses"), bParsed)) return true;
    if (!TestEqual(TEXT("two auxiliary bones parsed"), ModelData.AuxiliaryBones.Num(), 2)) return true;

    const FSAImportBone& ReferenceA = ModelData.AuxiliaryBones[0];
    const FSAImportBone& ReferenceB = ModelData.AuxiliaryBones[1];
    TestTrue(TEXT("standard translation remains in UE centimeters"),
        ReferenceA.LocalTranslation.Equals(FVector(10.0, 20.0, 30.0)));
    TestTrue(TEXT("standard rotation remains unchanged"), ReferenceA.LocalRotation.Equals(FQuat::Identity));
    TestTrue(TEXT("standard scale parses"), ReferenceA.LocalScale.Equals(FVector(1.0, 2.0, 3.0)));
    TestTrue(TEXT("LocalTranslation alias parses"), ReferenceB.LocalTranslation.Equals(FVector(1.0, 2.0, 3.0)));
    TestTrue(TEXT("omitted rotation defaults to identity"), ReferenceB.LocalRotation.Equals(FQuat::Identity));
    TestTrue(TEXT("omitted scale defaults to one"), ReferenceB.LocalScale.Equals(FVector::OneVector));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSAModelAuxiliaryBoneAppendTest,
    "Sekiro.AssetManager.Model.AuxiliaryBones.AppendValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证辅助骨骼只追加在既有骨架尾部、允许引用更早的辅助父级，并原子拒绝重复名、缺失父级和后置父级。
 * 测试仅操作内存中的导入中间结构，不创建或保存 UE 资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FSAModelAuxiliaryBoneAppendTest::RunTest(const FString& Parameters)
{
    TArray<FSAImportBone> SkeletonBones;
    FSAImportBone RootBone;
    RootBone.Name = FName(TEXT("Root"));
    SkeletonBones.Add(RootBone);

    FSAImportBone ExistingBone;
    ExistingBone.Name = FName(TEXT("Existing"));
    ExistingBone.ParentName = RootBone.Name;
    ExistingBone.ParentIndex = 0;
    ExistingBone.LocalTranslation = FVector(5.0, 0.0, 0.0);
    SkeletonBones.Add(ExistingBone);
    const TArray<FSAImportBone> OriginalSkeletonBones = SkeletonBones;

    TArray<FSAImportBone> ValidAuxiliaryBones;
    FSAImportBone ReferenceA;
    ReferenceA.Name = FName(TEXT("ReferenceA"));
    ReferenceA.ParentName = RootBone.Name;
    ReferenceA.LocalTranslation = FVector(10.0, 0.0, 0.0);
    ValidAuxiliaryBones.Add(ReferenceA);
    FSAImportBone ReferenceB;
    ReferenceB.Name = FName(TEXT("ReferenceB"));
    ReferenceB.ParentName = ReferenceA.Name;
    ReferenceB.LocalTranslation = FVector(0.0, 20.0, 0.0);
    ValidAuxiliaryBones.Add(ReferenceB);

    TestTrue(TEXT("valid ordered auxiliary bones append"),
        AppendAuxiliaryBones(SkeletonBones, ValidAuxiliaryBones, TEXT("TestSkeleton")));
    TestEqual(TEXT("two auxiliary bones are appended"), SkeletonBones.Num(), 4);
    TestEqual(TEXT("first auxiliary parent resolves to Root"), SkeletonBones[2].ParentIndex, 0);
    TestEqual(TEXT("second auxiliary parent resolves to prior auxiliary"), SkeletonBones[3].ParentIndex, 2);
    TestTrue(TEXT("existing bone transform is unchanged"),
        SkeletonBones[1].LocalTranslation.Equals(OriginalSkeletonBones[1].LocalTranslation));
    TestTrue(TEXT("nested auxiliary component translation is derived without altering local pose"),
        SkeletonBones[3].WorldTranslation.Equals(FVector(10.0, 20.0, 0.0)));

    TArray<FSAImportBone> DuplicateTarget = OriginalSkeletonBones;
    TArray<FSAImportBone> DuplicateAuxiliary;
    FSAImportBone DuplicateBone = ReferenceA;
    DuplicateBone.Name = ExistingBone.Name;
    DuplicateAuxiliary.Add(DuplicateBone);
    AddExpectedError(
        TEXT("has invalid or duplicate name 'Existing'"),
        EAutomationExpectedErrorFlags::Contains,
        1);
    TestFalse(TEXT("existing name collision is rejected"),
        AppendAuxiliaryBones(DuplicateTarget, DuplicateAuxiliary, TEXT("TestSkeleton")));
    TestEqual(TEXT("duplicate failure is atomic"), DuplicateTarget.Num(), OriginalSkeletonBones.Num());

    TArray<FSAImportBone> MissingParentTarget = OriginalSkeletonBones;
    TArray<FSAImportBone> MissingParentAuxiliary;
    FSAImportBone MissingParentBone = ReferenceA;
    MissingParentBone.ParentName = FName(TEXT("Missing"));
    MissingParentAuxiliary.Add(MissingParentBone);
    AddExpectedError(
        TEXT("parent 'Missing' does not exist"),
        EAutomationExpectedErrorFlags::Contains,
        1);
    TestFalse(TEXT("missing parent is rejected"),
        AppendAuxiliaryBones(MissingParentTarget, MissingParentAuxiliary, TEXT("TestSkeleton")));
    TestEqual(TEXT("missing parent failure is atomic"), MissingParentTarget.Num(), OriginalSkeletonBones.Num());

    TArray<FSAImportBone> LateParentTarget = OriginalSkeletonBones;
    TArray<FSAImportBone> LateParentAuxiliary;
    FSAImportBone ChildBeforeParent = ReferenceB;
    ChildBeforeParent.Name = FName(TEXT("ChildBeforeParent"));
    ChildBeforeParent.ParentName = FName(TEXT("LateParent"));
    LateParentAuxiliary.Add(ChildBeforeParent);
    FSAImportBone LateParent = ReferenceA;
    LateParent.Name = FName(TEXT("LateParent"));
    LateParentAuxiliary.Add(LateParent);
    AddExpectedError(
        TEXT("parent 'LateParent' is declared at or after its child"),
        EAutomationExpectedErrorFlags::Contains,
        1);
    TestFalse(TEXT("parent declared after child is rejected"),
        AppendAuxiliaryBones(LateParentTarget, LateParentAuxiliary, TEXT("TestSkeleton")));
    TestEqual(TEXT("late parent failure is atomic"), LateParentTarget.Num(), OriginalSkeletonBones.Num());
    return true;
}

#endif

