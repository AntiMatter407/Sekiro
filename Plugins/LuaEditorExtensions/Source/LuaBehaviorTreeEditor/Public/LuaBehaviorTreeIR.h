#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#include "LuaBehaviorTreeIR.generated.h"

UENUM(BlueprintType)
enum class ELuaBehaviorTreeValueType : uint8
{
    Bool,
    Integer,
    Float,
    String,
    Name,
    Text,
    Enum,
    Object,
    SoftObject,
    Class,
    SoftClass,
    Struct,
    Array,
};

UENUM(BlueprintType)
enum class ELuaBehaviorTreeDiagnosticSeverity : uint8
{
    Info,
    Warning,
    Error,
};

/** Lua 源码中的稳定定位信息。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeSourceLocation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString LuaModule;                    // Lua 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 Line = 0;                       // 一基行号，未知时为 0

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 Column = 0;                     // 一基列号，未知时为 0
};

/** 扁平值池中的强类型值；StructFields 与 ArrayItems 存储其他值的索引。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeIRValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    ELuaBehaviorTreeValueType Type = ELuaBehaviorTreeValueType::Bool; // 显式类型标签

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    bool BoolValue = false;               // Bool 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int64 IntegerValue = 0;               // 整数或枚举底层值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FName EnumTypeName = NAME_None;        // 导出边界使用的 UEnum 反射名，导入写入不依赖此字段

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    double FloatValue = 0.0;              // 浮点值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString StringValue;                  // 文本或对象路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TMap<FName, int32> StructFields;       // Struct 字段到值池索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<int32> ArrayItems;              // Array 元素值池索引
};

/** 一个反射属性赋值。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeIRProperty
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FName Name = NAME_None;               // 反射属性名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 ValueIndex = INDEX_NONE;         // Values 中的根值索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 DeclarationOrder = 0;           // 源码声明顺序
};

/** 行为树主节点或挂载节点。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeIRNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString Id;                           // 全局稳定 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString ParentId;                     // 父 Composite 或被挂载主节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FSoftClassPath ClassPath;              // 反射加载类路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString DisplayName;                  // 编辑器显示名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRProperty> Properties; // 反射属性

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 DeclarationOrder = 0;           // 确定性兄弟顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FLuaBehaviorTreeSourceLocation SourceLocation; // 源码定位
};

/** Blackboard Key 声明。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBlackboardIRKey
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString Id;                           // 全局稳定 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FName Name = NAME_None;               // Blackboard Key 名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FSoftClassPath ClassPath;              // UBlackboardKeyType 子类

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRProperty> Properties; // KeyType 反射属性

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    bool bInstanceSynced = false;          // 是否跨实例同步

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 DeclarationOrder = 0;           // 确定性 Key 顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FLuaBehaviorTreeSourceLocation SourceLocation; // 源码定位
};

/** 与具体节点类型解耦的完整行为树 IR。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeIR
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    int32 SchemaVersion = 1;              // IR Schema 版本

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString SourceModule;                 // Lua 唯一源码模块

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString RootNodeId;                   // 根 Composite ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FSoftObjectPath ParentBlackboard;      // 可选父 Blackboard

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRValue> Values; // 扁平强类型值池

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRNode> Nodes; // 全部主节点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRNode> Decorators; // 全部 Decorator

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBehaviorTreeIRNode> Services; // 全部 Service

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    TArray<FLuaBlackboardIRKey> BlackboardKeys; // Blackboard Keys
};

/** 编译、校验或反射写入诊断。 */
USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeDiagnostic
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    ELuaBehaviorTreeDiagnosticSeverity Severity = ELuaBehaviorTreeDiagnosticSeverity::Error; // 严重级别

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FName Code = NAME_None;               // 稳定错误码

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString Message;                      // 中文诊断正文

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString Path;                         // IR 字段路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FLuaBehaviorTreeSourceLocation SourceLocation; // 源码定位
};
