#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "MD_BarnacleSpawner.generated.h"

class AMD_Barnacle;
class UBoxComponent;

/** 따개비 초기 배치에 사용할 시드의 수명 정책 */
UENUM(BlueprintType)
enum class EMD_BarnaclePlacementSeedMode : uint8
{
	/** 에디터에 입력한 PlacementSeed를 항상 사용한다. */
	Fixed,

	/** 새 게임에서 서버가 한 번 생성하고 세이브와 Day 스냅샷이 보존한다. */
	RandomPerNewGame
};

/**
 * 따개비를 범위 안 표면에 흩뿌리는 배치기 (기획 03 §1.5 폐기물 배치 예산)
 *
 * 기획서는 개수만 정하고 위치는 "보통벽 전역"이라고만 한다
 * (X-7 10개 / TR-9 14개, 05 §3A.1·§3B.1). 하나씩 손으로 붙이는 대신
 * 볼륨을 놓고 개수를 적으면 표면을 찾아 붙인다.
 *
 * 배치는 시드 기반이다. Fixed는 에디터 시드를, RandomPerNewGame은 서버가 새 게임에
 * 한 번 생성한 런타임 시드를 사용한다. 런타임 시드는 저장되므로 같은 세이브를 다시
 * 불러오거나 레벨을 재방문해도 같은 자리에 나온다. 서버에서만 스폰하고 따개비 액터가
 * 복제되므로 클라이언트와 어긋나지 않는다.
 *
 * 볼륨 안에서 BarnacleTrace를 Block하는 벽과 가구만 대상이 될 수 있다. 철거 대상
 * 격벽은 기존 기획 제약을 유지해 기본적으로 제외하고, 허용된 로컬 면만 후보로 사용한다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_BarnacleSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMD_BarnacleSpawner();

	/**
	 * 볼륨 안 표면에 따개비를 붙인다. 서버 전용.
	 * @return 실제로 붙인 개수. 요청 개수보다 적으면 자리를 못 찾은 것이다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Barnacle|Spawner")
	int32 SpawnBarnacles();

	/**
	 * 밤사이 새 자리에 소량 더 붙인다 (기획 03 §3 "따개비 소량 재부착"). 서버 전용.
	 *
	 * 떼어낸 것을 되살리지 않고 새 자리에 돋운다. 어제 한 청소를 무의미하게 만들지
	 * 않으면서 "밤새 조금 나빠진" 연출만 남기기 위해서다 (03:101 강제 초기화 금지).
	 * 살아 있는 개수가 MaximumAliveBarnacleCount를 넘지 않는다.
	 *
	 * @return 실제로 돋운 개수
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Barnacle|Spawner")
	int32 GrowBarnacles(int32 GrowthCount, int32 GrowthSeedOffset);

	/** 따개비가 떨어질 때 스스로 알린다. 서버 전용 */
	void NotifyBarnacleRemoved(int32 RemovedSpawnIndex);

	/** Day 스냅샷이 저장할 목록 */
	const TSet<int32>& GetRemovedSpawnIndices() const { return RemovedSpawnIndices; }

	/** RandomPerNewGame 모드에서 저장할 서버 런타임 시드를 반환한다. */
	bool TryGetRuntimePlacementSeed(int32& OutRuntimePlacementSeed) const;

	/**
	 * 세이브 또는 Day 스냅샷의 시드와 제거 순번을 함께 복원한다. 서버 전용.
	 * BeginPlay에서 임시 배치가 끝난 뒤 호출돼도 저장 시드로 전체 배치를 다시 구성한다.
	 */
	void RestorePersistentState(
		const TArray<int32>& InRemovedSpawnIndices,
		bool bHasSavedRuntimePlacementSeed,
		int32 SavedRuntimePlacementSeed);

	/**
	 * Day 스냅샷에서 복원한다. 서버 전용.
	 * 목록에 든 순번의 따개비를 월드에서 지운다. 배치기가 이미 다 붙인 뒤에 불러야 한다.
	 */
	void RestoreRemovedSpawnIndices(const TArray<int32>& InRemovedSpawnIndices);

