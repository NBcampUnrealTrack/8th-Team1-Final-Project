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
			// Native StateTree conditions/tasks are exposed by public headers.
			"StateTreeModule",
			// Online
			"OnlineSubsystem", "OnlineSubsystemUtils",
			//UI
			"UMG", "CommonUI", "CommonInput",
			// Landscape (editor import helper uses ALandscape::Import)
			"Landscape",
			"GeometryCollectionEngine",
			// Geometry types used by placed-contaminant surface fitting.
			"GeometryCore",
			// FInstancedStruct
			"StructUtils"
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper", "Slate", "SlateCore", "MoviePlayer",
			// 패키지 빌드의 맵 에셋 클래스 조회
			"AssetRegistry",
			"DeveloperSettings",
			"GameplayStateTreeModule",
            // Process newly registered collision bodies without advancing simulation during map loading.
            "Chaos", "PhysicsCore",
			"ApplicationCore",
			"CoreOnline",
			// 기존 온라인 음성 캡처와 입출력 장치 런타임 제어
			"Voice", "AudioCaptureCore", "AudioMixerCore", "SignalProcessing",
			// 렌더 리소스와 셰이더
			"RHI", "RenderCore",
            // Bounded runtime solid-contamination geometry (no editor dependency).
            "GeometryFramework", "GeometryAlgorithms",
			// 절단·배치 오염물 VFX
			"Niagara",
			// 수중 서브믹스 LPF (SubmixEffectFilterPreset)
			"Synthesis"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MeshDescription", "StaticMeshDescription", "MeshConversion", "DynamicMesh" });
			// 캐러셀의 Enum 전용 디테일 선택기 지원
			PrivateDependencyModuleNames.AddRange(new string[] { "PropertyEditor", "KismetWidgets", "BlueprintGraph" });
			PrivateDependencyModuleNames.Add("EditorWidgets");
		}

        DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
	}
}
