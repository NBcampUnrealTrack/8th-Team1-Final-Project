#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "MD_CuttableWall.generated.h"

class UDecalComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UMD_ScrapeSurfaceComponent;
class AMD_CuttableWall;
class AMD_ContaminationItem;
class AMD_WallPiece;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnStrutCollapsed,
	AMD_CuttableWall*,
	Wall,
	AActor*,
	Cutter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnWallDemolished,
	AMD_CuttableWall*,
	Wall,
	AActor*,
	Cutter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMD_OnWallScanned,
	AMD_CuttableWall*,
	Wall,
	float,
	Duration,
	AActor*,
	Scanner);

UENUM(BlueprintType)
enum class EMD_WallType : uint8
{
	Partition UMETA(DisplayName = "칸막이격벽"),
	Strut UMETA(DisplayName = "지주격벽"),
};

/**
 * A demolishable wall.
 *
 * Wall demolition remains here. Reusable contamination behavior lives in
 * UMD_ScrapeSurfaceComponent. This actor only keeps serialized Blueprint
 * settings that have not been migrated to the component yet.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_CuttableWall : public AActor
{
	GENERATED_BODY()

public:
	AMD_CuttableWall();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void SetSupported(bool bSupported) { bHasSupport = bSupported; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool IsSupported() const { return bHasSupport; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	EMD_WallType GetWallType() const { return WallType; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	UMD_ScrapeSurfaceComponent* GetScrapeSurfaceComponent() const
	{
		return ScrapeSurface.Get();
	}

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnStrutCollapsed OnStrutCollapsed;

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnWallDemolished OnWallDemolished;

	/** Broadcasts a local scan request to overlapping wall utilities. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnWallScanned OnWallScanned;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool CanCut() const;

	UFUNCTION(BlueprintCallable, Category = "MD|Wall")
	void ScanHighlight(float Duration = 3.0f, AActor* Scanner = nullptr);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Existing native visual subobjects are retained so old BP overrides survive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Overlay")
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> FrontContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination")
	TObjectPtr<UMD_ScrapeSurfaceComponent> ScrapeSurface;

	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	UPROPERTY(EditAnywhere, Replicated, Category = "MD|Wall")
	bool bHasSupport = false;

	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	int32 PieceCount = 10;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/** Serialized BP values retained until BP_MD_CuttableWall is migrated. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris")
	TSubclassOf<AMD_ContaminationItem> ContaminationItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris",
		meta = (ClampMin = "0", UIMax = "10"))
	int32 ContaminationItemsPerFace = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural")
	bool bUseProceduralContamination = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float PartitionCutGate = 0.0f;

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float StrutCutGate = 0.0f;

private:
	friend class FMD_CuttableWallScrapeIntegrationTest;
	friend class FMD_CuttableWallArbitraryMeshTest;
	friend class FMD_BarnacleSpawnerTest;
	friend class FMD_BarnacleStrokeBlockingTest;

	void SyncLegacySurfaceConfiguration();

	void RestoreScanColor();

	bool bCut = false;
	FTimerHandle ScanTimerHandle;
};
