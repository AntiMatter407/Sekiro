#include "SekiroSkeletonBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"

// ============================================================================
// World->Local推导
// ============================================================================

void FSekiroSkeletonBuilder::DeriveLocalFromWorld(TArray<FSekiroImportBone>& Bones)
{
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        FSekiroImportBone& Bone = Bones[i];

        if (Bone.ParentIndex != INDEX_NONE && Bone.ParentIndex < Bones.Num())
        {
            const FSekiroImportBone& Parent = Bones[Bone.ParentIndex];

            FTransform ParentWorld;
            ParentWorld.SetRotation(Parent.WorldRotation);
            ParentWorld.SetTranslation(Parent.WorldTranslation);
            ParentWorld.SetScale3D(Parent.WorldScale);

            FTransform World;
            World.SetRotation(Bone.WorldRotation);
            World.SetTranslation(Bone.WorldTranslation);
            World.SetScale3D(Bone.WorldScale);

            FTransform Local = World.GetRelativeTransform(ParentWorld);
            Bone.LocalRotation = Local.GetRotation();
            Bone.LocalTranslation = Local.GetTranslation();
            Bone.LocalScale = Local.GetScale3D();
        }
        else
        {
            // Root骨骼：Local == World
            Bone.LocalRotation = Bone.WorldRotation;
            Bone.LocalTranslation = Bone.WorldTranslation;
            Bone.LocalScale = Bone.WorldScale;
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("已从World变换推导 %d 根骨骼的Local变换"), Bones.Num());
}

// ============================================================================
// 合并模型WorldPos到动画骨骼
// ============================================================================

void FSekiroSkeletonBuilder::MergeModelWorldTransforms(TArray<FSekiroImportBone>& AnimBones, const TArray<FSekiroImportBone>& ModelBones)
{
    // 构建模型骨骼名->WorldTransform查找表
    TMap<FName, FTransform> ModelWorldMap;
    for (const FSekiroImportBone& B : ModelBones)
    {
        FTransform T;
        T.SetRotation(B.WorldRotation);
        T.SetTranslation(B.WorldTranslation);
        T.SetScale3D(B.WorldScale);
        ModelWorldMap.Add(B.Name, T);
    }
    UE_LOG(LogSekiroImport, Log, TEXT("MergeModelWorld: 模型有 %d 根骨骼 (WorldPos)"), ModelWorldMap.Num());

    // 第一步: 对于模型中存在的骨骼，用模型的WorldTransform覆盖World变换
    int32 ReplacedCount = 0;
    for (FSekiroImportBone& B : AnimBones)
    {
        if (const FTransform* World = ModelWorldMap.Find(B.Name))
        {
            B.WorldRotation = World->GetRotation();
            B.WorldTranslation = World->GetTranslation();
            B.WorldScale = World->GetScale3D();
            ++ReplacedCount;
        }
    }
    UE_LOG(LogSekiroImport, Log, TEXT("MergeModelWorld: %d 根骨骼从模型获取WorldPos"), ReplacedCount);

    // 第二步: 对于仅在动画中存在（不在模型中）的骨骼（IK Target等），通过FK计算WorldTransform
    // 骨骼已按层级顺序排列（父<子），可从前向后计算
    int32 FKCount = 0;
    for (int32 i = 0; i < AnimBones.Num(); ++i)
    {
        FSekiroImportBone& B = AnimBones[i];

        // 如果已从模型获取WorldPos，跳过
        if (ModelWorldMap.Contains(B.Name))
            continue;

        // 通过FK计算WorldTransform
        FTransform Local;
        Local.SetRotation(B.LocalRotation);
        Local.SetTranslation(B.LocalTranslation);
        Local.SetScale3D(B.LocalScale);

        FTransform ParentWorld = FTransform::Identity;
        if (B.ParentIndex >= 0 && B.ParentIndex < AnimBones.Num())
        {
            const FSekiroImportBone& Parent = AnimBones[B.ParentIndex];
            ParentWorld.SetRotation(Parent.WorldRotation);
            ParentWorld.SetTranslation(Parent.WorldTranslation);
            ParentWorld.SetScale3D(Parent.WorldScale);
        }

        FTransform World = Local * ParentWorld;
        B.WorldRotation = World.GetRotation();
        B.WorldTranslation = World.GetTranslation();
        B.WorldScale = World.GetScale3D();
        ++FKCount;
    }
    UE_LOG(LogSekiroImport, Log, TEXT("MergeModelWorld: %d 根骨骼通过FK计算WorldPos"), FKCount);

    // Step 3: derive local from world
    DeriveLocalFromWorld(AnimBones);
    UE_LOG(LogSekiroImport, Log, TEXT("MergeModelWorld: derived local transforms for %d bones"), AnimBones.Num());

    // Step 4: verify FK world positions match model WorldPos
    int32 FKMatchErrors = 0;
    for (int32 i = 0; i < AnimBones.Num(); ++i)
    {
        const FSekiroImportBone& B = AnimBones[i];
        FTransform Local;
        Local.SetRotation(B.LocalRotation);
        Local.SetTranslation(B.LocalTranslation);
        Local.SetScale3D(B.LocalScale);

        FTransform ParentWorld = FTransform::Identity;
        if (B.ParentIndex >= 0 && B.ParentIndex < AnimBones.Num())
        {
            const FSekiroImportBone& Parent = AnimBones[B.ParentIndex];
            ParentWorld.SetRotation(Parent.WorldRotation);
            ParentWorld.SetTranslation(Parent.WorldTranslation);
            ParentWorld.SetScale3D(Parent.WorldScale);
        }
        FTransform FK = Local * ParentWorld;

        if (const FTransform* ModelWorld = ModelWorldMap.Find(B.Name))
        {
            float PosDiff = FVector::Dist(FK.GetTranslation(), ModelWorld->GetTranslation());
            if (PosDiff > 1.0f)
            {
                UE_LOG(LogSekiroImport, Warning,
                    TEXT("  [FK mismatch] %s[%d]: FK=(%.1f,%.1f,%.1f) Model=(%.1f,%.1f,%.1f) delta=%.1fcm"),
                    *B.Name.ToString(), i,
                    FK.GetTranslation().X, FK.GetTranslation().Y, FK.GetTranslation().Z,
                    ModelWorld->GetTranslation().X, ModelWorld->GetTranslation().Y, ModelWorld->GetTranslation().Z,
                    PosDiff);
                ++FKMatchErrors;
            }
        }
    }
    if (FKMatchErrors > 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("MergeModelWorld: %d bones have FK/model mismatch (>1cm)!"), FKMatchErrors);
    }
    else
    {
        UE_LOG(LogSekiroImport, Log, TEXT("MergeModelWorld: FK verification passed, all bones match model WorldPos"));
    }
}

