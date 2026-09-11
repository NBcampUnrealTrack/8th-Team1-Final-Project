#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_SurfaceStainDefinition.generated.h"

class AMD_ContaminationItem;
class UMaterialInterface;

/** Numeric identities and asset paths remain stable when silhouettes are revised. */
UENUM(BlueprintType)
enum class EMD_SurfaceStainKind : uint8
{
	Puddle, Droplets,
	Splatter UMETA(DisplayName = "Spilled Paint"),
	Smear, Ring,
	DragMark UMETA(DisplayName = "Flowing Slime"),
	Handprint UMETA(DisplayName = "Merged Puddles"),
	Speckles
};

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SurfaceStainDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	EMD_SurfaceStainKind Kind = EMD_SurfaceStainKind::Puddle;

	/** Flat deferred decal. Projection and scan tint are evaluated on the GPU. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	TObjectPtr<UMaterialInterface> DecalMaterial;

	/** Shared mesh-decal material: intrinsic UV crop and scan color use custom primitive data. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	TObjectPtr<UMaterialInterface> StickerMaterial;

	/** Footprint at 300 units. Other sizes preserve aspect ratio and scale area linearly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain", meta = (ClampMin = "1", Units = "cm"))
	FVector2D SizeCm = FVector2D(40.0, 40.0);

	/** Half-depth. Its central 75% covers height differences without UE's boundary fade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain", meta = (ClampMin = "1", ClampMax = "1500", Units = "cm"))
	float ProjectionDepthCm = 100.0f;

	/** Match the material's angle cutoff to reject stretched side projections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain", meta = (ClampMin = "0.1", ClampMax = "1"))
	float MinimumSurfaceCosine = 0.75f;

	/** Offline alpha coverage, 64 x 64 bytes shared by every instance of this definition. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Stain|Authoring", AdvancedDisplay)
	TArray<uint8> FootprintMask;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	TSubclassOf<AMD_ContaminationItem> RemovedItemClass;

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	bool IsUsable() const;
	FVector2D GetHalfSize(int32 RemainingSize) const;
	bool ContainsUV(const FVector2D& UV) const;
};
