#include "SekiroBehaviorTreeExporterLibrary.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "AIGraphNode.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "SekiroBehaviorTreeFactoryLibrary.h"
#include "SekiroBehaviorTreeIRLibrary.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace SekiroBehaviorTreeExporter
{
    const FName InvalidAsset(TEXT("BT.Export.InvalidAsset"));
    const FName InvalidModulePath(TEXT("BT.Export.InvalidModulePath"));
    const FName FileExists(TEXT("BT.Export.FileExists"));
    const FName FileWriteFailed(TEXT("BT.Export.FileWriteFailed"));
    const FName GraphInvalid(TEXT("BT.Export.GraphInvalid"));
    const FName UnsupportedProperty(TEXT("BT.Export.UnsupportedProperty"));
    const FName UnsupportedContainer(TEXT("BT.Export.UnsupportedContainer"));
    const FName RoundTripFailed(TEXT("BT.Export.RoundTripFailed"));

    struct FExtractionContext
    {
        FSekiroBehaviorTreeIR* IR = nullptr; // 正在构建的规范 IR
        TArray<FSekiroBehaviorTreeDiagnostic>* Diagnostics = nullptr; // 诊断输出
        TMap<const UAIGraphNode*, FString> NodeIds; // Graph 主节点到稳定 ID
        TArray<const UAIGraphNode*> OrderedNodes; // 深度优先主节点顺序
        TSet<const UAIGraphNode*> VisitedNodes; // 防止损坏 Graph 形成环
        TSet<FString> UsedMainNames;       // 主节点唯一显示名
        TSet<FString> UsedDecoratorNames;  // Decorator 唯一显示名
        TSet<FString> UsedServiceNames;    // Service 唯一显示名
        int32 DeclarationOrder = 0;        // 全局确定性声明顺序
    };

    /** 追加一条稳定导出错误，不记录日志。 */
    void AddError(
        TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& Path)
    {
        FSekiroBehaviorTreeDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Message = Message;
        Diagnostic.Path = Path;
    }

    /** 判断当前编辑器是否处于 PIE 或 SIE，查询不产生副作用。 */
    bool IsPlaySessionActive()
    {
        return GEditor
            && (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor);
    }

    /** 判断反射属性是否属于 Writer 允许写回的可编辑集合。 */
    bool IsExportableProperty(const FProperty* Property)
    {
        return Property
            && Property->HasAnyPropertyFlags(CPF_Edit)
            && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
            && !Property->IsA<FDelegateProperty>()
            && !Property->IsA<FMulticastDelegateProperty>();
    }

    /**
     * 把一个反射值递归追加到 IR 值池，与 ReflectionWriter 支持的类型严格对称。
     * DefaultAddress 仅用于 Struct 字段跳过默认值；函数不修改 UObject。
     */
    bool AppendValue(
        FProperty* Property,
        const void* ValueAddress,
        const void* DefaultAddress,
        FSekiroBehaviorTreeIR& IR,
        int32& OutValueIndex,
        const FString& Path,
        TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics)
    {
        if (!Property || !ValueAddress) return false;
        FSekiroBehaviorTreeIRValue Value;

        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Bool;
            Value.BoolValue = BoolProperty->GetPropertyValue(ValueAddress);
        }
        else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Enum;
            const int64 EnumValue = EnumProperty->GetUnderlyingProperty()
                ->GetSignedIntPropertyValue(ValueAddress);
            Value.StringValue = EnumProperty->GetEnum()->GetNameStringByValue(EnumValue);
        }
        else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            if (ByteProperty->Enum)
            {
                Value.Type = ESekiroBehaviorTreeValueType::Enum;
                Value.StringValue = ByteProperty->Enum->GetNameStringByValue(
                    ByteProperty->GetPropertyValue(ValueAddress));
            }
            else
            {
                Value.Type = ESekiroBehaviorTreeValueType::Integer;
                Value.IntegerValue = ByteProperty->GetPropertyValue(ValueAddress);
            }
        }
        else if (const FNumericProperty* NumericProperty =
            CastField<FNumericProperty>(Property))
        {
            if (NumericProperty->IsInteger())
            {
                Value.Type = ESekiroBehaviorTreeValueType::Integer;
                if (Property->IsA<FUInt16Property>()
                    || Property->IsA<FUInt32Property>()
                    || Property->IsA<FUInt64Property>())
                {
                    const uint64 UnsignedValue =
                        NumericProperty->GetUnsignedIntPropertyValue(ValueAddress);
                    if (UnsignedValue > static_cast<uint64>(MAX_int64))
                    {
                        AddError(
                            Diagnostics,
                            UnsupportedProperty,
                            TEXT("无符号整数超出 Lua IR 的 int64 范围。"),
                            Path);
                        return false;
                    }
                    Value.IntegerValue = static_cast<int64>(UnsignedValue);
                }
                else
                {
                    Value.IntegerValue =
                        NumericProperty->GetSignedIntPropertyValue(ValueAddress);
                }
            }
            else
            {
                Value.Type = ESekiroBehaviorTreeValueType::Float;
                Value.FloatValue =
                    NumericProperty->GetFloatingPointPropertyValue(ValueAddress);
                if (!FMath::IsFinite(Value.FloatValue))
                {
                    AddError(
                        Diagnostics,
                        UnsupportedProperty,
                        TEXT("Lua 无法稳定表示 NaN 或 Infinity。"),
                        Path);
                    return false;
                }
            }
        }
        else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::String;
            Value.StringValue = StringProperty->GetPropertyValue(ValueAddress);
        }
        else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Name;
            Value.StringValue = NameProperty->GetPropertyValue(ValueAddress).ToString();
        }
        else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Text;
            Value.StringValue = TextProperty->GetPropertyValue(ValueAddress).ToString();
        }
        else if (const FSoftClassProperty* SoftClassProperty =
            CastField<FSoftClassProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::SoftClass;
            Value.StringValue = SoftClassProperty->GetPropertyValue(ValueAddress)
                .ToSoftObjectPath().ToString();
        }
        else if (const FSoftObjectProperty* SoftObjectProperty =
            CastField<FSoftObjectProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::SoftObject;
            Value.StringValue = SoftObjectProperty->GetPropertyValue(ValueAddress)
                .ToSoftObjectPath().ToString();
        }
        else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Class;
            const UObject* ClassValue = ClassProperty->GetObjectPropertyValue(ValueAddress);
            Value.StringValue = ClassValue ? ClassValue->GetPathName() : FString();
        }
        else if (const FObjectPropertyBase* ObjectProperty =
            CastField<FObjectPropertyBase>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Object;
            const UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValueAddress);
            Value.StringValue = ObjectValue ? ObjectValue->GetPathName() : FString();
        }
        else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Struct;
            OutValueIndex = IR.Values.Add(Value);
            TArray<FProperty*> Fields;
            for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
            {
                FProperty* Field = *FieldIt;
                if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
                    || Field->IsA<FDelegateProperty>()
                    || Field->IsA<FMulticastDelegateProperty>())
                {
                    continue;
                }
                Fields.Add(Field);
            }
            Fields.Sort([](const FProperty& Left, const FProperty& Right)
            {
                return Left.GetName() < Right.GetName();
            });
            for (FProperty* Field : Fields)
            {
                const void* FieldValue = Field->ContainerPtrToValuePtr<void>(ValueAddress);
                const void* FieldDefault = DefaultAddress
                    ? Field->ContainerPtrToValuePtr<void>(DefaultAddress)
                    : nullptr;
                if (FieldDefault && Field->Identical(FieldValue, FieldDefault)) continue;
                int32 ChildIndex = INDEX_NONE;
                if (!AppendValue(
                        Field,
                        FieldValue,
                        FieldDefault,
                        IR,
                        ChildIndex,
                        Path + TEXT(".") + Field->GetName(),
                        Diagnostics))
                {
                    return false;
                }
                IR.Values[OutValueIndex].StructFields.Add(Field->GetFName(), ChildIndex);
            }
            return true;
        }
        else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            Value.Type = ESekiroBehaviorTreeValueType::Array;
            OutValueIndex = IR.Values.Add(Value);
            FScriptArrayHelper ArrayHelper(ArrayProperty, ValueAddress);
            for (int32 ItemIndex = 0; ItemIndex < ArrayHelper.Num(); ++ItemIndex)
            {
                int32 ChildIndex = INDEX_NONE;
                if (!AppendValue(
                        ArrayProperty->Inner,
                        ArrayHelper.GetRawPtr(ItemIndex),
                        nullptr,
                        IR,
                        ChildIndex,
                        FString::Printf(TEXT("%s[%d]"), *Path, ItemIndex),
                        Diagnostics))
                {
                    return false;
                }
                IR.Values[OutValueIndex].ArrayItems.Add(ChildIndex);
            }
            return true;
        }
        else if (Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
        {
            AddError(
                Diagnostics,
                UnsupportedContainer,
                TEXT("非默认 Set/Map 无法导出；旧 Lua 文件保持不变。"),
                Path);
            return false;
        }
        else
        {
            AddError(
                Diagnostics,
                UnsupportedProperty,
                TEXT("非默认可编辑属性类型尚未支持反向导出。"),
                Path);
            return false;
        }

        OutValueIndex = IR.Values.Add(Value);
        return true;
    }

    /** 提取一个 UObject 相对 CDO 发生变化的全部可编辑属性。 */
    bool ExtractProperties(
        UObject* Object,
        FSekiroBehaviorTreeIR& IR,
        TArray<FSekiroBehaviorTreeIRProperty>& OutProperties,
        TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics)
    {
        if (!Object) return false;
        const UObject* DefaultObject = Object->GetClass()->GetDefaultObject();
        TArray<FProperty*> Properties;
        for (TFieldIterator<FProperty> PropertyIt(
                Object->GetClass(),
                EFieldIteratorFlags::IncludeSuper);
            PropertyIt;
            ++PropertyIt)
        {
            FProperty* Property = *PropertyIt;
            if (IsExportableProperty(Property)
                && !Property->Identical_InContainer(Object, DefaultObject))
            {
                Properties.Add(Property);
            }
        }
        Properties.Sort([](const FProperty& Left, const FProperty& Right)
        {
            return Left.GetName() < Right.GetName();
        });
        for (int32 PropertyIndex = 0; PropertyIndex < Properties.Num(); ++PropertyIndex)
        {
            FProperty* Property = Properties[PropertyIndex];
            FSekiroBehaviorTreeIRProperty& IRProperty = OutProperties.AddDefaulted_GetRef();
            IRProperty.Name = Property->GetFName();
            IRProperty.DeclarationOrder = PropertyIndex + 1;
            if (!AppendValue(
                    Property,
                    Property->ContainerPtrToValuePtr<void>(Object),
                    Property->ContainerPtrToValuePtr<void>(DefaultObject),
                    IR,
                    IRProperty.ValueIndex,
                    Object->GetClass()->GetPathName() + TEXT(".") + Property->GetName(),
                    Diagnostics))
            {
                return false;
            }
        }
        return true;
    }

    /** 为重复节点名生成确定性后缀，保证 DSL 稳定 ID 唯一。 */
    FString MakeUniqueSemanticName(
        const UBTNode* Node,
        const FString& Fallback,
        TSet<FString>& UsedNames)
    {
        FString BaseName = Node ? Node->GetNodeName().TrimStartAndEnd() : FString();
        if (BaseName.IsEmpty()) BaseName = Fallback;
        FString Candidate = BaseName;
        int32 Suffix = 2;
        while (UsedNames.Contains(Candidate))
        {
            Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
            ++Suffix;
        }
        UsedNames.Add(Candidate);
        return Candidate;
    }

    /** 按输出 Pin 顺序和每个 Pin 内的 X 坐标收集直接主节点，保持编辑器执行顺序。 */
    void CollectChildren(
        const UAIGraphNode* Parent,
        TArray<const UAIGraphNode*>& OutChildren)
    {
        OutChildren.Reset();
        if (!Parent) return;
        for (const UEdGraphPin* Pin : Parent->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output) continue;
            TArray<const UAIGraphNode*> PinChildren;
            for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                const UAIGraphNode* Child = LinkedPin
                    ? Cast<UAIGraphNode>(LinkedPin->GetOwningNode())
                    : nullptr;
                const UBTNode* Instance = Child ? Cast<UBTNode>(Child->NodeInstance) : nullptr;
                if (Instance
                    && (Instance->IsA<UBTCompositeNode>() || Instance->IsA<UBTTaskNode>()))
                {
                    PinChildren.AddUnique(Child);
                }
            }
            PinChildren.Sort([](
                const UAIGraphNode& Left,
                const UAIGraphNode& Right)
            {
                if (Left.NodePosX != Right.NodePosX) return Left.NodePosX < Right.NodePosX;
                if (Left.NodePosY != Right.NodePosY) return Left.NodePosY < Right.NodePosY;
                const UBTNode* LeftInstance = Cast<UBTNode>(Left.NodeInstance);
                const UBTNode* RightInstance = Cast<UBTNode>(Right.NodeInstance);
                const FString LeftKey = LeftInstance
                    ? LeftInstance->GetClass()->GetPathName() + LeftInstance->GetNodeName()
                    : FString();
                const FString RightKey = RightInstance
                    ? RightInstance->GetClass()->GetPathName() + RightInstance->GetNodeName()
                    : FString();
                return LeftKey < RightKey;
            });
            OutChildren.Append(PinChildren);
        }
    }

    /** 深度优先提取一个主节点及其子树，不依赖具体节点类型注册表。 */
    bool ExtractMainSubtree(
        const UAIGraphNode* GraphNode,
        const FString& ParentId,
        FExtractionContext& Context)
    {
        if (!GraphNode || Context.VisitedNodes.Contains(GraphNode))
        {
            AddError(*Context.Diagnostics, GraphInvalid, TEXT("行为树 Graph 存在环或空节点。"), ParentId);
            return false;
        }
        UBTNode* Instance = Cast<UBTNode>(GraphNode->NodeInstance);
        if (!Instance
            || (!Instance->IsA<UBTCompositeNode>() && !Instance->IsA<UBTTaskNode>()))
        {
            AddError(*Context.Diagnostics, GraphInvalid, TEXT("主 Graph 节点缺少 Composite/Task 实例。"), GraphNode->GetPathName());
            return false;
        }

        Context.VisitedNodes.Add(GraphNode);
        const FString Name = MakeUniqueSemanticName(
            Instance,
            Instance->GetClass()->GetName(),
            Context.UsedMainNames);
        const FString NodeId = TEXT("Node:") + Name;
        FSekiroBehaviorTreeIRNode Node;
        Node.Id = NodeId;
        Node.ParentId = ParentId;
        Node.ClassPath = FSoftClassPath(Instance->GetClass()->GetPathName());
        Node.DisplayName = Name;
        Node.DeclarationOrder = ++Context.DeclarationOrder;
        Node.SourceLocation.LuaModule = Context.IR->SourceModule;
        if (!ExtractProperties(Instance, *Context.IR, Node.Properties, *Context.Diagnostics)) return false;
        Context.IR->Nodes.Add(MoveTemp(Node));
        if (ParentId.IsEmpty()) Context.IR->RootNodeId = NodeId;
        Context.NodeIds.Add(GraphNode, NodeId);
        Context.OrderedNodes.Add(GraphNode);

        TArray<const UAIGraphNode*> Children;
        CollectChildren(GraphNode, Children);
        for (const UAIGraphNode* Child : Children)
        {
            if (!ExtractMainSubtree(Child, NodeId, Context)) return false;
        }
        return true;
    }

    /** 通过 UProperty 查询编辑器 Shell 的注入标记，避免链接未导出的具体 Graph 类。 */
    bool IsInjectedGraphNode(const UAIGraphNode* GraphNode)
    {
        if (!GraphNode) return false;
        const FBoolProperty* InjectedProperty = FindFProperty<FBoolProperty>(
            GraphNode->GetClass(),
            TEXT("bInjectedNode"));
        if (!InjectedProperty) return false;
        const void* ValueAddress = InjectedProperty->ContainerPtrToValuePtr<void>(GraphNode);
        return InjectedProperty->GetPropertyValue(ValueAddress);
    }

    /** 提取一个主节点的 Decorator 与 Service 数组，跳过只读注入 Shell。 */
    bool ExtractAttachedNodes(FExtractionContext& Context)
    {
        for (const UAIGraphNode* Parent : Context.OrderedNodes)
        {
            const FString* ParentId = Context.NodeIds.Find(Parent);
            if (!ParentId) return false;
            for (const UAIGraphNode* SubNode : Parent->SubNodes)
            {
                if (!SubNode || IsInjectedGraphNode(SubNode)) continue;
                if (UBTDecorator* DecoratorInstance = Cast<UBTDecorator>(SubNode->NodeInstance))
                {
                    const FString Name = MakeUniqueSemanticName(
                        DecoratorInstance,
                        DecoratorInstance->GetClass()->GetName(),
                        Context.UsedDecoratorNames);
                    FSekiroBehaviorTreeIRNode& Node = Context.IR->Decorators.AddDefaulted_GetRef();
                    Node.Id = TEXT("Decorator:") + Name;
                    Node.ParentId = *ParentId;
                    Node.ClassPath = FSoftClassPath(DecoratorInstance->GetClass()->GetPathName());
                    Node.DisplayName = Name;
                    Node.DeclarationOrder = ++Context.DeclarationOrder;
                    Node.SourceLocation.LuaModule = Context.IR->SourceModule;
                    if (!ExtractProperties(
                            DecoratorInstance,
                            *Context.IR,
                            Node.Properties,
                            *Context.Diagnostics))
                    {
                        return false;
                    }
                    continue;
                }
                if (UBTService* ServiceInstance = Cast<UBTService>(SubNode->NodeInstance))
                {
                    const FString Name = MakeUniqueSemanticName(
                        ServiceInstance,
                        ServiceInstance->GetClass()->GetName(),
                        Context.UsedServiceNames);
                    FSekiroBehaviorTreeIRNode& Node = Context.IR->Services.AddDefaulted_GetRef();
                    Node.Id = TEXT("Service:") + Name;
                    Node.ParentId = *ParentId;
                    Node.ClassPath = FSoftClassPath(ServiceInstance->GetClass()->GetPathName());
                    Node.DisplayName = Name;
                    Node.DeclarationOrder = ++Context.DeclarationOrder;
                    Node.SourceLocation.LuaModule = Context.IR->SourceModule;
                    if (!ExtractProperties(
                            ServiceInstance,
                            *Context.IR,
                            Node.Properties,
                            *Context.Diagnostics))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /** 把任意 Unicode FString 转义为 Lua 双引号字符串。 */
    FString QuoteLuaString(const FString& Input)
    {
        FString Result(TEXT("\""));
        for (int32 Index = 0; Index < Input.Len(); ++Index)
        {
            const TCHAR Character = Input[Index];
            if (Character == TEXT('\\')) Result += TEXT("\\\\");
            else if (Character == TEXT('"')) Result += TEXT("\\\"");
            else if (Character == TEXT('\r')) Result += TEXT("\\r");
            else if (Character == TEXT('\n')) Result += TEXT("\\n");
            else if (Character == TEXT('\t')) Result += TEXT("\\t");
            else if (Character < 32) Result += FString::Printf(TEXT("\\%03d"), static_cast<int32>(Character));
            else Result.AppendChar(Character);
        }
        Result += TEXT("\"");
        return Result;
    }

    /** 把语义名转换为唯一、合法且确定性的 Lua local 标识符。 */
    FString MakeLuaIdentifier(const FString& Input, TSet<FString>& UsedIdentifiers)
    {
        FString Result;
        for (int32 Index = 0; Index < Input.Len(); ++Index)
        {
            const TCHAR Character = Input[Index];
            const bool bValid = FChar::IsAlnum(Character) || Character == TEXT('_');
            Result.AppendChar(bValid ? Character : TEXT('_'));
        }
        if (Result.IsEmpty() || FChar::IsDigit(Result[0])) Result = TEXT("node_") + Result;
        FString Candidate = Result;
        int32 Suffix = 2;
        while (UsedIdentifiers.Contains(Candidate))
        {
            Candidate = FString::Printf(TEXT("%s_%d"), *Result, Suffix);
            ++Suffix;
        }
        UsedIdentifiers.Add(Candidate);
        return Candidate;
    }

    /** 递归把一个 IR 值渲染为 LuaBehaviorTree.Value 构造器。 */
    FString RenderValue(
        const FSekiroBehaviorTreeIR& IR,
        const int32 ValueIndex,
        const int32 Indent)
    {
        if (!IR.Values.IsValidIndex(ValueIndex)) return TEXT("nil");
        const FSekiroBehaviorTreeIRValue& Value = IR.Values[ValueIndex];
        const TCHAR* TypeName = TEXT("String");
        if (Value.Type == ESekiroBehaviorTreeValueType::Bool)
            return FString::Printf(TEXT("LuaBehaviorTree.Value.Bool(%s)"), Value.BoolValue ? TEXT("true") : TEXT("false"));
        if (Value.Type == ESekiroBehaviorTreeValueType::Integer)
            return FString::Printf(TEXT("LuaBehaviorTree.Value.Integer(%lld)"), Value.IntegerValue);
        if (Value.Type == ESekiroBehaviorTreeValueType::Float)
            return FString::Printf(TEXT("LuaBehaviorTree.Value.Float(%.17g)"), Value.FloatValue);
        if (Value.Type == ESekiroBehaviorTreeValueType::Name) TypeName = TEXT("Name");
        else if (Value.Type == ESekiroBehaviorTreeValueType::Text) TypeName = TEXT("Text");
        else if (Value.Type == ESekiroBehaviorTreeValueType::Enum) TypeName = TEXT("Enum");
        else if (Value.Type == ESekiroBehaviorTreeValueType::Object) TypeName = TEXT("Object");
        else if (Value.Type == ESekiroBehaviorTreeValueType::SoftObject) TypeName = TEXT("SoftObject");
        else if (Value.Type == ESekiroBehaviorTreeValueType::Class) TypeName = TEXT("Class");
        else if (Value.Type == ESekiroBehaviorTreeValueType::SoftClass) TypeName = TEXT("SoftClass");
        if (Value.Type != ESekiroBehaviorTreeValueType::Struct
            && Value.Type != ESekiroBehaviorTreeValueType::Array)
        {
            return FString::Printf(
                TEXT("LuaBehaviorTree.Value.%s(%s)"),
                TypeName,
                *QuoteLuaString(Value.StringValue));
        }

        const FString CurrentIndent = FString::ChrN(Indent, TEXT(' '));
        const FString ChildIndent = FString::ChrN(Indent + 4, TEXT(' '));
        FString Result = Value.Type == ESekiroBehaviorTreeValueType::Struct
            ? TEXT("LuaBehaviorTree.Value.Struct({\r\n")
            : TEXT("LuaBehaviorTree.Value.Array({\r\n");
        if (Value.Type == ESekiroBehaviorTreeValueType::Struct)
        {
            TArray<FName> FieldNames;
            Value.StructFields.GetKeys(FieldNames);
            FieldNames.Sort([](const FName& Left, const FName& Right)
            {
                return Left.LexicalLess(Right);
            });
            for (const FName FieldName : FieldNames)
            {
                Result += ChildIndent
                    + TEXT("[") + QuoteLuaString(FieldName.ToString()) + TEXT("] = ")
                    + RenderValue(IR, Value.StructFields[FieldName], Indent + 4)
                    + TEXT(",\r\n");
            }
        }
        else
        {
            for (const int32 ChildIndex : Value.ArrayItems)
                Result += ChildIndent + RenderValue(IR, ChildIndex, Indent + 4) + TEXT(",\r\n");
        }
        Result += CurrentIndent + TEXT("})");
        return Result;
    }

    /** 把属性数组渲染为稳定、按名称排序的 Lua table；空数组输出 nil。 */
    FString RenderProperties(
        const FSekiroBehaviorTreeIR& IR,
        const TArray<FSekiroBehaviorTreeIRProperty>& Properties,
        const int32 Indent)
    {
        if (Properties.IsEmpty()) return TEXT("nil");
        TArray<const FSekiroBehaviorTreeIRProperty*> SortedProperties;
        for (const FSekiroBehaviorTreeIRProperty& Property : Properties)
            SortedProperties.Add(&Property);
        SortedProperties.Sort([](
            const FSekiroBehaviorTreeIRProperty& Left,
            const FSekiroBehaviorTreeIRProperty& Right)
        {
            return Left.Name.LexicalLess(Right.Name);
        });
        const FString CurrentIndent = FString::ChrN(Indent, TEXT(' '));
        const FString ChildIndent = FString::ChrN(Indent + 4, TEXT(' '));
        FString Result(TEXT("{\r\n"));
        for (const FSekiroBehaviorTreeIRProperty* Property : SortedProperties)
        {
            Result += ChildIndent
                + TEXT("[") + QuoteLuaString(Property->Name.ToString()) + TEXT("] = ")
                + RenderValue(IR, Property->ValueIndex, Indent + 4)
                + TEXT(",\r\n");
        }
        Result += CurrentIndent + TEXT("}");
        return Result;
    }

    /** 把规范 IR 渲染为可读、UTF-8 无 BOM、CRLF 的 Lua DSL 源码。 */
    FString RenderLuaSource(const FSekiroBehaviorTreeIR& IR)
    {
        FString Source;
        Source += TEXT("-- Lua 类型：纯 Lua 行为树同步副本。由 BehaviorTree → Lua 显式导出生成。\r\n");
        Source += TEXT("-- BehaviorTree 资产保持运行与编辑权威；本文件不会自动回写资产。\r\n\r\n");
        Source += TEXT("local LuaBehaviorTree = require(\"AI.Compiler.LuaBehaviorTree\")\r\n\r\n");
        Source += TEXT("local Definition = LuaBehaviorTree.New({\r\n");
        Source += TEXT("    SourceModule = ") + QuoteLuaString(IR.SourceModule) + TEXT(",\r\n");
        if (!IR.ParentBlackboard.IsNull())
            Source += TEXT("    ParentBlackboard = ") + QuoteLuaString(IR.ParentBlackboard.ToString()) + TEXT(",\r\n");
        Source += TEXT("})\r\n\r\n");

        if (!IR.BlackboardKeys.IsEmpty())
        {
            Source += TEXT("---声明当前 BehaviorTree 资产的本地 Blackboard Keys。\r\n");
            Source += TEXT("---@param blackboard LuaBehaviorTreeDefinition Blackboard 声明器。\r\n");
            Source += TEXT("---@return nil\r\n");
            Source += TEXT("function Definition:DeclareBlackboard(blackboard)\r\n");
            for (const FSekiroBlackboardIRKey& Key : IR.BlackboardKeys)
            {
                Source += TEXT("    blackboard:Key(\r\n");
                Source += TEXT("        ") + QuoteLuaString(Key.ClassPath.ToString()) + TEXT(",\r\n");
                Source += TEXT("        ") + QuoteLuaString(Key.Name.ToString()) + TEXT(",\r\n");
                Source += TEXT("        ") + RenderProperties(IR, Key.Properties, 8) + TEXT(",\r\n");
                Source += FString::Printf(TEXT("        %s)\r\n"), Key.bInstanceSynced ? TEXT("true") : TEXT("false"));
            }
            Source += TEXT("end\r\n\r\n");
        }

        Source += TEXT("---声明从当前 BehaviorTree Graph 显式导出的主节点与附属节点。\r\n");
        Source += TEXT("---@param tree LuaBehaviorTreeDefinition 行为树声明器。\r\n");
        Source += TEXT("---@return nil\r\n");
        Source += TEXT("function Definition:BehaviorTree(tree)\r\n");
        TMap<FString, FString> VariablesById;
        TSet<FString> UsedIdentifiers;
        for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
        {
            const FString Variable = MakeLuaIdentifier(Node.DisplayName, UsedIdentifiers);
            VariablesById.Add(Node.Id, Variable);
            const FString* ParentVariable = VariablesById.Find(Node.ParentId);
            UClass* NodeClass = Node.ClassPath.TryLoadClass<UBTNode>();
            const TCHAR* FunctionName = NodeClass && NodeClass->IsChildOf(UBTCompositeNode::StaticClass())
                ? TEXT("Composite")
                : TEXT("Task");
            Source += TEXT("    local ") + Variable + TEXT(" = ")
                + (ParentVariable ? *ParentVariable : FString(TEXT("tree")))
                + TEXT(":") + FunctionName + TEXT("(\r\n");
            Source += TEXT("        ") + QuoteLuaString(Node.ClassPath.ToString()) + TEXT(",\r\n");
            Source += TEXT("        ") + QuoteLuaString(Node.DisplayName);
            if (!Node.Properties.IsEmpty())
                Source += TEXT(",\r\n        ") + RenderProperties(IR, Node.Properties, 8);
            Source += TEXT(")\r\n");
        }
        for (const FSekiroBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            const FString* ParentVariable = VariablesById.Find(Decorator.ParentId);
            if (!ParentVariable) continue;
            Source += TEXT("    ") + *ParentVariable + TEXT(":Decorator(\r\n");
            Source += TEXT("        ") + QuoteLuaString(Decorator.ClassPath.ToString()) + TEXT(",\r\n");
            Source += TEXT("        ") + QuoteLuaString(Decorator.DisplayName);
            if (!Decorator.Properties.IsEmpty())
                Source += TEXT(",\r\n        ") + RenderProperties(IR, Decorator.Properties, 8);
            Source += TEXT(")\r\n");
        }
        for (const FSekiroBehaviorTreeIRNode& Service : IR.Services)
        {
            const FString* ParentVariable = VariablesById.Find(Service.ParentId);
            if (!ParentVariable) continue;
            Source += TEXT("    ") + *ParentVariable + TEXT(":Service(\r\n");
            Source += TEXT("        ") + QuoteLuaString(Service.ClassPath.ToString()) + TEXT(",\r\n");
            Source += TEXT("        ") + QuoteLuaString(Service.DisplayName);
            if (!Service.Properties.IsEmpty())
                Source += TEXT(",\r\n        ") + RenderProperties(IR, Service.Properties, 8);
            Source += TEXT(")\r\n");
        }
        Source += TEXT("end\r\n\r\nreturn Definition\r\n");
        return Source;
    }

    /** 递归比较两个 IR 强类型值，不要求值池索引相同。 */
    bool AreValuesEquivalent(
        const FSekiroBehaviorTreeIR& LeftIR,
        const int32 LeftIndex,
        const FSekiroBehaviorTreeIR& RightIR,
        const int32 RightIndex)
    {
        if (!LeftIR.Values.IsValidIndex(LeftIndex) || !RightIR.Values.IsValidIndex(RightIndex)) return false;
        const FSekiroBehaviorTreeIRValue& Left = LeftIR.Values[LeftIndex];
        const FSekiroBehaviorTreeIRValue& Right = RightIR.Values[RightIndex];
        if (Left.Type != Right.Type
            || Left.BoolValue != Right.BoolValue
            || Left.IntegerValue != Right.IntegerValue
            || Left.FloatValue != Right.FloatValue
            || Left.StringValue != Right.StringValue
            || Left.ArrayItems.Num() != Right.ArrayItems.Num()
            || Left.StructFields.Num() != Right.StructFields.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < Left.ArrayItems.Num(); ++Index)
        {
            if (!AreValuesEquivalent(LeftIR, Left.ArrayItems[Index], RightIR, Right.ArrayItems[Index])) return false;
        }
        for (const TPair<FName, int32>& Pair : Left.StructFields)
        {
            const int32* RightChild = Right.StructFields.Find(Pair.Key);
            if (!RightChild || !AreValuesEquivalent(LeftIR, Pair.Value, RightIR, *RightChild)) return false;
        }
        return true;
    }

    /** 比较两组属性的名称与递归值，忽略值池物理索引。 */
    bool ArePropertiesEquivalent(
        const FSekiroBehaviorTreeIR& LeftIR,
        const TArray<FSekiroBehaviorTreeIRProperty>& Left,
        const FSekiroBehaviorTreeIR& RightIR,
        const TArray<FSekiroBehaviorTreeIRProperty>& Right)
    {
        if (Left.Num() != Right.Num()) return false;
        for (int32 Index = 0; Index < Left.Num(); ++Index)
        {
            if (Left[Index].Name != Right[Index].Name
                || !AreValuesEquivalent(
                    LeftIR,
                    Left[Index].ValueIndex,
                    RightIR,
                    Right[Index].ValueIndex))
            {
                return false;
            }
        }
        return true;
    }

    /** 比较 round-trip 前后的关键拓扑、类、属性和 Blackboard 契约。 */
    bool AreIRsEquivalent(
        const FSekiroBehaviorTreeIR& Left,
        const FSekiroBehaviorTreeIR& Right)
    {
        if (Left.SourceModule != Right.SourceModule
            || Left.RootNodeId != Right.RootNodeId
            || Left.ParentBlackboard != Right.ParentBlackboard
            || Left.Nodes.Num() != Right.Nodes.Num()
            || Left.Decorators.Num() != Right.Decorators.Num()
            || Left.Services.Num() != Right.Services.Num()
            || Left.BlackboardKeys.Num() != Right.BlackboardKeys.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < Left.Nodes.Num(); ++Index)
        {
            const FSekiroBehaviorTreeIRNode& A = Left.Nodes[Index];
            const FSekiroBehaviorTreeIRNode& B = Right.Nodes[Index];
            if (A.Id != B.Id || A.ParentId != B.ParentId || A.ClassPath != B.ClassPath
                || A.DisplayName != B.DisplayName
                || !ArePropertiesEquivalent(Left, A.Properties, Right, B.Properties)) return false;
        }
        for (int32 Index = 0; Index < Left.Decorators.Num(); ++Index)
        {
            const FSekiroBehaviorTreeIRNode& A = Left.Decorators[Index];
            const FSekiroBehaviorTreeIRNode& B = Right.Decorators[Index];
            if (A.Id != B.Id || A.ParentId != B.ParentId || A.ClassPath != B.ClassPath
                || !ArePropertiesEquivalent(Left, A.Properties, Right, B.Properties)) return false;
        }
        for (int32 Index = 0; Index < Left.Services.Num(); ++Index)
        {
            const FSekiroBehaviorTreeIRNode& A = Left.Services[Index];
            const FSekiroBehaviorTreeIRNode& B = Right.Services[Index];
            if (A.Id != B.Id || A.ParentId != B.ParentId || A.ClassPath != B.ClassPath
                || !ArePropertiesEquivalent(Left, A.Properties, Right, B.Properties)) return false;
        }
        for (int32 Index = 0; Index < Left.BlackboardKeys.Num(); ++Index)
        {
            const FSekiroBlackboardIRKey& A = Left.BlackboardKeys[Index];
            const FSekiroBlackboardIRKey& B = Right.BlackboardKeys[Index];
            if (A.Id != B.Id || A.Name != B.Name || A.ClassPath != B.ClassPath
                || A.bInstanceSynced != B.bInstanceSynced
                || !ArePropertiesEquivalent(Left, A.Properties, Right, B.Properties)) return false;
        }
        return true;
    }

    /** 使用同目录临时文件恢复旧内容；无旧文件时删除失败的新文件。 */
    void RestoreFile(
        const FString& TargetPath,
        const bool bHadOldFile,
        const TArray<uint8>& OldBytes)
    {
        IFileManager& FileManager = IFileManager::Get();
        if (!bHadOldFile)
        {
            FileManager.Delete(*TargetPath, false, true, true);
            return;
        }
        const FString RestorePath = TargetPath + TEXT(".restore.")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (FFileHelper::SaveArrayToFile(OldBytes, *RestorePath))
            FileManager.Move(*TargetPath, *RestorePath, true, true, false, true);
    }
}

/**
 * 从当前 BehaviorTree 编辑器 Graph 和 Blackboard 抽取规范 IR，不修改资产或文件。
 * 只能在游戏线程调用；真实 ClassPath 与属性来自 NodeInstance，Shell 仅提供拓扑和附属关系。
 *
 * @param BehaviorTree 当前编辑器资产，必须具有有效 BTGraph、根 Composite 和 Blackboard。
 * @param SourceModule 输出 IR 使用的合法 Lua 模块名。
 * @param OutIR 接收规范值语义 IR；函数开始时重置。
 * @param OutDiagnostics 接收 Graph、反射和容器错误；函数开始时清空。
 * @return 主树、附属节点和 Blackboard 全部无损提取时返回 true。
 */
bool USekiroBehaviorTreeExporterLibrary::ExtractBehaviorTreeIR(
    UBehaviorTree* BehaviorTree,
    const FString& SourceModule,
    FSekiroBehaviorTreeIR& OutIR,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace SekiroBehaviorTreeExporter;
    OutIR = FSekiroBehaviorTreeIR();
    OutDiagnostics.Reset();
    if (!IsInGameThread() || !BehaviorTree || !BehaviorTree->BTGraph || SourceModule.IsEmpty())
    {
        AddError(OutDiagnostics, InvalidAsset, TEXT("BehaviorTree、BTGraph 和 SourceModule 必须有效。"), SourceModule);
        return false;
    }

    const UAIGraphNode* VirtualRoot = nullptr;
    for (const UEdGraphNode* Node : BehaviorTree->BTGraph->Nodes)
    {
        if (Node && Node->GetClass()->GetPathName()
            == TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Root"))
        {
            VirtualRoot = Cast<UAIGraphNode>(Node);
            break;
        }
    }
    TArray<const UAIGraphNode*> RootChildren;
    CollectChildren(VirtualRoot, RootChildren);
    if (RootChildren.Num() != 1)
    {
        AddError(OutDiagnostics, GraphInvalid, TEXT("虚拟 Root 必须恰好连接一个 IR 根 Composite。"), BehaviorTree->GetPathName());
        return false;
    }

    OutIR.SchemaVersion = 1;
    OutIR.SourceModule = SourceModule;
    FExtractionContext Context;
    Context.IR = &OutIR;
    Context.Diagnostics = &OutDiagnostics;
    if (!ExtractMainSubtree(RootChildren[0], FString(), Context)
        || !ExtractAttachedNodes(Context))
    {
        return false;
    }

    UBlackboardData* Blackboard = BehaviorTree->BlackboardAsset;
    if (Blackboard)
    {
        if (Blackboard->Parent) OutIR.ParentBlackboard = FSoftObjectPath(Blackboard->Parent);
        for (const FBlackboardEntry& Entry : Blackboard->Keys)
        {
            if (!Entry.KeyType)
            {
                AddError(OutDiagnostics, InvalidAsset, TEXT("Blackboard Key 缺少 KeyType。"), Entry.EntryName.ToString());
                return false;
            }
            FSekiroBlackboardIRKey& Key = OutIR.BlackboardKeys.AddDefaulted_GetRef();
            Key.Id = TEXT("BlackboardKey:") + Entry.EntryName.ToString();
            Key.Name = Entry.EntryName;
            Key.ClassPath = FSoftClassPath(Entry.KeyType->GetClass()->GetPathName());
            Key.bInstanceSynced = Entry.bInstanceSynced;
            Key.DeclarationOrder = ++Context.DeclarationOrder;
            Key.SourceLocation.LuaModule = SourceModule;
            if (!ExtractProperties(Entry.KeyType, OutIR, Key.Properties, OutDiagnostics)) return false;
        }
    }
    return USekiroBehaviorTreeIRLibrary::Validate(OutIR, OutDiagnostics);
}

/**
 * 显式把当前 BehaviorTree 导出到 Content/Script 下的 Lua 模块，并进行强制重载 round-trip。
 * 只能在非 PIE/SIE 游戏线程调用；使用同目录临时文件替换，验证失败会恢复旧文件或删除新文件。
 *
 * @param BehaviorTree 当前资产权威来源，不会因导出而重建 Graph。
 * @param LuaModuleName 映射到 Content/Script 的合法点分模块名。
 * @param bOverwrite 目标存在时是否允许覆盖；false 会稳定失败且不触碰旧文件。
 * @param OutFilePath 成功或路径解析成功时接收规范绝对目标路径。
 * @param OutDiagnostics 接收抽取、路径、文件和 round-trip 诊断。
 * @return 文件原子替换、强制重载和规范 IR 对比全部成功时返回 true。
 */
bool USekiroBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
    UBehaviorTree* BehaviorTree,
    const FString& LuaModuleName,
    const bool bOverwrite,
    FString& OutFilePath,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace SekiroBehaviorTreeExporter;
    OutFilePath.Reset();
    OutDiagnostics.Reset();
    if (!IsInGameThread() || IsPlaySessionActive())
    {
        AddError(OutDiagnostics, InvalidAsset, TEXT("BehaviorTree → Lua 在 PIE/SIE 期间被禁用。"), LuaModuleName);
        return false;
    }
    if (!ResolveLuaModuleFilePath(LuaModuleName, OutFilePath, OutDiagnostics)) return false;
    IFileManager& FileManager = IFileManager::Get();
    const bool bHadOldFile = FileManager.FileExists(*OutFilePath);
    if (bHadOldFile && !bOverwrite)
    {
        AddError(OutDiagnostics, FileExists, TEXT("目标 Lua 已存在且 bOverwrite=false。"), OutFilePath);
        return false;
    }

    FSekiroBehaviorTreeIR ExportedIR;
    if (!ExtractBehaviorTreeIR(BehaviorTree, LuaModuleName, ExportedIR, OutDiagnostics)) return false;
    const FString Source = RenderLuaSource(ExportedIR);
    TArray<uint8> OldBytes;
    if (bHadOldFile && !FFileHelper::LoadFileToArray(OldBytes, *OutFilePath))
    {
        AddError(OutDiagnostics, FileWriteFailed, TEXT("无法读取旧 Lua，已取消覆盖。"), OutFilePath);
        return false;
    }
    const FString Directory = FPaths::GetPath(OutFilePath);
    if (!FileManager.MakeDirectory(*Directory, true))
    {
        AddError(OutDiagnostics, FileWriteFailed, TEXT("无法创建 Lua 模块目录。"), Directory);
        return false;
    }
    const FString TemporaryPath = OutFilePath + TEXT(".tmp.")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveStringToFile(
            Source,
            *TemporaryPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !FileManager.Move(*OutFilePath, *TemporaryPath, true, true, false, true))
    {
        FileManager.Delete(*TemporaryPath, false, true, true);
        AddError(OutDiagnostics, FileWriteFailed, TEXT("Lua 临时文件写入或原子替换失败，旧文件未改变。"), OutFilePath);
        return false;
    }

    FSekiroBehaviorTreeIR RoundTripIR;
    TArray<FSekiroBehaviorTreeDiagnostic> RoundTripDiagnostics;
    const bool bRoundTripCompiled = USekiroBehaviorTreeIRLibrary::CompileLuaModuleFresh(
        LuaModuleName,
        RoundTripIR,
        RoundTripDiagnostics);
    if (!bRoundTripCompiled || !AreIRsEquivalent(ExportedIR, RoundTripIR))
    {
        RestoreFile(OutFilePath, bHadOldFile, OldBytes);
        FSekiroBehaviorTreeIR RestoredIR;
        TArray<FSekiroBehaviorTreeDiagnostic> RestoreDiagnostics;
        USekiroBehaviorTreeIRLibrary::CompileLuaModuleFresh(
            LuaModuleName,
            RestoredIR,
            RestoreDiagnostics);
        OutDiagnostics.Append(RoundTripDiagnostics);
        AddError(OutDiagnostics, RoundTripFailed, TEXT("新 Lua round-trip 与资产 IR 不等价，已恢复旧文件。"), OutFilePath);
        return false;
    }

    FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
    USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(BehaviorTree, Configuration);
    Configuration.LuaModuleName = LuaModuleName;
    Configuration.SourceMode = ESekiroLuaBehaviorTreeSourceMode::BehaviorTree;
    USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(BehaviorTree, Configuration);
    return true;
}

/**
 * 把点分 Lua 模块名安全映射到项目 Content/Script 目录内的绝对 .lua 路径。
 * 本函数不访问或创建文件；拒绝空段、路径分隔符、绝对路径、.. 和非法标识符段。
 *
 * @param LuaModuleName 待解析模块名，每段必须是 Lua 标识符。
 * @param OutFilePath 成功时接收规范绝对路径；函数开始时重置。
 * @param OutDiagnostics 接收稳定路径错误；函数不清除调用方已有诊断。
 * @return 规范目标仍位于 Content/Script 根目录内时返回 true。
 */
bool USekiroBehaviorTreeExporterLibrary::ResolveLuaModuleFilePath(
    const FString& LuaModuleName,
    FString& OutFilePath,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace SekiroBehaviorTreeExporter;
    OutFilePath.Reset();
    if (LuaModuleName.IsEmpty()
        || LuaModuleName.Contains(TEXT("/"))
        || LuaModuleName.Contains(TEXT("\\"))
        || LuaModuleName.Contains(TEXT(":"))
        || LuaModuleName.Contains(TEXT("..")))
    {
        AddError(OutDiagnostics, InvalidModulePath, TEXT("Lua 模块名为空或包含路径逃逸字符。"), LuaModuleName);
        return false;
    }
    TArray<FString> Segments;
    LuaModuleName.ParseIntoArray(Segments, TEXT("."), false);
    if (Segments.IsEmpty())
    {
        AddError(OutDiagnostics, InvalidModulePath, TEXT("Lua 模块名没有有效段。"), LuaModuleName);
        return false;
    }
    for (const FString& Segment : Segments)
    {
        if (Segment.IsEmpty()
            || !(FChar::IsAlpha(Segment[0]) || Segment[0] == TEXT('_')))
        {
            AddError(OutDiagnostics, InvalidModulePath, TEXT("Lua 模块段必须是非空标识符。"), Segment);
            return false;
        }
        for (int32 Index = 1; Index < Segment.Len(); ++Index)
        {
            if (!(FChar::IsAlnum(Segment[Index]) || Segment[Index] == TEXT('_')))
            {
                AddError(OutDiagnostics, InvalidModulePath, TEXT("Lua 模块段包含非法字符。"), Segment);
                return false;
            }
        }
    }
    FString RelativePath = FString::Join(Segments, TEXT("/")) + TEXT(".lua");
    FString ScriptRoot = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Script")));
    OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ScriptRoot, RelativePath));
    FPaths::NormalizeDirectoryName(ScriptRoot);
    FPaths::NormalizeFilename(OutFilePath);
    if (!FPaths::IsUnderDirectory(OutFilePath, ScriptRoot))
    {
        AddError(OutDiagnostics, InvalidModulePath, TEXT("规范化目标路径越出 Content/Script。"), OutFilePath);
        OutFilePath.Reset();
        return false;
    }
    return true;
}
