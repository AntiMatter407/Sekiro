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

        // 直接�?Materials 对象读取 Textures 映射（Python 端已内联�?
        const TSharedPtr<FJsonObject>* InlineTexObj = nullptr;
        if ((*Obj)->TryGetObjectField(TEXT("Textures"), InlineTexObj))
            for (const auto& Pair : (*InlineTexObj)->Values)
                Mat.ResolvedTextures.Add(Pair.Key, Pair.Value->AsString());

        OutMaterials.Add(MoveTemp(Mat));
    }

    // 兼容�?JSON 格式：通过 ResolvedMaterials 找未匹配的纹�?
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

            // 仅当 Materials 中未内联 Textures 时才�?ResolvedMaterials 读取
            // ResolvedMaterials always override Materials textures (Python resolution wins)
            {
                const TSharedPtr<FJsonObject>* TexObj = nullptr;
                if ((*RmObj)->TryGetObjectField(TEXT("Textures"), TexObj))
                    for (const auto& Pair : (*TexObj)->Values)
                        Mat->ResolvedTextures.Add(Pair.Key, Pair.Value->AsString());
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
// 网格体解�?
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

    // 解析可选的 FlverBones（用�?ModelOnly 追加的数据源�?
    const TArray<TSharedPtr<FJsonValue>>* FlverBonesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("FlverBones"), FlverBonesArr))
        ParseBones(*FlverBonesArr, OutData.FlverBones);

    const TArray<TSharedPtr<FJsonValue>>* MatsArr = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* ResolvedMatsArr = nullptr;
    Root->TryGetArrayField(TEXT("ResolvedMaterials"), ResolvedMatsArr);
    if (Root->TryGetArrayField(TEXT("Materials"), MatsArr))
        ParseMaterials(*MatsArr, ResolvedMatsArr, OutData.Materials);

    const TArray<TSharedPtr<FJsonValue>>* MeshesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("Meshes"), MeshesArr))
        ParseMeshes(*MeshesArr, OutData.Meshes);

    UE_LOG(LogTemp, Log, TEXT("解析完成: %d 骨骼, %d FlverBones, %d 材质, %d 网格"),
        OutData.Bones.Num(), OutData.FlverBones.Num(), OutData.Materials.Num(), OutData.Meshes.Num());
    return true;
}

// ============================================================================
// 收集网格引用的骨骼名
// ============================================================================

// ============================================================================
// 追加 ModelOnly 骨骼
// ============================================================================



// ============================================================================
// Build Skeleton
// ============================================================================

USkeleton* SAModelImporter::BuildSkeleton(const TArray<FSAImportBone>& Bones, const FString& SkeletonName, const FString& PackagePath)
{
    if (Bones.Num() == 0) return nullptr;

    const FQuat OrientQ = GetOrientQ();

    TArray<FSAImportBone> OrientedBones = Bones;
    for (auto& Bone : OrientedBones)
    {
        Bone.WorldTranslation = OrientQ.RotateVector(Bone.WorldTranslation);
        Bone.WorldRotation = OrientQ * Bone.WorldRotation;
    }
    for (int32 i = 0; i < OrientedBones.Num(); ++i)
    {
        auto& Bone = OrientedBones[i];
        if (Bone.ParentIndex != INDEX_NONE)
        {
            const auto& P = OrientedBones[Bone.ParentIndex];
            FTransform PW; PW.SetRotation(P.WorldRotation); PW.SetTranslation(P.WorldTranslation); PW.SetScale3D(P.WorldScale);
            FTransform W;  W.SetRotation(Bone.WorldRotation);  W.SetTranslation(Bone.WorldTranslation);  W.SetScale3D(Bone.WorldScale);
            FTransform L = W.GetRelativeTransform(PW);
            Bone.LocalRotation = L.GetRotation(); Bone.LocalTranslation = L.GetTranslation(); Bone.LocalScale = L.GetScale3D();
        }
        else { Bone.LocalRotation = Bone.WorldRotation; Bone.LocalTranslation = Bone.WorldTranslation; Bone.LocalScale = Bone.WorldScale; }
    }

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
            StalePackage->ClearInternalFlags(EInternalObjectFlags::AsyncLoading);
        }
    }
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package) return nullptr;

    USkeleton* Skeleton = NewObject<USkeleton>(Package, USkeleton::StaticClass(), FName(*SkeletonName), RF_Public | RF_Standalone);
    if (!Skeleton) return nullptr;

    FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(Skeleton);
    for (int32 i = 0; i < OrientedBones.Num(); ++i)
    {
        const auto& B = OrientedBones[i];
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

    UE_LOG(LogTemp, Log, TEXT("Skeleton build success: %s (%d bones)"), *PackagePath, OrientedBones.Num());
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
            StalePackage->ClearInternalFlags(EInternalObjectFlags::AsyncLoading);
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

// ============================================================================
// Main Import
// ============================================================================

bool SAModelImporter::Import(const FString& JsonPath, const FString& TargetPackagePath, const TArray<FString>& TextureSourceDirs,
                              USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton)
{
    FSAModelData ModelData;
    if (!ParseFromFile(JsonPath, ModelData))
        return false;

    // DSAnimStudio approach: Python already merged all needed bones into Bones array.
    // No AppendModelOnlyBones needed �?Bones is the authoritative skeleton.

	FString SkeletonName = ModelData.SkeletonName;
	if (SkeletonName.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("SkeletonName is empty in JSON")); return false; }
    FString MeshName = SkeletonName.Replace(TEXT("_Skeleton"), TEXT("_Model"));
    FString SkeletonPackagePath = TargetPackagePath / SkeletonName;
    FString MeshPackagePath     = TargetPackagePath / MeshName;

    OutSkeleton = BuildSkeleton(ModelData.Bones, SkeletonName, SkeletonPackagePath);
    if (!OutSkeleton) { UE_LOG(LogTemp, Error, TEXT("Skeleton build failed")); return false; }

    OutSkeletalMesh = BuildSkeletalMesh(ModelData, OutSkeleton, MeshPackagePath);
    if (!OutSkeletalMesh) { UE_LOG(LogTemp, Error, TEXT("SkeletalMesh build failed")); return false; }

    // Build materials: uses TargetPackagePath as base ? Materials/ and Textures/ subfolders
    TArray<UMaterial*> Materials = SAMaterialImporter::BuildAll(ModelData, OutSkeletalMesh, TargetPackagePath, TextureSourceDirs);
    UE_LOG(LogTemp, Log, TEXT("Materials built: %d"), Materials.Num());

    return true;
}

