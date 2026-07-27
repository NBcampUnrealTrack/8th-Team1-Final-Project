#include "Environment/MD_StylizedSkyControllerComponent.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Environment/MD_DayCycleTimeSource.h"
#include "Materials/MaterialParameterCollection.h"

UMD_StylizedSkyControllerComponent::UMD_StylizedSkyControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMD_StylizedSkyControllerComponent::OnRegister()
{
	Super::OnRegister();
	ResolveSoftAssets();
}

void UMD_StylizedSkyControllerComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveSoftAssets();
	ApplyResolvedAssets();
	UpdateSkyFromTime(GetEffectiveTimeOfDayHours(), GetEffectiveDayCycleProgress());
}

#if WITH_EDITOR
void UMD_StylizedSkyControllerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ResolveSoftAssets();
	ApplyResolvedAssets();
	UpdateSkyFromTime(GetEffectiveTimeOfDayHours(), GetEffectiveDayCycleProgress());
}
#endif

void UMD_StylizedSkyControllerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bFrozenTime && bLocalDayCycle && !IsDrivenByExternalTimeSource())
	{
		const float SecondsPerDay = FMath::Max(DayLengthSeconds, 1.f);
		LocalTimeOfDayHours = FMath::Fmod(
			LocalTimeOfDayHours + (DeltaTime / SecondsPerDay) * 24.f + 24.f,
			24.f);
	}

	UpdateSkyFromTime(GetEffectiveTimeOfDayHours(), GetEffectiveDayCycleProgress());
}

void UMD_StylizedSkyControllerComponent::BindPresentation(const FMD_StylizedSkyPresentationRefs& Refs)
{
	Presentation = Refs;
	bDidSuppressLevelSky = false;
	ApplyResolvedAssets();
	UpdateSkyFromTime(GetEffectiveTimeOfDayHours(), GetEffectiveDayCycleProgress());
}

void UMD_StylizedSkyControllerComponent::SetFrozenTime(bool bInFrozen, float InFrozenTimeOfDayHours)
{
	bFrozenTime = bInFrozen;
	FrozenTimeOfDayHours = InFrozenTimeOfDayHours;
	if (bInFrozen)
	{
		LocalTimeOfDayHours = InFrozenTimeOfDayHours;
	}
}

void UMD_StylizedSkyControllerComponent::RefreshSkyNow()
{
	ResolveSoftAssets();
	ApplyResolvedAssets();
	UpdateSkyFromTime(GetEffectiveTimeOfDayHours(), GetEffectiveDayCycleProgress());

	if (Presentation.SkyDome)
	{
		Presentation.SkyDome->MarkRenderStateDirty();
	}
	if (Presentation.CloudDome)
	{
		Presentation.CloudDome->MarkRenderStateDirty();
	}
}

bool UMD_StylizedSkyControllerComponent::IsDrivenByExternalTimeSource() const
{
	if (bFrozenTime)
	{
		return false;
	}

	if (const AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->Implements<UMD_DayCycleTimeSource>())
			{
				if (IMD_DayCycleTimeSource::Execute_IsDayCycleActive(Comp))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool UMD_StylizedSkyControllerComponent::TryReadExternalTime(float& OutHours, float& OutDayProgress) const
{
	// Lobby/editor frozen preview must win over GameState adapter.
	if (bFrozenTime)
	{
		return false;
	}

	if (const AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->Implements<UMD_DayCycleTimeSource>()
				&& IMD_DayCycleTimeSource::Execute_IsDayCycleActive(Comp))
			{
				OutHours = IMD_DayCycleTimeSource::Execute_GetTimeOfDayHours(Comp);
				OutDayProgress = IMD_DayCycleTimeSource::Execute_GetDayCycleProgress(Comp);
				return true;
			}
		}
	}
	return false;
}

