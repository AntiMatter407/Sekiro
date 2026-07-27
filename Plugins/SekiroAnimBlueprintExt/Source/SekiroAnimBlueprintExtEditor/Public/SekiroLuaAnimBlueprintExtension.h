#pragma once

#include "Blueprint/BlueprintExtension.h"
#include "CoreMinimal.h"
#include "SekiroAnimGraphIR.h"

#include "SekiroLuaAnimBlueprintExtension.generated.h"

class UAnimBlueprint;

UENUM(BlueprintType)
enum class ESekiroLuaAnimBlueprintCompileStatus : uint8
{
    NeverCompiled,
    OutOfDate,
    UpToDate,
    Error,
};

UENUM(BlueprintType)
enum class ESekiroLuaAnimBlueprintSourceMode : uint8
{
    NativeBlueprint UMETA(DisplayName = "Native Blueprint"),
    Lua UMETA(DisplayName = "Lua"),
};

/** 保存标准 UAnimBlueprint 对应的 Lua 源模块和编辑器编译元数据。 */
UCLASS(BlueprintType)
class SEKIROANIMBLUEPRINTEXTEDITOR_API USekiroLuaAnimBlueprintExtension : public UBlueprintExtension
{
    GENERATED_BODY()

public:
    /** 当前原生图生成器版本；变更时已加载旧资产会在 PIE 前重新编译。 */
    static constexpr int32 CurrentCompilerVersion = 2;

    // ── 资产身份 ──────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    FString LuaModuleName; // Lua 动画蓝图模块名，不是文件系统路径

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    ESekiroLuaAnimBlueprintSourceMode SourceMode =
        ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint; // 普通 Compile/F7 使用的源码模式

    // ── 编译状态 ──────────────────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 CompilerVersion = CurrentCompilerVersion; // 最近成功使用的生成器架构版本

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SuccessfulCompileRevision = 0; // 当前资产成功原地编译次数

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SourceRevision = 0; // 已观察到的 Lua 源修订号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 SuccessfulSourceRevision = 0; // 最近成功编译采用的 Lua 源修订号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    int32 LastCheckedSourceRevision = INDEX_NONE; // 最近成功 Check Lua 对应的源码修订号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    bool bSourceDirty = true; // Lua 源是否尚未生成到当前 Graph

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    ESekiroLuaAnimBlueprintCompileStatus CompileStatus =
        ESekiroLuaAnimBlueprintCompileStatus::NeverCompiled; // 最近一次编译状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lua Anim Blueprint")
    FString LastCompileMessage; // 最近一次编译结果摘要

    UPROPERTY()
    FSekiroAnimBlueprintIR LastSuccessfulIR; // 最近一次成功 Check Lua 生成的 IR 缓存

    UPROPERTY()
    bool bHasLastSuccessfulIR = false; // 是否存在可用于失败恢复的成功 IR

    UPROPERTY()
    TArray<FName> GeneratedVariableNames; // 当前 AnimGraph 中由 Lua 生成器拥有的成员变量名

    /** 查找资产已有的 Lua 动画蓝图扩展。 */
    static USekiroLuaAnimBlueprintExtension* Find(const UAnimBlueprint* AnimBlueprint);

    /** 查找当前资产或最近父 AnimBlueprint 提供的有效 Lua 源扩展。 */
    static const USekiroLuaAnimBlueprintExtension* FindEffective(
        const UAnimBlueprint* AnimBlueprint,
        bool* bOutInherited = nullptr);

    /** 获取或创建资产唯一的 Lua 动画蓝图扩展。 */
    static USekiroLuaAnimBlueprintExtension* Request(UAnimBlueprint* AnimBlueprint);

    /** 缓存一次成功的 Lua 检查结果。 */
    void MarkCheckSucceeded(const FSekiroAnimBlueprintIR& CheckedIR);

    /** 记录一次 Lua 检查失败。 */
    void MarkCheckFailed(const FString& Message);

    /** 记录一次成功的 Lua 模式原生编译。 */
    void MarkCompileSucceeded();

    /** 记录一次失败编译。 */
    void MarkCompileFailed(const FString& Message);

    /** 记录 Lua 源发生变化并保持现有生成类可运行。 */
    void MarkSourceDirty(const FString& Message);

};
