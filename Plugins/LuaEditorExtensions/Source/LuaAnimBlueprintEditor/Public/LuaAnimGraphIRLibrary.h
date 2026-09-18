#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaAnimGraphIR.h"

#include "LuaAnimGraphIRLibrary.generated.h"

/** 为 Lua、蓝图与 C++ 提供编辑器期 IR 规范化和验证入口。 */
UCLASS()
class LUAANIMBLUEPRINTEDITOR_API ULuaAnimGraphIRLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── IR 处理 ───────────────────────────────────────────────
    /** 调用 Lua 模块的 CompileIR 并导入、验证规范 AnimBlueprint IR。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Animation|IR")
    static bool CompileLuaModule(
        const FString& LuaModuleName,
        FLuaAnimBlueprintIR& OutBlueprint,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics);

    /** 将 IR 原地转换为确定性顺序。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Animation|IR")
    static void Canonicalize(UPARAM(ref) FLuaAnimBlueprintIR& Blueprint);

    /** 验证 IR 并返回结构化诊断。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Animation|IR")
    static bool Validate(const FLuaAnimBlueprintIR& Blueprint, TArray<FLuaAnimIRDiagnostic>& OutDiagnostics);
};
