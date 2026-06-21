#include "Tools/USKBlueprintTool.h"
#include "SekiroAIBridgeLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

// 材质操作
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "MaterialEditingLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/Texture2D.h"

FString USKBlueprintTool::GetToolDescription() const
{
    return TEXT("Blueprint操作：创建BP资产、添加变量/函数/组件/接口、设置CDO默认值、查询BP结构、编译。");
}

FString USKBlueprintTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"add_variable\",\"add_function\",\"add_component\",\"set_property\",\"get_info\",\"compile\",\"add_interface\",\"add_node\",\"layout\"]},\"path\":{\"type\":\"string\",\"description\":\"Blueprint asset path\"},\"parent_class\":{\"type\":\"string\",\"description\":\"Parent class name\"},\"name\":{\"type\":\"string\",\"description\":\"Variable/function/component name\"},\"type\":{\"type\":\"string\",\"description\":\"Variable type or component class\"},\"value\":{\"type\":\"string\",\"description\":\"Property value\"},\"target\":{\"type\":\"string\",\"description\":\"Target component variable name (for sub-component properties)\"},\"interface_class\":{\"type\":\"string\",\"description\":\"Interface class path\"}},\"required\":[\"action\",\"path\"]}");
}

bool USKBlueprintTool::RequiresConfirmation() const
{
    return true;
}

FString USKBlueprintTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Action = ArgsObj->GetStringField(TEXT("action"));
        FString Path = ArgsObj->GetStringField(TEXT("path"));
        return FString::Printf(TEXT("Blueprint操作: %s -> %s"), *Action, *Path);
    }
    return TEXT("Blueprint操作");
}

FString USKBlueprintTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Action;
    if (!ArgsObj->TryGetStringField(TEXT("action"), Action))
    {
        OutError = TEXT("缺少 action 参数");
        return FString();
    }

    if (Action == TEXT("create"))          return HandleCreate(ArgsObj, OutError);
    if (Action == TEXT("add_variable"))    return HandleAddVariable(ArgsObj, OutError);
    if (Action == TEXT("add_function"))    return HandleAddFunction(ArgsObj, OutError);
    if (Action == TEXT("add_component"))   return HandleAddComponent(ArgsObj, OutError);
    if (Action == TEXT("set_property"))    return HandleSetProperty(ArgsObj, OutError);
    if (Action == TEXT("get_info"))        return HandleGetInfo(ArgsObj, OutError);
    if (Action == TEXT("compile"))         return HandleCompile(ArgsObj, OutError);
    if (Action == TEXT("add_interface"))   return HandleAddInterface(ArgsObj, OutError);
    if (Action == TEXT("add_node"))        return HandleAddNode(ArgsObj, OutError);
    if (Action == TEXT("layout"))          return HandleLayout(ArgsObj, OutError);
    if (Action == TEXT("setup_material"))  return HandleSetupMaterial(ArgsObj, OutError);
    if (Action == TEXT("assign_material_slot")) return HandleAssignMaterialSlot(ArgsObj, OutError);

    OutError = FString::Printf(TEXT("未知操作: %s"), *Action);
    return FString();
}

// ============================================================================
// ResolveClassByName — 按类名查找 UClass
//     约定：传入不带 UE 前缀的 object name（SKWeapon / Actor / Character），
//           或 /Script/Module.Class / /Game/... 全路径。
//     native class 的 object name 不带 A/U/I/F/E/T/S 前缀
//     （C++ 类名 ASKWeapon 对应 object name SKWeapon）。
// ============================================================================

UClass* USKBlueprintTool::ResolveClassByName(const FString& InClassName)
{
    if (InClassName.IsEmpty()) return nullptr;

    // 1) 全路径格式直接 LoadObject
    if (InClassName.Contains(TEXT("/Script/")) || InClassName.Contains(TEXT("/Game/")))
    {
        return LoadObject<UClass>(nullptr, *InClassName);
    }

    // 2) 裸 object name：在候选模块下拼 /Script/Module.Name 尝试加载
    //    LoadObject 会触发 package 加载，比 FindObject 可靠
    const TArray<FString> Modules = {
        TEXT("Sekiro"), TEXT("Engine"), TEXT("CoreUObject"),
        TEXT("SekiroAIBridge"), TEXT("SekiroImport")
    };
    for (const FString& Module : Modules)
    {
        FString FullPath = FString::Printf(TEXT("/Script/%s.%s"), *Module, *InClassName);
        if (UClass* C = LoadObject<UClass>(nullptr, *FullPath))
        {
            return C;
        }
    }

    // 3) FindFirstObject 兜底（按 object name 短名查找）
    return FindFirstObject<UClass>(*InClassName, EFindFirstObjectOptions::None);
}

// ============================================================================
// HandleCreate
// ============================================================================

