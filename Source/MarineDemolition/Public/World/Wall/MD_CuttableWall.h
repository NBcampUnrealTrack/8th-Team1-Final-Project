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
class UNiagaraSystem;
class AMD_CuttableWall;
class AMD_ContaminationItem;
class AMD_WallPiece;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnStrutCollapsed, AMD_CuttableWall*, Wall, AActor*, Cutter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnWallDemolished, AMD_CuttableWall*, Wall,	AActor*, Cutter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMD_OnWallScanned, AMD_CuttableWall*, Wall, float, Duration, AActor*, Scanner);

// 진행도 게이지 UI가 구독한다 (기획 §6)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnCutProgressChanged,
	AMD_CuttableWall*, Wall,
	float, ProgressRatio);

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

	/** 오염 상태와 무관하게 아직 파괴되지 않은 벽이면 절단할 수 있다. */
	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool CanCut() const;

	UFUNCTION(BlueprintCallable, Category = "MD|Wall")
	void ScanHighlight(float Duration = 3.0f, AActor* Scanner = nullptr);

	// 절단 진행도 누적. 서버 전용. 100% 도달 시 Cut()을 호출한다
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall|Cut")
	void AddCutProgress(float DeltaSeconds, AActor* Cutter);

	/** 서버 GA의 마지막 벽 충돌면을 조각 배분까지 동기 전달한다. */
	void AddCutProgressFromHit(
		float DeltaSeconds,
		AActor* Cutter,
		const FHitResult& WallHit);

	// 이 벽을 자르는 데 필요한 총 시간(초)
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Cut")
	float GetRequiredCutSeconds() const;

	// 0~1 진행도. UI 게이지용
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Cut")
	float GetCutProgressRatio() const;

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall|Cut")
	FMD_OnCutProgressChanged OnCutProgressChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * Deprecated inert compatibility shell. It is retained temporarily so old
	 * Blueprint component overrides do not become orphaned.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		AdvancedDisplay, Category = "MD|Wall|Contamination|Deprecated",
		meta = (DeprecatedProperty,
			DeprecationMessage = "Cell ISM contamination overlays are no longer used."))
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

	UPROPERTY(EditAnywhere, Category = "MD|Wall",
		meta = (ClampMin = "0"))
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

	/** 철거 완료 순간 벽 중심에 재생할 원샷 버스트 (NS_Cutter_Finish) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall|VFX")
	TObjectPtr<UNiagaraSystem> DemolishEffect;

	// 기획 §2 — 애니메이션 1사이클(3.567초)의 정수배로 잡는다
	UPROPERTY(EditAnywhere, Category = "MD|Wall|Cut",
		meta = (ClampMin = "0.1", Units = "s"))
	float PartitionCutSeconds = 10.8f;      // 3사이클

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Cut",
		meta = (ClampMin = "0.1", Units = "s"))
	float StrutCutSeconds = 14.4f;          // 4사이클

private:
	friend class FMD_CuttableWallScrapeIntegrationTest;
	friend class FMD_CuttableWallArbitraryMeshTest;
	friend class FMD_BarnacleSpawnerTest;
	friend class FMD_BarnacleStrokeBlockingTest;

	void SyncLegacySurfaceConfiguration();
	void CutFromHit(AActor* Cutter, const FHitResult* WallHit);
	float ResolveCutSurfaceCleanRatio(const FHitResult* WallHit);
	void CompleteCutWithCleanRatio(AActor* Cutter, float CutSurfaceCleanRatio);
	void AddCutProgressInternal(
		float DeltaSeconds,
		AActor* Cutter,
		const FHitResult* WallHit);

	// 파괴 직전 서버가 쏘고 모든 클라이언트가 벽 중심에 원샷 재생한다
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayDemolishEffect();

	UFUNCTION()
	void OnRep_CutProgress();

	bool bCut = false;

	//  누적 절단 시간. 감소하지 않으며 Day 간에도 유지된다 (기획 §3)
	UPROPERTY(ReplicatedUsing = OnRep_CutProgress)
	float CutProgressSeconds = 0.f;
};
