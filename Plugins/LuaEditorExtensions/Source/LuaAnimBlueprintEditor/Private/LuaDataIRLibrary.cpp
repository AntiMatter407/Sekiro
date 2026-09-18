#include "LuaDataIRLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "EnumColumn.h"
#include "HAL/FileManager.h"
#include "LuaEnv.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "MultiEnumColumn.h"
#include "ObjectChooser_Asset.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnLuaModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "lua.hpp"

namespace LuaDataIR
{
    constexpr int32 MaximumValueDepth = 64;

    struct FCanonicalObjectMember
    {
        FString Key;                      // 已复制并用于排序的对象键
        FString JsonValue;                // 对应值的 Canonical JSON
    };

#if WITH_EDITOR
    struct FObjectPropertyPatchSpec
    {
        FString TargetObjectPath;                         // 待修改对象的完整软路径
        UClass* ExpectedClass = nullptr;                  // 调用方声明的目标类型约束
        TArray<TSharedPtr<FJsonValue>> Assignments;       // 按声明顺序保存的强类型属性赋值
    };

    struct FChooserColumnSpec
    {
        FString Id;                       // IR 中稳定且唯一的列标识
        TArray<FName> BindingPath;         // 从上下文类开始解析的反射属性链
        const UEnum* Enum = nullptr;       // 叶属性解析出的枚举类型
    };

    struct FChooserRowSpec
    {
        UObject* ResultObject = nullptr;   // 已加载且满足结果类约束的行结果对象
        TArray<uint32> ColumnMasks;        // 按 Columns 顺序排列的枚举允许值位掩码
    };

    struct FChooserAssetSpec
    {
        FString TargetObjectPath;          // 目标 Chooser Table 的完整对象路径
        FString PackageName;               // 从目标对象路径拆出的长包名
        FString AssetName;                 // 从目标对象路径拆出的资产名
        UClass* ContextClass = nullptr;     // Chooser 读取属性的上下文类
        UClass* ResultClass = nullptr;      // 所有结果对象必须继承的类
        TArray<FChooserColumnSpec> Columns; // 通用枚举过滤列
        TArray<FChooserRowSpec> Rows;       // 已解析并按列对齐的结果行
    };

    struct FChooserTableSnapshot
    {
        EObjectChooserResultType ResultType = EObjectChooserResultType::ObjectResult; // 修改前的结果类型
        UClass* OutputObjectType = nullptr;                                            // 修改前的结果对象类
        TArray<FInstancedStruct> ContextData;                                          // 修改前的上下文声明
        UChooserTable* RootChooser = nullptr;                                          // 修改前的根 Chooser 引用
        FInstancedStruct FallbackResult;                                               // 修改前的回退结果
        TArray<FInstancedStruct> ResultsStructs;                                       // 修改前的编辑器结果行
        TArray<bool> DisabledRows;                                                      // 修改前的禁用行状态
        TArray<TObjectPtr<UChooserTable>> NestedChoosers;                              // 修改前的内嵌 Chooser
        TArray<TObjectPtr<UObject>> NestedObjects;                                     // 修改前的内嵌对象
        UChooserTable* ParentTable = nullptr;                                           // 修改前的旧版父表引用
        uint32 Version = 0;                                                            // 修改前的 Chooser 数据版本
        TArray<FInstancedStruct> CookedResults;                                        // 修改前的运行时结果缓存
        TArray<FInstancedStruct> ColumnsStructs;                                       // 修改前的过滤列
    };

    bool ReadRequiredString(
        const TSharedPtr<FJsonObject>& Object,
        FStringView FieldName,
        const FString& ValuePath,
        FString& OutValue,
        FString& OutError);

    bool ReadObjectValue(
        const TSharedPtr<FJsonValue>& Value,
        const FString& ValuePath,
        TSharedPtr<FJsonObject>& OutObject,
        FString& OutError);

    bool ApplyReflectedAssignments(
        UStruct* OwnerStruct,
        void* OwnerAddress,
        UObject* InstanceOuter,
        const TArray<TSharedPtr<FJsonValue>>& AssignmentValues,
        const FString& ValuePath,
        TArray<FProperty*>* OutTopLevelProperties,
        FString& OutError);

    /**
     * 保存一个顶层反射属性的真实原值，供同一调用内失败时恢复。
     * 与 DuplicateObject 快照不同，此处保留原 Instanced UObject 引用，不会把瞬态副本写回正式资产。
     */
    class FReflectedPropertySnapshot
    {
    public:
        FReflectedPropertySnapshot(FProperty* InProperty, const void* SourceContainer)
            : Property(InProperty)
        {
            check(Property && SourceContainer);
            Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
            Property->InitializeValue(Value);
            Property->CopyCompleteValue(
                Value,
                Property->ContainerPtrToValuePtr<const void>(SourceContainer));
        }

        ~FReflectedPropertySnapshot()
        {
            if (Value)
            {
                Property->DestroyValue(Value);
                FMemory::Free(Value);
            }
        }

        FReflectedPropertySnapshot(const FReflectedPropertySnapshot&) = delete;
        FReflectedPropertySnapshot& operator=(const FReflectedPropertySnapshot&) = delete;

        void Restore(void* TargetContainer) const
        {
            check(Property && Value && TargetContainer);
            Property->CopyCompleteValue(
                Property->ContainerPtrToValuePtr<void>(TargetContainer),
                Value);
        }

    private:
        FProperty* Property = nullptr; // 目标类型拥有的稳定反射描述
        void* Value = nullptr;         // 已初始化并深拷贝容器、浅保留 UObject 引用的属性存储
    };