FString USKBlueprintTool::HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath;
    FString ParentClassName;
    if (!Args->TryGetStringField(TEXT("path"), AssetPath))
    {
        OutError = TEXT("缺少 path 参数");
        return FString();
    }
    Args->TryGetStringField(TEXT("parent_class"), ParentClassName);
    if (ParentClassName.IsEmpty())
    {
        ParentClassName = TEXT("Actor");
    }

    // 查找父类 — 全局搜索，不限定模块
    // 注意: UE native class 的 object name 不带 A/U/I/F 前缀
    //   例如 ASKWeapon 的路径是 /Script/Sekiro.SKWeapon（GetName() 才返回 ASKWeapon）
    //   因此传入带前缀的类名时，需要同时尝试去掉前缀的路径
    UClass* ParentClass = ResolveClassByName(ParentClassName);

    if (!ParentClass)
    {
        // 默认Actor
        ParentClass = AActor::StaticClass();
    }

    // 解析路径
    int32 LastSlash;
    if (!AssetPath.FindLastChar('/', LastSlash))
    {
        OutError = TEXT("无效的资产路径");
        return FString();
    }

    FString PackagePath = AssetPath.Left(LastSlash);
    FString BPName = AssetPath.RightChop(LastSlash + 1);

    // 检查是否已存在
    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        OutError = FString::Printf(TEXT("Blueprint已存在: %s"), *AssetPath);
        return FString();
    }

    // 查找或创建Package
    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = TEXT("创建Package失败");
        return FString();
    }

    // 创建Blueprint
    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        FName(*BPName),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass()
    );

    if (!BP)
    {
        OutError = TEXT("创建Blueprint失败");
        return FString();
    }

    // 标记dirty并保存
    BP->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    // 通知AssetRegistry
    FAssetRegistryModule::AssetCreated(BP);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), BPName);
    ResultObj->SetStringField(TEXT("parent_class"), ParentClass->GetName());
    ResultObj->SetStringField(TEXT("type"), TEXT("Blueprint"));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddVariable
// ============================================================================

