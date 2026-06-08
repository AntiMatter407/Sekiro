#include "SekiroModelParser.h"
#include "SekiroImportLog.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// 坐标缩放（保持Y-up，仅m→cm，旋转在Builder阶段应用）
// ============================================================================

static FVector ScaleYUpVector(const FVector& V)
{
    return V * SekiroToUEScale; // m → cm
}

static FVector3f ScaleYUpVector(const FVector3f& V)
{
    return V * SekiroToUEScale;
}

static FQuat YUpQuatIdentity(const FQuat& Q)
{
    return Q; // Y-up四元数保持不变，旋转在Builder施加
}

// ============================================================================
// MTD → BlendMode 推导（对齐 Blender 管线 CLOTH_KEYWORDS 逻辑）
// ============================================================================

/// 从MTD路径推导BlendMode（当MTDInfo.BlendMode为空时的回退）
static FString DeriveBlendModeFromMTD(const FString& MatName, const FString& MTDPath)
{
    if (MTDPath.IsEmpty()) return FString();

    const FString MtdBase = FPaths::GetBaseFilename(MTDPath).ToLower();
    const FString MatLower = MatName.ToLower();

    const bool bIsCloth = SekiroContainsClothKeyword(MatLower) || MtdBase.Contains(TEXT("cloth"));
    const bool bIsDecal = MtdBase.Contains(TEXT("decal"));

    if (bIsCloth)  return TEXT("Masked");
    if (bIsDecal)  return TEXT("Translucent");
    return TEXT("Opaque");
}

// 保持旧函数名兼容
FVector FSekiroModelParser::SekiroToUnrealVector(const FVector& V) { return ScaleYUpVector(V); }
FVector3f FSekiroModelParser::SekiroToUnrealVector(const FVector3f& V) { return ScaleYUpVector(V); }
FQuat FSekiroModelParser::SekiroToUnrealQuat(const FQuat& Q) { return YUpQuatIdentity(Q); }

// ============================================================================
// JSON基础类型解析
// ============================================================================

FVector FSekiroModelParser::ParseVector3(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 3)
    {
        return FVector(
            (float)Arr[0]->AsNumber(),
            (float)Arr[1]->AsNumber(),
            (float)Arr[2]->AsNumber()
        );
    }
    return FVector::ZeroVector;
}

FVector3f FSekiroModelParser::ParseVector3f(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 3)
    {
        return FVector3f(
            (float)Arr[0]->AsNumber(),
            (float)Arr[1]->AsNumber(),
            (float)Arr[2]->AsNumber()
        );
    }
    return FVector3f::ZeroVector;
}

FQuat FSekiroModelParser::ParseQuat(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 4)
    {
        // JSON中四元数为xyzw顺序，与UE FQuat一致
        return FQuat(
            (float)Arr[0]->AsNumber(),
            (float)Arr[1]->AsNumber(),
            (float)Arr[2]->AsNumber(),
            (float)Arr[3]->AsNumber()
        );
    }
    return FQuat::Identity;
}

FVector2f FSekiroModelParser::ParseVector2f(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 2)
    {
        return FVector2f(
            (float)Arr[0]->AsNumber(),
            (float)Arr[1]->AsNumber()
        );
    }
    return FVector2f::ZeroVector;
}

// ============================================================================
// 骨骼解析
// ============================================================================

