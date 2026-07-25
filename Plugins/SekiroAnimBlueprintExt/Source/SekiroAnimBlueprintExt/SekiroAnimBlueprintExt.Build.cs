using UnrealBuildTool;

public class SekiroAnimBlueprintExt : ModuleRules
{
    public SekiroAnimBlueprintExt(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "AnimGraphRuntime",
            "Core",
            "CoreUObject",
            "Engine",
            "UnLua",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Json",
            "Lua",
        });
    }
}
