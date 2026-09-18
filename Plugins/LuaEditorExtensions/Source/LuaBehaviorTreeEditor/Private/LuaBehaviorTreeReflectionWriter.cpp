#include "LuaBehaviorTreeReflectionWriter.h"

#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    const FName PropertyNotFound(TEXT("BT.Reflection.PropertyNotFound"));
    const FName PropertyNotEditable(TEXT("BT.Reflection.PropertyNotEditable"));
    const FName PropertyTypeMismatch(TEXT("BT.Reflection.PropertyTypeMismatch"));
    const FName InvalidEnumValue(TEXT("BT.Reflection.InvalidEnumValue"));
    const FName AssetLoadFailed(TEXT("BT.Reflection.AssetLoadFailed"));
    const FName UnsupportedContainer(TEXT("BT.Reflection.UnsupportedContainer"));
    const FName InvalidValueIndex(TEXT("BT.Reflection.InvalidValueIndex"));

    /**
     * 判断强类型 IR 标签是否属于所有字符串承载类型。
     *
     * @param Type 待判断标签。
     * @return String、Name、Text、对象路径或类路径标签返回 true。
     */
    bool IsStringBackedType(const ELuaBehaviorTreeValueType Type)
    {
        return Type == ELuaBehaviorTreeValueType::String
            || Type == ELuaBehaviorTreeValueType::Name
            || Type == ELuaBehaviorTreeValueType::Text
            || Type == ELuaBehaviorTreeValueType::Object
            || Type == ELuaBehaviorTreeValueType::SoftObject
            || Type == ELuaBehaviorTreeValueType::Class
            || Type == ELuaBehaviorTreeValueType::SoftClass;
    }
}

/**
 * 将 IR 属性按名称写入 UObject 的可编辑 FProperty。
 * 只允许游戏线程调用；函数不会调用 PostEditChangeProperty、保存资产或保留外部引用。
 *
 * @param Target 目标节点或 KeyType 实例，不能为空。
 * @param Properties 属性名和值池根索引。
 * @param Values 只读强类型值池。
 * @param SourceLocation 所属 Lua 对象定位。
 * @param OutDiagnostics 接收全部属性错误，不静默忽略。
 * @return 所有属性均找到、可编辑且类型匹配时返回 true。
 */
bool FLuaBehaviorTreeReflectionWriter::ApplyProperties(
    UObject* Target,
    const TArray<FLuaBehaviorTreeIRProperty>& Properties,
    const TArray<FLuaBehaviorTreeIRValue>& Values,
    const FLuaBehaviorTreeSourceLocation& SourceLocation,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    if (!Target || !IsInGameThread()) return false;

    bool bSuccess = true;
    for (const FLuaBehaviorTreeIRProperty& IRProperty : Properties)
    {
        const FString Path = Target->GetClass()->GetPathName() + TEXT(".") + IRProperty.Name.ToString();
        FProperty* Property = Target->GetClass()->FindPropertyByName(IRProperty.Name);
        if (!Property)
        {
            AddError(PropertyNotFound, TEXT("反射属性不存在。"), Path, SourceLocation, OutDiagnostics);
            bSuccess = false;
            continue;
        }
        if (!Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
            || Property->IsA<FDelegateProperty>()
            || Property->IsA<FMulticastDelegateProperty>())
        {
            AddError(PropertyNotEditable, TEXT("属性不是可编辑配置，或属于 transient/deprecated/delegate 内部状态。"), Path, SourceLocation, OutDiagnostics);
            bSuccess = false;
            continue;
        }
        if (!Values.IsValidIndex(IRProperty.ValueIndex))
        {
            AddError(InvalidValueIndex, TEXT("属性引用了无效值池索引。"), Path, SourceLocation, OutDiagnostics);
            bSuccess = false;
            continue;
        }
        void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Target);
        if (!WriteValue(Property, ValueAddress, Values[IRProperty.ValueIndex], Values, Path, SourceLocation, OutDiagnostics))
            bSuccess = false;
    }
    return bSuccess;
}

/**
 * 将一个强类型 IR 值递归写入指定 FProperty 地址。
 * 调用方负责确保地址属于 Property；对象和类硬引用会同步加载，软引用只保存路径。
 *
 * @param Property 目标反射属性。
 * @param ValueAddress 目标属性值地址。
 * @param Value 当前强类型值。
 * @param Values 完整值池，供 Struct/Array 子索引读取。
 * @param Path 诊断属性路径。
 * @param SourceLocation Lua 源码定位。
 * @param OutDiagnostics 接收类型、路径或容器错误。
 * @return 值完成写入时返回 true；失败时目标可能只包含此前成功写入的结构字段。
 */
