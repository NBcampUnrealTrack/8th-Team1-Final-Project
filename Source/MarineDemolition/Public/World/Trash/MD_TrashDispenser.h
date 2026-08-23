#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_TrashDispenser.generated.h"

class APawn;
class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class AMD_TrashBin;

/** 보급 요청의 서버 판정 결과. Blueprint 연출이 실패 이유를 구분할 때 사용한다. */
UENUM(BlueprintType)
enum class EMD_TrashDispenseResult : uint8
{
	Success,
	/**
	 * 통이 아닌 물리 오브젝트나 사람이 출구를 막았다.
	 * 통이 막은 경우는 RefreshInteractTraceResponse가 조준을 그 통으로 넘기므로 여기까지 오지 않는다.
	 */
	Blocked,
	NotConfigured,
	SpawnFailed,
	LiveLimitReached
};

/**
 * 기지 1층 폐기물 대기장의 빈 임시 쓰레기통 보급기.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_TrashDispenser : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_TrashDispenser();

	/**
	 * 새 통과 저장에서 복원한 통을 같은 생존 한도·파괴 추적 원장에 등록한다.
	 * 복원 경로는 저장된 SourceDispenserName이 이 액터와 일치할 때만 성공한다.
	 */
	bool RegisterDispensedBin(AMD_TrashBin* Bin);

	/** Implements IMD_Interactable */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	/** ~Implements IMD_Interactable */

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 서버 판정 결과를 각 머신의 같은 보급기 인스턴스에 전달한다.
	 * 연출 전용이라 한 번 유실돼도 게임 상태가 어긋나지 않으므로 Unreliable로 보낸다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDispenseResult(EMD_TrashDispenseResult Result, APawn* RequestingPawn);

	UFUNCTION()
	void HandleClearanceBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleClearanceEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	/**
	 * Blueprint에서 소리·표시등·메시지 같은 연출을 붙이는 지점.
	 * 모든 머신에서 실행되므로 소리·표시등은 그대로 재생하되,
	 * 요청자 화면에만 띄울 실패 메시지는 bIsLocalRequester로 걸러야 옆 사람에게도 뜨지 않는다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash Dispenser")
	void OnDispenseResult(EMD_TrashDispenseResult Result, bool bIsLocalRequester);

	/** 배치 트랜스폼과 시각 요소를 분리하는 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TObjectPtr<USceneComponent> Root;

	/** 보급기 외형. 기존 SM_TrashDispenser는 Blueprint에서 연결한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 메시의 단순 충돌 유무와 관계없이 상호작용 Trace를 받는 영역 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TObjectPtr<UBoxComponent> InteractionVolume;

	/** 쓰레기통이 나올 위치와 앞 방향. Blueprint 뷰포트에서 조정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TObjectPtr<UArrowComponent> SpawnPoint;

	/** 새 통이 차지할 공간. 서버가 이 박스에 막는 물체가 있는지 검사한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TObjectPtr<UBoxComponent> SpawnClearance;

	/** 상호작용 한 번에 서버가 한 개 생성할 빈 임시 쓰레기통 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Dispenser")
	TSubclassOf<AMD_TrashBin> TrashBinClass;

	/** 서버 요청 스팸과 결과 multicast 증폭을 막는 최소 요청 간격 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Dispenser",
		meta = (ClampMin = "0.05", UIMin = "0.1", Units = "s"))
	float InteractionCooldown = 0.25f;

	/** 누적 보급량은 무한이지만 이 보급기가 동시에 유지할 수 있는 통 수는 제한한다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Dispenser",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxLiveDispensedBins = 8;

private:
	UFUNCTION()
	void HandleDispensedBinDestroyed(AActor* DestroyedActor);

	void CompactLiveDispensedBins();

	/**
	 * 새 통을 스폰해도 되는지 서버에서 확인한다.
	 * 월드 overlap 질의로 모든 PhysicsBody와 Pawn을 장애물로 보며, 스폰 성사 여부를 가른다.
	 * ShouldYieldInteractTraceToBin과 판정 기준이 다르므로 한쪽만 고치지 말 것.
	 */
	bool CanSpawnBinHere() const;

	/**
	 * 출구를 막은 통을 대신 조준할 수 있도록 보급기가 조준을 양보해야 하는지 확인한다.
	 * SpawnClearance의 overlap 캐시로 AMD_TrashBin만 보며, 조준 우선순위만 가른다.
	 * CanSpawnBinHere와 판정 기준이 다르므로 한쪽만 고치지 말 것.
	 */
	bool ShouldYieldInteractTraceToBin() const;

	/** 출구를 막은 통을 대신 조준할 수 있도록 보급기의 InteractTrace 응답을 갱신한다. */
	void RefreshInteractTraceResponse();

	/** Blueprint에서 프로파일을 바꿔도 원래 응답으로 되돌리도록 BeginPlay에서 기록한다. */
	ECollisionResponse DefaultMeshInteractResponse = ECR_Block;
	ECollisionResponse DefaultVolumeInteractResponse = ECR_Block;

	/** 현재 적용된 InteractTrace 응답 상태. 같은 값을 반복 적용하지 않기 위한 기준이다. */
	bool bClearanceOccupied = false;

	TArray<TWeakObjectPtr<AMD_TrashBin>> LiveDispensedBins;
	double NextAllowedInteractionTime = 0.0;

};
