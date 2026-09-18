using UnrealBuildTool;

public class LuaAnimBlueprint : ModuleRules
{
    public LuaAnimBlueprint(ReadOnlyTargetRules Target) : base(Target)
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
