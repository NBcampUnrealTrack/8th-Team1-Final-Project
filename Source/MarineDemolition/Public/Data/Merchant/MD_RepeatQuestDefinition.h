#pragma once

#include "CoreMinimal.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "GameplayTagContainer.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_RepeatQuestDefinition.generated.h"

/** 한 장의 반복 퀘스트 안에서 순차 회수 목표와 완료 대사를 정의하는 페이즈 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RepeatQuestPhaseData
{
	GENERATED_BODY()

	/** 회수 이벤트와 퀘스트 목표를 연결할 대상 물품 식별자 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FName TargetObjectId;

	/** 비어 있으면 등급을 제한하지 않고 값이 있으면 하나와 일치해야 하는 허용 등급 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FGameplayTagContainer AcceptedGradeTags;

	/** 손상된 회수물도 목표 진행과 납품 대상으로 인정할지 여부 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	bool bAllowDamagedTarget = false;

	/** 현재 목표를 충족하기 위해 필요한 이벤트 누적 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase", meta = (ClampMin = "1", UIMin = "1"))
	int32 RequiredCount = 1;

	/** 회수 시 R04 카드처럼 인벤토리 대신 퀘스트 상태에 기록할 선택적 플래그 식별자 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase|Flag")
	FName GrantedQuestFlagId;

	/** 현재 페이즈의 회수 목표를 완료했을 때 표시할 대사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FText CompletionDialogue;
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
	/** 새 자산의 공통 단일 목표 정보를 기존 FMD_QuestDefinitionData 런타임에 연결하기 위한 호환 변환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FMD_QuestDefinitionData BuildRuntimeDefinition() const;
};
