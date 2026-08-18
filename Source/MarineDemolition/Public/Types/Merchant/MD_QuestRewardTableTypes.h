#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/Merchant/MD_QuestRewardTypes.h"
#include "MD_QuestRewardTableTypes.generated.h"

/** 회수 대상 샘플 등급별 성공 및 실패 정산값을 제공하는 DataTable 행 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_QuestSampleGradeRewardRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 행 이름과 무관하게 보상 조회에 사용할 Resource.Sample.Grade 하위 등급 태그 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward", meta = (Categories = "Resource.Sample.Grade"))
	FGameplayTag GradeTag;

	/** 요구 등급의 샘플을 정상적으로 회수하고 납품했을 때 지급할 보상 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData SuccessReward;

	/** 계약 종료까지 샘플을 회수하지 못했을 때 차감할 양수 크기의 실패 보상 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData UnrecoveredFailureReward;

	/** 회수 대상 샘플이 파괴되었을 때 차감할 양수 크기의 실패 보상 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData DestroyedFailureReward;
};
