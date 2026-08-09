#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MD_CuttableWall.generated.h"

class UInstancedStaticMeshComponent;
class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class AMD_WallPiece;
class AMD_CuttableWall;

/**
 * 서버에서 값이 달라진 오염 셀 하나만 전송하기 위한 Fast Array 항목.
 *
 * 셀 번호는 uint16, 잔여 오염량은 uint8로 보내 항목당 8바이트를 3바이트로 줄인다.
 * 두 값 모두 FMD_ScrapeReplicationCodec을 거쳐야 한다. 특히 잔여 오염량은
 * 0이 "깨끗함" 전용이라 단순 캐스팅으로 만들면 완료 판정이 서버와 갈린다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellState
	: public FFastArraySerializerItem
{
	GENERATED_BODY()

	FMD_ReplicatedScrapeCellState()
		: EncodedCellIndex(FMD_ScrapeReplicationCodec::InvalidCellIndex)
		, QuantizedRemainingContamination(0)
	{
	}

	FMD_ReplicatedScrapeCellState(
		const uint16 InEncodedCellIndex,
		const uint8 InQuantizedRemainingContamination)
		: EncodedCellIndex(InEncodedCellIndex)
		, QuantizedRemainingContamination(InQuantizedRemainingContamination)
	{
	}

	/** 앞뒤 통합 셀 번호. 유효하지 않으면 INDEX_NONE을 반환한다. */
	int32 GetCellIndex() const
	{
		return FMD_ScrapeReplicationCodec::DecodeCellIndex(EncodedCellIndex);
	}

	/** 역양자화한 잔여 오염량(0~1). */
	float GetRemainingContamination() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedRemainingContamination);
	}

	/** 앞뒤 통합 셀 번호. MAX_uint16은 유효하지 않음을 뜻하는 예약값이다. */
	UPROPERTY()
	uint16 EncodedCellIndex;

	/** 0~255로 양자화한 잔여 오염량. 0은 "깨끗함" 전용이다. */
	UPROPERTY()
	uint8 QuantizedRemainingContamination;
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

// 철거 성공(칸막이 절단 또는 지지 상태 지주 절단) 시 한 번 발생
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnWallDemolished, AMD_CuttableWall*, Wall, AActor*, Cutter);

