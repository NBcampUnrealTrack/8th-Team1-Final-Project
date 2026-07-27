#pragma once

#include "CoreMinimal.h"
#include "Demolition/Scrape/MD_ScrapeTypes.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "MD_CuttableWall.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class AMD_WallPiece;
class AMD_CuttableWall;

/**
 * 오염막이 처음으로 완전히 제거된 순간 한 번만 방송되는 이벤트.
 *
 * 절단 Ability나 미션 시스템은 벽의 셀 배열을 직접 수정하지 않고 이 이벤트 또는
 * CanCut()을 통해 해금 상태를 확인한다. 이렇게 해야 오염도 상태의 소유자가 벽 하나로 유지된다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnContaminationFullyRemoved, AMD_CuttableWall*,	Wall);

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

	// Cutter = 절단 버튼을 누른 캐릭터
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintPure, Category = "Wall")
	bool CanCut() const;
	
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ScanHighlight(float Duration = 3.0f);

	/**
	 * 월드 충돌점 두 개를 벽면의 로컬 2D 좌표로 투영한 뒤 오염 격자에 적용한다.
	 *
	 * StartHit과 EndHit은 모두 이 벽의 Mesh 및 로컬 +Y 방향 작업면에 닿아야 한다.
	 * Gameplay Ability는 오염 셀을 직접 알 필요 없이 연속된 두 FHitResult만 전달하면 된다.
	 * 잘못된 벽, 반대쪽 면, 유효하지 않은 충돌점이면 기본 FMD_ScrapeResult를 반환하며
	 * 오염 상태를 변경하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHits(
		const FHitResult& StartHit,
		const FHitResult& EndHit);

	/**
	 * 이미 벽면 로컬 2D 좌표로 변환된 긁기 경로를 오염 격자에 적용한다.
	 *
	 * 자동화 테스트나 벽 내부 코드에서 좌표 변환과 판정 규칙을 따로 확인할 때 사용한다.
	 * 일반 Gameplay Ability에서는 ApplyScrapeStrokeFromHits() 사용을 우선한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStroke(const FMD_ScrapeStroke& Stroke);

	/** 전체 초기 오염량 중 제거된 비율. 내부 규칙과 동일한 0~1 범위를 사용한다. */
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetContaminationCleanRatio() const;

	/** UI와 기존 절단 게이트 표시를 위한 0~100 퍼센트 값이다. 별도 상태를 저장하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetContaminationRemovedPercent() const;

	/**
	 * 점수 감액에 사용할 잔여 오염 비율.
	 * 예: 절반만 긁은 벽은 0.5, 완전히 긁은 벽은 0을 반환한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	float GetResidualContaminationRatio() const;

	/** 초기 오염이 있던 모든 셀이 CleanTolerance 이하까지 제거됐는지 확인한다. */
	UFUNCTION(BlueprintPure, Category = "Wall|Contamination")
	bool IsContaminationFullyRemoved() const;

	/** 한 벽에서 완료 전환을 여러 번 처리하지 않도록 최초 완료 순간에만 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "Wall|Contamination")
	FMD_OnContaminationFullyRemoved OnContaminationFullyRemoved;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * 프로토타입용 오염막 표현.
	 * 한 셀마다 얇은 인스턴스를 배치하고 남은 오염량에 따라 해당 인스턴스 면적을 줄인다.
	 * 오염 셀의 Static Mesh와 Material은 Blueprint에서 이 컴포넌트에 직접 지정한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual")
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	UPROPERTY(EditAnywhere, Category = "Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	// 임시: 지지대 설치 여부. true면 오절단해도 붕괴하지 않음
	UPROPERTY(EditAnywhere, Category = "Wall")
	bool bHasSupport = false;

	// 절단 시 드랍할 조각 수 (기획: 5~10개)
	UPROPERTY(EditAnywhere, Category = "Wall")
	int32 PieceCount = 10;

	UPROPERTY(EditAnywhere, Category = "Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	// 스캔 하이라이트 / 원복에 재사용할 다이내믹 머티리얼
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/**
	 * 벽면 오염 격자의 가로(X)·세로(Z) 셀 수.
	 *
	 * 프로토타입 기본값 30×25는 생성자에서 설정하며 Blueprint와 인스턴스에서 변경할 수 있다.
	 * 런타임 초기화 시 각 축을 1~256 범위로 한 번 더 보정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (UIMin = "1", UIMax = "128"))
	FIntPoint ContaminationGridDimensions;

	/**
	 * 모든 셀에 공통으로 넣을 시작 오염량.
	 * 0은 처음부터 깨끗한 벽, 1은 전체가 완전히 오염막으로 덮인 벽을 의미한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	/**
	 * 0보다 큰 X/Y가 모두 입력되면 메시의 실제 크기 대신 사용할 작업면 크기(cm).
	 *
	 * 기본값 (0, 0)에서는 Mesh 로컬 X/Z Bounds와 월드 Scale로 실제 크기를 자동 산출한다.
	 * 충돌 메시와 보이는 오염 작업면 크기가 다른 특수 벽에서만 수동 지정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	/** 기본 헤라의 폭, 이동 거리당 제거량, 샘플 간격, 완료 허용 오차 설정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall|Contamination")
	FMD_ScrapeSettings ScrapeSettings;

	/** 보이는 벽면보다 오염막을 바깥쪽으로 띄우는 실제 월드 거리. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationOverlayOffsetCm;

	/** 셀 오염막 인스턴스의 실제 월드 두께. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Visual",
		meta = (ClampMin = "0.01", Units = "cm"))
	float ContaminationOverlayThicknessCm;

	/** false면 판정은 유지하고 프로토타입 셀 오염막 표현만 숨긴다. */
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
	/** 한 벽이 생성할 수 있는 오염 격자 한 축의 안전 상한. */
	static constexpr int32 MaximumContaminationGridAxis = 256;

	/** 충돌 면이 로컬 +Y 작업면으로 인정되는 최소 법선 내적값. */
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;

	/**
	 * 이 벽의 실제 오염 상태 원본.
	 *
	 * 전체 제거율을 별도 편집 값으로 보관하지 않고 각 셀의 Initial/Remaining 값에서 계산한다.
	 * 따라서 헤라가 지나간 셀만 감소하며, 50%만 제거했다면 잔여 감점도 정확히 50%가 된다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "Wall|Contamination|Runtime",
		meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;

	/** 메시 Bounds 또는 수동 Override로 확정한 실제 작업면 가로(X)·세로(Z) 크기(cm). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "Wall|Contamination|Runtime",
		meta = (AllowPrivateAccess = "true"))
	FVector2D ResolvedContaminationSurfaceSizeCm;

	/**
	 * 오염 작업면 중앙의 Mesh 로컬 위치.
	 * 피벗이 메시 중앙에 있지 않아도 월드 충돌점을 올바르게 중앙 기준 2D 좌표로 변환하기 위해 저장한다.
	 */
	FVector ContaminationSurfaceCenterLocal;

	/** 오염막 인스턴스의 앞면 위치와 로컬 범위를 계산하기 위한 원본 Mesh Bounds. */
	FVector ContaminationMeshLocalBoundsMinimum;
	FVector ContaminationMeshLocalBoundsMaximum;

	/**
	 * 매 조회마다 전체 셀을 순회하지 않기 위한 제거율 캐시.
	 * 편집 가능한 원본이 아니며 초기화 및 ApplyLocalScrapeStroke() 직후에만 셀 상태에서 갱신한다.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "Wall|Contamination|Runtime",
		meta = (AllowPrivateAccess = "true"))
	float CachedContaminationCleanRatio;

	/** 유효한 표면 크기와 셀 배열이 준비된 경우에만 true다. 잘못된 설정에서는 절단을 실패 처리한다. */
	bool bContaminationSurfaceInitialized;

	/** 완료 이벤트가 이미 방송됐는지 기록하여 100% 상태에서 반복 긁어도 중복 해금되지 않게 한다. */
	bool bContaminationCompletionBroadcast;

	bool bCut = false;

	/** 셀마다 완전히 오염된 상태의 로컬 Transform. InstanceIndex와 CellIndex가 일치한다. */
	TArray<FTransform> ContaminationCellBaseTransforms;

	/** 메시 Bounds와 Scale을 이용해 작업면 중앙 및 실제 X/Z 크기를 다시 계산한다. */
	bool ResolveContaminationSurfaceGeometry();

	/** 런타임 시작 시 격자 크기를 검증하고 각 셀을 동일한 초기 오염량으로 채운다. */
	void InitializeContaminationSurface();

	/** 초기 셀 상태로 프로토타입용 인스턴스 오염막을 생성한다. */
	void InitializeContaminationVisualization();

	/** 실제 값이 바뀐 셀 인스턴스만 남은 오염량에 맞게 갱신한다. */
	void UpdateContaminationVisualization(
		const TArray<FMD_ScrapeCellChange>& CellChanges);

	/** 한 셀의 남은 오염 비율을 인스턴스 면적에 반영한 Transform을 만든다. */
	FTransform MakeContaminationCellVisualTransform(int32 CellIndex) const;

	/** 월드 위치를 작업면 중앙 기준 X/Z 좌표(cm)로 투영한다. */
	bool TryProjectWorldPointToContaminationSurface(
		const FVector& WorldPoint,
		FVector2D& OutSurfacePositionCm) const;

	/**
	 * 충돌점이 이 벽의 Mesh와 로컬 +Y 작업면에 속하는지 검사한다.
	 * 반대편(-Y)에서 긁은 경로가 앞면의 같은 셀을 지우는 거울 현상을 차단한다.
	 */
	bool IsHitOnContaminationFace(const FHitResult& Hit) const;

	// 하이라이트 원복
	void RestoreScanColor();

	FTimerHandle ScanTimerHandle;
};
