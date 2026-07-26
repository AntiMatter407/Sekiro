using UnrealBuildTool;

public class SekiroLuaBehaviorTreeExt : ModuleRules
{
    public SekiroLuaBehaviorTreeExt(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });
    }
}
