using UnrealBuildTool;

public class LuaBehaviorTreeEditor : ModuleRules
{
    public LuaBehaviorTreeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "AIModule",
            "Core",
            "CoreUObject",
            "Engine",
            "LuaBehaviorTree",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AIGraph",
            "BehaviorTreeEditor",
            "Kismet",
            "Lua",
            "MessageLog",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnLua",
            "UnrealEd",
        });
    }
}
