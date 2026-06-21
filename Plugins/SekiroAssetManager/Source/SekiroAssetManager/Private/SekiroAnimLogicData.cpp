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
	const FSKAttackHitboxList* List = AttackHitboxConfigs.Find(AnimID);
	if (!List) return false;

	for (const FSKAttackHitboxConfig& Cfg : List->Hitboxes)
	{
		if (Frame >= Cfg.StartFrame && Frame <= Cfg.EndFrame)
		{
			OutConfig = Cfg;
			return true;
		}
	}
	return false;
}

bool USKAnimationLogicData::GetFrameFlags(int32 AnimID, int32 Frame,
	FSKFrameFlags& OutFlags) const
{
	const FSKAnimFrameData* FrameData = AnimFrameFlags.Find(AnimID);
	if (!FrameData || FrameData->KeyFrames.Num() == 0)
	{
		OutFlags = FSKFrameFlags();
		return false;
	}

	// 浜屽垎鏌ユ壘锛氭壘鍒?<= Frame 鐨勬渶澶у叧閿抚
	int32 Index = -1;
	for (int32 i = 0; i < FrameData->KeyFrames.Num(); ++i)
	{
		if (FrameData->KeyFrames[i] <= Frame)
		{
			Index = i;
		}
		else
		{
			break;
		}
	}

	if (Index < 0)
	{
		return false;
	}

	OutFlags = FrameData->Flags[Index];
	return true;
}

void USKAnimationLogicData::GetActiveHitboxesAtFrame(int32 AnimID, int32 Frame,
	TArray<FSKAttackHitboxConfig>& OutHitboxes) const
{
	OutHitboxes.Reset();
	const FSKAttackHitboxList* List = AttackHitboxConfigs.Find(AnimID);
	if (!List) return;

	for (const FSKAttackHitboxConfig& Cfg : List->Hitboxes)
	{
		if (Frame >= Cfg.StartFrame && Frame <= Cfg.EndFrame)
		{
			OutHitboxes.Add(Cfg);
		}
	}
}

