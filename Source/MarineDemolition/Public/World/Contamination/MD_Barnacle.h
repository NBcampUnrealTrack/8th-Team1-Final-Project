#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "MD_Barnacle.generated.h"

class AMD_BarnacleItem;
class AMD_BarnacleSpawner;
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
	 * @param DurabilityDamage 이번 클릭이 감소시킬 내구도
	 */
	bool ApplyScrapeClickDamage(float DurabilityDamage);

	/**
	 * 서버의 deferred spawn 구간에서 스포너 id와 최대 내구도를 한 번만 초기화
	 * @return 유효한 최초 초기화가 적용됐으면 true
	 */
	bool InitializeFromSpawner(
		AMD_BarnacleSpawner* InSpawner,
		int32 InSpawnIndex,
		int32 InMaximumDurability);

	/** 스포너가 부여한 순번. 스포너가 붙인 것이 아니면 INDEX_NONE */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	int32 GetSpawnIndex() const { return SpawnIndex; }

	/** 이 개체가 완전히 붙어 있을 때의 최대 내구도 */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetMaximumDurability() const;

	/** 현재 남아 있는 내구도 반환*/
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetCurrentDurability() const;

	/** 따개비 제거 진행도 반환 */
	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	float GetRemovalProgress() const;

	UFUNCTION(BlueprintPure, Category = "MD|Barnacle")
	bool IsFullyRemoved() const;

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Barnacle")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 떨어질 때 생성할 따개비 산출물 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Barnacle")
	TSubclassOf<AMD_BarnacleItem> BarnacleItemClass;

	/** 떼어내는 데 필요한 최대 내구도.	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_RemovalWorkRequired, Category = "MD|Barnacle|Durability",
		meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "Maximum Durability"))
	float RemovalWorkRequired = 1.0f;

	/** 떨어진 산출물이 메시 안쪽에서 스폰돼 튀지 않도록 위로 밀어내는 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle", meta = (ClampMin = "0.0", Units = "cm"))
	float ItemSpawnOffsetCm = 8.0f;

	/** 한 번 맞고 흔들리는 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.0", Units = "s"))
	float ImpactWobbleDurationSeconds = 0.35f;

	/** 흔들림의 초당 진동 횟수 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.1"))
	float ImpactWobbleFrequency = 11.0f;

	/** 흔들림이 기울어지는 최대 각도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Barnacle|Impact",	meta = (ClampMin = "0.0", Units = "deg"))
	float ImpactWobbleMaximumTiltDegrees = 9.0f;

	/** 지금까지 누적된 내구도 피해 */
	UPROPERTY(ReplicatedUsing = OnRep_AccumulatedRemovalWork, VisibleInstanceOnly, BlueprintReadOnly, Category = "MD|Barnacle")
	float AccumulatedRemovalWork = 0.0f;

private:
	// 자동화 테스트가 배치 프로퍼티를 직접 세팅한다. 지우면 테스트가 컴파일되지 않는다
	friend class FMD_BarnacleRemovalTest;
	friend class FMD_ScrapeLoadTest;

	UFUNCTION()
	void OnRep_AccumulatedRemovalWork();

	UFUNCTION()
	void OnRep_RemovalWorkRequired();

	/** 히트 되는 순간을 모든 클라이언트에서 재생 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayImpactWobble();

	/** 고정 로컬 축과 에디터 설정값으로 흔들림을 시작한다 */
	void StartImpactWobble();

	/** 진행도를 머티리얼에 반영 */
	void ApplyRemovalProgressVisual();

	/** 이 따개비를 붙인 스포너. 떨어질 때 순번을 돌려주려고 들고 있다 */
	TWeakObjectPtr<AMD_BarnacleSpawner> OwningSpawner;

	/** 스포너가 부여한 순번. 손으로 배치한 것은 INDEX_NONE */
	int32 SpawnIndex = INDEX_NONE;

	/** deferred spawn 초기화가 이미 끝났는지 여부 */
	bool bInitializedFromSpawner = false;

	/** 흔들림 시작 이후 지난 시간. 음수면 흔들리고 있지 않다 */
	float WobbleElapsedSeconds = -1.0f;

	/** 흔들리기 전 메시의 부착 회전. 흔들림이 끝나면 여기로 정확히 돌아간다 */
	FRotator MeshRestRelativeRotation = FRotator::ZeroRotator;

	/** 산출물을 떨어뜨리고 자신을 파괴한다. 서버 전용 */
	void BreakOff();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MeshMID;
};