FString USKBlueprintTool::HandleAddVariable(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString VarName;
    FString VarType;
    if (!Args->TryGetStringField(TEXT("name"), VarName))
    {
        OutError = TEXT("缺少 name 参数");
        return FString();
    }
    Args->TryGetStringField(TEXT("type"), VarType);
    if (VarType.IsEmpty())
    {
        VarType = TEXT("bool");
    }

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    // 检查重名
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        if (Var.VarName == FName(*VarName))
        {
            OutError = FString::Printf(TEXT("变量已存在: %s"), *VarName);
            return FString();
        }
    }

    // 解析类型
    FEdGraphPinType PinType;
    PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;  // 默认bool

    if (VarType == TEXT("int") || VarType == TEXT("int32"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (VarType == TEXT("float"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (VarType == TEXT("double"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (VarType == TEXT("bool"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (VarType == TEXT("string") || VarType == TEXT("FString") || VarType == TEXT("FName") || VarType == TEXT("FText"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (VarType == TEXT("FVector"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (VarType == TEXT("FRotator"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (VarType == TEXT("FTransform"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
    }
    else
    {
        // 尝试作为Object类型 — 用 ResolveClassByName 统一查找（支持前缀/全路径）
        UClass* ObjClass = ResolveClassByName(VarType);
        if (ObjClass)
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
            PinType.PinSubCategoryObject = ObjClass;
        }
        else
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
            PinType.PinSubCategoryObject = AActor::StaticClass();
        }
    }

    bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
    if (!bSuccess)
    {
        OutError = TEXT("添加变量失败");
        return FString();
    }

    BP->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("variable"), VarName);
    ResultObj->SetStringField(TEXT("type"), VarType);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddFunction
// ============================================================================

FString USKBlueprintTool::HandleAddFunction(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString FuncName;
    if (!Args->TryGetStringField(TEXT("name"), FuncName))
    {
        OutError = TEXT("缺少 name 参数");
        return FString();
    }

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    // 检查重名
    if (FBlueprintEditorUtils::FindOverrideForFunction(BP, BP->GeneratedClass, FName(*FuncName)))
    {
        OutError = FString::Printf(TEXT("函数已存在: %s"), *FuncName);
        return FString();
    }

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        BP,
        FName(*FuncName),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass()
    );

    if (!NewGraph)
    {
        OutError = TEXT("创建函数图失败");
        return FString();
    }

    FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, false, nullptr);

    BP->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("function"), FuncName);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddComponent
// ============================================================================

FString USKBlueprintTool::HandleAddComponent(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString ComponentName;
    FString ComponentType;
    if (!Args->TryGetStringField(TEXT("name"), ComponentName))
    {
        OutError = TEXT("缺少 name 参数");
        return FString();
    }
    Args->TryGetStringField(TEXT("type"), ComponentType);
    if (ComponentType.IsEmpty())
    {
        ComponentType = TEXT("StaticMeshComponent");
    }

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    // 查找组件类 — 用 ResolveClassByName 统一查找（支持前缀/全路径）
    UClass* ComponentClass = ResolveClassByName(ComponentType);
    if (!ComponentClass)
    {
        OutError = FString::Printf(TEXT("组件类未找到: %s"), *ComponentType);
        return FString();
    }

    if (!ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        OutError = FString::Printf(TEXT("%s 不是ActorComponent子类"), *ComponentType);
        return FString();
    }

    // 获取或创建 SimpleConstructionScript
    USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
    if (!SCS)
    {
        SCS = NewObject<USimpleConstructionScript>(BP);
        BP->SimpleConstructionScript = SCS;
    }

    // 创建 SCS 节点
    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, FName(*ComponentName));
    if (!NewNode)
    {
        OutError = TEXT("创建组件节点失败");
        return FString();
    }

    SCS->AddNode(NewNode);

    // SCS->CreateNode + AddNode 已足够，编译时自动创建组件

    BP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(BP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    ResultObj->SetStringField(TEXT("type"), ComponentType);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleSetProperty
// ============================================================================

FString USKBlueprintTool::HandleSetProperty(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString PropName;
    FString Value;
    FString Target;
    if (!Args->TryGetStringField(TEXT("name"), PropName))
    {
        OutError = TEXT("缺少 name 参数（属性名）");
        return FString();
    }
    Args->TryGetStringField(TEXT("value"), Value);
    Args->TryGetStringField(TEXT("target"), Target);

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    if (!BP->GeneratedClass)
    {
        OutError = TEXT("Blueprint缺少GeneratedClass，请先编译");
        return FString();
    }

    // ── 查找目标对象（CDO 或子组件） ──
    UObject* TargetObj = BP->GeneratedClass->GetDefaultObject();
    if (!TargetObj)
    {
        OutError = TEXT("无法获取CDO");
        return FString();
    }

    if (!Target.IsEmpty())
    {
        // 第1层：SCS 节点变量名精确匹配
        UObject* Found = nullptr;
        if (BP->SimpleConstructionScript)
        {
            TArray<USCS_Node*> AllNodes = BP->SimpleConstructionScript->GetAllNodes();
            UE_LOG(LogSekiroAIBridge, Log, TEXT("[HandleSetProperty] Target='%s', SCS节点数=%d"), *Target, AllNodes.Num());

            for (USCS_Node* Node : AllNodes)
            {
                if (!Node) continue;
                FString NodeVarName = Node->GetVariableName().ToString();
                FString NodeCompClassName = Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None");

                UE_LOG(LogSekiroAIBridge, Verbose, TEXT("[HandleSetProperty] SCS节点: VarName='%s', ComponentClass='%s'"),
                    *NodeVarName, *NodeCompClassName);

                // 子条件1：变量名精确匹配
                bool bNameMatch = (NodeVarName == Target);

                // 子条件2：组件类名匹配（不区分大小写）
                bool bClassMatch = false;
                FString TargetLower = Target.ToLower();
                if (!bNameMatch && !TargetLower.Contains(TEXT("/")))
                {
                    bClassMatch = (NodeCompClassName.ToLower() == TargetLower);
                }

                if (bNameMatch || bClassMatch)
                {
                    // 优先使用 GetActualComponentTemplate，它在 UE5.2 中更稳定
                    UActorComponent* CompTemplate = nullptr;
                    if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))
                    {
                        CompTemplate = Node->GetActualComponentTemplate(BPGC);
                    }
                    if (!CompTemplate)
                    {
                        CompTemplate = Node->ComponentTemplate;
                    }
                    if (CompTemplate)
                    {
                        Found = CompTemplate;
                        UE_LOG(LogSekiroAIBridge, Log, TEXT("[HandleSetProperty] SCS匹配成功: VarName='%s', Class='%s', 匹配方式=%s"),
                            *NodeVarName, *NodeCompClassName, bNameMatch ? TEXT("变量名") : TEXT("组件类名"));
                        break;
                    }
                    else
                    {
                        UE_LOG(LogSekiroAIBridge, Warning, TEXT("[HandleSetProperty] SCS节点匹配但模板为null: VarName='%s', Class='%s'"),
                            *NodeVarName, *NodeCompClassName);
                    }
                }
            }
        }
        else
        {
            UE_LOG(LogSekiroAIBridge, Verbose, TEXT("[HandleSetProperty] BP无SimpleConstructionScript"));
        }

        if (Found)
        {
            TargetObj = Found;
        }
        else
        {
            // 第2层：CDO 上查找直接对象属性（属性名 == Target）
            bool bFoundOnCDO = false;
            if (FObjectProperty* CompProp = CastField<FObjectProperty>(BP->GeneratedClass->FindPropertyByName(FName(*Target))))
            {
                UObject* CompObj = CompProp->GetObjectPropertyValue_InContainer(TargetObj);
                if (CompObj)
                {
                    TargetObj = CompObj;
                    bFoundOnCDO = true;
                    UE_LOG(LogSekiroAIBridge, Log, TEXT("[HandleSetProperty] CDO直接属性匹配: Property='%s', Obj=%s"),
                        *Target, *CompObj->GetName());
                }
            }

            // 第3层（回退）：递归查找 CDO 上所有 FObjectProperty，找其值的类名与 Target 匹配（不区分大小写）
            if (!bFoundOnCDO)
            {
                FString TargetLower = Target.ToLower();
                for (TFieldIterator<FObjectProperty> It(BP->GeneratedClass); It; ++It)
                {
                    FObjectProperty* ObjProp = *It;
                    if (!ObjProp) continue;

                    UObject* PropValue = ObjProp->GetObjectPropertyValue_InContainer(TargetObj);
                    if (!PropValue) continue;

                    FString PropValueClassName2 = PropValue->GetClass()->GetName().ToLower();
                    FString ObjPropName = ObjProp->GetName();

                    UE_LOG(LogSekiroAIBridge, Verbose, TEXT("[HandleSetProperty] CDO对象属性遍历: PropName='%s', ValueClass='%s'"),
                        *ObjPropName, *PropValueClassName2);

                    if (PropValueClassName2 == TargetLower)
                    {
                        TargetObj = PropValue;
                        UE_LOG(LogSekiroAIBridge, Log, TEXT("[HandleSetProperty] CDO递归匹配成功: PropName='%s', ValueClass='%s'"),
                            *ObjPropName, *PropValueClassName2);
                        break;
                    }
                }
            }
        }
    }

    FProperty* Property = TargetObj->GetClass()->FindPropertyByName(FName(*PropName));
    if (!Property)
    {
        OutError = FString::Printf(TEXT("属性未找到: %s（在对象 %s 上）"), *PropName, *TargetObj->GetName());
        return FString();
    }

    // 尝试设置值（基本类型转换）
    void* PropertyAddress = Property->ContainerPtrToValuePtr<void>(TargetObj);
    if (!PropertyAddress)
    {
        OutError = FString::Printf(TEXT("属性地址无效: %s（ContainerPtrToValuePtr返回null）"), *PropName);
        return FString();
    }

    if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
    {
        BoolProp->SetPropertyValue(PropertyAddress, Value.ToLower() == TEXT("true") || Value == TEXT("1"));
    }
    else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
    {
        FloatProp->SetFloatingPointPropertyValue(PropertyAddress, FCString::Atof(*Value));
    }
    else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
    {
        DoubleProp->SetFloatingPointPropertyValue(PropertyAddress, FCString::Atod(*Value));
    }
    else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
    {
        IntProp->SetPropertyValue(PropertyAddress, FCString::Atoi(*Value));
    }
    else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
    {
        Int64Prop->SetPropertyValue(PropertyAddress, FCString::Atoi64(*Value));
    }
    else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
    {
        StrProp->SetPropertyValue(PropertyAddress, Value);
    }
    else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
    {
        NameProp->SetPropertyValue(PropertyAddress, FName(*Value));
    }
    else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
    {
        TextProp->SetPropertyValue(PropertyAddress, FText::FromString(Value));
    }
    else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
    {
        if (ByteProp->Enum)
        {
            int64 EnumVal = ByteProp->Enum->GetValueByNameString(Value);
            ByteProp->SetPropertyValue(PropertyAddress, (uint8)EnumVal);
        }
        else
        {
            ByteProp->SetPropertyValue(PropertyAddress, (uint8)FCString::Atoi(*Value));
        }
    }
    else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(Value);
        FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
        UnderlyingProp->SetIntPropertyValue(PropertyAddress, EnumVal);
    }
    else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("FObjectProperty: Setting '%s' with Value='%s', PropertyClass=%s"),
            *PropName, *Value, *GetNameSafe(ObjProp->PropertyClass));

        // TSubclassOf<T> 在 UE5.2 中被 UHT 编译为 FObjectProperty（PropertyClass=UClass::StaticClass()），
        // 而非 FClassProperty。需要特殊处理：加载 Blueprint 后取 GeneratedClass。
        if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf<UClass>())
        {
            // ── TSubclassOf 分支 ──
            UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
            UClass* ResolvedClass = Cast<UClass>(LoadedObj);
            if (!ResolvedClass)
            {
                if (UBlueprint* LoadedBP = Cast<UBlueprint>(LoadedObj))
                {
                    ResolvedClass = LoadedBP->GeneratedClass;
                }
            }
            if (ResolvedClass)
            {
                ObjProp->SetObjectPropertyValue(PropertyAddress, ResolvedClass);
            }
            else
            {
                Property->ImportText_Direct(*Value, PropertyAddress, TargetObj, PPF_None);
            }
        }
        else
        {
            // ── 普通对象引用分支 ──
            UObject* Obj = LoadObject<UObject>(nullptr, *Value);
            if (Obj || Value.IsEmpty())
            {
                ObjProp->SetObjectPropertyValue(PropertyAddress, Obj);
            }
            else
            {
                Property->ImportText_Direct(*Value, PropertyAddress, TargetObj, PPF_None);
            }
        }
    }
    else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("FSoftObjectProperty: Setting '%s' with Value='%s'"), *PropName, *Value);

        FSoftObjectPath SoftPath(Value);
        FSoftObjectPtr SoftObj(SoftPath);
        SoftObjProp->SetPropertyValue(PropertyAddress, SoftObj);
    }
    else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("FSoftClassProperty: Setting '%s' with Value='%s'"), *PropName, *Value);

        // FSoftClassProperty 也需要处理 Blueprint 资产路径 → GeneratedClass
        UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
        UClass* ResolvedClass = Cast<UClass>(LoadedObj);
        if (!ResolvedClass)
        {
            if (UBlueprint* LoadedBP = Cast<UBlueprint>(LoadedObj))
            {
                ResolvedClass = LoadedBP->GeneratedClass;
            }
        }
        if (ResolvedClass)
        {
            SoftClassProp->SetObjectPropertyValue(PropertyAddress, ResolvedClass);
        }
        else
        {
            // 回退到 FSoftObjectPath 直接设
            const FSoftObjectPath SoftPath(Value);
            SoftClassProp->SetPropertyValue(PropertyAddress, FSoftObjectPtr(SoftPath));
        }
    }
    else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("FClassProperty: Setting '%s' with ClassRef='%s'"), *PropName, *Value);

        // 使用 ImportText_Direct 设置 Blueprint 类引用，避免 SetObjectPropertyValue 的 IsA<UClass> 断言
        // TSubclassOf 在 UE5.2 中可能编译为 FObjectProperty 而非 FClassProperty，
        // 即使匹配到 FClassProperty，ContainerPtrToValuePtr 返回的地址可能不兼容
        const FString ClassRef = FString::Printf(TEXT("Class'%s'"), *Value);
        ClassProp->ImportText_Direct(*ClassRef, PropertyAddress, TargetObj, PPF_None);
    }
    else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        // FVector / FRotator / FTransform / FColor / FLinearColor 等常见结构体
        if (StructProp->Struct == TBaseStructure<FVector>::Get()
            || StructProp->Struct == TBaseStructure<FRotator>::Get()
            || StructProp->Struct == TBaseStructure<FTransform>::Get()
            || StructProp->Struct == TBaseStructure<FColor>::Get()
            || StructProp->Struct == TBaseStructure<FLinearColor>::Get()
            || StructProp->Struct == TBaseStructure<FVector2D>::Get()
            || StructProp->Struct == TBaseStructure<FIntPoint>::Get()
            || StructProp->Struct == TBaseStructure<FGuid>::Get())
        {
            Property->ImportText_Direct(*Value, PropertyAddress, TargetObj, PPF_None);
        }
        else
        {
            // 其他结构体：尝试 ImportText
            Property->ImportText_Direct(*Value, PropertyAddress, TargetObj, PPF_None);
        }
    }
    else
    {
        // 通用回退
        Property->ImportText_Direct(*Value, PropertyAddress, TargetObj, PPF_None);
    }

    TargetObj->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("property"), PropName);
    ResultObj->SetStringField(TEXT("value"), Value);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleGetInfo
