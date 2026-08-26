using UnrealBuildTool;

public class SekiroGameplayEditor : ModuleRules
{
    public SekiroGameplayEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "SlateCore", "Slate" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "ToolMenus", "DesktopPlatform", "GameplayTags", "AssetRegistry", "ContentBrowser", "SourceControl", "Projects", "InputCore" });
    }
}
