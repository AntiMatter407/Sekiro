#include "SekiroLuaAnimGraphAsset.h"

bool USekiroLuaAnimGraphAsset::FindStateCopy(FName StateName, FSekiroLuaAnimState& OutState) const
{
    const FSekiroLuaAnimState* FoundState = FindState(StateName);
    if (FoundState)
    {
        OutState = *FoundState;
        return true;
    }

    OutState = FSekiroLuaAnimState();
    return false;
}

const FSekiroLuaAnimState* USekiroLuaAnimGraphAsset::FindState(FName StateName) const
{
    if (StateName.IsNone()) return nullptr;

    for (const FSekiroLuaAnimState& State : States)
    {
        if (State.StateName == StateName)
        {
            return &State;
        }
    }

    return nullptr;
}