float UMD_StylizedSkyControllerComponent::SanitizeTimeOfDayHours(float Hours) const
{
	// 24:00 is midnight — same as 0:00 (night). Do not remap to 23.999 (samples day end on curves).
	if (Hours >= 24.f - KINDA_SMALL_NUMBER)
	{
		const float Remainder = FMath::Fmod(Hours, 24.f);
		if (Remainder <= KINDA_SMALL_NUMBER || Remainder >= 24.f - KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return Remainder;
	}

	float Normalized = FMath::Fmod(Hours, 24.f);
	if (Normalized < 0.f)
	{
		Normalized += 24.f;
	}
	return Normalized;
}

float UMD_StylizedSkyControllerComponent::GetEffectiveTimeOfDayHours() const
{
	if (bFrozenTime)
	{
		return SanitizeTimeOfDayHours(FrozenTimeOfDayHours);
	}

	float ExternalHours = 0.f;
	float ExternalProgress = 0.f;
	if (TryReadExternalTime(ExternalHours, ExternalProgress))
	{
		return SanitizeTimeOfDayHours(ExternalHours);
	}

	return SanitizeTimeOfDayHours(LocalTimeOfDayHours);
}

float UMD_StylizedSkyControllerComponent::GetEffectiveDayCycleProgress() const
{
	const auto ProgressFromClock = [this](float ClockHours) -> float
	{
		return FMath::Frac(MapClockTimeToMpcShaderTime(SanitizeTimeOfDayHours(ClockHours)) / 24.f);
	};

	if (bFrozenTime)
	{
		return ProgressFromClock(FrozenTimeOfDayHours);
	}

	float ExternalHours = 0.f;
	float ExternalProgress = 0.f;
	if (TryReadExternalTime(ExternalHours, ExternalProgress))
	{
		return ProgressFromClock(ExternalHours);
	}

	return ProgressFromClock(LocalTimeOfDayHours);
}

float UMD_StylizedSkyControllerComponent::ComputeOrbitOrientationDegrees(float TimeOfDayHours) const
{
	return FMath::UnwindDegrees(TimeOfDayHours * OrbitDegreesPerHour + OrbitOrientationOffsetDegrees);
}

float UMD_StylizedSkyControllerComponent::MapClockTimeToMpcShaderTime(float ClockHours) const
{
	return FMath::Fmod(ClockHours + MpcShaderTimeOffsetHours + 24.f, 24.f);
}

float UMD_StylizedSkyControllerComponent::SampleClassicCurve01(float TimeOfDayHours) const
{
	return FMath::Fmod(TimeOfDayHours + 24.f, 24.f) / 24.f;
}

void UMD_StylizedSkyControllerComponent::ResolveSoftAssets()
{
	auto Load = [](auto& SoftPtr)
	{
		return SoftPtr.IsNull() ? nullptr : SoftPtr.LoadSynchronous();
	};

	ResolvedMPC = Load(EnvironmentMPC);
	ResolvedSunColorCurve = Load(SunColorCurve);
	ResolvedSunGlowCurve = Load(SunGlowCurve);
	ResolvedMoonColorCurve = Load(MoonColorCurve);
	ResolvedMoonGlowCurve = Load(MoonGlowCurve);

	ResolvedSupplementaryMPCs.Reset();
	for (const TSoftObjectPtr<UMaterialParameterCollection>& SoftMpc : SupplementaryMPCs)
	{
		if (UMaterialParameterCollection* Mpc = Load(SoftMpc))
		{
			if (Mpc != ResolvedMPC)
			{
				ResolvedSupplementaryMPCs.Add(Mpc);
			}
		}
	}
}

void UMD_StylizedSkyControllerComponent::ApplyRuntimePresentation()
{
	if (Presentation.OrbitInclinator)
	{
		Presentation.OrbitInclinator->SetRelativeRotation(FRotator(OrbitInclinationDegrees, 0.f, 0.f));
	}
	if (Presentation.SkyDome)
	{
		Presentation.SkyDome->SetRelativeScale3D(SkyDomeScale);
	}
	if (Presentation.CloudDome)
	{
		Presentation.CloudDome->SetRelativeScale3D(CloudDomeScale);
	}
	if (Presentation.DirectionalLight)
	{
		Presentation.DirectionalLight->SetIntensity(DirectionalLightIntensity);
	}
	if (Presentation.SkyLight)
	{
		Presentation.SkyLight->SetIntensity(SkyLightIntensity);
	}
	if (Presentation.HeightFog)
	{
		Presentation.HeightFog->SetFogDensity(FogDensity);
	}
	if (Presentation.PostProcess)
	{
		FPostProcessSettings& Settings = Presentation.PostProcess->Settings;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = BloomIntensity;
		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Settings.AutoExposureApplyPhysicalCameraExposure = false;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = AutoExposureBias;
	}
}

void UMD_StylizedSkyControllerComponent::SuppressConflictingLevelSky()
{
	if (!bSuppressLevelSkyActors || bDidSuppressLevelSky)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	auto HideActor = [](AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		Actor->SetActorHiddenInGame(true);
#if WITH_EDITOR
		Actor->SetIsTemporarilyHiddenInEditor(true);
#endif
	};

	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		HideActor(*It);
	}
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (It->GetOwner() != GetOwner())
		{
			HideActor(*It);
		}
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (It->GetOwner() != GetOwner())
		{
			HideActor(*It);
		}
	}

	bDidSuppressLevelSky = true;
}

