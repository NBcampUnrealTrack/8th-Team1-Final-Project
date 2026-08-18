#pragma once

#include "CoreMinimal.h"
#include "MD_QuestRewardTypes.generated.h"

/** 회수 퀘스트 정산에 사용하는 크레딧·점수·호감도 데이터 */
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

	/** 퀘스트 정산에서 증가하거나 감소할 호감도 크기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float Affinity = 0.1f;
};
