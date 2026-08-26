#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroAnimGraphIR.h"

#include "SekiroAnimBlueprintFactoryLibrary.generated.h"

class UAnimBlueprint;
class USkeletalMesh;

/** 将已验证 AnimBlueprint IR 物化为 UE 原生编辑器资产。 */
UCLASS()
class SEKIROANIMBLUEPRINTEXTEDITOR_API USekiroAnimBlueprintFactoryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 在 transient package 中创建并编译原生 AnimBlueprint。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static UAnimBlueprint* CreateTransientAnimBlueprint(
        const FSekiroAnimBlueprintIR& Blueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 在指定内容包目录创建并编译原生 AnimBlueprint；已有资产不会被覆盖。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static UAnimBlueprint* CreateAnimBlueprintAsset(
        const FSekiroAnimBlueprintIR& Blueprint,
        const FString& PackagePath,
        const FString& AssetName,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 从 Lua 模块一键编译、创建并保存新的原生 AnimBlueprint 资产。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static UAnimBlueprint* CompileLuaModuleToAnimBlueprintAsset(
        const FString& LuaModuleName,
        const FString& PackagePath,
        const FString& AssetName,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 创建或更新 SkeletalMesh 的 Mesh Socket，并同步保存资产。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Skeletal Mesh")
    static bool UpsertSkeletalMeshSocket(
        USkeletalMesh* SkeletalMesh,
        FName SocketName,
        FName BoneName,
        const FTransform& RelativeTransform,
        FString& OutError);

    /** 将已有标准 AnimBlueprint 配置为由指定 Lua 模块管理。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool ConfigureLuaAnimBlueprintSource(
        UAnimBlueprint* AnimBlueprint,
        const FString& LuaModuleName);

    /** 只检查 Lua 源并缓存最近成功 IR，不修改 Graph 或调用原生编译。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool CheckLuaAnimBlueprint(
        UAnimBlueprint* AnimBlueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 使用当前有效 IR 事务性重建 Graph，不调用原生编译或保存资产。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool GenerateLuaAnimBlueprintGraph(
        UAnimBlueprint* AnimBlueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 执行 Check、Generate、一次原生编译并可选保存同一个 AnimBlueprint。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool CompileLuaAnimBlueprintInPlace(
        UAnimBlueprint* AnimBlueprint,
        bool bSavePackage,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 将全部已加载 Lua AnimBlueprint 标记为源已过期，不执行编译。 */
    static int32 MarkLoadedLuaAnimBlueprintsDirty(const FString& Reason);

    /** 只读地将现有标准 AnimBlueprint 转换为完整、规范化 IR。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool ReadAnimBlueprintToIR(
        const UAnimBlueprint* AnimBlueprint,
        FSekiroAnimBlueprintIR& OutBlueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 计算包含 Layout Positions 的 Canonical IR 稳定哈希。 */
    static bool ComputeCanonicalIRHash(
        const FSekiroAnimBlueprintIR& Blueprint,
        FString& OutHash,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 只读读取 Graph 与 Lua IR 并刷新同步状态，不修改 Graph 或文件。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool RefreshLuaAnimBlueprintSyncStatus(
        UAnimBlueprint* AnimBlueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);

    /** 将标准 AnimBlueprint 安全写为独立 generated Lua 交换模块。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Factory")
    static bool AnimBlueprintToLua(
        UAnimBlueprint* AnimBlueprint,
        FString& OutGeneratedModuleName,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics,
        bool bKeepBackup = true);

};
