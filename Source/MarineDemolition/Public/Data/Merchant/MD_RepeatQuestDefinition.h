#pragma once

#include "CoreMinimal.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "GameplayTagContainer.h"
#include "MD_RepeatQuestDefinition.generated.h"

/** 한 장의 반복 퀘스트 안에서 순차 회수 목표와 완료 대사를 정의하는 페이즈 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RepeatQuestPhaseData
{
	GENERATED_BODY()

	/** 비어 있으면 등급을 제한하지 않고 값이 있으면 하나와 일치해야 하는 허용 등급 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FGameplayTagContainer AcceptedGradeTags;

	/** 손상된 회수물도 목표 진행과 납품 대상으로 인정할지 여부 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	bool bAllowDamagedTarget = false;

	/** 현재 목표를 충족하기 위해 필요한 이벤트 누적 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase", meta = (ClampMin = "1", UIMin = "1"))
	int32 RequiredCount = 1;

	/** 현재 페이즈 진행 중 HUD 퀘스트 카드에 표시할 목표 내용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FText HUDDescription;

	/** 현재 페이즈의 회수 목표를 완료했을 때 표시할 대사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FText CompletionDialogue;

public:
	/** 제출 또는 파괴된 샘플이 현재 페이즈의 등급·손상 조건을 충족하는지 판정하는 함수 */
	bool AcceptsSample(const FGameplayTag& GradeTag, bool bIntact) const;
};

/** R01~R08의 회수 대상과 연계 페이즈를 정의하는 반복 퀘스트 원본 자산 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_RepeatQuestDefinition : public UMD_QuestDefinition
{
	GENERATED_BODY()

public:
	/** 하나의 퀘스트 안의 단일 또는 복수 단계 목표와 중간 전환 데이터 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	TArray<FMD_RepeatQuestPhaseData> Phases;

public:
	/** 저장·복제된 PrimaryAssetId로 반복 퀘스트 원본 자산을 동기 로드하는 함수 */
	static UMD_RepeatQuestDefinition* LoadDefinition(const FPrimaryAssetId& DefinitionId);

	/** 상태의 페이즈 인덱스를 검증하고 해당 회수 목표를 반환하는 함수 */
	const FMD_RepeatQuestPhaseData* GetPhase(int32 PhaseIndex) const;

	/** 마지막 페이즈의 목표 수량을 충족하여 상인 보상을 받을 수 있는지 판정하는 함수 */
	bool IsObjectiveComplete(int32 PhaseIndex, int32 CurrentProgress) const;
};
