#pragma once

#include "Blueprint/BlueprintExtension.h"
#include "CoreMinimal.h"
#include "LuaAnimGraphIR.h"

#include "LuaAnimBlueprintExtension.generated.h"

class UAnimBlueprint;

UENUM(BlueprintType)
enum class ELuaAnimBlueprintCompileStatus : uint8
{
    NeverCompiled,
    OutOfDate,
    UpToDate,
    Error,
};

UENUM(BlueprintType)
enum class ELuaAnimBlueprintSourceMode : uint8
{
    NativeBlueprint UMETA(DisplayName = "Native Blueprint"),
    Lua UMETA(DisplayName = "Lua"),
};

/** AnimBlueprint Graph 与 Lua 交换模块相对最近成功同步点的状态。 */
UENUM(BlueprintType)
enum class ELuaAnimBlueprintSyncStatus : uint8
{
    NeverSynchronized,
    InSync,
    BlueprintChanged,
    LuaChanged,
    BothChanged,
    Error,
};

/** 保存标准 UAnimBlueprint 对应的 Lua 源模块和编辑器编译元数据。 */
UCLASS(BlueprintType)
class LUAANIMBLUEPRINTEDITOR_API ULuaAnimBlueprintExtension : public UBlueprintExtension
{
    GENERATED_BODY()

public:
    /** 当前原生图生成器版本；用于显式 Lua → AnimBlueprint 同步时识别旧资产。 */
    static constexpr int32 CurrentCompilerVersion = 4;

    // ── 资产身份 ──────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    FString LuaModuleName; // Lua 动画蓝图模块名，不是文件系统路径

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    FString GeneratedLuaModuleName; // 独立交换模块名；为空时兼容读取 LuaModuleName

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "SourceMode is retained only for old asset serialization and no longer affects Compile/F7 or Lua synchronization."))
    ELuaAnimBlueprintSourceMode SourceMode =
        ELuaAnimBlueprintSourceMode::NativeBlueprint; // 仅供旧资产反序列化兼容，任何行为不再读取

    // ── 编译状态 ──────────────────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 CompilerVersion = CurrentCompilerVersion; // 最近成功使用的生成器架构版本

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SuccessfulCompileRevision = 0; // 当前资产成功原地编译次数

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SourceRevision = 0; // 已观察到的 Lua 源修订号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SuccessfulSourceRevision = 0; // 最近成功编译采用的 Lua 源修订号

    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 LastCheckedSourceRevision = INDEX_NONE; // 最近成功 Check Lua 对应的源码修订号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    bool bSourceDirty = true; // Lua 源是否尚未生成到当前 Graph

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    ELuaAnimBlueprintCompileStatus CompileStatus =
        ELuaAnimBlueprintCompileStatus::NeverCompiled; // 最近一次编译状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    FString LastCompileMessage; // 最近一次编译结果摘要

    UPROPERTY(Transient)
    FLuaAnimBlueprintIR LastSuccessfulIR; // 最近一次成功 Check Lua 生成的 IR 缓存

    UPROPERTY(Transient)
    bool bHasLastSuccessfulIR = false; // 是否存在可用于失败恢复的成功 IR

    UPROPERTY()
    TArray<FName> GeneratedVariableNames; // 当前 AnimGraph 中由 Lua 生成器拥有的成员变量名

    UPROPERTY()
    FLuaAnimBlueprintIR LastGeneratedIR; // 最近一次实际写入原生 Graph 的 IR，用作增量合并所有权基线

    UPROPERTY()
    bool bHasLastGeneratedIR = false; // 是否已有可跨编辑器会话使用的 Lua 生成所有权基线

    // ── 双向同步状态 ──────────────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint|Synchronization")
    ELuaAnimBlueprintSyncStatus SyncStatus =
        ELuaAnimBlueprintSyncStatus::NeverSynchronized; // 当前 Graph/Lua 相对最近同步点的状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint|Synchronization")
    FString LastSynchronizedBlueprintHash; // 最近成功同步后的 Blueprint Canonical IR 哈希

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint|Synchronization")
    FString LastSynchronizedLuaHash; // 最近成功同步后的 Lua Canonical IR 哈希

    /** 查找资产已有的 Lua 动画蓝图扩展。 */
    static ULuaAnimBlueprintExtension* Find(const UAnimBlueprint* AnimBlueprint);

    /** 查找当前资产或最近父 AnimBlueprint 提供的有效 Lua 源扩展。 */
    static const ULuaAnimBlueprintExtension* FindEffective(
        const UAnimBlueprint* AnimBlueprint,
        bool* bOutInherited = nullptr);

    /** 获取或创建资产唯一的 Lua 动画蓝图扩展。 */
    static ULuaAnimBlueprintExtension* Request(UAnimBlueprint* AnimBlueprint);

    /** 返回交换模块名；旧资产没有独立字段时回退到运行时模块名。 */
    FString GetExchangeLuaModuleName() const;

    /** 记录一次成功的 Lua 模式原生编译。 */
    void MarkCompileSucceeded();

    /** 记录一次失败编译。 */
    void MarkCompileFailed(const FString& Message);

    /** 记录 Lua 源发生变化并保持现有生成类可运行。 */
    void MarkSourceDirty(const FString& Message);

    /** 记录文件监听观察到 Lua 改变，不读取源码或触发导入。 */
    void MarkLuaChanged(const FString& Message);

    /** 保存一次成功双向同步的两个 Canonical IR 哈希。 */
    void MarkSynchronized(const FString& BlueprintHash, const FString& LuaHash);

    /** 缓存一次成功的 Lua 检查结果。 */
    void MarkCheckSucceeded(const FLuaAnimBlueprintIR& CheckedIR);

    /** 记录一次 Lua 检查失败。 */
    void MarkCheckFailed(const FString& Message);

};
