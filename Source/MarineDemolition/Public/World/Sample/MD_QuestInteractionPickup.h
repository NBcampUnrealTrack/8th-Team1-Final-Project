#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_QuestInteractionPickup.generated.h"

class UMD_InteractionPromptComponent;

class UStaticMeshComponent;

/**
 * 상호작용(F키)으로 획득하면 Interaction 페이즈의 진행도를 올리고 사라지는 퀘스트 오브젝트.
 *
 * 기획 03 §5.1의 "B 캐비닛 카드"처럼 핫바·손 슬롯 없이 퀘스트 플래그로만 기능하는 아이템에 사용한다.
 * 물리 운반을 사용하지 않으므로 AMD_QuestItem이 아닌 독립 액터다.
 * 서버에서 Interact가 실행되면 MerchantSubsystem에 Progress를 기록하고 액터를 파괴한다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_QuestInteractionPickup : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_QuestInteractionPickup();

	// --- IMD_Interactable ---
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

	/** 이 오브젝트가 귀속된 메인 퀘스트의 DA 식별자 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	FPrimaryAssetId QuestDefinitionId;

	/** 이 오브젝트가 귀속된 페이즈 인덱스 (DA의 Phases 배열 기준) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	int32 PhaseIndex = 0;

	/** 상호작용 시 HUD에 표시할 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	FText DisplayName;

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
