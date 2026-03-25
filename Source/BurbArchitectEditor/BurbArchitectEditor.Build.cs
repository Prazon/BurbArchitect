// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BurbArchitectEditor : ModuleRules
{
	public BurbArchitectEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"BurbArchitect",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetTools",
				"ProceduralMeshComponent",
				"MeshDescription",
				"StaticMeshDescription",
				"EditorScriptingUtilities",
				"Blutility",
				"Projects",
				"AdvancedPreviewScene",
				"PropertyEditor",
				"EditorStyle",
				"InputCore",
				"RenderCore",
				"ToolMenus",
				// ... add private dependencies that you statically link with here ...
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}