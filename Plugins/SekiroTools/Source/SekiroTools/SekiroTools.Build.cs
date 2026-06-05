using UnrealBuildTool;

public class SekiroTools : ModuleRules
{
    public SekiroTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",
            "MaterialEditor",
        });
    }
}
