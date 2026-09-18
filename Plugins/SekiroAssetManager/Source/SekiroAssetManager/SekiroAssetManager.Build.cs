// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SekiroAssetManager : ModuleRules
{
    public SekiroAssetManager(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InputCore",
            "Slate",
            "SlateCore",
            "Json",
            "JsonUtilities",
            "AssetRegistry",
            "EditorStyle",
            "UnrealEd",
            "MainFrame",
            "ContentBrowser",
            "RawMesh",
            "MeshDescription",
            "StaticMeshDescription",
            "SkeletalMeshDescription",
            "AnimationCore",
            "AnimationDataController",
            "MaterialEditor",       // UMaterialEditingLibrary：材质表达式创建与连接
            "TargetPlatform",
        });
    }
}
