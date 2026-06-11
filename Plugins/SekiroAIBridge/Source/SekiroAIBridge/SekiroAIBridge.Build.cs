using UnrealBuildTool;

public class SekiroAIBridge : ModuleRules
{
    public SekiroAIBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "Sockets",
            "Networking",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            // 编辑器框架
            "UnrealEd",
            "EditorSubsystem",
            "EditorScriptingUtilities",

            // Blueprint 操作
            "Kismet",
            "BlueprintGraph",
            "KismetCompiler",

            // Slate (确认对话框等UI)
            "Slate",
            "SlateCore",
            "EditorStyle",
            "ToolMenus",

            // 资产
            "AssetTools",
            "AssetRegistry",

            // Python
            "PythonScriptPlugin",

            // 项目配置
            "DeveloperSettings",

            // Enhanced Input
            "EnhancedInput",

            // Animation Blueprint 编辑
            "AnimGraph",
            "AnimGraphRuntime",

            // 消息
            "ApplicationCore",
            "InputCore",
        });
    }
}
