using UnrealBuildTool;

public class LuaAnimBlueprintEditor : ModuleRules
{
    public LuaAnimBlueprintEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Chooser",
            "Engine",
            "LuaAnimBlueprint",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AnimGraph",
            "AnimGraphRuntime",
            "AnimationWarpingEditor",
            "AnimationWarpingRuntime",
            "AnimationBlueprintEditor",
            "AssetRegistry",
            "BlueprintGraph",
            "DirectoryWatcher",
            "DesktopPlatform",
            "InputCore",
            "Json",
            "Kismet",
            "KismetCompiler",
            "Lua",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnLua",
            "UnrealEd",
            "WorkspaceMenuStructure",
        });
    }
}
