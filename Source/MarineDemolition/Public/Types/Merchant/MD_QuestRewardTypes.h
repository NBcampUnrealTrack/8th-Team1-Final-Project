#pragma once

#include "CoreMinimal.h"
#include "MD_QuestRewardTypes.generated.h"

/** 회수 퀘스트 정산에 사용하는 크레딧·점수 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_QuestRewardData
{
	GENERATED_BODY()

public:
	/** 퀘스트 정산에서 지급하거나 차감할 크레딧 크기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward", meta = (ClampMin = "0", UIMin = "0"))
	int32 Credits = 0;

	/** 퀘스트 정산에서 지급하거나 차감할 점수 크기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward", meta = (ClampMin = "0", UIMin = "0"))
	int32 Score = 0;
};
