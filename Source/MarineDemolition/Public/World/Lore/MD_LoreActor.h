#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_LoreActor.generated.h"

class UMD_LoreSubsystem;
class USphereComponent;

/** 현장에 고정 배치되어 조사 즉시 서버 로어 원장에 등록되는 기록물 액터 */
UCLASS()
class MARINEDEMOLITION_API AMD_LoreActor : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	/** 틱과 이동 복제 없이 서버 파괴 상태만 전달하는 기록물 생성자 */
	AMD_LoreActor();

	/** 기록물 조사를 기존 서버 검증 상호작용 경로로 제한하는 설정 함수 */
	virtual bool RequiresServerInteraction_Implementation() const override;

	/** 검증된 플레이어의 최초 조사 요청을 서버 로어 원장에 전달하는 함수 */
	virtual void Interact_Implementation(AActor* Interactor) override;

	/** 현재 조준 중인 기록물 메시를 하이라이트하는 표시 함수 */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** 할당된 로어 정의 에셋의 기록물 이름을 상호작용 HUD에 제공한다. */
	virtual FText GetInteractionDisplayName_Implementation() const override;

protected:
	/** 서버 원장 변경을 구독하고 이미 발견된 배치물을 제거하는 초기화 함수 */
	virtual void BeginPlay() override;

	/** 파괴 또는 레벨 종료 시 서버 원장 변경 구독을 해제하는 종료 함수 */
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
	/** 서버 원장 이벤트 구독을 액터 수명에 맞춰 해제하기 위한 핸들 */
	FDelegateHandle LoreProgressChangedHandle;

public:
	/** 레벨 배치 검증과 UI에서 사용할 로어 Asset ID 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	FORCEINLINE FPrimaryAssetId GetLoreAssetId() const { return LoreAssetId; }

private:
	/** 서버가 신뢰할 수 있는 플레이어와 기록물 설정 및 거리인지 확인하는 함수 */
	bool CanCollectLore(APawn* InteractorPawn) const;

	/** 빈 Asset ID와 잘못된 정의 에셋 참조를 차단하는 배치 설정 검사 */
	bool IsLoreConfigurationValid() const;

	/** 현재 게임 인스턴스에서 영구 누적 서버 로어 원장을 조회하는 함수 */
	UMD_LoreSubsystem* GetLoreSubsystem() const;

	/** 저장 복원 또는 다른 플레이어 수집 이후 발견된 배치물을 서버에서 제거하는 함수 */
	void RefreshCollectionState();
};
