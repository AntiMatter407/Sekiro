using UnrealBuildTool;

public class SekiroLuaBehaviorTreeExt : ModuleRules
{
    public SekiroLuaBehaviorTreeExt(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "AIModule",
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTasks",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Lua",
            "UnLua",
        });
    }
}
