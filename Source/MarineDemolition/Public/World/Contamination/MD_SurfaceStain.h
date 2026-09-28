#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "MD_SurfaceStain.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMD_SurfaceStainDefinition;

/** Surface-bound contamination with an authoritative cleaning budget and one final sortable item. */
UCLASS()
class MARINEDEMOLITION_API AMD_SurfaceStain : public AActor
{
	GENERATED_BODY()
public:
	AMD_SurfaceStain();
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual void PostEditUndo() override;
	virtual void PostEditImport() override;
	bool ApplyPreparedSticker(FMD_StainStickerPatch Patch, UStaticMesh* Mesh);
	TSoftObjectPtr<UMD_SurfaceStainDefinition> GetRefitSource() const;
	float GetRefitSearchDistance() const { return RefitSearchDistance; }
#endif
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetRemainingSize() const { return RemainingSize; }

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetInitialSize() const;

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	UMD_SurfaceStainDefinition* GetDefinition() const { return FittedDefinition ? FittedDefinition : Definition; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MD|Stain")
	bool RebuildSticker();
	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	bool HasSticker() const;
	UFUNCTION(BlueprintPure, Category = "MD|Stain|Volume")
	bool UsesSharedVolume() const;
	UFUNCTION(BlueprintPure, Category = "MD|Stain|Volume")
	int32 GetVolumeStageIndex() const;
	UFUNCTION(BlueprintCallable, Category = "MD|Stain|Volume")
	bool TraceVolume(const FVector& Origin, const FVector& Direction, double MaxDistance, double& OutDistance) const;
	const FString& GetStickerStatus() const { return StickerStatus; }
	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetStickerTriangleCount() const { return StickerPatch.Positions.Num() / 3; }

	/** Authoring only. Runtime budget changes require an authoritative hit or save restore. */
	void ConfigurePlacement(UMD_SurfaceStainDefinition* InDefinition, int32 InSize,
		UMD_SurfaceStainDefinition* InSourceDefinition = nullptr);
	/** RemovalAmount is actual stored size, after tool-strength conversion; 1000 is one basic stroke. */
	bool ApplyScrapeStroke(int32 RemovalAmount = 1000);
	bool RestoreSize(int32 Size);
	bool ContainsSurfacePoint(const FVector& Point, const FVector& Normal) const;
	bool TraceSticker(const FVector& Origin, const FVector& Direction, double MaxDistance, double& OutDistance) const;
	void SetScanHighlighted(UObject* Source, bool bHighlighted);
	UDecalComponent* GetDecal() const { return Decal; }
	FBoxSphereBounds GetStainBounds() const;

protected:
	virtual void BeginPlay() override;
private:
	UFUNCTION()
	void RefreshSize();
	void RefreshMaterial();
	bool TransferRemovedItem();

	UPROPERTY(EditAnywhere, ReplicatedUsing = RefreshSize, Category = "MD|Stain")
	TObjectPtr<UMD_SurfaceStainDefinition> Definition;

	/** Immutable level-authored geometry. Clients load it with the actor; only cleaning state is replicated. */
	UPROPERTY(Instanced)
	TObjectPtr<UMD_SurfaceStainDefinition> FittedDefinition;

#if WITH_EDITORONLY_DATA
	/** Original unfitted shape. A soft editor-only reference adds no runtime mesh dependency. */
	UPROPERTY(EditInstanceOnly, Category = "MD|Stain|Surface Fit", meta = (DisplayName = "Refit Source"))
	TSoftObjectPtr<UMD_SurfaceStainDefinition> RefitSource;

	/** Search along the stain's local Z axis; size is controlled by Initial Size. */
	UPROPERTY(EditInstanceOnly, Category = "MD|Stain|Surface Fit", meta = (ClampMin = "1", ClampMax = "1500", Units = "cm"))
	float RefitSearchDistance = 100.0f;
#endif

	/** Zero is a serialization sentinel for level assets authored before numeric sizes. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = RefreshSize, Category = "MD|Stain", meta = (ClampMin = "100", ClampMax = "5000"))
	int32 InitialSize = 0;

	/** Retained solely to migrate previously saved level actors. */
	UPROPERTY()
	uint8 InitialStage = 3;

	UPROPERTY(ReplicatedUsing = RefreshSize, VisibleInstanceOnly, Category = "MD|Stain")
	int32 RemainingSize = 300;

	UPROPERTY(VisibleAnywhere, Category = "MD|Stain")
	TObjectPtr<UBoxComponent> SurfaceQuery;
	UPROPERTY(VisibleAnywhere, Category = "MD|Stain")
	TObjectPtr<UDecalComponent> Decal;
	UPROPERTY(VisibleAnywhere, Category = "MD|Stain")
	TObjectPtr<UStaticMeshComponent> StickerMesh;
	UPROPERTY()
	FMD_StainStickerPatch StickerPatch;
	/** Preserve the original baked mesh while the same actor previews an opt-in shared volume. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> LegacyStickerMesh;
	UPROPERTY(VisibleInstanceOnly, Category = "MD|Stain")
	FString StickerStatus;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScanMaterial;
	TSet<TWeakObjectPtr<UObject>> ScanSources;
	bool bTransferringItem = false;
};