void UMD_StylizedSkyControllerComponent::ConfigureInteriorDomeMesh(UStaticMeshComponent* Dome) const
{
	if (!Dome)
	{
		return;
	}

	Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dome->SetCastShadow(false);
	Dome->SetReceivesDecals(false);
	Dome->SetGenerateOverlapEvents(false);
	Dome->SetCanEverAffectNavigation(false);
	Dome->SetVisibility(true);
	Dome->SetHiddenInGame(false);
	Dome->SetReverseCulling(true);
	Dome->BoundsScale = 1000.f;
	Dome->SetTranslucentSortPriority(0);
	Dome->SetForceDisableNanite(true);
}

void UMD_StylizedSkyControllerComponent::ApplyResolvedAssets()
{
	auto LoadMesh = [](const TSoftObjectPtr<UStaticMesh>& Soft) -> UStaticMesh*
	{
		return Soft.IsNull() ? nullptr : Soft.LoadSynchronous();
	};
	auto LoadMat = [](const TSoftObjectPtr<UMaterialInterface>& Soft) -> UMaterialInterface*
	{
		return Soft.IsNull() ? nullptr : Soft.LoadSynchronous();
	};

	if (Presentation.SkyDome)
	{
		if (UStaticMesh* Mesh = LoadMesh(SkyDomeMesh))
		{
			Presentation.SkyDome->SetStaticMesh(Mesh);
			ConfigureInteriorDomeMesh(Presentation.SkyDome);
		}
		if (UMaterialInterface* Mat = LoadMat(SkyMaterial))
		{
			Presentation.SkyDome->SetMaterial(0, Mat);
		}
	}

	if (Presentation.CloudDome)
	{
		if (UStaticMesh* Mesh = LoadMesh(CloudDomeMesh))
		{
			Presentation.CloudDome->SetStaticMesh(Mesh);
			ConfigureInteriorDomeMesh(Presentation.CloudDome);
		}
		if (UMaterialInterface* Mat = LoadMat(CloudMaterial))
		{
			Presentation.CloudDome->SetMaterial(0, Mat);
		}
	}

	if (Presentation.SkyLight && !SkyLightCubemap.IsNull())
	{
		if (UTextureCube* Cube = SkyLightCubemap.LoadSynchronous())
		{
			Presentation.SkyLight->SetCubemap(Cube);
			Presentation.SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
			Presentation.SkyLight->RecaptureSky();
		}
	}

	ApplyRuntimePresentation();
}

void UMD_StylizedSkyControllerComponent::UpdateSkyFromTime(float TimeOfDayHours, float DayProgress01)
{
	SuppressConflictingLevelSky();

	const float OrbitDegrees = ComputeOrbitOrientationDegrees(TimeOfDayHours);
	UpdateOrbit(OrbitDegrees);
	UpdateDirectionalLight(TimeOfDayHours, DayProgress01);
	WriteMpc(TimeOfDayHours, DayProgress01, OrbitDegrees);
}

void UMD_StylizedSkyControllerComponent::UpdateOrbit(float OrbitOrientationDegrees)
{
	if (Presentation.OrbitOrientator)
	{
		Presentation.OrbitOrientator->SetRelativeRotation(FRotator(0.f, OrbitOrientationDegrees, 0.f));
	}
}

