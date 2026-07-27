#include "SekiroLuaAnimBlueprintExtension.h"

#include "Animation/AnimBlueprint.h"

/**
 * 在标准 AnimBlueprint 的持久化扩展列表中查找插件元数据，不创建对象也不改变资产。
 * 只能在游戏线程调用，因为函数遍历 UObject 所有的实例化子对象。
 *
 * @param AnimBlueprint 待查询的标准动画蓝图，可为空；函数不持有传入对象。
 * @return 找到精确扩展类型时返回该资产拥有的对象，否则返回 nullptr。
 */
USekiroLuaAnimBlueprintExtension* USekiroLuaAnimBlueprintExtension::Find(
    const UAnimBlueprint* AnimBlueprint)
{
    if (AnimBlueprint == nullptr) return nullptr;

    for (const TObjectPtr<UBlueprintExtension>& Extension : AnimBlueprint->GetExtensions())
    {
        if (Extension != nullptr
            && Extension->GetClass() == USekiroLuaAnimBlueprintExtension::StaticClass())
        {
            return CastChecked<USekiroLuaAnimBlueprintExtension>(Extension.Get());
        }
    }

    return nullptr;
}

/**
 * 沿 AnimBlueprint ParentClass 的 UE 反射继承链查找最近的有效 Lua 源，不依赖项目类名或资产路径。
 * 当前资产的扩展优先；本地扩展不存在或模块名为空时，继续检查每一级 GeneratedClass 的 ClassGeneratedBy。
 * 只能在游戏线程调用；函数只读 Blueprint 扩展和 UClass 元数据，不创建对象、不标脏 package。
 *
 * @param AnimBlueprint 待解析 Lua 源的当前动画蓝图，可为空。
 * @param bOutInherited 可选输出；返回父 AnimBlueprint 扩展时为 true，返回本地扩展或未找到时为 false。
 * @return 当前资产或最近父 AnimBlueprint 上模块名非空的扩展；继承链没有 Lua 源时返回 nullptr。
 */
const USekiroLuaAnimBlueprintExtension* USekiroLuaAnimBlueprintExtension::FindEffective(
    const UAnimBlueprint* AnimBlueprint,
    bool* bOutInherited)
{
    if (bOutInherited != nullptr) *bOutInherited = false;
    if (AnimBlueprint == nullptr) return nullptr;

    const USekiroLuaAnimBlueprintExtension* LocalExtension = Find(AnimBlueprint);
    if (LocalExtension != nullptr && !LocalExtension->LuaModuleName.IsEmpty())
    {
        return LocalExtension;
    }

    const UClass* ParentClass = AnimBlueprint->ParentClass.Get();
    while (ParentClass != nullptr)
    {
        const UAnimBlueprint* ParentAnimBlueprint =
            Cast<UAnimBlueprint>(ParentClass->ClassGeneratedBy);
        const USekiroLuaAnimBlueprintExtension* ParentExtension =
            Find(ParentAnimBlueprint);
        if (ParentExtension != nullptr && !ParentExtension->LuaModuleName.IsEmpty())
        {
            if (bOutInherited != nullptr) *bOutInherited = true;
            return ParentExtension;
        }
        ParentClass = ParentClass->GetSuperClass();
    }

    return nullptr;
}

/**
 * 返回 AnimBlueprint 已有的唯一 Lua 元数据扩展，缺失时以资产为 Outer 创建并注册一个。
 * 本类型刻意继承 UBlueprintExtension 而非 UAnimBlueprintExtension：UE5.2 动画编译器会删除未由节点请求的
 * UAnimBlueprintExtension，而通用 Blueprint 扩展能够稳定保存编译源身份。只能在游戏线程调用。
 *
 * @param AnimBlueprint 接收扩展的标准动画蓝图，不可为空；资产拥有返回对象。
 * @return 已有或新建的扩展；参数为空时返回 nullptr。
 */
USekiroLuaAnimBlueprintExtension* USekiroLuaAnimBlueprintExtension::Request(
    UAnimBlueprint* AnimBlueprint)
{
    if (AnimBlueprint == nullptr) return nullptr;
    if (USekiroLuaAnimBlueprintExtension* Existing = Find(AnimBlueprint)) return Existing;

    USekiroLuaAnimBlueprintExtension* Extension =
        NewObject<USekiroLuaAnimBlueprintExtension>(AnimBlueprint, NAME_None, RF_Transactional);
    AnimBlueprint->AddExtension(Extension);
    return Extension;
}

/**
 * 将最近编译状态更新为成功并递增资产级修订号，不保存 package，也不触发 Blueprint 编译。
 * 只能在游戏线程调用；调用方应先对扩展执行 Modify 以纳入编辑器事务。
 */
void USekiroLuaAnimBlueprintExtension::MarkCompileSucceeded()
{
    CompilerVersion = CurrentCompilerVersion;
    ++SuccessfulCompileRevision;
    SuccessfulSourceRevision = SourceRevision;
    bSourceDirty = false;
    CompileStatus = ESekiroLuaAnimBlueprintCompileStatus::UpToDate;
    LastCompileMessage = TEXT("Lua animation blueprint compiled successfully.");
}

/**
 * 记录最近一次失败摘要，不改变上次成功修订号、Graph 或 GeneratedClass，也不保存 package。
 * 只能在游戏线程调用。
 *
 * @param Message 面向资产详情与工具输出的失败摘要；允许为空。
 */
void USekiroLuaAnimBlueprintExtension::MarkCompileFailed(const FString& Message)
{
    bSourceDirty = true;
    CompileStatus = ESekiroLuaAnimBlueprintCompileStatus::Error;
    LastCompileMessage = Message;
}

/**
 * 递增资产观察到的 Lua 源修订并标记待编译，不修改 Graph、GeneratedClass 或成功修订。
 * 只能在游戏线程调用；源码监听器可仅更新内存状态，实际改变资产结构的调用方才负责事务与 package 标脏。
 *
 * @param Message 面向资产详情的变更原因；允许为空。
 */
void USekiroLuaAnimBlueprintExtension::MarkSourceDirty(const FString& Message)
{
    ++SourceRevision;
    bSourceDirty = true;
    CompileStatus = ESekiroLuaAnimBlueprintCompileStatus::OutOfDate;
    LastCompileMessage = Message;
}
/**
 * 缓存当前源码成功构建且通过验证的 IR，供 Generate From Lua 在同一源码修订上复用。
 * 只能在游戏线程调用；函数不修改 Graph、不调用原生编译、不保存或主动标脏 package。
 *
 * @param CheckedIR 已完成 Lua 导入、IR 验证和资源预检的不可变结果；函数复制其内容。
 */
void USekiroLuaAnimBlueprintExtension::MarkCheckSucceeded(
    const FSekiroAnimBlueprintIR& CheckedIR)
{
    check(IsInGameThread());
    LastSuccessfulIR = CheckedIR;
    bHasLastSuccessfulIR = true;
    LastCheckedSourceRevision = SourceRevision;
    LastCompileMessage = TEXT("Lua animation source check succeeded.");
}

/**
 * 记录 Check Lua 的失败摘要，同时保留上一次成功 IR，但使其因修订不匹配而不可直接生成。
 * 只能在游戏线程调用；函数不修改 Graph、不调用原生编译、不保存或主动标脏 package。
 *
 * @param Message 带源码位置的检查失败摘要；允许为空。
 */
void USekiroLuaAnimBlueprintExtension::MarkCheckFailed(const FString& Message)
{
    check(IsInGameThread());
    CompileStatus = ESekiroLuaAnimBlueprintCompileStatus::Error;
    LastCompileMessage = Message;
}
