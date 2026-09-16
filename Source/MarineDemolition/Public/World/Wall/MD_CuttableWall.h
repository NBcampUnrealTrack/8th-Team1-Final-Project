#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Scanning/MD_Scannable.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "MD_CuttableWall.generated.h"

class UDecalComponent;
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

/**
 * 절단 가능 지점을 제한하는 선택적 필터. false를 반환하면 그 타격은 진행도를 쌓지 않는다.
 * 인자는 null일 수 있다 (히트 정보 없이 진행도만 더하는 경로).
 * 튜토리얼이 "배관 자리만 자를 수 있다"를 구현하는 데 쓴다.
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FMD_CutLocationFilter, const FHitResult*);

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
class MARINEDEMOLITION_API AMD_CuttableWall : public AActor, public IGameplayTagAssetInterface, public IMD_Scannable
{
	GENERATED_BODY()

public:
	AMD_CuttableWall();

	//~ IGameplayTagAssetInterface
	/** 절단 어빌리티가 대상 여부를 이 태그로 판정한다 */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		TagContainer.AppendTags(OwnedGameplayTags);
	}

	/**
	 * 이 액터가 가진 태그. 기본으로 `Interaction.Cuttable` 이 들어 있어
	 * 배치된 격벽은 추가 작업 없이 절단 대상이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|태그")
	FGameplayTagContainer OwnedGameplayTags;

	virtual void PreRegisterAllComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void SetSupported(bool bSupported) { bHasSupport = bSupported; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool IsSupported() const { return bHasSupport; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	EMD_WallType GetWallType() const { return WallType; }

	/** Original wall geometry, excluding contamination and shadow-only proxies. */
	UStaticMeshComponent* GetScanMesh() const { return Mesh.Get(); }

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

	// IMD_Scannable — 기존 벽 스캔 델리게이트 경로로 연결한다.
	virtual void OnScanned_Implementation(float Duration, AActor* Scanner) override;

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

	/**
	 * 비어 있으면 어디를 쳐도 잘린다 — 배치된 벽의 기본 상태다.
	 * 바인딩하는 쪽이 벽보다 오래 살아야 한다. 서버에서만 평가된다.
	 */
	FMD_CutLocationFilter CutLocationFilter;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

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

	/** Serialized BP values retained until BP_MD_CuttableWall is migrated. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris")
	TSubclassOf<AMD_ContaminationItem> ContaminationItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris",
		meta = (ClampMin = "0", UIMax = "10"))
	int32 ContaminationItemsPerFace = 3;

	/** Legacy authored mode is retained for pre-default-change saves only. */
    UPROPERTY()
    bool bUseProceduralContamination = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural",
        meta = (DisplayName = "Full Contamination"))
    bool bForceFullContamination = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	/** 철거 완료 순간 벽 중심에 재생할 원샷 버스트 (NS_Cutter_Finish) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall|VFX")
	TObjectPtr<UNiagaraSystem> DemolishEffect;

	/**
	 * 철거 완료음 (SFX.Cut.Wall.Break). 소재는 오디오 카탈로그(DA_MD_Audio_Cut)가 정한다.
	 * 비워 두면 무음.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall|Audio", meta = (Categories = "SFX"))
	FGameplayTag DemolishSoundTag;

	// 기획 §2 — 애니메이션 1사이클(3.567초)의 정수배로 잡는다
	UPROPERTY(EditAnywhere, Category = "MD|Wall|Cut",
		meta = (ClampMin = "0.1", Units = "s"))
	float PartitionCutSeconds = 10.8f;      // 3사이클

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Cut",
		meta = (ClampMin = "0.1", Units = "s"))
	float StrutCutSeconds = 14.4f;          // 4사이클

private:

	void SyncLegacySurfaceConfiguration();
	void ApplyNeutralWallColor();
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