protected:
	virtual void BeginPlay() override;

	/** 따개비를 찾을 범위. 섹터 하나를 덮게 잡는다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner")
	TObjectPtr<UBoxComponent> SpawnVolume;

	/** 붙일 따개비 Blueprint. 지정하지 않으면 아무것도 하지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner")
	TSubclassOf<AMD_Barnacle> BarnacleClass;

	/**
	 * 이 볼륨이 붙일 개수.
	 * 기획 05 배치 예산이 맵 전체 개수를 정하므로(X-7 10 / TR-9 14),
	 * 볼륨이 여러 개면 합이 그 수가 되게 나눠 적는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner",
		meta = (ClampMin = "0", ClampMax = "30", UIMax = "30"))
	int32 BarnacleCount = 5;

	/** 초기 배치 시드를 에디터 고정값과 새 게임별 런타임 값 중에서 선택한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Seed")
	EMD_BarnaclePlacementSeedMode PlacementSeedMode =
		EMD_BarnaclePlacementSeedMode::Fixed;

	/** Fixed 모드의 배치 시드. 구버전 세이브의 배치를 복원할 때도 대체값으로 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Seed",
		meta = (EditCondition = "PlacementSeedMode == EMD_BarnaclePlacementSeedMode::Fixed",
			EditConditionHides))
	int32 PlacementSeed = 1337;

	/** 생성되는 따개비 한 개의 최소 내구도. 최대값과 역전되면 실행 시 자동 정렬한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Durability",
		meta = (ClampMin = "1", ClampMax = "1000000", UIMin = "1", UIMax = "10"))
	int32 MinimumBarnacleDurability = 1;

	/** 생성되는 따개비 한 개의 최대 내구도. 범위 양 끝을 포함해 무작위로 정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Durability",
		meta = (ClampMin = "1", ClampMax = "1000000", UIMin = "1", UIMax = "10"))
	int32 MaximumBarnacleDurability = 3;

	/** 볼륨 안 임의 지점에서 표면을 찾을 때 쏘는 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner",
		meta = (ClampMin = "10.0", Units = "cm"))
	float SurfaceSearchDistanceCm = 400.0f;

	/**
	 * 같은 면의 따개비 사이에 반드시 확보할 최소 거리.
	 * 실제 배치는 이 하한만 만족하는 데 그치지 않고 후보 중 가장 먼 자리를 우선한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumSeparationCm = 18.0f;

	/**
	 * 따개비 하나당 미리 수집할 표면 후보 수.
	 * 높일수록 더 멀리 퍼질 가능성이 커지지만 레벨 시작 시 트레이스 수가 늘어난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Distribution",
		meta = (ClampMin = "4", ClampMax = "64"))
	int32 PlacementCandidatesPerBarnacle = 24;

	/** 이전 군집 배치 에셋을 읽기 위한 값. 새 분산 배치에서는 사용하지 않는다. */
	UPROPERTY(meta = (DeprecatedProperty,
		DeprecationMessage = "군집 배치는 폐기됐으며 최대 거리 우선 분산 배치를 사용한다."))
	FIntPoint ClusterSizeRange = FIntPoint(2, 4);

	/** 이전 군집 배치 에셋을 읽기 위한 값. 새 분산 배치에서는 사용하지 않는다. */
	UPROPERTY(meta = (DeprecatedProperty,
		DeprecationMessage = "군집 배치는 폐기됐으며 최대 거리 우선 분산 배치를 사용한다."))
	float ClusterRadiusCm = 70.0f;

	/**
	 * 표면 안쪽으로 파묻는 깊이.
	 *
	 * 히트 지점에 원점을 그대로 두면 피벗 위치에 따라 뜨거나 어중간하게 걸친다.
	 * 밑동을 벽에 묻어야 "붙어 자란" 것으로 읽힌다. 메시 피벗이 밑면이면 0에 가깝게,
	 * 가운데면 메시 두께의 절반쯤 넣는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Attach",
		meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceEmbedCm = 4.0f;

	/**
	 * 법선 축을 중심으로 무작위로 돌린다.
	 * 끄면 전부 같은 방향을 보고 찍어낸 티가 난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Attach")
	bool bRandomizeYawAroundNormal = true;

	/** 개체 크기 편차. X가 최소 배율, Y가 최대 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Attach",
		meta = (ClampMin = "0.05"))
	FVector2D ScaleRange = FVector2D(0.75, 1.35);

	/**
	 * 맞은 컴포넌트에 붙인다.
	 *
	 * 안 붙이면 벽이 움직이거나 사라져도 따개비가 공중에 남는다.
	 * 보통벽은 대개 정적이지만, 부착물이라는 관계를 씬 그래프에 남겨 두는 편이
	 * 나중에 문제를 덜 만든다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Attach")
	bool bAttachToSurface = true;

	/**
	 * SpawnVolume 로컬 +X/-X 방향의 앞면·뒷면을 배치 대상으로 사용한다.
	 * 기존 벽 스포너 동작을 유지하기 위해 기본값은 켜짐이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Surface Faces")
	bool bAllowFrontBackSurfaces = true;

	/**
	 * SpawnVolume 로컬 +Y/-Y 방향의 오른쪽면·왼쪽면을 배치 대상으로 사용한다.
	 * 기존 배치 시드와 면 구성을 유지하기 위해 기본값은 꺼짐이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Surface Faces")
	bool bAllowLeftRightSurfaces = false;

	/**
	 * SpawnVolume 로컬 +Z/-Z 방향의 윗면·아랫면을 배치 대상으로 사용한다.
	 * 가구처럼 수평면에도 붙여야 하는 스포너에서 켠다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner|Surface Faces")
	bool bAllowTopBottomSurfaces = false;

	/** 이전 월드 Z 기반 필터 값을 읽기 위한 프로퍼티. 새 로컬 6면 정책에서는 사용하지 않는다. */
	UPROPERTY(meta = (DeprecatedProperty,
		DeprecationMessage = "앞/뒤, 좌/우 및 위/아래 허용 플래그가 로컬 면 판정을 대신한다."))
	float MaximumSurfaceUpwardDot = 0.5f;

	/**
	 * 켜면 철거 대상 격벽에도 붙는다.
	 * 기획 02 §1이 금지하므로 기본은 꺼짐. 테스트용 탈출구다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner")
	bool bAllowCuttableWallSurfaces = false;

	/** 레벨 시작과 함께 붙일지. 끄면 Blueprint가 SpawnBarnacles를 직접 부른다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner")
	bool bSpawnOnBeginPlay = true;

	/**
	 * 살아 있을 수 있는 최대 개수. 0 이하면 BarnacleCount를 쓴다.
	 *
	 * 밤샘 증식에 상한이 없으면 3일 동안 개수가 계속 늘어 05 배치 예산을 넘긴다
	 * (따개비 개수가 곧 점수다. X-7 10개=320점).
	 * 오염물은 진하기·넓이·확률이 자연 상한이었지만 따개비에는 그런 것이 없다.
	 * 초기 개수로 막아 두면 "청소하면 조금 돋아나되 처음보다 더러워지지는 않는다"가 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Spawner",
		meta = (ClampMin = "0"))
	int32 MaximumAliveBarnacleCount = 0;

private:
	/** 회전·스케일을 반영한 볼륨 안에서 허용 가능한 표면 후보를 충분히 수집한다. */
	void GatherSurfaceCandidates(
		FRandomStream& RandomStream,
		int32 DesiredCandidateCount,
		const TArray<AActor*>& IgnoredActors,
		TArray<FHitResult>& OutHits) const;

	/** 트레이스가 맞은 지점이 실제 SpawnVolume 안에 있는지 확인한다. */
	bool IsPointInsideSpawnVolume(const FVector& WorldLocation) const;

	/** 히트를 "벽에 붙어 자란" 트랜스폼으로 바꾼다 */
	FTransform MakeAttachedTransform(
		FRandomStream& RandomStream,
		const FHitResult& Hit) const;

	/** 붙일 수 있는 액터와 로컬 면 방향인지 확인한다. */
	bool IsAcceptableSurface(const FHitResult& Hit) const;

	/** 잘못 입력된 Min-Max를 1 이상 오름차순 범위로 보정한다. */
	FIntPoint GetSanitizedDurabilityRange() const;

	/** 배치 RNG를 소비하지 않고 시드·순번만으로 개별 최대 내구도를 결정한다. */
	int32 GenerateDurabilityForSpawn(int32 PlacementSeedToUse, int32 SpawnIndex) const;

	/** RandomPerNewGame 모드라면 서버 런타임 시드를 정확히 한 번 준비한다. */
	void InitializeRuntimePlacementSeed();

	/** 현재 모드에서 초기 배치와 증식에 공통으로 사용할 시드를 반환한다. */
	int32 GetEffectivePlacementSeed();

	/** 저장 시드 재배치 전에 현재 런타임 따개비를 아이템 드롭 없이 제거한다. */
	void DestroySpawnedBarnaclesForRebuild();

	/**
	 * 배치 루프 본체. 초기 배치와 밤샘 증식이 공유한다.
	 * @param StartSpawnIndex  이번에 붙일 것들의 첫 순번
	 * @param bSkipRemoved     제거 목록에 든 순번은 자리만 소비하고 붙이지 않는다
	 */
	int32 PlaceBarnacles(
		int32 RequestedCount,
		int32 PlacementSeedToUse,
		int32 StartSpawnIndex,
		bool bSkipRemoved);

	/** 이 배치기가 붙여 아직 살아 있는 개수 */
	int32 CountAliveBarnacles() const;

	/** 상한. MaximumAliveBarnacleCount가 0 이하면 BarnacleCount를 쓴다 */
	int32 GetEffectiveMaximumAliveCount() const;

	/**
	 * 떨어져 나간 순번.
	 * Day가 바뀌어도 같은 시드로 같은 자리에 다시 붙으므로, 이 목록으로 걸러야
	 * 어제 떼어낸 것이 되살아나지 않는다.
	 */
	TSet<int32> RemovedSpawnIndices;

	/** 다음에 부여할 순번. 증식분은 초기 배치 뒤 번호를 이어 쓴다 */
	int32 NextSpawnIndex = 0;

	/** 이 배치기가 붙인 따개비. 상한 계산과 복원에 쓴다 */
	TArray<TWeakObjectPtr<AMD_Barnacle>> SpawnedBarnacles;

	/** RandomPerNewGame 모드에서 서버가 생성하거나 세이브에서 복원한 활성 시드 */
	UPROPERTY(Transient)
	int32 RuntimePlacementSeed = 0;

	/** RuntimePlacementSeed가 현재 새 게임 또는 복원 상태에 맞게 준비됐는지 */
	UPROPERTY(Transient)
	bool bHasRuntimePlacementSeed = false;

	/** BeginPlay 또는 Blueprint가 초기 배치를 이미 요청했는지 */
	UPROPERTY(Transient)
	bool bHasSpawnedInitialPlacement = false;
};
