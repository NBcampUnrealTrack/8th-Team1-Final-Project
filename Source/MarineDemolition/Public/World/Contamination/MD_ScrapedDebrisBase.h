#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "MD_ScrapedDebrisBase.generated.h"

class UMD_InteractionPromptComponent;

class UStaticMeshComponent;

/**
 * 헤라로 긁어내 월드에 떨어지는 산출물의 공통 뼈대 (기획 02 §1 헤라 핵심 규칙 3)
 *
 * "긁어낸 오염물·따개비는 월드 물리 오브젝트로 떨어진다. 인벤에 들어가지 않는다."
 * 손으로 집거나 임시 쓰레기통에 담아 모선 분류장으로 옮긴다.
 *
 * 오염물과 따개비는 둘 다 오염된 쓰레기 칸(+32)으로 가지만 기획상 별도 품목이다
 * (02 §1 표 "오염물과 별도 품목", §11 분류표 별도 행, §1.5 쓰레기통 수용 목록에도
 * 따로 적혀 있고, 05 배치 예산도 따개비x32와 오염노드x32를 따로 센다).
 * 그래서 한 클래스로 합치지 않고 파생 타입으로 가른다. 개수 집계와 배치 검증이
 * 타입만으로 되어야 한다.
 *
 * 이 클래스는 직접 배치하지 않는다. 파생 클래스가 StorageContentTag를 정한다.
 */
UCLASS(Abstract)
class MARINEDEMOLITION_API AMD_ScrapedDebrisBase
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Interactable
	, public IMD_TrashSortable
{
	GENERATED_BODY()

public:
	AMD_ScrapedDebrisBase();

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** Implements IMD_TrashSortable */
	/** 긁어낸 산출물은 종류를 가리지 않고 오염된 쓰레기다 (기획 02 §11 매핑표) */
	virtual FGameplayTag GetTrashCategory_Implementation() const override;

	/** 품목 보너스는 산재 쓰레기 4종에만 있다 (기획 05 §1.1 S+06~09) */
	virtual FGameplayTag GetSortScoreBonusTag_Implementation() const override { return FGameplayTag(); }
	/** ~Implements IMD_TrashSortable */

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	UPROPERTY(VisibleAnywhere, Category = "MD|Contamination|Debris")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터를 집을 때 사용할 물리 운반 방식 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/**
	 * 보관함이 이 산출물을 식별할 때 사용할 내용물 분류 태그.
	 * 파생 클래스 생성자가 반드시 채운다. 비어 있으면 쓰레기통에 담기지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;
};
