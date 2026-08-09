#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_SiteDefinition.generated.h"

/** 작업 현장 1곳의 메타데이터 + 메인/반복 퀘스트 목록. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SiteDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText SiteName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText ClientName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	int32 ContractDayCount = 3;

	/** Day1, Day2, … 순서의 메인 퀘스트. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Main")
	TArray<FMD_QuestDefinitionData> MainQuestLine;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	TArray<FMD_QuestDefinitionData> RepeatQuestPool;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MinRepeatCount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MaxRepeatCount = 3;
};
