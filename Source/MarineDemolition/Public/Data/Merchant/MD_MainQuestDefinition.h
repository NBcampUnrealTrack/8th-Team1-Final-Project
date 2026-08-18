#pragma once

#include "CoreMinimal.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "GameplayTagContainer.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_MainQuestDefinition.generated.h"

class UWorld;

/** 일차별 고정 목표와 실행 맵을 정의하는 메인 퀘스트 원본 자산 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_MainQuestDefinition : public UMD_QuestDefinition
{
	GENERATED_BODY()

public:
	UMD_MainQuestDefinition();

	/** HUD와 상인 메뉴의 메인 퀘스트 상세 영역에 표시할 목표 설명 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	FText HUDDescription;

	/** 납품·청소·마감 등 메인 퀘스트 목표 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	EMD_QuestObjectiveType ObjectiveType = EMD_QuestObjectiveType::RecoverIntact;

	/** 회수 목표가 인정하는 샘플 등급 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	FGameplayTag TargetGradeTag;

	/** 목표를 완료하기 위해 필요한 이벤트 누적 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective", meta = (ClampMin = "1", UIMin = "1"))
	int32 RequiredCount = 1;

	/** 이 퀘스트가 속한 원정 맵 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	TSoftObjectPtr<UWorld> QuestMap;

public:
	/** 저장·복제된 PrimaryAssetId로 메인 퀘스트 원본 자산을 동기 로드하는 함수 */
	static UMD_MainQuestDefinition* LoadDefinition(const FPrimaryAssetId& DefinitionId);

	/** 필수 식별자와 목표 수량이 런타임에서 사용할 수 있는지 확인하는 함수 */
	bool IsValidDefinition() const;

	/** 제출 샘플이 이 메인 퀘스트의 등급·손상 조건을 충족하는지 판정하는 함수 */
	bool AcceptsSample(const FGameplayTag& GradeTag, bool bIntact) const;

	/** 파괴 샘플의 등급이 이 메인 퀘스트의 회수 목표와 일치하는지 판정하는 함수 */
	bool TargetsGrade(const FGameplayTag& GradeTag) const;
};
