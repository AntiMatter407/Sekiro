#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LuaDataIRLibrary.generated.h"

class UChooserTable;

/** 将任意无参 Lua 编译函数的纯值结果转换为稳定 Canonical JSON。 */
UCLASS()
class LUAANIMBLUEPRINTEDITOR_API ULuaDataIRLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Lua 纯值 IR ───────────────────────────────────────────
    /** 调用指定 Lua 模块函数并将返回的纯值树严格转换为 Canonical JSON。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Data IR")
    static bool CompileLuaDataIRToCanonicalJson(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        FString& OutCanonicalJson,
        FString& OutError);

    /** 为 Python 调用稳定返回成功状态、Canonical JSON 和错误文本。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Data IR")
    static void CompileLuaDataIRToCanonicalJsonDetailed(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        bool& bSuccess,
        FString& OutCanonicalJson,
        FString& OutError);

    // ── 通用 UObject 属性 IR ──────────────────────────────────
    /** 从 Lua 通用强类型 IR 递归覆盖现有 UObject 的指定属性，可选择保存目标包。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Data IR")
    static bool CompileLuaObjectPropertyPatchAsset(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        bool bSavePackage,
        UObject*& OutObject,
        FString& OutError);

    /** 为 Python 调用稳定返回成功状态、目标对象和错误文本。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Data IR")
    static void CompileLuaObjectPropertyPatchAssetDetailed(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        bool bSavePackage,
        bool& bSuccess,
        UObject*& OutObject,
        FString& OutError);

    // ── 通用 Chooser IR ───────────────────────────────────────
    /** 从 Lua 通用 IR 全量物化 Chooser Table，可选择保存目标包。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Chooser")
    static bool CompileLuaChooserTableAsset(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        bool bSavePackage,
        UChooserTable*& OutChooserTable,
        FString& OutError);

    /** 为 Python 调用稳定返回成功状态、Chooser Table 和错误文本。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Lua|Chooser")
    static void CompileLuaChooserTableAssetDetailed(
        const FString& LuaModuleName,
        const FString& CompileFunctionName,
        bool bSavePackage,
        bool& bSuccess,
        UChooserTable*& OutChooserTable,
        FString& OutError);
};