// ============================================================================

FString USKBlueprintTool::HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    return BlueprintToJson(BP);
}

FString USKBlueprintTool::BlueprintToJson(UBlueprint* BP) const
{
    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("name"), BP->GetName());
    ResultObj->SetStringField(TEXT("path"), BP->GetPathName());
    ResultObj->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));
    ResultObj->SetStringField(TEXT("status"), BP->Status == BS_UpToDate ? TEXT("UpToDate") : (BP->Status == BS_Error ? TEXT("Error") : TEXT("Dirty")));

    // 变量列表
    TArray<TSharedPtr<FJsonValue>> VarsArray;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        VarsArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
    }
    ResultObj->SetArrayField(TEXT("variables"), VarsArray);
    ResultObj->SetNumberField(TEXT("variable_count"), BP->NewVariables.Num());

    // 函数列表
    TArray<TSharedPtr<FJsonValue>> FuncsArray;
    for (const UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (Graph)
        {
            TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject());
            FuncObj->SetStringField(TEXT("name"), Graph->GetName());
            FuncsArray.Add(MakeShareable(new FJsonValueObject(FuncObj)));
        }
    }
    ResultObj->SetArrayField(TEXT("functions"), FuncsArray);
    ResultObj->SetNumberField(TEXT("function_count"), BP->FunctionGraphs.Num());

    // 组件列表
    TArray<TSharedPtr<FJsonValue>> CompsArray;
    if (BP->SimpleConstructionScript)
    {
        for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
        {
            if (Node)
            {
                TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
                CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("Unknown"));
                CompsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
            }
        }
    }
    ResultObj->SetArrayField(TEXT("components"), CompsArray);

    // 接口列表
    TArray<TSharedPtr<FJsonValue>> IfacesArray;
    for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
    {
        TSharedPtr<FJsonObject> IfaceObj = MakeShareable(new FJsonObject());
        IfaceObj->SetStringField(TEXT("class"), Iface.Interface ? Iface.Interface->GetName() : TEXT("Unknown"));
        IfacesArray.Add(MakeShareable(new FJsonValueObject(IfaceObj)));
    }
    ResultObj->SetArrayField(TEXT("interfaces"), IfacesArray);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleCompile
