// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Sekiro : ModuleRules
{
	public Sekiro(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Sekiro");

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "EnhancedInput", "AnimGraphRuntime", "SekiroAssetManager" });
	}
}

