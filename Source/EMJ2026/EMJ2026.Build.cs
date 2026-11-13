// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EMJ2026 : ModuleRules
{
	public EMJ2026(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GeometryFramework", "GeometryScriptingCore", "GeometryCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "Slate", "SlateCore", "UMG", "Json", "RenderCore" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor", "AssetRegistry" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
