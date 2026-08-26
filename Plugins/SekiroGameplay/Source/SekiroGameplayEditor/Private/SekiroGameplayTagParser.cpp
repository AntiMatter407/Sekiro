#include "SekiroGameplayTagParser.h"

#include "Containers/StringConv.h"
#include "Misc/PackageName.h"

namespace SekiroGameplayTagParsing
{
    constexpr int32 MaxSourceBytes = 1024 * 1024;
    constexpr int32 MaxDepth = 64;
    constexpr int32 MaxTags = 10000;

    enum class ETokenKind : uint8 { End, Identifier, String, Symbol };

    struct FToken
    {
        ETokenKind Kind = ETokenKind::End; // 当前词法类型
        FString Text; // 解码后的词法内容
        int32 Offset = 0; // 原文字符偏移，用于一基行列诊断
    };

    /** 查询 ASCII 标识符起始字符；任意线程，无副作用。参数为单字符，返回是否合规。 */
    bool IsStart(TCHAR Character)
    {
        return (Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z') || Character == '_';
    }

    /** 查询 ASCII 标识符后续字符；任意线程，无副作用。参数为单字符，返回是否合规。 */
    bool IsPart(TCHAR Character)
    {
        return IsStart(Character) || (Character >= '0' && Character <= '9');
    }

    /** 判断名字是否为 Lua 关键字；任意线程，无副作用。Name 是完整标识符，返回保留字匹配结果。 */
    bool IsKeyword(const FString& Name)
    {
        static const TSet<FString> Keywords = {
            TEXT("and"), TEXT("break"), TEXT("do"), TEXT("else"), TEXT("elseif"), TEXT("end"),
            TEXT("false"), TEXT("for"), TEXT("function"), TEXT("goto"), TEXT("if"), TEXT("in"),
            TEXT("local"), TEXT("nil"), TEXT("not"), TEXT("or"), TEXT("repeat"), TEXT("return"),
            TEXT("then"), TEXT("true"), TEXT("until"), TEXT("while")
        };
        return Keywords.Contains(Name);
    }

    class FParser
    {
    public:
        // ── 声明式解析 ──
        FParser(const FString& InSource, const FString& InName);
        bool Run(TArray<FSekiroGameplayTagEntry>& OutTags, FString& OutError);

    private:
        // ── 词法与层次表 ──
        bool Fail(const FString& Message, int32 Offset);
        bool Next();
        bool Take(const TCHAR* Expected);
        bool Table(const FString& Parent, int32 Depth, int32 ParentIndex);
        bool Entry(const FString& Parent, int32 Depth, int32 ParentIndex, TSet<FString>& Seen);

        const FString& Source; // 调用期间借用原文，不跨调用保留
        const FString& SourceName; // 诊断中显示的来源名
        int32 Cursor = 0; // 下一个待扫描字符
        FToken Token; // 已扫描的当前单元
        FString Error; // 首个错误，后续错误不覆盖
        TArray<FSekiroGameplayTagEntry> Tags; // 完整成功前不发布的临时结果
    };

    /** 为单次解析借用原文和诊断名；可在任意线程创建，引用仅需存活到 Run 返回。 */
    FParser::FParser(const FString& InSource, const FString& InName) : Source(InSource), SourceName(InName)
    {
    }

    /** 解析唯一 return 表或 local 表并返回；任意线程。输出成功替换标签，失败清空标签并填充错误，无外部副作用。 */
    bool FParser::Run(TArray<FSekiroGameplayTagEntry>& OutTags, FString& OutError)
    {
        OutTags.Reset();
        OutError.Reset();
        bool bValid = Next();
        if (bValid && Token.Kind == ETokenKind::Identifier && Token.Text == TEXT("local"))
        {
            bValid = Next();
            const FString Variable = Token.Text;
            if (bValid && (Token.Kind != ETokenKind::Identifier || IsKeyword(Variable)))
            {
                bValid = Fail(TEXT("局部表名必须是非关键字标识符"), Token.Offset);
            }
            bValid = bValid && Next() && Take(TEXT("=")) && Table(TEXT(""), 0, INDEX_NONE);
            if (bValid && Token.Kind == ETokenKind::Symbol && Token.Text == TEXT(";")) bValid = Next();
            bValid = bValid && Take(TEXT("return")) && Token.Kind == ETokenKind::Identifier && Take(*Variable);
        }
        else
        {
            bValid = bValid && Take(TEXT("return")) && Table(TEXT(""), 0, INDEX_NONE);
        }
        if (bValid && Token.Kind == ETokenKind::Symbol && Token.Text == TEXT(";")) bValid = Next();
        if (bValid && Token.Kind != ETokenKind::End) bValid = Fail(TEXT("返回表之后不允许其他语句"), Token.Offset);
        if (!bValid)
        {
            if (Error.IsEmpty()) Fail(TEXT("需要返回先前声明的局部表变量"), Token.Offset);
            OutError = Error;
            return false;
        }
        Tags.Sort([](const FSekiroGameplayTagEntry& Left, const FSekiroGameplayTagEntry& Right)
        {
            return Left.Tag.Compare(Right.Tag, ESearchCase::IgnoreCase) < 0;
        });
        OutTags = MoveTemp(Tags);
        return true;
    }

    /** 记录首次错误及一基行列；任意线程。Message 为原因，Offset 为字符偏移，始终返回 false 便于传播失败。 */
    bool FParser::Fail(const FString& Message, int32 Offset)
    {
        if (Error.IsEmpty())
        {
            int32 Line = 1;
            int32 Column = 1;
            for (int32 Index = 0; Index < Offset && Index < Source.Len(); ++Index)
            {
                if (Source[Index] == '\n') { ++Line; Column = 1; }
                else { ++Column; }
            }
            Error = FString::Printf(TEXT("%s:%d:%d: %s"), *SourceName, Line, Column, *Message);
        }
        return false;
    }

    /** 扫描一个单元并跳过 Lua 普通/长注释；任意线程。成功更新 Token，失败记录错误；从不执行 Lua。 */
    bool FParser::Next()
    {
        while (Cursor < Source.Len())
        {
            if (FChar::IsWhitespace(Source[Cursor])) { ++Cursor; continue; }
            if (Source.Mid(Cursor, 2) != TEXT("--")) break;
            const int32 CommentStart = Cursor;
            Cursor += 2;
            int32 Probe = Cursor;
            if (Source.IsValidIndex(Probe) && Source[Probe] == '[')
            {
                ++Probe;
                while (Source.IsValidIndex(Probe) && Source[Probe] == '=') ++Probe;
                if (Source.IsValidIndex(Probe) && Source[Probe] == '[')
                {
                    const FString Closing = TEXT("]") + Source.Mid(Cursor + 1, Probe - Cursor - 1) + TEXT("]");
                    const int32 End = Source.Find(Closing, ESearchCase::CaseSensitive, ESearchDir::FromStart, Probe + 1);
                    if (End == INDEX_NONE) return Fail(TEXT("长注释没有结束"), CommentStart);
                    Cursor = End + Closing.Len();
                    continue;
                }
            }
            while (Cursor < Source.Len() && Source[Cursor] != '\n') ++Cursor;
        }
        Token = FToken();
        Token.Offset = Cursor;
        if (Cursor == Source.Len()) return true;
        const TCHAR Character = Source[Cursor++];
        if (FCString::Strchr(TEXT("{}[]=,;"), Character))
        {
            Token.Kind = ETokenKind::Symbol;
            Token.Text.AppendChar(Character);
            return true;
        }
        if (Character == '"' || Character == '\'')
        {
            Token.Kind = ETokenKind::String;
            while (Cursor < Source.Len())
            {
                TCHAR Value = Source[Cursor++];
                if (Value == Character) return true;
                if (Value == '\n' || Value == '\r') return Fail(TEXT("短字符串不能包含原始换行"), Cursor - 1);
                if (Value == '\\')
                {
                    if (Cursor == Source.Len()) return Fail(TEXT("字符串转义没有结束"), Token.Offset);
                    Value = Source[Cursor++];
                    switch (Value)
                    {
                    case 'n': Value = '\n'; break;
                    case 'r': Value = '\r'; break;
                    case 't': Value = '\t'; break;
                    case '\\': case '\'': case '"': break;
                    default: return Fail(TEXT("仅支持 n/r/t/引号/反斜杠转义"), Cursor - 2);
                    }
                }
                if (Value < 32 && Value != '\n' && Value != '\r' && Value != '\t') return Fail(TEXT("说明含非法控制字符"), Cursor - 1);
                Token.Text.AppendChar(Value);
            }
            return Fail(TEXT("字符串没有结束"), Token.Offset);
        }
        if (IsStart(Character))
        {
            Token.Kind = ETokenKind::Identifier;
            Token.Text.AppendChar(Character);
            while (Cursor < Source.Len() && IsPart(Source[Cursor])) Token.Text.AppendChar(Source[Cursor++]);
            return true;
        }
        return Fail(TEXT("不支持的语法字符；只允许声明式 Lua 表"), Token.Offset);
    }

    /** 消费特定标识符或标点；任意线程。Expected 非空且不是字符串内容，返回匹配及后续扫描是否成功。 */
    bool FParser::Take(const TCHAR* Expected)
    {
        if ((Token.Kind != ETokenKind::Identifier && Token.Kind != ETokenKind::Symbol) || Token.Text != Expected)
        {
            return Fail(FString::Printf(TEXT("需要 %s，实际为 %s"), Expected, *Token.Text), Token.Offset);
        }
        return Next();
    }

    /** 读取层次表；任意线程。Parent 是完整父标签，Depth 根为零，ParentIndex 根为 INDEX_NONE；失败不发布暂存标签。 */
    bool FParser::Table(const FString& Parent, int32 Depth, int32 ParentIndex)
    {
        if (Depth > MaxDepth) return Fail(TEXT("标签层次超过 64 级"), Token.Offset);
        if (!Take(TEXT("{"))) return false;
        TSet<FString> Seen;
        while (Token.Kind != ETokenKind::Symbol || Token.Text != TEXT("}"))
        {
            if (!Entry(Parent, Depth, ParentIndex, Seen)) return false;
            if (Token.Kind == ETokenKind::Symbol && (Token.Text == TEXT(",") || Token.Text == TEXT(";")))
            {
                if (!Next()) return false;
            }
            else if (Token.Kind != ETokenKind::Symbol || Token.Text != TEXT("}")) return Fail(TEXT("表项之间需要逗号或分号"), Token.Offset);
        }
        return Take(TEXT("}"));
    }

    /** 读取单键值并展开节点；任意线程。Seen 为当前表已见键，其他参数同 Table；返回是否合法，失败保留首错。 */
    bool FParser::Entry(const FString& Parent, int32 Depth, int32 ParentIndex, TSet<FString>& Seen)
    {
        const int32 KeyOffset = Token.Offset;
        FString Key;
        if (Token.Kind == ETokenKind::Symbol && Token.Text == TEXT("["))
        {
            if (!Next()) return false;
            if (Token.Kind != ETokenKind::String) return Fail(TEXT("方括号键必须是字符串"), Token.Offset);
            Key = Token.Text;
            if (!Next() || !Take(TEXT("]"))) return false;
        }
        else
        {
            if (Token.Kind != ETokenKind::Identifier || IsKeyword(Token.Text)) return Fail(TEXT("需要非关键字命名键，禁止数组"), Token.Offset);
            Key = Token.Text;
            if (!Next()) return false;
        }
        if (Key.IsEmpty() || !IsStart(Key[0])) return Fail(TEXT("节点名必须是 ASCII 标识符"), KeyOffset);
        for (TCHAR Character : Key)
        {
            if (!IsPart(Character)) return Fail(TEXT("节点名仅允许字母数字下划线，请用嵌套表达层级"), KeyOffset);
        }
        const FString Folded = Key.ToLower();
        if (Seen.Contains(Folded)) return Fail(TEXT("重复键或 FName 大小写冲突：") + Key, KeyOffset);
        Seen.Add(Folded);
        if (!Take(TEXT("="))) return false;
        if (Key == TEXT("_Comment"))
        {
            if (Token.Kind != ETokenKind::String) return Fail(TEXT("_Comment 必须是字符串"), Token.Offset);
            if (ParentIndex != INDEX_NONE) Tags[ParentIndex].Comment = Token.Text;
            return Next();
        }
        if (Depth >= MaxDepth) return Fail(TEXT("标签层次超过 64 级"), KeyOffset);
        const FString FullName = Parent.IsEmpty() ? Key : Parent + TEXT(".") + Key;
        if (FullName.Len() >= NAME_SIZE || FullName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            return Fail(TEXT("完整标签不能是 None 且最多 1023 字符"), KeyOffset);
        }
        if (Tags.Num() >= MaxTags) return Fail(TEXT("标签数量超过 10000"), KeyOffset);
        FSekiroGameplayTagEntry NewTag;
        NewTag.Tag = FullName;
        const int32 TagIndex = Tags.Add(MoveTemp(NewTag));
        if (Token.Kind == ETokenKind::String)
        {
            Tags[TagIndex].Comment = Token.Text;
            return Next();
        }
        if (Token.Kind == ETokenKind::Identifier && Token.Text == TEXT("true")) return Next();
        if (Token.Kind == ETokenKind::Symbol && Token.Text == TEXT("{")) return Table(FullName, Depth + 1, TagIndex);
        return Fail(TEXT("值必须是层次表、说明字符串或 true；禁止动态表达式"), Token.Offset);
    }
}

/**
 * 在任意线程将声明式 Lua 原文编译为排序标签，不运行 Lua、不触碰 UObject 或文件。
 * @param Source UTF-16 原文，UTF-8 编码后不得超过 1 MiB。
 * @param SourceName 仅用于错误诊断的来源名，可为空。
 * @param OutTags 输出父节点与叶节点；失败清空。
 * @param OutError 输出带来源和一基行列的错误；成功为空。
 * @return 完整语法和语义校验成功时 true，否则 false。
 */
bool FSekiroGameplayTagParser::Parse(const FString& Source, const FString& SourceName, TArray<FSekiroGameplayTagEntry>& OutTags, FString& OutError)
{
    if (FTCHARToUTF8(*Source, Source.Len()).Length() > SekiroGameplayTagParsing::MaxSourceBytes)
    {
        OutTags.Reset();
        OutError = SourceName + TEXT(":1:1: Lua 输入超过 1 MiB");
        return false;
    }
    SekiroGameplayTagParsing::FParser Parser(Source, SourceName);
    return Parser.Run(OutTags, OutError);
}

/**
 * 校验可写资产包名的纯语法边界；任意线程，无磁盘或 UObject 副作用。
 * @param PackagePath 用户明确输入的 /Game/ 下长包名，不接受对象路径、扩展名或其他挂载根。
 * @param OutError 失败原因；成功清空。
 * @return 满足包名与对象名约束时 true；不保证磁盘可写或资产所有权。
 */
bool FSekiroGameplayTagParser::ValidateAssetPackagePath(const FString& PackagePath, FString& OutError)
{
    OutError.Reset();
    FText Reason;
    if (PackagePath.Len() >= NAME_SIZE)
    {
        OutError = TEXT("完整资产包名最多 1023 字符，避免超过 UE FName 上限");
        return false;
    }
    if (!PackagePath.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive) || !FPackageName::IsValidLongPackageName(PackagePath, false, &Reason))
    {
        OutError = TEXT("输出必须是 /Game/ 下的有效资产长包名（不带 .uasset 或对象后缀）：") + Reason.ToString();
        return false;
    }
    const FString ObjectName = FPackageName::GetLongPackageAssetName(PackagePath);
    if (ObjectName.IsEmpty() || PackagePath.Len() + ObjectName.Len() + 1 >= NAME_SIZE || ObjectName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("资产名不能为空或 None，且完整资产对象路径最多 1023 字符");
        return false;
    }
    return true;
}
