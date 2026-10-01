#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/DataAsset.h"
#include "MD_SiteDefinition.generated.h"

class UMD_LoreCatalog;
class UWorld;

/** 작업 현장의 원정 맵과 로어 카탈로그 정의 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SiteDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText SiteName;

	/** 현장 계약과 일차 진행에 사용하는 원정 맵 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	TSoftObjectPtr<UWorld> QuestMap;




	// 현장별 고정 로어 목록과 코덱스 표시 순서를 제공하는 카탈로그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Lore")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;
};