// ============================================================================
// 层级顺序验证
// ============================================================================

bool FSekiroSkeletonBuilder::ValidateHierarchyOrder(const TArray<FSekiroImportBone>& Bones)
{
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        const FSekiroImportBone& Bone = Bones[i];
        if (Bone.ParentIndex != INDEX_NONE && Bone.ParentIndex >= i)
        {
            UE_LOG(LogSekiroImport, Error,
                TEXT("骨骼层级顺序错误: '%s' (索引 %d) 的父骨骼索引为 %d, 但父骨骼必须在子骨骼之前"),
                *Bone.Name.ToString(), i, Bone.ParentIndex);
            return false;
        }
    }
    return true;
}

// ============================================================================
// ExportRoot 方向转换
// ============================================================================

void FSekiroSkeletonBuilder::ApplyExportRootOrientation(TArray<FSekiroImportBone>& Bones)
{
    // Havok Y-up → UE5 Z-up 坐标转换，无需额外骨骼
    // RotZ(180°) * RotX(90°): 先RotX(90°)完成Y→Z转换，再RotZ(180°)翻转朝向使角色面朝+X
    const FQuat OrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);

    // 验证OrientQ: Y-up(0,1,0) → Z-up(0,0,1)
    {
        FVector TestUp = OrientQ.RotateVector(FVector(0, 1, 0));
        UE_LOG(LogSekiroImport, Warning, TEXT("[OrientQ校验] Y-up(0,1,0) → (%.2f,%.2f,%.2f) %s"),
            TestUp.X, TestUp.Y, TestUp.Z,
            FMath::IsNearlyEqual(TestUp.Z, 1.0f, 0.01f) ? TEXT("✓ 正确(Z-up)") : TEXT("✗ 错误!"));
    }

    // 直接将OrientQ应用到所有骨骼的World变换
    for (FSekiroImportBone& Bone : Bones)
    {
        Bone.WorldTranslation = OrientQ.RotateVector(Bone.WorldTranslation);
        Bone.WorldRotation = OrientQ * Bone.WorldRotation;
    }

    // 从旋转后的World变换推导Local变换
    DeriveLocalFromWorld(Bones);

    UE_LOG(LogSekiroImport, Log, TEXT("ApplyExportRootOrientation: %d 根骨骼已转换到UE5空间"), Bones.Num());
}

