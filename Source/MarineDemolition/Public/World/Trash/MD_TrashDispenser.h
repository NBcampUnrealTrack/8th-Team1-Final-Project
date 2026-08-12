#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_TrashDispenser.generated.h"

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

	/** Implements IMD_Interactable */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual bool RequiresServerInteraction_Implementation() const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	/** ~Implements IMD_Interactable */

	int32 GetLiveDispensedBinCount() const { return LiveDispensedBins.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 서버 판정 결과를 각 플레이어의 같은 보급기 인스턴스에 전달한다. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDispenseResult(EMD_TrashDispenseResult Result);

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

	/** Blueprint에서 소리·표시등·메시지 같은 연출을 붙이는 지점 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash Dispenser")
	void OnDispenseResult(EMD_TrashDispenseResult Result);

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

	/** 서버 요청 스팸과 Reliable 결과 multicast 증폭을 막는 최소 요청 간격 */
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
	bool RegisterTrackedBin(AMD_TrashBin* Bin);

	/** PhysicsActor 충돌 규칙을 기준으로 출구가 비어 있는지 서버에서 확인한다. */
	bool IsSpawnAreaClear() const;

	/** SpawnClearance 안에 아직 치우지 않은 쓰레기통이 있는지 확인한다. */
	bool HasTrashBinInClearance() const;

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
