#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_GlassMarkerSettings.generated.h"

/** Provisional marker tuning is authored in an asset; input keys remain in the IMC. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_GlassMarkerSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker", meta = (ClampMin = "1.0", ClampMax = "250.0", Units = "cm"))
	float ReachCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker", meta = (ClampMin = "0.05", ClampMax = "0.25", Units = "s"))
	float SampleIntervalSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker", meta = (ClampMin = "0.01", Units = "cm"))
	float MinimumPointSpacingCm = 0.15f;

	/** Contact gaps begin a new line; continuous aim movement is split into bounded line pieces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker", meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float MaximumJoinGapSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker", meta = (ClampMin = "1.0", ClampMax = "100.0", Units = "cm"))
	float MaximumSegmentLengthCm = 30.0f;

	/** Turn the tool around its authored grip to present the rear eraser. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker|Presentation")
	FRotator EraserRotationOffset = FRotator(180.0f, 0.0f, 0.0f);

	/** Authored mesh slot whose MarkerInkColor parameter shows the selected ink. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass Marker|Presentation")
	FName InkIndicatorMaterialSlot = TEXT("M_GM_YellowMarking");

	bool IsValidForMarker() const;
};