bool UMD_StylizedSkyControllerComponent::IsSunVisibleAtTime(float TimeOfDayHours) const
{
	const float ShaderTime = MapClockTimeToMpcShaderTime(TimeOfDayHours);
	// Day band around midnight on wall/shader clock: 0/24 day, 12 night.
	const bool bWithinDayWindow = ShaderTime <= SunriseHour || ShaderTime >= SunsetHour;

	if (!bWithinDayWindow)
	{
		return false;
	}

	// Classic SunGlow has a zero key at t=0.5 (shader noon). Never drop out of day window.
	if (!ResolvedSunGlowCurve || SunVisibilityGlowThreshold <= 0.f)
	{
		return true;
	}

	const float Glow = FMath::Max(
		ResolvedSunGlowCurve->GetLinearColorValue(SampleClassicCurve01(ShaderTime)).R,
		0.25f);
	return Glow > SunVisibilityGlowThreshold;
}

void UMD_StylizedSkyControllerComponent::UpdateDirectionalLight(float TimeOfDayHours, float DayProgress01)
{
	if (!Presentation.DirectionalLight)
	{
		return;
	}

	const float ShaderTime = MapClockTimeToMpcShaderTime(TimeOfDayHours);
	const bool bSunVisible = IsSunVisibleAtTime(TimeOfDayHours);
	const float CurveSample01 = SampleClassicCurve01(ShaderTime);

	FLinearColor LightColor = FLinearColor(1.f, 0.98f, 0.92f, 1.f);
	float Glow = 1.f;

	if (bSunVisible)
	{
		if (ResolvedSunColorCurve)
		{
			LightColor = ResolvedSunColorCurve->GetLinearColorValue(CurveSample01);
		}
		if (ResolvedSunGlowCurve)
		{
			Glow = ResolvedSunGlowCurve->GetLinearColorValue(CurveSample01).R;
			// Classic SunGlow has an explicit zero key at t=0.5 (noon on 0..1 curve).
			Glow = FMath::Max(Glow, 0.25f);
		}
		Presentation.DirectionalLight->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	}
	else
	{
		LightColor = FLinearColor(0.75f, 0.82f, 1.f, 1.f);
		Glow = 0.35f;
		if (ResolvedMoonColorCurve)
		{
			LightColor = ResolvedMoonColorCurve->GetLinearColorValue(CurveSample01);
		}
		if (ResolvedMoonGlowCurve)
		{
			Glow = ResolvedMoonGlowCurve->GetLinearColorValue(CurveSample01).R;
		}
		Presentation.DirectionalLight->SetRelativeRotation(FRotator(-55.f, MoonOrbitYawOffsetDegrees, 0.f));
	}

	Presentation.DirectionalLight->SetLightColor(LightColor);
	Presentation.DirectionalLight->SetIntensity(DirectionalLightIntensity * FMath::Max(Glow, 0.05f));
	Presentation.DirectionalLight->SetAtmosphereSunLight(false);
}

