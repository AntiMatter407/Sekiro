#include "SekiroLuaAnimInstance.h"

#include "SekiroAnimBlueprintExt.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static const char* LuaAnimUpdateFunctionName = "Update";
    static const char* LuaAnimUpdateLayerFunctionName = "UpdateLayer";
    static const TCHAR* DefaultLuaAnimLayerNameText = TEXT("Default");

    float GetSafeLuaPlayRate(float PlayRate)
    {
        if (!FMath::IsFinite(PlayRate))
        {
            return 1.0f;
        }

        return PlayRate;
    }

    float AdvanceSekiroAnimTime(const FSekiroLuaAnimState* State, float CurrentTime, float PlayRate, float DeltaSeconds)
    {
        if (!State || !State->Sequence) return 0.0f;

        const float PlayLength = State->Sequence->GetPlayLength();
        if (PlayLength <= UE_KINDA_SMALL_NUMBER) return 0.0f;

        const float MoveDelta = DeltaSeconds * PlayRate * State->Sequence->RateScale;
        float NewTime = CurrentTime + MoveDelta;

        if (State->bLoop)
        {
            NewTime = FMath::Fmod(NewTime, PlayLength);
            if (NewTime < 0.0f)
            {
                NewTime += PlayLength;
            }

            return NewTime;
        }

        return FMath::Clamp(NewTime, 0.0f, PlayLength);
    }

    bool ReadLuaNameField(UnLua::FLuaTable& LuaTable, const char* FieldName, FName& OutName)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TSTRING) return false;

        const FString FieldText = FieldValue.Value<FString>();
        OutName = FName(*FieldText);
        return !OutName.IsNone();
    }

    bool ReadLuaFloatField(UnLua::FLuaTable& LuaTable, const char* FieldName, float& OutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TNUMBER) return false;

        OutValue = FieldValue.Value<float>();
        return true;
    }

    bool ReadLuaBoolField(UnLua::FLuaTable& LuaTable, const char* FieldName, bool& bOutValue)
    {
        UnLua::FLuaValue FieldValue = LuaTable[FieldName];
        if (FieldValue.GetType() != LUA_TBOOLEAN) return false;

        bOutValue = FieldValue.Value<bool>();
        return true;
    }

    bool ReadLuaAnimDecision(UnLua::FLuaEnv* LuaEnv, const UnLua::FLuaValue& ReturnValue, FSekiroLuaAnimDecision& OutDecision)
    {
        if (!LuaEnv) return false;

        if (ReturnValue.GetType() == LUA_TSTRING)
        {
            const FString StateNameText = ReturnValue.Value<FString>();
            OutDecision.StateName = FName(*StateNameText);
            return !OutDecision.StateName.IsNone();
        }

        if (ReturnValue.GetType() != LUA_TTABLE) return false;

        UnLua::FLuaTable DecisionTable(LuaEnv, ReturnValue);
        FName StateName = NAME_None;
        if (!ReadLuaNameField(DecisionTable, "StateName", StateName))
        {
            ReadLuaNameField(DecisionTable, "State", StateName);
        }

        if (StateName.IsNone()) return false;

        OutDecision.StateName = StateName;
        ReadLuaFloatField(DecisionTable, "PlayRate", OutDecision.PlayRate);
        if (!ReadLuaBoolField(DecisionTable, "ResetTime", OutDecision.bResetTime))
        {
            ReadLuaBoolField(DecisionTable, "bResetTime", OutDecision.bResetTime);
        }

        return true;
    }

    bool TryCallLuaAnimUpdate(UnLua::FLuaEnv* LuaEnv, UObject* ContextObject, const FString& LuaModuleName, FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision)
    {
        if (!LuaEnv || !ContextObject || LuaModuleName.IsEmpty()) return false;

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return false;

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues RequireReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!RequireReturnValues.IsValid() || RequireReturnValues.Num() == 0)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim module require failed. Module=%s"), *LuaModuleName);
            return false;
        }

        const UnLua::FLuaValue& ModuleValue = RequireReturnValues[0];
        if (ModuleValue.GetType() != LUA_TTABLE)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim module must return a table. Module=%s Type=%s"),
                *LuaModuleName,
                UTF8_TO_TCHAR(lua_typename(LuaState, ModuleValue.GetType())));
            return false;
        }

        UnLua::FLuaTable ModuleTable(LuaEnv, ModuleValue);
        UnLua::FLuaValue LuaFunctionValue = ModuleTable[LuaAnimUpdateLayerFunctionName];
        bool bUseLayerFunction = LuaFunctionValue.GetType() == LUA_TFUNCTION;
        if (!bUseLayerFunction)
        {
            LuaFunctionValue = ModuleTable[LuaAnimUpdateFunctionName];
        }

        if (LuaFunctionValue.GetType() != LUA_TFUNCTION)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim module has no UpdateLayer or Update function. Module=%s"), *LuaModuleName);
            return false;
        }

        UnLua::FLuaFunction LuaFunction(LuaEnv, LuaFunctionValue);
        UnLua::FLuaRetValues FunctionReturnValues = bUseLayerFunction
            ? LuaFunction.Call(ContextObject, LayerName, DeltaSeconds)
            : LuaFunction.Call(ContextObject, DeltaSeconds);
        if (!FunctionReturnValues.IsValid() || FunctionReturnValues.Num() == 0)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim Update did not return a state. Module=%s Layer=%s"),
                *LuaModuleName,
                *LayerName.ToString());
            return false;
        }

        if (!ReadLuaAnimDecision(LuaEnv, FunctionReturnValues[0], OutDecision))
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim Update returned unsupported value. Module=%s Layer=%s Type=%s"),
                *LuaModuleName,
                *LayerName.ToString(),
                UTF8_TO_TCHAR(lua_typename(LuaState, FunctionReturnValues[0].GetType())));
            return false;
        }

        return true;
    }
}

