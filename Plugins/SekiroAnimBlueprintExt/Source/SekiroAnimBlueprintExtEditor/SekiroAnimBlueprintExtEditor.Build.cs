using UnrealBuildTool;

public class SekiroAnimBlueprintExtEditor : ModuleRules
{
    public SekiroAnimBlueprintExtEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "SekiroAnimBlueprintExt",
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
            "Kismet",
            "KismetCompiler",
            "Lua",
            "Slate",
            "SlateCore",
            "UnLua",
            "UnrealEd",
        });
    }
}