void UMD_StylizedSkyControllerComponent::WriteMpc(
	float TimeOfDayHours,
	float DayProgress01,
	float OrbitOrientationDegrees)
{
	if (!GetWorld() || !GetOwner())
	{
		return;
	}

	TSet<UMaterialParameterCollection*> MpcTargets;
	auto AddMpcTarget = [&MpcTargets](UMaterialParameterCollection* Mpc)
	{
		if (Mpc)
		{
			MpcTargets.Add(Mpc);
		}
	};

	AddMpcTarget(EnvironmentMPC.LoadSynchronous());
	for (const TSoftObjectPtr<UMaterialParameterCollection>& SoftMpc : SupplementaryMPCs)
	{
		AddMpcTarget(SoftMpc.LoadSynchronous());
	}
	AddMpcTarget(ResolvedMPC);

	const bool bSunVisible = IsSunVisibleAtTime(TimeOfDayHours);

	FLinearColor SunColor = FLinearColor(1.f, 0.98f, 0.92f, 1.f);
	float SunGlow = 1.f;
	FLinearColor MoonColor = FLinearColor(0.75f, 0.82f, 1.f, 1.f);
	float MoonGlow = 0.35f;

	const float ShaderTime = MapClockTimeToMpcShaderTime(TimeOfDayHours);
	const float CurveSample01 = SampleClassicCurve01(ShaderTime);

	if (ResolvedSunColorCurve)
	{
		SunColor = ResolvedSunColorCurve->GetLinearColorValue(CurveSample01);
	}
	if (ResolvedSunGlowCurve)
	{
		SunGlow = ResolvedSunGlowCurve->GetLinearColorValue(CurveSample01).R;
		if (bSunVisible)
		{
			SunGlow = FMath::Max(SunGlow, 0.25f);
		}
	}
	if (ResolvedMoonColorCurve)
	{
		MoonColor = ResolvedMoonColorCurve->GetLinearColorValue(CurveSample01);
	}
	if (ResolvedMoonGlowCurve)
	{
		MoonGlow = ResolvedMoonGlowCurve->GetLinearColorValue(CurveSample01).R;
	}

	const FLinearColor ActiveLightColor = bSunVisible ? SunColor : MoonColor;
	const float ActiveGlow = bSunVisible ? SunGlow : MoonGlow;

	const float MpcShaderTime = MapClockTimeToMpcShaderTime(TimeOfDayHours);
	const float MpcShaderProgress = FMath::Frac(MpcShaderTime / 24.f);

	FLinearColor SunLightDir(0.f, 0.f, 1.f, 1.f);
	FLinearColor MoonLightDir(0.f, 0.f, 1.f, 1.f);
	if (Presentation.DirectionalLight)
	{
		// SoStylized cloud mats treat this as direction toward the light (to the sun).
		// DirectionalLight forward is the light travel direction (into the ground at day).
		const FVector ToSun = -Presentation.DirectionalLight->GetForwardVector();
		SunLightDir = FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 1.f);
		MoonLightDir = FLinearColor(-ToSun.X, -ToSun.Y, -ToSun.Z, 1.f);
	}

	AActor* WorldContext = GetOwner();
	auto SetScalar = [WorldContext](UMaterialParameterCollection* Collection, FName Name, float Value)
	{
		if (Collection && !Name.IsNone() && WorldContext)
		{
			UKismetMaterialLibrary::SetScalarParameterValue(WorldContext, Collection, Name, Value);
		}
	};
	auto SetVector = [WorldContext](UMaterialParameterCollection* Collection, FName Name, const FLinearColor& Value)
	{
		if (Collection && !Name.IsNone() && WorldContext)
		{
			UKismetMaterialLibrary::SetVectorParameterValue(WorldContext, Collection, Name, Value);
		}
	};

	for (UMaterialParameterCollection* MPCAsset : MpcTargets)
	{
		SetScalar(MPCAsset, MpcTimeOfDayName, MpcShaderTime);
		SetScalar(MPCAsset, MpcDayCycleProgressName, MpcShaderProgress);
		SetScalar(MPCAsset, MpcOrbitOrientationName, OrbitOrientationDegrees);
		SetScalar(MPCAsset, MpcOrbitInclinationName, OrbitInclinationDegrees);
		SetScalar(MPCAsset, MpcDayLengthName, MpcDayLengthSeconds);
		SetScalar(MPCAsset, MpcNightLengthName, MpcNightLengthSeconds);
		SetScalar(MPCAsset, MpcDayStartTimeName, MpcDayStartTimeHours);
		const float SunStrengthValue = bSunVisible
			? FMath::Max(ActiveGlow * MpcShaderSunStrengthScale, 1.0f * MpcShaderSunStrengthScale)
			: FMath::Max(MoonGlow * MpcShaderSunStrengthScale, 0.15f * MpcShaderSunStrengthScale);
		SetScalar(MPCAsset, MpcSunStrengthName, SunStrengthValue);
		SetVector(MPCAsset, MpcSunColorName, ActiveLightColor);
		SetVector(MPCAsset, MpcSunLightDirectionName, SunLightDir);
		SetVector(MPCAsset, MpcMoonLightDirectionName, MoonLightDir);
	}

	if (Presentation.SkyDome)
	{
		Presentation.SkyDome->MarkRenderStateDirty();
	}
	if (Presentation.CloudDome)
	{
		Presentation.CloudDome->MarkRenderStateDirty();
	}
}
