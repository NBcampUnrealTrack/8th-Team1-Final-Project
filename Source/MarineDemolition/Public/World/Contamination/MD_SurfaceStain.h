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

/** Surface-bound decal with an authoritative cleaning budget and one final sortable item. */
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
	bool ApplyPreparedSticker(FMD_StainStickerPatch Patch, UStaticMesh* Mesh);
#endif
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetRemainingSize() const { return RemainingSize; }

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetInitialSize() const;

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	UMD_SurfaceStainDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MD|Stain")
	bool RebuildSticker();
	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	bool HasSticker() const;
	const FString& GetStickerStatus() const { return StickerStatus; }
	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	int32 GetStickerTriangleCount() const { return StickerPatch.Positions.Num() / 3; }

	/** Authoring only. Runtime budget changes require an authoritative hit or save restore. */
	void ConfigurePlacement(UMD_SurfaceStainDefinition* InDefinition, int32 InSize);
	bool ApplyScrapeStroke(int32 RemovalAmount = 100);
	bool RestoreSize(int32 Size);
	bool ContainsSurfacePoint(const FVector& Point, const FVector& Normal) const;
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

	/** Zero is a serialization sentinel for level assets authored before numeric sizes. */
	UPROPERTY(EditAnywhere, Category = "MD|Stain", meta = (ClampMin = "100", ClampMax = "10000"))
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
	UPROPERTY(VisibleInstanceOnly, Category = "MD|Stain")
	FString StickerStatus;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScanMaterial;
	TSet<TWeakObjectPtr<UObject>> ScanSources;
	bool bTransferringItem = false;
};