// ============================================================================

FString USKBlueprintTool::HandleCompile(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetBoolField(TEXT("success"), BP->Status != BS_Error);
    ResultObj->SetStringField(TEXT("status"), BP->Status == BS_UpToDate ? TEXT("UpToDate") : (BP->Status == BS_Error ? TEXT("Error") : TEXT("Dirty")));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddInterface
// ============================================================================

FString USKBlueprintTool::HandleAddInterface(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString InterfacePath;
    if (!Args->TryGetStringField(TEXT("interface_class"), InterfacePath))
    {
        OutError = TEXT("缺少 interface_class 参数");
        return FString();
    }

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePath);
    if (!InterfaceClass)
    {
        OutError = FString::Printf(TEXT("接口类未找到: %s"), *InterfacePath);
        return FString();
    }

    bool bSuccess = FBlueprintEditorUtils::ImplementNewInterface(BP, FTopLevelAssetPath(InterfaceClass->GetPathName()));
    if (!bSuccess)
    {
        OutError = FString::Printf(TEXT("添加接口失败: %s"), *InterfacePath);
        return FString();
    }

    BP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(BP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("interface"), InterfaceClass->GetName());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddNode — 向函数图/事件图添加K2节点
// ============================================================================

FString USKBlueprintTool::HandleAddNode(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString NodeType;
    FString GraphName;
    if (!Args->TryGetStringField(TEXT("node_type"), NodeType))
    {
        OutError = TEXT("缺少 node_type 参数");
        return FString();
    }
    Args->TryGetStringField(TEXT("graph_name"), GraphName);
    if (GraphName.IsEmpty())
    {
        GraphName = TEXT("EventGraph");
    }

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    // 查找目标图
    UEdGraph* TargetGraph = nullptr;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (Graph && Graph->GetFName() == FName(*GraphName))
        {
            TargetGraph = Graph;
            break;
        }
    }
    if (!TargetGraph && BP->UbergraphPages.Num() > 0)
    {
        TargetGraph = BP->UbergraphPages[0];
    }
    if (!TargetGraph)
    {
        OutError = FString::Printf(TEXT("图未找到: %s"), *GraphName);
        return FString();
    }

    // 解析节点位置
    int32 PosX = 0, PosY = 0;
    Args->TryGetNumberField(TEXT("x"), PosX);
    Args->TryGetNumberField(TEXT("y"), PosY);

    UEdGraphNode* NewNode = nullptr;

    if (NodeType == TEXT("PrintString"))
    {
        UK2Node_CallFunction* CallFuncNode = NewObject<UK2Node_CallFunction>(TargetGraph);
        UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
        if (PrintFunc)
        {
            CallFuncNode->SetFromFunction(PrintFunc);
            CallFuncNode->AllocateDefaultPins();
            FString InString;
            if (Args->TryGetStringField(TEXT("in_string"), InString))
            {
                CallFuncNode->FindPin(TEXT("InString"))->DefaultValue = InString;
            }
            NewNode = CallFuncNode;
        }
    }
    else if (NodeType == TEXT("Branch"))
    {
        NewNode = NewObject<UK2Node_IfThenElse>(TargetGraph);
        NewNode->AllocateDefaultPins();
    }
    else if (NodeType == TEXT("Sequence"))
    {
        UK2Node_ExecutionSequence* SeqNode = NewObject<UK2Node_ExecutionSequence>(TargetGraph);
        SeqNode->AllocateDefaultPins();
        int32 NumPins = 2;
        Args->TryGetNumberField(TEXT("num_pins"), NumPins);
        // AllocateDefaultPins 默认创建2个Then引脚，需更多时手动添加
        NewNode = SeqNode;
    }
    else if (NodeType == TEXT("CallFunction"))
    {
        FString FuncName;
        if (!Args->TryGetStringField(TEXT("function_name"), FuncName))
        {
            OutError = TEXT("CallFunction 类型需要 function_name 参数");
            return FString();
        }
        // 在蓝图父类中查找函数
        UClass* ParentClass = BP->ParentClass;
        UFunction* TargetFunc = nullptr;
        if (ParentClass)
        {
            TargetFunc = ParentClass->FindFunctionByName(FName(*FuncName));
        }
        if (!TargetFunc)
        {
            OutError = FString::Printf(TEXT("函数未找到: %s"), *FuncName);
            return FString();
        }
        UK2Node_CallFunction* CallFuncNode = NewObject<UK2Node_CallFunction>(TargetGraph);
        CallFuncNode->SetFromFunction(TargetFunc);
        CallFuncNode->AllocateDefaultPins();
        NewNode = CallFuncNode;
    }
    else
    {
        OutError = FString::Printf(TEXT("不支持的 node_type: %s（支持: PrintString, Branch, Sequence, CallFunction）"), *NodeType);
        return FString();
    }

    if (!NewNode)
    {
        OutError = FString::Printf(TEXT("创建节点失败: %s"), *NodeType);
        return FString();
    }

    NewNode->NodePosX = PosX;
    NewNode->NodePosY = PosY;
    TargetGraph->AddNode(NewNode);

    BP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(BP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("graph"), GraphName);
    ResultObj->SetStringField(TEXT("node_type"), NodeType);
    ResultObj->SetStringField(TEXT("node_id"), NewNode->GetFName().ToString());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleLayout — 自动排版 Blueprint 图中节点（Event/CustomEvent在上，其它垂直流）
// ============================================================================

FString USKBlueprintTool::HandleLayout(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString GraphName;
    Args->TryGetStringField(TEXT("graph_name"), GraphName);

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    const UEdGraphSchema* Schema = GetDefault<UEdGraphSchema_K2>();

    auto LayoutGraph = [&](UEdGraph* Graph) {
        if (!Graph) return;

        TArray<UEdGraphNode*> Nodes = Graph->Nodes;
        if (Nodes.Num() == 0) return;

        // 分类：Event/CustomEvent 在上方，其他按类型分
        TArray<UEdGraphNode*> EventNodes;
        TArray<UEdGraphNode*> OtherNodes;
        for (UEdGraphNode* N : Nodes)
        {
            UK2Node_Event* Evt = Cast<UK2Node_Event>(N);
            UK2Node_CustomEvent* CustEvt = Cast<UK2Node_CustomEvent>(N);
            if (Evt || CustEvt)
                EventNodes.Add(N);
            else
                OtherNodes.Add(N);
        }

        // Event 节点排在第一行 (y=0)，水平展开
        for (int32 i = 0; i < EventNodes.Num(); ++i)
            Schema->SetNodePosition(EventNodes[i], FVector2D(i * 400, 0));

        // 其他节点按现有 Y 排序后垂直展开
        OtherNodes.Sort([](UEdGraphNode& A, UEdGraphNode& B) {
            if (A.NodePosY != B.NodePosY) return A.NodePosY < B.NodePosY;
            return A.NodePosX < B.NodePosX;
        });

        int32 StartY = EventNodes.Num() > 0 ? 300 : 0;
        for (int32 i = 0; i < OtherNodes.Num(); ++i)
        {
            // 保持相对 Y 间距，最小 250
            int32 NewY = StartY + i * 250;
            Schema->SetNodePosition(OtherNodes[i], FVector2D(OtherNodes[i]->NodePosX, NewY));
        }
    };

    if (!GraphName.IsEmpty())
    {
        // 排版指定图
        for (UEdGraph* Graph : BP->FunctionGraphs)
        {
            if (Graph && Graph->GetFName() == FName(*GraphName))
            {
                LayoutGraph(Graph);
                break;
            }
        }
        for (UEdGraph* Graph : BP->UbergraphPages)
        {
            if (Graph && Graph->GetFName() == FName(*GraphName))
            {
                LayoutGraph(Graph);
                break;
            }
        }
    }
    else
    {
        // 排版所有图
        for (UEdGraph* Graph : BP->FunctionGraphs)
            LayoutGraph(Graph);
        for (UEdGraph* Graph : BP->UbergraphPages)
            LayoutGraph(Graph);
    }

    BP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(BP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleSetupMaterial — 创建/获取材质并设置纹理参数
// ============================================================================

FString USKBlueprintTool::HandleSetupMaterial(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    UE_LOG(LogSekiroAIBridge, Log, TEXT("HandleSetupMaterial: path=%s"), *AssetPath);

    // 加载或创建材质
    UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *AssetPath);
    UE_LOG(LogSekiroAIBridge, Log, TEXT("  LoadObject -> %s"), Mat ? TEXT("found") : TEXT("null"));
    if (!Mat)
    {
        // 尝试创建 MaterialInstanceConstant
        FString ParentPath;
        if (Args->TryGetStringField(TEXT("parent"), ParentPath) && !ParentPath.IsEmpty())
        {
            UE_LOG(LogSekiroAIBridge, Log, TEXT("  Loading parent: %s"), *ParentPath);
            UMaterialInterface* ParentMat = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
            UE_LOG(LogSekiroAIBridge, Log, TEXT("  ParentMat -> %s"), ParentMat ? *ParentMat->GetName() : TEXT("null"));

            if (ParentMat)
            {
                FString PkgName = FPackageName::GetLongPackagePath(AssetPath);
                FString ObjName = FPackageName::GetShortName(AssetPath);
                UE_LOG(LogSekiroAIBridge, Log, TEXT("  Creating MIC: Pkg=%s Obj=%s"), *PkgName, *ObjName);

                UPackage* Pkg = CreatePackage(*PkgName);
                if (Pkg) Pkg->SetFlags(RF_Public | RF_Standalone);
                UE_LOG(LogSekiroAIBridge, Log, TEXT("  Package -> %s"), Pkg ? TEXT("ok") : TEXT("null"));

                UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Pkg, FName(*ObjName), RF_Public | RF_Standalone);
                UE_LOG(LogSekiroAIBridge, Log, TEXT("  NewObject MIC -> %s"), MIC ? TEXT("ok") : TEXT("null"));

                if (MIC)
                {
                    MIC->SetParentEditorOnly(ParentMat);
                    FTextureParameterValue TexParam;
                    // 不再手动创建表达式节点，用 MIC 的纹理参数系统
                    Mat = MIC;
                    UE_LOG(LogSekiroAIBridge, Log, TEXT("  MIC created and parent set"));
                }
            }
        }
    }

    if (!Mat)
    {
        OutError = FString::Printf(TEXT("无法创建或加载材质: %s"), *AssetPath);
        return FString();
    }

    // 设置纹理参数
    const TSharedPtr<FJsonObject>* TexturesObj = nullptr;
    if (Args->TryGetObjectField(TEXT("textures"), TexturesObj))
    {
        for (const auto& Pair : (*TexturesObj)->Values)
        {
            FString ParamName = Pair.Key;
            FString TexPath = Pair.Value->AsString();

            UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
            if (!Tex) continue;

            // MaterialInstance 直接设参数
            if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Mat))
            {
                UE_LOG(LogSekiroAIBridge, Log, TEXT("  设置纹理: param=%s tex=%s"), *ParamName, *TexPath);
                MIC->SetTextureParameterValueEditorOnly(FName(*ParamName), Tex);
            }
            // 独立 Material — 用材质编辑器 API
            else if (UMaterial* SourceMat = Cast<UMaterial>(Mat))
            {
                UE_LOG(LogSekiroAIBridge, Log, TEXT("  独立Material纹理设置: 参数名=%s, 纹理=%s (暂不支持自动创建节点)"),
                    *ParamName, *TexPath);
            }
        }
    }

    if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Mat))
    {
        MIC->PostEditChange();
    }

    Mat->MarkPackageDirty();
    // SaveAsset 保存 MaterialInstanceConstant
    FString SavePath = AssetPath;
    if (!SavePath.Contains(TEXT(".")))
    {
        SavePath += TEXT(".") + FPackageName::GetShortName(AssetPath);
    }
    UEditorAssetLibrary::SaveAsset(SavePath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("material"), AssetPath);
    ResultObj->SetBoolField(TEXT("success"), true);
    if (Cast<UMaterialInstanceConstant>(Mat))
        ResultObj->SetStringField(TEXT("type"), TEXT("MaterialInstance"));
    else if (Cast<UMaterial>(Mat))
        ResultObj->SetStringField(TEXT("type"), TEXT("Material"));
    else
        ResultObj->SetStringField(TEXT("type"), TEXT("Unknown"));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAssignMaterialSlot — 设置网格体材质槽
