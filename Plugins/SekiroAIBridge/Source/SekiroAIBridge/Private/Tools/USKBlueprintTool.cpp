#include "Tools/USKBlueprintTool.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

FString USKBlueprintTool::GetToolDescription() const
{
    return TEXT("Blueprint操作：创建BP资产、添加变量/函数/组件/接口、设置CDO默认值、查询BP结构、编译。");
}

FString USKBlueprintTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"add_variable\",\"add_function\",\"add_component\",\"set_property\",\"get_info\",\"compile\",\"add_interface\"]},\"path\":{\"type\":\"string\",\"description\":\"Blueprint asset path\"},\"parent_class\":{\"type\":\"string\",\"description\":\"Parent class name\"},\"name\":{\"type\":\"string\",\"description\":\"Variable/function/component name\"},\"type\":{\"type\":\"string\",\"description\":\"Variable type or component class\"},\"value\":{\"type\":\"string\",\"description\":\"Property value\"},\"interface_class\":{\"type\":\"string\",\"description\":\"Interface class path\"}},\"required\":[\"action\",\"path\"]}");
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

    OutError = FString::Printf(TEXT("未知操作: %s"), *Action);
    return FString();
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

    // 查找父类
    UClass* ParentClass = nullptr;
    // 先尝试直接查找
    ParentClass = FindObject<UClass>(nullptr, *ParentClassName);
    if (!ParentClass)
    {
        FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassName);
        ParentClass = FindObject<UClass>(nullptr, *FullPath);
    }
    if (!ParentClass)
    {
        ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
    }
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
        // 尝试作为Object类型
        UClass* ObjClass = FindObject<UClass>(nullptr, *VarType);
        if (!ObjClass)
        {
            FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *VarType);
            ObjClass = FindObject<UClass>(nullptr, *FullPath);
        }
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

    // 查找组件类
    UClass* ComponentClass = FindObject<UClass>(nullptr, *ComponentType);
    if (!ComponentClass)
    {
        FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *ComponentType);
        ComponentClass = FindObject<UClass>(nullptr, *FullPath);
    }
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
    if (!Args->TryGetStringField(TEXT("name"), PropName))
    {
        OutError = TEXT("缺少 name 参数（属性名）");
        return FString();
    }
    Args->TryGetStringField(TEXT("value"), Value);

    UBlueprint* BP = LoadBlueprint(AssetPath, OutError);
    if (!BP) return FString();

    if (!BP->GeneratedClass)
    {
        OutError = TEXT("Blueprint缺少GeneratedClass，请先编译");
        return FString();
    }

    UObject* CDO = BP->GeneratedClass->GetDefaultObject();
    if (!CDO)
    {
        OutError = TEXT("无法获取CDO");
        return FString();
    }

    FProperty* Property = BP->GeneratedClass->FindPropertyByName(FName(*PropName));
    if (!Property)
    {
        OutError = FString::Printf(TEXT("属性未找到: %s"), *PropName);
        return FString();
    }

    // 尝试设置值（基本类型转换）
    void* PropertyAddress = Property->ContainerPtrToValuePtr<void>(CDO);

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
    else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
    {
        StrProp->SetPropertyValue(PropertyAddress, Value);
    }
    else
    {
        // 尝试用FString导入
        Property->ImportText_Direct(*Value, PropertyAddress, CDO, PPF_None);
    }

    CDO->MarkPackageDirty();
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
// Helper
// ============================================================================

UBlueprint* USKBlueprintTool::LoadBlueprint(const FString& AssetPath, FString& OutError)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP)
    {
        OutError = FString::Printf(TEXT("Blueprint未找到: %s"), *AssetPath);
    }
    return BP;
}
