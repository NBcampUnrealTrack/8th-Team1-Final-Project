#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/DataAsset.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_SiteDefinition.generated.h"

class UMD_LoreCatalog;

/** 작업 현장 1곳의 메타데이터 + 메인/반복 퀘스트 목록. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SiteDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText SiteName;

	/** Day1, Day2, … 순서의 메인 퀘스트. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Main")
	TArray<FMD_QuestDefinitionData> MainQuestLine;

	/** 현장에서 선택할 MD_RepeatQuestDefinition 에셋의 PrimaryAssetId 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat", meta = (AllowedTypes = "Quest"))
	TArray<FPrimaryAssetId> RepeatQuestPool;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MinRepeatCount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MaxRepeatCount = 3;

	// 현장별 고정 로어 목록과 코덱스 표시 순서를 제공하는 카탈로그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Lore")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;
};
