#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_StylizedSkyActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UMD_StylizedSkyControllerComponent;

/**
 * SoStylized sky actor shell: presentation components + controller/adapters.
 * Logic lives in UMD_StylizedSkyControllerComponent — assign SoftObjects on BP_Sky CDO.
 * Match time: local 24h loop (DayLengthSeconds, default 3600s = 1 real hour per sky day).
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_StylizedSkyActor : public AActor
{
	GENERATED_BODY()

public:
	AMD_StylizedSkyActor();

	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "MD|Sky")
	void SetFrozenTime(bool bInFrozen, float InFrozenTimeOfDayHours = 12.f);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MD|Sky")
	void RefreshSkyNow();

	UFUNCTION(BlueprintPure, Category = "MD|Sky")
	float GetEffectiveTimeOfDayHours() const;

	UFUNCTION(BlueprintPure, Category = "MD|Sky")
	float GetEffectiveDayCycleProgress() const;

	UFUNCTION(BlueprintPure, Category = "MD|Sky")
	UMD_StylizedSkyControllerComponent* GetSkyController() const { return SkyController; }

protected:
	void BindSkyPresentation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> OrbitInclinator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> OrbitOrientator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CloudDome;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_StylizedSkyControllerComponent> SkyController;
};
