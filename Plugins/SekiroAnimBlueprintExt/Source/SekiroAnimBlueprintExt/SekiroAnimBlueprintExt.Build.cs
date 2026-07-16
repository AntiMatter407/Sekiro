using UnrealBuildTool;

public class SekiroAnimBlueprintExt : ModuleRules
{
    public SekiroAnimBlueprintExt(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnLua",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Lua",
        });
    }
}
