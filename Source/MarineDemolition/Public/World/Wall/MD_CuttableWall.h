#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MD_CuttableWall.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class AMD_WallPiece;
class AMD_CuttableWall;

/** 서버에서 값이 달라진 오염 셀 하나만 전송하기 위한 Fast Array 항목. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellState
	: public FFastArraySerializerItem
{
	GENERATED_BODY()

	FMD_ReplicatedScrapeCellState()
		: CellIndex(INDEX_NONE)
		, RemainingContamination(0.0f)
	{
	}

	FMD_ReplicatedScrapeCellState(
		const int32 InCellIndex,
		const float InRemainingContamination)
		: CellIndex(InCellIndex)
		, RemainingContamination(InRemainingContamination)
	{
	}

	UPROPERTY()
	int32 CellIndex;

	UPROPERTY()
	float RemainingContamination;
};

/**
 * 초기 오염값과 동일한 셀은 보내지 않고, 서버에서 한 번이라도 달라진 셀만 복제한다.
 * Fast Array이므로 연속 긁기 중에는 변경된 항목만 전송되고 늦게 접속한 클라이언트도
 * 지금까지 변경된 전체 항목을 한 번에 받을 수 있다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellStateArray
	: public FFastArraySerializer
{
	GENERATED_BODY()

public:
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FastArrayDeltaSerialize<
			FMD_ReplicatedScrapeCellState,
			FMD_ReplicatedScrapeCellStateArray>(
				Items,
				DeltaParameters,
				*this);
	}

	void SetOwner(AMD_CuttableWall* InOwner);
	void SetCellRemaining(int32 CellIndex, float RemainingContamination);
	void ResetState();

	const TArray<FMD_ReplicatedScrapeCellState>& GetItems() const
	{
		return Items;
	}

	void PreReplicatedRemove(
		const TArrayView<int32>& RemovedIndices,
		int32 FinalSize);
	void PostReplicatedAdd(
		const TArrayView<int32>& AddedIndices,
		int32 FinalSize);
	void PostReplicatedChange(
		const TArrayView<int32>& ChangedIndices,
		int32 FinalSize);

private:
	UPROPERTY()
	TArray<FMD_ReplicatedScrapeCellState> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AMD_CuttableWall> Owner;

	TMap<int32, int32> CellToItemIndex;
};

template<>
struct TStructOpsTypeTraits<FMD_ReplicatedScrapeCellStateArray>
	: public TStructOpsTypeTraitsBase2<FMD_ReplicatedScrapeCellStateArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

// 오염막 완전 제거 시 한 번 발생
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnContaminationFullyRemoved, AMD_CuttableWall*,	Wall);

// 지주격벽 오절단으로 붕괴가 실제 발생했을 때 한 번 발생
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnStrutCollapsed, AMD_CuttableWall*, Wall, AActor*, Cutter);

// 격벽 타입. 칸막이 = 비내력벽(자유 철거), 지주 = 내력벽 (오절단 시 붕괴)
UENUM(BlueprintType)
enum class EMD_WallType : uint8
{
	Partition UMETA(DisplayName = "칸막이격벽"),
	Strut UMETA(DisplayName = "지주격벽"),
};

// 절단 격벽. 스캔 전에는 타입 구분 없는 외형, 전처리 게이트 통과 시만 절단 가능.
// 지주격벽을 지지대 없이 자르면 절단자가 있는 섹터의 천장이 붕괴한다. (시스템 기획 §2)
UCLASS()
class MARINEDEMOLITION_API AMD_CuttableWall : public AActor
{
	GENERATED_BODY()

public:
	AMD_CuttableWall();

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 지지대 설치 시스템이 호출. 서버에서만 변경
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall")
	void SetSupported(bool bSupported) { bHasSupport = bSupported; }

	UFUNCTION(BlueprintPure, Category = "Wall")
	bool IsSupported() const { return bHasSupport; }

	UFUNCTION(BlueprintPure, Category = "Wall")
	EMD_WallType GetWallType() const { return WallType; }

	// 무지지 붕괴 발생 시 방송. 점수 처리는 GameMode가 구독
	UPROPERTY(BlueprintAssignable, Category = "Wall")
	FMD_OnStrutCollapsed OnStrutCollapsed;

	// Cutter = 절단 버튼을 누른 캐릭터
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintPure, Category = "Wall")
	bool CanCut() const;
	
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ScanHighlight(float Duration = 3.0f);

	// 충돌점 사이의 오염 셀을 제거
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHits(
		const FHitResult& StartHit,
		const FHitResult& EndHit);

	// 플레이어가 장착한 헤라 성능을 사용해 충돌점 사이의 오염 셀을 제거
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHitsWithToolSpec(
		const FHitResult& StartHit,
		const FHitResult& EndHit,
		const FMD_ScrapeToolSpec& ToolSpec);

	// 로컬 좌표의 긁기 경로를 적용
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStroke(const FMD_ScrapeStroke& Stroke);

	// 플레이어가 장착한 헤라 성능을 사용해 로컬 긁기 경로를 적용
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithToolSpec(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec);

	/** 유효한 벽 충돌점을 표면 로컬 2D 좌표로 변환 */
	bool TryGetScrapeSurfacePositionFromHit(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm);

	/** 표면 로컬 2D 좌표를 피드백용 월드 좌표로 변환 */
	bool TryGetScrapeSurfaceWorldPosition(
		const FVector2D& SurfacePositionCm,
		FVector& OutWorldPosition) const;

	// 전체 오염 제거 비율(0~1)
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetContaminationCleanRatio() const;

	// 전체 오염 제거율(0~100)
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetContaminationRemovedPercent() const;

	// 남은 오염 비율(0~1)
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetResidualContaminationRatio() const;

	// 오염 완전 제거 여부
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	bool IsContaminationFullyRemoved() const;

	// 최초 완전 제거 시 한 번 방송
	UPROPERTY(BlueprintAssignable, Category = "Wall|Contamination")
	FMD_OnContaminationFullyRemoved OnContaminationFullyRemoved;
	
	// Day 전환 악화: 무작위 위치에 오염 셀을 소량 리필
	void ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells);

	// Day 스냅샷용: 초기값과 다른 셀을 (인덱스, 남은 오염량) 쌍으로 내보낸다
	void ExportContaminationDeltas(TArray<int32>& OutCellIndices, TArray<float>& OutRemainingAmounts) const;

	// Day 스냅샷용: 저장된 셀 상태를 적용하고 시각화·복제를 갱신한다
	void RestoreContaminationCells(const TArray<int32>& CellIndices, const TArray<float>& RemainingAmounts);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 셀 단위 오염막
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual")
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	UPROPERTY(EditAnywhere, Category = "Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	// 지지대 설치 여부. true면 오절단해도 붕괴하지 않음
	UPROPERTY(EditAnywhere, Replicated, Category = "Wall")
	bool bHasSupport = false;

	// 절단 시 드랍할 조각 수 (기획: 5~10개)
	UPROPERTY(EditAnywhere, Category = "Wall")
	int32 PieceCount = 10;

	/** 절단 시 생성할 벽 조각 Blueprint 클래스. Class Defaults에서 반드시 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	// 스캔 하이라이트 / 원복에 재사용할 다이내믹 머티리얼
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	// 오염 격자 크기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (UIMin = "1", UIMax = "128"))
	FIntPoint ContaminationGridDimensions;

	// 셀의 초기 오염량(0~1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	// 작업면 크기(cm, 0이면 자동)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	// 표면 판정 설정. 포함된 헤라 수치는 기존 직접 호출의 기본값으로만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination")
	FMD_ScrapeSettings ScrapeSettings;

	// 벽면과 오염막 사이 거리(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationOverlayOffsetCm;

	// 오염막 두께(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual",
		meta = (ClampMin = "0.01", Units = "cm"))
	float ContaminationOverlayThicknessCm;

	// 오염막 표시 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual")
	bool bShowContaminationOverlay;

	// 절단 허용 게이트. 회의 결정으로 기본 0(잠금 없음) — 정책이 바뀌면 에디터에서 수치만 올리면 됨
	UPROPERTY(EditAnywhere, Category = "Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float PartitionCutGate = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float StrutCutGate = 0.0f;

private:
	friend struct FMD_ReplicatedScrapeCellStateArray;

	// 격자 한 축의 최대 셀 수
	static constexpr int32 MaximumContaminationGridAxis = 256;

	// 작업면 판정 최소 내적값
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;

	// 셀별 오염 상태
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;

	/** 서버에서 변경된 셀만 모든 클라이언트와 늦은 접속자에게 전송한다. */
	UPROPERTY(Replicated, Transient)
	FMD_ReplicatedScrapeCellStateArray ReplicatedContaminationState;

	// 계산된 작업면 크기(cm)
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	FVector2D ResolvedContaminationSurfaceSizeCm;

	// 작업면 로컬 중심
	FVector ContaminationSurfaceCenterLocal;

	// 오염 메시 로컬 Bounds
	FVector ContaminationMeshLocalBoundsMinimum;
	FVector ContaminationMeshLocalBoundsMaximum;

	// 제거 비율 캐시
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float CachedContaminationCleanRatio;

	// 오염 표면 초기화 여부
	bool bContaminationSurfaceInitialized;

	// 완료 이벤트 중복 방지
	bool bContaminationCompletionBroadcast;

	bool bCut = false;

	// 셀 인스턴스 기준 Transform
	TArray<FTransform> ContaminationCellBaseTransforms;

	// 작업면 위치와 크기 계산
	bool ResolveContaminationSurfaceGeometry();

	// 오염 셀 초기화
	void InitializeContaminationSurface();

	// 오염막 인스턴스 생성
	void InitializeContaminationVisualization();

	// 변경된 셀 시각화 갱신
	void UpdateContaminationVisualization(const TArray<FMD_ScrapeCellChange>& CellChanges);

	void HandleReplicatedContaminationState(
		TConstArrayView<int32> StateItemIndices,
		bool bResetToInitial);
	void ApplyAllReplicatedContaminationState();
	void PublishContaminationChanges(
		const TArray<FMD_ScrapeCellChange>& CellChanges);

	// 셀 오염량을 시각 Transform으로 변환
	FTransform MakeContaminationCellVisualTransform(int32 CellIndex) const;

	// 월드 위치를 작업면 좌표로 변환
	bool TryProjectWorldPointToContaminationSurface(const FVector& WorldPoint, FVector2D& OutSurfacePositionCm) const;

	// 충돌점이 오염 작업면인지 확인
	bool IsHitOnContaminationFace(const FHitResult& Hit) const;

	// 하이라이트 원복
	void RestoreScanColor();

	FTimerHandle ScanTimerHandle;
};