// ============================================================================
// 骨架构建
// ============================================================================

USkeleton* FSekiroSkeletonBuilder::Build(const TArray<FSekiroImportBone>& Bones, const FString& SkeletonName, const FString& PackagePath)
{
    if (Bones.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("骨骼数组为空，无法构建骨架"));
        return nullptr;
    }

    if (!ValidateHierarchyOrder(Bones))
    {
        return nullptr;
    }

    // 制作可变副本并施加ExportRoot方向转换
    TArray<FSekiroImportBone> OrientedBones = Bones;
    ApplyExportRootOrientation(OrientedBones);

    UE_LOG(LogSekiroImport, Log, TEXT("开始构建骨架 '%s': %d 根骨骼 -> %s"), *SkeletonName, OrientedBones.Num(), *PackagePath);

    // 创建Package
    FString PackageName = PackagePath;
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackageName);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建Package: %s"), *PackageName);
        return nullptr;
    }

    // 创建USkeleton资产（对象名 = 包名，UE约定）
    USkeleton* Skeleton = NewObject<USkeleton>(Package, USkeleton::StaticClass(), FName(*SkeletonName), RF_Public | RF_Standalone);
    if (!Skeleton)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建USkeleton: %s"), *SkeletonName);
        return nullptr;
    }

    // 添加骨骼到ReferenceSkeleton（使用方向转换后的数据）
    FReferenceSkeletonModifier Modifier(Skeleton);

    for (int32 i = 0; i < OrientedBones.Num(); ++i)
    {
        const FSekiroImportBone& Bone = OrientedBones[i];

        FMeshBoneInfo BoneInfo(Bone.Name, Bone.Name.ToString(), Bone.ParentIndex);

        FTransform BoneTransform;
        BoneTransform.SetRotation(Bone.LocalRotation);
        BoneTransform.SetTranslation(Bone.LocalTranslation);
        BoneTransform.SetScale3D(Bone.LocalScale);

        Modifier.Add(BoneInfo, BoneTransform);

        UE_LOG(LogSekiroImport, Verbose, TEXT("  添加骨骼 [%d]: %s (父: %d) Pos=(%.2f, %.2f, %.2f)"),
            i, *Bone.Name.ToString(), Bone.ParentIndex,
            Bone.LocalTranslation.X, Bone.LocalTranslation.Y, Bone.LocalTranslation.Z);
    }
    // Modifier析构时自动调用RebuildRefSkeleton

    Skeleton->MarkPackageDirty();

    // 保存Package
    FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;

    if (UPackage::SavePackage(Package, Skeleton, *PackageFileName, SaveArgs))
    {
        UE_LOG(LogSekiroImport, Log, TEXT("骨架构建成功: %s"), *PackageFileName);
    }
    else
    {
        UE_LOG(LogSekiroImport, Error, TEXT("骨架保存失败: %s"), *PackageFileName);
    }

    return Skeleton;
}

// ============================================================================
// 追加Model-Only骨骼
// ============================================================================

