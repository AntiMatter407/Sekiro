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
            "SekiroAnimBlueprintExt",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",
            "BlueprintGraph",
        });
    }
}
