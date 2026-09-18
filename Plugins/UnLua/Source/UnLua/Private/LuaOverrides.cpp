#include "LuaOverrides.h"
#include "LuaFunction.h"
#include "LuaOverridesClass.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/MetaData.h"

namespace UnLua
{
    FLuaOverrides& FLuaOverrides::Get()
    {
        static FLuaOverrides Override;
        return Override;
    }

    FLuaOverrides::FLuaOverrides()
    {
        GUObjectArray.AddUObjectDeleteListener(this);
    }

    void FLuaOverrides::NotifyUObjectDeleted(const UObjectBase* Object, int32 Index)
    {
        ULuaOverridesClass* OverridesClass;
        if (Overrides.RemoveAndCopyValue((UClass*)Object, OverridesClass))
            OverridesClass->Restore();
    }

    void FLuaOverrides::OnUObjectArrayShutdown()
    {
        GUObjectArray.RemoveUObjectDeleteListener(this);
    }

    void FLuaOverrides::Override(UFunction* Function, UClass* Class, FName NewName)
    {
        const auto OverridesClass = GetOrAddOverridesClass(Class);

        ULuaFunction* LuaFunction;
        if (ULuaFunction* ExistingLuaFunction = Cast<ULuaFunction>(
            Class->FindFunctionByName(NewName, EIncludeSuperFlag::ExcludeSuper)))
        {
            ExistingLuaFunction->Initialize();
            return;
        }

        const bool bDeclaredOnClass = Function->GetOuter() == Class;
#if UE_VERSION_NEWER_THAN(5, 7, 0)
        // UE 5.8 的 BlueprintNativeEvent 包装函数只有找到非 Native Owner 的覆盖函数时才走
        // ProcessEvent。同类覆盖不能继续原地修改 Native UFunction，必须临时替换类函数表项。
        const bool bReplaceExisting = bDeclaredOnClass
            && Function->HasAllFunctionFlags(FUNC_Native | FUNC_BlueprintEvent)
            && Function->GetFName() == NewName;
#else
        const bool bReplaceExisting = false;
#endif
        const bool bAddNew = !bDeclaredOnClass || bReplaceExisting;
        if (bAddNew)
        {
            const auto Exists = Class->FindFunctionByName(NewName, EIncludeSuperFlag::ExcludeSuper);
            if (!bReplaceExisting && Exists && Exists->GetSuperStruct() == Function)
                return;
        }
        else
        {
            LuaFunction = Cast<ULuaFunction>(Function);
            if (LuaFunction)
            {
                LuaFunction->Initialize();
                return;
            }
        }

        FObjectDuplicationParameters DuplicationParams(Function, OverridesClass);
        DuplicationParams.InternalFlagMask &= ~EInternalObjectFlags::Native;
        DuplicationParams.DestName = NewName;
        DuplicationParams.DestClass = ULuaFunction::StaticClass();
        LuaFunction = static_cast<ULuaFunction*>(StaticDuplicateObjectEx(DuplicationParams));

        LuaFunction->Next = OverridesClass->Children;
        OverridesClass->Children = LuaFunction;

        LuaFunction->StaticLink(true);
        LuaFunction->Initialize();
        LuaFunction->Override(Function, Class, bAddNew, bReplaceExisting);
        LuaFunction->Bind();

        if (Class->IsRooted() || GUObjectArray.IsDisregardForGC(Class))
            LuaFunction->AddToRoot();
        else
            LuaFunction->AddToCluster(Class);
    }

    void FLuaOverrides::Restore(UClass* Class)
    {
        const auto Exists = Overrides.Find(Class);
        if (!Exists)
            return;

        const auto OverridesClass = *Exists;
        OverridesClass->Restore();
        Overrides.Remove(Class);
    }

    void FLuaOverrides::RestoreAll()
    {
        TArray<UClass*> Classes;
        Overrides.GenerateKeyArray(Classes);
        for (const auto& Class : Classes)
            Restore(Class);
    }

    void FLuaOverrides::Suspend(UClass* Class)
    {
        if (const auto Exists = Overrides.Find(Class))
            (*Exists)->SetActive(false);
    }

    void FLuaOverrides::Resume(UClass* Class)
    {
        if (const auto Exists = Overrides.Find(Class))
            (*Exists)->SetActive(true);
    }

    UClass* FLuaOverrides::GetOrAddOverridesClass(UClass* Class)
    {
        const auto Exists = Overrides.Find(Class);
        if (Exists)
            return *Exists;

        const auto OverridesClass = ULuaOverridesClass::Create(Class);
        Overrides.Add(Class, OverridesClass);
        return OverridesClass;
    }
}
