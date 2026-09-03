// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MarineDemolition : ModuleRules
{
	public MarineDemolition(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore", "AudioMixer", "AudioCapture",
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
			// FInstancedStruct
			"StructUtils"
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper", "Slate", "SlateCore", "MoviePlayer",
			"DeveloperSettings",
			"ApplicationCore",
			"CoreOnline",
			"VoiceChat",
			// 오염 마스크 런타임 텍스처 업로드
			"RHI",
			// 절단·헤라 VFX
			"Niagara",
			// 수중 서브믹스 LPF (SubmixEffectFilterPreset)
			"Synthesis"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			// 캐러셀의 Enum 전용 디테일 선택기 지원
			PrivateDependencyModuleNames.AddRange(new string[] { "PropertyEditor", "KismetWidgets", "BlueprintGraph" });
		}

        DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
	}
}
