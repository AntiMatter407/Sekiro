#include "SekiroGameplayTagParser.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSekiroGameplayTagHierarchyTest, "Sekiro.GameplayTools.Tags.Hierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 校验纯数据层次、父说明、转义和排序；游戏线程测试框架调用。Parameters 未使用，返回断言是否通过；不读写资产。 */
bool FSekiroGameplayTagHierarchyTest::RunTest(const FString& Parameters)
{
    TArray<FSekiroGameplayTagEntry> Tags;
    FString Error;
    const FString Source = TEXT("--[=[ 注释 ]=]\nlocal Tags = { State = { _Comment = '状态', Life = { Dead = '死亡\\n说明' }, Ready = true, Empty = {} }; ['Ability'] = '能力'; }; return Tags;");
    if (!TestTrue(TEXT("声明式局部表可解析"), FSekiroGameplayTagParser::Parse(Source, TEXT("Hierarchy.lua"), Tags, Error))) return false;
    TestEqual(TEXT("所有父节点和叶节点均生成"), Tags.Num(), 6);
    if (Tags.Num() == 6)
    {
        TestEqual(TEXT("结果按标签排序"), Tags[0].Tag, FString(TEXT("Ability")));
        TestEqual(TEXT("父节点说明来自 _Comment"), Tags[1].Comment, FString(TEXT("状态")));
        TestEqual(TEXT("层次使用点连接"), Tags[4].Tag, FString(TEXT("State.Life.Dead")));
        TestEqual(TEXT("字符串转义正确"), Tags[4].Comment, FString(TEXT("死亡\n说明")));
    }
    TestTrue(TEXT("根说明不产生标签"), FSekiroGameplayTagParser::Parse(TEXT("return { _Comment = '根' }"), TEXT("Root.lua"), Tags, Error));
    TestEqual(TEXT("只有元数据时输出为空"), Tags.Num(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSekiroGameplayTagRejectionTest, "Sekiro.GameplayTools.Tags.RejectUnsafeSyntax", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 校验非法语法和 FName 冲突不会发布部分结果；游戏线程测试框架调用。Parameters 未使用，返回断言结果，不执行 Lua。 */
bool FSekiroGameplayTagRejectionTest::RunTest(const FString& Parameters)
{
    const TArray<FString> Sources = {
        TEXT("return require('Tags')"), TEXT("return { A = false }"), TEXT("return { A = 42 }"),
        TEXT("return { A = nil }"), TEXT("return { 'Array' }"), TEXT("return { A = true, a = true }"),
        TEXT("return { ['A.B'] = true }"), TEXT("return { None = true }"), TEXT("return { _Comment = {} }"),
        TEXT("return { A = function() end }"), TEXT("return { A = 'x' .. 'y' }"), TEXT("return {} print('x')"),
        TEXT("local Tags={} return Other"), TEXT("--[[ no ending"), TEXT("return { A = '\\z' }"),
        TEXT("'local' Tags={} return Tags"), TEXT("return {} ';'"), TEXT("return { A=true ',' B=true }"),
        TEXT("return { '[' 'A' ]=true }"), TEXT("return { A=true '}' }")
    };
    for (const FString& Source : Sources)
    {
        TArray<FSekiroGameplayTagEntry> Tags;
        Tags.AddDefaulted();
        FString Error;
        TestFalse(Source, FSekiroGameplayTagParser::Parse(Source, TEXT("Rejected.lua"), Tags, Error));
        TestEqual(TEXT("失败清空预览"), Tags.Num(), 0);
        TestTrue(TEXT("错误带来源行列"), Error.StartsWith(TEXT("Rejected.lua:1:")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSekiroGameplayTagLimitsTest, "Sekiro.GameplayTools.Tags.ResourceLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 校验深度、数量、名称与源大小上限；游戏线程测试框架调用。Parameters 未使用，返回断言结果，不生成资产。 */
bool FSekiroGameplayTagLimitsTest::RunTest(const FString& Parameters)
{
    TArray<FSekiroGameplayTagEntry> Tags;
    FString Error;
    FString Deep = TEXT("return {");
    for (int32 Index = 0; Index < 65; ++Index) Deep += TEXT("A={");
    for (int32 Index = 0; Index < 66; ++Index) Deep += TEXT("}");
    TestFalse(TEXT("拒绝65级标签"), FSekiroGameplayTagParser::Parse(Deep, TEXT("Depth.lua"), Tags, Error));
    const FString LongName = TEXT("return { ") + FString::ChrN(NAME_SIZE, 'A') + TEXT("=true }");
    TestFalse(TEXT("拒绝超长FName"), FSekiroGameplayTagParser::Parse(LongName, TEXT("Name.lua"), Tags, Error));
    FString Many = TEXT("return {");
    for (int32 Index = 0; Index <= 10000; ++Index) Many += FString::Printf(TEXT("A%d=true,"), Index);
    Many += TEXT("}");
    TestFalse(TEXT("拒绝超过10000标签"), FSekiroGameplayTagParser::Parse(Many, TEXT("Count.lua"), Tags, Error));
    TestFalse(TEXT("拒绝超过1MiB"), FSekiroGameplayTagParser::Parse(FString::ChrN(1024 * 1024 + 1, ' '), TEXT("Size.lua"), Tags, Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSekiroGameplayTagPackageBoundaryTest, "Sekiro.GameplayTools.Tags.AssetPackageBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 校验资产路径只允许用户工程内容根；游戏线程测试框架调用。Parameters 未使用，返回断言结果，不创建目录或资产。 */
bool FSekiroGameplayTagPackageBoundaryTest::RunTest(const FString& Parameters)
{
    FString Error;
    TestTrue(TEXT("接受合法工程包路径"), FSekiroGameplayTagParser::ValidateAssetPackagePath(TEXT("/Game/Data/Tags"), Error));
    const TArray<FString> InvalidPaths = { TEXT("/Engine/Tags"), TEXT("/Plugin/Tags"), TEXT("C:/Tags"), TEXT("/Game/../Tags"), TEXT("/Game/Tags.Tags"), TEXT("/Game/Tags.uasset"), TEXT("/Game/None"), TEXT("/Game/") };
    for (const FString& Path : InvalidPaths) TestFalse(Path, FSekiroGameplayTagParser::ValidateAssetPackagePath(Path, Error));
    FString LongPackage = TEXT("/Game/");
    for (int32 Index = 0; Index < 512; ++Index) LongPackage += TEXT("A/");
    LongPackage += TEXT("Tags");
    TestFalse(TEXT("拒绝整体超长多段包名"), FSekiroGameplayTagParser::ValidateAssetPackagePath(LongPackage, Error));
    TestFalse(TEXT("拒绝对象路径整体超长"), FSekiroGameplayTagParser::ValidateAssetPackagePath(TEXT("/Game/") + FString::ChrN(600, 'A'), Error));
    return !HasAnyErrors();
}

#endif
