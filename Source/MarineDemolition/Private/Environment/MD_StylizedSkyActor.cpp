#include "Environment/MD_StylizedSkyActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Environment/MD_GameStateDayCycleAdapterComponent.h"
#include "Environment/MD_StylizedSkyControllerComponent.h"

AMD_StylizedSkyActor::AMD_StylizedSkyActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	OrbitInclinator = CreateDefaultSubobject<USceneComponent>(TEXT("OrbitInclinator"));
	OrbitInclinator->SetupAttachment(SceneRoot);

	OrbitOrientator = CreateDefaultSubobject<USceneComponent>(TEXT("OrbitOrientator"));
	OrbitOrientator->SetupAttachment(OrbitInclinator);

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SkyDirectionalLight"));
	DirectionalLight->SetupAttachment(OrbitOrientator);
	DirectionalLight->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	DirectionalLight->SetAtmosphereSunLight(false);
	DirectionalLight->SetMobility(EComponentMobility::Movable);

	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(SceneRoot);

	CloudDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDomeClouds"));
	CloudDome->SetupAttachment(SceneRoot);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("ExponentialHeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetFogHeightFalloff(0.2f);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;

	SkyController = CreateDefaultSubobject<UMD_StylizedSkyControllerComponent>(TEXT("SkyController"));
	GameStateTimeAdapter = CreateDefaultSubobject<UMD_GameStateDayCycleAdapterComponent>(TEXT("GameStateTimeAdapter"));
}

void AMD_StylizedSkyActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BindSkyPresentation();
}

void AMD_StylizedSkyActor::BeginPlay()
{
	Super::BeginPlay();
	BindSkyPresentation();
}

void AMD_StylizedSkyActor::BindSkyPresentation()
{
	if (!SkyController)
	{
		return;
	}

	FMD_StylizedSkyPresentationRefs Refs;
	Refs.OrbitInclinator = OrbitInclinator;
	Refs.OrbitOrientator = OrbitOrientator;
	Refs.SkyDome = SkyDome;
	Refs.CloudDome = CloudDome;
	Refs.DirectionalLight = DirectionalLight;
	Refs.SkyLight = SkyLight;
	Refs.HeightFog = HeightFog;
	Refs.PostProcess = PostProcess;
	SkyController->BindPresentation(Refs);
}

void AMD_StylizedSkyActor::SetFrozenTime(bool bInFrozen, float InFrozenTimeOfDayHours)
{
	if (SkyController)
	{
		SkyController->SetFrozenTime(bInFrozen, InFrozenTimeOfDayHours);
	}
}

void AMD_StylizedSkyActor::RefreshSkyNow()
{
	if (SkyController)
	{
		SkyController->RefreshSkyNow();
	}
}

float AMD_StylizedSkyActor::GetEffectiveTimeOfDayHours() const
{
	return SkyController ? SkyController->GetEffectiveTimeOfDayHours() : 12.f;
}

float AMD_StylizedSkyActor::GetEffectiveDayCycleProgress() const
{
	return SkyController ? SkyController->GetEffectiveDayCycleProgress() : 0.5f;
}
