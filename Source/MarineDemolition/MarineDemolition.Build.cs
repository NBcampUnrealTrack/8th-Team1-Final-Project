// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MarineDemolition : ModuleRules
{
	public MarineDemolition(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore",
			// GAS
			"GameplayAbilities", "GameplayTags", "GameplayTasks",
			// AI
			"AIModule",
			// Online
			"OnlineSubsystem", "OnlineSubsystemUtils",
			//UI
			"UMG", "CommonUI", "CommonInput",
			// Landscape (editor import helper uses ALandscape::Import)
			"Landscape",
			"GeometryCollectionEngine",
			// Solid geometry bounds are exposed by the runtime visual component header.
			"GeometryCore",
			// FInstancedStruct
			"StructUtils"
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper", "Slate", "SlateCore", "MoviePlayer",
			"DeveloperSettings",
			"ApplicationCore",
			"CoreOnline",
			// 오염 마스크 런타임 텍스처 업로드
			"RHI", "RenderCore",
            // Bounded runtime solid-contamination geometry (no editor dependency).
            "GeometryFramework", "GeometryAlgorithms",
            // Shared solid templates, GPU displacement and sparse GPU updates.
            "MDSolidRendering",
			// 절단·헤라 VFX
			"Niagara",
			// 수중 서브믹스 LPF (SubmixEffectFilterPreset)
			"Synthesis", "AudioMixer"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MeshDescription", "StaticMeshDescription", "MeshConversion" });
		}

        DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
	}
}
