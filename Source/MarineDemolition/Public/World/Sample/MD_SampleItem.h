#pragma once

#include "CoreMinimal.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "World/Sample/MD_QuestItem.h"
#include "MD_SampleItem.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

/** 파괴 불가 퀘스트 아이템에 등급·손상·충격 파괴 기능을 추가하는 샘플 클래스 */
UCLASS()
class MARINEDEMOLITION_API AMD_SampleItem : public AMD_QuestItem
{
	GENERATED_BODY()

public:
	/** 샘플 전용 파괴 임계값과 피해 허용 정책을 구성하는 생성자 */
	AMD_SampleItem();

	/** 손상 상태를 네트워크로 복제하기 위한 등록 함수 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 샘플 등급을 퀘스트 판정용 게임플레이 태그로 변환하는 함수 */
	virtual FGameplayTag GetQuestGradeTag() const override;

	/** 샘플의 손상 상태를 퀘스트 제출 조건으로 반환하는 함수 */
	virtual bool IsQuestItemIntact() const override;

	/** 샘플이 게임플레이 피해와 폐기 경로에서 파괴될 수 있음을 반환하는 함수 */
	virtual bool CanBeDestroyedByGameplay() const override;

	// 샘플 등급. 배치 시 인스턴스별 지정 (단가 조회 키)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sample")
	EMD_SampleGrade Grade = EMD_SampleGrade::C;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Damaged, Category = "Sample")
	bool bDamaged = false;

	/** 붕괴 피해와 Day 복원에서 공용으로 사용하는 손상 상태 적용 함수 */
	void SetDamaged();

	/** 모든 머신에서 로컬로 파편 연출을 스폰. Destroy 직전에 서버가 호출 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnBreakDebris();
	
	/** 파괴 시 스폰할 파편(지오메트리 컬렉션) 액터. 복제 없는 연출 전용 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample")
	TSubclassOf<AActor> BreakDebrisClass;

	/**
	 * 스폰 직후 이 시간(초) 동안 손상·파괴 판정을 막는다. 퀘스트 스포너가 값을 지정한다.
	 *
	 * 기본값이 0이 아닌 이유: 스포너가 만든 샘플은 공중에서 떨어져 착지하고, Day 스냅샷
	 * 복원으로 되살아난 샘플도 로드 직후 물리가 안정될 때까지 흔들린다. 그 구간의 충격은
	 * 플레이어가 만든 게 아니라서 손 운반 리스크(기획 02 §4)로 치면 안 된다.
	 * 보호가 끝나면 정적·동적 구분 없이 평소 임계값으로 판정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest|Spawn",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Spawn Protection Duration"))
	float SpawnProtectionDuration = 3.f;

	/**
	 * 그랩 시작 직후 이 시간(초) 동안 손상·파괴 판정을 막는다.
	 * PhysicsHandle이 목표 지점(HoldPoint)으로 급가속하는 순간 생기는 오탐 충격을 흡수한다.
	 * 그 외의 충격(운반 중 실제로 세게 부딪힘)은 그대로 판정된다 (기획 02 §4 손 운반 리스크).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GrabProtectionDuration = 0.4f;

	/** MD_InteractionComponent가 그랩 성공 직후 호출한다. */
	virtual void NotifyGrabbed() override;

	/**
	 * 이 시간(초) 동안 손상·파괴 판정을 막는다.
	 *
	 * 파손이 일어나면 안 되는 구역(회수 선반 등)이 호출한다. 선반은 샘플을 그 앞까지
	 * 들고 와야 하는 곳이라 접촉이 불가피한데, 그 접촉으로 깨지면 회수 자체가 불가능해진다.
	 * 구역에 머무는 동안 주기적으로 갱신해서 쓴다.
	 */
	virtual void SuppressImpactDamage(float Duration) override;

protected:
	virtual void BeginPlay() override;

	void BeginSpawnProtection();
	void BeginImpactProtection(float Duration);
	void EndImpactProtection();

	bool bImpactProtected = false;
	FTimerHandle ImpactProtectionTimerHandle;

	UFUNCTION()
	void OnAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType, class AController* InstigatedBy, AActor* DamageCauser);

	/** 큰 물리 충격을 받으면 서버에서 샘플을 파괴한다. */
	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/**
	 * 샘플을 파괴하는 최소 충격 속도(cm/s).
	 * 물리 충돌 impulse를 샘플 질량으로 나눈 값과 비교한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BreakImpactSpeed;
	
	UFUNCTION()
	void OnRep_Damaged();

	// 손상 시각 표현. 서버(리슨 호스트)와 클라이언트 OnRep 공용
	void ApplyDamagedVisual();

	/**
	 * 샘플을 손상시키는 최소 충격 속도(cm/s). BreakImpactSpeed보다 낮게 설정한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample",
			meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DamageImpactSpeed;
};