void FSekiroModelParser::ParseBones(const TArray<TSharedPtr<FJsonValue>>& BonesArray, TArray<FSekiroImportBone>& OutBones)
{
    OutBones.Reserve(BonesArray.Num());

    for (const TSharedPtr<FJsonValue>& BoneVal : BonesArray)
    {
        const TSharedPtr<FJsonObject>* BoneObjPtr = nullptr;
        if (!BoneVal->TryGetObject(BoneObjPtr))
        {
            continue;
        }
        const TSharedPtr<FJsonObject>& Obj = *BoneObjPtr;

        FSekiroImportBone Bone;
        Bone.Name = FName(*Obj->GetStringField(TEXT("Name")));

        // ParentName: JSON中root骨骼为null
        FString ParentName;
        if (Obj->TryGetStringField(TEXT("ParentName"), ParentName) && !ParentName.IsEmpty())
        {
            Bone.ParentName = FName(*ParentName);
        }

        // 使用World变换（quaternion，更可靠），而非Local Euler角
        // WorldRot已是xyzw四元数，WorldPos是位移
        FVector WorldPos = ParseVector3(Obj->GetArrayField(TEXT("WorldPos")));
        FQuat WorldRot = ParseQuat(Obj->GetArrayField(TEXT("WorldRot")));
        FVector WorldScale = ParseVector3(Obj->GetArrayField(TEXT("WorldScale")));

        // 坐标系转换
        Bone.WorldTranslation = SekiroToUnrealVector(WorldPos);
        Bone.WorldRotation = SekiroToUnrealQuat(WorldRot);
        Bone.WorldScale = WorldScale; // Scale不变

        // 保留Local变换作为备用（后续Build阶段会从World变换推导Local变换）
        FVector LocalPos = ParseVector3(Obj->GetArrayField(TEXT("LocalPos")));
        FVector LocalScale = ParseVector3(Obj->GetArrayField(TEXT("LocalScale")));
        Bone.LocalTranslation = SekiroToUnrealVector(LocalPos);
        Bone.LocalScale = LocalScale;

        // LocalRot是Euler角(XZY顺序)，暂不转换。仅在无动画JSON时作为骨架的fallback使用
        // 正常流程中LocalRotation会在SkeletonBuilder中从WorldRot推导

        OutBones.Add(MoveTemp(Bone));
    }

    // 解析ParentName → ParentIndex
    TMap<FName, int32> NameToIndex;
    for (int32 i = 0; i < OutBones.Num(); ++i)
    {
        NameToIndex.Add(OutBones[i].Name, i);
    }

    for (FSekiroImportBone& Bone : OutBones)
    {
        if (!Bone.ParentName.IsNone())
        {
            if (int32* ParentIdx = NameToIndex.Find(Bone.ParentName))
            {
                Bone.ParentIndex = *ParentIdx;
            }
            else
            {
                UE_LOG(LogSekiroImport, Error, TEXT("骨骼 '%s' 的父骨骼 '%s' 未找到"), *Bone.Name.ToString(), *Bone.ParentName.ToString());
                Bone.ParentIndex = INDEX_NONE;
            }
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("解析了 %d 根骨骼"), OutBones.Num());
}

// ============================================================================
// 材质解析
// ============================================================================

void FSekiroModelParser::ParseMaterials(const TArray<TSharedPtr<FJsonValue>>& MatsArray, TArray<FSekiroImportMaterial>& OutMaterials)
{
    OutMaterials.Reserve(MatsArray.Num());

    for (const TSharedPtr<FJsonValue>& MatVal : MatsArray)
    {
        const TSharedPtr<FJsonObject>* MatObjPtr = nullptr;
        if (!MatVal->TryGetObject(MatObjPtr))
        {
            continue;
        }
        const TSharedPtr<FJsonObject>& Obj = *MatObjPtr;

        FSekiroImportMaterial Mat;
        Mat.Name = Obj->GetStringField(TEXT("Name"));
        Obj->TryGetStringField(TEXT("MTD"), Mat.MTDPath);

        // Textures数组
        const TArray<TSharedPtr<FJsonValue>>* TexturesArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("Textures"), TexturesArray))
        {
            for (const TSharedPtr<FJsonValue>& TexVal : *TexturesArray)
            {
                const TSharedPtr<FJsonObject>* TexObjPtr = nullptr;
                if (!TexVal->TryGetObject(TexObjPtr))
                {
                    continue;
                }
                const TSharedPtr<FJsonObject>& TexObj = *TexObjPtr;

                FString ParamName = TexObj->GetStringField(TEXT("ParamName"));
                FString Path;
                TexObj->TryGetStringField(TEXT("Path"), Path);
                Mat.TextureSlots.Add(ParamName, Path);
            }
        }

        // MTDInfo（可能为null）
        const TSharedPtr<FJsonObject>* MTDInfoObjPtr = nullptr;
        if (Obj->TryGetObjectField(TEXT("MTDInfo"), MTDInfoObjPtr) && *MTDInfoObjPtr)
        {
            const TSharedPtr<FJsonObject>& MTDInfo = *MTDInfoObjPtr;
            MTDInfo->TryGetStringField(TEXT("ShaderPath"), Mat.ShaderPath);
            MTDInfo->TryGetStringField(TEXT("BlendMode"), Mat.BlendMode);
        }

        // MTD→BlendMode 推导（当MTDInfo为空时的回退）
        if (Mat.BlendMode.IsEmpty() && !Mat.MTDPath.IsEmpty())
        {
            Mat.BlendMode = DeriveBlendModeFromMTD(Mat.Name, Mat.MTDPath);
        }

        // AvailableTextures
        const TArray<TSharedPtr<FJsonValue>>* AvailTexArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("AvailableTextures"), AvailTexArray))
        {
            for (const TSharedPtr<FJsonValue>& AvailTex : *AvailTexArray)
            {
                Mat.AvailableTextures.Add(AvailTex->AsString());
            }
        }

        OutMaterials.Add(MoveTemp(Mat));
    }

    UE_LOG(LogSekiroImport, Log, TEXT("解析了 %d 个材质"), OutMaterials.Num());
}