bool FLuaBehaviorTreeReflectionWriter::WriteValue(
    FProperty* Property,
    void* ValueAddress,
    const FLuaBehaviorTreeIRValue& Value,
    const TArray<FLuaBehaviorTreeIRValue>& Values,
    const FString& Path,
    const FLuaBehaviorTreeSourceLocation& SourceLocation,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Bool)
        {
            AddError(PropertyTypeMismatch, TEXT("Bool 属性要求 Value.Bool。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        BoolProperty->SetPropertyValue(ValueAddress, Value.BoolValue);
        return true;
    }
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Enum)
        {
            AddError(PropertyTypeMismatch, TEXT("Enum 属性要求 Value.Enum。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        const int64 EnumValue = Value.IntegerValue;
        if (!EnumProperty->GetEnum()->IsValidEnumValue(EnumValue))
        {
            AddError(InvalidEnumValue, TEXT("枚举底层数值无效。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValueAddress, static_cast<uint64>(EnumValue));
        return true;
    }
    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        if (ByteProperty->Enum)
        {
            if (Value.Type != ELuaBehaviorTreeValueType::Enum)
            {
                AddError(PropertyTypeMismatch, TEXT("枚举 Byte 属性要求 Value.Enum。"), Path, SourceLocation, OutDiagnostics);
                return false;
            }
            const int64 EnumValue = Value.IntegerValue;
            if (!ByteProperty->Enum->IsValidEnumValue(EnumValue)
                || EnumValue < 0
                || EnumValue > MAX_uint8)
            {
                AddError(InvalidEnumValue, TEXT("枚举底层数值无效或超出 uint8。"), Path, SourceLocation, OutDiagnostics);
                return false;
            }
            ByteProperty->SetPropertyValue(ValueAddress, static_cast<uint8>(EnumValue));
            return true;
        }
    }
    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            if (Value.Type != ELuaBehaviorTreeValueType::Integer)
            {
                AddError(PropertyTypeMismatch, TEXT("整数属性要求 Value.Integer。"), Path, SourceLocation, OutDiagnostics);
                return false;
            }
            if (!NumericProperty->CanHoldValue(Value.IntegerValue))
            {
                AddError(PropertyTypeMismatch, TEXT("整数值超出目标属性可表示范围。"), Path, SourceLocation, OutDiagnostics);
                return false;
            }
            NumericProperty->SetIntPropertyValue(ValueAddress, static_cast<uint64>(Value.IntegerValue));
            return true;
        }
        if (Value.Type != ELuaBehaviorTreeValueType::Float)
        {
            AddError(PropertyTypeMismatch, TEXT("浮点属性要求 Value.Float。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        NumericProperty->SetFloatingPointPropertyValue(ValueAddress, Value.FloatValue);
        return true;
    }
    if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::String)
        {
            AddError(PropertyTypeMismatch, TEXT("FString 属性要求 Value.String。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        StringProperty->SetPropertyValue(ValueAddress, Value.StringValue);
        return true;
    }
    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Name)
        {
            AddError(PropertyTypeMismatch, TEXT("FName 属性要求 Value.Name。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        NameProperty->SetPropertyValue(ValueAddress, FName(*Value.StringValue));
        return true;
    }
    if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Text)
        {
            AddError(PropertyTypeMismatch, TEXT("FText 属性要求 Value.Text。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        TextProperty->SetPropertyValue(ValueAddress, FText::FromString(Value.StringValue));
        return true;
    }
    if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::SoftClass)
        {
            AddError(PropertyTypeMismatch, TEXT("软类属性要求 Value.SoftClass。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        SoftClassProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(Value.StringValue)));
        return true;
    }
    if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::SoftObject)
        {
            AddError(PropertyTypeMismatch, TEXT("软对象属性要求 Value.SoftObject。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        SoftObjectProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(Value.StringValue)));
        return true;
    }
    if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Class)
        {
            AddError(PropertyTypeMismatch, TEXT("硬类属性要求 Value.Class。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        UClass* LoadedClass = Value.StringValue.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *Value.StringValue);
        if (!Value.StringValue.IsEmpty() && (!LoadedClass || !LoadedClass->IsChildOf(ClassProperty->MetaClass)))
        {
            AddError(AssetLoadFailed, TEXT("类路径无法加载或不满足 MetaClass。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        ClassProperty->SetObjectPropertyValue(ValueAddress, LoadedClass);
        return true;
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Object)
        {
            AddError(PropertyTypeMismatch, TEXT("硬对象属性要求 Value.Object。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        UObject* LoadedObject = Value.StringValue.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Value.StringValue);
        if (!Value.StringValue.IsEmpty() && (!LoadedObject || !LoadedObject->IsA(ObjectProperty->PropertyClass)))
        {
            AddError(AssetLoadFailed, TEXT("对象路径无法加载或类型不匹配。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(ValueAddress, LoadedObject);
        return true;
    }
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Struct)
        {
            AddError(PropertyTypeMismatch, TEXT("Struct 属性要求 Value.Struct。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        bool bSuccess = true;
        for (const TPair<FName, int32>& FieldPair : Value.StructFields)
        {
            FProperty* FieldProperty = StructProperty->Struct->FindPropertyByName(FieldPair.Key);
            if (!FieldProperty)
            {
                AddError(PropertyNotFound, TEXT("Struct 字段不存在。"), Path + TEXT(".") + FieldPair.Key.ToString(), SourceLocation, OutDiagnostics);
                bSuccess = false;
                continue;
            }
            if (FieldProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
                || FieldProperty->IsA<FDelegateProperty>()
                || FieldProperty->IsA<FMulticastDelegateProperty>())
            {
                AddError(PropertyNotEditable, TEXT("Struct 字段属于禁止写入的内部状态。"), Path + TEXT(".") + FieldPair.Key.ToString(), SourceLocation, OutDiagnostics);
                bSuccess = false;
                continue;
            }
            if (!Values.IsValidIndex(FieldPair.Value))
            {
                AddError(InvalidValueIndex, TEXT("Struct 字段值池索引无效。"), Path, SourceLocation, OutDiagnostics);
                bSuccess = false;
                continue;
            }
            void* FieldAddress = FieldProperty->ContainerPtrToValuePtr<void>(ValueAddress);
            if (!WriteValue(FieldProperty, FieldAddress, Values[FieldPair.Value], Values, Path + TEXT(".") + FieldPair.Key.ToString(), SourceLocation, OutDiagnostics))
                bSuccess = false;
        }
        return bSuccess;
    }
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        if (Value.Type != ELuaBehaviorTreeValueType::Array)
        {
            AddError(PropertyTypeMismatch, TEXT("Array 属性要求 Value.Array。"), Path, SourceLocation, OutDiagnostics);
            return false;
        }
        FScriptArrayHelper ArrayHelper(ArrayProperty, ValueAddress);
        ArrayHelper.Resize(Value.ArrayItems.Num());
        bool bSuccess = true;
        for (int32 ItemIndex = 0; ItemIndex < Value.ArrayItems.Num(); ++ItemIndex)
        {
            const int32 ValueIndex = Value.ArrayItems[ItemIndex];
            if (!Values.IsValidIndex(ValueIndex)
                || !WriteValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(ItemIndex), Values.IsValidIndex(ValueIndex) ? Values[ValueIndex] : FLuaBehaviorTreeIRValue(), Values,
                    FString::Printf(TEXT("%s[%d]"), *Path, ItemIndex), SourceLocation, OutDiagnostics))
            {
                if (!Values.IsValidIndex(ValueIndex))
                    AddError(InvalidValueIndex, TEXT("Array 元素值池索引无效。"), Path, SourceLocation, OutDiagnostics);
                bSuccess = false;
            }
        }
        return bSuccess;
    }
    if (Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
    {
        AddError(UnsupportedContainer, TEXT("当前版本暂不支持 Set/Map；请改用可编辑 Struct 或 Array。"), Path, SourceLocation, OutDiagnostics);
        return false;
    }
    if (IsStringBackedType(Value.Type))
    {
        AddError(PropertyTypeMismatch, TEXT("字符串承载值与目标 FProperty 类型不匹配。"), Path, SourceLocation, OutDiagnostics);
        return false;
    }
    AddError(PropertyTypeMismatch, TEXT("目标 FProperty 类型尚未支持。"), Path, SourceLocation, OutDiagnostics);
    return false;
}

/**
 * 追加一条反射写入错误；不记录日志、不抛异常。
 *
 * @param Code 稳定机器错误码。
 * @param Message 中文说明。
 * @param Path 目标属性路径。
 * @param SourceLocation Lua 源码定位。
 * @param OutDiagnostics 诊断接收数组。
 */
void FLuaBehaviorTreeReflectionWriter::AddError(
    const FName Code,
    const FString& Message,
    const FString& Path,
    const FLuaBehaviorTreeSourceLocation& SourceLocation,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    FLuaBehaviorTreeDiagnostic& Diagnostic = OutDiagnostics.AddDefaulted_GetRef();
    Diagnostic.Code = Code;
    Diagnostic.Message = Message;
    Diagnostic.Path = Path;
    Diagnostic.SourceLocation = SourceLocation;
}