void FSekiroSkeletonBuilder::AppendModelOnlyBones(TArray<FSekiroImportBone>& AnimBones, const TArray<FSekiroImportBone>& ModelBones, const TSet<FName>& MeshBoneNames)
{
    // 构建动画骨骼名快速查找集
    TSet<FName> AnimBoneNames;
    for (const FSekiroImportBone& B : AnimBones)
        AnimBoneNames.Add(B.Name);

    // 找出仅存在于模型中的骨骼
    TArray<FSekiroImportBone> ModelOnlyBones;
    for (const FSekiroImportBone& B : ModelBones)
    {
        if (!AnimBoneNames.Contains(B.Name))
        {
            // 跳过未被任何网格引用的孤立零位骨骼（如HD_L/R，WorldPos=(0,0,0)且无父骨骼）
            // 被网格实际使用的零位骨骼（如オブジェクト002）必须保留，否则蒙皮缺失
            if (B.WorldTranslation.IsNearlyZero() && B.ParentName.IsNone() && !MeshBoneNames.Contains(B.Name))
            {
                UE_LOG(LogSekiroImport, Log, TEXT("AppendModelOnlyBones: 跳过孤立零位骨骼 '%s'（未被网格引用）"), *B.Name.ToString());
                continue;
            }
            ModelOnlyBones.Add(B);
        }
    }

    if (ModelOnlyBones.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Log, TEXT("AppendModelOnlyBones: 没有Model-Only骨骼"));
        return;
    }

    UE_LOG(LogSekiroImport, Log, TEXT("AppendModelOnlyBones: 找到 %d 个Model-Only骨骼"), ModelOnlyBones.Num());

    // 构建动画骨骼名→索引查找表（含后续追加的Model-Only骨骼）
    TMap<FName, int32> NameToIndex;
    for (int32 i = 0; i < AnimBones.Num(); ++i)
        NameToIndex.Add(AnimBones[i].Name, i);

    // 拓扑追加：每次循环追加父骨骼已存在的Model-Only骨骼
    TArray<FSekiroImportBone> Pending = MoveTemp(ModelOnlyBones);
    int32 AddedCount = 0;
    int32 StuckCount = 0;

    while (Pending.Num() > 0)
    {
        bool bAddedAny = false;
        TArray<FSekiroImportBone> Retry;

        for (FSekiroImportBone& Bone : Pending)
        {
            // 解析父骨骼索引
            int32* ParentIdx = nullptr;
            if (!Bone.ParentName.IsNone())
                ParentIdx = NameToIndex.Find(Bone.ParentName);

            if (ParentIdx)
            {
                Bone.ParentIndex = *ParentIdx;
            }
            else if (!Bone.ParentName.IsNone() && Bone.ParentName != FName(TEXT("Master")))
            {
                // 父骨骼尚未加入，留到下一轮
                Retry.Add(MoveTemp(Bone));
                continue;
            }
            else
            {
                // 无父骨骼或父骨骼为Master → 挂到根骨骼(索引0)
                Bone.ParentIndex = 0;
            }

            // 计算Local变换: Local = World.GetRelativeTransform(ParentWorld)
            FTransform World;
            World.SetRotation(Bone.WorldRotation);
            World.SetTranslation(Bone.WorldTranslation);
            World.SetScale3D(Bone.WorldScale);

            FTransform ParentWorld = FTransform::Identity;
            if (Bone.ParentIndex >= 0 && Bone.ParentIndex < AnimBones.Num())
            {
                const FSekiroImportBone& Parent = AnimBones[Bone.ParentIndex];
                ParentWorld.SetRotation(Parent.WorldRotation);
                ParentWorld.SetTranslation(Parent.WorldTranslation);
                ParentWorld.SetScale3D(Parent.WorldScale);
            }

            FTransform Local = World.GetRelativeTransform(ParentWorld);
            Bone.LocalRotation = Local.GetRotation();
            Bone.LocalTranslation = Local.GetTranslation();
            Bone.LocalScale = Local.GetScale3D();

            UE_LOG(LogSekiroImport, Log, TEXT("  +ModelOnly[%d] '%s' Parent='%s'(%d) WorldPos=(%.1f,%.1f,%.1f)"),
                AnimBones.Num(), *Bone.Name.ToString(),
                Bone.ParentIndex < AnimBones.Num() ? *AnimBones[Bone.ParentIndex].Name.ToString() : TEXT("Root"),
                Bone.ParentIndex,
                Bone.WorldTranslation.X, Bone.WorldTranslation.Y, Bone.WorldTranslation.Z);

            NameToIndex.Add(Bone.Name, AnimBones.Num());
            AnimBones.Add(MoveTemp(Bone));
            bAddedAny = true;
            ++AddedCount;
        }

        Pending = MoveTemp(Retry);

        if (!bAddedAny)
        {
            ++StuckCount;
            if (StuckCount > 3)
            {
                UE_LOG(LogSekiroImport, Error, TEXT("AppendModelOnlyBones: %d 个Model-Only骨骼无法解析父骨骼, 放弃"), Pending.Num());
                for (const FSekiroImportBone& B : Pending)
                    UE_LOG(LogSekiroImport, Error, TEXT("  无法挂载: '%s' Parent='%s'"), *B.Name.ToString(), *B.ParentName.ToString());
                break;
            }
            // 将剩余骨骼全部挂到Root
            for (FSekiroImportBone& Bone : Pending)
            {
                Bone.ParentIndex = 0;
                FTransform World;
                World.SetRotation(Bone.WorldRotation);
                World.SetTranslation(Bone.WorldTranslation);
                World.SetScale3D(Bone.WorldScale);
                FTransform Local = World; // Root的父是Identity
                Bone.LocalRotation = Local.GetRotation();
                Bone.LocalTranslation = Local.GetTranslation();
                Bone.LocalScale = Local.GetScale3D();
                NameToIndex.Add(Bone.Name, AnimBones.Num());
                AnimBones.Add(MoveTemp(Bone));
                ++AddedCount;
            }
            break;
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("AppendModelOnlyBones: 成功追加 %d 根Model-Only骨骼, 骨架总数=%d"), AddedCount, AnimBones.Num());
}
