using UnrealBuildTool;

public class SekiroAnimBlueprintExtEditor : ModuleRules
{
    public SekiroAnimBlueprintExtEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "AnimGraph",
            "AnimGraphRuntime",
            "AnimationWarpingRuntime",
            "SekiroAnimBlueprintExt",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",
            "BlueprintGraph",
            "UnLua",
        });
    }
}
