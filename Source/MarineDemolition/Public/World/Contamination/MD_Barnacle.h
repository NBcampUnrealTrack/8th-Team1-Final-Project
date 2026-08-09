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

/**
 * 보통벽에 붙은 따개비 (기획 02 §1 청소 요소 표)
 *
 * 오염물과 별도 품목이며 보통벽에만 붙는다. 내력·칸막이 세그먼트에는 두지 않는다(02 §1).
 * 여기서 "보통벽"은 게임플레이 액터가 아니라 환경 아트 표면이므로, 이 액터는 무엇에도
 * 붙지 않는 자기 완결적 배치물이다. 레벨에 그냥 놓으면 된다.
 *
 * 오염물과 달리 격자·마스크·데칼을 쓰지 않는다. 내구도 숫자 하나와 입체 메시뿐이다.
 * 셀 격자에 넣으면 개별 내구도·도구 등급·개수 집계를 담을 수 없다.
 *
 * 손맛은 문지르기가 아니라 타격에서 나온다. 따개비는 벽에 붙어 자란 딱딱한
 * 부착물이라 벗겨지는 것이 아니라 톡 하고 떨어진다. 그래서 헤라가 닿는 시간이
 * 아니라 부딪히는 속도가 힘을 정하고, 여러 번 쳐야 떨어진다.
 *
 * 헤라 경로를 물리적으로 막기도 한다. 오염 표면에 붙어 있으면 그 자리를
 * 지나는 스트로크가 따개비에서 멈추므로, 뒤쪽 오염은 따개비를 떼기 전에는 못 지운다.
 * 그 등록은 벽 종류와 무관하게 UMD_ScrapeSurfaceComponent가 관리한다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_Barnacle : public AActor
{
	GENERATED_BODY()

public:
	AMD_Barnacle();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 헤라에 맞은 충격만큼 내구도를 깎는다. 서버 전용.
	 *
	 * 작업량이 0이어도 흔들림은 재생한다. 느리게 밀 때 "닿기는 했는데 힘이 모자란다"를
	 * 화면으로 알려야 플레이어가 더 빠르게 그어야 한다는 것을 안다.
	 *
	 * @param ImpactWork            이번 타격이 깎을 정규화 작업량. 0이면 흔들림만 남는다
	 * @param ImpactWorldDirection  헤라가 밀고 지나간 월드 방향. 흔들리는 방향을 정한다
	 * @return 이번 호출로 따개비가 떨어져 나갔으면 true
	 */
	bool ApplyScrapeImpact(float ImpactWork, const FVector& ImpactWorldDirection);

	/**
	 * 이 따개비가 벽면에서 헤라를 막는 반경(cm).
	 *
	 * 메시 Bounds를 부착면에 투영한 크기에서 구한다. 스케일 편차를 주고 붙이므로
	 * 고정값으로 둘 수 없다.
	 */
	float GetScrapeBlockRadiusCm() const;

	/** 마지막 타격 이후 아직 다음 타격을 받을 수 없는 상태인지. 서버에서만 의미가 있다 */
	bool IsImpactOnCooldown() const;

	/**
	 * 배치기가 붙일 때 신원을 새긴다. 서버 전용.
	 *
	 * Day가 바뀌면 레벨이 다시 로드되고 배치기가 같은 시드로 같은 자리에 다시 붙인다.
	 * 어제 떼어낸 것이 되살아나지 않으려면 "몇 번째였는지"를 스냅샷에 남겨야 한다.
	 * 런타임 스폰 액터라 FName이 세션마다 달라져서 이름으로는 식별할 수 없다.
	 */
	void AssignSpawnerIdentity(AMD_BarnacleSpawner* InSpawner, int32 InSpawnIndex);

	/** 배치기가 부여한 순번. 배치기가 붙인 것이 아니면 INDEX_NONE */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	int32 GetSpawnIndex() const { return SpawnIndex; }

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
	 * 떼어내는 데 필요한 총 작업량. 정규화 값이라 0~1로 둔다.
	 *
	 * 기획서에 절대 수치가 없다. 08 §2는 도구 배율만 준다
	 * (기본 헤라 1.0 / 광폭 x1.6 / 열선 x2.2 — 08 §2.1 IT-H0~H2).
	 * 실제 타격 횟수는 이 값과 도구의 속도당 작업량, 그리고 플레이어가 얼마나 빠르게
	 * 긋는지에 함께 달려 있다. 임시값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle",
		meta = (ClampMin = "0.01", UIMin = "0.1"))
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
	 * 같은 따개비를 다시 때릴 수 있게 되기까지의 시간.
	 *
	 * 없으면 헤라를 대고 밀고 있는 동안 매 프레임 타격이 들어가 한순간에 떨어진다.
	 * 플레이어가 헤라를 빼서 다시 긋는 리듬을 만드는 값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",
		meta = (ClampMin = "0.0", Units = "s"))
	float MinimumImpactIntervalSeconds = 0.2f;

	/** 한 번 맞고 흔들리는 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",
		meta = (ClampMin = "0.0", Units = "s"))
	float ImpactWobbleDurationSeconds = 0.35f;

	/** 흔들림의 초당 진동 횟수 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",
		meta = (ClampMin = "0.1"))
	float ImpactWobbleFrequency = 11.0f;

	/**
	 * 흔들림이 기울어지는 최대 각도.
	 *
	 * 실제 각도는 여기에 타격 세기와 현재 진행도를 함께 곱한 값이다. 거의 다 떨어진
	 * 따개비가 더 크게 흔들려야 "이제 곧 떨어진다"가 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",
		meta = (ClampMin = "0.0", Units = "deg"))
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

	/** 지금까지 누적된 작업량. 복제되어 클라이언트도 같은 진행도를 그린다 */
	UPROPERTY(ReplicatedUsing = OnRep_AccumulatedRemovalWork, VisibleInstanceOnly,
		BlueprintReadOnly, Category = "MD|Barnacle")
	float AccumulatedRemovalWork = 0.0f;

private:
	// 자동화 테스트가 배치 프로퍼티를 직접 세팅한다. 지우면 테스트가 컴파일되지 않는다
	friend class FMD_BarnacleRemovalTest;

	UFUNCTION()
	void OnRep_AccumulatedRemovalWork();

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
	void RegisterWithScrapeSurface();
	void UnregisterFromScrapeSurface();

	/** 등록해 둔 표면. 떨어질 때 장애물 등록을 지우려고 들고 있다 */
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> RegisteredScrapeSurface;

	/** 이 따개비를 붙인 배치기. 떨어질 때 순번을 돌려주려고 들고 있다 */
	TWeakObjectPtr<AMD_BarnacleSpawner> OwningSpawner;

	/** 배치기가 부여한 순번. 손으로 배치한 것은 INDEX_NONE */
	int32 SpawnIndex = INDEX_NONE;

	/** 마지막으로 타격을 받은 서버 시각. 연타 방지에 쓴다 */
	double LastImpactWorldSeconds = TNumericLimits<double>::Lowest();

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