USekiroLuaAnimInstance::USekiroLuaAnimInstance()
{
}

void USekiroLuaAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    LuaAnimSnapshot = FSekiroLuaAnimSnapshot();
    LuaAnimLayerSnapshots.Empty();
}

void USekiroLuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    UpdateLuaDrivenAnimation(DeltaSeconds);
}

bool USekiroLuaAnimInstance::UpdateLuaDrivenAnimation(float DeltaSeconds)
{
    bool bHasAnyDecision = false;
    if (LuaAnimLayers.Num() > 0)
    {
        for (const FSekiroLuaAnimLayerBinding& LayerBinding : LuaAnimLayers)
        {
            if (!LayerBinding.GraphAsset) continue;

            bHasAnyDecision |= UpdateLuaDrivenAnimationLayer(LayerBinding.LayerName, LayerBinding.GraphAsset, DeltaSeconds);
        }
    }
    else
    {
        bHasAnyDecision = UpdateLuaDrivenAnimationLayer(DefaultLuaAnimLayerName, LuaAnimGraphAsset, DeltaSeconds);
    }

    LuaAnimSnapshot = GetLuaAnimLayerSnapshot(DefaultLuaAnimLayerName);
    return bHasAnyDecision;
}

bool USekiroLuaAnimInstance::UpdateLuaDrivenAnimationLayer(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, float DeltaSeconds)
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    if (!GraphAsset)
    {
        FSekiroLuaAnimSnapshot& Snapshot = LuaAnimLayerSnapshots.FindOrAdd(ResolvedLayerName);
        Snapshot = FSekiroLuaAnimSnapshot();
        return false;
    }

    FSekiroLuaAnimDecision Decision;
    const bool bHasDecision = EvaluateLuaAnimDecision(ResolvedLayerName, DeltaSeconds, Decision);
    if (bHasDecision)
    {
        ApplyLuaAnimDecision(ResolvedLayerName, GraphAsset, Decision);
    }

    AdvanceLuaAnimSnapshot(ResolvedLayerName, GraphAsset, DeltaSeconds);
    return bHasDecision;
}

FSekiroLuaAnimSnapshot USekiroLuaAnimInstance::GetLuaAnimSnapshot() const
{
    return LuaAnimSnapshot;
}

FSekiroLuaAnimSnapshot USekiroLuaAnimInstance::GetLuaAnimLayerSnapshot(FName LayerName) const
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    const FSekiroLuaAnimSnapshot* FoundSnapshot = LuaAnimLayerSnapshots.Find(ResolvedLayerName);
    if (FoundSnapshot)
    {
        return *FoundSnapshot;
    }

    return FSekiroLuaAnimSnapshot();
}

const FSekiroLuaAnimSnapshot& USekiroLuaAnimInstance::GetLuaAnimSnapshotRef() const
{
    return LuaAnimSnapshot;
}

USekiroLuaAnimGraphAsset* USekiroLuaAnimInstance::GetLuaAnimGraphAsset() const
{
    return GetLuaAnimLayerGraphAsset(DefaultLuaAnimLayerName);
}

USekiroLuaAnimGraphAsset* USekiroLuaAnimInstance::GetLuaAnimLayerGraphAsset(FName LayerName) const
{
    const FName ResolvedLayerName = ResolveLuaAnimLayerName(LayerName);
    for (const FSekiroLuaAnimLayerBinding& LayerBinding : LuaAnimLayers)
    {
        if (ResolveLuaAnimLayerName(LayerBinding.LayerName) == ResolvedLayerName && LayerBinding.GraphAsset)
        {
            return LayerBinding.GraphAsset;
        }
    }

    if (ResolvedLayerName == ResolveLuaAnimLayerName(DefaultLuaAnimLayerName))
    {
        return LuaAnimGraphAsset;
    }

    return nullptr;
}

