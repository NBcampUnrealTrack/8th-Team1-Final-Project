#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "MD_Barnacle.generated.h"

class AMD_BarnacleItem;
class AMD_BarnacleSpawner;
class UMD_ScrapeSurfaceComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;


UCLASS()
class MARINEDEMOLITION_API AMD_Barnacle : public AActor
{
	GENERATED_BODY()

public:
	AMD_Barnacle();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 검증된 헤라 클릭의 피해만큼 내구도를 깎음
	 *
	 * 양수 피해가 적용될 때마다 기존 메시 흔들림을 재생한다. 누른 채 움직이는 연속
	 * 스트로크에서는 이 함수를 호출하지 않아 한 클릭에 한 번만 감소한다.
	 *
	 * @param DurabilityDamage      이번 클릭이 감소시킬 내구도
	 * @param WobbleWorldDirection  클릭 피드백으로 메시가 기울 월드 방향
	 * @return 이번 호출로 따개비가 떨어져 나갔으면 true
	 */
	bool ApplyScrapeClickDamage(float DurabilityDamage,	const FVector& WobbleWorldDirection);

	/**
	 * 이 따개비가 벽면에서 헤라를 막는 반경(cm).
	 *
	 * 메시 Bounds를 부착면에 투영한 크기에서 구한다. 스케일 편차를 주고 붙이므로
	 * 고정값으로 둘 수 없다.
	 */
	float GetScrapeBlockRadiusCm() const;

	/**
	 * 배치기가 붙일 때 신원과 최대 내구도를 함께 초기화한다. 서버 전용.
	 *
	 * Day가 바뀌면 레벨이 다시 로드되고 배치기가 같은 시드로 같은 자리에 다시 붙인다.
	 * 어제 떼어낸 것이 되살아나지 않으려면 "몇 번째였는지"를 스냅샷에 남겨야 한다.
	 * 런타임 스폰 액터라 FName이 세션마다 달라져서 이름으로는 식별할 수 없다.
	 * FinishSpawning 전에 호출해 BeginPlay와 최초 복제가 최종 내구도를 보게 한다.
	 */
	void InitializeFromSpawner(
		AMD_BarnacleSpawner* InSpawner,
		int32 InSpawnIndex,
		int32 InMaximumDurability);

	/** 배치기가 부여한 순번. 배치기가 붙인 것이 아니면 INDEX_NONE */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	int32 GetSpawnIndex() const { return SpawnIndex; }

	/** 이 개체가 완전히 붙어 있을 때의 최대 내구도 */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetMaximumDurability() const;

	/** 현재 남아 있는 내구도. 기존 연속 작업량과 호환하기 위해 float로 반환한다 */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetCurrentDurability() const;

	/** 0은 손대지 않은 상태, 1은 완전히 떨어진 상태 */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetRemovalProgress() const;

	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	bool IsFullyRemoved() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	/**
	 * 기획 02 §1은 따개비를 내력·칸막이 세그먼트에 두지 말라고 한다.
	 * 눈으로는 잡기 어려운 배치 실수라 에디터에서 경고한다.
	 */
	virtual void CheckForErrors() override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Barnacle")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * 떨어질 때 생성할 따개비 산출물. 지정하지 않으면 사라지기만 하고
	 * 주울 것이 남지 않아 점수를 얻을 수 없다 (기획 05 따개비x32).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Barnacle")
	TSubclassOf<AMD_BarnacleItem> BarnacleItemClass;

	/**
	 * 떼어내는 데 필요한 최대 내구도.
	 *
	 * 이름과 float 타입은 기존 Blueprint 직렬화 호환을 위해 유지한다. 스포너가 생성한
	 * 개체에는 정수 Min-Max에서 뽑은 값이 들어가며, 손으로 배치한 개체는 이 값을 쓴다.
	 * 서버가 개체별 값을 정하므로 클라이언트의 진행도 계산을 위해 최초 복제한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		ReplicatedUsing = OnRep_RemovalWorkRequired,
		Category = "MD|Barnacle|Durability",
		meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "Maximum Durability"))
	float RemovalWorkRequired = 1.0f;

	/** 떨어진 산출물이 메시 안쪽에서 스폰돼 튀지 않도록 위로 밀어내는 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ItemSpawnOffsetCm = 8.0f;

	/**
	 * 벗겨지는 정도를 받을 머티리얼 스칼라 파라미터 이름.
	 *
	 * 코드는 0~1 숫자 하나만 넣는다. 메시를 깎아 보이게 할지 조각이 떨어져 나가게 할지는
	 * 머티리얼이 정한다. 데칼을 못 쓰는 입체 물체라 이 방식으로 갈랐다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle")
	FName RemovalProgressParameterName = TEXT("RemovalProgress");

	/**
	 * 기존 속도 타격 방식의 직렬화 호환용 값. 클릭 방식에서는 사용하지 않는다.
	 */
	UPROPERTY(meta = (DeprecatedProperty,
		DeprecationMessage = "Barnacle damage is now gated by one validated click."))
	float MinimumImpactIntervalSeconds = 0.2f;

