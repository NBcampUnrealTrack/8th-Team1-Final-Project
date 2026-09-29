#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_QuestItem.generated.h"

class UStaticMeshComponent;

/** 운반·퀘스트 귀속·납품 기능만 제공하고 게임플레이 파괴를 허용하지 않는 기본 퀘스트 아이템 클래스 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_QuestItem : public AActor, public IGameplayTagAssetInterface, public IMD_Interactable
{
	GENERATED_BODY()

public:
	/** 물리 운반과 파괴 불가 정책을 구성하는 생성자 */
	AMD_QuestItem();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 상호작용 시스템에 물리 운반·보관 분류 태그를 제공하는 함수 */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	/** 조준 중인 퀘스트 아이템의 외곽선 표시를 제어하는 함수 */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** 귀속된 퀘스트 정의의 이름을 상호작용 HUD에 제공한다. */
	virtual FText GetInteractionDisplayName_Implementation() const override;

	/** 등급 조건을 적용하지 않는 기본 퀘스트 아이템의 빈 등급 태그 반환 함수 */
	virtual FGameplayTag GetQuestGradeTag() const;

	/** 손상 기능이 없는 기본 퀘스트 아이템의 온전한 상태 반환 함수 */
	virtual bool IsQuestItemIntact() const;

	/** 게임플레이 피해와 폐기 경로에서 파괴를 거부하는 정책 반환 함수 */
	virtual bool CanBeDestroyedByGameplay() const;

	/** 파생 클래스가 그랩 직후 동작을 추가하기 위한 확장 함수 */
	virtual void NotifyGrabbed();
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	/** 파생 클래스가 지정 시간 동안 충격 보호를 추가하기 위한 확장 함수 */
	virtual void SuppressImpactDamage(float Duration);

	/** 아이템을 생성하거나 할당한 메인·반복 퀘스트와 납품 이벤트를 연결하는 식별자 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "MD|Quest")
	FPrimaryAssetId QuestDefinitionId;

protected:
	/** 퀘스트 아이템의 물리 운반과 시각 표현을 담당하는 메시 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Quest|Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 아이템을 집을 때 사용할 물리 운반 방식 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction", meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 보관함이 이 아이템을 식별할 때 사용할 내용물 분류 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource", meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;
};
