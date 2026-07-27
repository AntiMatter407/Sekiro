using UnrealBuildTool;

public class SekiroLuaBehaviorTreeExtEditor : ModuleRules
{
    public SekiroLuaBehaviorTreeExtEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "AIModule",
            "Core",
            "CoreUObject",
            "Engine",
            "SekiroLuaBehaviorTreeExt",
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
