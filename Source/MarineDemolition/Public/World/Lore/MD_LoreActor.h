#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_LoreActor.generated.h"

class UMD_LoreSubsystem;
class UMD_LoreProfileSubsystem;
class USphereComponent;

// 개인 월드에 배치되어 조사한 사용자에게만 사라지는 기록물 액터
UCLASS()
class MARINEDEMOLITION_API AMD_LoreActor : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	// 다른 참가자와 수집 상태를 공유하지 않는 기록물 생성자
	AMD_LoreActor();

	virtual bool RequiresServerInteraction_Implementation() const override;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	virtual void Interact_Implementation(AActor* Interactor) override;

	/** 현재 조준 중인 기록물 메시를 하이라이트하는 표시 함수 */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** 할당된 로어 정의 에셋의 기록물 이름을 상호작용 HUD에 제공한다. */
	virtual FText GetInteractionDisplayName_Implementation() const override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** 메시의 단순 충돌 유무와 관계없이 상호작용 트레이스 대상을 제공하는 전용 충돌체 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Lore")
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 충돌 판정과 독립적으로 기록물 외형과 하이라이트를 제공하는 메시 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Lore")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 작은 기록물 메시도 안정적으로 조준할 수 있도록 전용 충돌체에 적용하는 반지름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore|Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float InteractionRadius = 35.0f;

	/** 배치 액터에 할당된 독립 로어 정의 에셋의 Primary Asset ID */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "MD|Lore", meta = (AllowedTypes = "Quest"))
	FPrimaryAssetId LoreAssetId;

	/** 직접 호출된 상호작용도 원거리 수집으로 악용되지 않도록 제한하는 서버 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore|Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxCollectionDistance = 250.0f;

private:
	// 개인 원장 이벤트 구독 해제를 위한 핸들
	FDelegateHandle LoreProgressChangedHandle;

	// 로그인과 개인 저장 복원을 기다리는 초기화 재시도
	FTimerHandle ProfileReadyTimer;

	// 현재 기록물을 소유한 로컬 사용자의 개인 원장
	TWeakObjectPtr<UMD_LoreProfileSubsystem> LocalProfile;

public:
	/** 레벨 배치 검증과 UI에서 사용할 로어 Asset ID 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	FORCEINLINE FPrimaryAssetId GetLoreAssetId() const { return LoreAssetId; }

private:
	// 로컬 플레이어의 개인 보유 상태와 기록물 거리 검사
	bool CanCollectLore(APawn* InteractorPawn) const;

	/** 빈 Asset ID와 잘못된 정의 에셋 참조를 차단하는 배치 설정 검사 */
	bool IsLoreConfigurationValid() const;

	// 현재 게임 인스턴스의 개인 로어 접근 경로 조회
	UMD_LoreSubsystem* GetLoreSubsystem() const;

	// 저장 복원과 본인 수집에 따른 로컬 기록물 제거
	void RefreshCollectionState();
};
