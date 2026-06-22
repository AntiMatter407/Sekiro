#include "SekiroCombatData.h"

int32 USKCombatData::GetNextComboAnim(int32 CurrentAnimID) const
{
    const FSKComboEntry* Entry = ComboChain.Find(CurrentAnimID);
    if (!Entry)
        return -1;

    return Entry->NextOnR1;
}

int32 USKCombatData::GetDerivedAnim(int32 CurrentAnimID, FName Action) const
{
    const FSKComboEntry* Entry = ComboChain.Find(CurrentAnimID);
    if (!Entry)
        return -1;

    if (Action == TEXT("R1"))          return Entry->NextOnR1;
    if (Action == TEXT("Charged"))     return Entry->NextOnCharged;
    if (Action == TEXT("Guard"))       return Entry->NextOnGuard;
    if (Action == TEXT("Dodge"))       return Entry->NextOnDodge;
    if (Action == TEXT("Counter"))     return Entry->NextOnCounter;
    if (Action == TEXT("Jump"))        return Entry->NextOnJump;

    return -1;
}
