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
			"UMG",
			// Landscape (editor import helper uses ALandscape::Import)
			"Landscape",
			"GeometryCollectionEngine",
			// FInstancedStruct
			"StructUtils"
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper", "Slate", "SlateCore",
			"DeveloperSettings",
			"ApplicationCore",
			"CoreOnline",
			// Database
			"SQLiteCore", "SQLiteSupport",
			// 오염 마스크 런타임 텍스처 업로드
			"RHI",
			// 절단·헤라 VFX
			"Niagara"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

        DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
	}
}
