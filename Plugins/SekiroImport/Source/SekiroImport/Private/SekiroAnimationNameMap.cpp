#include "SekiroAnimationNameMap.h"

static TMap<FString, FString> BuildAnimationMap()
{
	TMap<FString, FString> Map;

	// ================================================================
	// Locomotion: Idle, Walk, Jog, Run, Sprint, Turn (000000-009999)
	// ================================================================
	Map.Add(TEXT("000000"), TEXT("Idle_Default"));
	Map.Add(TEXT("000010"), TEXT("Idle_Combat"));
	Map.Add(TEXT("000011"), TEXT("Idle_Combat_L"));
	Map.Add(TEXT("000012"), TEXT("Idle_Combat_R"));
	Map.Add(TEXT("000013"), TEXT("Idle_Combat_Shift"));
	Map.Add(TEXT("000100"), TEXT("Walk_Fwd"));
	Map.Add(TEXT("000101"), TEXT("Walk_Fwd_L"));
	Map.Add(TEXT("000102"), TEXT("Walk_Fwd_R"));
	Map.Add(TEXT("000103"), TEXT("Walk_Fwd_Stop"));
	Map.Add(TEXT("000110"), TEXT("Walk_Bwd"));
	Map.Add(TEXT("000111"), TEXT("Walk_Bwd_L"));
	Map.Add(TEXT("000112"), TEXT("Walk_Bwd_R"));
	Map.Add(TEXT("000113"), TEXT("Walk_Bwd_Stop"));
	Map.Add(TEXT("000120"), TEXT("Walk_L"));
	Map.Add(TEXT("000121"), TEXT("Walk_L_Stop"));
	Map.Add(TEXT("000122"), TEXT("Walk_R"));
	Map.Add(TEXT("000123"), TEXT("Walk_R_Stop"));
	Map.Add(TEXT("000132"), TEXT("Walk_LSlow"));
	Map.Add(TEXT("000133"), TEXT("Walk_RSlow"));
	Map.Add(TEXT("000200"), TEXT("Jog_Fwd"));
	Map.Add(TEXT("000201"), TEXT("Jog_Fwd_L"));
	Map.Add(TEXT("000202"), TEXT("Jog_Fwd_R"));
	Map.Add(TEXT("000203"), TEXT("Jog_Fwd_Stop"));
	Map.Add(TEXT("000300"), TEXT("Sprint_Fwd"));
	Map.Add(TEXT("000301"), TEXT("Sprint_Fwd_L"));
	Map.Add(TEXT("000302"), TEXT("Sprint_Fwd_R"));
	Map.Add(TEXT("000303"), TEXT("Sprint_Fwd_Stop"));
	Map.Add(TEXT("000400"), TEXT("Run_Fast_Fwd"));
	Map.Add(TEXT("000401"), TEXT("Run_Fast_Fwd_L"));
	Map.Add(TEXT("000402"), TEXT("Run_Fast_Fwd_R"));
	Map.Add(TEXT("000403"), TEXT("Run_Fast_Fwd_Stop"));
	Map.Add(TEXT("000420"), TEXT("Run_Fast_L"));
	Map.Add(TEXT("000421"), TEXT("Run_Fast_L_Stop"));
	Map.Add(TEXT("000422"), TEXT("Run_Fast_R"));
	Map.Add(TEXT("000423"), TEXT("Run_Fast_R_Stop"));
	Map.Add(TEXT("000500"), TEXT("Sprint_To_Idle"));
	Map.Add(TEXT("000600"), TEXT("Sprint_To_Idle_Fast"));
	Map.Add(TEXT("001151"), TEXT("Idle_WeaponOut"));
	Map.Add(TEXT("001152"), TEXT("Idle_WeaponOut_L"));
	Map.Add(TEXT("001153"), TEXT("Idle_WeaponOut_R"));
	Map.Add(TEXT("001154"), TEXT("Idle_WeaponOut_Shift"));
	Map.Add(TEXT("001200"), TEXT("Idle_WeaponSheathe"));
	Map.Add(TEXT("005000"), TEXT("Turn_L45"));
	Map.Add(TEXT("005010"), TEXT("Turn_L90"));
	Map.Add(TEXT("005011"), TEXT("Turn_L90_Fast"));
	Map.Add(TEXT("005100"), TEXT("Turn_R45"));
	Map.Add(TEXT("005110"), TEXT("Turn_R90"));
	Map.Add(TEXT("005111"), TEXT("Turn_R90_Fast"));
	Map.Add(TEXT("005200"), TEXT("Turn_L135"));
	Map.Add(TEXT("005300"), TEXT("Turn_R135"));
	Map.Add(TEXT("005400"), TEXT("Turn_L180"));
	Map.Add(TEXT("005410"), TEXT("Turn_L180_Fast"));
	Map.Add(TEXT("005500"), TEXT("Turn_R180"));
	Map.Add(TEXT("005510"), TEXT("Turn_R180_Fast"));

	// ================================================================
	// Movement transitions (020000-049999)
	// ================================================================
	Map.Add(TEXT("020000"), TEXT("Walk_To_Idle"));
	Map.Add(TEXT("020010"), TEXT("Walk_To_Jog"));
	Map.Add(TEXT("021000"), TEXT("Walk_Stop_Turn"));
	Map.Add(TEXT("023000"), TEXT("Run_To_Idle"));
	Map.Add(TEXT("023200"), TEXT("Run_To_Walk"));
	Map.Add(TEXT("023300"), TEXT("Run_To_Jog"));
	Map.Add(TEXT("024200"), TEXT("Sprint_To_Run"));
	Map.Add(TEXT("024300"), TEXT("Sprint_To_Jog"));
	Map.Add(TEXT("030000"), TEXT("StepDodge_Fwd"));
	Map.Add(TEXT("030100"), TEXT("StepDodge_Bwd"));
	Map.Add(TEXT("030110"), TEXT("StepDodge_L"));
	Map.Add(TEXT("030200"), TEXT("StepDodge_R"));
	Map.Add(TEXT("030300"), TEXT("StepDodge_Dash"));
	Map.Add(TEXT("031400"), TEXT("Quickstep_Fwd"));
	Map.Add(TEXT("031410"), TEXT("Quickstep_L"));
	Map.Add(TEXT("031420"), TEXT("Quickstep_R"));
	Map.Add(TEXT("031430"), TEXT("Quickstep_Bwd"));

	// ================================================================
	// Jump (040000-049999)
	// ================================================================
	Map.Add(TEXT("040000"), TEXT("Jump_Neutral"));
	Map.Add(TEXT("040012"), TEXT("Jump_Fwd"));
	Map.Add(TEXT("040100"), TEXT("Jump_Bwd"));
	Map.Add(TEXT("040110"), TEXT("Jump_L"));
	Map.Add(TEXT("040120"), TEXT("Jump_R"));
	Map.Add(TEXT("040200"), TEXT("Jump_Land"));
	Map.Add(TEXT("040210"), TEXT("Jump_Fwd_Land"));
	Map.Add(TEXT("040300"), TEXT("Jump_Fall"));
	Map.Add(TEXT("040310"), TEXT("Jump_Fall_Long"));
	Map.Add(TEXT("040320"), TEXT("Jump_Ledge_Grab"));
	Map.Add(TEXT("041400"), TEXT("Jump_WallKick_Fwd"));
	Map.Add(TEXT("041410"), TEXT("Jump_WallKick_L"));
	Map.Add(TEXT("041420"), TEXT("Jump_WallKick_R"));
	Map.Add(TEXT("041430"), TEXT("Jump_WallKick_Bwd"));

	// ================================================================
	// Attack (100000-199999)
	// ================================================================
	Map.Add(TEXT("100000"), TEXT("Attack_R1_Combo01"));
	Map.Add(TEXT("100001"), TEXT("Attack_R1_Combo02"));
	Map.Add(TEXT("100002"), TEXT("Attack_R1_Combo03"));
	Map.Add(TEXT("100003"), TEXT("Attack_R1_Combo04"));
	Map.Add(TEXT("100100"), TEXT("Attack_R1_Step01"));
	Map.Add(TEXT("100101"), TEXT("Attack_R1_Step02"));
	Map.Add(TEXT("100102"), TEXT("Attack_R1_Step03"));
	Map.Add(TEXT("100200"), TEXT("Attack_R1_Dash01"));
	Map.Add(TEXT("100201"), TEXT("Attack_R1_Dash02"));
	Map.Add(TEXT("100300"), TEXT("Attack_R1_L_Combo01"));
	Map.Add(TEXT("100301"), TEXT("Attack_R1_L_Combo02"));
	Map.Add(TEXT("100310"), TEXT("Attack_R1_L_Step01"));
	Map.Add(TEXT("100311"), TEXT("Attack_R1_L_Step02"));
	Map.Add(TEXT("100320"), TEXT("Attack_R1_L_Dash01"));
	Map.Add(TEXT("100400"), TEXT("Attack_Charged"));
	Map.Add(TEXT("100401"), TEXT("Attack_Charged_Step"));
	Map.Add(TEXT("100410"), TEXT("Attack_Charged_L"));
	Map.Add(TEXT("100420"), TEXT("Attack_Charged_Dash"));
	Map.Add(TEXT("100500"), TEXT("Attack_Thrust"));
	Map.Add(TEXT("100600"), TEXT("Attack_Thrust_Charged"));
	Map.Add(TEXT("100610"), TEXT("Attack_Thrust_Charged_L"));
	Map.Add(TEXT("100800"), TEXT("Attack_GuardBreak"));
	Map.Add(TEXT("101100"), TEXT("Attack_Sprint_R1"));
	Map.Add(TEXT("101300"), TEXT("Attack_Sprint_Thrust"));
	Map.Add(TEXT("102000"), TEXT("Attack_Dodge_Fwd"));
	Map.Add(TEXT("102100"), TEXT("Attack_Dodge_L"));
	Map.Add(TEXT("102110"), TEXT("Attack_Dodge_R"));
	Map.Add(TEXT("102300"), TEXT("Attack_Dodge_Dash"));
	Map.Add(TEXT("102310"), TEXT("Attack_Dodge_Back"));
	Map.Add(TEXT("102390"), TEXT("Attack_Dodge_Charged"));
	Map.Add(TEXT("102500"), TEXT("Attack_Slide_Fwd"));
	Map.Add(TEXT("102510"), TEXT("Attack_Slide_L"));
	Map.Add(TEXT("102520"), TEXT("Attack_Slide_R"));
	Map.Add(TEXT("102900"), TEXT("Attack_Quickstep_Fwd"));
	Map.Add(TEXT("102910"), TEXT("Attack_Quickstep_L"));
	Map.Add(TEXT("102930"), TEXT("Attack_Quickstep_R"));
	Map.Add(TEXT("102940"), TEXT("Attack_Quickstep_Bwd"));
	Map.Add(TEXT("103000"), TEXT("Attack_Jump"));
	Map.Add(TEXT("103100"), TEXT("Attack_Jump_Fwd"));
	Map.Add(TEXT("103300"), TEXT("Attack_Jump_Charged"));
	Map.Add(TEXT("104100"), TEXT("Attack_Crouch"));
	Map.Add(TEXT("104300"), TEXT("Attack_Crouch_Charged"));

	// ================================================================
	// Combat Art (110000-199999)
	// ================================================================
	Map.Add(TEXT("110000"), TEXT("CombatArt_Whirlwind"));
	Map.Add(TEXT("110001"), TEXT("CombatArt_Whirlwind_L"));
	Map.Add(TEXT("110010"), TEXT("CombatArt_Whirlwind_Alt"));
	Map.Add(TEXT("110030"), TEXT("CombatArt_Nightjar"));
	Map.Add(TEXT("110031"), TEXT("CombatArt_Nightjar_Reversal"));
	Map.Add(TEXT("111000"), TEXT("CombatArt_Ichimonji"));
	Map.Add(TEXT("111001"), TEXT("CombatArt_Ichimonji_Double"));
	Map.Add(TEXT("111010"), TEXT("CombatArt_Ichimonji_Alt"));
	Map.Add(TEXT("111011"), TEXT("CombatArt_Ichimonji_Double_Alt"));
	Map.Add(TEXT("111030"), TEXT("CombatArt_Ichimonji_Jump"));
	Map.Add(TEXT("111031"), TEXT("CombatArt_Ichimonji_Double_Jump"));
	Map.Add(TEXT("112020"), TEXT("CombatArt_PrayingStrikes"));
	Map.Add(TEXT("112021"), TEXT("CombatArt_PrayingStrikes_Alt"));
	Map.Add(TEXT("113000"), TEXT("CombatArt_AshinaCross"));
	Map.Add(TEXT("113010"), TEXT("CombatArt_AshinaCross_Alt"));
	Map.Add(TEXT("113030"), TEXT("CombatArt_AshinaCross_Dash"));
	Map.Add(TEXT("114000"), TEXT("CombatArt_Shadowrush"));
	Map.Add(TEXT("114010"), TEXT("CombatArt_Shadowrush_Alt"));
	Map.Add(TEXT("114030"), TEXT("CombatArt_Shadowrush_Dash"));
	Map.Add(TEXT("190000"), TEXT("CombatArt_MortalDraw"));
	Map.Add(TEXT("190001"), TEXT("CombatArt_MortalDraw_Empowered"));
	Map.Add(TEXT("190010"), TEXT("CombatArt_MortalDraw_Jump"));
	Map.Add(TEXT("190011"), TEXT("CombatArt_MortalDraw_Jump_Empowered"));
	Map.Add(TEXT("190030"), TEXT("CombatArt_OneMind"));
	Map.Add(TEXT("191000"), TEXT("CombatArt_SakuraDance"));
	Map.Add(TEXT("191200"), TEXT("CombatArt_Lightning"));
	Map.Add(TEXT("191400"), TEXT("CombatArt_HighMonk"));
	Map.Add(TEXT("191500"), TEXT("CombatArt_HighMonk_Leap"));
	Map.Add(TEXT("192400"), TEXT("CombatArt_SenThrow"));
	Map.Add(TEXT("192500"), TEXT("CombatArt_PhantomKunai"));

	// ================================================================
	// Defense: Guard, Parry, Deflect, Mikiri (200000-219999)
	// ================================================================
	Map.Add(TEXT("200000"), TEXT("Guard_Idle"));
	Map.Add(TEXT("200100"), TEXT("Guard_Raise"));
	Map.Add(TEXT("200120"), TEXT("Guard_Lower"));
	Map.Add(TEXT("200121"), TEXT("Guard_Lower_Fast"));
	Map.Add(TEXT("201000"), TEXT("Parry_Idle"));
	Map.Add(TEXT("201001"), TEXT("Parry_Deflect"));
	Map.Add(TEXT("201010"), TEXT("Parry_Raise"));
	Map.Add(TEXT("201011"), TEXT("Parry_Lower"));
	Map.Add(TEXT("201030"), TEXT("Parry_Deflect_Success"));
	Map.Add(TEXT("201040"), TEXT("Parry_Deflect_L"));
	Map.Add(TEXT("201045"), TEXT("Parry_Deflect_R"));
	Map.Add(TEXT("201050"), TEXT("Parry_Deflect_Fwd"));
	Map.Add(TEXT("201055"), TEXT("Parry_Deflect_Bwd"));
	Map.Add(TEXT("201110"), TEXT("Deflect_Counter_R1"));
	Map.Add(TEXT("201140"), TEXT("Deflect_Counter_Step"));
	Map.Add(TEXT("201141"), TEXT("Deflect_Counter_Step_L"));
	Map.Add(TEXT("201142"), TEXT("Deflect_Counter_Step_R"));
	Map.Add(TEXT("201200"), TEXT("Deflect_Receive_01"));
	Map.Add(TEXT("201210"), TEXT("Deflect_Receive_02"));
	Map.Add(TEXT("201300"), TEXT("Guard_Heavy_Hit"));
	Map.Add(TEXT("201301"), TEXT("Guard_Heavy_Hit_L"));
	Map.Add(TEXT("201302"), TEXT("Guard_Heavy_Hit_R"));
	Map.Add(TEXT("201303"), TEXT("Guard_Heavy_Hit_Fwd"));
	Map.Add(TEXT("201304"), TEXT("Guard_Heavy_Hit_Back"));
	Map.Add(TEXT("201305"), TEXT("Guard_Heavy_Hit_KnockBack"));
	Map.Add(TEXT("201320"), TEXT("Guard_Break_Recover"));
	Map.Add(TEXT("201321"), TEXT("Guard_Break_Recover_Fast"));
	Map.Add(TEXT("201500"), TEXT("Guard_Counter"));
	Map.Add(TEXT("201501"), TEXT("Guard_Counter_Alt"));
	Map.Add(TEXT("201600"), TEXT("Deflect_Jump"));
	Map.Add(TEXT("201610"), TEXT("Deflect_Jump_Receive"));
	Map.Add(TEXT("202000"), TEXT("Parry_Recover"));
	Map.Add(TEXT("202010"), TEXT("Parry_Recover_L"));
	Map.Add(TEXT("202100"), TEXT("Parry_Into_Guard"));
	Map.Add(TEXT("202300"), TEXT("Guard_Into_Parry"));
	Map.Add(TEXT("202400"), TEXT("Guard_Into_Attack"));
	Map.Add(TEXT("202600"), TEXT("Guard_Break"));
	Map.Add(TEXT("202610"), TEXT("Guard_Break_L"));
	Map.Add(TEXT("202700"), TEXT("Guard_Break_Attack"));
	Map.Add(TEXT("202710"), TEXT("Guard_Break_Attack_L"));
	Map.Add(TEXT("205010"), TEXT("Mikiri_Counter"));
	Map.Add(TEXT("205011"), TEXT("Mikiri_Counter_Alt"));
	Map.Add(TEXT("205020"), TEXT("Mikiri_StepIn"));
	Map.Add(TEXT("205030"), TEXT("Mikiri_Stab"));
	Map.Add(TEXT("206100"), TEXT("Mikiri_Receive"));
	Map.Add(TEXT("206101"), TEXT("Mikiri_Receive_Alt"));
	Map.Add(TEXT("206120"), TEXT("Mikiri_Recover"));

	// ================================================================
	// Posture Break, Sweep, Grab (210000-219999)
	// ================================================================
	Map.Add(TEXT("210000"), TEXT("PostureBreak_Receive"));
	Map.Add(TEXT("210001"), TEXT("PostureBreak_Receive_Heavy"));
	Map.Add(TEXT("210010"), TEXT("PostureBreak_Stagger"));
	Map.Add(TEXT("210018"), TEXT("PostureBreak_Fall"));
	Map.Add(TEXT("210050"), TEXT("PostureBreak_Recover"));
	Map.Add(TEXT("210100"), TEXT("PostureBreak_KnockDown"));
	Map.Add(TEXT("210150"), TEXT("PostureBreak_KnockDown_Recover"));
	Map.Add(TEXT("213100"), TEXT("Sweep_JumpKick"));
	Map.Add(TEXT("213110"), TEXT("Sweep_JumpKick_L"));
	Map.Add(TEXT("213301"), TEXT("Sweep_Jump"));
	Map.Add(TEXT("213302"), TEXT("Sweep_Jump_L"));
	Map.Add(TEXT("213303"), TEXT("Sweep_Jump_R"));
	Map.Add(TEXT("213304"), TEXT("Sweep_Jump_Bwd"));
	Map.Add(TEXT("216000"), TEXT("Grab_Receive"));
	Map.Add(TEXT("216010"), TEXT("Grab_Receive_Alt"));
	Map.Add(TEXT("216020"), TEXT("Grab_Escape"));
	Map.Add(TEXT("216100"), TEXT("Grab_Throw_Receive"));

	// ================================================================
	// Deathblow (220000-229999)
	// ================================================================
	Map.Add(TEXT("220000"), TEXT("Deathblow_Front"));
	Map.Add(TEXT("220001"), TEXT("Deathblow_Back"));
	Map.Add(TEXT("220002"), TEXT("Deathblow_Air"));
	Map.Add(TEXT("220003"), TEXT("Deathblow_Plunge"));
	Map.Add(TEXT("220004"), TEXT("Deathblow_Ledge"));
	Map.Add(TEXT("220005"), TEXT("Deathblow_Climb"));
	Map.Add(TEXT("220006"), TEXT("Deathblow_Sneak"));
	Map.Add(TEXT("220010"), TEXT("Deathblow_Front_Receive"));
	Map.Add(TEXT("220011"), TEXT("Deathblow_Back_Receive"));
	Map.Add(TEXT("220012"), TEXT("Deathblow_Air_Receive"));
	Map.Add(TEXT("220020"), TEXT("Deathblow_Front_Alt"));
	Map.Add(TEXT("220021"), TEXT("Deathblow_Back_Alt"));
	Map.Add(TEXT("220022"), TEXT("Deathblow_Air_Alt"));

	// ================================================================
	// Hit, Death, Resurrect (250000-259999)
	// ================================================================
	Map.Add(TEXT("250000"), TEXT("Hit_Light_Front"));
	Map.Add(TEXT("250001"), TEXT("Hit_Light_Back"));
	Map.Add(TEXT("250005"), TEXT("Hit_Light_L"));
	Map.Add(TEXT("250006"), TEXT("Hit_Light_R"));
	Map.Add(TEXT("250010"), TEXT("Hit_Medium_Front"));
	Map.Add(TEXT("250011"), TEXT("Hit_Medium_Back"));
	Map.Add(TEXT("250015"), TEXT("Hit_Medium_L"));
	Map.Add(TEXT("250016"), TEXT("Hit_Medium_R"));
	Map.Add(TEXT("250030"), TEXT("Hit_Heavy_Front"));
	Map.Add(TEXT("250031"), TEXT("Hit_Heavy_Back"));
	Map.Add(TEXT("250035"), TEXT("Hit_Heavy_L"));
	Map.Add(TEXT("250036"), TEXT("Hit_Heavy_R"));
	Map.Add(TEXT("250040"), TEXT("Hit_KnockBack"));
	Map.Add(TEXT("250041"), TEXT("Hit_KnockBack_Heavy"));
	Map.Add(TEXT("250045"), TEXT("Hit_KnockBack_L"));
	Map.Add(TEXT("250046"), TEXT("Hit_KnockBack_R"));
	Map.Add(TEXT("250100"), TEXT("Hit_Stagger"));
	Map.Add(TEXT("250110"), TEXT("Hit_Stagger_Heavy"));
	Map.Add(TEXT("250200"), TEXT("Hit_Ground_Recover"));
	Map.Add(TEXT("250300"), TEXT("Hit_Air_Recover"));
	Map.Add(TEXT("250400"), TEXT("Hit_HeadShot"));
	Map.Add(TEXT("250500"), TEXT("Hit_Poison"));
	Map.Add(TEXT("250600"), TEXT("Hit_Terror"));
	Map.Add(TEXT("250610"), TEXT("Hit_Terror_Death"));
	Map.Add(TEXT("251000"), TEXT("Death_Front"));
	Map.Add(TEXT("251100"), TEXT("Death_Back"));
	Map.Add(TEXT("251200"), TEXT("Death_L"));
	Map.Add(TEXT("251300"), TEXT("Death_R"));
	Map.Add(TEXT("251400"), TEXT("Death_Fall"));
	Map.Add(TEXT("251410"), TEXT("Death_Fall_Long"));
	Map.Add(TEXT("251500"), TEXT("Death_Special"));
	Map.Add(TEXT("251530"), TEXT("Death_Immortal"));
	Map.Add(TEXT("251540"), TEXT("Death_Immortal_Fall"));
	Map.Add(TEXT("251550"), TEXT("Death_Immortal_Recover"));
	Map.Add(TEXT("251600"), TEXT("Death_Plunge"));
	Map.Add(TEXT("251800"), TEXT("Death_Grab"));
	Map.Add(TEXT("252000"), TEXT("Death_Snake"));
	Map.Add(TEXT("259000"), TEXT("Resurrect_01"));
	Map.Add(TEXT("259010"), TEXT("Resurrect_02"));

	// ================================================================
	// Prosthetic Tools (700000-719999)
	// ================================================================
	Map.Add(TEXT("700010"), TEXT("Prosthetic_Shuriken"));
	Map.Add(TEXT("700200"), TEXT("Prosthetic_Shuriken_Charged"));
	Map.Add(TEXT("700240"), TEXT("Prosthetic_Shuriken_Jump"));
	Map.Add(TEXT("700280"), TEXT("Prosthetic_Shuriken_Sprint"));
	Map.Add(TEXT("700300"), TEXT("Prosthetic_Shuriken_Slide"));
	Map.Add(TEXT("700500"), TEXT("Prosthetic_Shuriken_Dash"));
	Map.Add(TEXT("710000"), TEXT("Prosthetic_Axe"));
	Map.Add(TEXT("710100"), TEXT("Prosthetic_Axe_Charged"));
	Map.Add(TEXT("710200"), TEXT("Prosthetic_Axe_Jump"));
	Map.Add(TEXT("710310"), TEXT("Prosthetic_Axe_Dash"));
	Map.Add(TEXT("710400"), TEXT("Prosthetic_Spear"));
	Map.Add(TEXT("710410"), TEXT("Prosthetic_Spear_Charged"));
	Map.Add(TEXT("710420"), TEXT("Prosthetic_Spear_Jump"));
	Map.Add(TEXT("710430"), TEXT("Prosthetic_Spear_Dash"));
	Map.Add(TEXT("710500"), TEXT("Prosthetic_FlameVent"));
	Map.Add(TEXT("710510"), TEXT("Prosthetic_FlameVent_Charged"));
	Map.Add(TEXT("710520"), TEXT("Prosthetic_FlameVent_Jump"));
	Map.Add(TEXT("710530"), TEXT("Prosthetic_FlameVent_Dash"));
	Map.Add(TEXT("710600"), TEXT("Prosthetic_Umbrella"));
	Map.Add(TEXT("710700"), TEXT("Prosthetic_Umbrella_Open"));
	Map.Add(TEXT("710800"), TEXT("Prosthetic_Umbrella_Spin"));
	Map.Add(TEXT("710900"), TEXT("Prosthetic_Umbrella_Attack"));
	Map.Add(TEXT("711000"), TEXT("Prosthetic_Sabimaru"));
	Map.Add(TEXT("711010"), TEXT("Prosthetic_Sabimaru_Combo"));
	Map.Add(TEXT("711020"), TEXT("Prosthetic_Sabimaru_Dash"));
	Map.Add(TEXT("711100"), TEXT("Prosthetic_Whistle"));
	Map.Add(TEXT("711110"), TEXT("Prosthetic_Whistle_Charged"));
	Map.Add(TEXT("711200"), TEXT("Prosthetic_Firecracker"));
	Map.Add(TEXT("711201"), TEXT("Prosthetic_Firecracker_Alt"));
	Map.Add(TEXT("711210"), TEXT("Prosthetic_Firecracker_Dash"));
	Map.Add(TEXT("711211"), TEXT("Prosthetic_Firecracker_Dash_Alt"));
	Map.Add(TEXT("711300"), TEXT("Prosthetic_MistRaven"));
	Map.Add(TEXT("711310"), TEXT("Prosthetic_MistRaven_Fwd"));
	Map.Add(TEXT("711311"), TEXT("Prosthetic_MistRaven_L"));
	Map.Add(TEXT("711312"), TEXT("Prosthetic_MistRaven_R"));
	Map.Add(TEXT("711315"), TEXT("Prosthetic_MistRaven_Bwd"));
	Map.Add(TEXT("711400"), TEXT("Prosthetic_FingerWhistle"));
	Map.Add(TEXT("711500"), TEXT("Prosthetic_DivineAbduction"));
	Map.Add(TEXT("711510"), TEXT("Prosthetic_DivineAbduction_Charged"));

	// ================================================================
	// Grapple (790000-790999)
	// ================================================================
	Map.Add(TEXT("790000"), TEXT("Grapple_Start"));
	Map.Add(TEXT("790010"), TEXT("Grapple_Fly"));
	Map.Add(TEXT("790020"), TEXT("Grapple_Land"));
	Map.Add(TEXT("790030"), TEXT("Grapple_Ledge"));
	Map.Add(TEXT("790040"), TEXT("Grapple_Vault"));
	Map.Add(TEXT("790050"), TEXT("Grapple_Swing"));
	Map.Add(TEXT("790060"), TEXT("Grapple_Swing_L"));
	Map.Add(TEXT("790070"), TEXT("Grapple_Swing_R"));
	Map.Add(TEXT("790080"), TEXT("Grapple_Swing_End"));
	Map.Add(TEXT("790090"), TEXT("Grapple_Attack"));
	Map.Add(TEXT("790100"), TEXT("Grapple_Attack_L"));
	Map.Add(TEXT("790110"), TEXT("Grapple_Attack_R"));
	Map.Add(TEXT("790120"), TEXT("Grapple_Attack_Fwd"));
	Map.Add(TEXT("790130"), TEXT("Grapple_Attack_Bwd"));
	Map.Add(TEXT("790500"), TEXT("Grapple_To_Ledge"));

	return Map;
}