// 스캔 하이라이트 시 발생. 겹친 인프라가 Duration 초 동안 실루엣을 노출하는 데 사용
// Scanner = 스캔한 캐릭터. 실루엣은 스캐너 로컬 클라에서만 떠야 하므로 전달한다
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMD_OnWallScanned, AMD_CuttableWall*, Wall, float, Duration, AActor*, Scanner);

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
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void SetSupported(bool bSupported) { bHasSupport = bSupported; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool IsSupported() const { return bHasSupport; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	EMD_WallType GetWallType() const { return WallType; }

	// 무지지 붕괴 발생 시 방송. 점수 처리는 GameMode가 구독
	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnStrutCollapsed OnStrutCollapsed;

	// 철거 성공 시 방송. 점수 처리는 GameMode가 구독
	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnWallDemolished OnWallDemolished;

	// 스캔 시 방송. 겹친 배관/배선이 실루엣을 켜는 데 사용 (파라미터 = 스캔 지속시간)
	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnWallScanned OnWallScanned;

	// Cutter = 절단 버튼을 누른 캐릭터
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool CanCut() const;
	
	UFUNCTION(BlueprintCallable, Category = "MD|Wall")
	void ScanHighlight(float Duration = 3.0f, AActor* Scanner = nullptr);

	// 충돌점 사이의 오염 셀을 제거
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHits(
		const FHitResult& StartHit,
		const FHitResult& EndHit);

	// 플레이어가 장착한 헤라 성능을 사용해 충돌점 사이의 오염 셀을 제거
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHitsWithToolSpec(
		const FHitResult& StartHit,
		const FHitResult& EndHit,
		const FMD_ScrapeToolSpec& ToolSpec);

	// 로컬 좌표의 긁기 경로를 적용
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStroke(const FMD_ScrapeStroke& Stroke);

	// 플레이어가 장착한 헤라 성능을 사용해 로컬 긁기 경로를 적용
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithToolSpec(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec);

	/** 유효한 벽 충돌점을 표면 로컬 2D 좌표로 변환 */
	bool TryGetScrapeSurfacePositionFromHit(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);

	/** 표면 로컬 2D 좌표를 피드백용 월드 좌표로 변환. 깊이는 메시 Bounds 바깥 평면을 쓴다. */
	bool TryGetScrapeSurfaceWorldPosition(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		FVector& OutWorldPosition) const;

	/**
	 * 표면 로컬 2D 좌표를 참조 월드 위치와 같은 깊이의 월드 좌표로 변환한다.
	 *
	 * 앞면이 여러 깊이에 걸친 벽에서는 Bounds 바깥 평면이 실제 표면보다 한참 앞이다.
	 * 지금 맞고 있는 히트 지점을 참조로 주면 조준 보정이 실제 표면 위에 남는다.
	 */
	bool TryGetScrapeSurfaceWorldPositionAtDepth(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector& ReferenceWorldPosition,
		FVector& OutWorldPosition) const;

	// 앞면과 뒷면 제거 비율의 전체 평균(0~1). 잔해량/점수처럼 양면 전체 상태에 사용
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetContaminationCleanRatio() const;

	/** 지정한 한 면의 오염 제거 비율(0~1) */
	float GetContaminationCleanRatio(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Get Contamination Clean Ratio For Face"))
	float GetContaminationCleanRatioForFace(EMD_ScrapeSurfaceFace Face) const;

	// 앞면과 뒷면 제거율의 전체 평균(0~100)
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetContaminationRemovedPercent() const;

	/** 지정한 한 면의 오염 제거율(0~100) */
	float GetContaminationRemovedPercent(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Get Contamination Removed Percent For Face"))
	float GetContaminationRemovedPercentForFace(EMD_ScrapeSurfaceFace Face) const;

	// 남은 오염 비율(0~1)
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetResidualContaminationRatio() const;

	// 앞면 또는 뒷면 중 한 면 이상이 완전히 깨끗한지 여부
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	bool IsContaminationFullyRemoved() const;

	/** 지정한 한 면의 오염 완전 제거 여부 */
	bool IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Is Contamination Fully Removed For Face"))
	bool IsContaminationFullyRemovedForFace(EMD_ScrapeSurfaceFace Face) const;

	/** 앞면(위)과 뒷면(아래)의 오염 상태가 담긴 런타임 G8 W x 2H 마스크. */
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Mask")
	UTexture2D* GetContaminationMaskTexture() const
	{
		return ContaminationMaskTexture.Get();
	}

	/** 현재 런타임 오염 마스크의 픽셀 크기. 초기화 전에는 (0, 0). */
	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Mask")
	FIntPoint GetContaminationMaskDimensions() const
	{
		return ContaminationMaskAtlasDimensions;
	}

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Decal")
	UDecalComponent* GetFrontContaminationDecal() const
	{
		return FrontContaminationDecal.Get();
	}

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Decal")
	UDecalComponent* GetBackContaminationDecal() const
	{
		return BackContaminationDecal.Get();
	}

	// 최초 완전 제거 시 한 번 방송
	UPROPERTY(BlueprintAssignable, Category = "MD|Wall|Contamination")
	FMD_OnContaminationFullyRemoved OnContaminationFullyRemoved;
	
	// Day 전환 악화: 무작위 위치에 오염 셀을 소량 리필
	void ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells);

	// Day 스냅샷용: 초기값과 다른 셀을 (인덱스, 남은 오염량) 쌍으로 내보낸다
	void ExportContaminationDeltas(TArray<int32>& OutCellIndices, TArray<float>& OutRemainingAmounts) const;

	// Day 스냅샷용: 저장된 셀 상태를 적용하고 시각화·복제를 갱신한다
	void RestoreContaminationCells(const TArray<int32>& CellIndices, const TArray<float>& RemainingAmounts);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 셀 단위 오염막
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Overlay")
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	// 최종 오염 시각화: 앞면과 뒷면에 한 장씩 투영하는 Deferred Decal
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> FrontContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	// 지지대 설치 여부. true면 오절단해도 붕괴하지 않음
	UPROPERTY(EditAnywhere, Replicated, Category = "MD|Wall")
	bool bHasSupport = false;

	// 절단 시 드랍할 조각 수 (기획: 5~10개)
	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	int32 PieceCount = 10;

	/** 절단 시 생성할 벽 조각 Blueprint 클래스. Class Defaults에서 반드시 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	// 스캔 하이라이트 / 원복에 재사용할 다이내믹 머티리얼
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	// 오염 격자 크기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination",
		meta = (UIMin = "1", UIMax = "128"))
	FIntPoint ContaminationGridDimensions;

	// 셀의 초기 오염량(0~1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	/** 켜면 월드 위치 기반 3D 노이즈가 셀별 초기 오염량을 결정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural")
	bool bUseProceduralContamination;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "1.0", Units = "cm"))
	float ContaminationNoiseScaleCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination"))
	int32 ContaminationNoiseSeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationCoverage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationMinimumAmount;

	// 작업면 크기(cm, 0이면 자동)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	/** Auto는 메시 Bounds에서 판재의 두께 축을 찾는다. 오판하면 로컬 X/Y/Z를 직접 지정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Geometry")
	EMD_ScrapeSurfaceNormalAxis ContaminationSurfaceNormalAxis;

	// 표면 판정 설정. 포함된 헤라 수치는 기존 직접 호출의 기본값으로만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination")
	FMD_ScrapeSettings ScrapeSettings;

	// 벽면과 오염막 사이 거리(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Overlay",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationOverlayOffsetCm;

	// 오염막 두께(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Overlay",
		meta = (ClampMin = "0.01", Units = "cm"))
	float ContaminationOverlayThicknessCm;

	// 오염막 표시 여부
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Overlay")
	bool bShowContaminationOverlay;

	/** Deferred Decal Domain이며 MaskTexture/MaskVScale/MaskVOffset/MaskUFlip/MaskHalfTexelU/V 파라미터를 제공한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	bool bShowContaminationDecals;

	// 한 면이 벽 안쪽으로 투영하는 최소 깊이. 런타임에 벽 전체 두께의 절반 이하로 제한한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.01", UIMin = "0.1", Units = "cm"))
	float ContaminationDecalProjectionDepthCm;

	/**
	 * 켜면 메시 정점에서 실제 앞/뒤 표면 깊이를 측정해 투영 깊이를 자동으로 넓힌다.
	 * 앞면이 여러 깊이에 걸친 벽 메시도 오염이 찍히게 하기 위한 설정이다.
	 * 반대 면 표면을 침범하지 않는 선까지만 넓히며, 측정에 실패하면 위 수동 깊이를 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	bool bAutoFitContaminationDecalDepth;

	/** 자동 맞춤이 실제 표면 뒤로 더 확보하는 여유. 반대 면과의 안전 간격으로도 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal",
		meta = (EditCondition = "bAutoFitContaminationDecalDepth",
			ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalDepthFitMarginCm;

	// Z-fighting 방지를 위해 Decal 중심을 벽 표면 바깥으로 조금 이동하는 거리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalSurfaceOffsetCm;

	/**
	 * 투영 상자의 옆면을 표면 테두리에서 안쪽으로 물리는 폭.
	 *
	 * 0으로 두면 상자 옆면이 벽 실루엣과 정확히 겹쳐, 벽의 두께 면이나 맞닿은
	 * 바닥처럼 투영축과 거의 평행한 면이 상자 경계에 걸린다. 그런 면에서는
	 * 텍셀이 길게 늘어나고, 카메라가 멈춰 있어도 시간적 AA의 서브픽셀 흔들림만으로
	 * 상자 안팎 판정이 매 프레임 뒤집혀 모서리가 지지직거린다.
	 *
	 * 이 폭만큼은 오염이 그려지지 않는다. 벽 테두리는 어차피 작업면 판정
	 * (MinimumContaminationFaceNormalDot)에 걸려 긁을 수 없는 영역이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalEdgeInsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Wall|Contamination|Decal")
	int32 ContaminationDecalSortOrder;

	// 절단 허용 게이트. 회의 결정으로 기본 0(잠금 없음) — 정책이 바뀌면 에디터에서 수치만 올리면 됨
	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float PartitionCutGate = 0.0f;

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float StrutCutGate = 0.0f;

private:
	friend struct FMD_ReplicatedScrapeCellStateArray;
	friend class FMD_CuttableWallScrapeIntegrationTest;
	friend class FMD_CuttableWallArbitraryMeshTest;

	// 격자 한 축의 최대 셀 수
	static constexpr int32 MaximumContaminationGridAxis = 256;

	// 작업면 판정 최소 내적값
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;

	// 셀별 오염 상태
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;

	// 앞면은 위쪽, 뒷면은 아래쪽에 저장하는 런타임 G8 W x 2H 마스크
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ContaminationMaskTexture;

	// 런타임 마스크와 동일한 행 우선 CPU G8 버퍼
	TArray<uint8> ContaminationMaskAtlasPixels;

	// 현재 런타임 마스크의 W x 2H 크기
	FIntPoint ContaminationMaskAtlasDimensions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrontContaminationDecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackContaminationDecalMID;

	bool bContaminationDecalsConfigured;

	/** 서버에서 변경된 셀만 모든 클라이언트와 늦은 접속자에게 전송한다. */
	UPROPERTY(Replicated, Transient)
	FMD_ReplicatedScrapeCellStateArray ReplicatedContaminationState;

	// 계산된 작업면 크기(cm)
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	FVector2D ResolvedContaminationSurfaceSizeCm;

	/** Auto 또는 수동 설정에서 최종 결정된 메시 로컬 앞/뒤 축. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	EMD_ScrapeSurfaceNormalAxis ResolvedContaminationSurfaceNormalAxis;

	/** 자동 맞춤까지 반영해 앞면 Decal이 실제로 벽 안쪽을 덮는 깊이(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedFrontDecalDepthCm;

	/** 자동 맞춤까지 반영해 뒷면 Decal이 실제로 벽 안쪽을 덮는 깊이(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedBackDecalDepthCm;

	/** 표면 크기 제한까지 적용해 실제로 테두리에서 줄인 폭(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedContaminationDecalEdgeInsetCm;

	// 작업면 로컬 중심
	FVector ContaminationSurfaceCenterLocal;

	// 오염 메시 로컬 Bounds
	FVector ContaminationMeshLocalBoundsMinimum;
	FVector ContaminationMeshLocalBoundsMaximum;

	// 앞면 제거 비율 캐시
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float CachedFrontContaminationCleanRatio;

	// 뒷면 제거 비율 캐시
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Wall|Contamination|Runtime", meta = (AllowPrivateAccess = "true"))
	float CachedBackContaminationCleanRatio;

	// 오염 표면 초기화 여부
	bool bContaminationSurfaceInitialized;

	// 완료 이벤트 중복 방지
	bool bContaminationCompletionBroadcast;

	bool bCut = false;

	// 셀 인스턴스 기준 Transform
	TArray<FTransform> ContaminationCellBaseTransforms;

	/** 통합 셀 배열에서 지정한 면의 연속 구간만 반환한다. */
	TArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face);
	TConstArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face) const;

	/** 앞면과 뒷면 제거 비율 캐시를 현재 셀 상태에서 함께 갱신한다. */
	void RefreshContaminationCleanRatioCache();

	/**
	 * 지정한 한 면의 제거 비율 캐시만 갱신한다.
	 *
	 * 제거 비율 계산은 면 전체 셀을 훑는다. 스트로크는 한 면만 건드리므로
	 * 반대 면까지 다시 계산할 이유가 없다.
	 */
	void RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace Face);

	/** 철거 준비도 표시용: 두 면 중 더 높은 제거 비율을 반환한다. */
	float GetContaminationDemolitionReadyRatio() const;

	// 작업면 위치와 크기 계산
	bool ResolveContaminationSurfaceGeometry();

	// 오염 셀 초기화
	void InitializeContaminationSurface();
	bool TryApplyProceduralInitialContamination(
		int32 CellsPerFace,
		float BaseInitialContamination);

	// 전체 셀 상태에서 Transient G8 마스크를 생성
	bool InitializeContaminationMask();

	// 런타임 마스크와 CPU 버퍼를 안전하게 정리
	void ReleaseContaminationMask();

	// 실제로 값이 달라진 셀만 CPU 버퍼와 GPU 마스크에 반영
	void UpdateContaminationMask(
		const TArray<FMD_ScrapeCellChange>& CellChanges);

	// 변경 픽셀을 감싸는 사각형 하나만 GPU 텍스처에 업로드
	// (Min은 포함, Max는 제외하는 반열린 구간)
	bool UploadContaminationMaskRegion(const FIntRect& AtlasRegion);

	/** 메시 형상과 벽 두께 배분까지 반영해 앞/뒤 안쪽 투영 깊이를 함께 계산한다. */
	void ResolveContaminationDecalDepths();

	// 앞/뒤 Decal의 위치, 크기, MID와 마스크 파라미터를 함께 구성
	bool ConfigureContaminationDecals();
	bool ConfigureContaminationDecalForFace(
		UDecalComponent* DecalComponent,
		EMD_ScrapeSurfaceFace Face,
		TObjectPtr<UMaterialInstanceDynamic>& InOutMID);
	void SetContaminationDecalsVisible(bool bVisible);
	void RefreshContaminationDecalMaterialParameters();

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

	// 충돌점이 앞면 또는 뒷면 오염 작업면인지 확인하고 해당 면을 반환
	bool TryGetContaminationFaceFromHit(
		const FHitResult& Hit,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;

	// 하이라이트 원복
	void RestoreScanColor();

	FTimerHandle ScanTimerHandle;
};
