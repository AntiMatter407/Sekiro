// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Sekiro : ModuleRules
{
	public Sekiro(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Sekiro");

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AIModule", "NavigationSystem", "InputCore", "HeadMountedDisplay", "EnhancedInput", "AnimGraphRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "MotionTrajectory", "PoseSearch", "SekiroAssetManager", "UnLua", "UMG" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Chooser", "Lua", "Slate", "SlateCore" });
	}
}

