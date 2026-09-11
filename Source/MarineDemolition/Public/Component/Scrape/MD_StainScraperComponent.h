#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_StainScraperComponent.generated.h"

class AMD_SurfaceStain;
class UMD_ToolDefinition;

/** One GAS activation owns one finite swing and at most one server-authorized impact. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_StainScraperComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_StainScraperComponent();
	bool StartStroke();
	void CancelStroke();
	float GetStrokeDuration() const { return StrokeDurationSeconds; }
	FVector GetHandSwingOffset() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:

	bool CanUseEquippedScraper() const;
	AMD_SurfaceStain* TraceTarget() const;
	void ApplyImpact();
	void PlaySwing();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSwing();

	UPROPERTY(EditDefaultsOnly, Category = "MD|Stain|Scraper", meta = (ClampMin = "0.2", ClampMax = "2", Units = "s"))
	float StrokeDurationSeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Stain|Scraper", meta = (ClampMin = "10", ClampMax = "250", Units = "cm"))
	float ReachCm = 180.0f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Stain|Scraper", meta = (ClampMin = "0", ClampMax = "20", Units = "cm"))
	float SwingDistanceCm = 6.0f;

	TWeakObjectPtr<AMD_SurfaceStain> StrokeTarget;
	TWeakObjectPtr<UMD_ToolDefinition> StrokeTool;
	FTimerHandle ImpactTimer;
	double StrokeStartedAt = -10.0;
	double NextStrokeAllowedAt = 0.0;
};