// ============================================================================
// 顶点解析
// ============================================================================

FSekiroImportVertex FSekiroModelParser::ParseVertex(const TSharedPtr<FJsonObject>& VertObj)
{
    FSekiroImportVertex Vert;

    // 位置 + 坐标系转换
    Vert.Position = SekiroToUnrealVector(
        ParseVector3f(VertObj->GetArrayField(TEXT("Pos")))
    );

    // 法线 + 坐标系转换
    Vert.Normal = SekiroToUnrealVector(
        ParseVector3f(VertObj->GetArrayField(TEXT("Normal")))
    );
    Vert.Normal.Normalize();

    // UV
    Vert.UV = ParseVector2f(VertObj->GetArrayField(TEXT("UV")));

    // 骨骼索引（局部索引，后续通过BoneIdxToName重映射到完整骨架）
    const TArray<TSharedPtr<FJsonValue>>& BoneIdxArr = VertObj->GetArrayField(TEXT("BoneIndices"));
    for (int32 i = 0; i < FMath::Min(4, BoneIdxArr.Num()); ++i)
    {
        Vert.BoneIndices[i] = (uint16)BoneIdxArr[i]->AsNumber();
    }

    // 骨骼权重
    const TArray<TSharedPtr<FJsonValue>>& BoneWeightArr = VertObj->GetArrayField(TEXT("BoneWeights"));
    float WeightSum = 0.0f;
    for (int32 i = 0; i < FMath::Min(4, BoneWeightArr.Num()); ++i)
    {
        Vert.BoneWeights[i] = (float)BoneWeightArr[i]->AsNumber();
        WeightSum += Vert.BoneWeights[i];
    }

    // 归一化权重（处理浮点精度导致的微小偏差）
    if (WeightSum > 0.0f && FMath::Abs(WeightSum - 1.0f) > KINDA_SMALL_NUMBER)
    {
        for (int32 i = 0; i < 4; ++i)
        {
            Vert.BoneWeights[i] /= WeightSum;
        }
    }

    return Vert;
}

// ============================================================================
// 网格体解析
// ============================================================================

