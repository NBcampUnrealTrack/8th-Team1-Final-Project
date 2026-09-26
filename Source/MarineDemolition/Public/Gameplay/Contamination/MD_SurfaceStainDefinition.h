#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_SurfaceStainDefinition.generated.h"

class AMD_ContaminationItem;
class UMaterialInterface;
class UStaticMesh;

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

UENUM(BlueprintType)
enum class EMD_SurfaceStainPresentation : uint8
{
	SurfaceSticker,
	SharedVolume
};

/** Automatic placement never infers that a hand-authored corner shape fits an arbitrary surface. */
UENUM(BlueprintType)
enum class EMD_SurfaceStainVolumePlacement : uint8
{
	ManualOnly,
	PlanarSupport,
	ConvexRightAngleSupport
};

/** Offline contact samples for one bounded, authored edge shape (positions in cm). */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SurfaceStainEdgeContact
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement")
	FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement")
	FVector Normal = FVector::UpVector;
};

/** One closed, shared removal shape. Mesh and query vertices use the same contact-anchor frame in cm. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SurfaceStainVolumeStage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Volume")
	TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Volume")
	TArray<FVector> QueryVerticesCm;
	/** Consecutive triples index welded query vertices. The query must be closed and consistently wound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Volume")
	TArray<int32> QueryTriangleIndices;

	FBox QueryBounds = FBox(ForceInit);
};

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SurfaceStainDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	static constexpr int32 VolumeStageCount = 3;
	static constexpr int32 MaximumVolumeQueryTriangles = 4096;
	static constexpr int32 MaximumVolumeQueryVertices = 4096;
	static constexpr int32 MaximumPlacementSamples = 256;

	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual void PostEditUndo() override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	EMD_SurfaceStainKind Kind = EMD_SurfaceStainKind::Puddle;

	/** SharedVolume is explicit: an invalid profile is hidden and cannot be cleaned; it never falls back to a decal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain")
	EMD_SurfaceStainPresentation Presentation = EMD_SurfaceStainPresentation::SurfaceSticker;

	/** Opaque surface material; custom primitive data index 1 carries scan highlighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Volume")
	TObjectPtr<UMaterialInterface> VolumeMaterial;

	/** Exactly three shapes, ordered full, two-thirds, one-third. Initial size scales XY independently of the stage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Volume")
	TArray<FMD_SurfaceStainVolumeStage> VolumeStages;

	UPROPERTY(VisibleAnywhere, Transient, Category = "MD|Stain|Volume")
	FString VolumeValidationStatus;

	/** ManualOnly is the safe default for curved or corner-specific shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement")
	EMD_SurfaceStainVolumePlacement VolumePlacementPolicy = EMD_SurfaceStainVolumePlacement::ManualOnly;

	/** Limits automatic placement only; the existing runtime cleaning-size contract is unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement", meta = (ClampMin = "100", ClampMax = "10000"))
	int32 MaximumPlanarInitialSize = 300;

	UPROPERTY(VisibleAnywhere, Transient, Category = "MD|Stain|Placement")
	FString VolumePlacementValidationStatus;

	/** Local +Y crosses the single convex 90-degree edge; +Z is the main face normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement", meta = (ClampMin = "1", Units = "cm"))
	float EdgeStartCm = 13.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement", meta = (ClampMin = "0", ClampMax = "3", Units = "cm"))
	float EdgeRadiusCm = 0.0f;
	/** Covers the underside of all three stages. Produced alongside the mesh, never at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Stain|Placement")
	TArray<FMD_SurfaceStainEdgeContact> EdgeContacts;

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
	bool UsesSharedVolume() const { return Presentation == EMD_SurfaceStainPresentation::SharedVolume; }
	/** Revalidate after profile authoring/loading, never in the per-actor refresh or targeting loop. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MD|Stain|Volume")
	bool RebuildVolumeValidation();
#if WITH_EDITOR
	bool CanPlaceEdgeVolume(int32 InitialSize, FString& Error) const;
	bool CanPlacePlanarVolume(int32 InitialSize, FString& Error) const;
	const TArray<FVector2D>& GetPlanarPlacementSamples() const { return PlanarPlacementSamples; }
	uint32 GetAuthoringRevision() const;
#endif
	FVector GetVolumeScale(int32 InitialSize) const;
	int32 GetVolumeStageIndex(int32 RemainingSize, int32 InitialSize) const;
	const FMD_SurfaceStainVolumeStage* GetVolumeStage(int32 RemainingSize, int32 InitialSize) const;
	FVector2D GetHalfSize(int32 RemainingSize) const;
	bool ContainsUV(const FVector2D& UV) const;

private:
	bool bVolumeGeometryValid = false;
#if WITH_EDITOR
	void RebuildPlanarPlacementCache();
	bool bEdgePlacementValid = false;
	TArray<FVector2D> PlanarPlacementSamples;
	TArray<FGuid> PlacementMeshGuids;
	uint32 AuthoringRevision = 0;
#endif
};
