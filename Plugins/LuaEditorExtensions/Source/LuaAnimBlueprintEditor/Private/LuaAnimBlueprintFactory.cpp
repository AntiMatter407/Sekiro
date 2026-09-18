#include "LuaAnimBlueprintFactory.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Factories/AnimBlueprintFactory.h"
#include "LuaAnimBlueprintExtension.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LuaAnimBlueprintFactory"

/**
 * 初始化内容浏览器创建入口；实际生成对象仍是标准 UAnimBlueprint，以便命中 UE5.2 原生动画编译器。
 * 构造期间不加载 Skeleton、父类或 Lua 模块。
 */
ULuaAnimBlueprintFactory::ULuaAnimBlueprintFactory()
{
    bCreateNew = true;
    bEditAfterNew = true;
    SupportedClass = UAnimBlueprint::StaticClass();
    ParentClass = UAnimInstance::StaticClass();
}

/**
 * 返回内容浏览器创建菜单显示名；可在编辑器游戏线程读取，无副作用。
 *
 * @return 本插件专用资产入口的本地化名称。
 */
FText ULuaAnimBlueprintFactory::GetDisplayName() const
{
    return LOCTEXT("DisplayName", "Lua Animation Blueprint");
}

/**
 * 返回内容浏览器建议的新资产名，不创建 package 或 UObject。
 *
 * @return 固定的通用名称前缀。
 */
FString ULuaAnimBlueprintFactory::GetDefaultNewAssetName() const
{
    return TEXT("ABP_Lua");
}

/**
 * 委托原生 UAnimBlueprintFactory 创建标准动画蓝图，再附加唯一 Lua 源元数据并关闭多线程动画更新。
 * 必须在游戏线程调用；Skeleton、ParentClass 与模板配置完全沿用父 Factory。
 *
 * @param Class 内容浏览器请求的资产类，必须兼容 UAnimBlueprint。
 * @param InParent 新对象所属 package，不可为空。
 * @param Name 新对象名。
 * @param Flags 新对象标志。
 * @param Context 工厂调用上下文，可为空并原样转交父类。
 * @param Warn 接收原生 Factory 诊断的反馈对象，可为空。
 * @param CallingContext 创建来源标识，原样转交父类。
 * @return 成功时返回标准 UAnimBlueprint；原生创建或扩展注册失败时返回 nullptr。
 */
UObject* ULuaAnimBlueprintFactory::FactoryCreateNew(
    UClass* Class,
    UObject* InParent,
    FName Name,
    EObjectFlags Flags,
    UObject* Context,
    FFeedbackContext* Warn,
    FName CallingContext)
{
    UAnimBlueprintFactory* NativeFactory = NewObject<UAnimBlueprintFactory>(GetTransientPackage());
    NativeFactory->ParentClass = ParentClass;
    NativeFactory->TargetSkeleton = TargetSkeleton;
    NativeFactory->bTemplate = TargetSkeleton == nullptr;
    UFactory* FactoryInterface = NativeFactory;
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FactoryInterface->FactoryCreateNew(
        UAnimBlueprint::StaticClass(),
        InParent,
        Name,
        Flags,
        Context,
        Warn,
        CallingContext));
    if (AnimBlueprint == nullptr) return nullptr;

    ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Request(AnimBlueprint);
    if (Extension == nullptr) return nullptr;

    AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
    Extension->LuaModuleName = LuaModuleName;
    Extension->MarkSourceDirty(
        TEXT("Lua animation blueprint source has not been imported explicitly."));
    return AnimBlueprint;
}

#undef LOCTEXT_NAMESPACE