    /**
     * 将一个带 ValueType 标签的 JSON 纯值写入目标反射属性。
     * 本函数只根据实际 FProperty 类型分派，不识别项目字段、资产类别或领域结构；Struct 与 Array
     * 会递归消费同一强类型协议。必须在编辑器游戏线程调用，因为对象和类值可能触发资产加载。
     *
     * @param Property 待写入的反射属性，不可为空。
     * @param ValueAddress Property 对应的已初始化存储地址，不可为空。
     * @param InstanceOuter 新建 Instanced UObject 的 Outer；递归进入该对象后切换为新对象自身。
     * @param TypedValue 包含 ValueType 与 Value 的强类型 JSON 对象。
     * @param ValuePath 用于错误信息的完整值路径。
     * @param OutError 接收首个类型、范围、资产加载或嵌套结构错误；成功时不修改。
     * @return 标签与反射属性匹配且值已完整写入时返回 true。
     */
    bool ApplyReflectedValue(
        FProperty* Property,
        void* ValueAddress,
        UObject* InstanceOuter,
        const TSharedPtr<FJsonObject>& TypedValue,
        const FString& ValuePath,
        FString& OutError)
    {
        FString ValueType;
        if (!ReadRequiredString(
            TypedValue,
            TEXT("ValueType"),
            ValuePath + TEXT(".ValueType"),
            ValueType,
            OutError))
        {
            return false;
        }

        const TSharedPtr<FJsonValue> JsonValue = TypedValue->TryGetField(TEXT("Value"));
        if (!JsonValue.IsValid())
        {
            OutError = FString::Printf(TEXT("%s.Value is required."), *ValuePath);
            return false;
        }

        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            bool bValue = false;
            if (ValueType != TEXT("Bool") || !JsonValue->TryGetBool(bValue))
            {
                OutError = FString::Printf(TEXT("%s must use Bool for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            BoolProperty->SetPropertyValue(ValueAddress, bValue);
            return true;
        }

        if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            double NumericValue = 0.0;
            if (ValueType != TEXT("Enum")
                || !JsonValue->TryGetNumber(NumericValue)
                || !FMath::IsFinite(NumericValue)
                || FMath::FloorToDouble(NumericValue) != NumericValue
                || NumericValue < static_cast<double>(MIN_int64)
                || NumericValue >= 9223372036854775808.0)
            {
                OutError = FString::Printf(TEXT("%s must use an integral native Enum value for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            const int64 EnumValue = static_cast<int64>(NumericValue);
            if (!EnumProperty->GetEnum()->IsValidEnumValue(EnumValue))
            {
                OutError = FString::Printf(
                    TEXT("%s enum value %lld is not declared by '%s'."),
                    *ValuePath,
                    EnumValue,
                    *EnumProperty->GetEnum()->GetPathName());
                return false;
            }
            EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValueAddress, EnumValue);
            return true;
        }

        if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
        {
            double NumericValue = 0.0;
            if (ValueType != TEXT("Enum")
                || !JsonValue->TryGetNumber(NumericValue)
                || !FMath::IsFinite(NumericValue)
                || FMath::FloorToDouble(NumericValue) != NumericValue)
            {
                OutError = FString::Printf(TEXT("%s must use an integral native Enum value for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            const int64 EnumValue = static_cast<int64>(NumericValue);
            if (!ByteProperty->Enum->IsValidEnumValue(EnumValue)
                || EnumValue < 0
                || EnumValue > MAX_uint8)
            {
                OutError = FString::Printf(
                    TEXT("%s enum value %lld is invalid for '%s'."),
                    *ValuePath,
                    EnumValue,
                    *ByteProperty->Enum->GetPathName());
                return false;
            }
            ByteProperty->SetPropertyValue(ValueAddress, static_cast<uint8>(EnumValue));
            return true;
        }

        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            double NumberValue = 0.0;
            if (!JsonValue->TryGetNumber(NumberValue) || !FMath::IsFinite(NumberValue))
            {
                OutError = FString::Printf(TEXT("%s.Value must be a finite number."), *ValuePath);
                return false;
            }
            if (NumericProperty->IsInteger())
            {
                if (ValueType != TEXT("Integer")
                    || FMath::FloorToDouble(NumberValue) != NumberValue
                    || NumberValue < static_cast<double>(MIN_int64)
                    || NumberValue >= 9223372036854775808.0)
                {
                    OutError = FString::Printf(TEXT("%s must use an in-range integral Integer value for property '%s'."), *ValuePath, *Property->GetName());
                    return false;
                }
                const int64 IntegerValue = static_cast<int64>(NumberValue);
                if (!NumericProperty->CanHoldValue(IntegerValue))
                {
                    OutError = FString::Printf(TEXT("%s.Value is outside the range of property '%s'."), *ValuePath, *Property->GetName());
                    return false;
                }
                NumericProperty->SetIntPropertyValue(ValueAddress, IntegerValue);
                return true;
            }
            const double FloatMax = static_cast<double>(TNumericLimits<float>::Max());
            const bool bExceedsFloatRange = CastField<FFloatProperty>(NumericProperty)
                && (NumberValue < -FloatMax || NumberValue > FloatMax);
            if (ValueType != TEXT("Float") || bExceedsFloatRange)
            {
                OutError = FString::Printf(TEXT("%s must use an in-range Float value for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            NumericProperty->SetFloatingPointPropertyValue(ValueAddress, NumberValue);
            return true;
        }

        if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
        {
            FString StringValue;
            if (ValueType != TEXT("String") || !JsonValue->TryGetString(StringValue))
            {
                OutError = FString::Printf(TEXT("%s must use String for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            StringProperty->SetPropertyValue(ValueAddress, StringValue);
            return true;
        }

        if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            FString StringValue;
            if (ValueType != TEXT("Name") || !JsonValue->TryGetString(StringValue))
            {
                OutError = FString::Printf(TEXT("%s must use Name for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            NameProperty->SetPropertyValue(ValueAddress, FName(*StringValue));
            return true;
        }

        if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
        {
            FString StringValue;
            if (ValueType != TEXT("Text") || !JsonValue->TryGetString(StringValue))
            {
                OutError = FString::Printf(TEXT("%s must use Text for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            TextProperty->SetPropertyValue(ValueAddress, FText::FromString(StringValue));
            return true;
        }

        if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
        {
            FString ObjectPath;
            if (ValueType != TEXT("SoftClass") || !JsonValue->TryGetString(ObjectPath))
            {
                OutError = FString::Printf(TEXT("%s must use SoftClass for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            SoftClassProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(ObjectPath)));
            return true;
        }

        if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
        {
            FString ObjectPath;
            if (ValueType != TEXT("SoftObject") || !JsonValue->TryGetString(ObjectPath))
            {
                OutError = FString::Printf(TEXT("%s must use SoftObject for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            SoftObjectProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(ObjectPath)));
            return true;
        }

        if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
        {
            FString ObjectPath;
            if (ValueType != TEXT("Class") || !JsonValue->TryGetString(ObjectPath))
            {
                OutError = FString::Printf(TEXT("%s must use Class for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            UClass* LoadedClass = ObjectPath.IsEmpty()
                ? nullptr
                : LoadObject<UClass>(nullptr, *ObjectPath, {}, LOAD_NoWarn);
            if (!ObjectPath.IsEmpty()
                && (!LoadedClass || !LoadedClass->IsChildOf(ClassProperty->MetaClass)))
            {
                OutError = FString::Printf(TEXT("%s could not load a compatible class: %s"), *ValuePath, *ObjectPath);
                return false;
            }
            ClassProperty->SetObjectPropertyValue(ValueAddress, LoadedClass);
            return true;
        }

        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            if (ValueType == TEXT("InstancedObject"))
            {
                if (!InstanceOuter || !Property->HasAnyPropertyFlags(CPF_InstancedReference))
                {
                    OutError = FString::Printf(
                        TEXT("%s may use InstancedObject only for an Instanced object property with a valid outer."),
                        *ValuePath);
                    return false;
                }

                TSharedPtr<FJsonObject> InstanceSpec;
                if (!ReadObjectValue(JsonValue, ValuePath + TEXT(".Value"), InstanceSpec, OutError))
                {
                    return false;
                }

                FString ClassPath;
                if (!ReadRequiredString(
                    InstanceSpec,
                    TEXT("Class"),
                    ValuePath + TEXT(".Value.Class"),
                    ClassPath,
                    OutError))
                {
                    return false;
                }
                UClass* InstanceClass = LoadObject<UClass>(nullptr, *ClassPath, {}, LOAD_NoWarn);
                if (!InstanceClass
                    || !InstanceClass->IsChildOf(ObjectProperty->PropertyClass)
                    || InstanceClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
                {
                    OutError = FString::Printf(
                        TEXT("%s could not load a concrete instanced class derived from '%s': %s"),
                        *ValuePath,
                        *ObjectProperty->PropertyClass->GetPathName(),
                        *ClassPath);
                    return false;
                }

                const TArray<TSharedPtr<FJsonValue>>* InstanceProperties = nullptr;
                if (!InstanceSpec->TryGetArrayField(TEXT("Properties"), InstanceProperties)
                    || !InstanceProperties)
                {
                    OutError = FString::Printf(
                        TEXT("%s.Value.Properties must be an assignment array."),
                        *ValuePath);
                    return false;
                }

                UObject* Instance = NewObject<UObject>(
                    InstanceOuter,
                    InstanceClass,
                    NAME_None,
                    RF_Transactional);
                if (!Instance
                    || !ApplyReflectedAssignments(
                        InstanceClass,
                        Instance,
                        Instance,
                        *InstanceProperties,
                        ValuePath + TEXT(".Value.Properties"),
                        nullptr,
                        OutError))
                {
                    return false;
                }
                ObjectProperty->SetObjectPropertyValue(ValueAddress, Instance);
                return true;
            }

            FString ObjectPath;
            if (ValueType != TEXT("Object") || !JsonValue->TryGetString(ObjectPath))
            {
                OutError = FString::Printf(TEXT("%s must use Object for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            UObject* LoadedObject = ObjectPath.IsEmpty()
                ? nullptr
                : StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *ObjectPath, {}, LOAD_NoWarn);
            if (!ObjectPath.IsEmpty() && !LoadedObject)
            {
                OutError = FString::Printf(
                    TEXT("%s could not load an object of class '%s': %s"),
                    *ValuePath,
                    *ObjectProperty->PropertyClass->GetPathName(),
                    *ObjectPath);
                return false;
            }
            ObjectProperty->SetObjectPropertyValue(ValueAddress, LoadedObject);
            return true;
        }

        if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
            if (ValueType != TEXT("Struct") || !JsonValue->TryGetArray(Fields) || !Fields)
            {
                OutError = FString::Printf(TEXT("%s must use Struct with an assignment array for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }
            return ApplyReflectedAssignments(
                StructProperty->Struct,
                ValueAddress,
                InstanceOuter,
                *Fields,
                ValuePath + TEXT(".Value"),
                nullptr,
                OutError);
        }

        if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
            if (ValueType != TEXT("Array") || !JsonValue->TryGetArray(Elements) || !Elements)
            {
                OutError = FString::Printf(TEXT("%s must use Array for property '%s'."), *ValuePath, *Property->GetName());
                return false;
            }

            FScriptArrayHelper ArrayHelper(ArrayProperty, ValueAddress);
            ArrayHelper.EmptyAndAddValues(Elements->Num());
            for (int32 ElementIndex = 0; ElementIndex < Elements->Num(); ++ElementIndex)
            {
                TSharedPtr<FJsonObject> ElementObject;
                const FString ElementPath = FString::Printf(
                    TEXT("%s.Value[%d]"),
                    *ValuePath,
                    ElementIndex + 1);
                if (!ReadObjectValue((*Elements)[ElementIndex], ElementPath, ElementObject, OutError)
                    || !ApplyReflectedValue(
                        ArrayProperty->Inner,
                        ArrayHelper.GetRawPtr(ElementIndex),
                        InstanceOuter,
                        ElementObject,
                        ElementPath,
                        OutError))
                {
                    return false;
                }
            }
            return true;
        }

        OutError = FString::Printf(
            TEXT("%s property '%s' uses unsupported reflection type '%s'."),
            *ValuePath,
            *Property->GetName(),
            *Property->GetClass()->GetName());
        return false;
    }

    /**
     * 按声明顺序将属性赋值数组应用到 UObject 或 UScriptStruct 存储。
     * 本函数拒绝缺失和重复字段；嵌套调用不收集顶层属性，顶层调用可收集属性供失败回滚。
     * 必须在编辑器游戏线程调用；不会调用 Modify、PostEditChange 或保存包。
     *
     * @param OwnerStruct OwnerAddress 的反射类型，不可为空。
     * @param OwnerAddress 已初始化的对象或结构体存储，不可为空。
     * @param InstanceOuter 新建 Instanced UObject 的 Outer；结构体和数组递归时保持不变。
     * @param AssignmentValues 由 Name、ValueType、Value 组成的赋值对象数组。
     * @param ValuePath 用于错误信息的数组路径。
     * @param OutTopLevelProperties 可空；非空时接收本层全部唯一属性，用于事务失败回滚。
     * @param OutError 接收首个结构或写入错误；成功时不修改。
     * @return 全部字段存在、唯一且成功写入时返回 true。
     */
    bool ApplyReflectedAssignments(
        UStruct* OwnerStruct,
        void* OwnerAddress,
        UObject* InstanceOuter,
        const TArray<TSharedPtr<FJsonValue>>& AssignmentValues,
        const FString& ValuePath,
        TArray<FProperty*>* OutTopLevelProperties,
        FString& OutError)
    {
        if (!OwnerStruct || !OwnerAddress)
        {
            OutError = FString::Printf(TEXT("%s has no valid reflection owner or storage."), *ValuePath);
            return false;
        }

        TSet<FName> SeenProperties;
        for (int32 AssignmentIndex = 0; AssignmentIndex < AssignmentValues.Num(); ++AssignmentIndex)
        {
            const FString AssignmentPath = FString::Printf(
                TEXT("%s[%d]"),
                *ValuePath,
                AssignmentIndex + 1);
            TSharedPtr<FJsonObject> Assignment;
            if (!ReadObjectValue(
                AssignmentValues[AssignmentIndex],
                AssignmentPath,
                Assignment,
                OutError))
            {
                return false;
            }

            FString PropertyName;
            if (!ReadRequiredString(
                Assignment,
                TEXT("Name"),
                AssignmentPath + TEXT(".Name"),
                PropertyName,
                OutError))
            {
                return false;
            }
            const FName PropertyFName(*PropertyName);
            if (SeenProperties.Contains(PropertyFName))
            {
                OutError = FString::Printf(
                    TEXT("%s duplicates property '%s'."),
                    *ValuePath,
                    *PropertyName);
                return false;
            }
            SeenProperties.Add(PropertyFName);

            FProperty* Property = FindFProperty<FProperty>(OwnerStruct, PropertyFName);
            if (!Property)
            {
                OutError = FString::Printf(
                    TEXT("%s property '%s' was not found on '%s'."),
                    *AssignmentPath,
                    *PropertyName,
                    *OwnerStruct->GetPathName());
                return false;
            }
            void* ValueAddress = Property->ContainerPtrToValuePtr<void>(OwnerAddress);
            if (!ApplyReflectedValue(
                Property,
                ValueAddress,
                InstanceOuter,
                Assignment,
                AssignmentPath,
                OutError))
            {
                return false;
            }
            if (OutTopLevelProperties) OutTopLevelProperties->Add(Property);
        }
        return true;
    }

    /**
     * 将 Canonical JSON 解析为项目无关的 UObject 属性补丁规格。
     * 本函数只校验版本、目标、类型约束和强类型赋值外形，不解释任何属性名称或业务值。
     * 必须在编辑器游戏线程调用，因为类型约束解析会加载 UClass。
     *
     * @param CanonicalJson Lua 纯值编译器生成的完整 JSON 对象文本。
     * @param OutSpec 接收已解析目标、类型与赋值对象。
     * @param OutError 接收首个 JSON、协议、路径或类型加载错误；成功时为空。
     * @return 根协议有效且目标类型可解析时返回 true。
     */
    bool ParseObjectPropertyPatchSpec(
        const FString& CanonicalJson,
        FObjectPropertyPatchSpec& OutSpec,
        FString& OutError)
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CanonicalJson);
        TSharedPtr<FJsonObject> RootObject;
        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutError = TEXT("Object property patch IR root must be a valid JSON object.");
            return false;
        }

        int32 Version = 0;
        FString AssetType;
        FString ExpectedClassPath;
        if (!RootObject->TryGetNumberField(TEXT("Version"), Version) || Version != 1)
        {
            OutError = TEXT("$.Version must be supported integer version 1.");
            return false;
        }
        if (!ReadRequiredString(RootObject, TEXT("AssetType"), TEXT("$.AssetType"), AssetType, OutError)
            || !ReadRequiredString(
                RootObject,
                TEXT("TargetObjectPath"),
                TEXT("$.TargetObjectPath"),
                OutSpec.TargetObjectPath,
                OutError)
            || !ReadRequiredString(
                RootObject,
                TEXT("ExpectedClassPath"),
                TEXT("$.ExpectedClassPath"),
                ExpectedClassPath,
                OutError))
        {
            return false;
        }
        if (AssetType != TEXT("ObjectPropertyPatch"))
        {
            OutError = TEXT("$.AssetType must equal 'ObjectPropertyPatch'.");
            return false;
        }

        FText PathReason;
        if (!FPackageName::IsValidObjectPath(OutSpec.TargetObjectPath, &PathReason))
        {
            OutError = FString::Printf(
                TEXT("$.TargetObjectPath is not a valid object path: %s"),
                *PathReason.ToString());
            return false;
        }

        OutSpec.ExpectedClass = LoadObject<UClass>(nullptr, *ExpectedClassPath, {}, LOAD_NoWarn);
        if (!OutSpec.ExpectedClass)
        {
            OutError = FString::Printf(
                TEXT("$.ExpectedClassPath class could not be loaded: %s"),
                *ExpectedClassPath);
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* PropertyValues = nullptr;
        if (!RootObject->TryGetArrayField(TEXT("Properties"), PropertyValues)
            || !PropertyValues
            || PropertyValues->IsEmpty())
        {
            OutError = TEXT("$.Properties must be a non-empty array.");
            return false;
        }
        OutSpec.Assignments.Reset(PropertyValues->Num());
        for (int32 Index = 0; Index < PropertyValues->Num(); ++Index)
        {
            TSharedPtr<FJsonObject> Assignment;
            if (!ReadObjectValue(
                (*PropertyValues)[Index],
                FString::Printf(TEXT("$.Properties[%d]"), Index + 1),
                Assignment,
                OutError))
            {
                return false;
            }
            OutSpec.Assignments.Add((*PropertyValues)[Index]);
        }
        return true;
    }


    /**
     * 从 JSON 对象读取必填非空字符串，不接受缺失、类型错误或纯空白值。
     * 本函数只读取内存中的 JSON，可在调用方独占该对象的任意线程执行。
     *
     * @param Object 待读取的有效 JSON 对象。
     * @param FieldName 必填字段名。
     * @param ValuePath 用于错误信息的完整字段路径。
     * @param OutValue 接收未经裁剪的原始字符串。
     * @param OutError 接收首个字段错误；成功时不修改。
     * @return 字段存在、类型为字符串且包含非空白字符时返回 true。
     */
    bool ReadRequiredString(
        const TSharedPtr<FJsonObject>& Object,
        const FStringView FieldName,
        const FString& ValuePath,
        FString& OutValue,
        FString& OutError)
    {
        if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
        {
            OutError = FString::Printf(TEXT("%s must be a non-empty string."), *ValuePath);
            return false;
        }
        return true;
    }

    /**
     * 从 JSON 对象读取必填非空字符串数组，并可按调用方要求拒绝重复值。
     * 本函数只执行通用结构校验，不解释字符串的领域语义。
     *
     * @param Object 待读取的有效 JSON 对象。
     * @param FieldName 必填数组字段名。
     * @param ValuePath 用于错误信息的完整字段路径。
     * @param bRejectDuplicates 是否把重复字符串视为契约错误。
     * @param OutValues 接收保持声明顺序的字符串数组；失败时内容未定义。
     * @param OutError 接收首个数组或元素错误；成功时不修改。
     * @return 字段是至少含一个非空字符串且满足重复策略的数组时返回 true。
     */
    bool ReadRequiredStringArray(
        const TSharedPtr<FJsonObject>& Object,
        const FStringView FieldName,
        const FString& ValuePath,
        const bool bRejectDuplicates,
        TArray<FString>& OutValues,
        FString& OutError)
    {
        const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
        if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, JsonValues) || !JsonValues || JsonValues->IsEmpty())
        {
            OutError = FString::Printf(TEXT("%s must be a non-empty array."), *ValuePath);
            return false;
        }

        TSet<FString> SeenValues;
        OutValues.Reset(JsonValues->Num());
        for (int32 Index = 0; Index < JsonValues->Num(); ++Index)
        {
            FString Value;
            if (!(*JsonValues)[Index].IsValid()
                || !(*JsonValues)[Index]->TryGetString(Value)
                || Value.TrimStartAndEnd().IsEmpty())
            {
                OutError = FString::Printf(TEXT("%s[%d] must be a non-empty string."), *ValuePath, Index + 1);
                return false;
            }
            if (bRejectDuplicates && SeenValues.Contains(Value))
            {
                OutError = FString::Printf(TEXT("%s contains duplicate value '%s'."), *ValuePath, *Value);
                return false;
            }
            SeenValues.Add(Value);
            OutValues.Add(MoveTemp(Value));
        }
        return true;
    }

    /**
     * 从 Canonical JSON 读取由 Lua 原生 UENUM 序列化得到的非空整数数组。
     * 本函数只校验数值形式和重复项，具体枚举值域由后续目标 UEnum 校验。
     *
     * @param Object 待读取的有效 JSON 对象。
     * @param FieldName 必填数组字段名。
     * @param ValuePath 用于错误信息的完整字段路径。
     * @param OutValues 接收保持声明顺序的枚举底层数值。
     * @param OutError 接收首个数组、元素或重复错误。
     * @return 字段是至少含一个不重复整数的数组时返回 true。
     */
    bool ReadRequiredIntegerArray(
        const TSharedPtr<FJsonObject>& Object,
        const FStringView FieldName,
        const FString& ValuePath,
        TArray<int64>& OutValues,
        FString& OutError)
    {
        const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
        if (!Object.IsValid()
            || !Object->TryGetArrayField(FieldName, JsonValues)
            || !JsonValues
            || JsonValues->IsEmpty())
        {
            OutError = FString::Printf(TEXT("%s must be a non-empty array."), *ValuePath);
            return false;
        }

        TSet<int64> SeenValues;
        OutValues.Reset(JsonValues->Num());
        for (int32 Index = 0; Index < JsonValues->Num(); ++Index)
        {
            double NumericValue = 0.0;
            if (!(*JsonValues)[Index].IsValid()
                || !(*JsonValues)[Index]->TryGetNumber(NumericValue)
                || !FMath::IsFinite(NumericValue)
                || FMath::FloorToDouble(NumericValue) != NumericValue
                || NumericValue < static_cast<double>(MIN_int64)
                || NumericValue >= 9223372036854775808.0)
            {
                OutError = FString::Printf(TEXT("%s[%d] must be an integer."), *ValuePath, Index + 1);
                return false;
            }
            const int64 Value = static_cast<int64>(NumericValue);
            if (SeenValues.Contains(Value))
            {
                OutError = FString::Printf(TEXT("%s contains duplicate enum value %lld."), *ValuePath, Value);
                return false;
            }
            SeenValues.Add(Value);
            OutValues.Add(Value);
        }
        return true;
    }

    /**
     * 将 JSON 数组元素严格读取为对象。
     * 本函数不保留数组元素之外的引用，也不修改 JSON 树。
     *
     * @param Value 待读取的有效或空 JSON 值。
     * @param ValuePath 用于错误信息的数组元素路径。
     * @param OutObject 接收有效 JSON 对象。
     * @param OutError 接收类型错误；成功时不修改。
     * @return Value 是有效 JSON 对象时返回 true。
     */
    bool ReadObjectValue(
        const TSharedPtr<FJsonValue>& Value,
        const FString& ValuePath,
        TSharedPtr<FJsonObject>& OutObject,
        FString& OutError)
    {
        const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(ObjectPointer) || !ObjectPointer || !ObjectPointer->IsValid())
        {
            OutError = FString::Printf(TEXT("%s must be an object."), *ValuePath);
            return false;
        }
        OutObject = *ObjectPointer;
        return true;
    }

    /**
     * 沿上下文类的属性链解析最终枚举属性，支持结构体或 UObject 中间层。
     * 解析只依赖 UE 反射，不认识调用方的字段名、结构名或枚举类型。
     *
     * @param ContextClass 属性链的起始 UObject 类，不可为空。
     * @param BindingPath 至少包含一个属性名的链。
     * @param ColumnPath 用于错误信息的列路径。
     * @param OutEnum 接收叶属性关联的 UEnum，不转移所有权。
     * @param OutError 接收缺失属性、不支持的中间层或非枚举叶错误。
     * @return 整条链存在且叶属性是带 UEnum 的 enum/byte 时返回 true。
     */
    bool ResolveEnumBinding(
        UClass* ContextClass,
        const TArray<FName>& BindingPath,
        const FString& ColumnPath,
        const UEnum*& OutEnum,
        FString& OutError)
    {
        OutEnum = nullptr;
        if (!ContextClass || BindingPath.IsEmpty())
        {
            OutError = FString::Printf(TEXT("%s.BindingPath must start from a valid context class."), *ColumnPath);
            return false;
        }

        UStruct* CurrentStruct = ContextClass;
        for (int32 Index = 0; Index < BindingPath.Num(); ++Index)
        {
            FProperty* Property = FindFProperty<FProperty>(CurrentStruct, BindingPath[Index]);
            if (!Property)
            {
                OutError = FString::Printf(
                    TEXT("%s.BindingPath[%d] property '%s' was not found on '%s'."),
                    *ColumnPath,
                    Index + 1,
                    *BindingPath[Index].ToString(),
                    *CurrentStruct->GetPathName());
                return false;
            }

            const bool bLeaf = Index == BindingPath.Num() - 1;
            if (!bLeaf)
            {
                if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
                {
                    CurrentStruct = StructProperty->Struct;
                    continue;
                }
                if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
                {
                    CurrentStruct = ObjectProperty->PropertyClass;
                    continue;
                }

                OutError = FString::Printf(
                    TEXT("%s.BindingPath[%d] property '%s' must expose a struct or UObject for the remaining chain."),
                    *ColumnPath,
                    Index + 1,
                    *BindingPath[Index].ToString());
                return false;
            }

            if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
            {
                OutEnum = EnumProperty->GetEnum();
            }
            else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
            {
                OutEnum = ByteProperty->Enum;
            }

            if (!OutEnum)
            {
                OutError = FString::Printf(
                    TEXT("%s.BindingPath leaf '%s' must be an enum property."),
                    *ColumnPath,
                    *BindingPath[Index].ToString());
                return false;
            }
        }
        return true;
    }

    /**
     * 将一组 Lua 原生枚举底层数值转换为 MultiEnumColumn 使用的位掩码。
     * Chooser 的该列使用 32 位掩码，因此只接受 0 到 31 的实际枚举值。
     *
     * @param Enum 已由绑定叶属性解析出的枚举，不可为空。
     * @param Values 至少包含一个唯一枚举底层数值。
     * @param ValuePath 用于错误信息的条件路径。
     * @param OutMask 接收非零允许值位掩码。
     * @param OutError 接收未知枚举值或超出列表示范围的错误。
     * @return 所有数值均是目标 UEnum 中 0..31 的不同值时返回 true。
     */
    bool BuildEnumMask(
        const UEnum* Enum,
        const TArray<int64>& Values,
        const FString& ValuePath,
        uint32& OutMask,
        FString& OutError)
    {
        OutMask = 0;
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            const int64 EnumValue = Values[Index];
            if (!Enum->IsValidEnumValue(EnumValue))
            {
                OutError = FString::Printf(
                    TEXT("%s[%d] value %lld is not declared by enum '%s'."),
                    *ValuePath,
                    Index + 1,
                    EnumValue,
                    *Enum->GetPathName());
                return false;
            }
            if (EnumValue < 0 || EnumValue > 31)
            {
                OutError = FString::Printf(
                    TEXT("%s[%d] enum value %lld is outside MultiEnumColumn range 0..31."),
                    *ValuePath,
                    Index + 1,
                    EnumValue);
                return false;
            }

            const uint32 Bit = 1u << static_cast<uint32>(EnumValue);
            if ((OutMask & Bit) != 0)
            {
                OutError = FString::Printf(
                    TEXT("%s resolves more than once to enum value %lld."),
                    *ValuePath,
                    EnumValue);
                return false;
            }
            OutMask |= Bit;
        }
        return true;
    }

    /**
     * 将 Canonical JSON 严格解析为通用 ChooserTable 物化规格，并预先解析全部反射类型与资产引用。
     * 支持版本 1、ObjectArray 结果和任意数量的 EnumAny 列；不解释列标识或枚举的业务含义。
     * 本函数必须在编辑器游戏线程调用，因为类与结果资产加载可能触发 UObject 加载。
     *
     * @param CanonicalJson Lua 纯值编译器生成的完整 JSON 对象文本。
     * @param OutSpec 接收已完成列对齐、枚举掩码和对象加载的物化规格。
     * @param OutError 接收首个 JSON、契约、反射或资产错误；成功时为空。
     * @return 全部输入均可在不修改目标资产的前提下解析完成时返回 true。
     */
    bool ParseChooserAssetSpec(
        const FString& CanonicalJson,
        FChooserAssetSpec& OutSpec,
        FString& OutError)
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CanonicalJson);
        TSharedPtr<FJsonObject> RootObject;
        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutError = TEXT("Chooser IR root must be a valid JSON object.");
            return false;
        }

        int32 Version = 0;
        if (!RootObject->TryGetNumberField(TEXT("Version"), Version) || Version != 1)
        {
            OutError = TEXT("$.Version must be supported integer version 1.");
            return false;
        }

        FString AssetType;
        FString ResultMode;
        FString ContextClassPath;
        FString ResultClassPath;
        if (!ReadRequiredString(RootObject, TEXT("AssetType"), TEXT("$.AssetType"), AssetType, OutError)
            || !ReadRequiredString(
                RootObject,
                TEXT("TargetObjectPath"),
                TEXT("$.TargetObjectPath"),
                OutSpec.TargetObjectPath,
                OutError)
            || !ReadRequiredString(
                RootObject,
                TEXT("ContextClassPath"),
                TEXT("$.ContextClassPath"),
                ContextClassPath,
                OutError)
            || !ReadRequiredString(
                RootObject,
                TEXT("ResultClassPath"),
                TEXT("$.ResultClassPath"),
                ResultClassPath,
                OutError)
            || !ReadRequiredString(RootObject, TEXT("ResultMode"), TEXT("$.ResultMode"), ResultMode, OutError))
        {
            return false;
        }
        if (AssetType != TEXT("ChooserTable"))
        {
            OutError = TEXT("$.AssetType must equal 'ChooserTable'.");
            return false;
        }
        if (ResultMode != TEXT("ObjectArray"))
        {
            OutError = TEXT("$.ResultMode must equal 'ObjectArray'.");
            return false;
        }

        FText PathReason;
        if (!FPackageName::IsValidObjectPath(OutSpec.TargetObjectPath, &PathReason))
        {
            OutError = FString::Printf(
                TEXT("$.TargetObjectPath is not a valid object path: %s"),
                *PathReason.ToString());
            return false;
        }
        OutSpec.PackageName = FPackageName::ObjectPathToPackageName(OutSpec.TargetObjectPath);
        OutSpec.AssetName = FPackageName::ObjectPathToObjectName(OutSpec.TargetObjectPath);
        if (!FPackageName::IsValidLongPackageName(OutSpec.PackageName, false, &PathReason))
        {
            OutError = FString::Printf(
                TEXT("$.TargetObjectPath package is not writable or valid: %s"),
                *PathReason.ToString());
            return false;
        }

        OutSpec.ContextClass = LoadObject<UClass>(nullptr, ContextClassPath, {}, LOAD_NoWarn);
        OutSpec.ResultClass = LoadObject<UClass>(nullptr, ResultClassPath, {}, LOAD_NoWarn);
        if (!OutSpec.ContextClass)
        {
            OutError = FString::Printf(TEXT("$.ContextClassPath class could not be loaded: %s"), *ContextClassPath);
            return false;
        }
        if (!OutSpec.ResultClass)
        {
            OutError = FString::Printf(TEXT("$.ResultClassPath class could not be loaded: %s"), *ResultClassPath);
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* ColumnValues = nullptr;
        if (!RootObject->TryGetArrayField(TEXT("Columns"), ColumnValues) || !ColumnValues || ColumnValues->IsEmpty())
        {
            OutError = TEXT("$.Columns must be a non-empty array.");
            return false;
        }

        TMap<FString, int32> ColumnIndices;
        OutSpec.Columns.Reset(ColumnValues->Num());
        for (int32 ColumnIndex = 0; ColumnIndex < ColumnValues->Num(); ++ColumnIndex)
        {
            const FString ColumnPath = FString::Printf(TEXT("$.Columns[%d]"), ColumnIndex + 1);
            TSharedPtr<FJsonObject> ColumnObject;
            if (!ReadObjectValue((*ColumnValues)[ColumnIndex], ColumnPath, ColumnObject, OutError)) return false;

            FChooserColumnSpec& Column = OutSpec.Columns.AddDefaulted_GetRef();
            FString ColumnType;
            if (!ReadRequiredString(ColumnObject, TEXT("Id"), ColumnPath + TEXT(".Id"), Column.Id, OutError)
                || !ReadRequiredString(ColumnObject, TEXT("Type"), ColumnPath + TEXT(".Type"), ColumnType, OutError))
            {
                return false;
            }
            if (ColumnIndices.Contains(Column.Id))
            {
                OutError = FString::Printf(TEXT("%s.Id duplicates column id '%s'."), *ColumnPath, *Column.Id);
                return false;
            }
            if (ColumnType != TEXT("EnumAny"))
            {
                OutError = FString::Printf(TEXT("%s.Type '%s' is not supported; expected 'EnumAny'."), *ColumnPath, *ColumnType);
                return false;
            }

            TArray<FString> BindingNames;
            if (!ReadRequiredStringArray(
                ColumnObject,
                TEXT("BindingPath"),
                ColumnPath + TEXT(".BindingPath"),
                false,
                BindingNames,
                OutError))
            {
                return false;
            }
            Column.BindingPath.Reserve(BindingNames.Num());
            for (const FString& BindingName : BindingNames) Column.BindingPath.Add(FName(*BindingName));
            if (!ResolveEnumBinding(OutSpec.ContextClass, Column.BindingPath, ColumnPath, Column.Enum, OutError)) return false;
            ColumnIndices.Add(Column.Id, ColumnIndex);
        }

        const TArray<TSharedPtr<FJsonValue>>* RowValues = nullptr;
        if (!RootObject->TryGetArrayField(TEXT("Rows"), RowValues) || !RowValues || RowValues->IsEmpty())
        {
            OutError = TEXT("$.Rows must be a non-empty array.");
            return false;
        }

        OutSpec.Rows.Reset(RowValues->Num());
        for (int32 RowIndex = 0; RowIndex < RowValues->Num(); ++RowIndex)
        {
            const FString RowPath = FString::Printf(TEXT("$.Rows[%d]"), RowIndex + 1);
            TSharedPtr<FJsonObject> RowObject;
            if (!ReadObjectValue((*RowValues)[RowIndex], RowPath, RowObject, OutError)) return false;

            FString ResultObjectPath;
            if (!ReadRequiredString(
                RowObject,
                TEXT("ResultObjectPath"),
                RowPath + TEXT(".ResultObjectPath"),
                ResultObjectPath,
                OutError))
            {
                return false;
            }

            FChooserRowSpec& Row = OutSpec.Rows.AddDefaulted_GetRef();
            Row.ResultObject = StaticLoadObject(
                OutSpec.ResultClass,
                nullptr,
                ResultObjectPath,
                {},
                LOAD_NoWarn);
            if (!Row.ResultObject)
            {
                OutError = FString::Printf(
                    TEXT("%s.ResultObjectPath could not load an object of class '%s': %s"),
                    *RowPath,
                    *OutSpec.ResultClass->GetPathName(),
                    *ResultObjectPath);
                return false;
            }
            Row.ColumnMasks.Init(0, OutSpec.Columns.Num());

            const TArray<TSharedPtr<FJsonValue>>* ConditionValues = nullptr;
            if (!RowObject->TryGetArrayField(TEXT("Conditions"), ConditionValues)
                || !ConditionValues
                || ConditionValues->Num() != OutSpec.Columns.Num())
            {
                OutError = FString::Printf(
                    TEXT("%s.Conditions must contain exactly one condition for each of the %d columns."),
                    *RowPath,
                    OutSpec.Columns.Num());
                return false;
            }

            TSet<FString> SeenConditionColumns;
            for (int32 ConditionIndex = 0; ConditionIndex < ConditionValues->Num(); ++ConditionIndex)
            {
                const FString ConditionPath = FString::Printf(
                    TEXT("%s.Conditions[%d]"),
                    *RowPath,
                    ConditionIndex + 1);
                TSharedPtr<FJsonObject> ConditionObject;
                if (!ReadObjectValue(
                    (*ConditionValues)[ConditionIndex],
                    ConditionPath,
                    ConditionObject,
                    OutError))
                {
                    return false;
                }

                FString ColumnId;
                if (!ReadRequiredString(
                    ConditionObject,
                    TEXT("ColumnId"),
                    ConditionPath + TEXT(".ColumnId"),
                    ColumnId,
                    OutError))
                {
                    return false;
                }
                const int32* ResolvedColumnIndex = ColumnIndices.Find(ColumnId);
                if (!ResolvedColumnIndex)
                {
                    OutError = FString::Printf(TEXT("%s.ColumnId references unknown column '%s'."), *ConditionPath, *ColumnId);
                    return false;
                }
                if (SeenConditionColumns.Contains(ColumnId))
                {
                    OutError = FString::Printf(TEXT("%s duplicates condition for column '%s'."), *RowPath, *ColumnId);
                    return false;
                }
                SeenConditionColumns.Add(ColumnId);

                TArray<int64> EnumValues;
                if (!ReadRequiredIntegerArray(
                    ConditionObject,
                    TEXT("Values"),
                    ConditionPath + TEXT(".Values"),
                    EnumValues,
                    OutError)
                    || !BuildEnumMask(
                        OutSpec.Columns[*ResolvedColumnIndex].Enum,
                        EnumValues,
                        ConditionPath + TEXT(".Values"),
                        Row.ColumnMasks[*ResolvedColumnIndex],
                        OutError))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * 仅根据已解析的纯值规格逐字段重建 ChooserTable 的全部受管结构。
     * 不复制其他 ChooserTable 的 FInstancedStruct 或已编译绑定缓存，避免瞬态验证对象的内部状态
     * 泄漏到正式资产。编辑器行和非编辑器运行时行会在同一次重建中保持一致。
     * 必须在编辑器游戏线程调用；调用方负责 Compile、事务、脏标记与保存。
     *
     * @param Spec 已完成反射和结果资产解析的只读规格。
     * @param Target 待全量重建的有效 ChooserTable。
     */
    void PopulateChooserTableFromSpec(const FChooserAssetSpec& Spec, UChooserTable* Target)
    {
        Target->ResultType = EObjectChooserResultType::ObjectResult;
        Target->OutputObjectType = Spec.ResultClass;
        Target->RootChooser = nullptr;
        Target->FallbackResult.Reset();
        Target->ContextData.SetNum(1);
        Target->ContextData[0].InitializeAs<FContextObjectTypeClass>();
        FContextObjectTypeClass& Context = Target->ContextData[0].GetMutable<FContextObjectTypeClass>();
        Context.Class = Spec.ContextClass;
        Context.Direction = EContextObjectDirection::Read;

        Target->ResultsStructs.Reset(Spec.Rows.Num());
        Target->DisabledRows.Init(false, Spec.Rows.Num());
        for (const FChooserRowSpec& Row : Spec.Rows)
        {
            FInstancedStruct& ResultStruct = Target->ResultsStructs.AddDefaulted_GetRef();
            ResultStruct.InitializeAs<FAssetChooser>();
            ResultStruct.GetMutable<FAssetChooser>().Asset = Row.ResultObject;
        }

        Target->ColumnsStructs.Reset(Spec.Columns.Num());
        for (int32 ColumnIndex = 0; ColumnIndex < Spec.Columns.Num(); ++ColumnIndex)
        {
            const FChooserColumnSpec& ColumnSpec = Spec.Columns[ColumnIndex];
            FInstancedStruct& ColumnStruct = Target->ColumnsStructs.AddDefaulted_GetRef();
            ColumnStruct.InitializeAs<FMultiEnumColumn>();
            FMultiEnumColumn& Column = ColumnStruct.GetMutable<FMultiEnumColumn>();
            Column.InputValue.InitializeAs<FEnumContextProperty>();
            FEnumContextProperty& Input = Column.InputValue.GetMutable<FEnumContextProperty>();
            Input.Binding.PropertyBindingChain = ColumnSpec.BindingPath;
            Input.Binding.ContextIndex = 0;
            Input.Binding.IsBoundToRoot = false;
            Input.Binding.Enum = ColumnSpec.Enum;
            Input.Binding.DisplayName = ColumnSpec.Id;
            Column.RowValues.SetNum(Spec.Rows.Num());
            for (int32 RowIndex = 0; RowIndex < Spec.Rows.Num(); ++RowIndex)
            {
                Column.RowValues[RowIndex].Value = Spec.Rows[RowIndex].ColumnMasks[ColumnIndex];
            }
        }

        Target->NestedChoosers.Reset();
        Target->NestedObjects.Reset();
        Target->ParentTable = nullptr;
        Target->Version = UChooserTable::CurrentVersion;
        Target->PopulateCookedData(false);
    }

    /**
     * 根据已解析规格在瞬态包中完整构建 ChooserTable，并执行一次强制绑定编译。
     * 本函数不查询或修改目标资产，不保存包；失败对象由 GC 回收。
     * 必须在编辑器游戏线程调用。
     *
     * @param Spec 已完成反射和结果资产解析的只读规格。
     * @param OutChooserTable 接收瞬态完整表；失败时为空。
     * @param OutError 接收构建或属性绑定编译错误；成功时为空。
     * @return 瞬态表结构完整且全部列绑定可编译时返回 true。
     */
    bool BuildTransientChooserTable(
        const FChooserAssetSpec& Spec,
        UChooserTable*& OutChooserTable,
        FString& OutError)
    {
        OutChooserTable = NewObject<UChooserTable>(GetTransientPackage(), NAME_None, RF_Transient);
        if (!OutChooserTable)
        {
            OutError = TEXT("Could not allocate transient ChooserTable staging object.");
            return false;
        }

        PopulateChooserTableFromSpec(Spec, OutChooserTable);
        OutChooserTable->Compile(true);

        for (int32 ColumnIndex = 0; ColumnIndex < OutChooserTable->ColumnsStructs.Num(); ++ColumnIndex)
        {
            FChooserColumnBase& Column =
                OutChooserTable->ColumnsStructs[ColumnIndex].GetMutable<FChooserColumnBase>();
            FChooserParameterBase* Input = Column.GetInputValue();
            FText CompileMessage;
            if (!Input || Input->HasCompileErrors(CompileMessage))
            {
                OutError = FString::Printf(
                    TEXT("$.Columns[%d] binding failed to compile: %s"),
                    ColumnIndex + 1,
                    Input ? *CompileMessage.ToString() : TEXT("input parameter is unavailable"));
                OutChooserTable = nullptr;
                return false;
            }
        }
        return true;
    }

    /**
     * 校验已物化表与解析规格完全一致，并确认编辑器结果行已同步到运行时结果行。
     * 该检查在正式资产保存前执行，防止“按钮报告成功但运行时表为空”再次静默发生。
     * 只读取内存对象，必须在编辑器游戏线程调用。
     *
     * @param Spec 作为唯一真源的已解析规格。
     * @param ChooserTable 已完成 Compile 的待提交表。
     * @param OutError 接收首个结构或绑定差异；成功时为空。
     * @return 正式表的上下文、结果、列、掩码及运行时结果均与规格一致时返回 true。
     */
    bool ValidateChooserTableAgainstSpec(
        const FChooserAssetSpec& Spec,
        const UChooserTable* ChooserTable,
        FString& OutError)
    {
        if (!ChooserTable
            || ChooserTable->ResultType != EObjectChooserResultType::ObjectResult
            || ChooserTable->OutputObjectType != Spec.ResultClass)
        {
            OutError = TEXT("Committed ChooserTable result type does not match the parsed IR.");
            return false;
        }
        if (ChooserTable->ContextData.Num() != 1)
        {
            OutError = TEXT("Committed ChooserTable must contain exactly one context declaration.");
            return false;
        }
        const FContextObjectTypeClass* Context =
            ChooserTable->ContextData[0].GetPtr<FContextObjectTypeClass>();
        if (!Context
            || Context->Class != Spec.ContextClass
            || Context->Direction != EContextObjectDirection::Read)
        {
            OutError = TEXT("Committed ChooserTable context declaration does not match the parsed IR.");
            return false;
        }
        if (ChooserTable->ResultsStructs.Num() != Spec.Rows.Num()
            || ChooserTable->DisabledRows.Num() != Spec.Rows.Num()
            || ChooserTable->CookedResults.Num() != Spec.Rows.Num())
        {
            OutError = FString::Printf(
                TEXT("Committed ChooserTable row counts are inconsistent: editor=%d, disabled=%d, runtime=%d, expected=%d."),
                ChooserTable->ResultsStructs.Num(),
                ChooserTable->DisabledRows.Num(),
                ChooserTable->CookedResults.Num(),
                Spec.Rows.Num());
            return false;
        }
        for (int32 RowIndex = 0; RowIndex < Spec.Rows.Num(); ++RowIndex)
        {
            const FAssetChooser* EditorResult =
                ChooserTable->ResultsStructs[RowIndex].GetPtr<FAssetChooser>();
            const FAssetChooser* RuntimeResult =
                ChooserTable->CookedResults[RowIndex].GetPtr<FAssetChooser>();
            if (!EditorResult
                || !RuntimeResult
                || EditorResult->Asset != Spec.Rows[RowIndex].ResultObject
                || RuntimeResult->Asset != Spec.Rows[RowIndex].ResultObject
                || ChooserTable->DisabledRows[RowIndex])
            {
                OutError = FString::Printf(
                    TEXT("Committed ChooserTable row %d does not preserve its enabled result object."),
                    RowIndex + 1);
                return false;
            }
        }
        if (ChooserTable->ColumnsStructs.Num() != Spec.Columns.Num())
        {
            OutError = FString::Printf(
                TEXT("Committed ChooserTable has %d columns; expected %d."),
                ChooserTable->ColumnsStructs.Num(),
                Spec.Columns.Num());
            return false;
        }
        for (int32 ColumnIndex = 0; ColumnIndex < Spec.Columns.Num(); ++ColumnIndex)
        {
            const FChooserColumnSpec& ColumnSpec = Spec.Columns[ColumnIndex];
            const FMultiEnumColumn* Column =
                ChooserTable->ColumnsStructs[ColumnIndex].GetPtr<FMultiEnumColumn>();
            const FEnumContextProperty* Input = Column
                ? Column->InputValue.GetPtr<FEnumContextProperty>()
                : nullptr;
            if (!Column
                || !Input
                || Input->Binding.PropertyBindingChain != ColumnSpec.BindingPath
                || Input->Binding.ContextIndex != 0
                || Input->Binding.IsBoundToRoot
                || Input->Binding.Enum != ColumnSpec.Enum
                || Column->RowValues.Num() != Spec.Rows.Num())
            {
                OutError = FString::Printf(
                    TEXT("Committed ChooserTable column %d does not match its parsed binding."),
                    ColumnIndex + 1);
                return false;
            }
            FText CompileMessage;
            if (Input->HasCompileErrors(CompileMessage))
            {
                OutError = FString::Printf(
                    TEXT("Committed ChooserTable column %d binding failed to compile: %s"),
                    ColumnIndex + 1,
                    *CompileMessage.ToString());
                return false;
            }
            for (int32 RowIndex = 0; RowIndex < Spec.Rows.Num(); ++RowIndex)
            {
                if (Column->RowValues[RowIndex].Value
                    != Spec.Rows[RowIndex].ColumnMasks[ColumnIndex])
                {
                    OutError = FString::Printf(
                        TEXT("Committed ChooserTable column %d row %d mask differs from the parsed IR."),
                        ColumnIndex + 1,
                        RowIndex + 1);
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * 捕获本物化器可能覆盖的 ChooserTable 字段，用于提交失败时恢复已有资产的内存状态。
     * 本函数只复制字段，不调用 Modify、Compile 或包保存，必须在游戏线程使用。
     *
     * @param ChooserTable 待捕获的有效现有资产。
     * @return 包含全部受管字段副本的快照。
     */
    FChooserTableSnapshot CaptureChooserTable(const UChooserTable* ChooserTable)
    {
        FChooserTableSnapshot Snapshot;
        Snapshot.ResultType = ChooserTable->ResultType;
        Snapshot.OutputObjectType = ChooserTable->OutputObjectType;
        Snapshot.ContextData = ChooserTable->ContextData;
        Snapshot.RootChooser = ChooserTable->RootChooser;
        Snapshot.FallbackResult = ChooserTable->FallbackResult;
        Snapshot.ResultsStructs = ChooserTable->ResultsStructs;
        Snapshot.DisabledRows = ChooserTable->DisabledRows;
        Snapshot.NestedChoosers = ChooserTable->NestedChoosers;
        Snapshot.NestedObjects = ChooserTable->NestedObjects;
        Snapshot.ParentTable = ChooserTable->ParentTable;
        Snapshot.Version = ChooserTable->Version;
        Snapshot.CookedResults = ChooserTable->CookedResults;
        Snapshot.ColumnsStructs = ChooserTable->ColumnsStructs;
        return Snapshot;
    }

    /**
     * 将已有 ChooserTable 恢复到提交前快照，并重新编译恢复后的属性绑定。
     * 本函数只恢复内存状态；调用方负责恢复包脏状态和取消事务。
     * 必须在编辑器游戏线程调用。
     *
     * @param Snapshot 提交前捕获的完整受管字段副本。
     * @param Target 待恢复的有效现有资产。
     */
    void RestoreChooserTable(const FChooserTableSnapshot& Snapshot, UChooserTable* Target)
    {
        Target->ResultType = Snapshot.ResultType;
        Target->OutputObjectType = Snapshot.OutputObjectType;
        Target->ContextData = Snapshot.ContextData;
        Target->RootChooser = Snapshot.RootChooser;
        Target->FallbackResult = Snapshot.FallbackResult;
        Target->ResultsStructs = Snapshot.ResultsStructs;
        Target->DisabledRows = Snapshot.DisabledRows;
        Target->NestedChoosers = Snapshot.NestedChoosers;
        Target->NestedObjects = Snapshot.NestedObjects;
        Target->ParentTable = Snapshot.ParentTable;
        Target->Version = Snapshot.Version;
        Target->CookedResults = Snapshot.CookedResults;
        Target->ColumnsStructs = Snapshot.ColumnsStructs;
        Target->Compile(true);
        Target->PostEditChange();
    }
#endif

    /**
     * 将 Lua 栈顶错误值复制为 UTF-16 诊断文本，不改变栈内容。
     * 必须在当前 UnLua Env 所在线程调用，State 必须有效且栈顶应为错误值。
     *
     * @param State 当前 Lua 主状态，不可为空。
     * @return 可转换时返回错误字符串副本，否则返回包含实际 Lua 类型的兜底文本。
     */
    FString ReadLuaError(lua_State* State)
    {
        size_t Utf8Length = 0;
        const char* Utf8Value = lua_tolstring(State, -1, &Utf8Length);
        if (!Utf8Value)
        {
            return FString::Printf(
                TEXT("Lua error value has type %s"),
                UTF8_TO_TCHAR(lua_typename(State, lua_type(State, -1))));
        }
        if (Utf8Length > static_cast<size_t>(MAX_int32)) return TEXT("Lua error message exceeds supported length");

        const FUTF8ToTCHAR ConvertedValue(Utf8Value, static_cast<int32>(Utf8Length));
        return FString(ConvertedValue.Length(), ConvertedValue.Get());
    }

    /**
     * 将 Lua 类型码转换为稳定诊断名称，不修改 Lua 栈。
     * 必须在当前 UnLua Env 所在线程调用，State 必须有效。
     *
     * @param State 当前 Lua 主状态，不可为空。
     * @param Type Lua C API 返回的类型码。
     * @return Lua 提供的类型名称副本。
     */
    FString DescribeLuaType(lua_State* State, const int32 Type)
    {
        return UTF8_TO_TCHAR(lua_typename(State, Type));
    }

    /**
     * 将严格 Lua string 复制为 FString，不接受 number 隐式转换。
     * 必须在当前 UnLua Env 所在线程调用；本函数不修改 Lua 栈。
     *
     * @param State 当前 Lua 主状态，不可为空。
     * @param ValueIndex string 值的有效栈索引。
     * @param ValuePath 诊断使用的值路径。
     * @param OutValue 接收 UTF-16 字符串副本。
     * @param OutError 接收长度错误，成功时不修改。
     * @return 字符串长度可由 FString 表示时返回 true。
     */
    bool CopyLuaString(
        lua_State* State,
        const int32 ValueIndex,
        const FString& ValuePath,
        FString& OutValue,
        FString& OutError)
    {
        size_t Utf8Length = 0;
        const char* Utf8Value = lua_tolstring(State, ValueIndex, &Utf8Length);
        if (!Utf8Value || Utf8Length > static_cast<size_t>(MAX_int32))
        {
            OutError = FString::Printf(TEXT("%s exceeds the supported string length."), *ValuePath);
            return false;
        }

        const FUTF8ToTCHAR ConvertedValue(Utf8Value, static_cast<int32>(Utf8Length));
        OutValue = FString(ConvertedValue.Length(), ConvertedValue.Get());
        return true;
    }

    /**
     * 使用引擎 JSON 写入器转义一个字符串，输出无空白的 JSON string token。
     * UE5.8 的 Writer 只允许对象或数组作为完整根值，因此先写单元素数组再移除外层括号。
     * 可在任意线程调用；函数仅操作调用方独占的 FString。
     *
     * @param Value 未转义的 UTF-16 字符串。
     * @param OutJson 接收带双引号的 JSON token。
     * @return 写入器成功关闭时返回 true。
     */
    bool SerializeStringToken(const FString& Value, FString& OutJson)
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
        Writer->WriteArrayStart();
        Writer->WriteValue(Value);
        Writer->WriteArrayEnd();
        if (!Writer->Close()
            || !OutJson.StartsWith(TEXT("["))
            || !OutJson.EndsWith(TEXT("]")))
        {
            OutJson.Reset();
            return false;
        }
        OutJson = OutJson.Mid(1, OutJson.Len() - 2);
        return true;
    }

    /**
     * 使用引擎 JSON 写入器将有限 double 转换为稳定的无空白 JSON number token。
     * UE5.8 的 Writer 只允许对象或数组作为完整根值，因此先写单元素数组再移除外层括号。
     * 可在任意线程调用；调用方必须先保证 Value 有限。
     *
     * @param Value 待编码的有限双精度数值。
     * @param OutJson 接收 JSON number token。
     * @return 写入器成功关闭时返回 true。
     */
    bool SerializeNumberToken(const double Value, FString& OutJson)
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
        Writer->WriteArrayStart();
        Writer->WriteValue(Value);
        Writer->WriteArrayEnd();
        if (!Writer->Close()
            || !OutJson.StartsWith(TEXT("["))
            || !OutJson.EndsWith(TEXT("]")))
        {
            OutJson.Reset();
            return false;
        }
        OutJson = OutJson.Mid(1, OutJson.Len() - 2);
        return true;
    }

    /**
     * 使用引擎 JSON 写入器将 int64 原样转换为稳定的无空白 JSON integer token。
     * UE5.8 的 Writer 只允许对象或数组作为完整根值，因此先写单元素数组再移除外层括号。
     * 可在任意线程调用；函数不会经过 double，避免大整数精度丢失。
     *
     * @param Value 待编码的 64 位有符号整数。
     * @param OutJson 接收 JSON integer token。
     * @return 写入器成功关闭时返回 true。
     */
    bool SerializeIntegerToken(const int64 Value, FString& OutJson)
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
        Writer->WriteArrayStart();
        Writer->WriteValue(Value);
        Writer->WriteArrayEnd();
        if (!Writer->Close()
            || !OutJson.StartsWith(TEXT("["))
            || !OutJson.EndsWith(TEXT("]")))
        {
            OutJson.Reset();
            return false;
        }
        OutJson = OutJson.Mid(1, OutJson.Len() - 2);
        return true;
    }

    bool SerializeValue(
        lua_State* State,
        int32 ValueIndex,
        const int32 Depth,
        const FString& ValuePath,
        TSet<const void*>& ActiveTables,
        FString& OutJson,
        FString& OutError);

    /**
     * 严格判定 table 是连续一基数组还是纯 string-key 对象，并递归生成 Canonical JSON。
     * 空 table 稳定编码为对象 {}；对象键按 FString 升序输出。混合键、稀疏数组、循环引用与过深嵌套均失败。
     * 必须在当前 UnLua Env 所在线程调用；函数返回前恢复进入时的 Lua 栈顶。
     *
     * @param State 当前 Lua 主状态，不可为空。
     * @param TableIndex table 值的有效栈索引。
     * @param Depth 当前值深度，根值为 0。
     * @param ValuePath 诊断使用的当前值路径。
     * @param ActiveTables 当前递归链上的 table 标识集合，用于拒绝循环引用。
     * @param OutJson 接收完整 table JSON，失败时内容未定义。
     * @param OutError 接收首个结构、递归或值类型错误。
     * @return table 可无损表示为受支持的 JSON 数组或对象时返回 true。
     */
    bool SerializeTable(
        lua_State* State,
        const int32 TableIndex,
        const int32 Depth,
        const FString& ValuePath,
        TSet<const void*>& ActiveTables,
        FString& OutJson,
        FString& OutError)
    {
        const int32 InitialTop = lua_gettop(State);
        ON_SCOPE_EXIT { lua_settop(State, InitialTop); };

        const int32 AbsoluteTableIndex = lua_absindex(State, TableIndex);
        const void* TableIdentity = lua_topointer(State, AbsoluteTableIndex);
        if (!TableIdentity)
        {
            OutError = FString::Printf(TEXT("%s has no stable Lua table identity."), *ValuePath);
            return false;
        }
        if (ActiveTables.Contains(TableIdentity))
        {
            OutError = FString::Printf(TEXT("%s contains a circular table reference."), *ValuePath);
            return false;
        }

        ActiveTables.Add(TableIdentity);
        ON_SCOPE_EXIT { ActiveTables.Remove(TableIdentity); };

        bool bHasIntegerKeys = false;
        bool bHasStringKeys = false;
        int32 EntryCount = 0;
        lua_pushnil(State);
        while (lua_next(State, AbsoluteTableIndex) != 0)
        {
            const int32 KeyType = lua_type(State, -2);
            if (KeyType == LUA_TNUMBER && lua_isinteger(State, -2))
            {
                bHasIntegerKeys = true;
            }
            else if (KeyType == LUA_TSTRING)
            {
                bHasStringKeys = true;
            }
            else
            {
                OutError = FString::Printf(
                    TEXT("%s contains unsupported table key type %s; keys must be all strings or contiguous one-based integers."),
                    *ValuePath,
                    *DescribeLuaType(State, KeyType));
                return false;
            }

            if (bHasIntegerKeys && bHasStringKeys)
            {
                OutError = FString::Printf(TEXT("%s mixes array and object keys."), *ValuePath);
                return false;
            }
            if (EntryCount == MAX_int32)
            {
                OutError = FString::Printf(TEXT("%s exceeds the maximum supported table size."), *ValuePath);
                return false;
            }
            ++EntryCount;
            lua_pop(State, 1);
        }

        if (EntryCount == 0)
        {
            OutJson = TEXT("{}");
            return true;
        }

        if (bHasIntegerKeys)
        {
            const lua_Unsigned RawLength = lua_rawlen(State, AbsoluteTableIndex);
            if (RawLength == 0
                || RawLength > static_cast<lua_Unsigned>(MAX_int32)
                || EntryCount != static_cast<int32>(RawLength))
            {
                OutError = FString::Printf(TEXT("%s must be a contiguous one-based array."), *ValuePath);
                return false;
            }

            OutJson = TEXT("[");
            for (int32 Index = 1; Index <= EntryCount; ++Index)
            {
                lua_rawgeti(State, AbsoluteTableIndex, Index);
                if (lua_type(State, -1) == LUA_TNIL)
                {
                    OutError = FString::Printf(TEXT("%s must not contain sparse array indices."), *ValuePath);
                    return false;
                }

                FString ElementJson;
                if (!SerializeValue(
                    State,
                    lua_absindex(State, -1),
                    Depth + 1,
                    FString::Printf(TEXT("%s[%d]"), *ValuePath, Index),
                    ActiveTables,
                    ElementJson,
                    OutError))
                {
                    return false;
                }
                lua_pop(State, 1);

                if (Index > 1) OutJson += TEXT(",");
                OutJson += ElementJson;
            }
            OutJson += TEXT("]");
            return true;
        }

        TSet<FString> SeenKeys;
        TArray<FCanonicalObjectMember> Members;
        Members.Reserve(EntryCount);
        lua_pushnil(State);
        while (lua_next(State, AbsoluteTableIndex) != 0)
        {
            FString Key;
            if (!CopyLuaString(State, -2, ValuePath + TEXT(".<key>"), Key, OutError)) return false;
            if (SeenKeys.Contains(Key))
            {
                OutError = FString::Printf(TEXT("%s contains duplicate normalized object key '%s'."), *ValuePath, *Key);
                return false;
            }
            SeenKeys.Add(Key);

            FCanonicalObjectMember& Member = Members.AddDefaulted_GetRef();
            Member.Key = Key;
            if (!SerializeValue(
                State,
                lua_absindex(State, -1),
                Depth + 1,
                ValuePath + TEXT(".") + Key,
                ActiveTables,
                Member.JsonValue,
                OutError))
            {
                return false;
            }
            lua_pop(State, 1);
        }

        Members.Sort(
            [](const FCanonicalObjectMember& Left, const FCanonicalObjectMember& Right)
            {
                return Left.Key < Right.Key;
            });

        OutJson = TEXT("{");
        for (int32 Index = 0; Index < Members.Num(); ++Index)
        {
            FString KeyJson;
            if (!SerializeStringToken(Members[Index].Key, KeyJson))
            {
                OutError = FString::Printf(TEXT("%s object key could not be encoded as JSON."), *ValuePath);
                return false;
            }
            if (Index > 0) OutJson += TEXT(",");
            OutJson += KeyJson + TEXT(":") + Members[Index].JsonValue;
        }
        OutJson += TEXT("}");
        return true;
    }

    /**
     * 将一个任意受支持 Lua 纯值递归转换为 Canonical JSON，不执行元方法或加载 UObject。
     * nil、boolean、有限 number、string 与受约束 table 可用；function、userdata、thread 及其他类型失败。
     * 必须在当前 UnLua Env 所在线程调用；除 table 子流程外不修改 Lua 栈。
     *
     * @param State 当前 Lua 主状态，不可为空。
     * @param ValueIndex 待编码值的有效栈索引。
     * @param Depth 当前值深度，根值为 0。
     * @param ValuePath 诊断使用的当前值路径。
     * @param ActiveTables 当前递归链上的 table 标识集合。
     * @param OutJson 接收单个完整 JSON value，失败时内容未定义。
     * @param OutError 接收首个类型、数值、层级或 table 结构错误。
     * @return 当前 Lua 值可完整、确定地表示为 JSON 时返回 true。
     */
    bool SerializeValue(
        lua_State* State,
        int32 ValueIndex,
        const int32 Depth,
        const FString& ValuePath,
        TSet<const void*>& ActiveTables,
        FString& OutJson,
        FString& OutError)
    {
        ValueIndex = lua_absindex(State, ValueIndex);
        if (Depth > MaximumValueDepth)
        {
            OutError = FString::Printf(
                TEXT("%s exceeds the maximum supported nesting depth of %d."),
                *ValuePath,
                MaximumValueDepth);
            return false;
        }

        const int32 Type = lua_type(State, ValueIndex);
        switch (Type)
        {
        case LUA_TNIL:
            OutJson = TEXT("null");
            return true;

        case LUA_TBOOLEAN:
            OutJson = lua_toboolean(State, ValueIndex) != 0 ? TEXT("true") : TEXT("false");
            return true;

        case LUA_TNUMBER:
        {
            if (lua_isinteger(State, ValueIndex))
            {
                const int64 Value = static_cast<int64>(lua_tointeger(State, ValueIndex));
                if (!SerializeIntegerToken(Value, OutJson))
                {
                    OutError = FString::Printf(TEXT("%s could not be encoded as a JSON integer."), *ValuePath);
                    return false;
                }
                return true;
            }

            const double Value = static_cast<double>(lua_tonumber(State, ValueIndex));
            if (!FMath::IsFinite(Value))
            {
                OutError = FString::Printf(TEXT("%s must be a finite number."), *ValuePath);
                return false;
            }
            if (!SerializeNumberToken(Value, OutJson))
            {
                OutError = FString::Printf(TEXT("%s could not be encoded as a JSON number."), *ValuePath);
                return false;
            }
            return true;
        }

        case LUA_TSTRING:
        {
            FString Value;
            if (!CopyLuaString(State, ValueIndex, ValuePath, Value, OutError)) return false;
            if (!SerializeStringToken(Value, OutJson))
            {
                OutError = FString::Printf(TEXT("%s could not be encoded as a JSON string."), *ValuePath);
                return false;
            }
            return true;
        }

        case LUA_TTABLE:
            return SerializeTable(State, ValueIndex, Depth, ValuePath, ActiveTables, OutJson, OutError);

        default:
            OutError = FString::Printf(
                TEXT("%s has unsupported Lua type %s; only nil, boolean, finite number, string, and table are allowed."),
                *ValuePath,
                *DescribeLuaType(State, Type));
            return false;
        }
    }
}

/**
 * 在编辑器游戏线程中 require 任意 Lua 模块、调用指定无参函数，并把其单个纯值结果序列化为 Canonical JSON。
 * 插件不认识返回值中的版本、路径或任何业务字段；对象键按字典序输出，数组保持索引顺序，空 table 统一输出为 {}。
 * Lua 被视为不可信输入：读取 module 字段时不触发 __index，拒绝混合/稀疏 table、循环引用、超过 64 层的值树、
 * 非有限数值以及 function、userdata、thread 等非纯值。本函数不加载、创建、修改或保存 UObject。
 *
 * @param LuaModuleName 交给 Lua require 的模块名，不是文件系统路径；不能为空或纯空白。
 * @param CompileFunctionName 模块 table 自有的无参函数名；不能为空或纯空白。
 * @param OutCanonicalJson 接收无多余空白且对象键稳定排序的完整 JSON value；失败时清空。
 * @param OutError 接收首个 require、Lua 调用、结构或值校验错误；成功时为空。
 * @return 模块函数成功返回且完整值树满足纯值约束并完成 JSON 编码时返回 true。
 */
bool ULuaDataIRLibrary::CompileLuaDataIRToCanonicalJson(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    FString& OutCanonicalJson,
    FString& OutError)
{
    using namespace LuaDataIR;

    OutCanonicalJson.Reset();
    OutError.Reset();
    if (LuaModuleName.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Lua module name must not be empty or whitespace.");
        return false;
    }
    if (CompileFunctionName.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Compile function name must not be empty or whitespace.");
        return false;
    }
    if (!IsInGameThread())
    {
        OutError = TEXT("CompileLuaDataIRToCanonicalJson must run on the game thread.");
        return false;
    }

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (!UnLuaModule)
    {
        OutError = TEXT("UnLua module could not be loaded.");
        return false;
    }
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);

    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv();
    if (!Environment)
    {
        OutError = TEXT("UnLua environment is not active.");
        return false;
    }

    lua_State* State = Environment->GetMainState();
    if (!State)
    {
        OutError = TEXT("UnLua main state is unavailable.");
        return false;
    }

    const int32 InitialTop = lua_gettop(State);
    ON_SCOPE_EXIT { lua_settop(State, InitialTop); };

    lua_getglobal(State, "require");
    if (lua_type(State, -1) != LUA_TFUNCTION)
    {
        OutError = TEXT("Lua global require is unavailable.");
        return false;
    }

    const FTCHARToUTF8 ModuleNameUtf8(*LuaModuleName);
    lua_pushlstring(State, ModuleNameUtf8.Get(), ModuleNameUtf8.Length());
    if (lua_pcall(State, 1, 1, 0) != LUA_OK)
    {
        OutError = FString::Printf(TEXT("require('%s') failed: %s"), *LuaModuleName, *ReadLuaError(State));
        return false;
    }
    if (lua_type(State, -1) != LUA_TTABLE)
    {
        OutError = FString::Printf(
            TEXT("Lua module '%s' must return table, got %s."),
            *LuaModuleName,
            *DescribeLuaType(State, lua_type(State, -1)));
        return false;
    }

    const int32 ModuleIndex = lua_absindex(State, -1);
    const FTCHARToUTF8 FunctionNameUtf8(*CompileFunctionName);
    lua_pushlstring(State, FunctionNameUtf8.Get(), FunctionNameUtf8.Length());
    const int32 CompileFunctionType = lua_rawget(State, ModuleIndex);
    if (CompileFunctionType != LUA_TFUNCTION)
    {
        OutError = FString::Printf(
            TEXT("Lua module '%s' must provide function '%s', got %s."),
            *LuaModuleName,
            *CompileFunctionName,
            *DescribeLuaType(State, CompileFunctionType));
        return false;
    }
    if (lua_pcall(State, 0, 1, 0) != LUA_OK)
    {
        OutError = FString::Printf(
            TEXT("%s.%s() failed: %s"),
            *LuaModuleName,
            *CompileFunctionName,
            *ReadLuaError(State));
        return false;
    }

    TSet<const void*> ActiveTables;
    FString CanonicalJson;
    if (!SerializeValue(
        State,
        lua_absindex(State, -1),
        0,
        TEXT("$"),
        ActiveTables,
        CanonicalJson,
        OutError))
    {
        return false;
    }

    OutCanonicalJson = MoveTemp(CanonicalJson);
    return true;
}

/**
 * 调用 Canonical JSON 编译入口并把成功状态改为普通输出参数，使 UE Python 在失败时仍返回错误文本。
 * 本函数继承原入口的游戏线程要求，不增加 UObject 加载、创建、修改或保存副作用。
 *
 * @param LuaModuleName 交给 Lua require 的模块名；不能为空或纯空白。
 * @param CompileFunctionName 模块 table 自有的无参函数名；不能为空或纯空白。
 * @param bSuccess 接收原编译入口的布尔结果；失败时仍会随其他输出参数返回给 Python。
 * @param OutCanonicalJson 成功时接收 Canonical JSON；失败时为空。
 * @param OutError 失败时接收具体错误；成功时为空。
 */
void ULuaDataIRLibrary::CompileLuaDataIRToCanonicalJsonDetailed(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    bool& bSuccess,
    FString& OutCanonicalJson,
    FString& OutError)
{
    bSuccess = CompileLuaDataIRToCanonicalJson(
        LuaModuleName,
        CompileFunctionName,
        OutCanonicalJson,
        OutError);
}

/**
 * 在编辑器游戏线程中调用 Lua 纯值编译入口，并以通用强类型反射协议递归覆盖现有 UObject 属性。
 * 插件只认识 Bool、Integer、Float、String、Name、Text、Enum、Object、SoftObject、Class、SoftClass、
 * Struct 和 Array，不识别目标项目、资产类别、字段名称或结构体语义。全部赋值先在瞬态副本中执行，
 * 正式对象仅在预检完整成功后进入事务；保存或正式写入失败时恢复所有受影响的顶层属性。
 * 目标包存在未保存修改时拒绝覆盖。本函数不创建目标资产，也不编译 Blueprint 或 AnimBlueprint。
 *
 * @param LuaModuleName 交给通用 Lua IR 编译器 require 的模块名；不能为空。
 * @param CompileFunctionName 返回 ObjectPropertyPatch IR 的无参模块函数名；不能为空。
 * @param bSavePackage 是否在属性提交成功后保存现有目标包。
 * @param OutObject 成功时接收被修改的正式对象；失败时为空，不转移所有权。
 * @param OutError 接收首个 Lua、JSON、反射、资产、事务或保存错误；成功时为空。
 * @return 瞬态预检与正式提交均成功，并且按请求完成可选保存时返回 true。
 */
bool ULuaDataIRLibrary::CompileLuaObjectPropertyPatchAsset(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    const bool bSavePackage,
    UObject*& OutObject,
    FString& OutError)
{
    OutObject = nullptr;
    OutError.Reset();
#if WITH_EDITOR
    using namespace LuaDataIR;

    if (!IsInGameThread())
    {
        OutError = TEXT("CompileLuaObjectPropertyPatchAsset must run on the editor game thread.");
        return false;
    }

    FString CanonicalJson;
    if (!CompileLuaDataIRToCanonicalJson(
        LuaModuleName,
        CompileFunctionName,
        CanonicalJson,
        OutError))
    {
        return false;
    }

    FObjectPropertyPatchSpec Spec;
    if (!ParseObjectPropertyPatchSpec(CanonicalJson, Spec, OutError)) return false;

    const FSoftObjectPath TargetPath(Spec.TargetObjectPath);
    UObject* TargetObject = TargetPath.ResolveObject();
    if (!TargetObject) TargetObject = TargetPath.TryLoad();
    if (!TargetObject)
    {
        OutError = FString::Printf(
            TEXT("Target object could not be loaded: %s"),
            *Spec.TargetObjectPath);
        return false;
    }
    if (!TargetObject->IsA(Spec.ExpectedClass))
    {
        OutError = FString::Printf(
            TEXT("Target object '%s' has class '%s'; expected '%s'."),
            *Spec.TargetObjectPath,
            *TargetObject->GetClass()->GetPathName(),
            *Spec.ExpectedClass->GetPathName());
        return false;
    }

    UPackage* Package = TargetObject->GetOutermost();
    if (!Package || Package->IsDirty())
    {
        OutError = FString::Printf(
            TEXT("Target object package is unavailable or has unsaved changes: %s"),
            *Spec.TargetObjectPath);
        return false;
    }

    TStrongObjectPtr<UObject> StagingObject(
        DuplicateObject<UObject>(TargetObject, GetTransientPackage()));
    if (!StagingObject.IsValid())
    {
        OutError = FString::Printf(
            TEXT("Could not create a transient validation copy for %s."),
            *Spec.TargetObjectPath);
        return false;
    }

    TArray<FProperty*> AffectedProperties;
    if (!ApplyReflectedAssignments(
        StagingObject->GetClass(),
        StagingObject.Get(),
        StagingObject.Get(),
        Spec.Assignments,
        TEXT("$.Properties"),
        &AffectedProperties,
        OutError))
    {
        return false;
    }

    // 属性存储保留正式资产中的原引用；显式强引用防止覆盖 Instanced 容器后原子对象在保存期间被回收。
    TArray<TUniquePtr<FReflectedPropertySnapshot>> PropertySnapshots;
    PropertySnapshots.Reserve(AffectedProperties.Num());
    for (FProperty* Property : AffectedProperties)
    {
        PropertySnapshots.Add(MakeUnique<FReflectedPropertySnapshot>(Property, TargetObject));
    }
    TArray<UObject*> ExistingSubobjects;
    GetObjectsWithOuter(
        TargetObject,
        ExistingSubobjects,
        true,
        RF_NoFlags,
        EInternalObjectFlags::Garbage);
    TArray<TStrongObjectPtr<UObject>> PreservedSubobjects;
    PreservedSubobjects.Reserve(ExistingSubobjects.Num());
    for (UObject* ExistingSubobject : ExistingSubobjects)
    {
        if (ExistingSubobject) PreservedSubobjects.Emplace(ExistingSubobject);
    }

    const auto RestoreOriginalProperties = [&PropertySnapshots, TargetObject]()
    {
        for (const TUniquePtr<FReflectedPropertySnapshot>& PropertySnapshot : PropertySnapshots)
        {
            PropertySnapshot->Restore(TargetObject);
        }
    };

    FScopedTransaction Transaction(NSLOCTEXT(
        "LuaDataIRLibrary",
        "CompileLuaObjectPropertyPatchAsset",
        "Compile Lua Object Property Patch Asset"));
    TargetObject->Modify();
    if (!ApplyReflectedAssignments(
        TargetObject->GetClass(),
        TargetObject,
        TargetObject,
        Spec.Assignments,
        TEXT("$.Properties"),
        nullptr,
        OutError))
    {
        RestoreOriginalProperties();
        TargetObject->PostEditChange();
        Package->SetDirtyFlag(false);
        Transaction.Cancel();
        return false;
    }

    TargetObject->PostEditChange();
    Package->MarkPackageDirty();
    if (bSavePackage)
    {
        const FString PackageName = FPackageName::ObjectPathToPackageName(Spec.TargetObjectPath);
        FString Filename;
        const bool bResolvedFilename = FPackageName::TryConvertLongPackageNameToFilename(
            PackageName,
            Filename,
            FPackageName::GetAssetPackageExtension());
        bool bSaved = false;
        if (bResolvedFilename)
        {
            FSavePackageArgs SaveArguments;
            SaveArguments.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArguments.SaveFlags = SAVE_NoError;
            bSaved = UPackage::SavePackage(Package, TargetObject, *Filename, SaveArguments);
        }
        if (!bSaved)
        {
            RestoreOriginalProperties();
            TargetObject->PostEditChange();
            Package->SetDirtyFlag(false);
            Transaction.Cancel();
            OutError = FString::Printf(
                TEXT("Failed to save object property patch package: %s"),
                *Filename);
            return false;
        }
        Package->SetDirtyFlag(false);
    }

    OutObject = TargetObject;
    return true;
#else
    OutError = TEXT("CompileLuaObjectPropertyPatchAsset is only available in editor builds.");
    return false;
#endif
}

/**
 * 调用通用 UObject 属性补丁入口并把成功状态改为普通输出参数，使 UE Python 在失败时仍返回错误文本。
 * 本函数继承原入口的编辑器游戏线程、事务、Dirty Package 拒绝和可选保存约束，不改变资产语义。
 *
 * @param LuaModuleName 交给通用 Lua IR 编译器 require 的模块名；不能为空。
 * @param CompileFunctionName 返回 ObjectPropertyPatch IR 的无参模块函数名；不能为空。
 * @param bSavePackage 是否在属性提交成功后保存目标包。
 * @param bSuccess 接收原资产补丁入口的布尔结果；失败时仍会随其他输出参数返回给 Python。
 * @param OutObject 成功时接收被修改的对象；失败时为空，不转移所有权。
 * @param OutError 失败时接收具体错误；成功时为空。
 */
void ULuaDataIRLibrary::CompileLuaObjectPropertyPatchAssetDetailed(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    const bool bSavePackage,
    bool& bSuccess,
    UObject*& OutObject,
    FString& OutError)
{
    bSuccess = CompileLuaObjectPropertyPatchAsset(
        LuaModuleName,
        CompileFunctionName,
        bSavePackage,
        OutObject,
        OutError);
}

/**
 * 在编辑器游戏线程中调用 Lua 纯值编译入口，严格解析通用 ChooserTable IR，并全量替换目标表的受管结构。
 * 插件只认识版本 1、ObjectArray、EnumAny、反射绑定链和 UObject 结果，不解释项目字段或结果类语义。
 * 所有类、结果对象、枚举值和列绑定先在瞬态表中解析、构建并编译；只有完整成功后才进入正式资产事务。
 * 已有目标包若有未保存修改会被拒绝，已有同名非 Chooser 对象或磁盘包缺少目标对象也不会被覆盖。
 * bSavePackage 为 false 时只创建或修改内存资产并标脏；为 true 时额外保存 .uasset。保存失败会恢复已有表
 * 的受管字段，或移除本次新建对象。本函数不编译 Blueprint、AnimBlueprint，也不生成其他资产。
 *
 * @param LuaModuleName 交给通用 Lua IR 编译器 require 的模块名；不能为空。
 * @param CompileFunctionName 返回通用 ChooserTable IR 的无参模块函数名；不能为空。
 * @param bSavePackage 是否在提交成功后将目标包保存到对应挂载目录。
 * @param OutChooserTable 成功时接收正式目标资产；失败时为空，不转移 UObject 所有权。
 * @param OutError 接收首个 Lua、JSON、反射、资产、绑定、事务或保存错误；成功时为空。
 * @return 瞬态校验和正式提交均成功，并且按请求完成可选保存时返回 true。
 */
bool ULuaDataIRLibrary::CompileLuaChooserTableAsset(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    const bool bSavePackage,
    UChooserTable*& OutChooserTable,
    FString& OutError)
{
    OutChooserTable = nullptr;
    OutError.Reset();
#if WITH_EDITOR
    using namespace LuaDataIR;

    if (!IsInGameThread())
    {
        OutError = TEXT("CompileLuaChooserTableAsset must run on the editor game thread.");
        return false;
    }

    FString CanonicalJson;
    if (!CompileLuaDataIRToCanonicalJson(
        LuaModuleName,
        CompileFunctionName,
        CanonicalJson,
        OutError))
    {
        return false;
    }

    FChooserAssetSpec Spec;
    if (!ParseChooserAssetSpec(CanonicalJson, Spec, OutError)) return false;

    UChooserTable* StagingChooser = nullptr;
    if (!BuildTransientChooserTable(Spec, StagingChooser, OutError)) return false;
    if (!ValidateChooserTableAgainstSpec(Spec, StagingChooser, OutError)) return false;

    const FSoftObjectPath TargetPath(Spec.TargetObjectPath);
    UObject* ExistingObject = TargetPath.ResolveObject();
    const bool bPackageOnDisk = FPackageName::DoesPackageExist(Spec.PackageName);
    if (!ExistingObject && bPackageOnDisk) ExistingObject = TargetPath.TryLoad();
    UChooserTable* TargetChooser = Cast<UChooserTable>(ExistingObject);
    UPackage* ExistingPackage = FindPackage(nullptr, *Spec.PackageName);
    if (ExistingObject && !TargetChooser)
    {
        OutError = FString::Printf(
            TEXT("Target object already exists and is not a ChooserTable: %s"),
            *Spec.TargetObjectPath);
        return false;
    }
    if (!ExistingObject && (bPackageOnDisk || ExistingPackage))
    {
        OutError = FString::Printf(
            TEXT("Target package already exists without the requested ChooserTable; refusing to overwrite package: %s"),
            *Spec.PackageName);
        return false;
    }
    if (TargetChooser && TargetChooser->GetOutermost()->IsDirty())
    {
        OutError = FString::Printf(
            TEXT("Target ChooserTable package has unsaved changes; save or revert it before Lua replacement: %s"),
            *Spec.TargetObjectPath);
        return false;
    }

    const bool bNewAsset = TargetChooser == nullptr;
    UPackage* Package = TargetChooser ? TargetChooser->GetOutermost() : CreatePackage(*Spec.PackageName);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Could not create target package: %s"), *Spec.PackageName);
        return false;
    }
    if (bNewAsset)
    {
        TargetChooser = NewObject<UChooserTable>(
            Package,
            FName(*Spec.AssetName),
            RF_Public | RF_Standalone | RF_Transactional);
        if (!TargetChooser)
        {
            OutError = FString::Printf(TEXT("Could not create target ChooserTable: %s"), *Spec.TargetObjectPath);
            return false;
        }
    }

    FChooserTableSnapshot PreviousState;
    if (!bNewAsset) PreviousState = CaptureChooserTable(TargetChooser);
    FScopedTransaction Transaction(NSLOCTEXT(
        "LuaDataIRLibrary",
        "CompileLuaChooserTableAsset",
        "Compile Lua Chooser Table Asset"));
    TargetChooser->Modify();
    PopulateChooserTableFromSpec(Spec, TargetChooser);
    TargetChooser->Compile(true);
    TargetChooser->PostEditChange();
    TargetChooser->PopulateCookedData(false);
    if (!ValidateChooserTableAgainstSpec(Spec, TargetChooser, OutError))
    {
        if (bNewAsset)
        {
            TargetChooser->ClearFlags(RF_Public | RF_Standalone);
            TargetChooser->Rename(
                nullptr,
                GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional);
            Package->Rename(
                *FString::Printf(TEXT("/Temp/LuaChooserFailed_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                nullptr,
                REN_DontCreateRedirectors | REN_NonTransactional);
        }
        else
        {
            RestoreChooserTable(PreviousState, TargetChooser);
        }
        Package->SetDirtyFlag(false);
        Transaction.Cancel();
        return false;
    }
    Package->MarkPackageDirty();

    if (bSavePackage)
    {
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(
            Spec.PackageName,
            Filename,
            FPackageName::GetAssetPackageExtension())
            || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
        {
            if (bNewAsset)
            {
                TargetChooser->ClearFlags(RF_Public | RF_Standalone);
                TargetChooser->Rename(
                    nullptr,
                    GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional);
                Package->Rename(
                    *FString::Printf(TEXT("/Temp/LuaChooserFailed_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr,
                    REN_DontCreateRedirectors | REN_NonTransactional);
            }
            else
            {
                RestoreChooserTable(PreviousState, TargetChooser);
            }
            Package->SetDirtyFlag(false);
            Transaction.Cancel();
            OutError = FString::Printf(TEXT("Could not resolve or create the target asset directory for %s."), *Spec.PackageName);
            return false;
        }

        FSavePackageArgs SaveArguments;
        SaveArguments.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArguments.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, TargetChooser, *Filename, SaveArguments))
        {
            if (bNewAsset)
            {
                TargetChooser->ClearFlags(RF_Public | RF_Standalone);
                TargetChooser->Rename(
                    nullptr,
                    GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional);
                Package->Rename(
                    *FString::Printf(TEXT("/Temp/LuaChooserFailed_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                    nullptr,
                    REN_DontCreateRedirectors | REN_NonTransactional);
            }
            else
            {
                RestoreChooserTable(PreviousState, TargetChooser);
            }
            Package->SetDirtyFlag(false);
            Transaction.Cancel();
            OutError = FString::Printf(TEXT("Failed to save ChooserTable package: %s"), *Filename);
            return false;
        }
        Package->SetDirtyFlag(false);
    }

    if (bNewAsset) FAssetRegistryModule::AssetCreated(TargetChooser);
    OutChooserTable = TargetChooser;
    return true;
#else
    OutError = TEXT("CompileLuaChooserTableAsset is only available in editor builds.");
    return false;
#endif
}

/**
 * 调用通用 Chooser Table 物化入口并把成功状态改为普通输出参数，使 UE Python 在失败时仍返回错误文本。
 * 本函数继承原入口的编辑器游戏线程、全量替换、事务和可选保存约束，不增加项目领域依赖。
 *
 * @param LuaModuleName 交给通用 Lua IR 编译器 require 的模块名；不能为空。
 * @param CompileFunctionName 返回 ChooserTable IR 的无参模块函数名；不能为空。
 * @param bSavePackage 是否在完整物化成功后保存目标包。
 * @param bSuccess 接收原 Chooser 入口的布尔结果；失败时仍会随其他输出参数返回给 Python。
 * @param OutChooserTable 成功时接收正式 Chooser Table；失败时为空，不转移所有权。
 * @param OutError 失败时接收具体错误；成功时为空。
 */
void ULuaDataIRLibrary::CompileLuaChooserTableAssetDetailed(
    const FString& LuaModuleName,
    const FString& CompileFunctionName,
    const bool bSavePackage,
    bool& bSuccess,
    UChooserTable*& OutChooserTable,
    FString& OutError)
{
    bSuccess = CompileLuaChooserTableAsset(
        LuaModuleName,
        CompileFunctionName,
        bSavePackage,
        OutChooserTable,
        OutError);
}
