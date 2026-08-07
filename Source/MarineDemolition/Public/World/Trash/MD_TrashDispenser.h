#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_TrashDispenser.generated.h"

class UArrowComponent;
class UBoxComponent;
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
	SpawnFailed
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

protected:
	/** 서버 판정 결과를 각 플레이어의 같은 보급기 인스턴스에 전달한다. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDispenseResult(EMD_TrashDispenseResult Result);

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

private:
	/** PhysicsActor 충돌 규칙을 기준으로 출구가 비어 있는지 서버에서 확인한다. */
	bool IsSpawnAreaClear() const;
};
