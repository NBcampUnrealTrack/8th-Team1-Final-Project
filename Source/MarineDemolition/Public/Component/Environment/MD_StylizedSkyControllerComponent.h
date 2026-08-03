#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_StylizedSkyControllerComponent.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UMaterialParameterCollection;
class UMaterialInterface;
class UStaticMesh;
class UTextureCube;
class UCurveLinearColor;

USTRUCT(BlueprintType)
struct FMD_StylizedSkyPresentationRefs
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> OrbitInclinator = nullptr;

	UPROPERTY()
	TObjectPtr<USceneComponent> OrbitOrientator = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SkyDome = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CloudDome = nullptr;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> DirectionalLight = nullptr;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLight = nullptr;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> HeightFog = nullptr;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcess = nullptr;
};

/**
 * SoStylized-style sky logic: orbit, MPC, directional light, asset resolve.
 * Meshes/lights stay on AMD_StylizedSkyActor; assign SoftObjects on BP_Sky CDO.
 * Match time: optional UMD_GameStateDayCycleAdapterComponent (never edit GameState for sky).
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_StylizedSkyControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_StylizedSkyControllerComponent();

	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "MD|Sky")
	void BindPresentation(const FMD_StylizedSkyPresentationRefs& Refs);

	UFUNCTION(BlueprintCallable, Category = "MD|Sky")
	void SetFrozenTime(bool bInFrozen, float InFrozenTimeOfDayHours = 12.f);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MD|Sky")
	void RefreshSkyNow();

	UFUNCTION(BlueprintPure, Category = "MD|Sky")
	float GetEffectiveTimeOfDayHours() const;

	UFUNCTION(BlueprintPure, Category = "MD|Sky")
	float GetEffectiveDayCycleProgress() const;

	float ComputeOrbitOrientationDegrees(float TimeOfDayHours) const;
	float MapClockTimeToMpcShaderTime(float ClockHours) const;
	float SampleClassicCurve01(float TimeOfDayHours) const;

protected:
	void ResolveSoftAssets();
	void ApplyResolvedAssets();
	void UpdateSkyFromTime(float TimeOfDayHours, float DayProgress01);
	void WriteMpc(float TimeOfDayHours, float DayProgress01, float OrbitOrientationDegrees);
	void UpdateOrbit(float OrbitOrientationDegrees);
	bool IsSunVisibleAtTime(float TimeOfDayHours) const;
	void UpdateDirectionalLight(float TimeOfDayHours, float DayProgress01);
	void ApplyRuntimePresentation();
	void SuppressConflictingLevelSky();
	void ConfigureInteriorDomeMesh(UStaticMeshComponent* Dome) const;
	float SanitizeTimeOfDayHours(float Hours) const;
	bool IsDrivenByExternalTimeSource() const;
	bool TryReadExternalTime(float& OutHours, float& OutDayProgress) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UStaticMesh> SkyDomeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UStaticMesh> CloudDomeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UMaterialInterface> SkyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UMaterialParameterCollection> EnvironmentMPC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TArray<TSoftObjectPtr<UMaterialParameterCollection>> SupplementaryMPCs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UTextureCube> SkyLightCubemap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UCurveLinearColor> SunColorCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UCurveLinearColor> SunGlowCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UCurveLinearColor> MoonColorCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Content")
	TSoftObjectPtr<UCurveLinearColor> MoonGlowCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	bool bFrozenTime = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float FrozenTimeOfDayHours = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	bool bLocalDayCycle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "1.0"))
	float DayLengthSeconds = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float LocalTimeOfDayHours = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	float OrbitInclinationDegrees = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	float OrbitDegreesPerHour = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	float OrbitOrientationOffsetDegrees = -90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "1.0"))
	float MpcDayLengthSeconds = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "1.0"))
	float MpcNightLengthSeconds = 480.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	FName MpcDayLengthName = TEXT("Day Length");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	FName MpcNightLengthName = TEXT("Night Length");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	FName MpcDayStartTimeName = TEXT("Day Start Time");

	/**
	 * Written to MPC "Day Start Time". Keep 0 so wall-clock hours match shader time
	 * (12 = day, 0/24 = night) for owned Classic sky/cloud materials.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float MpcDayStartTimeHours = 0.f;

	/**
	 * Added when writing MPC "Current Time". SoStylized Classic: CT 0 = day, CT 12 = night.
	 * Default 0: wall 0/24 -> MPC 0 (day); wall 12 -> MPC 12 (night).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float MpcShaderTimeOffsetHours = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float SunriseHour = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float SunsetHour = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	float MoonOrbitYawOffsetDegrees = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SunVisibilityGlowThreshold = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	FVector SkyDomeScale = FVector(1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|DayCycle")
	FVector CloudDomeScale = FVector(40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Lighting")
	float DirectionalLightIntensity = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Lighting")
	float SkyLightIntensity = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Lighting")
	float FogDensity = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Lighting")
	float BloomIntensity = 5.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|Lighting")
	float AutoExposureBias = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky")
	bool bSuppressLevelSkyActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float MpcShaderSunStrengthScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcTimeOfDayName = TEXT("Current Time");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcDayCycleProgressName = TEXT("Day Cycle Progress");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcOrbitOrientationName = TEXT("Orbit Orientation");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcOrbitInclinationName = TEXT("Orbit Inclination");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcSunStrengthName = TEXT("SunStrength");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcSunColorName = TEXT("SunColor");

	/** SoStylized cloud mats sample this for highlights; write directional light forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcSunLightDirectionName = TEXT("Sun Light Direction");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Sky|MPC")
	FName MpcMoonLightDirectionName = TEXT("Moon Light Direction");

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ResolvedMPC;

	UPROPERTY(Transient)
	TObjectPtr<UCurveLinearColor> ResolvedSunColorCurve;

	UPROPERTY(Transient)
	TObjectPtr<UCurveLinearColor> ResolvedSunGlowCurve;

	UPROPERTY(Transient)
	TObjectPtr<UCurveLinearColor> ResolvedMoonColorCurve;

	UPROPERTY(Transient)
	TObjectPtr<UCurveLinearColor> ResolvedMoonGlowCurve;

	TArray<TObjectPtr<UMaterialParameterCollection>> ResolvedSupplementaryMPCs;

	FMD_StylizedSkyPresentationRefs Presentation;
	bool bDidSuppressLevelSky = false;
};