// ============================================================================

FString USKBlueprintTool::HandleAssignMaterialSlot(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString MeshPath = Args->GetStringField(TEXT("mesh_path"));
    int32 SlotIndex = (int32)Args->GetNumberField(TEXT("slot_index"));
    FString MaterialPath = Args->GetStringField(TEXT("material_path"));

    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!Mesh)
    {
        OutError = FString::Printf(TEXT("SkeletalMesh未找到: %s"), *MeshPath);
        return FString();
    }

    UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Mat)
    {
        OutError = FString::Printf(TEXT("材质未找到: %s"), *MaterialPath);
        return FString();
    }

    if (SlotIndex < 0 || SlotIndex >= Mesh->GetMaterials().Num())
    {
        OutError = FString::Printf(TEXT("材质槽索引 %d 无效 (共 %d 个槽)"),
            SlotIndex, Mesh->GetMaterials().Num());
        return FString();
    }

    // 设置材质槽 — 直接替换 FSkeletalMaterial 数组中的材质
    Mesh->Modify();
    Mesh->GetMaterials()[SlotIndex].MaterialInterface = Mat;
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(MeshPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("mesh"), MeshPath);
    ResultObj->SetNumberField(TEXT("slot"), SlotIndex);
    ResultObj->SetStringField(TEXT("material"), MaterialPath);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

UBlueprint* USKBlueprintTool::LoadBlueprint(const FString& AssetPath, FString& OutError)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP)
    {
        OutError = FString::Printf(TEXT("Blueprint未找到: %s"), *AssetPath);
    }
    return BP;
}