FName USekiroLuaAnimInstance::ResolveLuaAnimLayerName(FName LayerName) const
{
    if (!LayerName.IsNone()) return LayerName;
    if (!DefaultLuaAnimLayerName.IsNone()) return DefaultLuaAnimLayerName;

    return FName(DefaultLuaAnimLayerNameText);
}

bool USekiroLuaAnimInstance::EvaluateLuaAnimDecision(FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision)
{
    if (LuaAnimModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Verbose, TEXT("Lua Anim skipped because UnLua env is not available. Module=%s Layer=%s"),
            *LuaAnimModuleName,
            *LayerName.ToString());
        return false;
    }

    return TryCallLuaAnimUpdate(LuaEnv, this, LuaAnimModuleName, LayerName, DeltaSeconds, OutDecision);
}

bool USekiroLuaAnimInstance::ApplyLuaAnimDecision(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, const FSekiroLuaAnimDecision& Decision)
{
    if (!GraphAsset || Decision.StateName.IsNone()) return false;

    const FSekiroLuaAnimState* TargetState = GraphAsset->FindState(Decision.StateName);
    if (!TargetState || !TargetState->Sequence)
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Warning, TEXT("Lua Anim state is not configured. Layer=%s State=%s Asset=%s"),
            *LayerName.ToString(),
            *Decision.StateName.ToString(),
            *GetNameSafe(GraphAsset));
        return false;
    }

    FSekiroLuaAnimSnapshot& Snapshot = LuaAnimLayerSnapshots.FindOrAdd(LayerName);
    const float TargetPlayRate = TargetState->PlayRate * GetSafeLuaPlayRate(Decision.PlayRate);
    const bool bStateChanged = !Snapshot.bHasPose || Snapshot.CurrentStateName != Decision.StateName;

    if (bStateChanged)
    {
        Snapshot.PreviousStateName = Snapshot.CurrentStateName;
        Snapshot.PreviousTime = Snapshot.CurrentTime;
        Snapshot.PreviousPlayRate = Snapshot.CurrentPlayRate;
        Snapshot.bPreviousLoop = Snapshot.bCurrentLoop;

        Snapshot.CurrentStateName = Decision.StateName;
        Snapshot.CurrentTime = 0.0f;
        Snapshot.CurrentPlayRate = TargetPlayRate;
        Snapshot.bCurrentLoop = TargetState->bLoop;
        Snapshot.bHasPose = true;

        Snapshot.BlendTime = Snapshot.PreviousStateName.IsNone() ? 0.0f : FMath::Max(0.0f, TargetState->BlendTime);
        Snapshot.BlendElapsedTime = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : Snapshot.BlendTime;
        Snapshot.BlendAlpha = Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : 1.0f;
        return true;
    }

    Snapshot.CurrentPlayRate = TargetPlayRate;
    Snapshot.bCurrentLoop = TargetState->bLoop;
    if (Decision.bResetTime)
    {
        Snapshot.CurrentTime = 0.0f;
    }

    return true;
}

void USekiroLuaAnimInstance::AdvanceLuaAnimSnapshot(FName LayerName, USekiroLuaAnimGraphAsset* GraphAsset, float DeltaSeconds)
{
    if (!GraphAsset) return;

    FSekiroLuaAnimSnapshot* SnapshotPtr = LuaAnimLayerSnapshots.Find(LayerName);
    if (!SnapshotPtr || !SnapshotPtr->bHasPose) return;

    FSekiroLuaAnimSnapshot& Snapshot = *SnapshotPtr;
    const FSekiroLuaAnimState* CurrentState = GraphAsset->FindState(Snapshot.CurrentStateName);
    Snapshot.CurrentTime = AdvanceSekiroAnimTime(CurrentState, Snapshot.CurrentTime, Snapshot.CurrentPlayRate, DeltaSeconds);

    if (!Snapshot.PreviousStateName.IsNone() && Snapshot.BlendAlpha < 1.0f)
    {
        const FSekiroLuaAnimState* PreviousState = GraphAsset->FindState(Snapshot.PreviousStateName);
        Snapshot.PreviousTime = AdvanceSekiroAnimTime(PreviousState, Snapshot.PreviousTime, Snapshot.PreviousPlayRate, DeltaSeconds);
    }

    if (Snapshot.BlendTime > UE_KINDA_SMALL_NUMBER)
    {
        Snapshot.BlendElapsedTime = FMath::Min(Snapshot.BlendElapsedTime + DeltaSeconds, Snapshot.BlendTime);
        Snapshot.BlendAlpha = FMath::Clamp(Snapshot.BlendElapsedTime / Snapshot.BlendTime, 0.0f, 1.0f);
    }
    else
    {
        Snapshot.BlendElapsedTime = Snapshot.BlendTime;
        Snapshot.BlendAlpha = 1.0f;
    }

    if (Snapshot.BlendAlpha >= 1.0f)
    {
        Snapshot.PreviousStateName = NAME_None;
    }
}