void FSekiroModelParser::ParseMeshes(const TArray<TSharedPtr<FJsonValue>>& MeshesArray, TArray<FSekiroImportMeshSection>& OutMeshes)
{
    OutMeshes.Reserve(MeshesArray.Num());

    for (const TSharedPtr<FJsonValue>& MeshVal : MeshesArray)
    {
        const TSharedPtr<FJsonObject>* MeshObjPtr = nullptr;
        if (!MeshVal->TryGetObject(MeshObjPtr))
        {
            continue;
        }
        const TSharedPtr<FJsonObject>& Obj = *MeshObjPtr;

        FSekiroImportMeshSection Section;
        Obj->TryGetStringField(TEXT("Part"), Section.PartName);
        Section.MaterialIndex = (int32)Obj->GetNumberField(TEXT("MaterialIndex"));

        // BoneIdxToName: JSON key为字符串数字（如"0", "1"），value为骨骼名
        const TSharedPtr<FJsonObject>* BoneMapObjPtr = nullptr;
        if (Obj->TryGetObjectField(TEXT("BoneIdxToName"), BoneMapObjPtr))
        {
            const TSharedPtr<FJsonObject>& BoneMap = *BoneMapObjPtr;
            for (const auto& Pair : BoneMap->Values)
            {
                int32 LocalIdx = FCString::Atoi(*Pair.Key);
                Section.BoneIdxToName.Add(LocalIdx, FName(*Pair.Value->AsString()));
            }
        }

        // 顶点
        int32 TotalVertices = 0;
        const TArray<TSharedPtr<FJsonValue>>* VertsArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("Vertices"), VertsArray))
        {
            Section.Vertices.Reserve(VertsArray->Num());
            for (const TSharedPtr<FJsonValue>& VertVal : *VertsArray)
            {
                const TSharedPtr<FJsonObject>* VertObjPtr = nullptr;
                if (VertVal->TryGetObject(VertObjPtr))
                {
                    Section.Vertices.Add(ParseVertex(*VertObjPtr));
                }
            }
            TotalVertices += Section.Vertices.Num();
        }

        // 三角形
        const TArray<TSharedPtr<FJsonValue>>* TrisArray = nullptr;
        if (Obj->TryGetArrayField(TEXT("Triangles"), TrisArray))
        {
            Section.Triangles.Reserve(TrisArray->Num());
            for (const TSharedPtr<FJsonValue>& TriVal : *TrisArray)
            {
                const TArray<TSharedPtr<FJsonValue>>& TriArr = TriVal->AsArray();
                if (TriArr.Num() >= 3)
                {
                    Section.Triangles.Add(FIntVector(
                        (int32)TriArr[0]->AsNumber(),
                        (int32)TriArr[1]->AsNumber(),
                        (int32)TriArr[2]->AsNumber()
                    ));
                }
            }
        }

        UE_LOG(LogSekiroImport, Verbose, TEXT("  Mesh '%s': %d 顶点, %d 三角形, %d 蒙皮骨骼"),
            *Section.PartName, Section.Vertices.Num(), Section.Triangles.Num(), Section.BoneIdxToName.Num());

        OutMeshes.Add(MoveTemp(Section));
    }

    UE_LOG(LogSekiroImport, Log, TEXT("解析了 %d 个网格体Section"), OutMeshes.Num());
}

// ============================================================================
// 主解析入口
// ============================================================================

bool FSekiroModelParser::ParseFromFile(const FString& FilePath, FSekiroModelData& OutData)
{
    UE_LOG(LogSekiroImport, Log, TEXT("开始解析模型JSON: %s"), *FilePath);

    // 读取文件
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法读取模型JSON文件: %s"), *FilePath);
        return false;
    }

    UE_LOG(LogSekiroImport, Log, TEXT("文件大小: %.1f MB"), JsonString.Len() / (1024.0f * 1024.0f));

    // 解析JSON
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogSekiroImport, Error, TEXT("JSON解析失败: %s"), *FilePath);
        return false;
    }

    // SkeletonName
    RootObject->TryGetStringField(TEXT("SkeletonName"), OutData.SkeletonName);

    // 骨骼
    const TArray<TSharedPtr<FJsonValue>>* BonesArray = nullptr;
    if (RootObject->TryGetArrayField(TEXT("Bones"), BonesArray))
    {
        ParseBones(*BonesArray, OutData.Bones);
    }
    else
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("未找到Bones数组"));
    }

    // 材质
    const TArray<TSharedPtr<FJsonValue>>* MatsArray = nullptr;
    if (RootObject->TryGetArrayField(TEXT("Materials"), MatsArray))
    {
        ParseMaterials(*MatsArray, OutData.Materials);
    }
    else
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("未找到Materials数组"));
    }

    // 网格体
    const TArray<TSharedPtr<FJsonValue>>* MeshesArray = nullptr;
    if (RootObject->TryGetArrayField(TEXT("Meshes"), MeshesArray))
    {
        ParseMeshes(*MeshesArray, OutData.Meshes);
    }
    else
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("未找到Meshes数组"));
    }

    UE_LOG(LogSekiroImport, Log, TEXT("模型JSON解析完成: %d骨骼, %d材质, %d网格体Section"),
        OutData.Bones.Num(), OutData.Materials.Num(), OutData.Meshes.Num());

    return true;
}
