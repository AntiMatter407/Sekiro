using UnrealBuildTool;

public class SekiroImport : ModuleRules
{
    public SekiroImport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "Slate",
            "SlateCore",
            "InputCore",
            "UnrealEd",
            "AssetTools",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "MeshBuilder",
            "MeshDescription",
            "SkeletalMeshDescription",
            "MaterialEditor",
            "EditorStyle",
            "AnimationDataController",
            "DesktopPlatform",
            "ToolMenus",
            "MainFrame",
            "AppFramework",
            "AssetRegistry",
        });
    }
}