	/** 한 번 맞고 흔들리는 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.0", Units = "s"))
	float ImpactWobbleDurationSeconds = 0.35f;

	/** 흔들림의 초당 진동 횟수 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.1"))
	float ImpactWobbleFrequency = 11.0f;

	/** 흔들림이 기울어지는 최대 각도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.0", Units = "deg"))
	float ImpactWobbleMaximumTiltDegrees = 9.0f;

	/**
	 * 헤라를 막는 반경에 곱하는 배율.
	 *
	 * 1.0이면 메시가 부착면에 드리우는 크기 그대로 막는다. 눈에 보이는 것보다
	 * 조금 넉넉하게 막고 싶으면 올린다. 0 이하로 두면 막지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",
		meta = (ClampMin = "0.0"))
	float ScrapeBlockRadiusScale = 1.0f;

	/** 지금까지 누적된 내구도 피해. 이름은 기존 직렬화 호환을 위해 유지한다 */
	UPROPERTY(ReplicatedUsing = OnRep_AccumulatedRemovalWork, VisibleInstanceOnly,
		BlueprintReadOnly, Category = "MD|Barnacle")
	float AccumulatedRemovalWork = 0.0f;

private:
	// 자동화 테스트가 배치 프로퍼티를 직접 세팅한다. 지우면 테스트가 컴파일되지 않는다
	friend class FMD_BarnacleRemovalTest;
	friend class FMD_ScrapeLoadTest;
	friend class AMD_BarnacleSpawner;
	friend class UMD_ScrapeToolComponent;

	UFUNCTION()
	void OnRep_AccumulatedRemovalWork();

	UFUNCTION()
	void OnRep_RemovalWorkRequired();

	/**
	 * 맞은 순간을 모든 클라이언트에서 재생한다.
	 *
	 * 흔들림은 연출이라 신뢰성 있게 보낼 이유가 없다. 한 번 빠져도 다음 타격이
	 * 곧 오고, 진행도 자체는 AccumulatedRemovalWork 복제가 따로 맞춘다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayImpactWobble(FVector_NetQuantizeNormal ImpactWorldDirection, float InWobbleScale);

	/** 흔들림을 시작한다. 서버·클라이언트가 같은 경로를 탄다 */
	void StartImpactWobble(const FVector& ImpactWorldDirection, float InWobbleScale);

	/** 진행도를 머티리얼에 반영. 서버·클라이언트가 같은 경로를 탄다 */
	void ApplyRemovalProgressVisual();

	/**
	 * 붙어 있는 오염 표면을 찾아 헤라를 막는 장애물로 등록한다.
	 * 표면 컴포넌트가 없는 환경 메시에서는 아무것도 등록하지 않는다.
	 */
	bool RegisterWithScrapeSurface(
		UMD_ScrapeSurfaceComponent* PreferredSurface = nullptr);
	void UnregisterFromScrapeSurface();

	/** 등록해 둔 표면. 떨어질 때 장애물 등록을 지우려고 들고 있다 */
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> RegisteredScrapeSurface;

	/** 이 따개비를 붙인 배치기. 떨어질 때 순번을 돌려주려고 들고 있다 */
	TWeakObjectPtr<AMD_BarnacleSpawner> OwningSpawner;

	/** 배치기가 부여한 순번. 손으로 배치한 것은 INDEX_NONE */
	int32 SpawnIndex = INDEX_NONE;

	/** 흔들림 시작 이후 지난 시간. 음수면 흔들리고 있지 않다 */
	float WobbleElapsedSeconds = -1.0f;

	/** 이번 흔들림의 세기(0~1)와 회전축. 축은 액터 로컬이라 벽이 움직여도 따라간다 */
	float WobbleScale = 0.0f;
	FVector WobbleLocalAxis = FVector::ZeroVector;

	/** 흔들리기 전 메시의 부착 회전. 흔들림이 끝나면 여기로 정확히 돌아간다 */
	FRotator MeshRestRelativeRotation = FRotator::ZeroRotator;

	/** 산출물을 떨어뜨리고 자신을 파괴한다. 서버 전용 */
	void BreakOff();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MeshMID;
};
