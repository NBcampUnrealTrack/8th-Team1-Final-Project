#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_ThrowSettings.generated.h"

class AMD_EquipmentPickup;

/** Shared, asset-authored tuning for charged throws. Input keys live in the IMC. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_ThrowSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.01", Units = "s"))
	float MaxChargeSeconds = 1.2f;

	/** Uncharged / fully charged launch speed, in cm/s. Actual distance depends on physics and aim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0"))
	FVector2D TrashLaunchSpeed = FVector2D(550.0, 1000.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0"))
	FVector2D EquipmentLaunchSpeed = FVector2D(450.0, 775.0);

	/** Added to the view direction's vertical component before normalizing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UpwardAimBias = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InheritedVelocityScale = 1.0f;

	/** Equipment starts outside the pawn; a swept collision check must also succeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "1.0", Units = "cm"))
	float EquipmentReleaseDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", Units = "cm"))
	float CollisionClearance = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0.0", Units = "s"))
	float ThrowCooldownSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw")
	TSubclassOf<AMD_EquipmentPickup> EquipmentPickupClass;

	bool IsValidForThrow() const;
};
