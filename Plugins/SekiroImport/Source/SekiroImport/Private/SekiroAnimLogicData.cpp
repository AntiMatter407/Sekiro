#include "SekiroAnimLogicData.h"

bool USKAnimationLogicData::CanCancelTo(int32 AnimID, float CurrentTime,
	FName TargetAction, float& OutCrossfade) const
{
	const FSKCancelRuleList* List = CancelRules.Find(AnimID);
	if (!List) return false;

	int32 CurrentFrame = FMath::RoundToInt(CurrentTime * 30.0f);

	for (const FSKCancelRule& Rule : List->Rules)
	{
		if (Rule.IsInWindow(CurrentFrame) && Rule.TargetAction == TargetAction)
		{
			OutCrossfade = Rule.CrossfadeDuration;
			return true;
		}
	}
	return false;
}

bool USKAnimationLogicData::GetAttackHitboxAtFrame(int32 AnimID, int32 Frame,
	FSKAttackHitboxConfig& OutConfig) const
{
	const FSKAttackHitboxConfig* Cfg = AttackHitboxConfigs.Find(AnimID);
	if (!Cfg) return false;

	if (Frame >= Cfg->StartFrame && Frame <= Cfg->EndFrame)
	{
		OutConfig = *Cfg;
		return true;
	}
	return false;
}