static const TMap<FString, FString>& GetAnimationMap()
{
	static TMap<FString, FString> Map = BuildAnimationMap();
	return Map;
}

FString FSekiroAnimationNameMap::Lookup(const FString& AnimId)
{
	const FString* Found = GetAnimationMap().Find(AnimId);
	return Found ? *Found : FString();
}

FString FSekiroAnimationNameMap::GetCategoryPrefix(const FString& AnimId)
{
	if (AnimId.Len() < 6) return FString();

	const int32 Id = FCString::Atoi(*AnimId);

	if (Id >= 0      && Id < 1000)   return TEXT("Locomotion");
	if (Id >= 1000   && Id < 10000)  return TEXT("WeaponPose");
	if (Id >= 10000  && Id < 50000)  return TEXT("Movement");
	if (Id >= 100000 && Id < 200000) return TEXT("Attack");
	if (Id >= 200000 && Id < 220000) return TEXT("Defense");
	if (Id >= 220000 && Id < 230000) return TEXT("Deathblow");
	if (Id >= 250000 && Id < 260000) return TEXT("Hit");
	if (Id >= 700000 && Id < 720000) return TEXT("Prosthetic");
	if (Id >= 790000 && Id < 791000) return TEXT("Grapple");

	return FString();
}

FString FSekiroAnimationNameMap::Translate(const FString& RawName)
{
	// Pattern: "Sekiro_a000_XXXXXX" → extract the last 6 digits
	FString AnimId;
	int32 LastUnderscore = -1;
	RawName.FindLastChar(TEXT('_'), LastUnderscore);

	if (LastUnderscore >= 0 && (RawName.Len() - LastUnderscore - 1) == 6)
	{
		AnimId = RawName.Right(6);

		// Validate all digits
		bool bAllDigits = true;
		for (int32 i = 0; i < 6; ++i)
		{
			if (!FChar::IsDigit(AnimId[i])) { bAllDigits = false; break; }
		}

		if (bAllDigits)
		{
			const FString Mapped = Lookup(AnimId);
			if (!Mapped.IsEmpty())
			{
				return FString::Printf(TEXT("Sekiro_%s"), *Mapped);
			}

			const FString Category = GetCategoryPrefix(AnimId);
			if (!Category.IsEmpty())
			{
				FString Prefix;
				int32 SecondLastUnderscore = -1;
				for (int32 i = LastUnderscore - 1; i >= 0; --i)
				{
					if (RawName[i] == TEXT('_')) { SecondLastUnderscore = i; break; }
				}
				if (SecondLastUnderscore >= 0)
				{
					Prefix = RawName.Mid(SecondLastUnderscore + 1, LastUnderscore - SecondLastUnderscore - 1);
				}
				return FString::Printf(TEXT("Sekiro_%s_%s_%s"), *Category, *Prefix, *AnimId);
			}
		}
	}

	return RawName;
}
