#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_GlassDrawingProfile.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Explicit glass geometry and draft drawing tuning. No other translucent surface is admitted. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_GlassDrawingProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface")
	TObjectPtr<UStaticMesh> ApprovedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (ClampMin = "0"))
	int32 GlassMaterialSlot = 5;

	/** Collision section, verified against a complex trace on the approved mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (ClampMin = "0"))
	int32 GlassCollisionSection = 5;

	/** Authored from the physics triangle list, retained when dedicated-server render data is stripped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Collision Contract")
	TArray<int32> GlassCollisionFaces;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Collision Contract")
	FGuid CollisionBodySetupGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Collision Contract")
	int32 CollisionMeshLOD = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Collision Contract")
	int32 CollisionMaterialSlot = INDEX_NONE;

	/** Drawing rectangle in mesh-local X/Z centimetres; authored inside the glass outline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface")
	FVector2D LocalMin = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface")
	FVector2D LocalMax = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (Units = "cm"))
	float FrontFaceY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (Units = "cm"))
	float BackFaceY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (ClampMin = "0.01", Units = "cm"))
	float SurfaceToleranceCm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Surface", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MaxSurfaceAngleDegrees = 10.0f;

	/** Ordinary XY plane, 100 cm square, with U along +X and V along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering")
	TObjectPtr<UStaticMesh> InkPlaneMesh;

	/** One-sided masked unlit material. InkMask RGB holds color and A holds coverage; InkGlow and InkFlipV remain parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering")
	TObjectPtr<UMaterialInterface> InkMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (ClampMin = "128", ClampMax = "1024"))
	int32 TextureWidth = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (ClampMin = "128", ClampMax = "1024"))
	int32 TextureHeight = 512;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering")
	FLinearColor InkColor = FLinearColor(1.0f, 0.8f, 0.08f, 1.0f);

	/** Candidate pen colors. An empty palette uses the legacy InkColor as its sole color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (EditFixedOrder))
	TArray<FLinearColor> InkPalette = {
		FLinearColor(1.0f, 0.8f, 0.08f, 1.0f),
		FLinearColor(0.08f, 0.85f, 1.0f, 1.0f),
		FLinearColor(1.0f, 0.18f, 0.55f, 1.0f),
		FLinearColor::White
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float GlowStrength = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "cm"))
	float InkWidthCm = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Rendering", meta = (ClampMin = "0.01", ClampMax = "2.0", Units = "cm"))
	float SurfaceOffsetCm = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Limits", meta = (ClampMin = "1", ClampMax = "2048"))
	int32 MaxSegments = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Limits", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxUsedSurfaces = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Limits", meta = (ClampMin = "1.0", ClampMax = "100.0", Units = "cm"))
	float MaxSegmentLengthCm = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Limits", meta = (ClampMin = "20.0", ClampMax = "300.0", Units = "cm"))
	float MaxDrawDistanceCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Eraser", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "cm"))
	float EraserRadiusCm = 6.0f;

	/** Draft permission: false permits any player holding the marker to erase shared ink. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Eraser")
	bool bOnlyAuthorMayErase = false;

	UFUNCTION(BlueprintPure, Category = "Glass")
	bool IsValidForDrawing() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	int32 GetColorCount() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	bool IsValidColorIndex(uint8 ColorIndex) const;

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	FLinearColor GetInkColor(uint8 ColorIndex) const;

	/** Authoring only: updates this DataAsset's collision contract without changing the source mesh. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Glass|Collision Contract")
	bool RebuildCollisionContract();

	bool ResolveHit(const FHitResult& Hit, FVector2D& OutUV) const;
	bool IsValidUV(const FVector2D& UV) const;
	FVector UVToLocalPoint(const FVector2D& UV, double FaceY) const;
};
